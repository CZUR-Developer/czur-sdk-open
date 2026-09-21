using System.Text.Json;
using SdkOpen.TestClient.Core.Configuration;
using SdkOpen.TestClient.Core.Diagnostics;
using SdkOpen.TestClient.Core.Protocol;
using SdkOpen.TestClient.Core.Transport;

namespace SdkOpen.TestClient.Core.Application;

public sealed record ConnectionBootstrapResult(bool IsAuthenticated, HealthCheckResult Health, string? Error = null);
public sealed record VideoStartResult(CommandResponse Response, string StreamId);
public sealed record CaptureSubmissionResult(CaptureTaskSnapshot Task, JsonElement RequestParameters, CommandResponse Response);
public sealed record CaptureQueryResult(CaptureTaskSnapshot Task, JsonElement RequestParameters, CommandResponse Response);

public interface ISdkCommandExecutor
{
    IReadOnlyCollection<string> Capabilities { get; }
    event EventHandler<CommandEvent>? EventReceived;
    Task<CommandResponse> ExecuteAsync(string method, JsonElement parameters, CancellationToken cancellationToken);
}

public sealed class SdkOpenCoordinator : ISdkCommandExecutor
{
    private readonly IHealthCheckClient _healthCheckClient;
    private readonly CommandWebSocketClient _commandClient;
    private readonly CommandRequestFactory _requestFactory;
    private readonly List<TimelineEntry> _timeline = [];

    public SdkOpenCoordinator(IHealthCheckClient healthCheckClient, CommandWebSocketClient commandClient, CommandRequestFactory requestFactory)
    {
        _healthCheckClient = healthCheckClient;
        _commandClient = commandClient;
        _requestFactory = requestFactory;
        _commandClient.EventReceived += OnEventReceived;
        _commandClient.ConnectionClosed += (_, args) => InvalidateSession(args.Reason);
    }

    public SessionState Session { get; } = new();
    public DeviceWorkflowState DeviceWorkflow { get; } = new();
    public CaptureTaskTracker CaptureTasks { get; } = new();
    public IReadOnlyList<TimelineEntry> Timeline => _timeline;
    public bool IsConnected => _commandClient.IsConnected;
    public IReadOnlyCollection<string> Capabilities => Session.Capabilities;
    public event EventHandler<CommandEvent>? EventReceived;

    public async Task<ConnectionBootstrapResult> ConnectAsync(EndpointProfile profile, string apiKey, CancellationToken cancellationToken)
    {
        // 连接顺序固定为 HTTP health -> WebSocket -> ping/info -> 建立会话 -> 读取能力。
        // 这样页面在发送设备命令前就能明确知道运行时是否可达、Token 是否有效。
        // The bootstrap order is HTTP health -> WebSocket -> ping/info -> session -> capabilities.
        // This validates runtime reachability and the token before device commands are sent.
        ArgumentNullException.ThrowIfNull(profile);
        ArgumentException.ThrowIfNullOrWhiteSpace(apiKey);
        InvalidateSession("Preparing a new connection.");
        var health = await _healthCheckClient.CheckAsync(profile.HealthEndpoint, cancellationToken).ConfigureAwait(false);
        AddTimeline("health", health.IsReachable ? "Health check succeeded." : $"Health check failed: {health.Error}");
        if (!health.IsReachable)
        {
            return new ConnectionBootstrapResult(false, health, health.Error ?? "Health endpoint is unavailable.");
        }

        try
        {
            await _commandClient.ConnectAsync(profile.CommandEndpoint, cancellationToken).ConfigureAwait(false);
            EnsureSuccess("system.ping", await ExecuteAsync("system.ping", EmptyObject(), cancellationToken).ConfigureAwait(false));
            EnsureSuccess("system.info", await ExecuteAsync("system.info", EmptyObject(), cancellationToken).ConfigureAwait(false));
            var session = await ExecuteAsync("auth.create_session", JsonSerializer.SerializeToElement(new { token = apiKey }), cancellationToken).ConfigureAwait(false);
            EnsureSuccess("auth.create_session", session);
            if (!session.Data.TryGetProperty("session_token", out var token) || string.IsNullOrWhiteSpace(token.GetString()))
            {
                throw new InvalidOperationException("auth.create_session did not return session_token.");
            }

            // session_token 仅保存在内存 SessionState 中，API Token 仍由设置页负责加密持久化。
            // Keep session_token only in memory; the settings page persists the API token with encryption.
            Session.SetSession(token.GetString()!);
            var context = await ExecuteAsync("auth.get_context", EmptyObject(), cancellationToken).ConfigureAwait(false);
            EnsureSuccess("auth.get_context", context);
            SetCapabilitiesFromAuthContext(context.Data);

            return new ConnectionBootstrapResult(true, health);
        }
        catch (Exception exception)
        {
            InvalidateSession($"Connection bootstrap failed: {exception.Message}");
            return new ConnectionBootstrapResult(false, health, exception.Message);
        }
    }

