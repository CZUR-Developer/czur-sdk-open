using System.Collections.Concurrent;
using System.Text.Json;
using SdkOpen.TestClient.Core.Protocol;

namespace SdkOpen.TestClient.Core.Transport;

public sealed class CommandConnectionClosedException : Exception
{
    public CommandConnectionClosedException(string reason)
        : base(reason)
    {
    }
}

public sealed class ConnectionClosedEventArgs : EventArgs
{
    public ConnectionClosedEventArgs(string reason)
    {
        Reason = reason;
    }

    public string Reason { get; }
}

public sealed class CommandWebSocketClient : IAsyncDisposable
{
    private readonly ICommandTransport _transport;
    // request_id -> completion 用于把乱序到达的 WebSocket 响应匹配回原始调用。
    // request_id -> completion correlates out-of-order WebSocket responses with their callers.
    private readonly ConcurrentDictionary<string, TaskCompletionSource<CommandResponse>> _pending = new();
    private readonly JsonSerializerOptions _serializerOptions = new(JsonSerializerDefaults.Web);
    private int _closed;

    public CommandWebSocketClient(ICommandTransport transport)
    {
        _transport = transport;
        _transport.MessageReceived += OnMessageReceived;
        _transport.Closed += OnTransportClosed;
    }

    public bool IsConnected => _transport.IsConnected && Volatile.Read(ref _closed) == 0;

    public event EventHandler<CommandEvent>? EventReceived;
    public event EventHandler<ConnectionClosedEventArgs>? ConnectionClosed;

    public async Task ConnectAsync(Uri endpoint, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(endpoint);
        Interlocked.Exchange(ref _closed, 0);
        await _transport.ConnectAsync(endpoint, cancellationToken).ConfigureAwait(false);
    }

    public async Task<CommandResponse> SendAsync(CommandRequest request, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (!IsConnected)
        {
            throw new CommandConnectionClosedException("Command WebSocket is not connected.");
        }

        var completion = new TaskCompletionSource<CommandResponse>(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!_pending.TryAdd(request.RequestId, completion))
        {
            throw new InvalidOperationException($"Duplicate request_id: {request.RequestId}");
        }

        using var registration = cancellationToken.Register(() =>
        {
            // 取消只移除当前请求，不影响同一连接上的其他并发命令。
            // Cancellation removes only this request and does not affect other concurrent commands.
            if (_pending.TryRemove(request.RequestId, out var pending))
            {
                pending.TrySetCanceled(cancellationToken);
            }
        });

        try
        {
            var payload = JsonSerializer.Serialize(request, _serializerOptions);
            await _transport.SendAsync(payload, cancellationToken).ConfigureAwait(false);
            return await completion.Task.ConfigureAwait(false);
        }
        catch
        {
            _pending.TryRemove(request.RequestId, out _);
            throw;
        }
    }

    public async Task CloseAsync(CancellationToken cancellationToken)
    {
        FailPending("Command WebSocket was closed.");
        await _transport.CloseAsync(cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask DisposeAsync()
    {
        _transport.MessageReceived -= OnMessageReceived;
        _transport.Closed -= OnTransportClosed;
        FailPending("Command WebSocket was disposed.");
        await _transport.DisposeAsync().ConfigureAwait(false);
    }

    private void OnMessageReceived(object? sender, string payload)
    {
        try
        {
            using var document = JsonDocument.Parse(payload);
            var root = document.RootElement;
            if (root.TryGetProperty("event", out _))
            {
                // 事件没有 request_id，必须先于普通响应分流，否则会被误当成命令响应丢弃。
                // Events have no request_id and must be routed before responses or they would be discarded.
                var commandEvent = JsonSerializer.Deserialize<CommandEvent>(root, _serializerOptions);
                if (commandEvent is not null)
                {
                    EventReceived?.Invoke(this, commandEvent);
                }

                return;
            }

            var response = JsonSerializer.Deserialize<CommandResponse>(root, _serializerOptions);
            if (response is not null && _pending.TryRemove(response.RequestId, out var completion))
            {
                completion.TrySetResult(response);
            }
        }
        catch (JsonException)
        {
            // 单条乱码消息不能终止接收循环，也不能影响其他仍在等待的命令。
            // A malformed message must not stop the receive loop or affect unrelated pending commands.
        }
    }

    private void OnTransportClosed(object? sender, string reason)
    {
        FailPending(reason);
    }

    private void FailPending(string reason)
    {
        if (Interlocked.Exchange(ref _closed, 1) == 0)
        {
            foreach (var pending in _pending)
            {
                if (_pending.TryRemove(pending.Key, out var completion))
                {
                    completion.TrySetException(new CommandConnectionClosedException(reason));
                }
            }

            ConnectionClosed?.Invoke(this, new ConnectionClosedEventArgs(reason));
        }
    }
}
