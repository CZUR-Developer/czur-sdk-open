# Changelog

[中文版本](./CHANGELOG_ZH.md)

All notable changes to the CZUR Open SDK are documented in this file.

## [0.0.4] - 2026-07-24

### Added

- Added Windows runtime and package support for 32-bit builds.
- Added `storage.cleanup_temp` to clean completed SDK-managed temporary tasks and cache files; cleanup is rejected while tasks are active.
- Added `profile.output.quality` and `profile.output.target_size` for capture profiles, allowing callers to request JPG output quality and target capture output size buckets.
- `device.open` responses now include `capture_output.target_sizes`, listing target output resolution options supported by the opened device.
- Updated the demo with capture output controls and a temporary-storage cleanup verification page.

### Fixed

- Fixed the OCR demo default output path, Linux cleanup-directory compatibility, and package validation issues.

### Compatibility Notes

- New command method: `storage.cleanup_temp`.
- New optional capture profile fields: `output.quality` and `output.target_size`.
- New optional `device.open` response field: `capture_output`.

## [0.0.3] - 2026-07-10

### Added

- Added the `device.added` device-attach event so the demo can refresh the authorized device list after hot-plug changes.
- Hard-grab events can now create asynchronous capture tasks and reuse the current preview/capture profile plus image-enhancement workflow.
- Auth context responses now include more state information for client-side entitlement, licensed tier, and trial-state display.

### Changed

- Improved device removal events: opened or previewing devices report the exact `device_id`, while unopened devices only report an inventory change to avoid exposing unauthorized device details.
- `ocr.recognize` now uses an isolated temporary directory for Linux PDF/OFD quality handling, while OCR still runs on the original input images to avoid recognition loss.
- Clarified that `file.convert.options.quality` remains the public quality parameter for image-layer and rendered-page outputs.
- Improved API Key validated/trial state handling and local activation recovery so capability checks are more stable across authorization modes.

### Fixed

- Fixed authorization state being overwritten by defaults, preventing mismatches between displayed state and capability checks.
- Fixed local offline activation restore behavior, improving authorization-state stability after runtime restart.
- Fixed Windows runtime loading for the private auth DLL and private Provider dependency copying.
- Fixed cleanup and error semantics when device close, video stop, and device removal happen together, reducing stale preview state, pending captures, and duplicate events.

### Compatibility Notes

- New command event: `device.added`.
- Auth context responses include additional state fields; legacy offline keys and existing local quota behavior remain compatible.

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
