using System.Net.WebSockets;
using System.Text;

namespace SdkOpen.TestClient.Core.Transport;

public sealed class VideoWebSocketClient : IAsyncDisposable
{
    private readonly ClientWebSocket _socket = new();
    private readonly CancellationTokenSource _lifetime = new();
    private Task? _receiveTask;

    public event EventHandler<string>? TextReceived;
    public event EventHandler<byte[]>? BinaryReceived;
    public event EventHandler<ConnectionClosedEventArgs>? ConnectionClosed;

    public bool IsConnected => _socket.State == WebSocketState.Open;

    public async Task ConnectAsync(Uri endpoint, string sessionToken, string streamId, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(endpoint);
        ArgumentException.ThrowIfNullOrWhiteSpace(sessionToken);
        ArgumentException.ThrowIfNullOrWhiteSpace(streamId);
        // sdk_open 的视频流握手约定把 session_token 和 stream_id 放在查询串中，命令通道仍使用内存凭据。
        // sdk_open requires session_token and stream_id in the video handshake query; the command channel keeps its in-memory credential.
        var connectionUri = BuildStreamUri(endpoint, sessionToken, streamId);
        await _socket.ConnectAsync(connectionUri, cancellationToken).ConfigureAwait(false);
        _receiveTask = ReceiveLoopAsync(_lifetime.Token);
    }

    public async Task CloseAsync(CancellationToken cancellationToken)
    {
        if (IsConnected)
        {
            await _socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, "Client closing video stream", cancellationToken).ConfigureAwait(false);
        }
    }

    public async ValueTask DisposeAsync()
    {
        _lifetime.Cancel();
        if (_receiveTask is not null)
        {
            try
            {
                await _receiveTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
        }

        _socket.Dispose();
        _lifetime.Dispose();
    }

    public static Uri BuildStreamUri(Uri endpoint, string sessionToken, string streamId)
    {
        ArgumentNullException.ThrowIfNull(endpoint);
        return new UriBuilder(endpoint)
        {
            Query = $"session_token={Uri.EscapeDataString(sessionToken)}&stream_id={Uri.EscapeDataString(streamId)}"
        }.Uri;
    }

    private async Task ReceiveLoopAsync(CancellationToken cancellationToken)
    {
        try
        {
            var buffer = new byte[16 * 1024];
            using var message = new MemoryStream();
            while (!cancellationToken.IsCancellationRequested)
            {
                var result = await _socket.ReceiveAsync(buffer, cancellationToken).ConfigureAwait(false);
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    ConnectionClosed?.Invoke(this, new ConnectionClosedEventArgs(_socket.CloseStatusDescription ?? "Server closed video WebSocket."));
                    return;
                }

                message.Write(buffer, 0, result.Count);
                if (!result.EndOfMessage)
                {
                    continue;
                }

                var bytes = message.ToArray();
                message.SetLength(0);
                // WebSocket 可能分片传输一帧，只有 EndOfMessage 到达后才向预览控件派发。
                // WebSocket frames may be fragmented; dispatch to the preview only at EndOfMessage.
                if (result.MessageType == WebSocketMessageType.Text)
                {
                    TextReceived?.Invoke(this, Encoding.UTF8.GetString(bytes));
                }
                else if (result.MessageType == WebSocketMessageType.Binary)
                {
                    BinaryReceived?.Invoke(this, bytes);
                }
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (WebSocketException exception)
        {
            ConnectionClosed?.Invoke(this, new ConnectionClosedEventArgs(exception.Message));
        }
    }
}