    public async Task<CommandResponse> ExecuteAsync(string method, JsonElement parameters, CancellationToken cancellationToken)
    {
        // 所有命令统一从这里经过，时间线因此可以同时记录原始请求和响应，便于复现问题。
        // All commands pass through here so requests and responses are recorded together for diagnosis.
        var request = _requestFactory.Create(method, parameters);
        AddTimeline("request", $"{method} ({request.RequestId})", JsonSerializer.Serialize(request));
        var response = await _commandClient.SendAsync(request, cancellationToken).ConfigureAwait(false);
        AddTimeline("response", $"{method}: code {response.Code} ({request.RequestId})", JsonSerializer.Serialize(response));
        return response;
    }

    public Task DisconnectAsync(CancellationToken cancellationToken) => _commandClient.CloseAsync(cancellationToken);

    public async Task<IReadOnlyList<DeviceSummary>> RefreshDevicesAsync(CancellationToken cancellationToken)
    {
        var response = await ExecuteRequiredAsync("device.list", EmptyObject(), cancellationToken).ConfigureAwait(false);
        var devices = ParseDeviceList(response.Data);
        DeviceWorkflow.ApplyDeviceList(devices);
        return devices;
    }

    public async Task<DeviceWorkflowSnapshot> SelectDeviceAsync(string deviceId, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(deviceId);
        DeviceWorkflow.SelectDevice(deviceId);
        try
        {
            var response = await ExecuteRequiredAsync("device.get", JsonSerializer.SerializeToElement(new { device_id = deviceId }), cancellationToken).ConfigureAwait(false);
            DeviceWorkflow.ApplyDeviceDetails(
                ParseResolutions(response.Data),
                ParseFeatures(response.Data),
                ParseCaptureOutput(response.Data));
            return DeviceWorkflow.Snapshot;
        }
        catch
        {
            DeviceWorkflow.SelectDevice(null);
            throw;
        }
    }

    public void SelectResolution(string resolutionKey) => DeviceWorkflow.SelectResolution(resolutionKey);

    public async Task<CommandResponse> OpenSelectedDeviceAsync(CancellationToken cancellationToken)
    {
        var snapshot = DeviceWorkflow.Snapshot;
        if (!snapshot.CanOpen || snapshot.SelectedResolution is null)
        {
            throw new InvalidOperationException("请先选择设备及运行时返回的分辨率，再打开设备。");
        }
        var resolution = snapshot.SelectedResolution;
        var response = await ExecuteRequiredAsync("device.open", JsonSerializer.SerializeToElement(new
        {
            device_id = snapshot.SelectedDeviceId,
            width = resolution.Width,
            height = resolution.Height,
            fps = resolution.Fps,
            pixel_format = resolution.PixelFormat
        }), cancellationToken).ConfigureAwait(false);
        DeviceWorkflow.MarkOpened();
        // Some runtimes only expose capture capabilities after device.open.
        // 部分 runtime 只有在 device.open 后才返回采集能力，因此这里再次合并打开结果。
        ApplyOpenedDeviceDetails(response.Data);
        return response;
    }

