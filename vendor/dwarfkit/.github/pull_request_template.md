# Summary

<!-- What does this change and why? For ports and parity fixes, link the
upstream file in reference/ (or the Wharfkit repo) this is checked against. -->

## Checklist

- [ ] Behavior matches the pinned upstream source in `reference/` (or the
      difference is recorded in DIVERGENCES.md with a one-line reason)
- [ ] Tests cover the change; upstream test cases and fixtures are ported
      where they exist
- [ ] `cmake --build build --config Debug` and
      `ctest --test-dir build -C Debug` are green
- [ ] PROGRESS.md / PORT_MANIFEST.md updated if a module's status changed
- [ ] Style follows CONTRIBUTING.md (C++20 subset, no exceptions in the
      public API, no em dashes anywhere)
