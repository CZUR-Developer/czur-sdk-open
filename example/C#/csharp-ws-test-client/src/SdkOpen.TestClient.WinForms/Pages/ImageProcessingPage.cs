using System.ComponentModel;
using System.Text.Json;
using SdkOpen.TestClient.Core.Application;
using SdkOpen.TestClient.Core.Protocol;

namespace SdkOpen.TestClient.WinForms;

public sealed class ImageProcessingPage : UserControl, ILocalizablePage, IPageLifecycle
{
    private sealed class LocalizedComboBox : ComboBox
    {
        public void RefreshLocalizedItems() => RefreshItems();
    }

    private sealed class OutputRow : IDisposable
    {
        public required ImageProcessingOutput Output { get; init; }
        public Image? Thumbnail { get; set; }
        public Image? FullImage { get; set; }
        public string AssetId => Output.AssetId;
        public string Role => Output.DisplayRole;
        public string ContentType => Output.ContentType;
        public string SizeText => Output.Width is > 0 && Output.Height is > 0 ? $"{Output.Width} × {Output.Height}" : "-";
        public string Url => Output.AccessUrl;
        public string State { get; set; } = string.Empty;
        public string Error { get; set; } = string.Empty;

        public void Dispose()
        {
            Thumbnail?.Dispose();
            FullImage?.Dispose();
        }
    }

    private readonly AppComposition _composition;
    private readonly Action<string, string> _showProtocol;
    private readonly LocalizedComboBox _operation = NewCombo();
    private readonly LocalizedComboBox _pageProcessing = NewCombo();
    private readonly LocalizedComboBox _colorMode = NewCombo();
    private readonly LocalizedComboBox _outputFormat = NewCombo();
    private readonly LocalizedComboBox _areaMode = NewCombo();
    private readonly LocalizedComboBox _fingerType = NewCombo();
    private readonly Button _chooseImage = new() { AutoSize = true };
    private readonly Button _upload = new() { AutoSize = true };
    private readonly Button _run = new() { AutoSize = true };
    private readonly Button _clearArea = new() { AutoSize = true };
    private readonly Label _title = Heading();
    private readonly Label _subtitle = Description();
    private readonly Label _inputImageLabel = FieldLabel();
    private readonly Label _fileName = Description();
    private readonly Label _originalTitle = FieldLabel();
    private readonly Label _processedTitle = FieldLabel();
    private readonly Label _processingOptionsTitle = Heading();
    private readonly Label _processingOptionsDescription = Description();
    private readonly Label _resultsTitle = Heading();
    private readonly Label _resultsDescription = Description();
    private readonly Label _operationLabel = FieldLabel();
    private readonly Label _pageProcessingLabel = FieldLabel();
    private readonly Label _colorModeLabel = FieldLabel();
    private readonly Label _outputFormatLabel = FieldLabel();
    private readonly Label _areaModeLabel = FieldLabel();
    private readonly Label _fingerTypeLabel = FieldLabel();
    private readonly Label _sessionName = FieldLabel();
    private readonly Label _sessionValue = Description();
    private readonly Label _uploadIdName = FieldLabel();
    private readonly Label _uploadIdValue = Description();
    private readonly Label _stateName = FieldLabel();
    private readonly Label _stateValue = Description();
    private readonly Label _codeName = FieldLabel();
    private readonly Label _codeValue = Description();
    private readonly Label _taskIdName = FieldLabel();
    private readonly Label _taskIdValue = Description();
    private readonly Label _outputPathName = FieldLabel();
    private readonly Label _outputPathValue = Description();
    private readonly Label _outputsName = FieldLabel();
    private readonly Label _outputsValue = Description();
    private readonly Label _notice = Description();
    private readonly Label _curvedNotice = Description();
    private readonly Label _selectionHint = Description();
    private readonly Label _error = Description();
    private readonly CheckBox _singleCrop = new() { AutoSize = true };
    private readonly NumericUpDown _singleCropWidth = MarginNumber();
    private readonly NumericUpDown _singleCropHeight = MarginNumber();
    private readonly Label _singleCropWidthLabel = FieldLabel();
    private readonly Label _singleCropHeightLabel = FieldLabel();
    private readonly CheckBox _idCardRoundCorner = new() { AutoSize = true };
    private readonly CheckBox _autoRotate = new() { AutoSize = true };
    private readonly CheckBox _smartBlackEdge = new() { AutoSize = true, Checked = true };
    private readonly CheckBox _multiTargetPaging = new() { AutoSize = true };
    private readonly CheckBox _removeFinger = new() { AutoSize = true, Checked = true };
    private readonly CheckBox _smartPaging = new() { AutoSize = true, Checked = true };
    private readonly CheckBox _curvedCrop = new() { AutoSize = true };
    private readonly NumericUpDown _curvedCropWidth = MarginNumber();
    private readonly NumericUpDown _curvedCropHeight = MarginNumber();
    private readonly Label _curvedCropWidthLabel = FieldLabel();
    private readonly Label _curvedCropHeightLabel = FieldLabel();
    private readonly CheckBox _autoComplete = new() { AutoSize = true };
    private readonly ImageSelectionPreviewControl _originalPreview = new() { Dock = DockStyle.Fill, MinimumSize = new Size(250, 260) };
    private readonly ImageSelectionPreviewControl _processedPreview = new() { Dock = DockStyle.Fill, MinimumSize = new Size(250, 260) };
    private readonly DataGridView _outputGrid = new()
    {
        Dock = DockStyle.Fill,
        AutoGenerateColumns = false,
        AllowUserToAddRows = false,
        AllowUserToDeleteRows = false,
        ReadOnly = true,
        MultiSelect = false,
        SelectionMode = DataGridViewSelectionMode.FullRowSelect,
        RowTemplate = { Height = 72 }
    };
    private readonly BindingList<OutputRow> _outputRows = [];
    private CancellationTokenSource? _operationCancellation;
    private UiLanguage _language = UiLanguage.Chinese;
    private string? _selectedFilePath;
    private string _uploadId = string.Empty;
    private string _lastRequestJson = "{}";
    private string _lastResponseJson = "{}";
    private string _status = "idle";
    private string _lastError = string.Empty;
    private int? _lastCode;
    private ImageProcessingResult? _lastResult;
    private bool _busy;
    private bool _disposing;

