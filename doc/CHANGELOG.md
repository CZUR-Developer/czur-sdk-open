# Changelog

[中文版本](./CHANGELOG_ZH.md)

All notable changes to the CZUR Open SDK are documented in this file.

## [0.0.1] - 2026-06-17

### Added

- Introduced the standalone `sdk_open_app` runtime for CZUR Open SDK.
- Added the four-layer runtime structure covering transport, application, facade, and provider interface boundaries.
- Added local HTTP services for admin, demo, and asset access.
- Added command WebSocket support for system, auth, device, video, capture, image, and file conversion commands.
- Added video WebSocket support for preview frame streaming and related video events.
- Added API Key based session creation, session refresh, session destruction, auth context lookup, and offline API Key activation flow.
- Added quota-controlled business methods for capture, image processing, page processing, color mode application, and file conversion.
- Added public provider interfaces and DTOs for device, graphic, image enhancement, OCR, OFD, recognition, SANE, and authorization capabilities.
- Added a mock provider bundle so the runtime can be built and exercised without exposing private capability-library types.
- Added bundled admin and demo web sites for local runtime management and SDK workflow verification.
- Added public documentation for runtime architecture, command channel flow, and SDK error codes.

### Platform

- Linux is the first implemented platform.
- Windows and macOS support are planned for later versions.
