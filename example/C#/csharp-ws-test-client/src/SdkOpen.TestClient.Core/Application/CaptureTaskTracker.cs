using System.Text.Json;

namespace SdkOpen.TestClient.Core.Application;

public enum CaptureTaskStatus { Queued, Running, Succeeded, Failed, Unknown }
public enum CaptureTaskUpdateSource { Submitted, Event, Poll }

public sealed record CaptureAssetSnapshot(
    string AssetId,
    string Kind,
    string? Url,
    string? DownloadUrl,
    string? Path,
    string? ContentType,
    int Width,
    int Height,
    long Size)
{
    public string? AccessUrl => Url ?? DownloadUrl ?? Path;
}

public sealed record CaptureStageSnapshot(string Name, string Status, string Provider, string Message);

public sealed record CaptureTaskSnapshot(
    string TaskId,
    CaptureTaskStatus Status,
    string OutputFormat,
    DateTimeOffset CreatedAt,
    DateTimeOffset UpdatedAt,
    string? AssetUrl,
    string? AssetContentType,
    string? Error,
    int PollAttempts,
    CaptureTaskUpdateSource LastUpdateSource)
{
    public IReadOnlyList<CaptureAssetSnapshot> Assets { get; init; } = [];
    public IReadOnlyList<CaptureStageSnapshot> Stages { get; init; } = [];
    public IReadOnlyList<string> Warnings { get; init; } = [];
    public string? AssetKind { get; init; }
    public string? ResultAssetUrl { get; init; }
    public string WarningsDisplay => string.Join("; ", Warnings);
    public string StagesDisplay => string.Join(" → ", Stages.Select(stage => $"{stage.Name}:{stage.Status}"));

    // 终态任务不会再被后台轮询覆盖；事件和查询只负责补充结果字段。
    // Terminal tasks are not overwritten by background polling; events and queries only enrich the result.
    public bool IsTerminal => Status is CaptureTaskStatus.Succeeded or CaptureTaskStatus.Failed;
    // 服务端时间统一转换成本机时间，避免任务表和时间线显示时区不一致。
    // Convert server timestamps to local time consistently for the task grid and timeline.
    public string LocalUpdatedAtDisplay => UpdatedAt.ToLocalTime().ToString("yyyy/MM/dd HH:mm:ss");
}

public sealed class CaptureTaskTracker
{
    // 事件回调和轮询任务运行在线程池线程，列表读写必须通过同一把锁保护。
    // Event callbacks and polling run on thread-pool threads, so all list access uses the same lock.
    private readonly object _sync = new();
    private readonly List<CaptureTaskSnapshot> _tasks = [];
    private readonly Func<TimeSpan, CancellationToken, Task> _delay;
    private readonly int _maximumPollAttempts;

    public CaptureTaskTracker(Func<TimeSpan, CancellationToken, Task>? delay = null, int maximumPollAttempts = 40)
    {
        if (maximumPollAttempts < 1) throw new ArgumentOutOfRangeException(nameof(maximumPollAttempts));
        _delay = delay ?? Task.Delay;
        _maximumPollAttempts = maximumPollAttempts;
    }

    public IReadOnlyList<CaptureTaskSnapshot> Tasks
    {
        get
        {
            lock (_sync)
            {
                // 返回快照而不是内部 List，避免 UI 绑定期间被后台线程修改而崩溃。
                // Return a snapshot instead of the internal list so UI binding cannot race with updates.
                return _tasks.ToArray();
            }
        }
    }
    public event EventHandler<CaptureTaskSnapshot>? Changed;