    public async Task<VideoStartResult> StartPreviewAsync(CancellationToken cancellationToken)
        => await StartPreviewCoreAsync(null, cancellationToken).ConfigureAwait(false);

    /// <summary>
    /// Starts the capture-owned stream with its profile in the initial video.start request.
    /// 采集页独立启动视频流，并在首次 video.start 时携带完整 profile。
    /// </summary>
    public async Task<VideoStartResult> StartCapturePreviewAsync(JsonElement profile, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(profile);
        var pipeline = CaptureEnhancePipelineBuilder.Build(null, CaptureOutputFormat.Jpg);
        return await StartPreviewCoreAsync(profile, pipeline, cancellationToken).ConfigureAwait(false);
    }

    public async Task<VideoStartResult> StartCapturePreviewAsync(JsonElement profile, JsonElement pipeline, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(profile);
        ArgumentNullException.ThrowIfNull(pipeline);
        return await StartPreviewCoreAsync(profile, pipeline, cancellationToken).ConfigureAwait(false);
    }

    private async Task<VideoStartResult> StartPreviewCoreAsync(JsonElement? profile, CancellationToken cancellationToken) =>
        await StartPreviewCoreAsync(profile, null, cancellationToken).ConfigureAwait(false);

    private async Task<VideoStartResult> StartPreviewCoreAsync(JsonElement? profile, JsonElement? pipeline, CancellationToken cancellationToken)
    {
        var snapshot = DeviceWorkflow.Snapshot;
        if (!snapshot.CanStartPreview || snapshot.SelectedResolution is null)
        {
            throw new InvalidOperationException("请先成功打开设备，再启动预览。");
        }
        var resolution = snapshot.SelectedResolution;
        var parameters = new Dictionary<string, object?>
        {
            ["device_id"] = snapshot.SelectedDeviceId,
            ["width"] = resolution.Width,
            ["height"] = resolution.Height,
            ["fps"] = resolution.Fps,
            ["pixel_format"] = resolution.PixelFormat,
            ["format"] = "mjpeg"
        };
        if (profile.HasValue) parameters["profile"] = profile.Value;
        if (pipeline.HasValue) parameters["pipeline"] = pipeline.Value;
        var response = await ExecuteRequiredAsync("video.start", JsonSerializer.SerializeToElement(parameters), cancellationToken).ConfigureAwait(false);
        var streamId = FindString(response.Data, "stream_id") ?? FindNestedString(response.Data, "stream", "stream_id");
        if (string.IsNullOrWhiteSpace(streamId))
        {
            throw new InvalidOperationException("video.start 未返回 stream_id。");
        }
        DeviceWorkflow.MarkPreviewStarted(streamId);
        return new VideoStartResult(response, streamId);
    }

    /// <summary>
    /// Applies the capture profile to an active preview so single-page detection rectangles
    /// are emitted by the runtime, matching the web demo's video.set_profile flow.
    /// 将采集 profile 同步到活动预览，使 runtime 按网页 demo 的流程输出单页检测框。
    /// </summary>
    public async Task<CommandResponse> SetVideoProfileAsync(JsonElement profile, CancellationToken cancellationToken)
    {
        var pipeline = CaptureEnhancePipelineBuilder.Build(null, CaptureOutputFormat.Jpg);
        return await SetVideoProfileAsync(profile, pipeline, cancellationToken).ConfigureAwait(false);
    }

    public async Task<CommandResponse> SetVideoProfileAsync(JsonElement profile, JsonElement pipeline, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(profile);
        ArgumentNullException.ThrowIfNull(pipeline);
        var snapshot = DeviceWorkflow.Snapshot;
        if (!snapshot.CanCapture || string.IsNullOrWhiteSpace(snapshot.SelectedDeviceId))
        {
            throw new InvalidOperationException("请先打开设备并启动预览，再同步视频 profile。");
        }

        return await ExecuteRequiredAsync("video.set_profile", JsonSerializer.SerializeToElement(new
        {
            device_id = snapshot.SelectedDeviceId,
            profile,
            pipeline
        }), cancellationToken).ConfigureAwait(false);
    }

