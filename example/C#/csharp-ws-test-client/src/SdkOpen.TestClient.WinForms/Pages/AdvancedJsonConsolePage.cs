using System.Text.Json;

namespace SdkOpen.TestClient.WinForms;

public sealed class AdvancedJsonConsolePage : UserControl, ILocalizablePage
{
    private readonly AppComposition _composition;
    private readonly Action<string, string> _showProtocol;
    private readonly TextBox _method = new() { Dock = DockStyle.Fill, PlaceholderText = "例如：system.ping" };
    private readonly RichTextBox _parameters = new() { Dock = DockStyle.Fill, Font = new Font("Consolas", 10), Text = "{}" };
    private readonly Label _status = new() { AutoSize = true };
    private readonly Button _execute = new() { AutoSize = true };
    private readonly Label _notice = new() { AutoSize = true, ForeColor = Color.DarkOrange };
    private UiLanguage _language = UiLanguage.Chinese;

    public AdvancedJsonConsolePage(AppComposition composition, Action<string, string> showProtocol)
    {
        _composition = composition;
        _showProtocol = showProtocol;
        _execute.Text = "发送原始 SDK 命令";
        _execute.Click += async (_, _) => await ExecuteAsync();
        _notice.Text = "高级工具：可调用目录外的公开 SDK 方法；参数必须是 JSON 对象。";
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12), RowCount = 5 };
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.Controls.Add(_notice, 0, 0);
        layout.Controls.Add(_method, 0, 1);
        layout.Controls.Add(_parameters, 0, 2);
        layout.Controls.Add(_execute, 0, 3);
        layout.Controls.Add(_status, 0, 4);
        Controls.Add(layout);
    }

    public void ApplyLanguage(UiLanguage language)
    {
        _language = language;
        _method.PlaceholderText = language == UiLanguage.English ? "Example: system.ping" : "例如：system.ping";
        _execute.Text = language == UiLanguage.English ? "Send Raw SDK Command" : "发送原始 SDK 命令";
        _notice.Text = language == UiLanguage.English
            ? "Advanced tool: call public SDK methods not listed in the catalog; parameters must be a JSON object."
            : "高级工具：可调用目录外的公开 SDK 方法；参数必须是 JSON 对象。";
    }

    private async Task ExecuteAsync()
    {
        try
        {
            if (string.IsNullOrWhiteSpace(_method.Text))
            {
                throw new InvalidOperationException(_language == UiLanguage.English ? "Method is required." : "必须填写 method。");
            }
            using var document = JsonDocument.Parse(_parameters.Text);
            if (document.RootElement.ValueKind != JsonValueKind.Object)
            {
                throw new JsonException(_language == UiLanguage.English ? "Parameters must be a JSON object." : "参数必须是 JSON 对象。");
            }
            var response = await _composition.Coordinator.ExecuteAsync(_method.Text.Trim(), document.RootElement, CancellationToken.None);
            _showProtocol(JsonSerializer.Serialize(new { method = _method.Text.Trim(), @params = document.RootElement }), JsonSerializer.Serialize(response));
            _status.Text = _language == UiLanguage.English ? $"Completed: code {response.Code}" : $"完成：code {response.Code}";
        }
        catch (Exception exception)
        {
            _status.Text = _language == UiLanguage.English ? $"Not sent: {exception.Message}" : $"未发送：{exception.Message}";
        }
    }
}
