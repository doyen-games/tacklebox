# TackleBox security design

This document explains what protects what, and where the honest limits are.

## The vault file

Everything sensitive or integrity-critical is one file: `vault.tbx` in the
platform data directory (`%APPDATA%\TackleBox`, `~/Library/Application
Support/TackleBox`, `$XDG_DATA_HOME/tacklebox`).

Contents: private keys, account list, **network endpoints**, **whitelist
rules**, security preferences, and the signing audit log.

Endpoints and rules are deliberately inside the ciphertext. An attacker who can
edit a plaintext config could otherwise:

- point the wallet at a malicious API node (phishing balances, poisoned ABIs
  that mis-decode what you are signing), or
- whitelist their own drain transaction for silent signing.

With TackleBox, changing either requires the unlocked vault.

### Cryptography

```
password --scrypt(N=2^15, r=8, p=1, 32-byte salt)--> 64-byte key block
                     [0..32) AES-256 key    [32..64) HMAC-SHA256 key

seal:  AES-256-CBC(random IV, PKCS7)  then  HMAC-SHA256(aad || iv || ct)
aad:   "TBX|v1|scrypt|logN,r,p|salt-hex"   (binds header to ciphertext)
```

- Encrypt-then-MAC; the MAC is checked in constant time **before** any
  decryption or padding inspection, so tampered files and wrong passwords are
  rejected identically without touching the AES layer (no padding oracle).
- KDF parameters live in the header and are validated against sane bounds on
  load, so they can be raised in future versions without breaking old vaults.
- scrypt is implemented against RFC 7914 and verified by its published test
  vectors in the test suite; PBKDF2-HMAC-SHA256 likewise. AES and HMAC come
  from the trezor-crypto library vendored (and already trusted) by dwarfkit.
- Fresh random IV and salt from the OS CSPRNG (BCryptGenRandom /
  arc4random_buf / /dev/urandom). The app aborts rather than run without an
  entropy source.
- Writes are atomic (temp file + rename) and keep a `.bak` of the previous
  vault, so a crash mid-save cannot destroy the only copy.

### Memory hygiene

- Passwords and derived keys travel in `SecureBytes`: zeroized on destruction
  (compiler-proof memzero), best-effort locked out of swap
  (VirtualLock/mlock).
- Locking the vault wipes key strings in place before releasing them.
- Honest limit: while the vault is unlocked, decrypted key material exists in
  regular heap structures during JSON (de)serialization and inside dwarfkit
  types during signing. A debugger or memory-dumping malware with user-level
  access on an unlocked machine wins - the same is true of every software
  wallet. The mitigations shrink the window; they do not eliminate it.

## The guard