    public async Task<CommandResponse?> StopPreviewAsync(CancellationToken cancellationToken)
    {
        var snapshot = DeviceWorkflow.Snapshot;
        if (!snapshot.CanStopPreview)
        {
            return null;
        }
        var response = await ExecuteRequiredAsync("video.stop", JsonSerializer.SerializeToElement(new { device_id = snapshot.SelectedDeviceId, stream_id = snapshot.StreamId }), cancellationToken).ConfigureAwait(false);
        DeviceWorkflow.MarkPreviewStopped();
        return response;
    }

    public async Task<CommandResponse?> CloseSelectedDeviceAsync(CancellationToken cancellationToken)
    {
        var snapshot = DeviceWorkflow.Snapshot;
        if (string.IsNullOrWhiteSpace(snapshot.SelectedDeviceId) || (!snapshot.IsOpened && string.IsNullOrWhiteSpace(snapshot.StreamId)))
        {
            return null;
        }
        if (snapshot.CanStopPreview)
        {
            await StopPreviewAsync(cancellationToken).ConfigureAwait(false);
            snapshot = DeviceWorkflow.Snapshot;
        }
        var response = await ExecuteRequiredAsync("device.close", JsonSerializer.SerializeToElement(new { device_id = snapshot.SelectedDeviceId }), cancellationToken).ConfigureAwait(false);
        DeviceWorkflow.MarkClosed();
        return response;
    }

    public async Task<CaptureTaskSnapshot> CaptureSelectedPageAsync(CaptureFormOptions options, CancellationToken cancellationToken)
    {
        var pipeline = CaptureEnhancePipelineBuilder.Build(null, options.OutputFormat);
        return (await CaptureSelectedPageAsync(options, pipeline, cancellationToken).ConfigureAwait(false)).Task;
    }

    public async Task<CaptureSubmissionResult> CaptureSelectedPageAsync(CaptureFormOptions options, JsonElement pipeline, CancellationToken cancellationToken)
    {
        var snapshot = DeviceWorkflow.Snapshot;
        if (!snapshot.CanCapture || string.IsNullOrWhiteSpace(snapshot.SelectedDeviceId))
        {
            throw new InvalidOperationException("请先选择并打开设备，再采集一页。");
        }
        var profile = CaptureProfileBuilder.Build(options, snapshot.SelectedResolution, snapshot.SelectedDeviceId);
        // capture.take 只负责提交任务；真正的图片结果通过 capture.* 事件或 capture.get 查询取得。
        // capture.take only submits a task; image results come from capture.* events or capture.get polling.
        var parameters = JsonSerializer.SerializeToElement(new
        {
            device_id = snapshot.SelectedDeviceId,
            profile,
            pipeline,
            timeout_ms = 20_000
        });
        var response = await ExecuteRequiredAsync("capture.take", parameters, cancellationToken).ConfigureAwait(false);
        var taskId = FindString(response.Data, "task_id") ?? FindNestedString(response.Data, "task", "task_id");
        if (string.IsNullOrWhiteSpace(taskId))
        {
            throw new InvalidOperationException("capture.take 未返回 task_id。");
        }
        // 先登记任务再应用响应，兼容“事件先到、take 响应后到”的 WebSocket 时序。
        // Register the task before applying the response to support event-before-response WebSocket ordering.
        var task = CaptureTasks.Register(taskId, options.OutputFormat.ToString().ToLowerInvariant());
        CaptureTasks.ApplyPayload(response.Data);
        _ = TrackCaptureAsync(taskId);
        var registered = CaptureTasks.Tasks.First(item => string.Equals(item.TaskId, task.TaskId, StringComparison.Ordinal));
        return new CaptureSubmissionResult(registered, parameters, response);
    }

