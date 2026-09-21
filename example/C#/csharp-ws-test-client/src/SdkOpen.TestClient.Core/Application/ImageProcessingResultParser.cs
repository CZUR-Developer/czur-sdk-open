using System.Text.Json;

namespace SdkOpen.TestClient.Core.Application;

public sealed record ImageProcessingOutput(
    string AssetId,
    string OutputId,
    int? Index,
    string Role,
    string Path,
    string Url,
    string DownloadUrl,
    string ContentType,
    int? Width,
    int? Height,
    long? Size)
{
    public string DisplayRole => !string.IsNullOrWhiteSpace(Role)
        ? Role
        : !string.IsNullOrWhiteSpace(OutputId) ? OutputId : $"#{(Index ?? 0) + 1}";
    public string AccessUrl => !string.IsNullOrWhiteSpace(Url) ? Url : DownloadUrl;
    public bool CanPreview => ContentType.ToLowerInvariant() is
        "image/jpeg" or "image/jpg" or "image/png" or "image/webp" or "image/gif" or "image/bmp";
}

public sealed record ImageProcessingResult(
    string TaskId,
    string OutputPath,
    IReadOnlyList<ImageProcessingOutput> Outputs);

public static class ImageProcessingResultParser
{
    public static ImageProcessingResult Parse(JsonElement data)
    {
        var outputs = new List<ImageProcessingOutput>();
        if (data.ValueKind == JsonValueKind.Object &&
            data.TryGetProperty("outputs", out var values) &&
            values.ValueKind == JsonValueKind.Array)
        {
            var ordinal = 0;
            foreach (var value in values.EnumerateArray())
            {
                if (value.ValueKind != JsonValueKind.Object) continue;
                var index = FindInt(value, "index") ?? ordinal;
                outputs.Add(new ImageProcessingOutput(
                    FindString(value, "asset_id") ?? $"output-{ordinal + 1}",
                    FindString(value, "output_id") ?? string.Empty,
                    index,
                    FindString(value, "role") ?? string.Empty,
                    FindString(value, "path") ?? string.Empty,
                    FindString(value, "url") ?? string.Empty,
                    FindString(value, "download_url") ?? string.Empty,
                    FindString(value, "content_type") ?? string.Empty,
                    FindInt(value, "width"),
                    FindInt(value, "height"),
                    FindLong(value, "size")));
                ordinal++;
            }
        }

        return new ImageProcessingResult(
            FindString(data, "task_id") ?? string.Empty,
            FindString(data, "output_path") ?? string.Empty,
            outputs);
    }

    private static string? FindString(JsonElement data, string property) =>
        data.ValueKind == JsonValueKind.Object && data.TryGetProperty(property, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;

    private static int? FindInt(JsonElement data, string property) =>
        data.ValueKind == JsonValueKind.Object && data.TryGetProperty(property, out var value) && value.TryGetInt32(out var number)
            ? number
            : null;

    private static long? FindLong(JsonElement data, string property) =>
        data.ValueKind == JsonValueKind.Object && data.TryGetProperty(property, out var value) && value.TryGetInt64(out var number)
            ? number
            : null;
}
