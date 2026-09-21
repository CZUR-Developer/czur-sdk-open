namespace SdkOpen.TestClient.Core.Configuration;

public sealed record EndpointProfile(
    string Name,
    Uri HealthEndpoint,
    Uri CommandEndpoint,
    Uri VideoEndpoint,
    Uri AssetEndpoint,
    string? ApiKeyAlias = null)
{
    public static EndpointProfile Default { get; } = new(
        "本机 SDK Open",
        new Uri("http://127.0.0.1:17080/healthz"),
        new Uri("ws://127.0.0.1:17090"),
        new Uri("ws://127.0.0.1:17091"),
        new Uri("http://127.0.0.1:17082"));
}
