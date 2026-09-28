using System.Text.Json.Serialization;

namespace SdkOpen.TestClient.Core.Protocol;

public sealed record DetectionRectangle(
    [property: JsonPropertyName("x")] int X,
    [property: JsonPropertyName("y")] int Y,
    [property: JsonPropertyName("width")] int Width,
    [property: JsonPropertyName("height")] int Height);

/// <summary>
/// Quadrilateral emitted by the web demo's single-page detector.
/// 网页 demo 的单页检测器返回四点裁切框。
/// The native SdkRect4P points are floating-point coordinates, so decimals must be preserved.
/// 原生 SdkRect4P 使用浮点坐标，不能按整数反序列化，否则整条 frame_meta 会被丢弃。
/// </summary>
public sealed record DetectedQuadrilateral(
    [property: JsonPropertyName("left_top")] IReadOnlyList<float>? LeftTop = null,
    [property: JsonPropertyName("right_top")] IReadOnlyList<float>? RightTop = null,
    [property: JsonPropertyName("right_down")] IReadOnlyList<float>? RightDown = null,
    [property: JsonPropertyName("left_down")] IReadOnlyList<float>? LeftDown = null);

public sealed record DetectedRectSource(
    [property: JsonPropertyName("width")] int Width,
    [property: JsonPropertyName("height")] int Height);

public sealed record VideoFrameMeta(
    [property: JsonPropertyName("stream_id")] string StreamId,
    [property: JsonPropertyName("frame_seq")] long FrameSequence,
    [property: JsonPropertyName("width")] int Width,
    [property: JsonPropertyName("height")] int Height,
    [property: JsonPropertyName("pixel_format")] string PixelFormat,
    [property: JsonPropertyName("detections")] IReadOnlyList<DetectionRectangle>? Detections = null,
    [property: JsonPropertyName("source_width")] int? SourceWidth = null,
    [property: JsonPropertyName("source_height")] int? SourceHeight = null,
    [property: JsonPropertyName("detected_rects")] IReadOnlyList<DetectedQuadrilateral>? DetectedRects = null,
    [property: JsonPropertyName("detected_rects_source")] DetectedRectSource? DetectedRectsSource = null);
