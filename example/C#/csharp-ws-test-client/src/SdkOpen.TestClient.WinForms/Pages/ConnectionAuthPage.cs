using SdkOpen.TestClient.Core.Configuration;
using System.Text.Json;

namespace SdkOpen.TestClient.WinForms;

public sealed class ConnectionAuthPage : UserControl, ILocalizablePage
{
    private readonly AppComposition _composition;
    private readonly Action<string, string> _showProtocol;
    private readonly TextBox _health = new();
    private readonly TextBox _command = new();
    private readonly TextBox _video = new();
    private readonly TextBox _asset = new();
    private readonly TextBox _alias = new();
    private readonly TextBox _apiKey = new() { UseSystemPasswordChar = true };
    private readonly Label _summary = new() { AutoSize = true };
    private readonly Button _connect = new() { AutoSize = true };
    private readonly Button _context = new() { AutoSize = true };
    private readonly Button _refresh = new() { AutoSize = true };
    private readonly Button _disconnect = new() { AutoSize = true };
    private Label? _healthLabel;
    private Label? _commandLabel;
    private Label? _videoLabel;
    private Label? _assetLabel;
    private Label? _aliasLabel;
    private Label? _tokenLabel;
    private Label? _operationLabel;
    private Label? _statusLabel;
    private UiLanguage _language = UiLanguage.Chinese;

    public ConnectionAuthPage(AppComposition composition, Action<string, string> showProtocol)
    {
        _composition = composition;
        _showProtocol = showProtocol;
        BuildLayout();
        LoadProfileAsync();
    }

    private async void LoadProfileAsync()
    {
        var profile = await _composition.ProfileStore.LoadAsync(CancellationToken.None);
        _health.Text = profile.HealthEndpoint.AbsoluteUri;
        _command.Text = profile.CommandEndpoint.AbsoluteUri;
        _video.Text = profile.VideoEndpoint.AbsoluteUri;
        _asset.Text = profile.AssetEndpoint.AbsoluteUri;
        _alias.Text = profile.ApiKeyAlias ?? string.Empty;
    }

