namespace SdkOpen.TestClient.Core.Application;

public sealed class SessionState
{
    private readonly HashSet<string> _capabilities = new(StringComparer.OrdinalIgnoreCase);

    // session_token 只在当前进程内存中存在，断开连接或应用退出时都会清除。
    // Keep session_token in process memory only; clear it on disconnect or application exit.
    public string? SessionToken { get; private set; }
    public IReadOnlyCollection<string> Capabilities => _capabilities;
    public bool IsAuthenticated => !string.IsNullOrEmpty(SessionToken);

    public void SetSession(string sessionToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(sessionToken);
        SessionToken = sessionToken;
    }

    public void SetCapabilities(IEnumerable<string> capabilities)
    {
        // 服务端能力列表决定页面是否显示/启用对应操作，不在客户端硬编码权限。
        // The server capability list controls available UI actions instead of hard-coded client permissions.
        _capabilities.Clear();
        foreach (var capability in capabilities.Where(capability => !string.IsNullOrWhiteSpace(capability)))
        {
            _capabilities.Add(capability);
        }
    }

    public void Clear()
    {
        SessionToken = null;
        _capabilities.Clear();
    }
}
