using System.Net.WebSockets;
using System.Text;

namespace SdkOpen.TestClient.Core.Transport;

public sealed class ClientWebSocketCommandTransport : ICommandTransport
{
    private ClientWebSocket? _socket;
    private CancellationTokenSource? _lifetime;
    private Task? _receiveTask;
    private int _closed = 1;

    public bool IsConnected => _socket?.State == WebSocketState.Open;

    public event EventHandler<string>? MessageReceived;
    public event EventHandler<string>? Closed;

    public async Task ConnectAsync(Uri endpoint, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(endpoint);
        if (IsConnected)
        {
            throw new InvalidOperationException("Command WebSocket is already connected.");
        }

        await DisposeCurrentAsync().ConfigureAwait(false);
        var socket = new ClientWebSocket();
        var lifetime = new CancellationTokenSource();
        _socket = socket;
        _lifetime = lifetime;
        Interlocked.Exchange(ref _closed, 0);
        try
        {
            await socket.ConnectAsync(endpoint, cancellationToken).ConfigureAwait(false);
            _receiveTask = ReceiveLoopAsync(socket, lifetime.Token);
        }
        catch
        {
            await DisposeCurrentAsync().ConfigureAwait(false);
            throw;
        }
    }

    public Task SendAsync(string payload, CancellationToken cancellationToken)
    {
        var socket = _socket ?? throw new InvalidOperationException("Command WebSocket is not initialized.");
        return socket.SendAsync(Encoding.UTF8.GetBytes(payload), WebSocketMessageType.Text, true, cancellationToken);
    }

    public async Task CloseAsync(CancellationToken cancellationToken)
    {
        var socket = _socket;
        if (socket?.State == WebSocketState.Open)
        {
            await socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, "Client closing", cancellationToken).ConfigureAwait(false);
        }

        if (socket is not null)
        {
            RaiseClosed(socket, "Client closed command WebSocket.");
        }
    }

    public async ValueTask DisposeAsync() => await DisposeCurrentAsync().ConfigureAwait(false);

    private async Task DisposeCurrentAsync()
    {
        var socket = _socket;
        var lifetime = _lifetime;
        var receiveTask = _receiveTask;
        _socket = null;
        _lifetime = null;
        _receiveTask = null;
        lifetime?.Cancel();
        if (receiveTask is not null)
        {
            try
            {
                await receiveTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
        }

        socket?.Dispose();
        lifetime?.Dispose();
    }

    private async Task ReceiveLoopAsync(ClientWebSocket socket, CancellationToken cancellationToken)
    {
        try
        {
            var buffer = new byte[16 * 1024];
            using var message = new MemoryStream();
            while (!cancellationToken.IsCancellationRequested)
            {
                var result = await socket.ReceiveAsync(buffer, cancellationToken).ConfigureAwait(false);
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    RaiseClosed(socket, socket.CloseStatusDescription ?? "Server closed command WebSocket.");
                    return;
                }

                if (result.MessageType != WebSocketMessageType.Text)
                {
                    continue;
                }

                message.Write(buffer, 0, result.Count);
                if (!result.EndOfMessage)
                {
                    continue;
                }

                MessageReceived?.Invoke(this, Encoding.UTF8.GetString(message.GetBuffer(), 0, checked((int)message.Length)));
                message.SetLength(0);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (WebSocketException exception)
        {
            RaiseClosed(socket, exception.Message);
        }
    }

    private void RaiseClosed(ClientWebSocket socket, string reason)
    {
        if (ReferenceEquals(_socket, socket) && Interlocked.Exchange(ref _closed, 1) == 0)
        {
            Closed?.Invoke(this, reason);
        }
    }
}
