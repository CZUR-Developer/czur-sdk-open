using System.Text.Json;
using SdkOpen.TestClient.Core.Application;
using SdkOpen.TestClient.Core.Protocol;
using SdkOpen.TestClient.Core.Transport;

namespace SdkOpen.TestClient.WinForms;

/// <summary>设备发现、默认分辨率选择、设备生命周期以及实时预览。</summary>
public sealed class DeviceVideoPage : UserControl, ILocalizablePage, IPageLifecycle
{
    private readonly AppComposition _composition;
    private readonly Action<string, string> _showProtocol;
    private readonly VideoPreviewControl _preview = new() { Dock = DockStyle.Fill };
    private readonly DataGridView _devices = new() { Dock = DockStyle.Fill, AutoGenerateColumns = false, ReadOnly = true, AllowUserToAddRows = false, AllowUserToDeleteRows = false, SelectionMode = DataGridViewSelectionMode.FullRowSelect, RowHeadersVisible = false };
    private readonly DataGridViewTextBoxColumn _deviceIdColumn = new() { DataPropertyName = nameof(DeviceSummary.DeviceId), Width = 180 };
    private readonly DataGridViewTextBoxColumn _deviceNameColumn = new() { DataPropertyName = nameof(DeviceSummary.DisplayName), Width = 170 };
    private readonly DataGridViewTextBoxColumn _deviceModelColumn = new() { DataPropertyName = nameof(DeviceSummary.Model), Width = 100 };
    private readonly DataGridViewTextBoxColumn _deviceStatusColumn = new() { DataPropertyName = nameof(DeviceSummary.Status), Width = 90 };
    private readonly ComboBox _deviceSelector = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 230 };
    private readonly ComboBox _resolutionSelector = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 220 };
    private readonly Label _status = new() { AutoSize = true, MaximumSize = new Size(760, 0) };
    private readonly Label _state = new() { AutoSize = true, MaximumSize = new Size(760, 0) };
    private readonly Button _open = new() { AutoSize = true };
    private readonly Button _close = new() { AutoSize = true };
    private readonly Button _start = new() { AutoSize = true };
    private readonly Button _stop = new() { AutoSize = true };
    private readonly Button _refreshDevices = new() { AutoSize = true };
    private readonly Label _deviceLabel = new() { AutoSize = true, Padding = new Padding(0, 7, 0, 0) };
    private readonly Label _resolutionLabel = new() { AutoSize = true, Padding = new Padding(6, 7, 0, 0) };
    private readonly System.Windows.Forms.Timer _renderTimer = new() { Interval = 33 };
    private bool _synchronizing;
    private bool _disposing;
    private bool _previewSessionChanging;
    private string? _previewSessionError;
    private readonly PageSessionActivation _pageActivation = new();
    private UiLanguage _language = UiLanguage.Chinese;

    public DeviceVideoPage(AppComposition composition, Action<string, string> showProtocol)
    {
        _composition = composition;
        _showProtocol = showProtocol;
        _deviceIdColumn.HeaderText = "设备 ID";
        _deviceNameColumn.HeaderText = "名称";
        _deviceModelColumn.HeaderText = "型号";
        _deviceStatusColumn.HeaderText = "状态";
        _devices.Columns.AddRange([_deviceIdColumn, _deviceNameColumn, _deviceModelColumn, _deviceStatusColumn]);
        _devices.SelectionChanged += async (_, _) => { if (!_synchronizing && _devices.CurrentRow?.DataBoundItem is DeviceSummary item) await SelectDeviceAsync(item.DeviceId); };
        _deviceSelector.SelectedIndexChanged += async (_, _) => { if (!_synchronizing && _deviceSelector.SelectedItem is DeviceSummary item) await SelectDeviceAsync(item.DeviceId); };
        _resolutionSelector.SelectedIndexChanged += (_, _) => { if (!_synchronizing && _resolutionSelector.SelectedItem is DeviceResolution item) _composition.Coordinator.SelectResolution(item.Key); };
        _open.Click += async (_, _) => await OpenAsync();
        _close.Click += async (_, _) => await CloseAsync();
        _start.Click += async (_, _) => await StartAsync();
        _stop.Click += async (_, _) => await StopAsync();
        _refreshDevices.Click += async (_, _) => await RefreshDevicesAsync();
        _renderTimer.Tick += (_, _) => RenderLatestFrame();
        _composition.Coordinator.DeviceWorkflow.Changed += OnWorkflowChanged;
        _composition.VideoPreview.ConnectionClosed += OnVideoConnectionClosed;
        BuildLayout();
        ApplyLanguage(_language);
        Synchronize(_composition.Coordinator.DeviceWorkflow.Snapshot);
    }

    private void BuildLayout()
    {
        var actions = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Top, Padding = new Padding(8) };
        actions.Controls.AddRange([_deviceLabel, _deviceSelector, _resolutionLabel, _resolutionSelector, _refreshDevices, _open, _close, _start, _stop]);
        var information = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Top, FlowDirection = FlowDirection.TopDown, Padding = new Padding(8) };
        information.Controls.AddRange([_state, _status]);
        var split = new SplitContainer { Dock = DockStyle.Fill, Orientation = Orientation.Horizontal, SplitterDistance = 230 };
        split.Panel1.Controls.Add(_devices);
        split.Panel2.Controls.Add(_preview);
        Controls.Add(split);
        Controls.Add(information);
        Controls.Add(actions);
    }

    public void ApplyLanguage(UiLanguage language)
    {
        _language = language;
        var en = language == UiLanguage.English;
        _deviceLabel.Text = en ? "Device:" : "设备：";
        _resolutionLabel.Text = en ? "Resolution:" : "分辨率：";
        _refreshDevices.Text = en ? "Refresh Devices" : "刷新设备";
        _open.Text = en ? "Open Device" : "打开设备";
        _close.Text = en ? "Close Device" : "关闭设备";
        _start.Text = en ? "Start Preview" : "启动预览";
        _stop.Text = en ? "Stop Preview" : "停止预览";
        _deviceIdColumn.HeaderText = en ? "Device ID" : "设备 ID";
        _deviceNameColumn.HeaderText = en ? "Name" : "名称";
        _deviceModelColumn.HeaderText = en ? "Model" : "型号";
        _deviceStatusColumn.HeaderText = en ? "Status" : "状态";
        _status.Text = en ? "Ready." : "就绪。";
        _preview.ApplyLanguage(language);
        Synchronize(_composition.Coordinator.DeviceWorkflow.Snapshot);
    }

    private async Task RefreshDevicesAsync() => await RunAsync("device.list", async () =>
    {
        var devices = await _composition.Coordinator.RefreshDevicesAsync(CancellationToken.None);
        _showProtocol(UiText.FormOperation(_language, "device.list"), JsonSerializer.Serialize(devices));
        _status.Text = _language == UiLanguage.English ? $"Loaded {devices.Count} device(s)." : $"已读取 {devices.Count} 个设备。";
    });

    private async Task SelectDeviceAsync(string deviceId)
    {
        if (string.Equals(_composition.Coordinator.DeviceWorkflow.Snapshot.SelectedDeviceId, deviceId, StringComparison.Ordinal)) return;
        await RunAsync("device.get", async () =>
        {
            var snapshot = await _composition.Coordinator.SelectDeviceAsync(deviceId, CancellationToken.None);
            _showProtocol(UiText.FormOperation(_language, "device.get"), JsonSerializer.Serialize(snapshot));
            _status.Text = _language == UiLanguage.English ? "Device details loaded; the default resolution is selected automatically." : "已读取设备详情，已自动选中设备默认分辨率。";
        });
    }

    private async Task OpenAsync() => await RunAsync("device.open", async () =>
    {
        var response = await _composition.Coordinator.OpenSelectedDeviceAsync(CancellationToken.None);
        _showProtocol(UiText.FormOperation(_language, "device.open"), JsonSerializer.Serialize(response));
        _status.Text = _language == UiLanguage.English ? "Device is open; start preview or go to Capture." : "设备已打开，可以启动预览或进入采集页。";
    });

    private async Task StartAsync() => await RunAsync("video.start", async () =>
    {
        var started = await _composition.DevicePreviewSessions.StartDevicePreviewAsync(CancellationToken.None);
        _renderTimer.Start();
        _showProtocol(UiText.FormOperation(_language, "video.start"), JsonSerializer.Serialize(started.Response));
        _status.Text = _language == UiLanguage.English ? $"Preview connected (stream_id: {started.StreamId})." : $"预览已连接（stream_id: {started.StreamId}）。";
    });

    private async Task StopAsync() => await RunAsync("video.stop", async () =>
    {
        await _composition.DevicePreviewSessions.StopDevicePreviewAsync(CancellationToken.None);
        _renderTimer.Stop();
        _showProtocol(UiText.FormOperation(_language, "video.stop"), "{}");
        _status.Text = _language == UiLanguage.English ? "Preview stopped." : "预览已停止。";
    });

    private async Task CloseAsync() => await RunAsync("device.close", async () =>
    {
        await _composition.DevicePreviewSessions.StopDevicePreviewAsync(CancellationToken.None);
        _renderTimer.Stop();
        _showProtocol(UiText.FormOperation(_language, "device.close"), "{}");
        _status.Text = _language == UiLanguage.English ? "Device closed." : "设备已关闭。";
    });

    private async Task RunAsync(string action, Func<Task> work)
    {
        try { await work(); }
        catch (Exception exception) { _status.Text = _language == UiLanguage.English ? $"{action} failed: {exception.Message}" : $"{action} 失败：{exception.Message}"; }
    }

    private void OnWorkflowChanged(object? sender, DeviceWorkflowSnapshot snapshot) => BeginSafe(() =>
    {
        Synchronize(snapshot);
    });

    private void Synchronize(DeviceWorkflowSnapshot snapshot)
    {
        _synchronizing = true;
        try
        {
            _devices.DataSource = snapshot.Devices.ToList();
            _deviceSelector.DataSource = snapshot.Devices.ToList();
            _deviceSelector.DisplayMember = nameof(DeviceSummary.DisplayName);
            _resolutionSelector.DataSource = snapshot.Resolutions.ToList();
            _resolutionSelector.DisplayMember = nameof(DeviceResolution.DisplayLabel);
            _deviceSelector.SelectedItem = snapshot.Devices.FirstOrDefault(item => item.DeviceId == snapshot.SelectedDeviceId);
            _resolutionSelector.SelectedItem = snapshot.Resolutions.FirstOrDefault(item => item.Key == snapshot.SelectedResolutionKey);
            _open.Enabled = snapshot.CanOpen;
            _close.Enabled = snapshot.IsOpened || snapshot.CanStopPreview;
            _start.Enabled = snapshot.CanStartPreview;
            _stop.Enabled = snapshot.CanStopPreview;
            _state.Text = _language == UiLanguage.English
                ? $"Device: {snapshot.SelectedDeviceId ?? "Not selected"}; State: {(snapshot.IsOpened ? "Open" : "Closed")}; Preview: {snapshot.StreamId ?? "Stopped"}"
                : $"当前设备：{snapshot.SelectedDeviceId ?? "未选择"}；状态：{(snapshot.IsOpened ? "已打开" : "未打开")}；预览：{snapshot.StreamId ?? "未启动"}";
            if (_composition.VideoPreview.StreamId is not null && !string.Equals(_composition.VideoPreview.StreamId, snapshot.StreamId, StringComparison.Ordinal)) _ = _composition.VideoPreview.StopAsync();
        }
        finally { _synchronizing = false; }
    }

    private void RenderLatestFrame()
    {
        var frame = _composition.VideoPreview.TakeLatestFrame();
        if (frame is not null) _preview.ShowFrame(frame, _composition.VideoPreview.Statistics);
    }

    private void OnVideoConnectionClosed(object? sender, ConnectionClosedEventArgs args) => BeginSafe(() => _status.Text = _language == UiLanguage.English ? $"Video connection closed: {args.Reason}" : $"视频连接已关闭：{args.Reason}");

    private async Task EnsurePreviewSessionAsync()
    {
        if (_disposing || _previewSessionChanging)
        {
            return;
        }

        var selectedDeviceId = _composition.Coordinator.DeviceWorkflow.Snapshot.SelectedDeviceId;
        if (string.IsNullOrWhiteSpace(selectedDeviceId)) return;
        if (!_pageActivation.TryRequestStart(_composition.VideoPreview.StreamId is not null)) return;

        _previewSessionChanging = true;
        _previewSessionError = null;
        try
        {
            var started = await _composition.DevicePreviewSessions.StartDevicePreviewAsync(CancellationToken.None).ConfigureAwait(true);
            _renderTimer.Start();
            _status.Text = _language == UiLanguage.English
                ? $"Preview connected (stream_id: {started.StreamId})."
                : $"预览已连接（stream_id: {started.StreamId}）。";
        }
        catch (Exception exception)
        {
            _previewSessionError = _language == UiLanguage.English
                ? $"Unable to restore preview: {exception.Message}"
                : $"无法恢复预览：{exception.Message}";
            BeginSafe(() => _status.Text = _previewSessionError);
        }
        finally
        {
            _previewSessionChanging = false;
        }
    }

    private void BeginSafe(Action action)
    {
        if (_disposing || IsDisposed || !IsHandleCreated) return;
        if (InvokeRequired) BeginInvoke(action); else action();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _disposing = true;
            _renderTimer.Stop();
            _composition.Coordinator.DeviceWorkflow.Changed -= OnWorkflowChanged;
            _composition.VideoPreview.ConnectionClosed -= OnVideoConnectionClosed;
        }
        base.Dispose(disposing);
    }

    public void ActivatePage()
    {
        if (_disposing) return;
        _pageActivation.Activate();
        Synchronize(_composition.Coordinator.DeviceWorkflow.Snapshot);
        _ = EnsurePreviewSessionAsync();
    }

    public void DeactivatePage()
    {
        _pageActivation.Deactivate();
        _renderTimer.Stop();
    }
}