    public async Task<CommandResponse> SetTurnDetectAsync(bool enabled, CancellationToken cancellationToken)
    {
        var deviceId = DeviceWorkflow.Snapshot.SelectedDeviceId;
        if (string.IsNullOrWhiteSpace(deviceId))
        {
            throw new InvalidOperationException("Select a device before changing page-turn detection.");
        }
        return await ExecuteRequiredAsync("capture.set_turn_detect", JsonSerializer.SerializeToElement(new
        {
            device_id = deviceId,
            enabled,
            auto_capture = false,
            scan_device_type = 0,
            cooldown_ms = 1000
        }), cancellationToken).ConfigureAwait(false);
    }

    public async Task<IReadOnlyList<ImageEnhanceWorkflow>> ListImageEnhanceWorkflowsAsync(CancellationToken cancellationToken)
    {
        var response = await ExecuteRequiredAsync("image.enhance_workflow_list", EmptyObject(), cancellationToken).ConfigureAwait(false);
        if (!response.Data.TryGetProperty("workflows", out var workflows) || workflows.ValueKind != JsonValueKind.Array) return [];
        return workflows.EnumerateArray()
            .Where(item => item.ValueKind == JsonValueKind.Object && item.TryGetProperty("pipeline", out var pipeline) && pipeline.ValueKind == JsonValueKind.Object)
            .Select(item => new ImageEnhanceWorkflow(
                FindString(item, "workflow_id") ?? string.Empty,
                FindString(item, "name") ?? string.Empty,
                FindString(item, "description") ?? string.Empty,
                item.GetProperty("pipeline").Clone()))
            .ToArray();
    }

    public async Task<IReadOnlyList<ImageEnhanceCapabilitySnapshot>> ListImageEnhanceCapabilitiesAsync(CancellationToken cancellationToken)
    {
        var response = await ExecuteRequiredAsync("image.enhance_capabilities", EmptyObject(), cancellationToken).ConfigureAwait(false);
        if (!response.Data.TryGetProperty("providers", out var providers) || providers.ValueKind != JsonValueKind.Array)
        {
            return [];
        }

        return providers.EnumerateArray()
            .Where(provider => provider.ValueKind == JsonValueKind.Object && provider.TryGetProperty("capabilities", out var capabilities) && capabilities.ValueKind == JsonValueKind.Array)
            .SelectMany(provider => provider.GetProperty("capabilities").EnumerateArray())
            .Where(item => item.ValueKind == JsonValueKind.Object)
            .Select(item => new ImageEnhanceCapabilitySnapshot(
                FindString(item, "type") ?? string.Empty,
                FindString(item, "title") ?? FindString(item, "type") ?? string.Empty,
                FindString(item, "description") ?? string.Empty,
                FindString(item, "category") ?? string.Empty,
                FindString(item, "runtime") ?? string.Empty,
                FindBoolean(item, "available") ?? false,
                FindString(item, "requires_capability") ?? string.Empty,
                FindString(item, "unavailable_reason") ?? string.Empty,
                FindObject(item, "defaults"),
                FindObject(item, "schema")))
            .OrderBy(item => item.Category, StringComparer.Ordinal)
            .ThenBy(item => item.Type, StringComparer.Ordinal)
            .ToArray();
    }

    public async Task<ImageEnhanceWorkflow> SaveImageEnhanceWorkflowAsync(
        string name,
        string description,
        JsonElement pipeline,
        CancellationToken cancellationToken)
    {
        var response = await ExecuteRequiredAsync(
            "image.enhance_workflow_save",
            ImageEnhancementRequestBuilder.BuildWorkflowSaveParameters(name, description, pipeline),
            cancellationToken).ConfigureAwait(false);
        if (!response.Data.TryGetProperty("workflow", out var workflow) || workflow.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidOperationException("image.enhance_workflow_save did not return a workflow.");
        }
        if (!workflow.TryGetProperty("pipeline", out var savedPipeline) || savedPipeline.ValueKind != JsonValueKind.Object)
        {
            savedPipeline = pipeline;
        }
        return new ImageEnhanceWorkflow(
            FindString(workflow, "workflow_id") ?? string.Empty,
            FindString(workflow, "name") ?? name,
            FindString(workflow, "description") ?? description,
            savedPipeline.Clone());
    }

