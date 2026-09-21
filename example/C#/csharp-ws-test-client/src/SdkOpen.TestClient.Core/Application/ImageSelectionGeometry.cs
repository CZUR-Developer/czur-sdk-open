namespace SdkOpen.TestClient.Core.Application;

public static class ImageSelectionGeometry
{
    public static ImagePoint DisplayToNatural(
        ImagePoint point,
        int displayWidth,
        int displayHeight,
        int naturalWidth,
        int naturalHeight)
    {
        ValidateDimensions(displayWidth, displayHeight, naturalWidth, naturalHeight);
        var x = Math.Clamp(point.X, 0, displayWidth);
        var y = Math.Clamp(point.Y, 0, displayHeight);
        return new ImagePoint(
            (int)Math.Round(x * (double)naturalWidth / displayWidth, MidpointRounding.AwayFromZero),
            (int)Math.Round(y * (double)naturalHeight / displayHeight, MidpointRounding.AwayFromZero));
    }

    public static ImageSelectedArea CreateRectangle(
        ImagePoint firstDisplayPoint,
        ImagePoint secondDisplayPoint,
        int displayWidth,
        int displayHeight,
        int naturalWidth,
        int naturalHeight)
    {
        var first = DisplayToNatural(firstDisplayPoint, displayWidth, displayHeight, naturalWidth, naturalHeight);
        var second = DisplayToNatural(secondDisplayPoint, displayWidth, displayHeight, naturalWidth, naturalHeight);
        var left = Math.Min(first.X, second.X);
        var right = Math.Max(first.X, second.X);
        var top = Math.Min(first.Y, second.Y);
        var bottom = Math.Max(first.Y, second.Y);
        return new ImageSelectedArea(
            naturalWidth,
            naturalHeight,
            new ImagePoint(left, top),
            new ImagePoint(right, top),
            new ImagePoint(right, bottom),
            new ImagePoint(left, bottom));
    }

    public static ImageSelectedArea NormalizeFourPoints(int sourceWidth, int sourceHeight, IReadOnlyCollection<ImagePoint> points)
    {
        ArgumentNullException.ThrowIfNull(points);
        if (sourceWidth <= 0 || sourceHeight <= 0) throw new ArgumentOutOfRangeException(nameof(sourceWidth));
        if (points.Count != 4) throw new ArgumentException("Exactly four points are required.", nameof(points));

        var clamped = points
            .Select(point => new ImagePoint(Math.Clamp(point.X, 0, sourceWidth), Math.Clamp(point.Y, 0, sourceHeight)))
            .ToArray();
        // 与网页 Demo 一致：先按 Y 分出上下两组，再按 X 识别左右角。
        // Match the web demo: split top/bottom by Y, then identify left/right by X.
        var top = clamped.OrderBy(point => point.Y).Take(2).OrderBy(point => point.X).ToArray();
        var bottom = clamped.OrderByDescending(point => point.Y).Take(2).OrderBy(point => point.X).ToArray();
        return new ImageSelectedArea(sourceWidth, sourceHeight, top[0], top[1], bottom[1], bottom[0]);
    }

    private static void ValidateDimensions(int displayWidth, int displayHeight, int naturalWidth, int naturalHeight)
    {
        if (displayWidth <= 0) throw new ArgumentOutOfRangeException(nameof(displayWidth));
        if (displayHeight <= 0) throw new ArgumentOutOfRangeException(nameof(displayHeight));
        if (naturalWidth <= 0) throw new ArgumentOutOfRangeException(nameof(naturalWidth));
        if (naturalHeight <= 0) throw new ArgumentOutOfRangeException(nameof(naturalHeight));
    }
}
