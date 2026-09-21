using SdkOpen.TestClient.Core.Diagnostics;

namespace SdkOpen.TestClient.WinForms;

public sealed class MainForm : Form
{
    private readonly AppComposition _composition;
    private readonly Panel _pageHost = new() { Dock = DockStyle.Fill };
    private readonly JsonInspectorControl _inspector = new();
    private readonly TimelineControl _timeline = new() { Dock = DockStyle.Fill };
    private readonly StatusSummaryControl _status = new();
    private readonly Dictionary<UiTextKey, Control> _pages;
    private readonly TreeView _navigation = new() { Dock = DockStyle.Fill, HideSelection = false, ShowNodeToolTips = true };
    private readonly ToolStripLabel _toolStripLabel = new();
    private readonly ToolStripButton _refreshTimelineButton = new();
    private readonly ToolStripButton _languageButton = new();
    private Control? _activePage;
    private TreeNode? _lastEnabledNavigationNode;
    private bool _restoringNavigation;
    private UiLanguage _language = UiLanguage.Chinese;

    public MainForm(AppComposition composition)
    {
        _composition = composition;
        Text = UiText.Get(_language, UiTextKey.ApplicationTitle);
        StartPosition = FormStartPosition.CenterScreen;
        MinimumSize = new Size(1180, 720);
        Size = new Size(1500, 920);
        _pages = CreatePages();
        BuildLayout();
        _timeline.EntrySelected += OnTimelineEntrySelected;
        ApplyLanguage();
        ShowPage(UiTextKey.ConnectionPage);
        RefreshTimeline();
    }

    public void ShowProtocol(string request, string response)
    {
        // 命令可能由后台事件触发，统一切回 UI 线程后再更新右侧协议检查器。
        // Commands may be raised from background events; marshal to the UI thread before updating the inspector.
        if (InvokeRequired)
        {
            BeginInvoke(() => ShowProtocol(request, response));
            return;
        }

        _inspector.ShowRequest(request);
        _inspector.ShowResponse(response);
        RefreshTimeline();
        _status.SetStatus(
            UiText.Get(_language, _composition.Coordinator.IsConnected ? UiTextKey.Connected : UiTextKey.Disconnected),
            _composition.Coordinator.IsConnected);
    }

    public void RefreshTimeline()
    {
        // TimelineControl 只接收快照，避免它在绑定过程中枚举正在增长的内部列表。
        // TimelineControl receives a snapshot so binding never enumerates a list that is still growing.
        if (InvokeRequired)
        {
            BeginInvoke(RefreshTimeline);
            return;
        }

        _timeline.SetEntries(_composition.Coordinator.Timeline);
    }

    private void OnTimelineEntrySelected(object? sender, TimelineEntry entry)
    {
        if (InvokeRequired)
        {
            BeginInvoke(() => OnTimelineEntrySelected(sender, entry));
            return;
        }

        _inspector.ShowTimelineEntry(entry);
    }

    private Dictionary<UiTextKey, Control> CreatePages() => new()
    {
        [UiTextKey.ConnectionPage] = new ConnectionAuthPage(_composition, ShowProtocol),
        [UiTextKey.QuickStartPage] = new QuickStartPage(_composition, ShowProtocol),
        [UiTextKey.DeviceVideoPage] = new DeviceVideoPage(_composition, ShowProtocol),
        [UiTextKey.CapturePage] = new CaptureAcquisitionPage(_composition, ShowProtocol),
        [UiTextKey.ImageProcessingPage] = new ImageProcessingPage(_composition, ShowProtocol),
        [UiTextKey.ImageEnhancementPage] = new ImageEnhancementPage(_composition, ShowProtocol),
        [UiTextKey.OcrPage] = new CommandPage(_composition, "OCR", ShowProtocol),
        [UiTextKey.FileConversionPage] = new CommandPage(_composition, "文件转换", ShowProtocol),
        [UiTextKey.TwainPage] = new CommandPage(_composition, "TWAIN", ShowProtocol),
        [UiTextKey.StoragePage] = new CommandPage(_composition, "存储", ShowProtocol),
        [UiTextKey.RawJsonPage] = new AdvancedJsonConsolePage(_composition, ShowProtocol)
    };

