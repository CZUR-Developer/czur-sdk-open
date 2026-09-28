using System.Text.Json;

namespace SdkOpen.TestClient.Core.Application;

public enum CapturePageProcessing { SinglePage, CurvedBook, Selection, KeepOriginal }
public enum CaptureColorMode { AutoOptimize, Color, BlackWhite, Grayscale, WhitePaperSeal, Certificate, Ancient, NoOptimize }
public enum CaptureOutputFormat { Jpg, Png, Tiff }

public sealed record CaptureSelection(int X, int Y, int Width, int Height)
{
    public bool IsValid => Width > 0 && Height > 0;
}

public sealed record CaptureThumbnailOptions(bool Original, bool PageProcessed, bool ColorProcessed, bool Final)
{
    public static CaptureThumbnailOptions Default { get; } = new(true, true, false, true);
}

public sealed record CaptureFormOptions(
    CapturePageProcessing PageProcessing,
    CaptureColorMode ColorMode,
    CaptureOutputFormat OutputFormat,
    int JpegQuality,
    bool RealtimeDetectRects,
    bool IdCardRoundCorner,
    bool AutoRotate,
    bool SmartBlackEdgeOptimize,
    bool MultiTargetPaging,
    CaptureThumbnailOptions Thumbnails,
    CaptureSelection? Selection,
    bool SinglePageCropBorderEnabled = false,
    int SinglePageCropBorderWidth = 0,
    int SinglePageCropBorderHeight = 0,
    bool CurvedBookRemoveFinger = true,
    string CurvedBookFingerType = "with_sleeve",
    bool CurvedBookSmartPaging = true,
    bool CurvedBookCropBorderEnabled = false,
    int CurvedBookCropBorderWidth = 0,
    int CurvedBookCropBorderHeight = 0,
    bool CurvedBookAutoComplete = false,
    int? OutputTargetSize = null)
{
    public static CaptureFormOptions Default { get; } = new(
        CapturePageProcessing.SinglePage, CaptureColorMode.Color, CaptureOutputFormat.Jpg, 90,
        true, false, false, true, false, CaptureThumbnailOptions.Default, null);
}

public static class CaptureProfileBuilder
{
    public static JsonElement Build(CaptureFormOptions options, DeviceResolution? resolution, string? deviceId = null)
    {
        ArgumentNullException.ThrowIfNull(options);
        if (options.OutputFormat == CaptureOutputFormat.Jpg && options.JpegQuality is < 1 or > 100)
        {
            throw new ArgumentOutOfRangeException(nameof(options.JpegQuality), "JPG 质量必须介于 1 到 100。");
        }
        if (options.PageProcessing == CapturePageProcessing.Selection && (options.Selection is null || !options.Selection.IsValid))
        {
            throw new InvalidOperationException("选区模式需要一个有效的预览矩形。");
        }

        var output = new Dictionary<string, object?>
        {
            ["format"] = OutputFormatValue(options.OutputFormat),
            ["thumbnails"] = new
            {
                original = options.Thumbnails.Original,
                page_processed = options.Thumbnails.PageProcessed,
                color_processed = options.Thumbnails.ColorProcessed,
                final = options.Thumbnails.Final
            }
        };
        if (options.OutputFormat == CaptureOutputFormat.Jpg)
        {
            output["quality"] = options.JpegQuality;
        }
        // The web demo omits target_size when no runtime-supported pixel bucket is selected.
        // Sending JSON null is not equivalent: sdk_open validates it as an integer bucket.
        // 未选择运行时支持的像素桶时，与网页 Demo 一样完全省略 target_size；不能发送 JSON null。
        if (options.OutputTargetSize is > 0)
        {
            output["target_size"] = options.OutputTargetSize.Value;
        }

        var profile = new Dictionary<string, object?>
        {
            ["profile_version"] = "capture.profile.v1",
            ["device"] = new
            {
                device_id = deviceId,
                resolution = resolution is null ? null : new { width = resolution.Width, height = resolution.Height, fps = resolution.Fps, pixel_format = resolution.PixelFormat, real_width = resolution.RealWidth, real_height = resolution.RealHeight, is_default = resolution.IsDefault }
            },
            ["capture"] = new Dictionary<string, object?>
            {
                ["page_processing"] = PageProcessingValue(options.PageProcessing),
                ["color_mode"] = ColorModeValue(options.ColorMode),
                ["single_page"] = new
                {
                    realtime_detect_rects = options.RealtimeDetectRects,
                    crop_border = new { enabled = options.SinglePageCropBorderEnabled, width = ClampCropMargin(options.SinglePageCropBorderWidth), height = ClampCropMargin(options.SinglePageCropBorderHeight) },
                    id_card_round_corner = options.IdCardRoundCorner,
                    auto_rotate = options.AutoRotate,
                    smart_black_edge_optimize = options.SmartBlackEdgeOptimize,
                    multi_target_paging = options.MultiTargetPaging
                },
                ["curved_book"] = new
                {
                    remove_finger = new { enabled = options.CurvedBookRemoveFinger, finger_type = options.CurvedBookFingerType },
                    smart_paging = options.CurvedBookSmartPaging,
                    crop_border = new { enabled = options.CurvedBookCropBorderEnabled, width = ClampCropMargin(options.CurvedBookCropBorderWidth), height = ClampCropMargin(options.CurvedBookCropBorderHeight) },
                    auto_complete = options.CurvedBookAutoComplete
                },
                ["selected_area"] = BuildSelectedArea(options.Selection, resolution)
            },
            ["output"] = output
        };
        return JsonSerializer.SerializeToElement(profile);
    }

    private static string PageProcessingValue(CapturePageProcessing value) => value switch
    {
        CapturePageProcessing.SinglePage => "single_page",
        CapturePageProcessing.CurvedBook => "curved_book",
        CapturePageProcessing.Selection => "selected_area",
        CapturePageProcessing.KeepOriginal => "keep_original",
        _ => throw new ArgumentOutOfRangeException(nameof(value))
    };

    private static string ColorModeValue(CaptureColorMode value) => value switch
    {
        CaptureColorMode.AutoOptimize => "auto_optimize",
        CaptureColorMode.Color => "color_enhance",
        CaptureColorMode.Grayscale => "grayscale",
        CaptureColorMode.BlackWhite => "black_white",
        CaptureColorMode.WhitePaperSeal => "white_paper_seal",
        CaptureColorMode.Certificate => "certificate",
        CaptureColorMode.Ancient => "ancient",
        CaptureColorMode.NoOptimize => "no_optimize",
        _ => throw new ArgumentOutOfRangeException(nameof(value))
    };

    private static string OutputFormatValue(CaptureOutputFormat value) => value.ToString().ToLowerInvariant();

    private static object? BuildSelectedArea(CaptureSelection? selection, DeviceResolution? resolution)
    {
        if (selection is null) return null;
        return new
        {
            points = new
            {
                left_top = new { x = selection.X, y = selection.Y },
                right_top = new { x = selection.X + selection.Width, y = selection.Y },
                right_down = new { x = selection.X + selection.Width, y = selection.Y + selection.Height },
                left_down = new { x = selection.X, y = selection.Y + selection.Height }
            },
            source = new { width = resolution?.Width ?? 0, height = resolution?.Height ?? 0 }
        };
    }

    private static int ClampCropMargin(int value) => Math.Clamp(value, -100, 100);
}
