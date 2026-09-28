namespace SdkOpen.TestClient.Core.Application;

public sealed record CommandTemplate(string Category, string Method, string ParametersJson);

public sealed class CommandCatalog
{
    private readonly IReadOnlyList<CommandTemplate> _templates =
    [
        new("系统与认证", "system.ping", "{}"), new("系统与认证", "system.info", "{}"), new("系统与认证", "system.capabilities", "{}"),
        new("系统与认证", "auth.get_context", "{}"), new("系统与认证", "auth.refresh_session", "{}"), new("系统与认证", "auth.destroy_session", "{}"),
        new("设备与视频", "device.list", "{}"), new("设备与视频", "device.get", "{\n  \"device_id\": \"\"\n}"), new("设备与视频", "device.open", "{\n  \"device_id\": \"\"\n}"), new("设备与视频", "device.close", "{}"),
        new("设备与视频", "video.start", "{\n  \"format\": \"mjpeg\"\n}"), new("设备与视频", "video.stop", "{}"), new("设备与视频", "video.set_format", "{\n  \"format\": \"mjpeg\"\n}"), new("设备与视频", "video.set_profile", "{}"),
        new("采集", "capture.take", "{}"), new("采集", "capture.get", "{\n  \"capture_id\": \"\"\n}"), new("采集", "capture.set_turn_detect", "{\n  \"enabled\": true\n}"),
        new("图像处理", "image.process", "{}"), new("图像处理", "image.process_page", "{}"), new("图像处理", "image.apply_color_mode", "{}"),
        new("图像增强", "image.enhance", "{}"), new("图像增强", "image.enhance_capabilities", "{}"), new("图像增强", "image.enhance_get", "{}"), new("图像增强", "image.enhance_workflow_list", "{}"), new("图像增强", "image.enhance_workflow_save", "{}"), new("图像增强", "image.enhance_workflow_delete", "{}"),
        new("OCR", "ocr.recognize", "{}"), new("OCR", "ocr.extract_text", "{}"), new("OCR", "ocr.get", "{}"),
        new("文件转换", "file.convert", "{}"), new("SANE", "sane.list_devices", "{}"), new("TWAIN", "twain.list_sources", "{}"), new("存储", "storage.cleanup_temp", "{}")
    ];

    public IReadOnlyList<CommandTemplate> All => _templates;
    public IEnumerable<IGrouping<string, CommandTemplate>> ByCategory() => _templates.GroupBy(template => template.Category);
}
