# Security Policy

## Supported versions

Dwarfkit is pre-1.0. Only the latest commit on `main` receives security
fixes.

## Reporting a vulnerability

Please do not report security issues through public GitHub issues.

Use GitHub's private vulnerability reporting instead: open the repository's
**Security** tab and choose **Report a vulnerability**, or go directly to
<https://github.com/on-a-t-break/dwarfkit/security/advisories/new>.

You should receive a response within a few days. Please include a minimal
reproduction where possible.

## Scope

Dwarfkit handles private keys, signing, and transaction encoding for Antelope
blockchains. Reports are especially welcome for:

- Key material handling: `PrivateKey`, shared secrets, sealed messages,
  memory left unzeroized after use
- Signature or digest construction that could sign unintended data
- ABI serializer parsing of untrusted input (malformed binary, deep nesting,
  integer overflow in length prefixes)
- Transport handling of untrusted responses (API client, websocket buoy
  listener, p2p envelope framing)

Bugs in behavior faithfully ported from upstream Wharfkit may also need an
upstream report; we will help coordinate that.

## Disclosure

Please give us a reasonable window to ship a fix before public disclosure.
Credit is given in the advisory unless you prefer otherwise.
