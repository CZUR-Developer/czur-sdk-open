using System.Net.Http.Headers;

namespace SdkOpen.TestClient.Core.Transport;

public static class AssetRequestFactory
{
    public static HttpRequestMessage Create(Uri assetUri, string sessionToken)
    {
        ArgumentNullException.ThrowIfNull(assetUri);
        ArgumentException.ThrowIfNullOrWhiteSpace(sessionToken);
        var request = new HttpRequestMessage(HttpMethod.Get, assetUri);
        // Asset 地址本身不携带 session_token，避免令牌出现在浏览器历史、日志或时间线中。
        // 下载请求复用当前内存会话的 Bearer 凭据，因此受保护的预览资源也能正常返回。
        // Do not put session_token in the Asset URL; use the in-memory Bearer credential for protected previews.
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", sessionToken);
        return request;
    }
}
