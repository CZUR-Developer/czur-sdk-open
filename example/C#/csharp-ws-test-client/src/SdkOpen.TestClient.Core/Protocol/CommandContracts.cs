using System.Text.Json;
using System.Text.Json.Serialization;

namespace SdkOpen.TestClient.Core.Protocol;

public sealed record ClientMetadata(
    [property: JsonPropertyName("source")] string Source,
    [property: JsonPropertyName("protocol_version")] string ProtocolVersion,
    [property: JsonPropertyName("trace_id")] string TraceId);

public sealed record CommandRequest(
    [property: JsonPropertyName("request_id")] string RequestId,
    [property: JsonPropertyName("method")] string Method,
    [property: JsonPropertyName("params")] JsonElement Params,
    [property: JsonPropertyName("client")] ClientMetadata Client);

public sealed record CommandResponse(
    [property: JsonPropertyName("request_id")] string RequestId,
    [property: JsonPropertyName("code")] int Code,
    [property: JsonPropertyName("message")] string Message,
    [property: JsonPropertyName("data")] JsonElement Data,
    [property: JsonPropertyName("ts")] long Ts);

public sealed record CommandEvent(
    [property: JsonPropertyName("event")] string Event,
    [property: JsonPropertyName("code")] int Code,
    [property: JsonPropertyName("message")] string Message,
    [property: JsonPropertyName("payload")] JsonElement Payload,
    [property: JsonPropertyName("ts")] long Ts);
