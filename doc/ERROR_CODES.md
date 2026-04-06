# CZUR Open SDK Error Codes

[中文版本](./ERROR_CODES_ZH.md)

## Overview

This document defines the public error-code contract for `sdk_open` command APIs, events, and related admin observability APIs.

It is the stable external reference for integrators. The implementation may evolve internally, but the public meaning of these codes should remain stable.

Current note:

- The current `sdk_open` runtime implements only a subset of the full protocol surface.
- This document describes the full planned public error-code model for the open SDK.
- Methods or domains that are not implemented yet may still reserve their code range and semantic meaning.

## Response Shape

Command responses and error responses use the following JSON shape:

```json
{
  "request_id": "req-001",
  "code": 1101,
  "message": "api key invalid",
  "data": {},
  "error": {
    "name": "API_KEY_INVALID",
    "details": {
      "reason": "signature_mismatch"
    }
  },
  "trace_id": "trace-001",
  "ts": 1710000000
}
```

Field notes:

- `code`: stable numeric error code.
- `message`: short human-readable description.
- `error.name`: stable symbolic error name when available.
- `error.details`: optional diagnostic details.
- `trace_id`: server-side trace identifier for support and log lookup.
- `ts`: server-side Unix timestamp.

## Access Model

`sdk_open` exposes two access surfaces:

- SDK business APIs
- admin observability APIs

Validation rules:

- SDK business APIs use SDK auth semantics such as `api_key`, `session_token`, `auth_scope`, and capability checks.
- Admin observability APIs do not use `api_key` or `session_token`.
- Minimal health endpoints may be anonymous.
- Protected admin APIs should use `Authorization: Bearer <auth_token>`.

The error codes in this document primarily target the SDK business protocol. Protected admin APIs may additionally return HTTP `401 Unauthorized` at the transport layer.

## Code Ranges

| Range | Category | Meaning |
|---|---|---|
| `0` | Success | Request completed successfully |
| `1000-1099` | Generic request errors | Protocol format, method, params, timeout, rate limit |
| `1100-1199` | Auth and authorization | API key, session, account tier, capability, device scope |
| `1200-1299` | Device errors | Device discovery, open, busy, disconnect, model support |
| `1300-1399` | Capture and video | Stream, capture, preview, scan-area, turn detection |
| `1400-1499` | Image processing | Image input, processing, normalization, watermark |
| `1500-1599` | OCR and recognition | OCR, language support, barcode decoding |
| `1600-1699` | File and OFD | File lookup, conversion, upload, print, metadata |
| `1700-1799` | SANE | Third-party scanner errors |
| `1900-1999` | Internal system errors | Provider state, internal failure, service unavailability |

## Success

| Code | Name | Description |
|---:|---|---|
| `0` | `OK` | Success |

## Generic Request Errors

| Code | Name | Description |
|---:|---|---|
| `1000` | `INVALID_REQUEST` | Request body is malformed |
| `1001` | `INVALID_METHOD` | `method` is missing or invalid |
| `1002` | `INVALID_PARAMS` | Params are missing, incomplete, or type-mismatched |
| `1003` | `UNSUPPORTED_METHOD` | Method is not exposed or not implemented |
| `1004` | `REQUEST_TIMEOUT` | Request timed out |
| `1005` | `RATE_LIMITED` | Request was rate-limited |
| `1006` | `TEXT_MESSAGE_REQUIRED` | Command channel requires a text message |
| `1007` | `BINARY_MESSAGE_REQUIRED` | Video channel requires a binary message |

## Auth and Authorization Errors

| Code | Name | Description |
|---:|---|---|
| `1100` | `AUTH_REQUIRED` | Authentication is required for this method |
| `1101` | `API_KEY_INVALID` | API key is invalid or cannot be parsed |
| `1102` | `API_KEY_EXPIRED` | API key has expired |
| `1103` | `SESSION_TOKEN_INVALID` | Session token is invalid or expired |
| `1104` | `ACCOUNT_TYPE_NOT_ALLOWED` | Account tier is not allowed to use this method |
| `1105` | `DEVICE_NOT_IN_AUTH_SCOPE` | Target device is outside the authorized device scope |
| `1106` | `AUTH_SCENE_MISMATCH` | Auth scene does not match SDK expectations |
| `1107` | `CAPABILITY_NOT_GRANTED` | The account is not granted this method or capability |

