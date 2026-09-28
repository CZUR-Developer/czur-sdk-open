using SdkOpen.TestClient.Core.Diagnostics;

namespace SdkOpen.TestClient.WinForms;

public sealed class JsonInspectorControl : UserControl, ILocalizableControl
{
    private readonly RichTextBox _request = CreateBox();
    private readonly RichTextBox _response = CreateBox();
    private readonly TabControl _tabs = new() { Dock = DockStyle.Fill };

    public JsonInspectorControl()
    {
        Dock = DockStyle.Fill;
        _tabs.TabPages.Add(CreateTab("请求（已脱敏）", _request));
        _tabs.TabPages.Add(CreateTab("响应/事件（已脱敏）", _response));
        Controls.Add(_tabs);
    }

    public void ApplyLanguage(UiLanguage language)
    {
        var english = language == UiLanguage.English;
        _tabs.TabPages[0].Text = english ? "Request (redacted)" : "请求（已脱敏）";
        _tabs.TabPages[1].Text = english ? "Response/Event (redacted)" : "响应/事件（已脱敏）";
    }

    public void ShowRequest(string json) => _request.Text = SecretRedactor.Redact(json);
    public void ShowResponse(string json) => _response.Text = SecretRedactor.Redact(json);

    /// <summary>
    /// Shows a timeline payload in the matching protocol tab.
    /// 将时间线条目的完整脱敏内容同步到对应的协议页签。
    /// </summary>
    public void ShowTimelineEntry(TimelineEntry entry)
    {
        ArgumentNullException.ThrowIfNull(entry);

        var payload = TimelineDetailFormatter.FormatPayload(entry);
        if (string.Equals(entry.Category, "request", StringComparison.OrdinalIgnoreCase))
        {
            ShowRequest(payload);
            _tabs.SelectedIndex = 0;
        }
        else
        {
            ShowResponse(payload);
            _tabs.SelectedIndex = 1;
        }
    }

    private static TabPage CreateTab(string title, Control control)
    {
        var tab = new TabPage(title);
        tab.Controls.Add(control);
        return tab;
    }

    private static RichTextBox CreateBox() => new() { Dock = DockStyle.Fill, ReadOnly = true, Font = new Font("Consolas", 9), WordWrap = false };
}
