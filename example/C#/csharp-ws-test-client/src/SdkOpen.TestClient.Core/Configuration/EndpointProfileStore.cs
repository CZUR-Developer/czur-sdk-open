using System.Text.Json;

namespace SdkOpen.TestClient.Core.Configuration;

public sealed class EndpointProfileStore
{
    private readonly string _profilePath;
    private readonly JsonSerializerOptions _jsonOptions = new(JsonSerializerDefaults.Web) { WriteIndented = true };

    public EndpointProfileStore(string? applicationDirectory = null)
    {
        var root = applicationDirectory ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "CZUR",
            "SdkOpenTestClient");
        _profilePath = Path.Combine(root, "profile.json");
    }

    public async Task<EndpointProfile> LoadAsync(CancellationToken cancellationToken)
    {
        if (!File.Exists(_profilePath))
        {
            return EndpointProfile.Default;
        }

        await using var stream = File.OpenRead(_profilePath);
        var document = await JsonSerializer.DeserializeAsync<EndpointProfileDocument>(stream, _jsonOptions, cancellationToken).ConfigureAwait(false);
        return document?.ToProfile() ?? EndpointProfile.Default;
    }

    public async Task SaveAsync(EndpointProfile profile, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(profile);
        var directory = Path.GetDirectoryName(_profilePath)!;
        Directory.CreateDirectory(directory);
        var temporary = $"{_profilePath}.{Guid.NewGuid():N}.tmp";
        var document = EndpointProfileDocument.FromProfile(profile);
        await File.WriteAllTextAsync(temporary, JsonSerializer.Serialize(document, _jsonOptions), cancellationToken).ConfigureAwait(false);
        File.Move(temporary, _profilePath, true);
    }

    private sealed record EndpointProfileDocument(
        string Name,
        string HealthEndpoint,
        string CommandEndpoint,
        string VideoEndpoint,
        string AssetEndpoint,
        string? ApiKeyAlias)
    {
        public EndpointProfile ToProfile() => new(Name, new Uri(HealthEndpoint), new Uri(CommandEndpoint), new Uri(VideoEndpoint), new Uri(AssetEndpoint), ApiKeyAlias);

        public static EndpointProfileDocument FromProfile(EndpointProfile profile) => new(
            profile.Name,
            profile.HealthEndpoint.AbsoluteUri,
            profile.CommandEndpoint.AbsoluteUri,
            profile.VideoEndpoint.AbsoluteUri,
            profile.AssetEndpoint.AbsoluteUri,
            profile.ApiKeyAlias);
    }
}
