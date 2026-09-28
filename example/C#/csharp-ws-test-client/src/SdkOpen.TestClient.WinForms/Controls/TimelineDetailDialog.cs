using SdkOpen.TestClient.Core.Diagnostics;

namespace SdkOpen.TestClient.WinForms;

public sealed class TimelineDetailDialog : Form
{
    private readonly TimelineEntry _entry;
    private readonly UiLanguage _language;
    private readonly Label _timestamp = new() { AutoSize = true };
    private readonly Label _category = new() { AutoSize = true };
    private readonly Label _summary = new() { AutoSize = true, MaximumSize = new Size(900, 0) };
    private readonly RichTextBox _payload = new()
    {
        Dock = DockStyle.Fill,
        ReadOnly = true,
        DetectUrls = false,
        Font = new Font("Consolas", 10),
        WordWrap = false,
        ScrollBars = RichTextBoxScrollBars.Both
    };
    private readonly Button _copy = new() { AutoSize = true };
    private readonly Button _close = new() { AutoSize = true };

    public TimelineDetailDialog(TimelineEntry entry, UiLanguage language)
    {
        _entry = entry ?? throw new ArgumentNullException(nameof(entry));
        _language = language;
        Text = language == UiLanguage.English ? "Timeline Detail" : "时间线详情";
        StartPosition = FormStartPosition.CenterParent;
        MinimumSize = new Size(760, 520);
        Size = new Size(1100, 720);
        BuildLayout();
    }

    private void BuildLayout()
    {
        var english = _language == UiLanguage.English;
        var metadata = new TableLayoutPanel
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            ColumnCount = 2,
            Padding = new Padding(10)
        };
        metadata.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        metadata.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        _timestamp.Text = $"{(english ? "Time" : "时间")}: {_entry.LocalTimestampDisplay}";
        _category.Text = $"{(english ? "Category" : "类型")}: {_entry.Category}";
        _summary.Text = $"{(english ? "Summary" : "摘要")}: {_entry.Summary}";
        metadata.Controls.Add(_timestamp, 0, 0);
        metadata.Controls.Add(_category, 0, 1);
        metadata.Controls.Add(_summary, 0, 2);
        metadata.SetColumnSpan(_summary, 2);

        var payloadTitle = new Label
        {
            Text = english ? "Payload (redacted, complete):" : "Payload（已脱敏，完整内容）：",
            Dock = DockStyle.Top,
            AutoSize = true,
            Padding = new Padding(10, 0, 0, 4)
        };
        _payload.Text = TimelineDetailFormatter.FormatPayload(_entry);
        var copyAndClose = new FlowLayoutPanel { Dock = DockStyle.Bottom, AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Padding = new Padding(8) };
        _copy.Text = english ? "Copy Payload" : "复制 Payload";
        _close.Text = english ? "Close" : "关闭";
        _copy.Click += (_, _) => Clipboard.SetText(_payload.Text);
        _close.Click += (_, _) => Close();
        copyAndClose.Controls.AddRange([_close, _copy]);

        Controls.Add(_payload);
        Controls.Add(payloadTitle);
        Controls.Add(copyAndClose);
        Controls.Add(metadata);
    }
}
