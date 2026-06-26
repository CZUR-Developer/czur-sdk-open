# Changelog

[中文版本](./CHANGELOG_ZH.md)

All notable changes to the CZUR Open SDK are documented in this file.

## [0.0.2] - 2026-06-26

### Added

- Added Windows Service mode for `sdk_open_app.exe` while keeping console startup for debugging.
- Added device action and removal support for capture workflows, including `capture.set_turn_detect`, `capture.turn_detected`, `capture.hardgrab_detected`, and `device.removed`.
- Added OCR downloadable task assets and runtime version metadata.

### Changed

- `ocr.recognize` can now use SDK-managed output paths when `output_path` / `output_dir` is omitted, supports `output_format` as a `format` alias, and returns downloadable `task.assets`.
- `video.set_profile` now accepts both nested and inline profile payloads; `capture.get` now returns a normalized task payload.
- Improved auth context responses and configurable demo command/video WebSocket endpoints.

### Fixed

- Fixed local authorization, offline activation, auth context serialization, and session storage stability.
- Fixed device video start/stop, video format setting, device close cleanup, and capture task exception handling.
- Fixed OCR export/download responses, demo routing and runtime event display, SANE unavailable responses, and Windows build/private Provider compatibility.

### Compatibility Notes

- New command method: `capture.set_turn_detect`.
- New command events: `capture.turn_detected`, `capture.hardgrab_detected`, and `device.removed`.
- New device action/event Provider APIs are optional; default implementations preserve compatibility for existing Providers.

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
