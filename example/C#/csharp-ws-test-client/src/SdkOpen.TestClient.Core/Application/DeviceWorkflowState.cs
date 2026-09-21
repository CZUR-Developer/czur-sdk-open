namespace SdkOpen.TestClient.Core.Application;

public sealed record DeviceSummary(string DeviceId, string DisplayName, string Status, string Model = "", bool SupportsVideo = true);

/// <summary>
/// Capabilities reported by <c>device.get</c>/<c>device.open</c>.
/// 设备详情和打开结果中的能力声明；不要根据型号字符串猜测设备行为。
/// </summary>
public sealed record DeviceFeatures(bool ImageTransferProtocol = false);

public sealed record OutputTargetSizeOption(int TargetSize, int Width, int Height, bool IsDeviceDefault = false)
{
    public string DisplayLabel => $"{TargetSize} px ({Width} x {Height})" + (IsDeviceDefault ? " (Default)" : string.Empty);
}

public sealed record CaptureOutputCapabilities(
    bool TargetSizeSupported,
    IReadOnlyList<OutputTargetSizeOption> TargetSizes)
{
    public static CaptureOutputCapabilities Empty { get; } = new(false, []);
}

public sealed record DeviceResolution(
    int Width,
    int Height,
    int Fps = 0,
    string PixelFormat = "mjpeg",
    int RealWidth = 0,
    int RealHeight = 0,
    bool IsDefault = false)
{
    public string Key => Fps > 0 ? $"{Width}x{Height}@{Fps}" : $"{Width}x{Height}";
    public string DisplayLabel
    {
        get
        {
            var label = Fps > 0 ? $"{Width} x {Height} @ {Fps}fps" : $"{Width} x {Height}";
            return IsDefault ? $"{label} (Default)" : label;
        }
    }
}

public sealed record DeviceWorkflowSnapshot(
    IReadOnlyList<DeviceSummary> Devices,
    string? SelectedDeviceId,
    IReadOnlyList<DeviceResolution> Resolutions,
    string? SelectedResolutionKey,
    bool IsOpened,
    string? StreamId)
{
    public DeviceFeatures Features { get; init; } = new();
    public CaptureOutputCapabilities CaptureOutput { get; init; } = CaptureOutputCapabilities.Empty;

    // 按网页 Demo 的操作顺序约束按钮：打开设备 -> 启动预览 -> 才允许采集。
    // Enforce the web demo order: open the device, start preview, then allow capture.
    public bool CanOpen => !string.IsNullOrWhiteSpace(SelectedDeviceId) && Resolutions.Count > 0 && !IsOpened && string.IsNullOrWhiteSpace(StreamId);
    public bool CanStartPreview => IsOpened && !string.IsNullOrWhiteSpace(SelectedDeviceId) && string.IsNullOrWhiteSpace(StreamId);
    public bool CanStopPreview => !string.IsNullOrWhiteSpace(StreamId);
    public bool CanCapture => IsOpened && !string.IsNullOrWhiteSpace(SelectedDeviceId) && !string.IsNullOrWhiteSpace(StreamId);
    public DeviceResolution? SelectedResolution => Resolutions.FirstOrDefault(item => string.Equals(item.Key, SelectedResolutionKey, StringComparison.Ordinal));
}

public sealed class DeviceWorkflowState
{
    private DeviceWorkflowSnapshot _snapshot = Empty();

    public DeviceWorkflowSnapshot Snapshot => _snapshot;
    public event EventHandler<DeviceWorkflowSnapshot>? Changed;

    public void ApplyDeviceList(IEnumerable<DeviceSummary> devices)
    {
        var normalized = devices.Where(device => !string.IsNullOrWhiteSpace(device.DeviceId)).ToArray();
        var selectedDeviceId = normalized.Any(device => string.Equals(device.DeviceId, _snapshot.SelectedDeviceId, StringComparison.Ordinal))
            ? _snapshot.SelectedDeviceId
            : null;
        _snapshot = selectedDeviceId is null
            ? Empty(normalized)
            : _snapshot with { Devices = normalized };
        RaiseChanged();
    }

    public void SelectDevice(string? deviceId)
    {
        if (string.IsNullOrWhiteSpace(deviceId) || !_snapshot.Devices.Any(device => string.Equals(device.DeviceId, deviceId, StringComparison.Ordinal)))
        {
            _snapshot = Empty(_snapshot.Devices);
        }
        else
        {
            // 切换设备时清空旧分辨率、打开状态和流 ID，避免把上一个设备的状态带到新设备。
            // Clear the previous resolution, open state, and stream ID when switching devices.
            _snapshot = new DeviceWorkflowSnapshot(_snapshot.Devices, deviceId, [], null, false, null)
            {
                Features = new(),
                CaptureOutput = CaptureOutputCapabilities.Empty
            };
        }
        RaiseChanged();
    }

