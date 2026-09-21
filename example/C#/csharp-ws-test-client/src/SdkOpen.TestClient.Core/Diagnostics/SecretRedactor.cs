using System.Text.Json;
using System.Text.Encodings.Web;
using System.Text.Json.Nodes;

namespace SdkOpen.TestClient.Core.Diagnostics;

public static class SecretRedactor
{
    private static readonly HashSet<string> SensitiveProperties = new(StringComparer.OrdinalIgnoreCase)
    {
        "token",
        "session_token",
        "authorization",
        "api_key",
        "secret"
    };

    public static string Redact(string content)
    {
        if (string.IsNullOrEmpty(content))
        {
            return content;
        }

        try
        {
            // 时间线同时记录请求、响应和事件；先解析 JSON 再递归脱敏，避免 Token 泄露到 UI 或日志。
            // The timeline records requests, responses, and events; parse and redact JSON before exposing it.
            var root = JsonNode.Parse(content);
            if (root is null)
            {
                return content;
            }

            RedactNode(root);
            return root.ToJsonString(new JsonSerializerOptions
            {
                WriteIndented = true,
                Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping
            });
        }
        catch (JsonException)
        {
            // 非 JSON 文本保持原样，协议调试仍可看到服务端返回的诊断信息。
            // Preserve non-JSON text so protocol diagnostics from the server remain visible.
            return content;
        }
    }

    private static void RedactNode(JsonNode node)
    {
        if (node is JsonObject jsonObject)
        {
            foreach (var property in jsonObject.ToArray())
            {
                if (SensitiveProperties.Contains(property.Key))
                {
                    jsonObject[property.Key] = "***";
                }
                else if (property.Value is not null)
                {
                    RedactNode(property.Value);
                }
            }

            return;
        }

        if (node is JsonArray jsonArray)
        {
            foreach (var item in jsonArray)
            {
                if (item is not null)
                {
                    RedactNode(item);
                }
            }
        }
    }
}