Every signature - transfers, contract actions, pasted ESR requests - flows
through one choke point (`Controller::guardedSign`, invoked by the wallet
plugin inside dwarfkit's transact pipeline). There is no code path that signs
without it.

1. Actions are decoded (via the chain ABI) to structured JSON.
2. For every touched contract the wallet fetches `get_raw_abi` **live** -
   `code_hash` and `abi_hash` are compared against any pinned rule. Cache TTL
   is zero for this check by design: a pin is only as good as the freshness of
   what it is compared against.
3. Whitelist evaluation:
   - identity match: chain, signer (`actor@permission`, wildcards allowed),
     contract (never wildcarded), action (wildcardable);
   - parameter constraints per field path: exact / one-of / range (numeric or
     asset with mandatory symbol+precision match) / any; optional strict mode
     rejects fields the rule does not list;
   - a matching rule whose pin no longer matches yields **STALE**: the rule is
     persisted as suspended immediately, and stays suspended even if hashes
     later match again, until a human re-approves (re-pinning the new hashes).
4. Risk analysis (independent of the whitelist): permission surgery
   (`updateauth`/`deleteauth`/`linkauth`/`unlinkauth`), code deploys
   (`setcode`/`setabi`), transfers over 50%/90% of the known balance,
   link/claim-style memo patterns, msig operations, first contact with unknown
   contracts.
5. Outcome:
   - **Auto-sign** only when: every action matches an auto-sign rule, every
     matching rule is pinned, all hashes were fetched fresh and match, the
     master switch in Settings is on, and there are no critical risk flags.
     Auto-signs are recorded in the audit log and announced with a toast.
   - Otherwise the signing review modal shows decoded parameters, per-action
     verdict chips, hash verification status, risk flags and an expiration
     countdown. Critical risk or a stale pin replaces the sign button with
     hold-to-sign. An optional setting requires the vault password per
     signature.
6. The decision (either way) is appended to the audit log inside the vault.

### Why hash pinning matters

Antelope contracts are mutable: `setcode` swaps the WASM under the same account
name. A dapp you trusted yesterday can be hostile after an upgrade (compromised
developer key, rug). Pinning freezes trust to the exact code+ABI you reviewed;
any redeploy - benign or not - drops the rule out of the fast path until you
look again. The suspended-rule banner shows the pinned vs observed hashes.

## Network trust

- Endpoints must be https (plain http only for localhost development). This
  applies to every pool: RPC, Atomic Assets, Hyperion and Light API nodes.
- Every RPC health probe verifies the endpoint serves the **configured chain
  id**; a mismatched or impostor endpoint is flagged red in Settings. Other
  node types are probed against their own service endpoints (`/v2/health`,
  `/health`, `/api/networks`).
- Round-robin and auto modes spread *queries* across the enabled pool - they
  widen how many operators see your read traffic, which is why they are
  opt-in per pool and default to priority mode. Signing never depends on the
  selection mode: a transaction is built and broadcast through one node, and
  the guard's hash pins are verified with a fresh fetch regardless of which
  node answered.
- The price oracle is **display-only by design**: prices render on the
  dashboard and nowhere else - the guard, risk analyzer, autopilot amounts
  and signing paths never read them, so a lying oracle can mislead your eyes
  but cannot move funds or widen a whitelist. Oracle endpoints are https-only
  and user-selected (Alcor/CoinGecko), or read on-chain through your own RPC
  pool (Delphi). Custom chains and testnets default to no oracle at all.
- A malicious RPC node still cannot steal keys or forge your signature; the
  worst it can do is lie about state and serve wrong ABIs. Wrong ABIs would
  change how action data *decodes for display*; the whitelist's ABI-hash pin
  also closes this hole for pinned contracts, because the served ABI must hash
  to what you approved.
- NFT media is fetched https-only, size-capped, decoded off-thread by
  stb_image from memory, and never executed - a hostile image can at worst
  fail to decode.

## Autopilot (scheduled transactions)

A schedule is a clock, never a fourth way to sign. Scheduled executions run
inside a thread-local "scheduled" context that forbids the guard from
prompting: the only path to a signature is the auto-sign fast path, which
already demands a pinned auto-sign rule matching the exact action, the
auto-sign master switch, fresh on-chain hash verification, and zero critical
risk flags. Anything else records the run as blocked (visible in the schedule
card, the sidebar pip and the audit log) and signs nothing. Schedules run only
while the app is open and the vault unlocked - there is deliberately no
background daemon holding keys.

Dynamic amounts (Y% of the live balance) change what a run sends but not who
guards it: the computed quantity still passes the whitelist, so the sane
setup for an auto-stack is an Exact `to` (destination-locked - even a 100%
sweep can only reach your own chosen address) plus, optionally, a Range cap
on the quantity. The percent math is exact integer basis points with floor
rounding and an optional untouched reserve; drafted rules leave run-time
fields (the dynamic amount, {placeholder} strings) unconstrained rather than
pinning values that change every run.

## Dapp links (experimental)

An anchor-link session gives a dapp a push channel into the wallet, not a
signature. Connecting requires an explicit confirmation; the session gets its
own random request key (stored in the vault, wiped on lock) and buoy channel.
Pushed requests are unsealed with ECDH (session key x dapp key), then routed
through the identical guard + signing review as a hand-pasted request. The
identity proof signed at login authorizes nothing on-chain. Unlinking removes
the key and stops the listener; listeners also stop the moment the vault
locks.

## Application hardening

- Auto-lock after configurable inactivity (never yanks the vault mid-review);
  Ctrl+Shift+L panic-locks instantly.
- Clipboard copies of secrets auto-clear after a configurable delay, and only
  if the clipboard still holds what the wallet put there.
- Key reveal and vault import/export re-verify the password against the
  on-disk envelope, not in-memory state.
- Deleting a key requires a hold-to-confirm; affected accounts degrade to
  watch-only rather than disappearing.
- The audit log lives inside the vault: it survives restarts and cannot be
  edited without the password.
- No telemetry, no update pings, no price feeds. The process talks exclusively
  to the endpoints in your vault.

## Reporting

This is young software. If you find a vulnerability, open a private report
rather than a public issue.
