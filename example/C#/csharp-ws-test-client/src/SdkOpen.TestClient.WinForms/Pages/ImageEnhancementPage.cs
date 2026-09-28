using System.ComponentModel;
using System.Text.Json;
using System.Text.Json.Nodes;
using SdkOpen.TestClient.Core.Application;

namespace SdkOpen.TestClient.WinForms;

/// <summary>
/// Structured image-enhancement workflow editor. It deliberately does not expose a raw command editor.
/// 结构化图像增强工作流编辑器，刻意不暴露原始命令编辑器。
/// </summary>
public sealed class ImageEnhancementPage : UserControl, ILocalizablePage, IPageLifecycle
{
    private sealed class EditableStep
    {
        public string Id { get; set; } = Guid.NewGuid().ToString("N")[..8];
        public string Type { get; set; } = string.Empty;
        public string Provider { get; set; } = "auto";
        public bool Enabled { get; set; } = true;
        public string OnError { get; set; } = "fail";
        public string ParamsJson { get; set; } = "{}";

        public override string ToString() => $"{Type} ({(Enabled ? "enabled" : "disabled")})";
    }

    private sealed class AssetRow
    {
        public required ImageEnhanceAssetSnapshot Asset { get; init; }
        public string Id => Asset.AssetId;
        public string ContentType => Asset.ContentType;
        public string Path => Asset.Path;
        public string Url => Asset.Url;
    }

