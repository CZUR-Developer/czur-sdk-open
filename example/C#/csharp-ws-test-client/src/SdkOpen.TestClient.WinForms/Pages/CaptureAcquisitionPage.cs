using System.ComponentModel;
using System.Text.Json;
using SdkOpen.TestClient.Core.Application;
using SdkOpen.TestClient.Core.Protocol;
using SdkOpen.TestClient.Core.Transport;
using SdkOpen.TestClient.WinForms.Controls;

namespace SdkOpen.TestClient.WinForms;

public sealed class CaptureAcquisitionPage : UserControl, ILocalizablePage, IPageLifecycle
{
    private sealed record TargetSizeChoice(int? Value, string Label);
    private sealed record WorkflowChoice(ImageEnhanceWorkflow? Workflow, string Label);
    private sealed class LocalizedComboBox : ComboBox
    {
        public void RefreshLocalizedItems() => RefreshItems();
    }

    private readonly AppComposition _composition;
    private readonly Action<string, string> _showProtocol;
    private readonly LocalizedComboBox _pageProcessing = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 140, FormattingEnabled = true };
    private readonly LocalizedComboBox _colorMode = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 140, FormattingEnabled = true };
    private readonly LocalizedComboBox _outputFormat = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 110, FormattingEnabled = true };
    private readonly NumericUpDown _quality = new() { Minimum = 1, Maximum = 100, Value = 90, Width = 70 };
    private readonly CheckBox _realtimeDetect = new() { Text = "实时检测矩形", Checked = true, AutoSize = true };
    private readonly CheckBox _autoRotate = new() { Text = "自动旋转", AutoSize = true };
    private readonly CheckBox _smartBlackEdge = new() { Text = "智能黑边优化", Checked = true, AutoSize = true };
    private readonly CheckBox _multiTarget = new() { Text = "多目标分页", AutoSize = true };
    private readonly CheckBox _originalThumbnail = new() { Checked = true, AutoSize = true };
    private readonly CheckBox _pageProcessedThumbnail = new() { Checked = true, AutoSize = true };
    private readonly CheckBox _colorProcessedThumbnail = new() { AutoSize = true };
    private readonly CheckBox _finalThumbnail = new() { Checked = true, AutoSize = true };
    private readonly NumericUpDown _x = new() { Minimum = 0, Maximum = 10000, Width = 70 };
    private readonly NumericUpDown _y = new() { Minimum = 0, Maximum = 10000, Width = 70 };
    private readonly NumericUpDown _width = new() { Minimum = 1, Maximum = 10000, Width = 70, Value = 1000 };
    private readonly NumericUpDown _height = new() { Minimum = 1, Maximum = 10000, Width = 70, Value = 1000 };
    private readonly CheckBox _singleCropBorder = new() { AutoSize = true };
    private readonly CheckBox _idCardRoundCorner = new() { AutoSize = true };
    private readonly CheckBox _curvedRemoveFinger = new() { Checked = true, AutoSize = true };
    private readonly CheckBox _curvedSmartPaging = new() { Checked = true, AutoSize = true };
    private readonly CheckBox _curvedCropBorder = new() { AutoSize = true };
    private readonly CheckBox _curvedAutoComplete = new() { AutoSize = true };
    private readonly CheckBox _turnDetect = new() { AutoSize = true };
    private readonly LocalizedComboBox _fingerType = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 130, FormattingEnabled = true };
    private readonly NumericUpDown _singleCropWidth = new() { Minimum = -100, Maximum = 100, Width = 60 };
    private readonly NumericUpDown _singleCropHeight = new() { Minimum = -100, Maximum = 100, Width = 60 };
    private readonly NumericUpDown _curvedCropWidth = new() { Minimum = -100, Maximum = 100, Width = 60 };
    private readonly NumericUpDown _curvedCropHeight = new() { Minimum = -100, Maximum = 100, Width = 60 };
    private readonly ComboBox _targetSize = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 150 };
    private readonly ComboBox _enhanceWorkflow = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 190 };
    private readonly Button _refreshWorkflows = new() { AutoSize = true };
    private readonly Label _singleCropWidthLabel = new() { AutoSize = true, Padding = new Padding(4, 6, 0, 0) };
    private readonly Label _singleCropHeightLabel = new() { AutoSize = true, Padding = new Padding(4, 6, 0, 0) };
    private readonly Label _curvedCropWidthLabel = new() { AutoSize = true, Padding = new Padding(4, 6, 0, 0) };
    private readonly Label _curvedCropHeightLabel = new() { AutoSize = true, Padding = new Padding(4, 6, 0, 0) };
    private readonly Label _targetSizeLabel = new() { AutoSize = true, Padding = new Padding(4, 6, 0, 0) };
    private readonly Label _enhanceWorkflowLabel = new() { AutoSize = true, Padding = new Padding(4, 6, 0, 0) };
    private readonly Button _capture = new() { Text = "采集一页", AutoSize = true };
    private readonly Button _query = new() { Text = "立即查询任务", AutoSize = true };
    private readonly Label _hint = new() { AutoSize = true, MaximumSize = new Size(680, 0) };
    private readonly BindingList<CaptureTaskSnapshot> _tasks = [];
    private readonly BindingList<CaptureTaskSnapshot> _previewTasks = [];
    private readonly DataGridView _taskGrid = CreateGrid();
    private readonly DataGridView _previewGrid = CreateGrid();
    private readonly PictureBox _previewImage = new() { Dock = DockStyle.Fill, SizeMode = PictureBoxSizeMode.Zoom, BorderStyle = BorderStyle.FixedSingle, BackColor = Color.White };
    private readonly Label _previewStatus = new() { AutoSize = true, MaximumSize = new Size(420, 0) };
    private readonly Label _previewTitle = new() { Dock = DockStyle.Top, Height = 24, Padding = new Padding(6, 4, 0, 0) };
    private readonly TextBox _previewAsset = new() { ReadOnly = true, Dock = DockStyle.Fill };
    private readonly VideoPreviewControl _livePreview = new() { Dock = DockStyle.Fill };
    private readonly Label _livePreviewTitle = new() { Dock = DockStyle.Top, Height = 26, Padding = new Padding(6, 5, 0, 0) };
    private readonly Label _livePreviewStatus = new() { Dock = DockStyle.Bottom, Height = 24, Padding = new Padding(6, 4, 0, 0) };
    private readonly System.Windows.Forms.Timer _livePreviewTimer = new() { Interval = 33 };
    private Label? _pageProcessingLabel;
    private Label? _colorLabel;
    private Label? _outputLabel;
    private Label? _qualityLabel;
    private Label? _selectionXLabel;
    private Label? _selectionYLabel;
    private Label? _widthLabel;
    private Label? _heightLabel;
    private CancellationTokenSource? _previewLoadCancellation;
    private bool _disposing;
    private bool _captureSessionChanging;
    private bool _synchronizingTargetSize;
    private bool _synchronizingWorkflows;
    private bool _loadingWorkflows;
    private string? _capturePreviewError;
    private readonly PageSessionActivation _pageActivation = new();
    private UiLanguage _language = UiLanguage.Chinese;

    public CaptureAcquisitionPage(AppComposition composition, Action<string, string> showProtocol)
    {
        _composition = composition;
        _showProtocol = showProtocol;
        // These are fixed form options, so populate them synchronously instead of using
        // CurrencyManager-backed DataSource binding. This guarantees SelectedItem is
        // available when the initial mode-dependent enabled state is calculated.
        // 这些是固定表单选项，直接同步填充，避免 DataSource 延迟绑定导致首次模式联动拿到空选项。
        _pageProcessing.Items.AddRange(Enum.GetValues<CapturePageProcessing>().Cast<object>().ToArray());
        _colorMode.Items.AddRange(Enum.GetValues<CaptureColorMode>().Cast<object>().ToArray());
        _outputFormat.Items.AddRange(Enum.GetValues<CaptureOutputFormat>().Cast<object>().ToArray());
        _fingerType.Items.AddRange(["with_sleeve", "without_sleeve"]);
        _pageProcessing.Format += (_, args) => args.Value = LocalizePageProcessing(args.ListItem is CapturePageProcessing item ? item : CapturePageProcessing.SinglePage);
        _colorMode.Format += (_, args) => args.Value = LocalizeColorMode(args.ListItem is CaptureColorMode item ? item : CaptureColorMode.Color);
        _outputFormat.Format += (_, args) => args.Value = LocalizeOutputFormat(args.ListItem is CaptureOutputFormat item ? item : CaptureOutputFormat.Jpg);
        _fingerType.Format += (_, args) => args.Value = _language == UiLanguage.English
            ? (string.Equals(args.ListItem?.ToString(), "without_sleeve", StringComparison.Ordinal) ? "Without sleeve" : "With sleeve")
            : (string.Equals(args.ListItem?.ToString(), "without_sleeve", StringComparison.Ordinal) ? "不带指套" : "带指套");
        _pageProcessing.SelectedItem = CapturePageProcessing.SinglePage;
        _colorMode.SelectedItem = CaptureColorMode.Color;
        _outputFormat.SelectedItem = CaptureOutputFormat.Jpg;
        _fingerType.SelectedIndex = 0;
        _pageProcessing.SelectedIndexChanged += (_, _) => { UpdateSelectionControls(); _ = ApplyVideoProfileAsync(); };
        _colorMode.SelectedIndexChanged += (_, _) => _ = ApplyVideoProfileAsync();
        _outputFormat.SelectedIndexChanged += (_, _) => { UpdateOutputControls(); _ = ApplyVideoProfileAsync(); };
        _realtimeDetect.CheckedChanged += (_, _) => { UpdateLivePreviewOverlay(); _ = ApplyVideoProfileAsync(); };
        _singleCropBorder.CheckedChanged += (_, _) => { UpdateSelectionControls(); _ = ApplyVideoProfileAsync(); };
        _curvedRemoveFinger.CheckedChanged += (_, _) => { UpdateSelectionControls(); _ = ApplyVideoProfileAsync(); };
        _curvedCropBorder.CheckedChanged += (_, _) => { UpdateSelectionControls(); _ = ApplyVideoProfileAsync(); };
        foreach (var control in new[] { _x, _y, _width, _height, _singleCropWidth, _singleCropHeight, _curvedCropWidth, _curvedCropHeight, _quality }) control.ValueChanged += (_, _) => _ = ApplyVideoProfileAsync();
        _targetSize.SelectedIndexChanged += (_, _) =>
        {
            if (!_synchronizingTargetSize) _ = ApplyVideoProfileAsync();
        };
        foreach (var control in new[] { _autoRotate, _smartBlackEdge, _multiTarget, _originalThumbnail, _pageProcessedThumbnail, _colorProcessedThumbnail, _finalThumbnail, _idCardRoundCorner, _curvedSmartPaging, _curvedAutoComplete }) control.CheckedChanged += (_, _) => _ = ApplyVideoProfileAsync();
        _turnDetect.CheckedChanged += async (_, _) => await ApplyTurnDetectAsync();
        _enhanceWorkflow.SelectedIndexChanged += (_, _) =>
        {
            if (!_synchronizingWorkflows) _ = ApplyVideoProfileAsync();
        };
        _refreshWorkflows.Click += async (_, _) => await LoadEnhanceWorkflowsAsync(forceRefresh: true);
        _fingerType.SelectedIndexChanged += (_, _) => _ = ApplyVideoProfileAsync();
        _capture.Click += async (_, _) => await CaptureAsync();
        _query.Click += async (_, _) => await QuerySelectedTaskAsync();
        _taskGrid.CellDoubleClick += (_, args) => ShowTaskDetails(args.RowIndex);
        _previewGrid.SelectionChanged += async (_, _) => await LoadSelectedPreviewAsync();
        _previewGrid.CellDoubleClick += (_, args) => ShowPreviewTaskDetails(args.RowIndex);
        _livePreviewTimer.Tick += (_, _) => RenderLivePreview();
        _composition.CaptureVideoPreview.ConnectionClosed += OnVideoConnectionClosed;
        ConfigureTaskGrid();
        ConfigurePreviewGrid();
        _composition.Coordinator.DeviceWorkflow.Changed += OnWorkflowChanged;
        _composition.Coordinator.CaptureTasks.Changed += OnTaskChanged;
        BuildLayout();
        ConfigureWorkflowChoices([]);
        _livePreview.ApplyLanguage(_language);
        UpdateLivePreviewOverlay();
        UpdateOutputControls();
        SynchronizeWorkflow(_composition.Coordinator.DeviceWorkflow.Snapshot);
        RefreshTasks();
    }

    private static DataGridView CreateGrid() => new()
    {
        Dock = DockStyle.Fill,
        AutoGenerateColumns = false,
        ReadOnly = true,
        AllowUserToAddRows = false,
        AllowUserToDeleteRows = false,
        SelectionMode = DataGridViewSelectionMode.FullRowSelect,
        MultiSelect = false,
        RowHeadersVisible = false
    };

    private void ConfigureTaskGrid()
    {
        _taskGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "时间", DataPropertyName = nameof(CaptureTaskSnapshot.LocalUpdatedAtDisplay), Width = 150 });
        _taskGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "任务 ID", DataPropertyName = nameof(CaptureTaskSnapshot.TaskId), Width = 175 });
        _taskGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "状态", DataPropertyName = nameof(CaptureTaskSnapshot.Status), Width = 90 });
        _taskGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "来源", DataPropertyName = nameof(CaptureTaskSnapshot.LastUpdateSource), Width = 80 });
        _taskGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "查询", DataPropertyName = nameof(CaptureTaskSnapshot.PollAttempts), Width = 55 });
        _taskGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "格式", DataPropertyName = nameof(CaptureTaskSnapshot.OutputFormat), Width = 60 });
        _taskGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Asset 类型", DataPropertyName = nameof(CaptureTaskSnapshot.AssetKind), Width = 145 });
        _taskGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "处理阶段", DataPropertyName = nameof(CaptureTaskSnapshot.StagesDisplay), Width = 210 });
        _taskGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "警告", DataPropertyName = nameof(CaptureTaskSnapshot.WarningsDisplay), Width = 220 });
        _taskGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Asset", DataPropertyName = nameof(CaptureTaskSnapshot.AssetUrl), AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill });
        _taskGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "错误", DataPropertyName = nameof(CaptureTaskSnapshot.Error), Width = 180 });
        _taskGrid.DataSource = _tasks;
    }

    private void ConfigurePreviewGrid()
    {
        _previewGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "拍照时间", DataPropertyName = nameof(CaptureTaskSnapshot.LocalUpdatedAtDisplay), Width = 150 });
        _previewGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "格式", DataPropertyName = nameof(CaptureTaskSnapshot.OutputFormat), Width = 60 });
        _previewGrid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "任务 ID", DataPropertyName = nameof(CaptureTaskSnapshot.TaskId), AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill });
        _previewGrid.DataSource = _previewTasks;
    }

    private void BuildLayout()
    {
        var form = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(10), WrapContents = true };
        form.Controls.AddRange([
            _pageProcessingLabel = LabelFor("页面处理"), _pageProcessing, _colorLabel = LabelFor("颜色"), _colorMode, _outputLabel = LabelFor("输出"), _outputFormat, _qualityLabel = LabelFor("JPG 质量"), _quality,
            _realtimeDetect, _autoRotate, _smartBlackEdge, _multiTarget]);
        _singleCropBorder.Text = "单页裁边";
        _idCardRoundCorner.Text = "证件圆角留白";
        _curvedRemoveFinger.Text = "去除手指";
        _curvedSmartPaging.Text = "曲面智能分页";
        _curvedCropBorder.Text = "曲面裁边";
        _curvedAutoComplete.Text = "曲面自动补全";
        _turnDetect.Text = "翻页检测";
        var extended = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(10), WrapContents = true };
        _singleCropWidthLabel.Text = "宽度裁边：";
        _singleCropHeightLabel.Text = "高度裁边：";
        _curvedCropWidthLabel.Text = "宽度裁边：";
        _curvedCropHeightLabel.Text = "高度裁边：";
        extended.Controls.AddRange([_singleCropBorder, _singleCropWidthLabel, _singleCropWidth, _singleCropHeightLabel, _singleCropHeight, _idCardRoundCorner, _curvedRemoveFinger, _fingerType, _curvedSmartPaging, _curvedCropBorder, _curvedCropWidthLabel, _curvedCropWidth, _curvedCropHeightLabel, _curvedCropHeight, _curvedAutoComplete, _turnDetect, _targetSizeLabel, _targetSize]);
        var outputOptions = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(10), WrapContents = true };
        outputOptions.Controls.AddRange([_originalThumbnail, _pageProcessedThumbnail, _colorProcessedThumbnail, _finalThumbnail, _enhanceWorkflowLabel, _enhanceWorkflow, _refreshWorkflows]);
        var region = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(10) };
        region.Controls.AddRange([_selectionXLabel = LabelFor("选区 X"), _x, _selectionYLabel = LabelFor("Y"), _y, _widthLabel = LabelFor("宽"), _width, _heightLabel = LabelFor("高"), _height]);
        var action = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(10) };
        action.Controls.AddRange([_capture, _query, _hint]);

        var previewSplit = new SplitContainer
        {
            Dock = DockStyle.Fill,
            Orientation = Orientation.Vertical
        };
        previewSplit.Panel1.Controls.Add(_previewGrid);
        _previewTitle.Text = "成功结果预览";
        previewSplit.Panel1.Controls.Add(_previewTitle);
        previewSplit.Panel2.Controls.Add(BuildPreviewPane());

        var taskAndPreview = new SplitContainer
        {
            Dock = DockStyle.Fill,
            Orientation = Orientation.Horizontal
        };
        taskAndPreview.Panel1.Controls.Add(_taskGrid);
        taskAndPreview.Panel2.Controls.Add(previewSplit);
        SplitContainerSizing.ApplyAfterFirstValidLayout(previewSplit, 420, 230, 260);
        SplitContainerSizing.ApplyAfterFirstValidLayout(taskAndPreview, 210, 120, 170);

        var livePanel = new Panel { Dock = DockStyle.Top, Height = 240, Padding = new Padding(6) };
        _livePreviewTitle.Text = "实时预览";
        _livePreviewStatus.Text = "请先在设备与视频页启动预览。";
        livePanel.Controls.Add(_livePreview);
        livePanel.Controls.Add(_livePreviewStatus);
        livePanel.Controls.Add(_livePreviewTitle);
        Controls.Add(taskAndPreview);
        Controls.Add(livePanel);
        Controls.Add(action);
        Controls.Add(region);
        Controls.Add(outputOptions);
        Controls.Add(extended);
        Controls.Add(form);
        UpdateSelectionControls();
    }

    public void ApplyLanguage(UiLanguage language)
    {
        _language = language;
        var english = language == UiLanguage.English;
        _pageProcessingLabel!.Text = english ? "Page processing:" : "页面处理方式：";
        _colorLabel!.Text = english ? "Color mode:" : "色彩模式：";
        _outputLabel!.Text = english ? "Output format:" : "输出格式：";
        _qualityLabel!.Text = english ? "JPEG quality:" : "JPEG 质量：";
        _selectionXLabel!.Text = english ? "Selection X:" : "选区 X：";
        _selectionYLabel!.Text = "Y：";
        _widthLabel!.Text = english ? "Width:" : "宽：";
        _heightLabel!.Text = english ? "Height:" : "高：";
        _capture.Text = english ? "Capture" : "拍照采集";
        _query.Text = english ? "Query task" : "查询任务";
        _realtimeDetect.Text = english ? "Realtime detection box" : "实时识别框";
        _autoRotate.Text = english ? "Auto rotate page" : "页面自动转正";
        _smartBlackEdge.Text = english ? "Smart black edge optimize" : "智能优化黑边";
        _multiTarget.Text = english ? "Multi-target auto paging" : "多目标自动分页";
        _originalThumbnail.Text = english ? "Original thumbnail" : "原始图缩略图";
        _pageProcessedThumbnail.Text = english ? "Page processing thumbnail" : "页面处理结果缩略图";
        _colorProcessedThumbnail.Text = english ? "Color mode thumbnail" : "色彩模式结果缩略图";
        _finalThumbnail.Text = english ? "Final output thumbnail" : "最终输出缩略图";
        _enhanceWorkflowLabel.Text = english ? "Image enhancement workflow:" : "图像增强工作流：";
        _refreshWorkflows.Text = english ? "Refresh workflows" : "刷新工作流";
        _previewTitle.Text = english ? "Capture results" : "采集结果";
        _singleCropBorder.Text = english ? "Crop border" : "裁边参数";
        _idCardRoundCorner.Text = english ? "ID card rounded corner padding" : "证件圆角留白";
        _curvedRemoveFinger.Text = english ? "Remove finger" : "清除手指";
        _curvedSmartPaging.Text = english ? "Smart paging" : "智能分页";
        _curvedCropBorder.Text = english ? "Crop border" : "裁边参数";
        _curvedAutoComplete.Text = english ? "Auto complete page" : "页面自动补全";
        _turnDetect.Text = english ? "Enable page turn detection" : "开启翻页检测";
        _singleCropWidthLabel.Text = english ? "Width margin:" : "宽度裁边：";
        _singleCropHeightLabel.Text = english ? "Height margin:" : "高度裁边：";
        _curvedCropWidthLabel.Text = english ? "Width margin:" : "宽度裁边：";
        _curvedCropHeightLabel.Text = english ? "Height margin:" : "高度裁边：";
        _targetSizeLabel.Text = english ? "Target size:" : "目标大小：";
        _livePreviewTitle.Text = english ? "Capture preview" : "拍照预览";
        _livePreviewStatus.Text = english ? "Capture preview is starting or not connected." : "采集预览正在启动或尚未连接。";
        _livePreview.ApplyLanguage(language);
        // WinForms caches the formatted ComboBox item text. RefreshItems reruns the
        // Format callbacks so both the selected text and drop-down rows change language.
        // WinForms 会缓存下拉项的格式化文本；RefreshItems 可让选中项和下拉列表同步切换语言。
        _pageProcessing.RefreshLocalizedItems();
        _colorMode.RefreshLocalizedItems();
        _outputFormat.RefreshLocalizedItems();
        _fingerType.RefreshLocalizedItems();
        ConfigureTargetSizeOptions(_composition.Coordinator.DeviceWorkflow.Snapshot.CaptureOutput);
        RelocalizeWorkflowChoices();
        UpdateLivePreviewOverlay();
        if (_tasks.Count == 0)
        {
            _hint.Text = english ? "No capture task yet." : "当前还没有采集任务。";
        }
        else if (_taskGrid.CurrentRow?.DataBoundItem is CaptureTaskSnapshot currentTask)
        {
            UpdateTaskHint(currentTask);
        }
        UpdateGridHeaders(english);
    }

    private void UpdateGridHeaders(bool english)
    {
        if (_taskGrid.Columns.Count >= 11)
        {
            var headers = english
                ? new[] { "Time", "Task ID", "Status", "Source", "Polls", "Format", "Asset Kind", "Stages", "Warnings", "Asset", "Error" }
                : new[] { "时间", "任务 ID", "状态", "来源", "查询", "格式", "Asset 类型", "处理阶段", "警告", "Asset", "错误" };
            for (var index = 0; index < headers.Length; index++) _taskGrid.Columns[index].HeaderText = headers[index];
        }
        if (_previewGrid.Columns.Count >= 3)
        {
            var headers = english ? new[] { "Capture Time", "Format", "Task ID" } : new[] { "拍照时间", "格式", "任务 ID" };
            for (var index = 0; index < headers.Length; index++) _previewGrid.Columns[index].HeaderText = headers[index];
        }
    }

    private Control BuildPreviewPane()
    {
        var metadata = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 1, Padding = new Padding(6) };
        metadata.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        metadata.Controls.Add(_previewStatus, 0, 0);
        metadata.Controls.Add(_previewAsset, 0, 1);

        var pane = new Panel { Dock = DockStyle.Fill, Padding = new Padding(6) };
        pane.Controls.Add(_previewImage);
        pane.Controls.Add(metadata);
        return pane;
    }

    private static Label LabelFor(string text) => new() { Text = text + "：", AutoSize = true, Padding = new Padding(4, 6, 0, 0) };

    private string LocalizePageProcessing(CapturePageProcessing value) => _language == UiLanguage.English
        ? value switch
        {
            CapturePageProcessing.SinglePage => "Single page",
            CapturePageProcessing.CurvedBook => "Curved book",
            CapturePageProcessing.Selection => "Selected area",
            CapturePageProcessing.KeepOriginal => "Keep original",
            _ => value.ToString()
        }
        : value switch
        {
            CapturePageProcessing.SinglePage => "单页",
            CapturePageProcessing.CurvedBook => "曲面",
            CapturePageProcessing.Selection => "区域",
            CapturePageProcessing.KeepOriginal => "保留原图",
            _ => value.ToString()
        };

    private string LocalizeColorMode(CaptureColorMode value) => _language == UiLanguage.English
        ? value switch
        {
            CaptureColorMode.AutoOptimize => "Auto optimize",
            CaptureColorMode.Color => "Color",
            CaptureColorMode.BlackWhite => "Black / white",
            CaptureColorMode.Grayscale => "Grayscale",
            CaptureColorMode.WhitePaperSeal => "White paper seal",
            CaptureColorMode.Certificate => "Certificate texture",
            CaptureColorMode.Ancient => "Ancient book",
            CaptureColorMode.NoOptimize => "No optimize",
            _ => value.ToString()
        }
        : value switch
        {
            CaptureColorMode.AutoOptimize => "自动优化",
            CaptureColorMode.Color => "彩色",
            CaptureColorMode.BlackWhite => "黑白",
            CaptureColorMode.Grayscale => "灰色",
            CaptureColorMode.WhitePaperSeal => "白纸印章",
            CaptureColorMode.Certificate => "证件底纹",
            CaptureColorMode.Ancient => "古籍模式",
            CaptureColorMode.NoOptimize => "无优化",
            _ => value.ToString()
        };

    private string LocalizeOutputFormat(CaptureOutputFormat value) => value switch
    {
        CaptureOutputFormat.Jpg => "JPG",
        CaptureOutputFormat.Png => "PNG",
        CaptureOutputFormat.Tiff => "TIFF",
        _ => value.ToString().ToUpperInvariant()
    };

    private void UpdateSelectionControls()
    {
        var enabled = _pageProcessing.SelectedItem is CapturePageProcessing.Selection;
        _x.Enabled = enabled;
        _y.Enabled = enabled;
        _width.Enabled = enabled;
        _height.Enabled = enabled;
        var single = _pageProcessing.SelectedItem is CapturePageProcessing.SinglePage;
        var curved = _pageProcessing.SelectedItem is CapturePageProcessing.CurvedBook;
        _realtimeDetect.Enabled = single;
        _autoRotate.Enabled = single;
        _smartBlackEdge.Enabled = single;
        _multiTarget.Enabled = single;
        _singleCropBorder.Enabled = single;
        _singleCropWidth.Enabled = single && _singleCropBorder.Checked;
        _singleCropHeight.Enabled = single && _singleCropBorder.Checked;
        _idCardRoundCorner.Enabled = single;
        _curvedRemoveFinger.Enabled = curved;
        _fingerType.Enabled = curved && _curvedRemoveFinger.Checked;
        _curvedSmartPaging.Enabled = curved;
        _curvedCropBorder.Enabled = curved;
        _curvedCropWidth.Enabled = curved && _curvedCropBorder.Checked;
        _curvedCropHeight.Enabled = curved && _curvedCropBorder.Checked;
        _curvedAutoComplete.Enabled = curved;
        _turnDetect.Enabled = true;
        UpdateLivePreviewOverlay();
    }

    private void UpdateOutputControls()
    {
        _quality.Enabled = _outputFormat.SelectedItem is CaptureOutputFormat.Jpg;
    }

    private void UpdateLivePreviewOverlay()
    {
        var enabled = _pageProcessing.SelectedItem is CapturePageProcessing.SinglePage && _realtimeDetect.Checked;
        var message = _language == UiLanguage.English
            ? "Realtime detection box"
            : "实时识别框";
        _livePreview.ConfigureSinglePageCropOverlay(enabled, message);
    }

    private async Task ApplyVideoProfileAsync()
    {
        var snapshot = _composition.Coordinator.DeviceWorkflow.Snapshot;
        if (!snapshot.CanCapture || _composition.CaptureVideoPreview.StreamId is null || snapshot.SelectedResolution is null || string.IsNullOrWhiteSpace(snapshot.SelectedDeviceId)) return;
        try
        {
            var profile = CaptureProfileBuilder.Build(BuildCaptureOptions(), snapshot.SelectedResolution, snapshot.SelectedDeviceId);
            await _composition.Coordinator.SetVideoProfileAsync(profile, BuildEnhancePipeline(), CancellationToken.None);
        }
        catch (Exception exception)
        {
            BeginSafe(() => _livePreviewStatus.Text = _language == UiLanguage.English
                ? $"Unable to apply live profile: {exception.Message}"
                : $"无法应用实时预览参数：{exception.Message}");
        }
    }

    private async Task ApplyTurnDetectAsync()
    {
        if (_disposing) return;
        var snapshot = _composition.Coordinator.DeviceWorkflow.Snapshot;
        if (!snapshot.CanCapture || _composition.CaptureVideoPreview.StreamId is null) return;
        try
        {
            var response = await _composition.Coordinator.SetTurnDetectAsync(_turnDetect.Checked, CancellationToken.None);
            _showProtocol(
                JsonSerializer.Serialize(new { method = "capture.set_turn_detect", @params = new { device_id = snapshot.SelectedDeviceId, enabled = _turnDetect.Checked, auto_capture = false, scan_device_type = 0, cooldown_ms = 1000 } }),
                JsonSerializer.Serialize(response));
        }
        catch (Exception exception)
        {
            BeginSafe(() => _livePreviewStatus.Text = _language == UiLanguage.English
                ? $"Unable to change page-turn detection: {exception.Message}"
                : $"无法切换翻页检测：{exception.Message}");
        }
    }

    private async Task CaptureAsync()
    {
        try
        {
            // 页面只负责收集表单值；JSON profile 的拼装和 capture.take 调用由 Core 协调器完成。
            // The page collects form values; Core builds the JSON profile and invokes capture.take.
            var options = BuildCaptureOptions();
            var submission = await _composition.Coordinator.CaptureSelectedPageAsync(options, BuildEnhancePipeline(), CancellationToken.None);
            var task = submission.Task;
            _showProtocol(
                JsonSerializer.Serialize(new { method = "capture.take", @params = submission.RequestParameters }),
                JsonSerializer.Serialize(submission.Response));
            _hint.Text = _language == UiLanguage.English
                ? $"Capture task submitted: {task.TaskId}. Waiting for events or query results."
                : $"采集任务已提交：{task.TaskId}。正在等待事件或查询结果。";
            RefreshTasks();
        }
        catch (Exception exception)
        {
            _hint.Text = _language == UiLanguage.English ? $"Unable to capture: {exception.Message}" : $"无法采集：{exception.Message}";
        }
    }

    private CaptureFormOptions BuildCaptureOptions() => new(
        (CapturePageProcessing)_pageProcessing.SelectedItem!,
        (CaptureColorMode)_colorMode.SelectedItem!,
        (CaptureOutputFormat)_outputFormat.SelectedItem!,
        decimal.ToInt32(_quality.Value),
         _realtimeDetect.Checked, _idCardRoundCorner.Checked, _autoRotate.Checked, _smartBlackEdge.Checked, _multiTarget.Checked,
        new CaptureThumbnailOptions(_originalThumbnail.Checked, _pageProcessedThumbnail.Checked, _colorProcessedThumbnail.Checked, _finalThumbnail.Checked),
        _pageProcessing.SelectedItem is CapturePageProcessing.Selection ? new CaptureSelection(decimal.ToInt32(_x.Value), decimal.ToInt32(_y.Value), decimal.ToInt32(_width.Value), decimal.ToInt32(_height.Value)) : null,
        SinglePageCropBorderEnabled: _singleCropBorder.Checked,
        SinglePageCropBorderWidth: decimal.ToInt32(_singleCropWidth.Value),
        SinglePageCropBorderHeight: decimal.ToInt32(_singleCropHeight.Value),
        CurvedBookRemoveFinger: _curvedRemoveFinger.Checked,
        CurvedBookFingerType: _fingerType.SelectedItem?.ToString() ?? "with_sleeve",
        CurvedBookSmartPaging: _curvedSmartPaging.Checked,
        CurvedBookCropBorderEnabled: _curvedCropBorder.Checked,
        CurvedBookCropBorderWidth: decimal.ToInt32(_curvedCropWidth.Value),
        CurvedBookCropBorderHeight: decimal.ToInt32(_curvedCropHeight.Value),
        CurvedBookAutoComplete: _curvedAutoComplete.Checked,
        OutputTargetSize: _targetSize.SelectedItem is TargetSizeChoice choice ? choice.Value : null);

    private JsonElement BuildEnhancePipeline()
    {
        var workflow = (_enhanceWorkflow.SelectedItem as WorkflowChoice)?.Workflow;
        var format = _outputFormat.SelectedItem is CaptureOutputFormat selected ? selected : CaptureOutputFormat.Jpg;
        return CaptureEnhancePipelineBuilder.Build(workflow?.Pipeline, format);
    }

    private async Task LoadEnhanceWorkflowsAsync(bool forceRefresh)
    {
        if (_disposing || _loadingWorkflows || (!forceRefresh && _enhanceWorkflow.Items.Count > 1)) return;
        _loadingWorkflows = true;
        _refreshWorkflows.Enabled = false;
        try
        {
            var workflows = await _composition.Coordinator.ListImageEnhanceWorkflowsAsync(CancellationToken.None);
            ConfigureWorkflowChoices(workflows);
        }
        catch (Exception exception)
        {
            _hint.Text = _language == UiLanguage.English
                ? $"Unable to load enhance workflows: {exception.Message}"
                : $"无法加载增强工作流：{exception.Message}";
        }
        finally
        {
            _loadingWorkflows = false;
            _refreshWorkflows.Enabled = true;
        }
    }

    private void ConfigureWorkflowChoices(IReadOnlyList<ImageEnhanceWorkflow> workflows)
    {
        var selectedId = (_enhanceWorkflow.SelectedItem as WorkflowChoice)?.Workflow?.WorkflowId;
        var choices = new List<WorkflowChoice>
        {
            new(null, _language == UiLanguage.English ? "No workflow selected" : "未选择工作流")
        };
        choices.AddRange(workflows.Select(workflow => new WorkflowChoice(workflow, workflow.DisplayName)));
        var selected = choices.FirstOrDefault(choice => string.Equals(choice.Workflow?.WorkflowId, selectedId, StringComparison.Ordinal)) ?? choices[0];
        _synchronizingWorkflows = true;
        try
        {
            _enhanceWorkflow.DataSource = choices;
            _enhanceWorkflow.DisplayMember = nameof(WorkflowChoice.Label);
            _enhanceWorkflow.SelectedItem = selected;
        }
        finally
        {
            _synchronizingWorkflows = false;
        }
    }

    private void RelocalizeWorkflowChoices()
    {
        var workflows = _enhanceWorkflow.Items.Cast<WorkflowChoice>()
            .Where(choice => choice.Workflow is not null)
            .Select(choice => choice.Workflow!)
            .ToArray();
        ConfigureWorkflowChoices(workflows);
    }

    private async Task QuerySelectedTaskAsync()
    {
        // 查询选中任务是手动兜底入口，适用于设备事件延迟或用户希望立即确认结果的场景。
        // Manual task query is a fallback for delayed events or immediate result verification.
        var task = _taskGrid.CurrentRow?.DataBoundItem as CaptureTaskSnapshot ?? _composition.Coordinator.CaptureTasks.Tasks.FirstOrDefault();
        if (task is null)
        {
            _hint.Text = _language == UiLanguage.English ? "There is no capture task to query." : "当前没有可查询的采集任务。";
            return;
        }
        try
        {
            var query = await _composition.Coordinator.RefreshCaptureTaskWithProtocolAsync(task.TaskId, CancellationToken.None);
            var refreshed = query.Task;
            _showProtocol(
                JsonSerializer.Serialize(new { method = "capture.get", @params = query.RequestParameters }),
                JsonSerializer.Serialize(query.Response));
            RefreshTasks();
            UpdateTaskHint(refreshed);
        }
        catch (Exception exception)
        {
            _hint.Text = _language == UiLanguage.English ? $"Query failed for task {task.TaskId}: {exception.Message}" : $"查询任务 {task.TaskId} 失败：{exception.Message}";
        }
    }

    private void ShowTaskDetails(int rowIndex)
    {
        if (rowIndex < 0 || rowIndex >= _taskGrid.Rows.Count || _taskGrid.Rows[rowIndex].DataBoundItem is not CaptureTaskSnapshot task) return;
        ShowTaskDetails(task);
    }

    private void ShowPreviewTaskDetails(int rowIndex)
    {
        if (rowIndex < 0 || rowIndex >= _previewGrid.Rows.Count || _previewGrid.Rows[rowIndex].DataBoundItem is not CaptureTaskSnapshot task) return;
        ShowTaskDetails(task);
    }

    private void ShowTaskDetails(CaptureTaskSnapshot task) => _showProtocol(
        JsonSerializer.Serialize(new { source = "capture.task", task_id = task.TaskId }),
        JsonSerializer.Serialize(task));

    private void OnWorkflowChanged(object? sender, DeviceWorkflowSnapshot snapshot) => BeginSafe(() =>
    {
        SynchronizeWorkflow(snapshot);
    });

    public void RefreshFromWorkflow()
    {
        SynchronizeWorkflow(_composition.Coordinator.DeviceWorkflow.Snapshot);
        RefreshTasks();
    }

    private async Task EnsureCaptureSessionAsync()
    {
        if (_disposing || _captureSessionChanging) return;
        var selectedDeviceId = _composition.Coordinator.DeviceWorkflow.Snapshot.SelectedDeviceId;
        if (string.IsNullOrWhiteSpace(selectedDeviceId))
        {
            _capturePreviewError = _language == UiLanguage.English
                ? "Select a device before opening capture preview."
                : "请先选择设备，再打开采集预览。";
            SynchronizeWorkflow(_composition.Coordinator.DeviceWorkflow.Snapshot);
            return;
        }
        if (!_pageActivation.TryRequestStart(_composition.CaptureVideoPreview.StreamId is not null)) return;

        _captureSessionChanging = true;
        _capturePreviewError = null;
        try
        {
            // A physical camera cannot be opened twice. Release the general preview
            // before taking ownership for the capture-specific stream.
            // Read every WinForms control before crossing the asynchronous thread boundary.
            // 在跨越异步线程边界前读取全部 WinForms 控件，后台协调器仅接收纯数据快照。
            var captureOptions = BuildCaptureOptions();
            var pipeline = BuildEnhancePipeline();
            await _composition.DevicePreviewSessions.StartCaptureAsync(
                captureOptions,
                pipeline,
                CancellationToken.None).ConfigureAwait(true);

            // Page-turn detection is a device command, not part of capture.profile.
            // 翻页检测是独立设备命令，不属于 capture.profile；视频会话重建后必须重放当前状态。
            await ApplyTurnDetectAsync().ConfigureAwait(true);
            // The user may have edited controls while the device transition was in progress.
            // 会话切换期间用户仍可能修改表单；启动完成后再同步一次当前 profile/pipeline，避免丢失最后一次修改。
            await ApplyVideoProfileAsync().ConfigureAwait(true);

            _capturePreviewError = null;
            BeginSafe(() =>
            {
                _livePreviewTimer.Start();
                SynchronizeWorkflow(_composition.Coordinator.DeviceWorkflow.Snapshot);
            });
        }
        catch (Exception exception)
        {
            _capturePreviewError = _language == UiLanguage.English
                ? $"Unable to start capture preview: {exception.Message}"
                : $"无法启动采集预览：{exception.Message}";
            BeginSafe(() => _livePreviewStatus.Text = _language == UiLanguage.English
                ? $"Unable to start capture preview: {exception.Message}"
                : $"无法启动采集预览：{exception.Message}");
        }
        finally
        {
            _captureSessionChanging = false;
            // If the page was hidden while the asynchronous hand-off was in progress,
            // finish releasing the capture-owned stream after the hand-off completes.
            if (!_disposing && !_pageActivation.IsActive && _composition.CaptureVideoPreview.StreamId is not null)
            {
                _ = StopCaptureSessionAsync();
            }
        }
    }

    private async Task StopCaptureSessionAsync()
    {
        if (_disposing || _captureSessionChanging) return;
        if (_composition.CaptureVideoPreview.StreamId is null && !_composition.Coordinator.DeviceWorkflow.Snapshot.IsOpened) return;
        _captureSessionChanging = true;
        try
        {
            await _composition.DevicePreviewSessions.StopCaptureAsync(CancellationToken.None).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            BeginSafe(() => _livePreviewStatus.Text = _language == UiLanguage.English
                ? $"Unable to close capture preview: {exception.Message}"
                : $"无法关闭采集预览：{exception.Message}");
        }
        finally
        {
            _captureSessionChanging = false;
        }
    }

    private void SynchronizeWorkflow(DeviceWorkflowSnapshot snapshot)
    {
        ConfigureTargetSizeOptions(snapshot.CaptureOutput);
        _capture.Enabled = snapshot.CanCapture && _composition.CaptureVideoPreview.StreamId is not null;
        var captureStreamId = _composition.CaptureVideoPreview.StreamId;
        if (_capturePreviewError is not null)
        {
            _livePreviewTimer.Stop();
            _livePreviewStatus.Text = _capturePreviewError;
        }
        else if (captureStreamId is not null)
        {
            _livePreviewTimer.Start();
            _livePreviewStatus.Text = _language == UiLanguage.English ? $"Capture stream: {captureStreamId}" : $"采集流：{captureStreamId}";
        }
        else
        {
            _livePreviewTimer.Stop();
            _livePreviewStatus.Text = _language == UiLanguage.English ? "Capture preview is starting or not connected." : "采集预览正在启动或尚未连接。";
        }
        _hint.Text = _language == UiLanguage.English
            ? snapshot.CanCapture
                ? $"Capture owns device {snapshot.SelectedDeviceId} with an independent preview stream; all parameters come from the form."
                : "Select a device and wait for the capture preview to connect; this page will not send an invalid capture.take."
            : snapshot.CanCapture
                ? $"将使用已打开设备 {snapshot.SelectedDeviceId} 采集；全部参数由表单生成。"
                : "请先到“设备与视频”页选择设备、读取详情、打开设备并启动预览；此页不会发送无效的 capture.take。";
    }

    private void ConfigureTargetSizeOptions(CaptureOutputCapabilities capabilities)
    {
        var current = (_targetSize.SelectedItem as TargetSizeChoice)?.Value;
        var choices = new List<TargetSizeChoice>
        {
            new(null, _language == UiLanguage.English ? "Not set" : "不设置")
        };
        if (capabilities.TargetSizeSupported)
        {
            choices.AddRange(capabilities.TargetSizes.Select(item =>
                new TargetSizeChoice(item.TargetSize,
                    _language == UiLanguage.English
                        ? $"Bucket {item.TargetSize} / {item.Width} x {item.Height}" + (item.IsDeviceDefault ? " (default)" : string.Empty)
                        : $"{item.TargetSize} 万 / {item.Width} x {item.Height}" + (item.IsDeviceDefault ? "（默认）" : string.Empty))));
        }
        var selected = choices.FirstOrDefault(item => item.Value == current) ?? choices[0];
        _synchronizingTargetSize = true;
        try
        {
            _targetSize.DataSource = choices;
            _targetSize.DisplayMember = nameof(TargetSizeChoice.Label);
            _targetSize.SelectedItem = selected;
        }
        finally
        {
            _synchronizingTargetSize = false;
        }
    }

    private void RenderLivePreview()
    {
        var frame = _composition.CaptureVideoPreview.TakeLatestFrame();
        if (frame is not null) _livePreview.ShowFrame(frame, _composition.CaptureVideoPreview.Statistics);
    }

    private void OnVideoConnectionClosed(object? sender, ConnectionClosedEventArgs args) => BeginSafe(() =>
        _livePreviewStatus.Text = _language == UiLanguage.English ? $"Video connection closed: {args.Reason}" : $"视频连接已关闭：{args.Reason}");

    private void OnTaskChanged(object? sender, CaptureTaskSnapshot task) => BeginSafe(() =>
    {
        RefreshTasks();
        UpdateTaskHint(task);
    });

    private void RefreshTasks()
    {
        var selectedTaskId = _previewGrid.CurrentRow?.DataBoundItem is CaptureTaskSnapshot selected ? selected.TaskId : null;
        ReplaceBindingItems(_tasks, _composition.Coordinator.CaptureTasks.Tasks);
        // 预览列表只展示“成功且存在 Asset”的任务，避免把失败/处理中任务误当成可预览图片。
        // Show only successful tasks with an Asset so failed or pending tasks are not treated as previews.
        ReplaceBindingItems(_previewTasks, _tasks.Where(task => task.Status == CaptureTaskStatus.Succeeded && !string.IsNullOrWhiteSpace(task.AssetUrl)));
        _query.Enabled = _tasks.Count > 0;
        SelectPreviewTask(selectedTaskId);
    }

    private static void ReplaceBindingItems(BindingList<CaptureTaskSnapshot> destination, IEnumerable<CaptureTaskSnapshot> source)
    {
        destination.RaiseListChangedEvents = false;
        destination.Clear();
        foreach (var task in source) destination.Add(task);
        destination.RaiseListChangedEvents = true;
        destination.ResetBindings();
    }

    private void SelectPreviewTask(string? taskId)
    {
        if (_previewTasks.Count == 0)
        {
            _previewGrid.ClearSelection();
            SetPreviewImage(null);
            _previewStatus.Text = _language == UiLanguage.English ? "Successful captures will appear here." : "成功采集的照片会在这里显示。";
            _previewAsset.Clear();
            return;
        }

        var index = taskId is null ? 0 : _previewTasks.ToList().FindIndex(task => task.TaskId == taskId);
        index = index < 0 ? 0 : index;
        _previewGrid.ClearSelection();
        _previewGrid.Rows[index].Selected = true;
        _previewGrid.CurrentCell = _previewGrid.Rows[index].Cells[0];
    }

    private async Task LoadSelectedPreviewAsync()
    {
        if (_disposing || _previewGrid.CurrentRow?.DataBoundItem is not CaptureTaskSnapshot task || string.IsNullOrWhiteSpace(task.AssetUrl)) return;
        _previewLoadCancellation?.Cancel();
        _previewLoadCancellation?.Dispose();
        var cancellation = _previewLoadCancellation = new CancellationTokenSource();
        // 选择另一条任务时取消上一张图片的下载，避免慢响应覆盖当前选中项。
        // Cancel the previous image download when selection changes so a slow response cannot overwrite the current item.
        _previewStatus.Text = _language == UiLanguage.English
            ? $"Loading {task.OutputFormat.ToUpperInvariant()} preview ({task.AssetKind ?? "asset"}): {task.TaskId}"
            : $"正在加载 {task.OutputFormat.ToUpperInvariant()} 预览（{task.AssetKind ?? "asset"}）：{task.TaskId}";
        _previewAsset.Text = task.AssetUrl;
        try
        {
            var bytes = await _composition.DownloadAssetAsync(task.AssetUrl, cancellation.Token);
            using var stream = new MemoryStream(bytes);
            using var downloaded = Image.FromStream(stream);
            // Image.FromStream 依赖底层流的生命周期，复制为 Bitmap 后才能安全释放响应流。
            // Image.FromStream depends on the stream lifetime; copy to a Bitmap before disposing the response stream.
            var image = new Bitmap(downloaded);
            if (_disposing || cancellation.IsCancellationRequested)
            {
                image.Dispose();
                return;
            }
            SetPreviewImage(image);
            _previewStatus.Text = _language == UiLanguage.English
                ? $"Preview loaded: {task.TaskId} ({task.OutputFormat.ToUpperInvariant()}, {task.AssetKind ?? "asset"}, {task.LocalUpdatedAtDisplay}){FormatWarnings(task, true)}"
                : $"预览已加载：{task.TaskId}（{task.OutputFormat.ToUpperInvariant()}，{task.AssetKind ?? "asset"}，{task.LocalUpdatedAtDisplay}）{FormatWarnings(task, false)}";
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            SetPreviewImage(null);
            _previewStatus.Text = _language == UiLanguage.English
                ? $"Unable to load preview: {exception.Message}"
                : $"无法加载预览：{exception.Message}";
        }
    }

    private void SetPreviewImage(Image? image)
    {
        var previous = _previewImage.Image;
        _previewImage.Image = image;
        previous?.Dispose();
    }

    private void UpdateTaskHint(CaptureTaskSnapshot task)
    {
        if (task.Status == CaptureTaskStatus.Succeeded)
        {
            _hint.Text = _language == UiLanguage.English
                ? $"Capture completed: {task.TaskId} ({task.LastUpdateSource}).{(string.IsNullOrWhiteSpace(task.AssetUrl) ? " No downloadable Asset is available." : $" Preview asset: {task.AssetKind ?? "unknown"}.")}{FormatWarnings(task, true)}"
                : $"采集完成：{task.TaskId}（{task.LastUpdateSource}）。{(string.IsNullOrWhiteSpace(task.AssetUrl) ? "结果不含可下载 Asset。" : $"预览资源：{task.AssetKind ?? "未知"}。")}{FormatWarnings(task, false)}";
            return;
        }
        if (task.Status == CaptureTaskStatus.Failed)
        {
            _hint.Text = _language == UiLanguage.English
                ? $"Capture failed: {task.TaskId} ({task.LastUpdateSource}). {task.Error ?? "The runtime did not provide a reason."}"
                : $"采集失败：{task.TaskId}（{task.LastUpdateSource}）。{task.Error ?? "运行时未提供原因。"}";
            return;
        }
        _hint.Text = _language == UiLanguage.English
            ? $"Task {task.TaskId} is waiting for completion; automatically polled {task.PollAttempts}/40 times. Last source: {task.LastUpdateSource}. Select 'Query Task Now' to refresh."
            : $"任务 {task.TaskId} 正在等待完成事件；已自动查询 {task.PollAttempts}/40 次，最后更新来源：{task.LastUpdateSource}。可点击“立即查询任务”。";
    }

    private static string FormatWarnings(CaptureTaskSnapshot task, bool english) => task.Warnings.Count == 0
        ? string.Empty
        : english ? $" Warnings: {task.WarningsDisplay}" : $" 警告：{task.WarningsDisplay}";

    private void BeginSafe(Action action)
    {
        if (_disposing || IsDisposed || !IsHandleCreated) return;
        if (InvokeRequired) BeginInvoke(action); else action();
    }

    public void ActivatePage()
    {
        if (_disposing) return;
        _pageActivation.Activate();
        RefreshFromWorkflow();
        _ = LoadEnhanceWorkflowsAsync(forceRefresh: false);
        _ = EnsureCaptureSessionAsync();
    }

    public void DeactivatePage()
    {
        if (_disposing) return;
        _pageActivation.Deactivate();
        _livePreviewTimer.Stop();
        _ = StopCaptureSessionAsync();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _disposing = true;
            _livePreviewTimer.Stop();
            _previewLoadCancellation?.Cancel();
            _previewLoadCancellation?.Dispose();
            SetPreviewImage(null);
            _composition.Coordinator.DeviceWorkflow.Changed -= OnWorkflowChanged;
            _composition.Coordinator.CaptureTasks.Changed -= OnTaskChanged;
            _composition.CaptureVideoPreview.ConnectionClosed -= OnVideoConnectionClosed;
        }
        base.Dispose(disposing);
    }
}
