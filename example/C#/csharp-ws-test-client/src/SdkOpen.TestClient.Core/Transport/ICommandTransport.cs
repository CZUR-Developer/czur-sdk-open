namespace SdkOpen.TestClient.Core.Transport;

public interface ICommandTransport : IAsyncDisposable
{
    bool IsConnected { get; }

    event EventHandler<string>? MessageReceived;
    event EventHandler<string>? Closed;

    Task ConnectAsync(Uri endpoint, CancellationToken cancellationToken);
    Task SendAsync(string payload, CancellationToken cancellationToken);
    Task CloseAsync(CancellationToken cancellationToken);
}
