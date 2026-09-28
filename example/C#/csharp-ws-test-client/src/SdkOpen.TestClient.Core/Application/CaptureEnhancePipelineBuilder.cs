using System.Text.Json;
using System.Text.Json.Nodes;

namespace SdkOpen.TestClient.Core.Application;

public sealed record ImageEnhanceWorkflow(string WorkflowId, string Name, string Description, JsonElement Pipeline)
{
    public string DisplayName => string.IsNullOrWhiteSpace(Name) ? WorkflowId : Name;
}

/// <summary>
/// Builds the pipeline envelope used consistently by video.start,
/// video.set_profile, and capture.take.
/// 为预览启动、实时参数更新和最终采集构造同一份增强流水线。
/// </summary>
public static class CaptureEnhancePipelineBuilder
{
    public static JsonElement Build(JsonElement? workflowPipeline, CaptureOutputFormat outputFormat)
    {
        JsonObject pipeline;
        if (workflowPipeline is { ValueKind: JsonValueKind.Object } value)
        {
            pipeline = JsonNode.Parse(value.GetRawText())?.AsObject() ?? new JsonObject();
        }
        else
        {
            pipeline = new JsonObject();
        }

        if (pipeline["version"] is not JsonValue version ||
            !version.TryGetValue<string>(out var versionText) ||
            string.IsNullOrWhiteSpace(versionText))
        {
            pipeline["version"] = "image.enhance.pipeline.v1";
        }
        if (pipeline["steps"] is not JsonArray)
        {
            pipeline["steps"] = new JsonArray();
        }
        pipeline["target"] = new JsonObject
        {
            ["type"] = "images",
            ["format"] = outputFormat.ToString().ToLowerInvariant(),
            ["export_type"] = "single-page"
        };
        if (pipeline["options"] is not JsonObject)
        {
            pipeline["options"] = new JsonObject
            {
                ["keep_intermediate"] = false,
                ["include_metadata"] = true
            };
        }

        return JsonSerializer.SerializeToElement(pipeline);
    }
}
