using SdkOpen.TestClient.Core.Protocol;

namespace SdkOpen.TestClient.Core.Application;

public sealed record VideoPreviewStatistics(long ReceivedFrames, long RenderedFrames, long DroppedFrames, long DecodeFailedFrames, DateTimeOffset? LastFrameReceivedAt);
public sealed record RenderableFrame(VideoFrameMeta Meta, byte[] Bytes);

public sealed class VideoPreviewPipeline
{
    private readonly object _sync = new();
    private readonly string _streamId;
    private VideoFrameMeta? _nextMeta;
    private RenderableFrame? _latestFrame;
    private long _lastSequence;
    private long _receivedFrames;
    private long _renderedFrames;
    private long _droppedFrames;
    private long _decodeFailedFrames;
    private DateTimeOffset? _lastFrameReceivedAt;

    public VideoPreviewPipeline(string streamId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(streamId);
        _streamId = streamId;
    }

    public event EventHandler? FrameAvailable;

    public VideoPreviewStatistics Statistics
    {
        get
        {
            lock (_sync)
            {
                return new VideoPreviewStatistics(_receivedFrames, _renderedFrames, _droppedFrames, _decodeFailedFrames, _lastFrameReceivedAt);
            }
        }
    }

    public bool AcceptMeta(VideoFrameMeta metadata)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        lock (_sync)
        {
            if (!string.Equals(metadata.StreamId, _streamId, StringComparison.Ordinal) || metadata.FrameSequence <= _lastSequence || !IsSupportedFormat(metadata.PixelFormat))
            {
                _droppedFrames++;
                return false;
            }

            _nextMeta = metadata;
            _lastSequence = metadata.FrameSequence;
            return true;
        }
    }

    public bool AcceptBinary(byte[] bytes)
    {
        ArgumentNullException.ThrowIfNull(bytes);
        var frameAvailable = false;
        lock (_sync)
        {
            if (_nextMeta is null)
            {
                _droppedFrames++;
                return false;
            }

            var metadata = _nextMeta;
            _nextMeta = null;
            if (!IsValidFrame(metadata, bytes))
            {
                _decodeFailedFrames++;
                return false;
            }

            _receivedFrames++;
            _lastFrameReceivedAt = DateTimeOffset.UtcNow;
            if (_latestFrame is not null)
            {
                _droppedFrames++;
            }

            _latestFrame = new RenderableFrame(metadata, bytes.ToArray());
            frameAvailable = true;
        }

        if (frameAvailable)
        {
            FrameAvailable?.Invoke(this, EventArgs.Empty);
        }

        return true;
    }

    public RenderableFrame? TakeLatestFrame()
    {
        lock (_sync)
        {
            var frame = _latestFrame;
            _latestFrame = null;
            if (frame is not null)
            {
                _renderedFrames++;
            }

            return frame;
        }
    }

    /// <summary>
    /// Reads the most recent frame without consuming it. This allows the device page and
    /// capture page to render the same live stream at the same time.
    /// 同时读取最新帧但不消费，保证设备页和采集页可以同时显示同一条实时流。
    /// </summary>
    public RenderableFrame? PeekLatestFrame()
    {
        lock (_sync)
        {
            // RenderableFrame owns an immutable byte snapshot created by AcceptBinary;
            // returning the same snapshot avoids cloning a full camera frame on every UI tick.
            // AcceptBinary 创建的字节数组在管线内不会再修改，直接共享可避免每次 UI 刷新复制整帧。
            return _latestFrame;
        }
    }

    private static bool IsSupportedFormat(string format) =>
        string.Equals(format, "jpeg", StringComparison.OrdinalIgnoreCase) ||
        string.Equals(format, "mjpeg", StringComparison.OrdinalIgnoreCase) ||
        string.Equals(format, "bgr24", StringComparison.OrdinalIgnoreCase);

    private static bool IsValidFrame(VideoFrameMeta metadata, byte[] bytes)
    {
        if (bytes.Length == 0)
        {
            return false;
        }

        return !string.Equals(metadata.PixelFormat, "bgr24", StringComparison.OrdinalIgnoreCase) ||
            metadata.Width > 0 && metadata.Height > 0 && bytes.Length >= checked(metadata.Width * metadata.Height * 3);
    }
}