    public void ApplyDeviceDetails(
        IEnumerable<DeviceResolution> resolutions,
        DeviceFeatures? features = null,
        CaptureOutputCapabilities? captureOutput = null)
    {
        EnsureSelectedDevice();
        var normalized = resolutions.Where(resolution => resolution.Width > 0 && resolution.Height > 0).ToArray();
        // Prefer the runtime-declared default instead of relying on array order.
        // 优先使用 runtime 返回的默认分辨率，不能假设数组第一项就是默认值。
        var selected = normalized.FirstOrDefault(item => item.IsDefault)?.Key ?? normalized.FirstOrDefault()?.Key;
        _snapshot = _snapshot with
        {
            Resolutions = normalized,
            SelectedResolutionKey = selected,
            Features = features ?? _snapshot.Features,
            CaptureOutput = captureOutput ?? _snapshot.CaptureOutput
        };
        RaiseChanged();
    }

    public void ApplyDeviceDetails(IEnumerable<string> resolutions) => ApplyDeviceDetails(resolutions.Select(ParseResolution));

    public void SelectResolution(string? resolutionKey)
    {
        EnsureSelectedDevice();
        if (!_snapshot.Resolutions.Any(item => string.Equals(item.Key, resolutionKey, StringComparison.Ordinal)))
        {
            throw new InvalidOperationException("请选择运行时返回的有效分辨率。");
        }
        _snapshot = _snapshot with { SelectedResolutionKey = resolutionKey };
        RaiseChanged();
    }

    /// <summary>
    /// Applies the web demo's capture rule: image-transfer devices always use 1536x1152;
    /// other devices retain the runtime-selected default resolution.
    /// </summary>
    public DeviceResolution PrepareCaptureAcquisitionResolution()
    {
        EnsureSelectedDevice();
        if (! _snapshot.Features.ImageTransferProtocol)
        {
            return _snapshot.SelectedResolution
                ?? throw new InvalidOperationException("Runtime did not return a usable device resolution.");
        }

        var resolution = _snapshot.Resolutions.FirstOrDefault(item => item.Width == 1536 && item.Height == 1152)
            ?? new DeviceResolution(1536, 1152, 15, "mjpeg", 1536, 1152);
        if (!_snapshot.Resolutions.Any(item => string.Equals(item.Key, resolution.Key, StringComparison.Ordinal)))
        {
            _snapshot = _snapshot with { Resolutions = [resolution, .. _snapshot.Resolutions] };
        }
        _snapshot = _snapshot with { SelectedResolutionKey = resolution.Key };
        RaiseChanged();
        return resolution;
    }

    public void MarkOpened()
    {
        EnsureSelectedDevice();
        _snapshot = _snapshot with { IsOpened = true };
        RaiseChanged();
    }

    public void MarkPreviewStarted(string streamId)
    {
        EnsureSelectedDevice();
        ArgumentException.ThrowIfNullOrWhiteSpace(streamId);
        _snapshot = _snapshot with { IsOpened = true, StreamId = streamId };
        RaiseChanged();
    }

    public void MarkPreviewStopped()
    {
        _snapshot = _snapshot with { StreamId = null };
        RaiseChanged();
    }

    public void MarkClosed()
    {
        _snapshot = _snapshot with
        {
            IsOpened = false,
            StreamId = null,
            Features = new(),
            CaptureOutput = CaptureOutputCapabilities.Empty
        };
        RaiseChanged();
    }

    public void RemoveDevice(string deviceId)
    {
        ApplyDeviceList(_snapshot.Devices.Where(item => !string.Equals(item.DeviceId, deviceId, StringComparison.Ordinal)));
    }

    public void Clear()
    {
        _snapshot = Empty();
        RaiseChanged();
    }

    private static DeviceWorkflowSnapshot Empty(IReadOnlyList<DeviceSummary>? devices = null) => new(devices ?? [], null, [], null, false, null);

    private static DeviceResolution ParseResolution(string value)
    {
        var parts = value.Split('@', 2, StringSplitOptions.TrimEntries);
        var dimensions = parts[0].Split('x', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        if (dimensions.Length != 2 || !int.TryParse(dimensions[0], out var width) || !int.TryParse(dimensions[1], out var height))
        {
            throw new ArgumentException($"无效分辨率：{value}", nameof(value));
        }
        var fps = parts.Length == 2 && int.TryParse(parts[1].TrimEnd('f', 'p', 's'), out var parsedFps) ? parsedFps : 0;
        return new DeviceResolution(width, height, fps);
    }

    private void EnsureSelectedDevice()
    {
        if (string.IsNullOrWhiteSpace(_snapshot.SelectedDeviceId))
        {
            throw new InvalidOperationException("请先选择设备。");
        }
    }

    private void RaiseChanged() => Changed?.Invoke(this, _snapshot);
}
