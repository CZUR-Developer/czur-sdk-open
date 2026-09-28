namespace SdkOpen.TestClient.WinForms;

public enum UiLanguage
{
    Chinese,
    English
}

public enum UiTextKey
{
    ApplicationTitle,
    ChineseLanguageButton,
    RefreshTimeline,
    Connected,
    Disconnected,
    ConnectionPage,
    QuickStartPage,
    DeviceVideoPage,
    CapturePage,
    ImageProcessingPage,
    ImageEnhancementPage,
    OcrPage,
    FileConversionPage,
    SanePage,
    TwainPage,
    StoragePage,
    RawJsonPage,
    TestClientLabel
}

public static class UiText
{
    // Keep user-facing shell strings in one table so language switching does not depend on localized control text.
    // 将主壳层文案集中管理，切换语言时不依赖控件当前显示的本地化文本。
    private static readonly IReadOnlyDictionary<UiTextKey, (string Chinese, string English)> Values =
        new Dictionary<UiTextKey, (string Chinese, string English)>
        {
            [UiTextKey.ApplicationTitle] = ("SDK Open WebSocket 测试台", "SDK Open WebSocket Test Client"),
            [UiTextKey.ChineseLanguageButton] = ("English", "中文"),
            [UiTextKey.RefreshTimeline] = ("刷新时间线", "Refresh Timeline"),
            [UiTextKey.Connected] = ("已连接", "Connected"),
            [UiTextKey.Disconnected] = ("未连接", "Disconnected"),
            [UiTextKey.ConnectionPage] = ("连接与认证", "Connection & Auth"),
            [UiTextKey.QuickStartPage] = ("快速开始", "Quick Start"),
            [UiTextKey.DeviceVideoPage] = ("设备与视频", "Device & Video"),
            [UiTextKey.CapturePage] = ("采集", "Capture"),
            [UiTextKey.ImageProcessingPage] = ("图像处理", "Image Processing"),
            [UiTextKey.ImageEnhancementPage] = ("图像增强", "Image Enhancement"),
            [UiTextKey.OcrPage] = ("OCR", "OCR"),
            [UiTextKey.FileConversionPage] = ("文件转换", "File Conversion"),
            [UiTextKey.SanePage] = ("SANE", "SANE"),
            [UiTextKey.TwainPage] = ("TWAIN", "TWAIN"),
            [UiTextKey.StoragePage] = ("存储", "Storage"),
            [UiTextKey.RawJsonPage] = ("原始 JSON 命令", "Raw JSON Command"),
            [UiTextKey.TestClientLabel] = ("SDK Open 测试台", "SDK Open Test Client")
        };

    public static string Get(UiLanguage language, UiTextKey key)
    {
        // Unknown enum values intentionally fall back to Chinese for a safe default.
        // 未知语言值明确回退中文，保证新增语言或配置异常时界面仍可用。
        var value = Values.TryGetValue(key, out var text)
            ? text
            : (Chinese: string.Empty, English: string.Empty);
        return language == UiLanguage.English ? value.English : value.Chinese;
    }

    public static string FormOperation(UiLanguage language, string method) =>
        language == UiLanguage.English ? $"Form operation: {method}" : $"表单操作：{method}";
}

/// <summary>
/// Windows 客户端只暴露当前支持的 UI 页面；协议目录仍可保留完整能力，便于后续扩展。
/// The Windows client exposes only supported UI pages; the protocol catalog remains complete for future extensions.
/// </summary>
public static class UiSurfacePolicy
{
    public static IReadOnlyList<UiTextKey> NavigationOrder { get; } =
    [
        UiTextKey.ConnectionPage,
        UiTextKey.QuickStartPage,
        UiTextKey.DeviceVideoPage,
        UiTextKey.CapturePage,
        UiTextKey.ImageProcessingPage,
        UiTextKey.ImageEnhancementPage,
        UiTextKey.OcrPage,
        UiTextKey.FileConversionPage,
        UiTextKey.TwainPage,
        UiTextKey.StoragePage,
        UiTextKey.RawJsonPage
    ];

    public static bool IsJsonExecutionEnabled(UiTextKey pageKey) =>
        pageKey != UiTextKey.ImageEnhancementPage;

    public static bool IsNavigationEnabled(UiTextKey pageKey) => pageKey is
        UiTextKey.ConnectionPage or
        UiTextKey.QuickStartPage or
        UiTextKey.DeviceVideoPage or
        UiTextKey.CapturePage or
        UiTextKey.ImageProcessingPage;
}

public interface ILocalizableControl
{
    void ApplyLanguage(UiLanguage language);
}

public interface ILocalizablePage : ILocalizableControl
{
}
