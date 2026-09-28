using System.Security.Cryptography;
using System.Text;

namespace SdkOpen.TestClient.Core.Configuration;

public sealed class DpapiSecretStore
{
    private readonly string _directory;

    public DpapiSecretStore(string? applicationDirectory = null)
    {
        var root = applicationDirectory ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "CZUR",
            "SdkOpenTestClient");
        _directory = Path.Combine(root, "secrets");
    }

    public async Task SaveAsync(string alias, string secret, CancellationToken cancellationToken)
    {
        if (!OperatingSystem.IsWindows())
        {
            throw new PlatformNotSupportedException("DPAPI secret storage requires Windows.");
        }
        ArgumentException.ThrowIfNullOrWhiteSpace(secret);
        var path = GetSecretPath(alias);
        Directory.CreateDirectory(_directory);
        // 使用 CurrentUser 作用域，只有当前 Windows 用户能解密 API Token；文件本身不保存明文。
        // CurrentUser scope limits decryption to the Windows user; the file never contains plaintext.
        var encrypted = ProtectedData.Protect(Encoding.UTF8.GetBytes(secret), null, DataProtectionScope.CurrentUser);
        await File.WriteAllBytesAsync(path, encrypted, cancellationToken).ConfigureAwait(false);
    }

    public async Task<string?> LoadAsync(string alias, CancellationToken cancellationToken)
    {
        if (!OperatingSystem.IsWindows())
        {
            throw new PlatformNotSupportedException("DPAPI secret storage requires Windows.");
        }
        var path = GetSecretPath(alias);
        if (!File.Exists(path))
        {
            return null;
        }

        var encrypted = await File.ReadAllBytesAsync(path, cancellationToken).ConfigureAwait(false);
        // 解密失败应直接向设置页暴露错误，不要把不可验证的密文当作 Token 继续连接。
        // Surface decryption failures to the settings page instead of connecting with unverifiable data.
        var plaintext = ProtectedData.Unprotect(encrypted, null, DataProtectionScope.CurrentUser);
        return Encoding.UTF8.GetString(plaintext);
    }

    public Task DeleteAsync(string alias, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var path = GetSecretPath(alias);
        if (File.Exists(path))
        {
            File.Delete(path);
        }

        return Task.CompletedTask;
    }

    private string GetSecretPath(string alias)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(alias);
        if (!string.Equals(Path.GetFileName(alias), alias, StringComparison.Ordinal) || alias.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            throw new ArgumentException("Secret alias must be a file-name-safe value.", nameof(alias));
        }

        return Path.Combine(_directory, $"{alias}.bin");
    }
}
