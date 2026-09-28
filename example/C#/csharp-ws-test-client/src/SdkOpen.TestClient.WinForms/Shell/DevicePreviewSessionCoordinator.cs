using SdkOpen.TestClient.Core.Application;
using SdkOpen.TestClient.Core.Configuration;
using System.Text.Json;

namespace SdkOpen.TestClient.WinForms;

/// <summary>
/// Owns physical-device preview transitions for both pages.
/// 页面不再互相停止/打开对方的会话；所有设备所有权切换都通过这里串行完成。
/// </summary>
public sealed class DevicePreviewSessionCoordinator : IAsyncDisposable
{
    private enum PreviewOwner { None, Device, Capture }

    private readonly SdkOpenCoordinator _coordinator;
    private readonly EndpointProfileStore _profileStore;
    private readonly SharedVideoPreviewSession _devicePreview;
    private readonly SharedVideoPreviewSession _capturePreview;
    private readonly SemaphoreSlim _transitionGate = new(1, 1);
    private PreviewOwner _owner;
    private bool _disposing;

    public DevicePreviewSessionCoordinator(
        SdkOpenCoordinator coordinator,
        EndpointProfileStore profileStore,
        SharedVideoPreviewSession devicePreview,
        SharedVideoPreviewSession capturePreview)
    {
        _coordinator = coordinator;
        _profileStore = profileStore;
        _devicePreview = devicePreview;
        _capturePreview = capturePreview;
    }

    public async Task<VideoStartResult> StartCaptureAsync(
        CaptureFormOptions captureOptions,
        JsonElement pipeline,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(captureOptions);
        return await RunExclusiveAsync(async () =>
        {
            var deviceId = RequireSelectedDevice();
            await _devicePreview.StopAsync().ConfigureAwait(false);
            await _coordinator.CloseSelectedDeviceAsync(cancellationToken).ConfigureAwait(false);
            await _coordinator.SelectDeviceAsync(deviceId, cancellationToken).ConfigureAwait(false);
            _coordinator.DeviceWorkflow.PrepareCaptureAcquisitionResolution();
            await _coordinator.OpenSelectedDeviceAsync(cancellationToken).ConfigureAwait(false);

            // captureOptions is an immutable snapshot collected by the page on
            // the UI thread. Never call back into WinForms controls from here.
            // captureOptions 是页面在 UI 线程采集的不可变快照；此处不得回调访问 WinForms 控件。
            var snapshot = _coordinator.DeviceWorkflow.Snapshot;
            var profile = CaptureProfileBuilder.Build(captureOptions, snapshot.SelectedResolution, snapshot.SelectedDeviceId);
            var started = await _coordinator.StartCapturePreviewAsync(profile, pipeline, cancellationToken).ConfigureAwait(false);
            var endpoint = await _profileStore.LoadAsync(cancellationToken).ConfigureAwait(false);
            var token = _coordinator.Session.SessionToken
                ?? throw new InvalidOperationException("SDK Open session expired; connect again.");
            try
            {
                await _capturePreview.StartAsync(endpoint, token, started.StreamId, cancellationToken).ConfigureAwait(false);
                _owner = PreviewOwner.Capture;
            }
            catch
            {
                await _coordinator.CloseSelectedDeviceAsync(CancellationToken.None).ConfigureAwait(false);
                throw;
            }

            return started;
        }, cancellationToken).ConfigureAwait(false);
    }

    public Task StopCaptureAsync(CancellationToken cancellationToken) => RunExclusiveAsync(async () =>
    {
        if (_owner != PreviewOwner.Capture && _capturePreview.StreamId is null) return;
        await _capturePreview.StopAsync().ConfigureAwait(false);
        await _coordinator.CloseSelectedDeviceAsync(cancellationToken).ConfigureAwait(false);
        _owner = PreviewOwner.None;
    }, cancellationToken);

    public async Task<VideoStartResult> StartDevicePreviewAsync(CancellationToken cancellationToken)
    {
        return await RunExclusiveAsync(async () =>
        {
            var deviceId = RequireSelectedDevice();
            await _capturePreview.StopAsync().ConfigureAwait(false);
            await _coordinator.CloseSelectedDeviceAsync(cancellationToken).ConfigureAwait(false);
            await _coordinator.SelectDeviceAsync(deviceId, cancellationToken).ConfigureAwait(false);
            await _coordinator.OpenSelectedDeviceAsync(cancellationToken).ConfigureAwait(false);

            var started = await _coordinator.StartPreviewAsync(cancellationToken).ConfigureAwait(false);
            var endpoint = await _profileStore.LoadAsync(cancellationToken).ConfigureAwait(false);
            var token = _coordinator.Session.SessionToken
                ?? throw new InvalidOperationException("SDK Open session expired; connect again.");
            try
            {
                await _devicePreview.StartAsync(endpoint, token, started.StreamId, cancellationToken).ConfigureAwait(false);
                _owner = PreviewOwner.Device;
            }
            catch
            {
                await _coordinator.CloseSelectedDeviceAsync(CancellationToken.None).ConfigureAwait(false);
                throw;
            }

            return started;
        }, cancellationToken).ConfigureAwait(false);
    }

    public Task StopDevicePreviewAsync(CancellationToken cancellationToken) => RunExclusiveAsync(async () =>
    {
        if (_owner != PreviewOwner.Device && _devicePreview.StreamId is null) return;
        await _devicePreview.StopAsync().ConfigureAwait(false);
        await _coordinator.CloseSelectedDeviceAsync(cancellationToken).ConfigureAwait(false);
        _owner = PreviewOwner.None;
    }, cancellationToken);

    public async ValueTask DisposeAsync()
    {
        if (_disposing) return;
        _disposing = true;
        try
        {
            await RunExclusiveAsync(async () =>
            {
                await _capturePreview.StopAsync().ConfigureAwait(false);
                await _devicePreview.StopAsync().ConfigureAwait(false);
                try
                {
                    await _coordinator.CloseSelectedDeviceAsync(CancellationToken.None).ConfigureAwait(false);
                }
                catch
                {
                    // Application shutdown must still release local WebSocket sessions
                    // when the command channel has already been disconnected.
                }
                _owner = PreviewOwner.None;
            }, CancellationToken.None).ConfigureAwait(false);
        }
        finally
        {
            _transitionGate.Dispose();
        }
    }

    private string RequireSelectedDevice() =>
        _coordinator.DeviceWorkflow.Snapshot.SelectedDeviceId
        ?? throw new InvalidOperationException("Select a device before starting preview.");

    private async Task RunExclusiveAsync(Func<Task> action, CancellationToken cancellationToken)
    {
        await _transitionGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try { await action().ConfigureAwait(false); }
        finally { _transitionGate.Release(); }
    }

    private async Task<T> RunExclusiveAsync<T>(Func<Task<T>> action, CancellationToken cancellationToken)
    {
        await _transitionGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try { return await action().ConfigureAwait(false); }
        finally { _transitionGate.Release(); }
    }
}