    public CaptureTaskSnapshot Register(string taskId, string outputFormat)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(taskId);
        CaptureTaskSnapshot task;
        lock (_sync)
        {
            var existing = FindUnsafe(taskId);
            if (existing is not null)
            {
                // capture.take 的响应可能晚于 capture.* 事件；事件先到时用表单格式补齐占位值。
                // capture.take may respond after a capture.* event; use the form format when the event arrives first.
                if (!string.IsNullOrWhiteSpace(existing.OutputFormat) && existing.OutputFormat != "-") return existing;
                task = existing with { OutputFormat = outputFormat };
                ReplaceUnsafe(task);
            }
            else
            {
                var now = DateTimeOffset.UtcNow;
                task = new CaptureTaskSnapshot(taskId, CaptureTaskStatus.Queued, outputFormat, now, now, null, null, null, 0, CaptureTaskUpdateSource.Submitted);
                _tasks.Insert(0, task);
            }
        }
        RaiseChanged(task);
        return task;
    }

    public void ApplyPayload(JsonElement payload, CaptureTaskUpdateSource source = CaptureTaskUpdateSource.Event)
    {
        // 事件通常是 { task: {...} }，capture.get 也可能直接返回任务对象，统一先拆包。
        // Events usually wrap the task as { task: {...} }, while capture.get may return it directly; unwrap both.
        var taskPayload = UnwrapTask(payload);
        var taskId = FindString(taskPayload, "task_id");
        if (string.IsNullOrWhiteSpace(taskId)) return;
        CaptureTaskSnapshot task;
        lock (_sync)
        {
            var existing = FindUnsafe(taskId);
            if (existing is null)
            {
                // 即使用户没有先看到 capture.take 响应，也要让事件创建一条可追踪的任务记录。
                // Create a trackable task even when its event arrives before the capture.take response is observed.
                var now = DateTimeOffset.UtcNow;
                existing = new CaptureTaskSnapshot(taskId, CaptureTaskStatus.Queued, FindString(taskPayload, "format") ?? "-", now, now, null, null, null, 0, CaptureTaskUpdateSource.Submitted);
                _tasks.Insert(0, existing);
            }
            var parsedAssets = ParseAssets(taskPayload);
            var assets = parsedAssets.Count > 0 ? parsedAssets : existing.Assets;
            var previewAsset = SelectAsset(assets,
                "final_thumbnail", "color_processed_thumbnail", "page_processed_thumbnail", "original_thumbnail",
                "final", "color_processed", "page_processed", "original");
            var resultAsset = SelectAsset(assets, "final", "color_processed", "page_processed", "original");
            var parsedStages = ParseStages(taskPayload);
            var parsedWarnings = ParseWarnings(taskPayload);
            task = existing with
            {
                Status = ParseStatus(FindString(taskPayload, "status")),
                UpdatedAt = DateTimeOffset.UtcNow,
                // 结果字段采用“新值优先、旧值兜底”，防止后续状态事件擦掉已收到的 Asset。
                // Prefer new values and fall back to old ones so later status events do not erase an Asset.
                AssetUrl = previewAsset?.AccessUrl ?? existing.AssetUrl,
                AssetContentType = previewAsset?.ContentType ?? existing.AssetContentType,
                Error = FindString(taskPayload, "error") ?? FindString(taskPayload, "message") ?? existing.Error,
                LastUpdateSource = source,
                Assets = assets,
                Stages = parsedStages.Count > 0 ? parsedStages : existing.Stages,
                Warnings = parsedWarnings.Count > 0 ? parsedWarnings : existing.Warnings,
                AssetKind = previewAsset?.Kind ?? existing.AssetKind,
                ResultAssetUrl = resultAsset?.AccessUrl ?? existing.ResultAssetUrl
            };
            ReplaceUnsafe(task);
        }
        RaiseChanged(task);
    }

    public async Task<CaptureTaskSnapshot?> PollUntilTerminalAsync(
        string taskId,
        Func<string, CancellationToken, Task<JsonElement>> getTask,
        CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(taskId);
        ArgumentNullException.ThrowIfNull(getTask);
        for (var attempt = 0; attempt < _maximumPollAttempts; attempt++)
        {
            var existing = Find(taskId);
            if (existing?.IsTerminal == true) return existing;
            // 首次查询也稍作延迟，给设备事件通道留出先返回的机会，减少无意义的重复请求。
            // Delay the first poll briefly to give the event channel time to deliver its update.
            await _delay(TimeSpan.FromMilliseconds(750), cancellationToken).ConfigureAwait(false);
            MarkPollAttempt(taskId);
            var payload = await getTask(taskId, cancellationToken).ConfigureAwait(false);
            ApplyPayload(payload, CaptureTaskUpdateSource.Poll);
        }
        return Find(taskId);
    }

    public void Clear()
    {
        lock (_sync)
        {
            _tasks.Clear();
        }
    }

    public void MarkPollAttempt(string taskId)
    {
        CaptureTaskSnapshot? task;
        lock (_sync)
        {
            var existing = FindUnsafe(taskId);
            if (existing is null || existing.IsTerminal) return;
            task = existing with { PollAttempts = existing.PollAttempts + 1, UpdatedAt = DateTimeOffset.UtcNow, LastUpdateSource = CaptureTaskUpdateSource.Poll };
            ReplaceUnsafe(task);
        }
        RaiseChanged(task);
    }

    private CaptureTaskSnapshot? Find(string taskId)
    {
        lock (_sync)
        {
            return FindUnsafe(taskId);
        }
    }

    private CaptureTaskSnapshot? FindUnsafe(string taskId) => _tasks.FirstOrDefault(item => string.Equals(item.TaskId, taskId, StringComparison.Ordinal));

    private void ReplaceUnsafe(CaptureTaskSnapshot task)
    {
        var index = _tasks.FindIndex(item => string.Equals(item.TaskId, task.TaskId, StringComparison.Ordinal));
        if (index >= 0) _tasks[index] = task;
        else _tasks.Insert(0, task);
    }

    private static JsonElement UnwrapTask(JsonElement payload) =>
        payload.ValueKind == JsonValueKind.Object && payload.TryGetProperty("task", out var task) && task.ValueKind == JsonValueKind.Object ? task : payload;

    private static CaptureTaskStatus ParseStatus(string? status) => status?.ToLowerInvariant() switch
    {
        "queued" or "pending" or "accepted" => CaptureTaskStatus.Queued,
        "running" or "processing" => CaptureTaskStatus.Running,
        "succeeded" or "completed" or "success" => CaptureTaskStatus.Succeeded,
        "failed" or "error" => CaptureTaskStatus.Failed,
        _ => CaptureTaskStatus.Unknown
    };

    private static IReadOnlyList<CaptureAssetSnapshot> ParseAssets(JsonElement payload)
    {
        if (!payload.TryGetProperty("assets", out var assets) || assets.ValueKind != JsonValueKind.Array) return [];
        return assets.EnumerateArray()
            .Where(asset => asset.ValueKind == JsonValueKind.Object)
            .Select(asset => new CaptureAssetSnapshot(
                FindString(asset, "asset_id") ?? string.Empty,
                FindString(asset, "kind") ?? string.Empty,
                FindString(asset, "url"),
                FindString(asset, "download_url"),
                FindString(asset, "path"),
                FindString(asset, "content_type"),
                FindInt32(asset, "width"),
                FindInt32(asset, "height"),
                FindInt64(asset, "size")))
            .ToArray();
    }

    private static IReadOnlyList<CaptureStageSnapshot> ParseStages(JsonElement payload)
    {
        if (!payload.TryGetProperty("stages", out var stages) || stages.ValueKind != JsonValueKind.Array) return [];
        return stages.EnumerateArray()
            .Where(stage => stage.ValueKind == JsonValueKind.Object)
            .Select(stage => new CaptureStageSnapshot(
                FindString(stage, "name") ?? string.Empty,
                FindString(stage, "status") ?? string.Empty,
                FindString(stage, "provider") ?? string.Empty,
                FindString(stage, "message") ?? string.Empty))
            .ToArray();
    }

    private static IReadOnlyList<string> ParseWarnings(JsonElement payload)
    {
        if (!payload.TryGetProperty("warnings", out var warnings) || warnings.ValueKind != JsonValueKind.Array) return [];
        return warnings.EnumerateArray()
            .Where(value => value.ValueKind == JsonValueKind.String && !string.IsNullOrWhiteSpace(value.GetString()))
            .Select(value => value.GetString()!)
            .ToArray();
    }

    private static CaptureAssetSnapshot? SelectAsset(IReadOnlyList<CaptureAssetSnapshot> assets, params string[] preferredKinds)
    {
        foreach (var kind in preferredKinds)
        {
            var match = assets.FirstOrDefault(asset =>
                !string.IsNullOrWhiteSpace(asset.AccessUrl) &&
                (string.Equals(asset.Kind, kind, StringComparison.Ordinal) || asset.Kind.StartsWith(kind + "_", StringComparison.Ordinal)));
            if (match is not null) return match;
        }
        return assets.FirstOrDefault(asset => !string.IsNullOrWhiteSpace(asset.AccessUrl));
    }

    private static int FindInt32(JsonElement data, string property) =>
        data.TryGetProperty(property, out var value) && value.TryGetInt32(out var parsed) ? parsed : 0;

    private static long FindInt64(JsonElement data, string property) =>
        data.TryGetProperty(property, out var value) && value.TryGetInt64(out var parsed) ? parsed : 0;

    private static string? FindString(JsonElement data, string property) =>
        data.ValueKind == JsonValueKind.Object && data.TryGetProperty(property, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() : null;

    private void RaiseChanged(CaptureTaskSnapshot task) => Changed?.Invoke(this, task);
}
