using System.Diagnostics;
using System.Net;

namespace SdkOpen.TestClient.Core.Transport;

public sealed record HealthCheckResult(bool IsReachable, HttpStatusCode? StatusCode, string Body, TimeSpan Elapsed, string? Error);

public interface IHealthCheckClient
{
    Task<HealthCheckResult> CheckAsync(Uri healthEndpoint, CancellationToken cancellationToken);
}

public sealed class HealthCheckClient : IHealthCheckClient
{
    private readonly HttpClient _httpClient;

    public HealthCheckClient(HttpClient httpClient)
    {
        _httpClient = httpClient;
    }

    public async Task<HealthCheckResult> CheckAsync(Uri healthEndpoint, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(healthEndpoint);
        // health 是连接流程的第一步；失败时不建立 WebSocket，错误会直接显示在连接页。
        // Health is the first connection step; on failure no WebSocket is opened and the page shows the error.
        var stopwatch = Stopwatch.StartNew();
        try
        {
            using var response = await _httpClient.GetAsync(healthEndpoint, cancellationToken).ConfigureAwait(false);
            var body = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
            return new HealthCheckResult(response.IsSuccessStatusCode, response.StatusCode, body, stopwatch.Elapsed, null);
        }
        catch (HttpRequestException exception)
        {
            return new HealthCheckResult(false, null, string.Empty, stopwatch.Elapsed, exception.Message);
        }
        catch (TaskCanceledException exception) when (!cancellationToken.IsCancellationRequested)
        {
            return new HealthCheckResult(false, null, string.Empty, stopwatch.Elapsed, exception.Message);
        }
    }
}
