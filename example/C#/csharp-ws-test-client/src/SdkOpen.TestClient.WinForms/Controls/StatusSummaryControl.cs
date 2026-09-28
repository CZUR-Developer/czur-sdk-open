namespace SdkOpen.TestClient.WinForms;

public sealed class StatusSummaryControl : ToolStripStatusLabel
{
    public void SetStatus(string message, bool connected)
    {
        Text = message;
        ForeColor = connected ? Color.DarkGreen : Color.DarkRed;
    }
}
