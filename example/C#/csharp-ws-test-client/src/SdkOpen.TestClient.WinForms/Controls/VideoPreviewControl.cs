using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using SdkOpen.TestClient.Core.Application;
using SdkOpen.TestClient.Core.Protocol;

namespace SdkOpen.TestClient.WinForms;

/// <summary>
/// Renders the live frame and the single-page crop overlay used by the web demo.
/// 绘制实时画面，并叠加网页 demo 使用的单页裁切检测框。
/// </summary>
public sealed class VideoPreviewControl : UserControl, ILocalizableControl
{
    private readonly PictureBox _picture = new() { Dock = DockStyle.Fill, SizeMode = PictureBoxSizeMode.Zoom, BackColor = Color.Black };
    private readonly PreviewOverlayControl _overlay = new() { Dock = DockStyle.Fill };
    private readonly Label _statistics = new() { Dock = DockStyle.Bottom, Height = 26, TextAlign = ContentAlignment.MiddleLeft };
    private UiLanguage _language = UiLanguage.Chinese;

    public VideoPreviewControl()
    {
        Controls.Add(_picture);
        // Make the overlay a child of the PictureBox so transparent painting is composed
        // over the decoded frame rather than over the UserControl background.
        // 将叠加层放到 PictureBox 内，透明绘制才会叠加在视频帧上，而不是控件背景上。
        _picture.Controls.Add(_overlay);
        Controls.Add(_statistics);
    }

    public void ShowFrame(RenderableFrame frame, VideoPreviewStatistics statistics)
    {
        var image = CreateImage(frame);
        var previous = _picture.Image;
        _picture.Image = image;
        previous?.Dispose();
        _overlay.Frame = frame;
        _overlay.Invalidate();
        _statistics.Text = _language == UiLanguage.English
            ? $"Received {statistics.ReceivedFrames}  Rendered {statistics.RenderedFrames}  Dropped {statistics.DroppedFrames}  Decode failures {statistics.DecodeFailedFrames}"
            : $"接收 {statistics.ReceivedFrames}  渲染 {statistics.RenderedFrames}  丢帧 {statistics.DroppedFrames}  解码失败 {statistics.DecodeFailedFrames}";
    }

    /// <summary>
    /// Enables the single-page detector message and crop frame.
    /// 启用单页检测提示及裁切框绘制。
    /// </summary>
    public void ConfigureSinglePageCropOverlay(bool enabled, string message)
    {
        _overlay.ShowSinglePageCrop = enabled;
        _overlay.CropMessage = message ?? string.Empty;
        _overlay.Invalidate();
    }

