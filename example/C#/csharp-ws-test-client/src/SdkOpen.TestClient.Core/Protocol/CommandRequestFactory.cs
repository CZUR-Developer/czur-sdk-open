using System.Text.Json;

namespace SdkOpen.TestClient.Core.Protocol;

public sealed class CommandRequestFactory
{
    private readonly string _source;
    private readonly string _protocolVersion;
    private long _sequence;

    public CommandRequestFactory(string source, string protocolVersion)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(source);
        ArgumentException.ThrowIfNullOrWhiteSpace(protocolVersion);
        _source = source;
        _protocolVersion = protocolVersion;
    }

    public CommandRequest Create(string method, JsonElement parameters)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(method);

        return new CommandRequest(
            $"req-{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds():x}-{Interlocked.Increment(ref _sequence):x}",
            method,
            parameters.Clone(),
            new ClientMetadata(_source, _protocolVersion, $"trc-{Guid.NewGuid():N}"));
    }
}
