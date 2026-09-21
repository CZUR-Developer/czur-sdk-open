using System.Net.Http.Headers;
using System.Text.Json;

namespace SdkOpen.TestClient.Core.Transport;

public static class ImageUploadRequestFactory
{
    public static HttpRequestMessage Create(
        Uri assetEndpoint,
        string sessionToken,
        Stream imageStream,
        string fileName,
        string contentType)
    {
        ArgumentNullException.ThrowIfNull(assetEndpoint);
        ArgumentException.ThrowIfNullOrWhiteSpace(sessionToken);
        ArgumentNullException.ThrowIfNull(imageStream);
        ArgumentException.ThrowIfNullOrWhiteSpace(fileName);
        ArgumentException.ThrowIfNullOrWhiteSpace(contentType);

        var request = new HttpRequestMessage(HttpMethod.Post, new Uri(assetEndpoint, "/api/uploads/images"));
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", sessionToken);
        var fileContent = new StreamContent(imageStream);
        fileContent.Headers.ContentType = MediaTypeHeaderValue.Parse(contentType);
        var multipart = new MultipartFormDataContent();
        multipart.Add(fileContent, "file", fileName);
        request.Content = multipart;
        return request;
    }

    public static string ParseUploadId(string json)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(json);
        using var document = JsonDocument.Parse(json);
        if (document.RootElement.ValueKind == JsonValueKind.Object &&
            document.RootElement.TryGetProperty("upload_id", out var value) &&
            value.ValueKind == JsonValueKind.String &&
            !string.IsNullOrWhiteSpace(value.GetString()))
        {
            return value.GetString()!;
        }

        throw new InvalidOperationException("Image upload response did not contain upload_id.");
    }

    public static string ContentTypeFromFileName(string fileName) => Path.GetExtension(fileName).ToLowerInvariant() switch
    {
        ".jpg" or ".jpeg" => "image/jpeg",
        ".png" => "image/png",
        ".bmp" => "image/bmp",
        ".gif" => "image/gif",
        ".webp" => "image/webp",
        ".tif" or ".tiff" => "image/tiff",
        _ => "application/octet-stream"
    };
}
