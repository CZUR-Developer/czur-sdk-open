using System.Drawing.Drawing2D;
using SdkOpen.TestClient.Core.Application;

namespace SdkOpen.TestClient.WinForms;

public enum ImageAreaMode { Rectangle, Points }

/// <summary>
/// Displays one contained image and edits a selected-area polygon in original-image pixels.
/// 以等比缩放方式显示图片，并用原图像素坐标编辑矩形或四点选区。
/// </summary>
public sealed class ImageSelectionPreviewControl : Control
{
    private Image? _image;
    private ImagePoint? _dragStart;
    private readonly List<ImagePoint> _points = [];
    private ImageAreaMode _areaMode;
    private bool _selectionEnabled;

    public ImageSelectionPreviewControl()
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        BackColor = Color.FromArgb(15, 23, 42);
        Cursor = Cursors.Default;
    }

    public event EventHandler? SelectionChanged;

    public bool SelectionEnabled
    {
        get => _selectionEnabled;
        set
        {
            _selectionEnabled = value;
            Cursor = value ? Cursors.Cross : Cursors.Default;
            Invalidate();
        }
    }

    public ImageAreaMode AreaMode
    {
        get => _areaMode;
        set
        {
            if (_areaMode == value) return;
            _areaMode = value;
            ClearSelection();
        }
    }

    public ImageSelectedArea? SelectedArea { get; private set; }
    public int SelectedPointCount => _points.Count;
    public bool HasImage => _image is not null;
    public Size NaturalImageSize => _image?.Size ?? Size.Empty;

    public void SetImage(Image? image)
    {
        var replacement = image is null ? null : new Bitmap(image);
        var previous = _image;
        _image = replacement;
        previous?.Dispose();
        ClearSelection();
        Invalidate();
    }

    public Image? GetImageCopy() => _image is null ? null : new Bitmap(_image);

    public void ClearSelection()
    {
        _dragStart = null;
        _points.Clear();
        SelectedArea = null;
        Invalidate();
        SelectionChanged?.Invoke(this, EventArgs.Empty);
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        if (!SelectionEnabled || e.Button != MouseButtons.Left || _image is null) return;
        var displayPoint = ToImageDisplayPoint(e.Location, out var imageRect);
        if (displayPoint is null) return;

        if (AreaMode == ImageAreaMode.Points)
        {
            if (_points.Count >= 4)
            {
                _points.Clear();
                SelectedArea = null;
            }
            _points.Add(ImageSelectionGeometry.DisplayToNatural(
                displayPoint,
                Math.Max(1, (int)Math.Round(imageRect.Width)),
                Math.Max(1, (int)Math.Round(imageRect.Height)),
                _image.Width,
                _image.Height));
            if (_points.Count == 4)
            {
                SelectedArea = ImageSelectionGeometry.NormalizeFourPoints(_image.Width, _image.Height, _points);
            }
            Invalidate();
            SelectionChanged?.Invoke(this, EventArgs.Empty);
            return;
        }

        _dragStart = displayPoint;
        UpdateRectangle(displayPoint, imageRect);
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        if (!SelectionEnabled || AreaMode != ImageAreaMode.Rectangle || _dragStart is null || e.Button != MouseButtons.Left || _image is null) return;
        var displayPoint = ToImageDisplayPoint(e.Location, out var imageRect);
        if (displayPoint is null) return;
        UpdateRectangle(displayPoint, imageRect);
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        if (AreaMode != ImageAreaMode.Rectangle || _dragStart is null || _image is null) return;
        var displayPoint = ToImageDisplayPoint(e.Location, out var imageRect);
        if (displayPoint is not null) UpdateRectangle(displayPoint, imageRect);
        _dragStart = null;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        if (_image is null)
        {
            DrawPlaceholder(e.Graphics);
            return;
        }

        var imageRect = FitRectangle(ClientRectangle, _image.Size);
        e.Graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
        e.Graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
        e.Graphics.DrawImage(_image, imageRect);
        if (!SelectionEnabled) return;

        var naturalPoints = SelectedArea is null
            ? _points
            : [SelectedArea.LeftTop, SelectedArea.RightTop, SelectedArea.RightDown, SelectedArea.LeftDown];
        if (naturalPoints.Count == 0) return;

        var mapped = naturalPoints.Select(point => new PointF(
            imageRect.Left + point.X / (float)_image.Width * imageRect.Width,
            imageRect.Top + point.Y / (float)_image.Height * imageRect.Height)).ToArray();
        using var fill = new SolidBrush(Color.FromArgb(45, 34, 211, 238));
        using var pen = new Pen(Color.FromArgb(255, 34, 211, 238), 2f) { LineJoin = LineJoin.Round };
        if (mapped.Length >= 3) e.Graphics.FillPolygon(fill, mapped);
        if (mapped.Length >= 2) e.Graphics.DrawLines(pen, mapped);
        if (mapped.Length == 4) e.Graphics.DrawLine(pen, mapped[3], mapped[0]);
        foreach (var point in mapped)
        {
            e.Graphics.FillEllipse(Brushes.White, point.X - 5, point.Y - 5, 10, 10);
            e.Graphics.FillEllipse(Brushes.DeepSkyBlue, point.X - 3, point.Y - 3, 6, 6);
        }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _image?.Dispose();
            _image = null;
        }
        base.Dispose(disposing);
    }

    private void UpdateRectangle(ImagePoint current, RectangleF imageRect)
    {
        if (_image is null || _dragStart is null) return;
        SelectedArea = ImageSelectionGeometry.CreateRectangle(
            _dragStart,
            current,
            Math.Max(1, (int)Math.Round(imageRect.Width)),
            Math.Max(1, (int)Math.Round(imageRect.Height)),
            _image.Width,
            _image.Height);
        _points.Clear();
        Invalidate();
        SelectionChanged?.Invoke(this, EventArgs.Empty);
    }

    private ImagePoint? ToImageDisplayPoint(Point location, out RectangleF imageRect)
    {
        imageRect = _image is null ? RectangleF.Empty : FitRectangle(ClientRectangle, _image.Size);
        if (_image is null || !imageRect.Contains(location)) return null;
        return new ImagePoint(
            (int)Math.Round(location.X - imageRect.Left),
            (int)Math.Round(location.Y - imageRect.Top));
    }

    private void DrawPlaceholder(Graphics graphics)
    {
        var text = Tag as string ?? string.Empty;
        if (string.IsNullOrWhiteSpace(text)) return;
        TextRenderer.DrawText(graphics, text, Font, ClientRectangle, Color.LightSlateGray,
            TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.WordBreak);
    }

    private static RectangleF FitRectangle(Rectangle bounds, Size imageSize)
    {
        if (bounds.Width <= 0 || bounds.Height <= 0 || imageSize.Width <= 0 || imageSize.Height <= 0) return RectangleF.Empty;
        var scale = Math.Min(bounds.Width / (float)imageSize.Width, bounds.Height / (float)imageSize.Height);
        var width = imageSize.Width * scale;
        var height = imageSize.Height * scale;
        return new RectangleF(bounds.Left + (bounds.Width - width) / 2f, bounds.Top + (bounds.Height - height) / 2f, width, height);
    }
}
