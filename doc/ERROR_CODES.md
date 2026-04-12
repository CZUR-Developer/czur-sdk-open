# CZUR Open SDK Error Codes

[中文版本](./ERROR_CODES_ZH.md)

## Overview

This document defines the currently public and stable error-code semantics used by `sdk_open`.

Scope:

- command WebSocket responses
- error states in command/video related events
- unified business error mapping

Notes:

- admin APIs may additionally return standard HTTP status codes
- business error codes are defined by `src/sdk_open/interfaces/sdk_status_code.h`

## Response Shape

```json
{
  "request_id": "req-001",
  "code": 1101,
  "message": "token invalid",
  "data": {},
  "ts": 1710000000
}
```

Field notes:

- `request_id`: request identifier
- `code`: stable error code
- `message`: short error description
- `data`: optional extended data
- `ts`: server timestamp

## Access Surfaces

`sdk_open` exposes two access surfaces:

- SDK business protocol
- admin observability APIs

Rules:

- the SDK business protocol uses a `token -> session_token -> connection-bound session` model
- admin APIs do not participate in business capability checks
- `GET /healthz` is anonymous
- `GET /api/status` uses `Authorization: Bearer <auth_token>`

## Error-Code Groups

| Range | Category | Meaning |
|---|---|---|
| `0` | Success | Request completed successfully |
| `1000-1099` | Request protocol errors | Request shape, method, params, rate limiting |
| `1100-1199` | Auth and authorization | token, session, capability, device scope |
| `1300-1399` | Stream errors | stream lifecycle related |
| `1900-1999` | Internal runtime errors | provider and internal failures |

## Currently Defined Codes

| Code | Name | Description |
|---:|---|---|
| `0` | `OK` | Success |
| `1000` | `INVALID_REQUEST` | Request body is malformed |
| `1001` | `INVALID_METHOD` | `method` is missing or invalid |
| `1002` | `INVALID_PARAMS` | `params` are missing, incomplete, or type-mismatched |
| `1003` | `UNSUPPORTED_METHOD` | Method is not public or not implemented |
| `1004` | `RATE_LIMITED` | Request hit a rate-limit or concurrency guard |
| `1100` | `AUTH_REQUIRED` | The method requires a valid bound session |
| `1101` | `TOKEN_INVALID` | `token` is invalid or cannot be parsed |
| `1102` | `TOKEN_EXPIRED` | `token` has expired |
| `1103` | `SESSION_TOKEN_INVALID` | The bound `session_token` is invalid or expired |
| `1104` | `ACCOUNT_TYPE_NOT_ALLOWED` | The account tier is not allowed to use the capability |
| `1105` | `DEVICE_NOT_IN_AUTH_SCOPE` | The target device is outside the authorized device scope |
| `1106` | `AUTH_SCENE_MISMATCH` | The auth scene does not satisfy SDK requirements |
| `1107` | `CAPABILITY_NOT_ALLOWED` | The current session is not granted the target method capability |
| `1300` | `STREAM_NOT_FOUND` | `stream_id` does not exist, is already closed, or does not belong to the session |
| `1900` | `INTERNAL_ERROR` | Unclassified internal error |
| `1901` | `PROVIDER_NOT_READY` | Provider is not attached or not initialized |
| `1902` | `PROVIDER_CALL_FAILED` | Provider call failed |

## Admin HTTP APIs

Admin APIs also use standard HTTP status codes:

| HTTP Code | Meaning |
|---:|---|
| `200` | Request succeeded |
| `401` | `Authorization` is missing or invalid |
| `404` | Route not found |

## Integration Guidance

- Integrators should branch on `code` first
- `message` is suitable for logs and diagnostics, not as the only decision key
- both command WS and video WS should treat `1100-1107` as business auth failures

## Documentation Links

- Target architecture: [RUNTIME_ARCHITECTURE_ZH.md](./RUNTIME_ARCHITECTURE_ZH.md)
- Command channel: [COMMAND_CHANNEL_FLOW.md](./COMMAND_CHANNEL_FLOW.md)
- Main README: [../README.md](../README.md)
