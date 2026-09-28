using SdkOpen.TestClient.Core.Diagnostics;

namespace SdkOpen.TestClient.WinForms;

public static class TimelineDetailFormatter
{
    public static string FormatPayload(TimelineEntry entry)
    {
        ArgumentNullException.ThrowIfNull(entry);
        // Payload 已在写入时间线前完成脱敏，这里只负责完整显示，不做截断或重新序列化。
        // Payload is already redacted before entering the timeline; display it completely without truncation or reserialization.
        return string.IsNullOrWhiteSpace(entry.Payload) ? "(no payload)" : entry.Payload;
    }
}
