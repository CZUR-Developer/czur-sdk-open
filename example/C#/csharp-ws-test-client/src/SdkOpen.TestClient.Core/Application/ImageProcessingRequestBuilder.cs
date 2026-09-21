using System.Text.Json;

namespace SdkOpen.TestClient.Core.Application;

public enum ImageProcessingOperation { Combined, Page, Color }

public sealed record ImagePoint(int X, int Y);

public sealed record ImageSelectedArea(
    int SourceWidth,
    int SourceHeight,
    ImagePoint LeftTop,
    ImagePoint RightTop,
    ImagePoint RightDown,
    ImagePoint LeftDown)
{
    public bool IsValid => SourceWidth > 0 && SourceHeight > 0 &&
        HorizontalSpan >= 8 && VerticalSpan >= 8 &&
        Points.All(point => point.X >= 0 && point.X <= SourceWidth && point.Y >= 0 && point.Y <= SourceHeight);

    private ImagePoint[] Points => [LeftTop, RightTop, RightDown, LeftDown];
    private int HorizontalSpan => Points.Max(point => point.X) - Points.Min(point => point.X);
    private int VerticalSpan => Points.Max(point => point.Y) - Points.Min(point => point.Y);
}

public sealed record ImageProcessingOptions(
    ImageProcessingOperation Operation,
    CapturePageProcessing PageProcessing,
    CaptureColorMode ColorMode,
    CaptureOutputFormat OutputFormat,
    bool SinglePageCropBorderEnabled,
    int SinglePageCropBorderWidth,
    int SinglePageCropBorderHeight,
    bool IdCardRoundCorner,
    bool AutoRotate,
    bool SmartBlackEdgeOptimize,
    bool MultiTargetPaging,
    ImageSelectedArea? SelectedArea,
    bool CurvedBookRemoveFinger,
    string CurvedBookFingerType,
    bool CurvedBookSmartPaging,
    bool CurvedBookCropBorderEnabled,
    int CurvedBookCropBorderWidth,
    int CurvedBookCropBorderHeight,
    bool CurvedBookAutoComplete)
{
    public static ImageProcessingOptions Default { get; } = new(
        ImageProcessingOperation.Combined,
        CapturePageProcessing.SinglePage,
        CaptureColorMode.AutoOptimize,
        CaptureOutputFormat.Jpg,
        false,
        0,
        0,
        false,
        false,
        true,
        false,
        null,
        true,
        "with_sleeve",
        true,
        false,
        0,
        0,
        false);
}

public sealed record ImageProcessingRequest(string Method, JsonElement Parameters);

public static class ImageProcessingRequestBuilder
{
    public static ImageProcessingRequest Build(string uploadId, ImageProcessingOptions options)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(uploadId);
        ArgumentNullException.ThrowIfNull(options);
        Validate(options);

        if (options.Operation == ImageProcessingOperation.Color)
        {
            return new ImageProcessingRequest("image.apply_color_mode", JsonSerializer.SerializeToElement(new
            {
                input_upload_id = uploadId,
                color_mode = ColorModeValue(options.ColorMode)
            }));
        }

        var parameters = BuildPageParameters(uploadId, options);
        if (options.Operation == ImageProcessingOperation.Combined)
        {
            parameters["color_mode"] = ColorModeValue(options.ColorMode);
            parameters["output_format"] = options.OutputFormat.ToString().ToLowerInvariant();
        }

        return new ImageProcessingRequest(
            options.Operation == ImageProcessingOperation.Combined ? "image.process" : "image.process_page",
            JsonSerializer.SerializeToElement(parameters));
    }

    private static Dictionary<string, object?> BuildPageParameters(string uploadId, ImageProcessingOptions options)
    {
        var parameters = new Dictionary<string, object?>
        {
            ["input_upload_id"] = uploadId,
            ["page_processing"] = PageProcessingValue(options.PageProcessing),
            ["single_page"] = new
            {
                crop_border = new
                {
                    enabled = options.SinglePageCropBorderEnabled,
                    width = ClampMargin(options.SinglePageCropBorderWidth),
                    height = ClampMargin(options.SinglePageCropBorderHeight)
                },
                id_card_round_corner = options.IdCardRoundCorner,
                auto_rotate = options.AutoRotate,
                smart_black_edge_optimize = options.SmartBlackEdgeOptimize,
                multi_target_paging = options.MultiTargetPaging
            },
            ["curved_book"] = new
            {
                remove_finger = new
                {
                    enabled = options.CurvedBookRemoveFinger,
                    finger_type = options.CurvedBookFingerType
                },
                smart_paging = options.CurvedBookSmartPaging,
                crop_border = new
                {
                    enabled = options.CurvedBookCropBorderEnabled,
                    width = ClampMargin(options.CurvedBookCropBorderWidth),
                    height = ClampMargin(options.CurvedBookCropBorderHeight)
                },
                auto_complete = options.CurvedBookAutoComplete
            }
        };

        if (options.PageProcessing == CapturePageProcessing.Selection && options.SelectedArea is not null)
        {
            var area = options.SelectedArea;
            // 选区坐标始终使用原图像素，避免窗口缩放后把显示坐标误传给 runtime。
            // Always send original-image pixels so window scaling never leaks display coordinates to the runtime.
            parameters["selected_area"] = new
            {
                source = new { width = area.SourceWidth, height = area.SourceHeight },
                points = new
                {
                    left_top = new { x = area.LeftTop.X, y = area.LeftTop.Y },
                    right_top = new { x = area.RightTop.X, y = area.RightTop.Y },
                    right_down = new { x = area.RightDown.X, y = area.RightDown.Y },
                    left_down = new { x = area.LeftDown.X, y = area.LeftDown.Y }
                }
            };
        }

        return parameters;
    }

    private static void Validate(ImageProcessingOptions options)
    {
        if (options.PageProcessing == CapturePageProcessing.Selection && (options.SelectedArea is null || !options.SelectedArea.IsValid))
        {
            throw new InvalidOperationException("A valid selected area in original-image pixels is required.");
        }

        if (options.CurvedBookFingerType is not ("with_sleeve" or "without_sleeve"))
        {
            throw new InvalidOperationException("finger_type must be with_sleeve or without_sleeve.");
        }
    }

    private static string PageProcessingValue(CapturePageProcessing value) => value switch
    {
        CapturePageProcessing.KeepOriginal => "keep_original",
        CapturePageProcessing.SinglePage => "single_page",
        CapturePageProcessing.Selection => "selected_area",
        CapturePageProcessing.CurvedBook => "curved_book",
        _ => throw new ArgumentOutOfRangeException(nameof(value))
    };

    private static string ColorModeValue(CaptureColorMode value) => value switch
    {
        CaptureColorMode.AutoOptimize => "auto_optimize",
        CaptureColorMode.Color => "color",
        CaptureColorMode.BlackWhite => "black_white",
        CaptureColorMode.WhitePaperSeal => "white_paper_seal",
        CaptureColorMode.Grayscale => "grayscale",
        CaptureColorMode.Certificate => "certificate",
        CaptureColorMode.Ancient => "ancient",
        CaptureColorMode.NoOptimize => "no_optimize",
        _ => throw new ArgumentOutOfRangeException(nameof(value))
    };

    private static int ClampMargin(int value) => Math.Clamp(value, -100, 100);
}
