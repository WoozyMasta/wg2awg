<!-- markdownlint-disable MD024 -->
# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog][], and this project adheres to
[Semantic Versioning][].

## [0.3.0] - 2026-06-15

### Added

* Add `AWG_REMOTE_SILENT_EXIT_TIMEOUT` (default `900`, `0` disables)
  to exit non-zero after prolonged one-sided remote silence
  so the container runtime can restart the service.
* Log a `WARN` line once one-sided remote silence (client active, remote silent)
  reaches half of the reconnect timeout (`keepalive * 2` by default),
  making WAN/PPPoE reconnect path loss visible in logs before reconnect fires.

### Changed

* `AWG_REMOTE_SILENT_TIMEOUT` now defaults to `auto`:
  derived from the peer `PersistentKeepalive` as `keepalive * 4`,
  with a lower bound of `30` and, when `AWG_REMOTE_SILENT_EXIT_TIMEOUT`
  is enabled, an upper bound of half that value (fallback `60`),
  down from the previous fixed `300`. An explicit value still overrides.

[0.3.0]: https://github.com/WoozyMasta/wg2awg/compare/0.2.1...0.3.0

## [0.2.1] - 2026-06-11

### Added

* Add CLI flags for mode, listen, remote, and source port overrides.

[0.2.1]: https://github.com/WoozyMasta/wg2awg/compare/0.2.0...0.2.1

## [0.2.0] - 2026-06-08

### Added

* Add `AWG_DNS_RESOLVE_FAILURE_TIMEOUT` to exit after prolonged consecutive
  DNS resolve failures so container runtime can restart the service.

### Changed

* Log `getaddrinfo()` failure reasons and DNS resolve failure timeout details.

[0.2.0]: https://github.com/WoozyMasta/wg2awg/compare/0.1.0...0.2.0

## [0.1.0] - 2026-05-24

### Added

* First initial release.

[0.1.0]: https://github.com/WoozyMasta/wg2awg/tree/0.1.0

<!--links-->
[Keep a Changelog]: https://keepachangelog.com/en/1.1.0/
[Semantic Versioning]: https://semver.org/spec/v2.0.0.html
