using System.Text.Json;
using SdkOpen.TestClient.Core.Application;

namespace SdkOpen.TestClient.WinForms;

public sealed class QuickStartPage : UserControl, ILocalizablePage
{
    private readonly AppComposition _composition;
    private readonly Action<string, string> _showProtocol;
    private readonly ListBox _methods = new() { Dock = DockStyle.Fill };
    private readonly Label _status = new() { AutoSize = true };
    private readonly Button _execute = new() { AutoSize = true };
    private UiLanguage _language = UiLanguage.Chinese;

    public QuickStartPage(AppComposition composition, Action<string, string> showProtocol)
    {
        _composition = composition;
        _showProtocol = showProtocol;
        _methods.DataSource = _composition.Catalog.All.Where(item => item.Category == "系统与认证").ToList();
        _methods.DisplayMember = nameof(CommandTemplate.Method);
        _execute.Text = "执行所选命令";
        _execute.Click += async (_, _) => await ExecuteAsync();
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12), RowCount = 3 };
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.Controls.Add(_methods, 0, 0);
        layout.Controls.Add(_execute, 0, 1);
        layout.Controls.Add(_status, 0, 2);
        Controls.Add(layout);
    }

    public void ApplyLanguage(UiLanguage language)
    {
        _language = language;
        _execute.Text = language == UiLanguage.English ? "Execute Selected Command" : "执行所选命令";
    }

    private async Task ExecuteAsync()
    {
        if (_methods.SelectedItem is not CommandTemplate template)
        {
            return;
        }

        try
        {
            using var parameters = JsonDocument.Parse(template.ParametersJson);
            var response = await _composition.Coordinator.ExecuteAsync(template.Method, parameters.RootElement, CancellationToken.None);
            _showProtocol(JsonSerializer.Serialize(new { method = template.Method, @params = parameters.RootElement }), JsonSerializer.Serialize(response));
            _status.Text = _language == UiLanguage.English ? $"{template.Method} completed, code {response.Code}" : $"{template.Method} 已完成，code {response.Code}";
        }
        catch (Exception exception)
        {
            _status.Text = exception.Message;
        }
    }
}