    private void BuildLayout()
    {
        var toolStrip = new ToolStrip();
        _toolStripLabel.Text = UiText.Get(_language, UiTextKey.TestClientLabel);
        toolStrip.Items.Add(_toolStripLabel);
        toolStrip.Items.Add(new ToolStripSeparator());
        _refreshTimelineButton.Click += (_, _) => RefreshTimeline();
        toolStrip.Items.Add(_refreshTimelineButton);
        toolStrip.Items.Add(new ToolStripSeparator());
        _languageButton.Click += (_, _) => ToggleLanguage();
        toolStrip.Items.Add(_languageButton);

        var statusStrip = new StatusStrip();
        _status.SetStatus("未连接", false);
        statusStrip.Items.Add(_status);

        foreach (var page in _pages.Keys)
        {
            var enabled = UiSurfacePolicy.IsNavigationEnabled(page);
            var node = new TreeNode { Tag = page, ForeColor = enabled ? SystemColors.WindowText : SystemColors.GrayText };
            _navigation.Nodes.Add(node);
        }
        _navigation.AfterSelect += (_, args) =>
        {
            if (args.Node?.Tag is UiTextKey pageKey)
            {
                if (!UiSurfacePolicy.IsNavigationEnabled(pageKey))
                {
                    if (!_restoringNavigation && _lastEnabledNavigationNode is not null)
                    {
                        _restoringNavigation = true;
                        _navigation.SelectedNode = _lastEnabledNavigationNode;
                        _restoringNavigation = false;
                    }
                    return;
                }
                _lastEnabledNavigationNode = args.Node;
                ShowPage(pageKey);
            }
        };
        _navigation.SelectedNode = _navigation.Nodes[0];
        _lastEnabledNavigationNode = _navigation.SelectedNode;

        // 三层 SplitContainer 对应“导航 | 工作区 | 协议检查器”和底部可拖拽时间线。
        // The three SplitContainers provide navigation, workspace, protocol inspector, and a resizable timeline.
        var centerAndInspector = new SplitContainer { Dock = DockStyle.Fill, SplitterDistance = 690 };
        centerAndInspector.Panel1.Controls.Add(_pageHost);
        centerAndInspector.Panel2.Controls.Add(_inspector);

        var navigationAndContent = new SplitContainer { Dock = DockStyle.Fill, SplitterDistance = 190, FixedPanel = FixedPanel.Panel1 };
        navigationAndContent.Panel1.Controls.Add(_navigation);
        navigationAndContent.Panel2.Controls.Add(centerAndInspector);

        var workspaceAndTimeline = new SplitContainer { Dock = DockStyle.Fill, Orientation = Orientation.Horizontal, SplitterDistance = 600, Panel1MinSize = 260, Panel2MinSize = 140 };
        workspaceAndTimeline.Panel1.Controls.Add(navigationAndContent);
        workspaceAndTimeline.Panel2.Controls.Add(_timeline);

        Controls.Add(workspaceAndTimeline);
        Controls.Add(statusStrip);
        Controls.Add(toolStrip);
        toolStrip.Dock = DockStyle.Top;
        statusStrip.Dock = DockStyle.Bottom;
    }

    private void ToggleLanguage()
    {
        _language = _language == UiLanguage.Chinese ? UiLanguage.English : UiLanguage.Chinese;
        ApplyLanguage();
    }

    private void ApplyLanguage()
    {
        Text = UiText.Get(_language, UiTextKey.ApplicationTitle);
        _toolStripLabel.Text = UiText.Get(_language, UiTextKey.TestClientLabel);
        _refreshTimelineButton.Text = UiText.Get(_language, UiTextKey.RefreshTimeline);
        _languageButton.Text = UiText.Get(_language, UiTextKey.ChineseLanguageButton);
        _status.SetStatus(
            UiText.Get(_language, _composition.Coordinator.IsConnected ? UiTextKey.Connected : UiTextKey.Disconnected),
            _composition.Coordinator.IsConnected);

        for (var index = 0; index < _navigation.Nodes.Count; index++)
        {
            if (_navigation.Nodes[index].Tag is UiTextKey key)
            {
                _navigation.Nodes[index].Text = UiText.Get(_language, key);
                _navigation.Nodes[index].ForeColor = UiSurfacePolicy.IsNavigationEnabled(key)
                    ? SystemColors.WindowText
                    : SystemColors.GrayText;
                _navigation.Nodes[index].ToolTipText = UiSurfacePolicy.IsNavigationEnabled(key)
                    ? string.Empty
                    : (_language == UiLanguage.English ? "This page is temporarily disabled." : "此页面暂时禁用。" );
            }
        }

        foreach (var page in _pages.Values)
        {
            if (page is ILocalizableControl localizablePage)
            {
                localizablePage.ApplyLanguage(_language);
            }
        }

        _inspector.ApplyLanguage(_language);
        _timeline.ApplyLanguage(_language);
    }

    private void ShowPage(UiTextKey key)
    {
        var page = _pages[key];
        if (ReferenceEquals(_activePage, page))
        {
            return;
        }

        if (_activePage is IPageLifecycle previousLifecycle)
        {
            previousLifecycle.DeactivatePage();
        }

        _pageHost.Controls.Clear();
        _pageHost.Controls.Add(page);
        page.Dock = DockStyle.Fill;
        _activePage = page;

        if (page is IPageLifecycle lifecycle)
        {
            lifecycle.ActivatePage();
        }
    }
}
