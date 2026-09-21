using System.Text.Json;

namespace SdkOpen.TestClient.Core.Application;

public sealed record ImageEnhanceStepSnapshot(string Id, string Type, string Status);

public sealed record ImageEnhanceAssetSnapshot(string AssetId, string Url, string Path, string ContentType);

public sealed record ImageEnhanceCapabilitySnapshot(
    string Type,
    string Title,
    string Description,
    string Category,
    string Runtime,
    bool Available,
    string RequiresCapability,
    string UnavailableReason,
    JsonElement Defaults,
    JsonElement Schema);

public sealed record ImageEnhanceTaskSnapshot(
    string TaskId,
    string Status,
    int Progress,
    string Phase,
    IReadOnlyList<ImageEnhanceStepSnapshot> Steps,
    IReadOnlyList<ImageEnhanceAssetSnapshot> Assets,
    string Error)
{
    public static ImageEnhanceTaskSnapshot Empty { get; } = new(string.Empty, "idle", 0, string.Empty, [], [], string.Empty);
}

/// <summary>
/// Builds structured image-enhancement requests so the UI never asks users to compose command JSON.
/// 构造结构化图像增强请求，UI 不再要求用户手工拼接命令 JSON。
/// </summary>
public static class ImageEnhancementRequestBuilder
{
    public static JsonElement BuildEnhanceParameters(IEnumerable<string> uploadIds, JsonElement pipeline)
    {
        ArgumentNullException.ThrowIfNull(uploadIds);
        if (pipeline.ValueKind != JsonValueKind.Object)
        {
            throw new ArgumentException("Image enhancement pipeline must be a JSON object.", nameof(pipeline));
        }

        var ids = uploadIds.Where(id => !string.IsNullOrWhiteSpace(id)).ToArray();
        if (ids.Length == 0)
        {
            throw new ArgumentException("At least one upload id is required.", nameof(uploadIds));
        }

        return JsonSerializer.SerializeToElement(new
        {
            source = new
            {
                type = ids.Length == 1 ? "image" : "images",
                input_upload_ids = ids
            },
            pipeline
        });
    }

    public static JsonElement BuildWorkflowSaveParameters(string name, string description, JsonElement pipeline)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        if (pipeline.ValueKind != JsonValueKind.Object)
        {
            throw new ArgumentException("Image enhancement pipeline must be a JSON object.", nameof(pipeline));
        }

        return JsonSerializer.SerializeToElement(new
        {
            workflow = new
            {
                name = name.Trim(),
                description = description?.Trim() ?? string.Empty,
                pipeline
            }
        });
    }

    public static JsonElement BuildWorkflowDeleteParameters(string workflowId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(workflowId);
        return JsonSerializer.SerializeToElement(new { workflow_id = workflowId.Trim() });
    }

    public static ImageEnhanceTaskSnapshot ParseTask(JsonElement data)
    {
        if (!data.TryGetProperty("task", out var task) || task.ValueKind != JsonValueKind.Object)
        {
            return ImageEnhanceTaskSnapshot.Empty;
        }

        var steps = task.TryGetProperty("steps", out var rawSteps) && rawSteps.ValueKind == JsonValueKind.Array
            ? rawSteps.EnumerateArray()
                .Where(item => item.ValueKind == JsonValueKind.Object)
                .Select((item, index) => new ImageEnhanceStepSnapshot(
                    StringValue(item, "id") ?? $"step-{index + 1}",
                    StringValue(item, "type") ?? $"step-{index + 1}",
                    StringValue(item, "status") ?? string.Empty))
                .ToArray()
            : [];
        var assets = task.TryGetProperty("assets", out var rawAssets) && rawAssets.ValueKind == JsonValueKind.Array
            ? rawAssets.EnumerateArray()
                .Where(item => item.ValueKind == JsonValueKind.Object)
                .Select((item, index) => new ImageEnhanceAssetSnapshot(
                    StringValue(item, "asset_id") ?? $"asset-{index + 1}",
                    StringValue(item, "url") ?? string.Empty,
                    StringValue(item, "path") ?? string.Empty,
                    StringValue(item, "content_type") ?? string.Empty))
                .ToArray()
            : [];

        return new ImageEnhanceTaskSnapshot(
            StringValue(task, "task_id") ?? string.Empty,
            StringValue(task, "status") ?? "idle",
            NumberValue(task, "progress"),
            StringValue(task, "phase") ?? string.Empty,
            steps,
            assets,
            StringValue(task, "error") ?? string.Empty);
    }

    private static string? StringValue(JsonElement element, string propertyName) =>
        element.TryGetProperty(propertyName, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;

    private static int NumberValue(JsonElement element, string propertyName) =>
        element.TryGetProperty(propertyName, out var value) && value.TryGetInt32(out var number) ? number : 0;
}
