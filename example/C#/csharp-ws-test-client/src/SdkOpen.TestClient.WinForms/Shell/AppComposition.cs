using SdkOpen.TestClient.Core.Application;
using SdkOpen.TestClient.Core.Configuration;
using SdkOpen.TestClient.Core.Protocol;
using SdkOpen.TestClient.Core.Transport;

namespace SdkOpen.TestClient.WinForms;

public sealed class AppComposition : IDisposable
{
    private readonly HttpClient _httpClient = new();
    private readonly CommandWebSocketClient _commandClient = new(new ClientWebSocketCommandTransport());

    public AppComposition()
    {
        Coordinator = new SdkOpenCoordinator(new HealthCheckClient(_httpClient), _commandClient, new CommandRequestFactory("csharp-ws-test-client", "2.0.0"));
        ProfileStore = new EndpointProfileStore();
        SecretStore = new DpapiSecretStore();
        Catalog = new CommandCatalog();
        VideoPreview = new SharedVideoPreviewSession();
        // Capture owns a separate video session so capture overlays/profile changes
        // never mutate the general Device & Video preview surface.
        // 采集页独立维护视频会话，避免裁切框和采集 profile 污染“设备与视频”页面。
        CaptureVideoPreview = new SharedVideoPreviewSession();
        DevicePreviewSessions = new DevicePreviewSessionCoordinator(Coordinator, ProfileStore, VideoPreview, CaptureVideoPreview);
    }

    public SdkOpenCoordinator Coordinator { get; }
    public EndpointProfileStore ProfileStore { get; }
    public DpapiSecretStore SecretStore { get; }
    public CommandCatalog Catalog { get; }
    public SharedVideoPreviewSession VideoPreview { get; }
    public SharedVideoPreviewSession CaptureVideoPreview { get; }
    public DevicePreviewSessionCoordinator DevicePreviewSessions { get; }
    public EndpointProfile ActiveProfile { get; private set; } = EndpointProfile.Default;

    public void SetActiveProfile(EndpointProfile profile)
    {
        ActiveProfile = profile ?? throw new ArgumentNullException(nameof(profile));
    }

    public async Task<byte[]> DownloadAssetAsync(string assetUrl, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(assetUrl);
        var sessionToken = Coordinator.Session.SessionToken;
        if (string.IsNullOrWhiteSpace(sessionToken))
        {
            throw new InvalidOperationException("预览需要有效的 SDK Open 会话；请重新连接后重试。");
        }
        var uri = Uri.TryCreate(assetUrl, UriKind.Absolute, out var absolute)
            ? absolute
            : new Uri(ActiveProfile.AssetEndpoint, assetUrl);
        using var request = AssetRequestFactory.Create(uri, sessionToken);
        using var response = await _httpClient.SendAsync(request, cancellationToken).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadAsByteArrayAsync(cancellationToken).ConfigureAwait(false);
    }

    public async Task<string> UploadImageAsync(string filePath, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(filePath);
        var sessionToken = Coordinator.Session.SessionToken;
        if (string.IsNullOrWhiteSpace(sessionToken))
        {
            throw new InvalidOperationException("上传图片需要有效的 SDK Open 会话；请重新连接后重试。");
        }

        // 上传请求只在内存中附加 Bearer 会话，不把令牌写入 URL 或持久化日志。
        // Attach the Bearer session in memory only; never place it in the URL or persisted logs.
        await using var stream = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.Read, 81920, useAsync: true);
        using var request = ImageUploadRequestFactory.Create(
            ActiveProfile.AssetEndpoint,
            sessionToken,
            stream,
            Path.GetFileName(filePath),
            ImageUploadRequestFactory.ContentTypeFromFileName(filePath));
        using var response = await _httpClient.SendAsync(request, cancellationToken).ConfigureAwait(false);
        var responseJson = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
        if (!response.IsSuccessStatusCode)
        {
            throw new HttpRequestException($"Image upload failed: {(int)response.StatusCode} {response.ReasonPhrase}. {responseJson}".Trim());
        }
        return ImageUploadRequestFactory.ParseUploadId(responseJson);
    }

    public void Dispose()
    {
        DevicePreviewSessions.DisposeAsync().AsTask().GetAwaiter().GetResult();
        _commandClient.DisposeAsync().AsTask().GetAwaiter().GetResult();
        VideoPreview.DisposeAsync().AsTask().GetAwaiter().GetResult();
        CaptureVideoPreview.DisposeAsync().AsTask().GetAwaiter().GetResult();
        _httpClient.Dispose();
    }
}
