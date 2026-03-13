# Third-Party Dependencies

This directory vendors fixed-source snapshots for `sdk_open` runtime networking.

## Included Libraries

1. `cpp-httplib`
- Upstream: https://github.com/yhirose/cpp-httplib
- Version tag: `v0.18.6`
- Commit: `eb30f15363fd418103d8cf1dd64e206f51e5cbc8`
- License: MIT

2. `websocketpp`
- Upstream: https://github.com/zaphoyd/websocketpp
- Version tag: `0.8.2`
- Commit: `56123c87598f8b1dd471be83ca841ceae07f95ba`
- License: BSD-3-Clause

3. `asio` (standalone)
- Upstream: https://github.com/chriskohlhoff/asio
- Version tag: `asio-1-30-2`
- Commit: `12e0ce9e0500bf0f247dbd1ae894272656456079`
- License: Boost Software License 1.0

## Notes

- The `.git` metadata from upstream repos is removed intentionally.
- Dependency updates must be explicit and reviewed as normal source changes.