    public ImageProcessingPage(AppComposition composition, Action<string, string> showProtocol)
    {
        _composition = composition;
        _showProtocol = showProtocol;
        ConfigureChoices();
        ConfigureGrid();
        BuildLayout();
        WireEvents();
        ApplyLanguage(_language);
        UpdateControlState();
    }

    public void ApplyLanguage(UiLanguage language)
    {
        _language = language;
        var english = language == UiLanguage.English;
        _title.Text = english ? "Image Processing" : "图像处理";
        _subtitle.Text = english
            ? "Choose an image, preview the original, run paper processing and color mode, then compare the processed result."
            : "选择图片并预览原图，执行纸张处理和色彩模式后，对比查看处理结果。";
        _inputImageLabel.Text = english ? "Input image" : "输入图片";
        _chooseImage.Text = english ? "Choose image" : "选择图片";
        _upload.Text = _busy && _status == "uploading" ? (english ? "Uploading..." : "上传中...") : (english ? "Upload" : "上传");
        _run.Text = _busy && _status == "processing" ? (english ? "Processing..." : "处理中...") : (english ? "Run" : "执行");
        _originalTitle.Text = english ? "Original preview" : "原图预览";
        _processedTitle.Text = english ? "Processed preview" : "处理后预览";
        _processingOptionsTitle.Text = english ? "Processing options" : "处理参数";
        _processingOptionsDescription.Text = english
            ? "Paper processing and color mode match the capture profile configuration."
            : "纸张处理和色彩模式沿用采集 Profile 的配置方式。";
        _resultsTitle.Text = english ? "Results" : "结果";
        _resultsDescription.Text = english
            ? "The first output is returned as output_path; multi-page results are listed below."
            : "第一页结果会通过 output_path 返回；多页结果会在下方列表展示。";
        _operationLabel.Text = english ? "Operation" : "处理类型";
        _pageProcessingLabel.Text = english ? "Page processing" : "页面处理方式";
        _colorModeLabel.Text = english ? "Color mode" : "色彩模式";
        _outputFormatLabel.Text = english ? "Output format" : "输出格式";
        _areaModeLabel.Text = english ? "Selected area" : "区域选取方式";
        _fingerTypeLabel.Text = english ? "Finger type" : "手指类型";
        _singleCrop.Text = english ? "Crop border" : "裁边";
        _singleCropWidthLabel.Text = english ? "Crop width" : "裁边宽度";
        _singleCropHeightLabel.Text = english ? "Crop height" : "裁边高度";
        _idCardRoundCorner.Text = english ? "ID card rounded corner padding" : "证件圆角补边";
        _autoRotate.Text = english ? "Auto rotate page" : "页面自动转正";
        _smartBlackEdge.Text = english ? "Smart black edge optimize" : "智能优化黑边";
        _multiTargetPaging.Text = english ? "Multi-target auto paging" : "多目标自动分页";
        _removeFinger.Text = english ? "Remove finger" : "清除手指";
        _smartPaging.Text = english ? "Smart paging" : "智能分页";
        _curvedCrop.Text = english ? "Crop border" : "裁边";
        _curvedCropWidthLabel.Text = english ? "Crop width" : "裁边宽度";
        _curvedCropHeightLabel.Text = english ? "Crop height" : "裁边高度";
        _autoComplete.Text = english ? "Auto complete page" : "自动补全";
        _clearArea.Text = english ? "Clear selected area" : "清除选区";
        _curvedNotice.Text = english
            ? "Standalone image processing uses edge-based curved-book flattening. Laser-line flattening is only available in capture flow results from supported devices."
            : "独立图像处理使用基于边缘的曲面展平；激光线展平仅在支持设备的采集流结果中生效。";
        _sessionName.Text = english ? "Session" : "会话";
        _uploadIdName.Text = english ? "Upload ID" : "上传 ID";
        _stateName.Text = english ? "Status" : "状态";
        _codeName.Text = english ? "Code" : "代码";
        _taskIdName.Text = english ? "Task ID" : "任务 ID";
        _outputPathName.Text = english ? "Output path" : "输出路径";
        _outputsName.Text = english ? "Outputs" : "输出数量";
        _originalPreview.Tag = english ? "Choose a local image to preview it here." : "选择本地图片后可在这里预览。";
        _processedPreview.Tag = english ? "Processed image will appear here after the selected image command completes." : "执行选中的图像处理指令后，处理结果会显示在这里。";
        var headers = english
            ? new[] { "Preview", "Role", "Content type", "Dimensions", "Asset ID", "URL", "State", "Error" }
            : new[] { "预览", "角色", "内容类型", "尺寸", "Asset ID", "地址", "状态", "错误" };
        for (var index = 0; index < Math.Min(headers.Length, _outputGrid.Columns.Count); index++) _outputGrid.Columns[index].HeaderText = headers[index];
        RefreshComboTexts();
        UpdateStatusSummary();
        UpdateSelectionHint();
        UpdateNotice();
        Invalidate(true);
    }

