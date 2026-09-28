using System.Text.Json;
using SdkOpen.TestClient.Core.Application;

namespace SdkOpen.TestClient.WinForms;

public sealed class CommandPage : UserControl, ILocalizablePage
{
    private readonly AppComposition _composition;
    private readonly Action<string, string> _showProtocol;
    private readonly ComboBox _commands = new() { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill };
    private readonly RichTextBox _parameters = new() { Dock = DockStyle.Fill, Font = new Font("Consolas", 10), Text = "{}" };
    private readonly Label _status = new() { AutoSize = true };
    private readonly Label _notice = new() { AutoSize = true, ForeColor = Color.DarkOrange };
    private readonly Button _execute = new() { AutoSize = true };
    private readonly bool _jsonExecutionEnabled;
    private UiLanguage _language = UiLanguage.Chinese;

    public CommandPage(
        AppComposition composition,
        string category,
        Action<string, string> showProtocol,
        bool jsonExecutionEnabled = true)
    {
        _composition = composition;
        _showProtocol = showProtocol;
        _jsonExecutionEnabled = jsonExecutionEnabled;
        Text = category;
        _commands.DataSource = _composition.Catalog.All.Where(item => item.Category == category).ToList();
        _commands.DisplayMember = nameof(CommandTemplate.Method);
        _commands.SelectedIndexChanged += (_, _) =>
        {
            if (_commands.SelectedItem is CommandTemplate template)
            {
                _parameters.Text = template.ParametersJson;
            }
        };
        BuildLayout();
    }

    private void BuildLayout()
    {
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12), ColumnCount = 1, RowCount = 5 };
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.Controls.Add(_commands, 0, 0);
        layout.Controls.Add(_parameters, 0, 1);
        _execute.Text = "执行命令";
        _execute.Click += async (_, _) => await ExecuteAsync();
        layout.Controls.Add(_execute, 0, 2);
        layout.Controls.Add(_notice, 0, 3);
        layout.Controls.Add(_status, 0, 4);
        Controls.Add(layout);
        if (_commands.Items.Count > 0)
        {
            _commands.SelectedIndex = 0;
        }

        ApplyJsonExecutionState();
    }

    public void ApplyLanguage(UiLanguage language)
    {
        _language = language;
        _execute.Text = language == UiLanguage.English ? "Execute Command" : "执行命令";
        _notice.Text = _jsonExecutionEnabled
            ? string.Empty
            : language == UiLanguage.English
                ? "Raw JSON execution is disabled for Image Enhancement. Use Image Processing forms instead."
                : "图像增强的原始 JSON 命令已禁用，请使用图像处理页面中的表单。";
        if (!_jsonExecutionEnabled)
        {
            _status.Text = language == UiLanguage.English ? "Unavailable" : "当前不可用";
        }
    }

    private async Task ExecuteAsync()
    {
        if (!_jsonExecutionEnabled)
        {
            return;
        }

        if (_commands.SelectedItem is not CommandTemplate template)
        {
            return;
        }

        try
        {
            using var document = JsonDocument.Parse(_parameters.Text);
            if (document.RootElement.ValueKind != JsonValueKind.Object)
            {
                throw new JsonException(_language == UiLanguage.English ? "Parameters must be a JSON object." : "参数必须是 JSON 对象。");
            }
            var response = await _composition.Coordinator.ExecuteAsync(template.Method, document.RootElement, CancellationToken.None);
            _showProtocol(JsonSerializer.Serialize(new { method = template.Method, @params = document.RootElement }), JsonSerializer.Serialize(response));
            _status.Text = _language == UiLanguage.English ? $"Completed: code {response.Code}" : $"完成：code {response.Code}";
        }
        catch (Exception exception)
        {
            _status.Text = _language == UiLanguage.English ? $"Not sent: {exception.Message}" : $"未发送：{exception.Message}";
        }
    }

    private void ApplyJsonExecutionState()
    {
        // Keep the enhancement command list visible for diagnostics, but prevent editing/sending raw JSON.
        // 保留增强命令列表用于查看协议范围，但禁止编辑和发送原始 JSON。
        _commands.Enabled = _jsonExecutionEnabled;
        _parameters.ReadOnly = !_jsonExecutionEnabled;
        _execute.Enabled = _jsonExecutionEnabled;
    }
}