    private readonly AppComposition _composition;
    private readonly Action<string, string> _showProtocol;
    private readonly Label _title = Heading();
    private readonly Label _subtitle = Description();
    private readonly Label _notice = Description();
    private readonly Label _inputHeader = SectionHeading();
    private readonly Label _workflowHeader = SectionHeading();
    private readonly Label _capabilityHeader = SectionHeading();
    private readonly Label _runHeader = SectionHeading();
    private readonly Label _stepHeader = SectionHeading();
    private readonly Label _resultHeader = SectionHeading();
    private readonly Label _workflowFieldLabel = FieldLabel();
    private readonly Label _nameFieldLabel = FieldLabel();
    private readonly Label _descriptionFieldLabel = FieldLabel();
    private readonly Label _onErrorLabel = FieldLabel();
    private readonly Label _paramsLabel = FieldLabel();
    private readonly Label _requestLabel = FieldLabel();
    private readonly Label _responseLabel = FieldLabel();
    private readonly Button _chooseImages = new() { AutoSize = true };
    private readonly Button _uploadImages = new() { AutoSize = true };
    private readonly Button _run = new() { AutoSize = true };
    private readonly ListBox _images = new() { Dock = DockStyle.Fill, IntegralHeight = false };
    private readonly ComboBox _workflows = new() { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill };
    private readonly Button _refreshWorkflows = new() { AutoSize = true };
    private readonly Button _saveWorkflow = new() { AutoSize = true };
    private readonly Button _deleteWorkflow = new() { AutoSize = true };
    private readonly TextBox _workflowName = new() { Dock = DockStyle.Fill };
    private readonly TextBox _workflowDescription = new() { Dock = DockStyle.Fill };
    private readonly ListBox _capabilities = new() { Dock = DockStyle.Fill, IntegralHeight = false };
    private readonly Button _addStep = new() { AutoSize = true };
    private readonly ListBox _steps = new() { Dock = DockStyle.Fill, IntegralHeight = false };
    private readonly Button _moveUp = new() { AutoSize = true };
    private readonly Button _moveDown = new() { AutoSize = true };
    private readonly Button _removeStep = new() { AutoSize = true };
    private readonly CheckBox _enabled = new() { AutoSize = true };
    private readonly ComboBox _onError = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 100 };
    private readonly TextBox _provider = new() { Width = 130 };
    private readonly RichTextBox _stepParams = new() { Dock = DockStyle.Fill, Font = new Font("Consolas", 9) };
    private readonly ProgressBar _progress = new() { Dock = DockStyle.Fill, Style = ProgressBarStyle.Continuous };
    private readonly Label _status = Description();
    private readonly DataGridView _assets = new()
    {
        Dock = DockStyle.Fill,
        AutoGenerateColumns = false,
        AllowUserToAddRows = false,
        AllowUserToDeleteRows = false,
        ReadOnly = true,
        MultiSelect = false,
        SelectionMode = DataGridViewSelectionMode.FullRowSelect
    };
    private readonly BindingList<AssetRow> _assetRows = [];
    private readonly RichTextBox _request = JsonBox();
    private readonly RichTextBox _response = JsonBox();
    private readonly Label _error = Description();
    private readonly List<EditableStep> _editableSteps = [];
    private readonly List<ImageEnhanceWorkflow> _workflowItems = [];
    private readonly List<ImageEnhanceCapabilitySnapshot> _capabilityItems = [];
    private CancellationTokenSource? _operationCancellation;
    private UiLanguage _language = UiLanguage.Chinese;
    private bool _synchronizing;
    private bool _disposing;

    public ImageEnhancementPage(AppComposition composition, Action<string, string> showProtocol)
    {
        _composition = composition;
        _showProtocol = showProtocol;
        _onError.Items.AddRange(["fail", "skip"]);
        _onError.SelectedIndex = 0;
        _images.DisplayMember = nameof(FileEntry.Display);
        _workflows.DisplayMember = nameof(ImageEnhanceWorkflow.DisplayName);
        _capabilities.SelectedIndexChanged += (_, _) => _addStep.Enabled = _capabilities.SelectedIndex >= 0;
        _steps.SelectedIndexChanged += (_, _) => { LoadSelectedStep(); UpdateState(); };
        _enabled.CheckedChanged += (_, _) => SaveSelectedStep();
        _onError.SelectedIndexChanged += (_, _) => SaveSelectedStep();
        _provider.TextChanged += (_, _) => SaveSelectedStep();
        _stepParams.Leave += (_, _) => SaveSelectedStep();
        _workflows.SelectedIndexChanged += (_, _) => ApplySelectedWorkflow();
        _chooseImages.Click += (_, _) => ChooseImages();
        _uploadImages.Click += async (_, _) => await UploadImagesAsync();
        _run.Click += async (_, _) => await RunAsync();
        _refreshWorkflows.Click += async (_, _) => await RefreshWorkflowsAsync();
        _saveWorkflow.Click += async (_, _) => await SaveWorkflowAsync();
        _deleteWorkflow.Click += async (_, _) => await DeleteWorkflowAsync();
        _addStep.Click += (_, _) => AddSelectedCapability();
        _moveUp.Click += (_, _) => MoveStep(-1);
        _moveDown.Click += (_, _) => MoveStep(1);
        _removeStep.Click += (_, _) => RemoveSelectedStep();
        _assets.DataSource = _assetRows;
        _assets.CellDoubleClick += async (_, args) => await PreviewAssetAsync(args.RowIndex);
        BuildLayout();
        ApplyLanguage(_language);
        UpdateState();
    }

    public void ActivatePage()
    {
        if (_disposing) return;
        _ = RefreshCapabilitiesAsync();
        _ = RefreshWorkflowsAsync();
    }

    public void DeactivatePage() => CancelOperation();

    public void ApplyLanguage(UiLanguage language)
    {
        _language = language;
        var english = language == UiLanguage.English;
        _title.Text = english ? "Image Enhancement" : "图像增强";
        _subtitle.Text = english
            ? "Build a structured enhancement workflow, run it on uploaded images, and inspect the asynchronous result."
            : "通过结构化表单创建增强工作流，对上传图片执行增强，并查看异步任务结果。";
        _notice.Text = english
            ? "Raw JSON command execution is disabled here. Request and response JSON are read-only diagnostics."
            : "此页面已禁用原始 JSON 命令执行，请使用结构化表单；请求和响应 JSON 仅用于只读诊断。";
        _chooseImages.Text = english ? "Choose images" : "选择图片";
        _uploadImages.Text = english ? "Upload all" : "上传全部";
        _run.Text = english ? "Run enhancement" : "执行增强";
        _refreshWorkflows.Text = english ? "Refresh" : "刷新";
        _saveWorkflow.Text = english ? "Save workflow" : "保存工作流";
        _deleteWorkflow.Text = english ? "Delete" : "删除";
        _addStep.Text = english ? "Add step" : "添加步骤";
        _moveUp.Text = english ? "Up" : "上移";
        _moveDown.Text = english ? "Down" : "下移";
        _removeStep.Text = english ? "Remove" : "移除";
        _enabled.Text = english ? "Enabled" : "启用";
        _inputHeader.Text = english ? "Input images" : "输入图片";
        _workflowHeader.Text = english ? "Workflow" : "工作流";
        _capabilityHeader.Text = english ? "Capability library" : "能力库";
        _runHeader.Text = english ? "Run" : "执行";
        _stepHeader.Text = english ? "Pipeline steps" : "流程步骤";
        _resultHeader.Text = english ? "Result" : "结果";
        _workflowFieldLabel.Text = english ? "Workflow" : "工作流";
        _nameFieldLabel.Text = english ? "Name" : "名称";
        _descriptionFieldLabel.Text = english ? "Description" : "描述";
        _onErrorLabel.Text = english ? "On error" : "错误处理";
        _paramsLabel.Text = english ? "Parameters JSON" : "参数 JSON";
        _requestLabel.Text = english ? "Request (read-only)" : "请求（只读）";
        _responseLabel.Text = english ? "Response (read-only)" : "响应（只读）";
        RelabelControls(english);
        UpdateState();
    }

    private void BuildLayout()
    {
        var left = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(10), ColumnCount = 1, RowCount = 8 };
        left.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        left.RowStyles.Add(new RowStyle(SizeType.Absolute, 120));
        left.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        left.RowStyles.Add(new RowStyle(SizeType.Absolute, 150));
        left.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        left.RowStyles.Add(new RowStyle(SizeType.Absolute, 210));
        left.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        left.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        left.Controls.Add(Header(_inputHeader, _chooseImages, _uploadImages), 0, 0);
        left.Controls.Add(_images, 0, 1);
        left.Controls.Add(Header(_workflowHeader, _refreshWorkflows, _saveWorkflow, _deleteWorkflow), 0, 2);
        left.Controls.Add(WorkflowEditor(), 0, 3);
        left.Controls.Add(Header(_capabilityHeader, _addStep), 0, 4);
        left.Controls.Add(_capabilities, 0, 5);
        left.Controls.Add(Header(_runHeader, _run), 0, 6);
        left.Controls.Add(_status, 0, 7);

        var stepPanel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3, Padding = new Padding(10) };
        stepPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        stepPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 190));
        stepPanel.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        stepPanel.Controls.Add(Header(_stepHeader, _moveUp, _moveDown, _removeStep), 0, 0);
        stepPanel.Controls.Add(_steps, 0, 1);
        stepPanel.Controls.Add(StepEditor(), 0, 2);

        var resultPanel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 5, Padding = new Padding(10) };
        resultPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        resultPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        resultPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        resultPanel.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        resultPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 210));
        resultPanel.Controls.Add(Header(_resultHeader), 0, 0);
        resultPanel.Controls.Add(_progress, 0, 1);
        resultPanel.Controls.Add(_error, 0, 2);
        resultPanel.Controls.Add(_assets, 0, 3);
        // Keep request and response diagnostics side by side in a bounded area.
        var diagnostics = new SplitContainer { Dock = DockStyle.Fill, Orientation = Orientation.Horizontal, SplitterDistance = 100 };
        diagnostics.Panel1.Controls.Add(DiagnosticPanel(_requestLabel, _request));
        diagnostics.Panel2.Controls.Add(DiagnosticPanel(_responseLabel, _response));
        resultPanel.Controls.Add(diagnostics, 0, 4);

        var content = new SplitContainer { Dock = DockStyle.Fill, SplitterDistance = 360 };
        content.Panel1.Controls.Add(left);
        var right = new SplitContainer { Dock = DockStyle.Fill, Orientation = Orientation.Horizontal, SplitterDistance = 320 };
        right.Panel1.Controls.Add(stepPanel);
        right.Panel2.Controls.Add(resultPanel);
        content.Panel2.Controls.Add(right);

        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 4, Padding = new Padding(8) };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.Controls.Add(_title, 0, 0);
        root.Controls.Add(_subtitle, 0, 1);
        root.Controls.Add(_notice, 0, 2);
        root.Controls.Add(content, 0, 3);
        Controls.Add(root);
    }

    private Control WorkflowEditor()
    {
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 3 };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 35));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 65));
        layout.Controls.Add(_workflowFieldLabel, 0, 0);
        layout.Controls.Add(_workflows, 1, 0);
        layout.Controls.Add(_nameFieldLabel, 0, 1);
        layout.Controls.Add(_workflowName, 1, 1);
        layout.Controls.Add(_descriptionFieldLabel, 0, 2);
        layout.Controls.Add(_workflowDescription, 1, 2);
        return layout;
    }

    private Control StepEditor()
    {
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 4, RowCount = 3 };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        layout.Controls.Add(_enabled, 0, 0);
        layout.Controls.Add(_provider, 1, 0);
        layout.Controls.Add(_onErrorLabel, 2, 0);
        layout.Controls.Add(_onError, 3, 0);
        layout.Controls.Add(_paramsLabel, 0, 1);
        layout.SetColumnSpan(_stepParams, 3);
        layout.Controls.Add(_stepParams, 1, 1);
        layout.SetRowSpan(_stepParams, 2);
        return layout;
    }

    private static Control Header(Label heading, params Control[] controls)
    {
        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, WrapContents = false };
        heading.Padding = new Padding(0, 7, 12, 0);
        panel.Controls.Add(heading);
        panel.Controls.AddRange(controls);
        return panel;
    }

    private static Control DiagnosticPanel(Label title, Control content)
    {
        var panel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2, Padding = new Padding(2) };
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        panel.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        panel.Controls.Add(title, 0, 0);
        panel.Controls.Add(content, 0, 1);
        return panel;
    }

    private void RelabelControls(bool english)
    {
        _status.Text = english ? "Ready" : "就绪";
        _assets.Columns.Clear();
        _assets.Columns.Add(new DataGridViewTextBoxColumn { DataPropertyName = nameof(AssetRow.Id), HeaderText = english ? "Asset" : "资源", AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill });
        _assets.Columns.Add(new DataGridViewTextBoxColumn { DataPropertyName = nameof(AssetRow.ContentType), HeaderText = english ? "Type" : "类型", Width = 130 });
        _assets.Columns.Add(new DataGridViewTextBoxColumn { DataPropertyName = nameof(AssetRow.Path), HeaderText = english ? "Path" : "路径", Width = 220 });
    }

    private void ChooseImages()
    {
        using var dialog = new OpenFileDialog { Filter = "Images|*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff|All files|*.*", Multiselect = true };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        _images.Items.Clear();
        foreach (var path in dialog.FileNames) _images.Items.Add(new FileEntry(path));
        ClearResults();
        UpdateState();
    }

    private async Task RefreshCapabilitiesAsync()
    {
        try
        {
            var capabilities = await _composition.Coordinator.ListImageEnhanceCapabilitiesAsync(CancellationToken.None);
            _capabilityItems.Clear();
            _capabilityItems.AddRange(capabilities);
            _capabilities.DataSource = null;
            _capabilities.DataSource = _capabilityItems;
            _capabilities.DisplayMember = nameof(ImageEnhanceCapabilitySnapshot.Title);
            _addStep.Enabled = _capabilityItems.Count > 0;
        }
        catch (Exception exception)
        {
            ShowError(exception);
        }
    }

    private async Task RefreshWorkflowsAsync()
    {
        try
        {
            var workflows = await _composition.Coordinator.ListImageEnhanceWorkflowsAsync(CancellationToken.None);
            _workflowItems.Clear();
            _workflowItems.AddRange(workflows);
            _workflows.DataSource = null;
            _workflows.DataSource = _workflowItems;
            _workflows.DisplayMember = nameof(ImageEnhanceWorkflow.DisplayName);
        }
        catch (Exception exception)
        {
            ShowError(exception);
        }
    }

    private void ApplySelectedWorkflow()
    {
        if (_synchronizing || _workflows.SelectedItem is not ImageEnhanceWorkflow workflow) return;
        _synchronizing = true;
        try
        {
            _workflowName.Text = workflow.Name;
            _workflowDescription.Text = workflow.Description;
            _editableSteps.Clear();
            if (workflow.Pipeline.TryGetProperty("steps", out var steps) && steps.ValueKind == JsonValueKind.Array)
            {
                foreach (var step in steps.EnumerateArray())
                {
                    _editableSteps.Add(new EditableStep
                    {
                        Id = StringValue(step, "id") ?? Guid.NewGuid().ToString("N")[..8],
                        Type = StringValue(step, "type") ?? string.Empty,
                        Provider = StringValue(step, "provider") ?? "auto",
                        Enabled = BooleanValue(step, "enabled", true),
                        OnError = StringValue(step, "on_error") ?? "fail",
                        ParamsJson = step.TryGetProperty("params", out var parameters) ? parameters.GetRawText() : "{}"
                    });
                }
            }
            RefreshSteps();
        }
        finally
        {
            _synchronizing = false;
        }
    }

    private void AddSelectedCapability()
    {
        if (_capabilities.SelectedItem is not ImageEnhanceCapabilitySnapshot capability) return;
        _editableSteps.Add(new EditableStep { Type = capability.Type, ParamsJson = capability.Defaults.GetRawText() });
        RefreshSteps();
        _steps.SelectedIndex = _steps.Items.Count - 1;
    }

    private void RefreshSteps()
    {
        _steps.DataSource = null;
        _steps.DataSource = _editableSteps;
        if (_editableSteps.Count == 0)
        {
            _stepParams.Text = string.Empty;
            _enabled.Checked = false;
            _provider.Text = string.Empty;
            return;
        }
        _steps.SelectedIndex = Math.Clamp(_steps.SelectedIndex, 0, _editableSteps.Count - 1);
        LoadSelectedStep();
    }

    private void LoadSelectedStep()
    {
        if (_synchronizing || _steps.SelectedItem is not EditableStep step) return;
        _synchronizing = true;
        try
        {
            _enabled.Checked = step.Enabled;
            _provider.Text = step.Provider;
            _onError.SelectedItem = step.OnError;
            _stepParams.Text = step.ParamsJson;
        }
        finally { _synchronizing = false; }
    }

    private void SaveSelectedStep()
    {
        if (_synchronizing || _steps.SelectedItem is not EditableStep step) return;
        step.Enabled = _enabled.Checked;
        step.Provider = string.IsNullOrWhiteSpace(_provider.Text) ? "auto" : _provider.Text.Trim();
        step.OnError = _onError.SelectedItem?.ToString() ?? "fail";
        step.ParamsJson = string.IsNullOrWhiteSpace(_stepParams.Text) ? "{}" : _stepParams.Text;
        _steps.Refresh();
    }

    private void MoveStep(int offset)
    {
        SaveSelectedStep();
        var index = _steps.SelectedIndex;
        var next = index + offset;
        if (index < 0 || next < 0 || next >= _editableSteps.Count) return;
        (_editableSteps[index], _editableSteps[next]) = (_editableSteps[next], _editableSteps[index]);
        RefreshSteps();
        _steps.SelectedIndex = next;
    }

    private void RemoveSelectedStep()
    {
        var index = _steps.SelectedIndex;
        if (index < 0) return;
        _editableSteps.RemoveAt(index);
        RefreshSteps();
    }

    private async Task UploadImagesAsync()
    {
        try
        {
            foreach (var item in SelectedFiles())
            {
                if (item.UploadId.Length == 0) item.UploadId = await _composition.UploadImageAsync(item.Path, CancellationToken.None);
            }
            _images.Refresh();
            _status.Text = _language == UiLanguage.English ? "All images uploaded." : "图片已全部上传。";
            UpdateState();
        }
        catch (Exception exception) { ShowError(exception); }
    }

    private async Task SaveWorkflowAsync()
    {
        try
        {
            SaveSelectedStep();
            var pipeline = BuildPipeline();
            await _composition.Coordinator.SaveImageEnhanceWorkflowAsync(_workflowName.Text, _workflowDescription.Text, pipeline, CancellationToken.None);
            await RefreshWorkflowsAsync();
            _status.Text = _language == UiLanguage.English ? "Workflow saved." : "工作流已保存。";
        }
        catch (Exception exception) { ShowError(exception); }
    }

    private async Task DeleteWorkflowAsync()
    {
        if (_workflows.SelectedItem is not ImageEnhanceWorkflow workflow || string.IsNullOrWhiteSpace(workflow.WorkflowId)) return;
        try
        {
            await _composition.Coordinator.DeleteImageEnhanceWorkflowAsync(workflow.WorkflowId, CancellationToken.None);
            await RefreshWorkflowsAsync();
            _status.Text = _language == UiLanguage.English ? "Workflow deleted." : "工作流已删除。";
        }
        catch (Exception exception) { ShowError(exception); }
    }

    private async Task RunAsync()
    {
        try
        {
            SaveSelectedStep();
            var files = SelectedFiles();
            if (files.Count == 0) throw new InvalidOperationException(_language == UiLanguage.English ? "Choose at least one image." : "请至少选择一张图片。" );
            if (_editableSteps.Count == 0) throw new InvalidOperationException(_language == UiLanguage.English ? "Add at least one enhancement step." : "请至少添加一个增强步骤。" );
            foreach (var file in files)
            {
                if (file.UploadId.Length == 0) file.UploadId = await _composition.UploadImageAsync(file.Path, CancellationToken.None);
            }
            _images.Refresh();
            var pipeline = BuildPipeline();
            var parameters = ImageEnhancementRequestBuilder.BuildEnhanceParameters(files.Select(file => file.UploadId), pipeline);
            _request.Text = JsonSerializer.Serialize(new { method = "image.enhance", @params = parameters }, new JsonSerializerOptions { WriteIndented = true });
            _progress.Value = 0;
            _error.Text = string.Empty;
            var response = await _composition.Coordinator.ExecuteImageEnhancementAsync(files.Select(file => file.UploadId).ToArray(), pipeline, CancellationToken.None);
            _response.Text = JsonSerializer.Serialize(response, new JsonSerializerOptions { WriteIndented = true });
            _showProtocol(_request.Text, _response.Text);
            var task = ImageEnhancementRequestBuilder.ParseTask(response.Data);
            var taskId = task.TaskId.Length > 0 ? task.TaskId : StringValue(response.Data, "task_id");
            if (string.IsNullOrWhiteSpace(taskId)) throw new InvalidOperationException("image.enhance did not return task_id.");
            await PollTaskAsync(taskId);
        }
        catch (Exception exception) { ShowError(exception); }
    }

    private async Task PollTaskAsync(string taskId)
    {
        CancelOperation();
        _operationCancellation = new CancellationTokenSource();
        var token = _operationCancellation.Token;
        for (var attempt = 0; attempt < 80; attempt++)
        {
            await Task.Delay(700, token);
            var task = await _composition.Coordinator.RefreshImageEnhancementTaskAsync(taskId, token);
            _progress.Value = Math.Clamp(task.Progress, 0, 100);
            _status.Text = $"{task.Status} ({task.Progress}%)";
            if (task.Status is "completed" or "failed" or "cancelled")
            {
                _assetRows.Clear();
                foreach (var asset in task.Assets) _assetRows.Add(new AssetRow { Asset = asset });
                if (task.Status != "completed") throw new InvalidOperationException(task.Error.Length > 0 ? task.Error : task.Status);
                return;
            }
        }
        throw new TimeoutException(_language == UiLanguage.English ? "Enhancement task timed out." : "图像增强任务查询超时。" );
    }

    private async Task PreviewAssetAsync(int rowIndex)
    {
        if (rowIndex < 0 || rowIndex >= _assetRows.Count) return;
        var asset = _assetRows[rowIndex].Asset;
        if (string.IsNullOrWhiteSpace(asset.Url)) return;
        try
        {
            var bytes = await _composition.DownloadAssetAsync(asset.Url, CancellationToken.None);
            using var stream = new MemoryStream(bytes);
            using var image = Image.FromStream(stream);
            using var dialog = new Form { Text = asset.AssetId, Size = new Size(900, 700), StartPosition = FormStartPosition.CenterParent };
            var picture = new PictureBox { Dock = DockStyle.Fill, SizeMode = PictureBoxSizeMode.Zoom, Image = new Bitmap(image) };
            dialog.Controls.Add(picture);
            dialog.ShowDialog(this);
            picture.Image?.Dispose();
        }
        catch (Exception exception) { ShowError(exception); }
    }

    private JsonElement BuildPipeline()
    {
        var steps = _editableSteps.Select(step =>
        {
            using var parameters = JsonDocument.Parse(string.IsNullOrWhiteSpace(step.ParamsJson) ? "{}" : step.ParamsJson);
            if (parameters.RootElement.ValueKind != JsonValueKind.Object) throw new JsonException("Step parameters must be a JSON object.");
            return new
            {
                id = step.Id,
                type = step.Type,
                provider = step.Provider,
                enabled = step.Enabled,
                on_error = step.OnError,
                @params = parameters.RootElement.Clone()
            };
        }).ToArray();
        return JsonSerializer.SerializeToElement(new
        {
            version = "image.enhance.pipeline.v1",
            steps,
            target = new { type = "images", format = "jpg", export_type = "single-page" },
            options = new { keep_intermediate = false, include_metadata = true }
        });
    }

    private List<FileEntry> SelectedFiles() => _images.Items.Cast<FileEntry>().ToList();

    private void UpdateState()
    {
        var hasFiles = _images.Items.Count > 0;
        _uploadImages.Enabled = hasFiles;
        _run.Enabled = hasFiles && _editableSteps.Count > 0;
        _deleteWorkflow.Enabled = _workflows.SelectedItem is ImageEnhanceWorkflow;
        _moveUp.Enabled = _steps.SelectedIndex > 0;
        _moveDown.Enabled = _steps.SelectedIndex >= 0 && _steps.SelectedIndex < _editableSteps.Count - 1;
        _removeStep.Enabled = _steps.SelectedIndex >= 0;
    }

    private void ClearResults()
    {
        _assetRows.Clear();
        _progress.Value = 0;
        _request.Text = "{}";
        _response.Text = "{}";
    }

    private void ShowError(Exception exception)
    {
        _error.Text = exception.Message;
        _status.Text = _language == UiLanguage.English ? "Failed" : "失败";
    }

    private void CancelOperation()
    {
        _operationCancellation?.Cancel();
        _operationCancellation?.Dispose();
        _operationCancellation = null;
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _disposing = true;
            CancelOperation();
        }
        base.Dispose(disposing);
    }

    private sealed class FileEntry(string path)
    {
        public string Path { get; } = path;
        public string UploadId { get; set; } = string.Empty;
        public string Display => string.IsNullOrWhiteSpace(UploadId) ? System.IO.Path.GetFileName(Path) : $"{System.IO.Path.GetFileName(Path)} [{UploadId}]";
    }

    private static string? StringValue(JsonElement item, string property) => item.TryGetProperty(property, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() : null;
    private static bool BooleanValue(JsonElement item, string property, bool fallback) => item.TryGetProperty(property, out var value) && value.ValueKind is JsonValueKind.True or JsonValueKind.False ? value.GetBoolean() : fallback;
    private static Label Heading() => new() { AutoSize = true, Font = new Font(SystemFonts.DefaultFont, FontStyle.Bold) };
    private static Label SectionHeading() => new() { AutoSize = true, Font = new Font(SystemFonts.DefaultFont, FontStyle.Bold) };
    private static Label FieldLabel() => new() { AutoSize = true, Padding = new Padding(4, 6, 0, 0) };
    private static Label Description() => new() { AutoSize = true, ForeColor = Color.DimGray };
    private static RichTextBox JsonBox() => new() { Dock = DockStyle.Fill, ReadOnly = true, Font = new Font("Consolas", 8), WordWrap = false, ScrollBars = RichTextBoxScrollBars.Both };
}