    public void ApplyLanguage(UiLanguage language)
    {
        _language = language;
        _overlay.Invalidate();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _picture.Image?.Dispose();
            _overlay.Frame = null;
        }
        base.Dispose(disposing);
    }

    private static Image CreateImage(RenderableFrame frame)
    {
        if (!string.Equals(frame.Meta.PixelFormat, "bgr24", StringComparison.OrdinalIgnoreCase))
        {
            using var stream = new MemoryStream(frame.Bytes, false);
            using var decoded = Image.FromStream(stream);
            return new Bitmap(decoded);
        }

        var bitmap = new Bitmap(frame.Meta.Width, frame.Meta.Height, PixelFormat.Format24bppRgb);
        var bitmapData = bitmap.LockBits(new Rectangle(0, 0, bitmap.Width, bitmap.Height), ImageLockMode.WriteOnly, bitmap.PixelFormat);
        try
        {
            var sourceStride = frame.Meta.Width * 3;
            for (var row = 0; row < frame.Meta.Height; row++)
            {
                Marshal.Copy(frame.Bytes, row * sourceStride, IntPtr.Add(bitmapData.Scan0, row * bitmapData.Stride), sourceStride);
            }
        }
        finally
        {
            bitmap.UnlockBits(bitmapData);
        }
        return bitmap;
    }

    private sealed class PreviewOverlayControl : Control
    {
        public PreviewOverlayControl()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.SupportsTransparentBackColor, true);
            BackColor = Color.Transparent;
        }

        public RenderableFrame? Frame { get; set; }
        public bool ShowSinglePageCrop { get; set; }
        public string CropMessage { get; set; } = string.Empty;

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            if (!ShowSinglePageCrop || Frame is null || ClientSize.Width <= 0 || ClientSize.Height <= 0) return;

            var sourceWidth = Frame.Meta.DetectedRectsSource?.Width > 0
                ? Frame.Meta.DetectedRectsSource.Width
                : Frame.Meta.SourceWidth is > 0 ? Frame.Meta.SourceWidth.Value : Frame.Meta.Width;
            var sourceHeight = Frame.Meta.DetectedRectsSource?.Height > 0
                ? Frame.Meta.DetectedRectsSource.Height
                : Frame.Meta.SourceHeight is > 0 ? Frame.Meta.SourceHeight.Value : Frame.Meta.Height;
            if (sourceWidth <= 0 || sourceHeight <= 0) return;

            // The displayed image follows the frame dimensions; detection points may use
            // a different source coordinate space and are scaled below like the web demo.
            // 画面区域按当前帧尺寸计算；检测点可来自不同坐标系，下面按网页 demo 方式缩放。
            var imageRect = FitRectangle(ClientSize, Frame.Meta.Width, Frame.Meta.Height);
            using var pen = new Pen(Color.FromArgb(255, 34, 211, 238), Math.Max(2, Math.Min(ClientSize.Width, ClientSize.Height) / 180f))
            {
                LineJoin = LineJoin.Round,
                DashStyle = DashStyle.Solid
            };
            using var shadow = new Pen(Color.FromArgb(170, 8, 47, 73), pen.Width + 4) { LineJoin = LineJoin.Round };
            using var fill = new SolidBrush(Color.FromArgb(32, 34, 211, 238));

            var drawn = false;
            foreach (var rect in Frame.Meta.DetectedRects ?? [])
            {
                if (!TryGetPoints(rect, out var points)) continue;
                DrawPolygon(e.Graphics, points, imageRect, sourceWidth, sourceHeight, shadow, fill, pen);
                drawn = true;
            }

            foreach (var rect in Frame.Meta.Detections ?? [])
            {
                var points = new[]
                {
                    new PointF(rect.X, rect.Y), new PointF(rect.X + rect.Width, rect.Y),
                    new PointF(rect.X + rect.Width, rect.Y + rect.Height), new PointF(rect.X, rect.Y + rect.Height)
                };
                DrawPolygon(e.Graphics, points, imageRect, sourceWidth, sourceHeight, shadow, fill, pen);
                drawn = true;
            }

            if (!drawn)
            {
                // No detector result yet: show a guide frame, never pretend it is a confirmed detection.
                // 尚未收到检测结果时只显示辅助框，不把辅助框误标成真实检测结果。
                var guide = RectangleF.Inflate(imageRect, -imageRect.Width * 0.08f, -imageRect.Height * 0.08f);
                using var guidePen = new Pen(Color.FromArgb(220, 125, 211, 252), pen.Width) { DashStyle = DashStyle.Dash };
                e.Graphics.DrawRectangle(guidePen, guide.X, guide.Y, guide.Width, guide.Height);
            }

            if (!string.IsNullOrWhiteSpace(CropMessage))
            {
                using var textBrush = new SolidBrush(Color.White);
                using var background = new SolidBrush(Color.FromArgb(190, 8, 47, 73));
                var textSize = e.Graphics.MeasureString(CropMessage, Font);
                var banner = new RectangleF(imageRect.Left + 8, imageRect.Top + 8, Math.Min(textSize.Width + 18, imageRect.Width - 16), textSize.Height + 10);
                e.Graphics.FillRectangle(background, banner);
                e.Graphics.DrawString(CropMessage, Font, textBrush, banner.Left + 9, banner.Top + 5);
            }
        }

        private static void DrawPolygon(Graphics graphics, IReadOnlyList<PointF> points, RectangleF imageRect, int sourceWidth, int sourceHeight, Pen shadow, Brush fill, Pen pen)
        {
            var mapped = points.Select(point => new PointF(imageRect.Left + point.X / sourceWidth * imageRect.Width, imageRect.Top + point.Y / sourceHeight * imageRect.Height)).ToArray();
            if (mapped.Length < 4) return;
            graphics.FillPolygon(fill, mapped);
            graphics.DrawPolygon(shadow, mapped);
            graphics.DrawPolygon(pen, mapped);
        }

        private static bool TryGetPoints(DetectedQuadrilateral rect, out PointF[] points)
        {
            points = [];
            if (rect.LeftTop is null || rect.RightTop is null || rect.RightDown is null || rect.LeftDown is null ||
                rect.LeftTop.Count < 2 || rect.RightTop.Count < 2 || rect.RightDown.Count < 2 || rect.LeftDown.Count < 2) return false;
            points =
            [
                new PointF(rect.LeftTop[0], rect.LeftTop[1]),
                new PointF(rect.RightTop[0], rect.RightTop[1]),
                new PointF(rect.RightDown[0], rect.RightDown[1]),
                new PointF(rect.LeftDown[0], rect.LeftDown[1])
            ];
            return true;
        }

        private static RectangleF FitRectangle(Size clientSize, int sourceWidth, int sourceHeight)
        {
            var scale = Math.Min((float)clientSize.Width / sourceWidth, (float)clientSize.Height / sourceHeight);
            var width = sourceWidth * scale;
            var height = sourceHeight * scale;
            return new RectangleF((clientSize.Width - width) / 2f, (clientSize.Height - height) / 2f, width, height);
        }
    }
}
