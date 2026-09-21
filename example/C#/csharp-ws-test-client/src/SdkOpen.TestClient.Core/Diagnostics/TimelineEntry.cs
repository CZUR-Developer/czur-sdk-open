namespace SdkOpen.TestClient.Core.Diagnostics;

public sealed record TimelineEntry(DateTimeOffset Timestamp, string Category, string Summary, string? Payload = null)
{
    // 内部统一存 UTC；只有展示层转换本机时间，避免跨线程/跨机器比较时产生歧义。
    // Store UTC internally and convert only at display time to avoid timezone ambiguity.
    public string LocalTimestampDisplay => Timestamp.ToLocalTime().ToString("yyyy/MM/dd HH:mm:ss");
}