    public async Task DeleteImageEnhanceWorkflowAsync(string workflowId, CancellationToken cancellationToken)
    {
        await ExecuteRequiredAsync(
            "image.enhance_workflow_delete",
            ImageEnhancementRequestBuilder.BuildWorkflowDeleteParameters(workflowId),
            cancellationToken).ConfigureAwait(false);
    }

    public async Task<CommandResponse> ExecuteImageEnhancementAsync(
        IReadOnlyList<string> uploadIds,
        JsonElement pipeline,
        CancellationToken cancellationToken)
    {
        var parameters = ImageEnhancementRequestBuilder.BuildEnhanceParameters(uploadIds, pipeline);
        return await ExecuteRequiredAsync("image.enhance", parameters, cancellationToken).ConfigureAwait(false);
    }

    public async Task<ImageEnhanceTaskSnapshot> RefreshImageEnhancementTaskAsync(string taskId, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(taskId);
        var response = await ExecuteRequiredAsync(
            "image.enhance_get",
            JsonSerializer.SerializeToElement(new { task_id = taskId }),
            cancellationToken).ConfigureAwait(false);
        return ImageEnhancementRequestBuilder.ParseTask(response.Data);
    }

    /// <summary>
    /// Executes one request produced by the image-processing builder and requires a successful runtime response.
    /// 执行由图像处理构造器生成的请求，并要求 runtime 返回成功状态。
    /// </summary>
    public async Task<CommandResponse> ExecuteImageProcessingAsync(ImageProcessingRequest request, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (request.Method is not ("image.process" or "image.process_page" or "image.apply_color_mode"))
        {
            throw new ArgumentException("Unsupported image-processing method.", nameof(request));
        }
        return await ExecuteRequiredAsync(request.Method, request.Parameters, cancellationToken).ConfigureAwait(false);
    }

    public async Task<CaptureTaskSnapshot> RefreshCaptureTaskAsync(string taskId, CancellationToken cancellationToken)
    {
        return (await RefreshCaptureTaskWithProtocolAsync(taskId, cancellationToken).ConfigureAwait(false)).Task;
    }

    public async Task<CaptureQueryResult> RefreshCaptureTaskWithProtocolAsync(string taskId, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(taskId);
        var parameters = JsonSerializer.SerializeToElement(new { task_id = taskId });
        var response = await ExecuteRequiredAsync("capture.get", parameters, cancellationToken).ConfigureAwait(false);
        CaptureTasks.ApplyPayload(response.Data, CaptureTaskUpdateSource.Poll);
        var task = CaptureTasks.Tasks.First(item => string.Equals(item.TaskId, taskId, StringComparison.Ordinal));
        return new CaptureQueryResult(task, parameters, response);
    }

    public void InvalidateSession(string reason)
    {
        Session.Clear();
        DeviceWorkflow.Clear();
        AddTimeline("session", reason);
    }

    private void OnEventReceived(object? sender, CommandEvent commandEvent)
    {
        AddTimeline("event", commandEvent.Event, JsonSerializer.Serialize(commandEvent));
        if (string.Equals(commandEvent.Event, "device.removed", StringComparison.Ordinal))
        {
            var deviceId = FindString(commandEvent.Payload, "device_id");
            if (string.IsNullOrWhiteSpace(deviceId))
            {
                DeviceWorkflow.Clear();
            }
            else
            {
                DeviceWorkflow.RemoveDevice(deviceId);
            }
        }
        if (commandEvent.Event.StartsWith("capture.", StringComparison.Ordinal))
        {
            // 事件是任务状态的实时来源；后台 TrackCaptureAsync 只作为事件丢失时的兜底轮询。
            // Events are the real-time task source; TrackCaptureAsync only polls when events are missed.
            CaptureTasks.ApplyPayload(commandEvent.Payload);
        }
        EventReceived?.Invoke(this, commandEvent);
    }

