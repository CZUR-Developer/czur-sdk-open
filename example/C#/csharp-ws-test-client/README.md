
### Overview

This is a standalone .NET 8 WinForms test client for the `sdk_open` WebSocket SDK. It only connects to an already running `sdk_open_app`; it never starts, stops, installs, or modifies the C++ runtime.

### Basic build environment

- Windows 10/11 x64. Run the demo under the same Windows user that owns the DPAPI-protected API token.
- Visual Studio 2022 17.x with the **.NET desktop development** workload.
- .NET 8 SDK (latest 8.0.x is recommended) and the .NET 8 Windows Desktop Runtime.
- Access to a NuGet feed. The first restore downloads `System.Security.Cryptography.ProtectedData` and other runtime dependencies.
- A running `sdk_open_app` instance with reachable health, command, video, and asset endpoints.
- The project targets `net8.0` for the Core library and `net8.0-windows` for WinForms. A C++ compiler is not required to build this C# test client.

### Run from Visual Studio or PowerShell

Open `SdkOpen.CSharpTestClient.sln` in Visual Studio 2022 and set `SdkOpen.TestClient.WinForms` as the startup project:

```powershell
# Run from the directory that contains this README (the project root).
dotnet restore SdkOpen.CSharpTestClient.sln --configfile NuGet.Config
dotnet run --project src\SdkOpen.TestClient.WinForms\SdkOpen.TestClient.WinForms.csproj --no-restore
```

Default endpoints are health `http://127.0.0.1:17080/healthz`, command `ws://127.0.0.1:17090`, video `ws://127.0.0.1:17091`, and asset `http://127.0.0.1:17082`. The default TLS ports are `18090`, `18091`, and `18082`.

### Authorization and local configuration locations

- After **Save and Connect**, the API token is encrypted with Windows DPAPI using the `CurrentUser` scope and stored at `%LOCALAPPDATA%\CZUR\SdkOpenTestClient\secrets\<alias>.bin`. The file does not contain the plaintext token and can only be decrypted by the same Windows user; it cannot be copied directly to another user profile or runtime environment.
- Endpoint values, the profile name, and the API token alias are stored in `%LOCALAPPDATA%\CZUR\SdkOpenTestClient\profile.json`. This file contains connection settings and the alias only, never the plaintext API token. To reset the local settings, close the client and delete that directory; the next launch restores the default endpoints.
- `session_token` is the short-lived credential returned by `auth.create_session`. It exists only in the current process memory and is not written to disk after disconnect or application exit.
- Requests, responses, and events are shown in the in-memory timeline for the current run. Copying or opening details recursively redacts token-like fields; session credentials are not saved into the project directory.
- These settings are stored in the user-data directory, not beside the source, solution, or build output. All commands above use paths relative to the directory containing this README.

### Main workflow

1. Start the external `sdk_open_app` runtime.
2. Open **Connection & Auth**, enter the API token alias and token, then select **Save and Connect**. The API token is stored with Windows DPAPI (`CurrentUser`); the session token remains in memory only.
3. Use **Device & Video** to refresh devices, select a device and runtime-provided resolution, open the device, and start or stop preview. Buttons are enabled by the device state machine.
4. Use **Capture** to select processing, color, output format, four independent thumbnail outputs, and an optional enhancement workflow. The client sends the same `capture.profile.v1` and enhancement `pipeline` through `video.start`, `video.set_profile`, and `capture.take`; it consumes `capture.completed`/`capture.failed` events and uses limited `capture.get` polling as a fallback.
   The Capture page owns an independent capture preview session and uses the web-demo acquisition resolution policy. In **Single page** mode, enabling **Realtime detection box** sends `video.set_profile`; four-point `detected_rects` metadata is drawn over the preview using runtime-reported source dimensions. If no detector result has arrived yet, the UI shows a dashed guide and status message without treating it as a confirmed crop.
   Automatic crop is represented by `page_processing=single_page`; crop-border width and height are additional edge margins. Page-turn detection is configured separately through `capture.set_turn_detect`, matching the web demo and runtime contract.
   Task snapshots preserve all runtime `assets`, `stages`, and `warnings`. Preview selection prefers processed/final thumbnails and processed/final assets over `original`, and double-clicking a task exposes its complete snapshot in the response inspector for fallback diagnosis.
5. Use **Image Processing** to choose and upload one local image, compare original and processed previews, draw a rectangular or four-point selected area, and inspect multiple outputs. The client uploads through `POST /api/uploads/images` with the in-memory Bearer session, then runs `image.process`, `image.process_page`, or `image.apply_color_mode`. Combined processing can select JPG/PNG/TIFF; the split paper/color commands preserve the source format, so format conversion belongs to `file.convert`. Double-click an output row to restore the complete request/response in the protocol inspector.
6. **Image Enhancement** has been split into a dedicated structured workflow page. Its upload, capability, workflow, asynchronous-task, and output-preview logic is retained in the code, but the Windows UI currently disables **Image Enhancement** and all following tabs until the runtime capability is validated.
7. **Raw JSON Command** is currently disabled together with Image Enhancement and the following tabs. Its implementation is retained in code and, when re-enabled, will be limited to public SDK methods not pre-configured in the form pages.
8. The video connection uses the stream id returned by `video.start` together with the in-memory session token. This client only connects to an already running runtime; it never starts, stops, or modifies the C++ runtime.
9. The Windows UI does not expose SANE or a scenario page. SANE depends on Linux scanner support, and the runtime/demo package no longer contains scenario JSON files or a scenario runner.
10. Use the **English/中文** toolbar button to switch the main shell and supported page controls without restarting the application.
11. Double-click a request, response, or event row in the timeline (or press Enter) to open the full redacted Payload detail view. The detail view supports scrolling, selecting, and copying long responses for diagnosis.

Requests, responses, events, timeline entries, and copied payloads recursively redact `token`, `session_token`, `api_key`, `authorization`, and `secret` properties.

### Runtime solution and verification

- `SdkOpen.CSharpTestClient.sln` is the only runtime/demo solution and contains the Core library plus the single WinForms demo.
- A runtime distribution only needs the WinForms executable, its Core dependency, and the .NET 8 Windows Desktop Runtime. SANE, scenario files, and the currently disabled Image Enhancement and following tabs are not part of the Windows runtime package.

```powershell
dotnet build SdkOpen.CSharpTestClient.sln --configuration Release --no-restore --nologo
```
