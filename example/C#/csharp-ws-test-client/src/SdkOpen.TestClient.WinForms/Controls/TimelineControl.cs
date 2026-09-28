using SdkOpen.TestClient.Core.Diagnostics;

namespace SdkOpen.TestClient.WinForms;

public sealed class TimelineControl : UserControl, ILocalizableControl
{
    private readonly DataGridView _grid = new()
    {
        Dock = DockStyle.Fill,
        ReadOnly = true,
        AllowUserToAddRows = false,
        AutoGenerateColumns = false,
        SelectionMode = DataGridViewSelectionMode.FullRowSelect
    };
    private UiLanguage _language = UiLanguage.Chinese;

    /// <summary>
    /// Raised when the user selects a timeline row.
    /// 用户主动选中时间线条目时触发，供右侧协议检查器同步显示。
    /// </summary>
    public event EventHandler<TimelineEntry>? EntrySelected;

    public TimelineControl()
    {
        _grid.Columns.Add(new DataGridViewTextBoxColumn { DataPropertyName = nameof(TimelineEntry.LocalTimestampDisplay), HeaderText = "时间", Width = 160 });
        _grid.Columns.Add(new DataGridViewTextBoxColumn { DataPropertyName = nameof(TimelineEntry.Category), HeaderText = "类型", Width = 80 });
        _grid.Columns.Add(new DataGridViewTextBoxColumn { DataPropertyName = nameof(TimelineEntry.Summary), HeaderText = "摘要", AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill });
        _grid.CellClick += (_, args) =>
        {
            if (args.RowIndex >= 0 &&
                _grid.Rows[args.RowIndex].DataBoundItem is TimelineEntry entry)
            {
                EntrySelected?.Invoke(this, entry);
            }
        };
        // 双击或按 Enter 打开完整详情，表格摘要只保留便于快速浏览的短文本。
        // Double-click or press Enter to open the full detail; the grid keeps only a compact summary.
        _grid.CellDoubleClick += (_, args) => OpenSelectedDetail(args.RowIndex);
        _grid.KeyDown += (_, args) =>
        {
            if (args.KeyCode == Keys.Enter)
            {
                args.Handled = true;
                args.SuppressKeyPress = true;
                OpenSelectedDetail(_grid.CurrentCell?.RowIndex ?? -1);
            }
        };
        Controls.Add(_grid);
    }

    public void ApplyLanguage(UiLanguage language)
    {
        _language = language;
        var english = language == UiLanguage.English;
        _grid.Columns[0].HeaderText = english ? "Time" : "时间";
        _grid.Columns[1].HeaderText = english ? "Category" : "类型";
        _grid.Columns[2].HeaderText = english ? "Summary" : "摘要";
    }

    public void SetEntries(IEnumerable<TimelineEntry> entries) => _grid.DataSource = entries.Reverse().ToList();

    private void OpenSelectedDetail(int rowIndex)
    {
        if (rowIndex < 0 || rowIndex >= _grid.Rows.Count || _grid.Rows[rowIndex].DataBoundItem is not TimelineEntry entry)
        {
            return;
        }

        using var dialog = new TimelineDetailDialog(entry, _language);
        var owner = FindForm();
        if (owner is null) dialog.ShowDialog();
        else dialog.ShowDialog(owner);
    }
}
