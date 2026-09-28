using System.Text.Json;
using SdkOpen.TestClient.Core.Application;
using SdkOpen.TestClient.Core.Configuration;
using SdkOpen.TestClient.Core.Protocol;
using SdkOpen.TestClient.Core.Transport;

namespace SdkOpen.TestClient.WinForms;

/// <summary>
/// Owns one independent video WebSocket session.
/// 每个页面会话各自维护一条视频 WebSocket；设备预览与采集预览互不共享生命周期。
/// </summary>
public sealed class SharedVideoPreviewSession : IAsyncDisposable
{
    private readonly object _sync = new();
    private VideoWebSocketClient? _client;
    private VideoPreviewPipeline? _pipeline;
    private bool _disposing;

    public bool IsConnected => _client?.IsConnected == true;
    public string? StreamId { get; private set; }
    public VideoPreviewStatistics Statistics => _pipeline?.Statistics ?? new VideoPreviewStatistics(0, 0, 0, 0, null);
    public event EventHandler? FrameAvailable;
    public event EventHandler<ConnectionClosedEventArgs>? ConnectionClosed;

    public async Task StartAsync(EndpointProfile profile, string sessionToken, string streamId, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(profile);
        ArgumentException.ThrowIfNullOrWhiteSpace(sessionToken);
        ArgumentException.ThrowIfNullOrWhiteSpace(streamId);
        await StopAsync().ConfigureAwait(false);

        var pipeline = new VideoPreviewPipeline(streamId);
        var client = new VideoWebSocketClient();
        client.TextReceived += OnTextReceived;
        client.BinaryReceived += (_, bytes) =>
        {
            pipeline.AcceptBinary(bytes);
            FrameAvailable?.Invoke(this, EventArgs.Empty);
        };
        client.ConnectionClosed += (_, args) => ConnectionClosed?.Invoke(this, args);
        lock (_sync)
        {
            _pipeline = pipeline;
            _client = client;
            StreamId = streamId;
        }

        try
        {
            await client.ConnectAsync(profile.VideoEndpoint, sessionToken, streamId, cancellationToken).ConfigureAwait(false);
        }
        catch
        {
            await StopAsync().ConfigureAwait(false);
            throw;
        }
    }

    /// <summary>
    /// Consumes the newest pending frame and records it as rendered.
    /// 消费最新待显示帧并增加渲染计数；未被 UI 及时消费而被覆盖的帧才计为丢帧。
    /// </summary>
    public RenderableFrame? TakeLatestFrame() => _pipeline?.TakeLatestFrame();

    public async Task StopAsync()
    {
        VideoWebSocketClient? client;
        lock (_sync)
        {
            client = _client;
            _client = null;
            _pipeline = null;
            StreamId = null;
        }
        if (client is not null)
        {
            client.TextReceived -= OnTextReceived;
            await client.CloseAsync(CancellationToken.None).ConfigureAwait(false);
            await client.DisposeAsync().ConfigureAwait(false);
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (_disposing) return;
        _disposing = true;
        await StopAsync().ConfigureAwait(false);
    }

    private void OnTextReceived(object? sender, string text)
    {
        try
        {
            using var message = JsonDocument.Parse(text);
            var root = message.RootElement;
            if (!root.TryGetProperty("event", out var eventName) || !string.Equals(eventName.GetString(), "stream.frame_meta", StringComparison.Ordinal)) return;
            var payload = root.TryGetProperty("payload", out var value) ? value : root;
            var metadata = JsonSerializer.Deserialize<VideoFrameMeta>(payload);
            if (metadata is not null) _pipeline?.AcceptMeta(metadata);
        }
        catch (JsonException)
        {
            // A malformed frame metadata message is ignored; the command timeline remains the diagnostic source.
            // 异常的帧元数据直接丢弃，详细诊断仍通过命令时间线查看。
        }
    }
}
