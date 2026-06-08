<!-- markdownlint-disable MD024 -->
# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog][], and this project adheres to
[Semantic Versioning][].

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