    private void BuildLayout()
    {
        var form = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 2, Padding = new Padding(16) };
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 150));
        form.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        _healthLabel = AddRow(form, "Health URL", _health);
        _commandLabel = AddRow(form, "Command WebSocket", _command);
        _videoLabel = AddRow(form, "Video WebSocket", _video);
        _assetLabel = AddRow(form, "Asset URL", _asset);
        _aliasLabel = AddRow(form, "API Token 别名", _alias);
        _tokenLabel = AddRow(form, "API Token（DPAPI）", _apiKey);
        var buttons = new FlowLayoutPanel { AutoSize = true };
        _connect.Text = "保存并连接";
        _connect.Click += async (_, _) => await ConnectAsync();
        _context.Text = "刷新认证上下文";
        _context.Click += async (_, _) => await ExecuteAsync("auth.get_context");
        _refresh.Text = "刷新会话";
        _refresh.Click += async (_, _) => await ExecuteAsync("auth.refresh_session");
        _disconnect.Text = "断开连接";
        _disconnect.Click += async (_, _) => await DisconnectAsync();
        buttons.Controls.AddRange([_connect, _context, _refresh, _disconnect]);
        _operationLabel = AddRow(form, "操作", buttons);
        _statusLabel = AddRow(form, "状态", _summary);
        Controls.Add(form);
    }

    public void ApplyLanguage(UiLanguage language)
    {
        _language = language;
        var english = language == UiLanguage.English;
        _healthLabel!.Text = "Health URL";
        _commandLabel!.Text = "Command WebSocket";
        _videoLabel!.Text = "Video WebSocket";
        _assetLabel!.Text = "Asset URL";
        _aliasLabel!.Text = english ? "API Token Alias" : "API Token 别名";
        _tokenLabel!.Text = english ? "API Token (DPAPI)" : "API Token（DPAPI）";
        _operationLabel!.Text = english ? "Actions" : "操作";
        _statusLabel!.Text = english ? "Status" : "状态";
        _connect.Text = language == UiLanguage.English ? "Save and Connect" : "保存并连接";
        _context.Text = language == UiLanguage.English ? "Refresh Auth Context" : "刷新认证上下文";
        _refresh.Text = language == UiLanguage.English ? "Refresh Session" : "刷新会话";
        _disconnect.Text = language == UiLanguage.English ? "Disconnect" : "断开连接";
        _summary.Text = _composition.Coordinator.Session.IsAuthenticated
            ? (english ? $"Authenticated, capabilities: {_composition.Coordinator.Session.Capabilities.Count}" : $"已认证，能力数：{_composition.Coordinator.Session.Capabilities.Count}")
            : (english ? "Not connected" : "未连接");
    }

    private async Task ConnectAsync()
    {
        try
        {
            var profile = ReadProfile();
            await _composition.ProfileStore.SaveAsync(profile, CancellationToken.None);
            var key = _apiKey.Text;
            if (string.IsNullOrWhiteSpace(key) && !string.IsNullOrWhiteSpace(profile.ApiKeyAlias))
            {
                key = await _composition.SecretStore.LoadAsync(profile.ApiKeyAlias, CancellationToken.None) ?? string.Empty;
            }
            else if (!string.IsNullOrWhiteSpace(key) && !string.IsNullOrWhiteSpace(profile.ApiKeyAlias))
            {
                await _composition.SecretStore.SaveAsync(profile.ApiKeyAlias, key, CancellationToken.None);
            }

            if (string.IsNullOrWhiteSpace(key))
            {
                throw new InvalidOperationException(_language == UiLanguage.English ? "Enter an API token or a saved token alias." : "请输入 API Token，或填写已保存 Token 对应的别名。");
            }

            var result = await _composition.Coordinator.ConnectAsync(profile, key, CancellationToken.None);
            if (result.IsAuthenticated)
            {
                _composition.SetActiveProfile(profile);
            }
            _apiKey.Clear();
            _summary.Text = result.IsAuthenticated
                ? (_language == UiLanguage.English ? $"Authenticated, capabilities: {_composition.Coordinator.Session.Capabilities.Count}" : $"已认证，能力数：{_composition.Coordinator.Session.Capabilities.Count}")
                : result.Error ?? (_language == UiLanguage.English ? "Connection failed" : "连接失败");
            _showProtocol("{\"action\":\"connect\"}", JsonSerializer.Serialize(result));
        }
        catch (Exception exception)
        {
            _summary.Text = exception.Message;
        }
    }

    private async Task ExecuteAsync(string method)
    {
        try
        {
            var response = await _composition.Coordinator.ExecuteAsync(method, JsonSerializer.SerializeToElement(new { }), CancellationToken.None);
            _showProtocol(JsonSerializer.Serialize(new { method, @params = new { } }), JsonSerializer.Serialize(response));
            _summary.Text = $"{method}: code {response.Code}";
        }
        catch (Exception exception)
        {
            _summary.Text = exception.Message;
        }
    }

    private async Task DisconnectAsync()
    {
        await _composition.Coordinator.DisconnectAsync(CancellationToken.None);
        _summary.Text = _language == UiLanguage.English ? "Disconnected; the in-memory session was cleared." : "已断开连接；内存中的 session 已清除。";
        _showProtocol("{\"action\":\"disconnect\"}", "{\"status\":\"closed\"}");
    }

    private EndpointProfile ReadProfile() => new("当前配置", new Uri(_health.Text), new Uri(_command.Text), new Uri(_video.Text), new Uri(_asset.Text), string.IsNullOrWhiteSpace(_alias.Text) ? null : _alias.Text.Trim());

    private static Label AddRow(TableLayoutPanel panel, string label, Control control)
    {
        var row = panel.RowCount++;
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        var labelControl = new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(3, 8, 3, 3) };
        panel.Controls.Add(labelControl, 0, row);
        control.Dock = control is TextBox ? DockStyle.Fill : DockStyle.None;
        panel.Controls.Add(control, 1, row);
        return labelControl;
    }
}