    private void AddTimeline(string category, string summary, string? payload = null) =>
        // 时间线保存 UTC，控件展示时再转换成本机时间；Payload 写入前统一脱敏。
        // Store UTC in the timeline, convert to local time in the UI, and redact payloads before storing them.
        _timeline.Add(new TimelineEntry(DateTimeOffset.UtcNow, category, summary, payload is null ? null : SecretRedactor.Redact(payload)));

    private static JsonElement EmptyObject() => JsonSerializer.SerializeToElement(new { });

    private async Task<CommandResponse> ExecuteRequiredAsync(string method, JsonElement parameters, CancellationToken cancellationToken)
    {
        var response = await ExecuteAsync(method, parameters, cancellationToken).ConfigureAwait(false);
        EnsureSuccess(method, response);
        return response;
    }

    private async Task TrackCaptureAsync(string taskId)
    {
        // 不阻塞采集按钮：轮询在后台运行，任务状态通过 CaptureTaskTracker.Changed 回推到 UI。
        // Keep the capture button responsive: polling runs in the background and Changed updates the UI.
        try
        {
            await CaptureTasks.PollUntilTerminalAsync(taskId, async (id, cancellationToken) =>
            {
                var task = await RefreshCaptureTaskAsync(id, cancellationToken).ConfigureAwait(false);
                return JsonSerializer.SerializeToElement(new { task_id = task.TaskId, status = task.Status.ToString().ToLowerInvariant() });
            }, CancellationToken.None).ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            AddTimeline("capture", $"capture.get ({taskId}) stopped: {exception.Message}");
        }
    }

    private static IReadOnlyList<DeviceSummary> ParseDeviceList(JsonElement data)
    {
        if (!data.TryGetProperty("devices", out var devices) || devices.ValueKind != JsonValueKind.Array) return [];
        return devices.EnumerateArray().Select(ParseDevice).Where(device => device is not null).Cast<DeviceSummary>().ToArray();
    }

    private static DeviceSummary? ParseDevice(JsonElement item)
    {
        var id = FindString(item, "device_id");
        if (string.IsNullOrWhiteSpace(id)) return null;
        return new DeviceSummary(id, FindString(item, "display_name") ?? id, FindString(item, "status") ?? "unknown", FindString(item, "model") ?? string.Empty, FindBoolean(item, "supports_video") ?? true);
    }

    private static IReadOnlyList<DeviceResolution> ParseResolutions(JsonElement data)
    {
        if (!data.TryGetProperty("resolutions", out var values) || values.ValueKind != JsonValueKind.Array) return [];
        return values.EnumerateArray().Select(item => new DeviceResolution(
            FindInt(item, "width") ?? 0,
            FindInt(item, "height") ?? 0,
            FindInt(item, "fps") ?? 0,
            FindString(item, "pixel_format") ?? "mjpeg",
            FindInt(item, "real_width") ?? 0,
            FindInt(item, "real_height") ?? 0,
            FindBoolean(item, "is_default") ?? false))
            .Where(item => item.Width > 0 && item.Height > 0).ToArray();
    }

    private static DeviceFeatures ParseFeatures(JsonElement data)
    {
        var source = data.TryGetProperty("features", out var features) && features.ValueKind == JsonValueKind.Object
            ? features
            : default;
        return new DeviceFeatures(FindBoolean(source, "image_transfer_protocol") ?? false);
    }

    private static CaptureOutputCapabilities ParseCaptureOutput(JsonElement data)
    {
        if (!data.TryGetProperty("capture_output", out var output) || output.ValueKind != JsonValueKind.Object)
        {
            return CaptureOutputCapabilities.Empty;
        }

        var targetSizes = output.TryGetProperty("target_sizes", out var values) && values.ValueKind == JsonValueKind.Array
            ? values.EnumerateArray()
                .Select(item => new OutputTargetSizeOption(
                    FindInt(item, "target_size") ?? 0,
                    FindInt(item, "width") ?? 0,
                    FindInt(item, "height") ?? 0,
                    FindBoolean(item, "is_device_default") ?? false))
                .Where(item => item.TargetSize > 0 && item.Width > 0 && item.Height > 0)
                .ToArray()
            : [];
        return new CaptureOutputCapabilities(
            (FindBoolean(output, "target_size_supported") ?? false) && targetSizes.Length > 0,
            targetSizes);
    }

