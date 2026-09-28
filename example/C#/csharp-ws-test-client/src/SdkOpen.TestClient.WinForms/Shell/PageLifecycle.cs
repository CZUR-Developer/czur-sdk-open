namespace SdkOpen.TestClient.WinForms;

/// <summary>
/// Receives explicit navigation lifecycle notifications from the shell.
/// 由主窗口显式通知页面进入和离开，避免依赖 WinForms VisibleChanged 的隐式行为。
/// </summary>
public interface IPageLifecycle
{
    void ActivatePage();

    void DeactivatePage();
}

/// <summary>
/// Limits automatic session startup to one request per page activation.
/// 每次进入页面只允许一次自动会话启动，工作流状态刷新不会重新授予启动机会。
/// </summary>
public sealed class PageSessionActivation
{
    private bool _isActive;
    private bool _startRequested;

    public bool IsActive => _isActive;

    public void Activate()
    {
        _isActive = true;
        _startRequested = false;
    }

    public void Deactivate()
    {
        _isActive = false;
        _startRequested = false;
    }

    public bool TryRequestStart(bool sessionExists)
    {
        if (!_isActive || sessionExists || _startRequested)
        {
            return false;
        }

        _startRequested = true;
        return true;
    }
}