## Device Errors

| Code | Name | Description |
|---:|---|---|
| `1200` | `DEVICE_NOT_FOUND` | Target device was not found |
| `1201` | `DEVICE_BUSY` | Device is already in use |
| `1202` | `DEVICE_OPEN_FAILED` | Failed to open the device |
| `1203` | `DEVICE_NOT_OPEN` | Device is not open |
| `1204` | `DEVICE_MODEL_UNSUPPORTED` | Current device model does not support this method |
| `1205` | `DEVICE_DISCONNECTED` | Device disconnected during execution |

## Capture and Video Errors

| Code | Name | Description |
|---:|---|---|
| `1300` | `STREAM_NOT_READY` | Stream is not ready |
| `1301` | `CAPTURE_FAILED` | Capture failed |
| `1302` | `PREVIEW_RESOLUTION_UNSUPPORTED` | Preview resolution is not supported |
| `1303` | `TURN_DETECT_NOT_SUPPORTED` | Turn-detection is not supported on this device |
| `1304` | `SCAN_AREA_INVALID` | Scan area or paper-region parameters are invalid |

## Image Processing Errors

| Code | Name | Description |
|---:|---|---|
| `1400` | `IMAGE_INPUT_INVALID` | Input image is invalid |
| `1401` | `IMAGE_PROCESS_FAILED` | Image processing failed |
| `1402` | `WATERMARK_INVALID` | Watermark parameters are invalid |
| `1403` | `NORMALIZATION_FAILED` | Image normalization failed |
| `1404` | `IMAGE_OPERATION_UNSUPPORTED` | The provider does not support this image operation |

## OCR and Recognition Errors

| Code | Name | Description |
|---:|---|---|
| `1500` | `OCR_FAILED` | OCR execution failed |
| `1501` | `OCR_LANGUAGE_UNSUPPORTED` | OCR language is not supported |
| `1502` | `BARCODE_DECODE_FAILED` | Barcode or QR decode failed |
| `1503` | `REALTIME_BARCODE_NOT_SUPPORTED` | Real-time barcode recognition is not supported on this device |

## File and OFD Errors

| Code | Name | Description |
|---:|---|---|
| `1600` | `FILE_NOT_FOUND` | File does not exist |
| `1601` | `FILE_CONVERT_FAILED` | File conversion failed |
| `1602` | `OFD_PROCESS_FAILED` | OFD processing failed |
| `1603` | `UPLOAD_FAILED` | Upload failed |
| `1604` | `PRINT_FAILED` | Print failed |
| `1605` | `FILE_METADATA_INVALID` | File metadata is invalid |

## SANE Errors

| Code | Name | Description |
|---:|---|---|
| `1700` | `SANE_NOT_AVAILABLE` | SANE capability is not available on the host |
| `1701` | `SANE_DEVICE_NOT_FOUND` | Target SANE device was not found |
| `1702` | `SANE_OPTION_INVALID` | SANE option is invalid |
| `1703` | `SANE_SCAN_FAILED` | SANE scan failed |
| `1704` | `SANE_SCAN_CANCELED` | SANE scan was canceled |

## Internal System Errors

| Code | Name | Description |
|---:|---|---|
| `1900` | `INTERNAL_ERROR` | Unclassified internal error |
| `1901` | `PROVIDER_NOT_READY` | Provider has not been initialized or attached |
| `1902` | `PROVIDER_CALL_FAILED` | Provider call failed |
| `1903` | `SERVICE_UNAVAILABLE` | Service is temporarily unavailable |

## Compatibility Notes

Current runtime compatibility notes:

- Legacy `id` may still be accepted and mapped to `request_id`.
- Earlier runtime stubs may return transitional codes such as `400` or `404`.
- Public integrations should treat the codes in this document as the target stable contract.

## Documentation Links

- Chinese version: [ERROR_CODES_ZH.md](./ERROR_CODES_ZH.md)
- Main project guide: [../README.md](../README.md)