    private void ApplyOpenedDeviceDetails(JsonElement data)
    {
        var source = data.TryGetProperty("device", out var nested) && nested.ValueKind == JsonValueKind.Object
            ? nested
            : data;
        var previousResolutionKey = DeviceWorkflow.Snapshot.SelectedResolutionKey;
        var hasFeatures = source.TryGetProperty("features", out var featureValue) && featureValue.ValueKind == JsonValueKind.Object;
        var hasCaptureOutput = source.TryGetProperty("capture_output", out var captureOutputValue) && captureOutputValue.ValueKind == JsonValueKind.Object;
        var features = ParseFeatures(source);
        var captureOutput = ParseCaptureOutput(source);
        var resolutions = ParseResolutions(source);
        if (resolutions.Count > 0 || hasFeatures || hasCaptureOutput)
        {
            DeviceWorkflow.ApplyDeviceDetails(
                resolutions.Count > 0 ? resolutions : DeviceWorkflow.Snapshot.Resolutions,
                hasFeatures ? features : DeviceWorkflow.Snapshot.Features,
                hasCaptureOutput ? captureOutput : DeviceWorkflow.Snapshot.CaptureOutput);

            // device.open may return a reduced resolution list. Keep the resolution
            // selected for capture whenever it is still present; ET/image-transfer
            // devices must be re-prepared if the runtime omitted the injected bucket.
            if (!string.IsNullOrWhiteSpace(previousResolutionKey) &&
                DeviceWorkflow.Snapshot.Resolutions.Any(item => string.Equals(item.Key, previousResolutionKey, StringComparison.Ordinal)))
            {
                DeviceWorkflow.SelectResolution(previousResolutionKey);
            }
            else if (DeviceWorkflow.Snapshot.Features.ImageTransferProtocol)
            {
                DeviceWorkflow.PrepareCaptureAcquisitionResolution();
            }
        }
    }

    private static string? FindString(JsonElement data, string property) =>
        data.ValueKind == JsonValueKind.Object && data.TryGetProperty(property, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() : null;

    private static string? FindNestedString(JsonElement data, string parent, string property) =>
        data.ValueKind == JsonValueKind.Object && data.TryGetProperty(parent, out var nested) ? FindString(nested, property) : null;

    private static int? FindInt(JsonElement data, string property) =>
        data.ValueKind == JsonValueKind.Object && data.TryGetProperty(property, out var value) && value.TryGetInt32(out var number) ? number : null;

    private static bool? FindBoolean(JsonElement data, string property) =>
        data.ValueKind == JsonValueKind.Object && data.TryGetProperty(property, out var value) && (value.ValueKind is JsonValueKind.True or JsonValueKind.False) ? value.GetBoolean() : null;

    private static JsonElement FindObject(JsonElement data, string property) =>
        data.ValueKind == JsonValueKind.Object && data.TryGetProperty(property, out var value) && value.ValueKind == JsonValueKind.Object
            ? value.Clone()
            : JsonSerializer.SerializeToElement(new { });

    private void SetCapabilitiesFromAuthContext(JsonElement data)
    {
        var context = data.TryGetProperty("auth_context", out var nested) && nested.ValueKind == JsonValueKind.Object
            ? nested
            : data;
        if (context.TryGetProperty("capabilities", out var capabilities) && capabilities.ValueKind == JsonValueKind.Array)
        {
            Session.SetCapabilities(capabilities.EnumerateArray().Select(item => item.GetString() ?? string.Empty));
        }
    }

    private static void EnsureSuccess(string method, CommandResponse response)
    {
        if (response.Code != 0)
        {
            throw new InvalidOperationException($"{method} failed: code {response.Code} ({response.Message}).");
        }
    }
}