    public void ActivatePage() => UpdateControlState();

    public void DeactivatePage()
    {
        _operationCancellation?.Cancel();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing && !_disposing)
        {
            _disposing = true;
            _operationCancellation?.Cancel();
            _operationCancellation?.Dispose();
            ClearOutputRows();
        }
        base.Dispose(disposing);
    }

    private void ConfigureChoices()
    {
        _operation.Items.AddRange([ImageProcessingOperation.Combined, ImageProcessingOperation.Page, ImageProcessingOperation.Color]);
        _pageProcessing.Items.AddRange([CapturePageProcessing.KeepOriginal, CapturePageProcessing.SinglePage, CapturePageProcessing.Selection, CapturePageProcessing.CurvedBook]);
        _colorMode.Items.AddRange([CaptureColorMode.AutoOptimize, CaptureColorMode.Color, CaptureColorMode.BlackWhite, CaptureColorMode.WhitePaperSeal, CaptureColorMode.Grayscale, CaptureColorMode.Certificate, CaptureColorMode.Ancient, CaptureColorMode.NoOptimize]);
        _outputFormat.Items.AddRange([CaptureOutputFormat.Jpg, CaptureOutputFormat.Png, CaptureOutputFormat.Tiff]);
        _areaMode.Items.AddRange([ImageAreaMode.Rectangle, ImageAreaMode.Points]);
        _fingerType.Items.AddRange(["with_sleeve", "without_sleeve"]);
        _operation.SelectedItem = ImageProcessingOperation.Combined;
        _pageProcessing.SelectedItem = CapturePageProcessing.SinglePage;
        _colorMode.SelectedItem = CaptureColorMode.AutoOptimize;
        _outputFormat.SelectedItem = CaptureOutputFormat.Jpg;
        _areaMode.SelectedItem = ImageAreaMode.Rectangle;
        _fingerType.SelectedItem = "with_sleeve";
        foreach (var combo in new[] { _operation, _pageProcessing, _colorMode, _outputFormat, _areaMode, _fingerType })
        {
            combo.Format += FormatChoice;
        }
    }

    private void ConfigureGrid()
    {
        _outputGrid.Columns.Add(new DataGridViewImageColumn { DataPropertyName = nameof(OutputRow.Thumbnail), ImageLayout = DataGridViewImageCellLayout.Zoom, Width = 90 });
        _outputGrid.Columns.Add(new DataGridViewTextBoxColumn { DataPropertyName = nameof(OutputRow.Role), Width = 100 });
        _outputGrid.Columns.Add(new DataGridViewTextBoxColumn { DataPropertyName = nameof(OutputRow.ContentType), Width = 115 });
        _outputGrid.Columns.Add(new DataGridViewTextBoxColumn { DataPropertyName = nameof(OutputRow.SizeText), Width = 95 });
        _outputGrid.Columns.Add(new DataGridViewTextBoxColumn { DataPropertyName = nameof(OutputRow.AssetId), Width = 135 });
        _outputGrid.Columns.Add(new DataGridViewTextBoxColumn { DataPropertyName = nameof(OutputRow.Url), Width = 210 });
        _outputGrid.Columns.Add(new DataGridViewTextBoxColumn { DataPropertyName = nameof(OutputRow.State), Width = 90 });
        _outputGrid.Columns.Add(new DataGridViewTextBoxColumn { DataPropertyName = nameof(OutputRow.Error), AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill, MinimumWidth = 140 });
        _outputGrid.DataSource = _outputRows;
    }

    private void BuildLayout()
    {
        var scroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true };
        var content = new TableLayoutPanel
        {
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            ColumnCount = 1,
            RowCount = 8,
            Padding = new Padding(12),
            MinimumSize = new Size(980, 0),
            Dock = DockStyle.Top
        };
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        content.Controls.Add(_title, 0, 0);
        content.Controls.Add(_subtitle, 0, 1);
        content.Controls.Add(BuildInputHeader(), 0, 2);
        content.Controls.Add(BuildPreviewArea(), 0, 3);
        content.Controls.Add(BuildRuntimeAndActions(), 0, 4);
        content.Controls.Add(BuildLowerArea(), 0, 5);
        scroll.Controls.Add(content);
        scroll.SizeChanged += (_, _) => content.Width = Math.Max(content.MinimumSize.Width, scroll.ClientSize.Width - 28);
        Controls.Add(scroll);
    }

    private Control BuildInputHeader()
    {
        var panel = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 3, Padding = new Padding(0, 8, 0, 8) };
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        panel.Controls.Add(_inputImageLabel, 0, 0);
        panel.Controls.Add(_fileName, 1, 0);
        panel.Controls.Add(_chooseImage, 2, 0);
        return panel;
    }

    private Control BuildPreviewArea()
    {
        var table = new TableLayoutPanel { Dock = DockStyle.Top, Height = 330, ColumnCount = 2, Padding = new Padding(0, 0, 0, 8) };
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        table.Controls.Add(BuildPreviewCard(_originalTitle, _originalPreview), 0, 0);
        table.Controls.Add(BuildPreviewCard(_processedTitle, _processedPreview), 1, 0);
        return table;
    }

    private static Control BuildPreviewCard(Label title, Control preview)
    {
        var panel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2, Margin = new Padding(4), Padding = new Padding(6), BackColor = Color.WhiteSmoke };
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        panel.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        panel.Controls.Add(title, 0, 0);
        panel.Controls.Add(preview, 0, 1);
        return panel;
    }

    private Control BuildRuntimeAndActions()
    {
        var panel = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 7, Padding = new Padding(0, 2, 0, 10) };
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 35));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 40));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        panel.Controls.Add(_sessionName, 0, 0);
        panel.Controls.Add(_sessionValue, 1, 0);
        panel.Controls.Add(_uploadIdName, 2, 0);
        panel.Controls.Add(_uploadIdValue, 3, 0);
        panel.Controls.Add(_error, 4, 0);
        panel.Controls.Add(_upload, 5, 0);
        panel.Controls.Add(_run, 6, 0);
        return panel;
    }

    private Control BuildLowerArea()
    {
        var table = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 2, Padding = new Padding(0, 0, 0, 12) };
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 43));
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 57));
        table.Controls.Add(BuildSettingsPanel(), 0, 0);
        table.Controls.Add(BuildResultsPanel(), 1, 0);
        return table;
    }

    private Control BuildSettingsPanel()
    {
        var panel = new TableLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, ColumnCount = 2, Padding = new Padding(8), Margin = new Padding(4) };
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 55));
        panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 45));
        AddFullRow(panel, _processingOptionsTitle);
        AddFullRow(panel, _processingOptionsDescription);
        AddField(panel, _operationLabel, _operation);
        AddField(panel, _pageProcessingLabel, _pageProcessing);
        AddField(panel, _areaModeLabel, _areaMode);
        AddFullRow(panel, _selectionHint);
        AddFullRow(panel, _clearArea);
        AddField(panel, _colorModeLabel, _colorMode);
        AddField(panel, _outputFormatLabel, _outputFormat);
        AddFullRow(panel, _notice);
        AddFullRow(panel, _singleCrop);
        AddField(panel, _singleCropWidthLabel, _singleCropWidth);
        AddField(panel, _singleCropHeightLabel, _singleCropHeight);
        AddFullRow(panel, _idCardRoundCorner);
        AddFullRow(panel, _autoRotate);
        AddFullRow(panel, _smartBlackEdge);
        AddFullRow(panel, _multiTargetPaging);
        AddFullRow(panel, _curvedNotice);
        AddFullRow(panel, _removeFinger);
        AddField(panel, _fingerTypeLabel, _fingerType);
        AddFullRow(panel, _smartPaging);
        AddFullRow(panel, _curvedCrop);
        AddField(panel, _curvedCropWidthLabel, _curvedCropWidth);
        AddField(panel, _curvedCropHeightLabel, _curvedCropHeight);
        AddFullRow(panel, _autoComplete);
        return panel;
    }

    private Control BuildResultsPanel()
    {
        var panel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 4, Padding = new Padding(8), Margin = new Padding(4), MinimumSize = new Size(0, 620) };
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        panel.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        panel.Controls.Add(_resultsTitle, 0, 0);
        panel.Controls.Add(_resultsDescription, 0, 1);
        var metadata = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 2 };
        metadata.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        metadata.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        AddField(metadata, _stateName, _stateValue);
        AddField(metadata, _codeName, _codeValue);
        AddField(metadata, _taskIdName, _taskIdValue);
        AddField(metadata, _outputPathName, _outputPathValue);
        AddField(metadata, _outputsName, _outputsValue);
        panel.Controls.Add(metadata, 0, 2);
        panel.Controls.Add(_outputGrid, 0, 3);
        return panel;
    }

    private void WireEvents()
    {
        _chooseImage.Click += (_, _) => ChooseImage();
        _upload.Click += async (_, _) => await UploadAsync();
        _run.Click += async (_, _) => await RunAsync();
        _clearArea.Click += (_, _) => _originalPreview.ClearSelection();
        _operation.SelectedIndexChanged += (_, _) => UpdateControlState();
        _pageProcessing.SelectedIndexChanged += (_, _) =>
        {
            if (SelectedPageProcessing() != CapturePageProcessing.Selection) _originalPreview.ClearSelection();
            UpdateControlState();
        };
        _areaMode.SelectedIndexChanged += (_, _) =>
        {
            _originalPreview.AreaMode = SelectedAreaMode();
            UpdateSelectionHint();
        };
        _singleCrop.CheckedChanged += (_, _) => UpdateControlState();
        _curvedCrop.CheckedChanged += (_, _) => UpdateControlState();
        _removeFinger.CheckedChanged += (_, _) => UpdateControlState();
        _originalPreview.SelectionChanged += (_, _) => UpdateSelectionHint();
        _originalPreview.DoubleClick += (_, _) =>
        {
            if (!_originalPreview.SelectionEnabled) ShowImageViewer(_originalPreview, _originalTitle.Text);
        };
        _processedPreview.DoubleClick += (_, _) => ShowImageViewer(_processedPreview, _processedTitle.Text);
        _outputGrid.SelectionChanged += (_, _) => ShowSelectedOutput();
        _outputGrid.CellDoubleClick += (_, args) =>
        {
            if (args.RowIndex >= 0)
            {
                ShowSelectedOutput();
                _showProtocol(_lastRequestJson, _lastResponseJson);
                ShowImageViewer(_processedPreview, _processedTitle.Text);
            }
        };
    }

    private void ChooseImage()
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "Image files|*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.webp;*.tif;*.tiff|All files|*.*",
            Multiselect = false,
            CheckFileExists = true
        };
        if (dialog.ShowDialog(FindForm()) != DialogResult.OK) return;

        try
        {
            using var stream = new FileStream(dialog.FileName, FileMode.Open, FileAccess.Read, FileShare.Read);
            using var decoded = Image.FromStream(stream);
            using var copy = new Bitmap(decoded);
            _originalPreview.SetImage(copy);
            _selectedFilePath = dialog.FileName;
            _fileName.Text = Path.GetFileName(dialog.FileName);
            _uploadId = string.Empty;
            _lastError = string.Empty;
            _lastCode = null;
            _lastResult = null;
            _status = "ready";
            ClearOutputRows();
            UpdateControlState();
        }
        catch (Exception exception)
        {
            _lastError = exception.Message;
            _status = "error";
            UpdateStatusSummary();
        }
    }

    private async Task UploadAsync()
    {
        if (_busy) return;
        var scope = BeginOperation("uploading");
        try
        {
            await EnsureUploadedAsync(scope.Token);
            _status = "uploaded";
        }
        catch (OperationCanceledException) when (scope.IsCancellationRequested)
        {
            _status = "cancelled";
        }
        catch (Exception exception)
        {
            _lastError = exception.Message;
            _status = "error";
        }
        finally
        {
            EndOperation(scope);
        }
    }

    private async Task RunAsync()
    {
        if (_busy || string.IsNullOrWhiteSpace(_selectedFilePath)) return;
        var scope = BeginOperation("processing");
        _lastError = string.Empty;
        _lastCode = null;
        _lastResult = null;
        ClearOutputRows();
        try
        {
            var uploadId = await EnsureUploadedAsync(scope.Token);
            var request = ImageProcessingRequestBuilder.Build(uploadId, ReadOptions());
            _lastRequestJson = JsonSerializer.Serialize(new { method = request.Method, @params = request.Parameters }, JsonOptions);
            var response = await _composition.Coordinator.ExecuteImageProcessingAsync(request, scope.Token);
            _lastResponseJson = JsonSerializer.Serialize(response, JsonOptions);
            _lastCode = response.Code;
            _showProtocol(_lastRequestJson, _lastResponseJson);
            _lastResult = ImageProcessingResultParser.Parse(response.Data);
            PopulateOutputs(_lastResult.Outputs);
            await LoadOutputPreviewsAsync(scope.Token);
            _status = "success";
        }
        catch (OperationCanceledException) when (scope.IsCancellationRequested)
        {
            _status = "cancelled";
        }
        catch (Exception exception)
        {
            _lastError = exception.Message;
            _status = "error";
            _lastResponseJson = JsonSerializer.Serialize(new { error = exception.Message }, JsonOptions);
            _showProtocol(_lastRequestJson, _lastResponseJson);
        }
        finally
        {
            EndOperation(scope);
        }
    }

    private async Task<string> EnsureUploadedAsync(CancellationToken cancellationToken)
    {
        if (!string.IsNullOrWhiteSpace(_uploadId)) return _uploadId;
        if (string.IsNullOrWhiteSpace(_selectedFilePath))
        {
            throw new InvalidOperationException(_language == UiLanguage.English ? "No image selected." : "未选择图片。");
        }
        _uploadId = await _composition.UploadImageAsync(_selectedFilePath, cancellationToken);
        UpdateStatusSummary();
        return _uploadId;
    }

    private ImageProcessingOptions ReadOptions() => ImageProcessingOptions.Default with
    {
        Operation = SelectedOperation(),
        PageProcessing = SelectedPageProcessing(),
        ColorMode = SelectedColorMode(),
        OutputFormat = SelectedOutputFormat(),
        SinglePageCropBorderEnabled = _singleCrop.Checked,
        SinglePageCropBorderWidth = (int)_singleCropWidth.Value,
        SinglePageCropBorderHeight = (int)_singleCropHeight.Value,
        IdCardRoundCorner = _idCardRoundCorner.Checked,
        AutoRotate = _autoRotate.Checked,
        SmartBlackEdgeOptimize = _smartBlackEdge.Checked,
        MultiTargetPaging = _multiTargetPaging.Checked,
        SelectedArea = _originalPreview.SelectedArea,
        CurvedBookRemoveFinger = _removeFinger.Checked,
        CurvedBookFingerType = _fingerType.SelectedItem as string ?? "with_sleeve",
        CurvedBookSmartPaging = _smartPaging.Checked,
        CurvedBookCropBorderEnabled = _curvedCrop.Checked,
        CurvedBookCropBorderWidth = (int)_curvedCropWidth.Value,
        CurvedBookCropBorderHeight = (int)_curvedCropHeight.Value,
        CurvedBookAutoComplete = _autoComplete.Checked
    };

    private void PopulateOutputs(IReadOnlyList<ImageProcessingOutput> outputs)
    {
        foreach (var output in outputs)
        {
            _outputRows.Add(new OutputRow
            {
                Output = output,
                State = output.CanPreview ? Localized("Waiting", "等待加载") : Localized("Preview unsupported", "不支持预览")
            });
        }
        if (_outputGrid.Rows.Count > 0) _outputGrid.Rows[0].Selected = true;
    }

    private async Task LoadOutputPreviewsAsync(CancellationToken cancellationToken)
    {
        for (var index = 0; index < _outputRows.Count; index++)
        {
            var row = _outputRows[index];
            if (!row.Output.CanPreview || string.IsNullOrWhiteSpace(row.Output.AccessUrl)) continue;
            row.State = Localized("Loading", "加载中");
            _outputRows.ResetItem(index);
            try
            {
                var bytes = await _composition.DownloadAssetAsync(row.Output.AccessUrl, cancellationToken);
                using var stream = new MemoryStream(bytes, false);
                using var decoded = Image.FromStream(stream);
                row.FullImage = new Bitmap(decoded);
                row.Thumbnail = new Bitmap(row.FullImage, CalculateThumbnailSize(row.FullImage.Size));
                row.State = Localized("Ready", "已就绪");
                if (index == 0) _processedPreview.SetImage(row.FullImage);
            }
            catch (OperationCanceledException) { throw; }
            catch (Exception exception)
            {
                row.State = Localized("Preview failed", "预览失败");
                row.Error = exception.Message;
            }
            _outputRows.ResetItem(index);
        }
    }

    private void ShowSelectedOutput()
    {
        if (_outputGrid.CurrentRow?.DataBoundItem is OutputRow row)
        {
            _processedPreview.SetImage(row.FullImage);
        }
    }

    private void UpdateControlState()
    {
        var operation = SelectedOperation();
        var pageEnabled = operation != ImageProcessingOperation.Color;
        var colorEnabled = operation != ImageProcessingOperation.Page;
        var pageMode = SelectedPageProcessing();
        var single = pageEnabled && pageMode == CapturePageProcessing.SinglePage;
        var curved = pageEnabled && pageMode == CapturePageProcessing.CurvedBook;
        var selection = pageEnabled && pageMode == CapturePageProcessing.Selection;

        _operation.Enabled = !_busy;
        _pageProcessing.Enabled = !_busy && pageEnabled;
        _colorMode.Enabled = !_busy && colorEnabled;
        _outputFormat.Enabled = !_busy && operation == ImageProcessingOperation.Combined;
        _areaMode.Enabled = !_busy && selection;
        _clearArea.Enabled = !_busy && selection;
        _originalPreview.SelectionEnabled = !_busy && selection;
        _singleCrop.Enabled = !_busy && single;
        _singleCropWidth.Enabled = !_busy && single && _singleCrop.Checked;
        _singleCropHeight.Enabled = !_busy && single && _singleCrop.Checked;
        _idCardRoundCorner.Enabled = !_busy && single;
        _autoRotate.Enabled = !_busy && single;
        _smartBlackEdge.Enabled = !_busy && single;
        _multiTargetPaging.Enabled = !_busy && single;
        _removeFinger.Enabled = !_busy && curved;
        _fingerType.Enabled = !_busy && curved && _removeFinger.Checked;
        _smartPaging.Enabled = !_busy && curved;
        _curvedCrop.Enabled = !_busy && curved;
        _curvedCropWidth.Enabled = !_busy && curved && _curvedCrop.Checked;
        _curvedCropHeight.Enabled = !_busy && curved && _curvedCrop.Checked;
        _autoComplete.Enabled = !_busy && curved;
        _chooseImage.Enabled = !_busy;
        _upload.Enabled = !_busy && !string.IsNullOrWhiteSpace(_selectedFilePath);
        _run.Enabled = !_busy && !string.IsNullOrWhiteSpace(_selectedFilePath);
        UpdateSelectionHint();
        UpdateNotice();
        UpdateStatusSummary();
    }

    private void UpdateSelectionHint()
    {
        if (!_originalPreview.HasImage)
        {
            _selectionHint.Text = Localized("Choose an image before drawing a selected area.", "请先选择图片，再绘制区域。");
            return;
        }
        if (SelectedAreaMode() == ImageAreaMode.Points)
        {
            _selectionHint.Text = _language == UiLanguage.English
                ? $"Click four corners ({_originalPreview.SelectedPointCount}/4)."
                : $"依次点击四个角点（{_originalPreview.SelectedPointCount}/4）。";
            return;
        }
        _selectionHint.Text = _originalPreview.SelectedArea?.IsValid == true
            ? Localized("Selected area is ready.", "选区已就绪。")
            : Localized("Drag on the original preview to draw a rectangle.", "在原图预览中拖动以绘制矩形。");
    }

    private void UpdateNotice()
    {
        _notice.Text = SelectedOperation() == ImageProcessingOperation.Combined
            ? Localized(
                "Combined processing uses image.process and can apply paper processing, color mode, and output format in one request.",
                "综合处理使用 image.process，可在一次请求中同时应用纸张处理、色彩模式和输出格式。")
            : Localized(
                "Paper processing and color mode keep the source image format. Use file.convert for format conversion.",
                "纸张处理和色彩模式会保持源图片格式；格式转换请使用 file.convert。");
    }

    private void UpdateStatusSummary()
    {
        _fileName.Text = string.IsNullOrWhiteSpace(_selectedFilePath)
            ? Localized("No image selected.", "未选择图片。")
            : Path.GetFileName(_selectedFilePath);
        _sessionValue.Text = string.IsNullOrWhiteSpace(_composition.Coordinator.Session.SessionToken)
            ? Localized("Not bound", "未绑定")
            : Localized("Bound", "已绑定");
        _uploadIdValue.Text = string.IsNullOrWhiteSpace(_uploadId) ? "-" : _uploadId;
        _stateValue.Text = LocalizeStatus(_status);
        _codeValue.Text = _lastCode?.ToString() ?? "-";
        _taskIdValue.Text = string.IsNullOrWhiteSpace(_lastResult?.TaskId) ? "-" : _lastResult.TaskId;
        _outputPathValue.Text = string.IsNullOrWhiteSpace(_lastResult?.OutputPath) ? "-" : _lastResult.OutputPath;
        _outputsValue.Text = (_lastResult?.Outputs.Count ?? 0).ToString();
        _error.Text = _lastError;
        _error.ForeColor = string.IsNullOrWhiteSpace(_lastError) ? SystemColors.ControlText : Color.Firebrick;
        _upload.Text = _busy && _status == "uploading" ? Localized("Uploading...", "上传中...") : Localized("Upload", "上传");
        _run.Text = _busy && _status == "processing" ? Localized("Processing...", "处理中...") : Localized("Run", "执行");
    }

    private CancellationTokenSource BeginOperation(string status)
    {
        _operationCancellation?.Cancel();
        _operationCancellation?.Dispose();
        _operationCancellation = new CancellationTokenSource();
        _busy = true;
        _status = status;
        _lastError = string.Empty;
        UpdateControlState();
        return _operationCancellation;
    }

    private void EndOperation(CancellationTokenSource scope)
    {
        if (ReferenceEquals(_operationCancellation, scope)) _operationCancellation = null;
        scope.Dispose();
        _busy = false;
        UpdateControlState();
    }

    private void ClearOutputRows()
    {
        _processedPreview.SetImage(null);
        foreach (var row in _outputRows) row.Dispose();
        _outputRows.Clear();
    }

    private void RefreshComboTexts()
    {
        _operation.RefreshLocalizedItems();
        _pageProcessing.RefreshLocalizedItems();
        _colorMode.RefreshLocalizedItems();
        _outputFormat.RefreshLocalizedItems();
        _areaMode.RefreshLocalizedItems();
        _fingerType.RefreshLocalizedItems();
    }

    private void FormatChoice(object? sender, ListControlConvertEventArgs args)
    {
        args.Value = args.ListItem switch
        {
            ImageProcessingOperation value => value switch
            {
                ImageProcessingOperation.Combined => Localized("Combined", "综合处理"),
                ImageProcessingOperation.Page => Localized("Paper processing", "纸张处理"),
                ImageProcessingOperation.Color => Localized("Color mode", "色彩模式"),
                _ => value.ToString()
            },
            CapturePageProcessing value => value switch
            {
                CapturePageProcessing.KeepOriginal => Localized("Keep original", "保留原图"),
                CapturePageProcessing.SinglePage => Localized("Single page", "单页"),
                CapturePageProcessing.Selection => Localized("Selected area", "区域"),
                CapturePageProcessing.CurvedBook => Localized("Curved book", "曲面"),
                _ => value.ToString()
            },
            CaptureColorMode value => value switch
            {
                CaptureColorMode.AutoOptimize => Localized("Auto optimize", "自动优化"),
                CaptureColorMode.Color => Localized("Color", "彩色"),
                CaptureColorMode.BlackWhite => Localized("Black / white", "黑白"),
                CaptureColorMode.WhitePaperSeal => Localized("White paper seal", "白纸印章"),
                CaptureColorMode.Grayscale => Localized("Grayscale", "灰色"),
                CaptureColorMode.Certificate => Localized("Certificate texture", "证件底纹"),
                CaptureColorMode.Ancient => Localized("Ancient book", "古籍模式"),
                CaptureColorMode.NoOptimize => Localized("No optimize", "无优化"),
                _ => value.ToString()
            },
            CaptureOutputFormat value => value.ToString().ToUpperInvariant(),
            ImageAreaMode value => value == ImageAreaMode.Rectangle ? Localized("Rectangle", "矩形") : Localized("Four points", "四点"),
            string value when value == "with_sleeve" => Localized("With sleeve", "带指套"),
            string value when value == "without_sleeve" => Localized("Without sleeve", "不带指套"),
            _ => args.ListItem?.ToString() ?? string.Empty
        };
    }

    private string LocalizeStatus(string status) => status switch
    {
        "ready" => Localized("Local image ready", "本地图片已就绪"),
        "uploading" => Localized("Uploading", "上传中"),
        "uploaded" => Localized("Uploaded", "已上传"),
        "processing" => Localized("Processing", "处理中"),
        "success" => Localized("Success", "成功"),
        "error" => Localized("Error", "错误"),
        "cancelled" => Localized("Cancelled", "已取消"),
        _ => Localized("Idle", "空闲")
    };

    private string Localized(string english, string chinese) => _language == UiLanguage.English ? english : chinese;
    private ImageProcessingOperation SelectedOperation() => _operation.SelectedItem is ImageProcessingOperation value ? value : ImageProcessingOperation.Combined;
    private CapturePageProcessing SelectedPageProcessing() => _pageProcessing.SelectedItem is CapturePageProcessing value ? value : CapturePageProcessing.SinglePage;
    private CaptureColorMode SelectedColorMode() => _colorMode.SelectedItem is CaptureColorMode value ? value : CaptureColorMode.AutoOptimize;
    private CaptureOutputFormat SelectedOutputFormat() => _outputFormat.SelectedItem is CaptureOutputFormat value ? value : CaptureOutputFormat.Jpg;
    private ImageAreaMode SelectedAreaMode() => _areaMode.SelectedItem is ImageAreaMode value ? value : ImageAreaMode.Rectangle;

    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private static LocalizedComboBox NewCombo() => new() { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill, FormattingEnabled = true };
    private static Label Heading() => new() { AutoSize = true, Font = new Font(SystemFonts.DefaultFont, FontStyle.Bold), Padding = new Padding(4) };
    private static Label FieldLabel() => new() { AutoSize = true, Padding = new Padding(4, 6, 8, 4) };
    private static Label Description() => new() { AutoSize = true, MaximumSize = new Size(900, 0), Padding = new Padding(4) };
    private static NumericUpDown MarginNumber() => new() { Minimum = -100, Maximum = 100, Dock = DockStyle.Fill };

    private static void AddFullRow(TableLayoutPanel panel, Control control)
    {
        var row = panel.RowCount++;
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        panel.Controls.Add(control, 0, row);
        panel.SetColumnSpan(control, 2);
    }

    private static void AddField(TableLayoutPanel panel, Control label, Control value)
    {
        var row = panel.RowCount++;
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        panel.Controls.Add(label, 0, row);
        panel.Controls.Add(value, 1, row);
    }

    private static Size CalculateThumbnailSize(Size source)
    {
        const int maxWidth = 120;
        const int maxHeight = 64;
        var scale = Math.Min(maxWidth / (double)Math.Max(1, source.Width), maxHeight / (double)Math.Max(1, source.Height));
        return new Size(Math.Max(1, (int)Math.Round(source.Width * scale)), Math.Max(1, (int)Math.Round(source.Height * scale)));
    }

    private void ShowImageViewer(ImageSelectionPreviewControl preview, string title)
    {
        var image = preview.GetImageCopy();
        if (image is null) return;
        using var dialog = new Form
        {
            Text = title,
            StartPosition = FormStartPosition.CenterParent,
            Size = new Size(1100, 780),
            MinimumSize = new Size(640, 480)
        };
        var picture = new PictureBox { Dock = DockStyle.Fill, BackColor = Color.Black, SizeMode = PictureBoxSizeMode.Zoom, Image = image };
        dialog.Controls.Add(picture);
        dialog.ShowDialog(FindForm());
        picture.Image = null;
        image.Dispose();
    }
}
