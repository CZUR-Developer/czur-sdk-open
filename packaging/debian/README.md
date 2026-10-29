# SDK Open Debian TLS 安装说明

Debian 安装脚本在 `/etc/czur/sdk-open/tls` 维护 SDK Open 本机 TLS 凭据。新安装
调用已安装的 `sdk_open_app --provision-tls`，在客户机器生成独立根 CA、根私钥、
`sdk-runtime.localhost` 叶证书和自动轮换状态。安装包不携带可跨机器复用的私钥。

升级时仅迁移仓库已知的旧版固定凭据：五年或 825 天叶证书、对应旧私钥，以及
可选的旧固定根证书都必须通过 SHA-256 精确匹配，同时不得存在根私钥或
`tls-state.json`。匹配后脚本删除旧默认文件并重新签发本机凭据；签发失败会恢复
原文件。未知根证书、客户证书对和已有托管凭据不会被重置。证书、私钥或根 CA
只存在一半时，安装会明确失败并保留现场，避免静默生成新的 CA 破坏既有信任。
`custom` 状态缺少整个叶证书对时也会失败，防止安装过程把客户模式静默转换成
`managed` 并签发新叶证书。

只有 `tls-state.json` 明确标记为 `managed` 且根证书与根私钥完整时，安装脚本才将
根证书复制到 `/usr/local/share/ca-certificates/local-runtime-root-ca.crt` 并刷新
系统 CA；依赖包提供的 `update-ca-certificates` 不可用时安装会明确失败，并且不会
启动服务。`custom` 模式不会自动信任 SDK 生成的根；客户自有 CA 需要由客户按其
安全策略安装。进入 `custom` 模式时，如果系统 CA 目录中仍是 SHA-256 精确匹配的
旧固定根证书，安装脚本会先删除并刷新系统信任，再启动服务；刷新失败会恢复 CA
源文件并中止安装。卸载时会清理这张已知旧根，即使 TLS 配置已经改成 custom 或
配置目录已经不存在；其他 CA 只在内容仍与 SDK 托管根证书一致时删除，防止误删
客户后来替换的同名信任文件。

可在 macOS 或 Linux 上运行不修改系统目录的回归测试：

```bash
bash src/sdk_open/packaging/debian/test_runtime_tls.sh
```

如本机已有可执行的 SDK Open runtime，可再验证真实签发分支：

```bash
SDK_OPEN_TEST_RUNTIME_BINARY=/path/to/sdk_open_app \
  bash src/sdk_open/packaging/debian/test_runtime_tls.sh
```

测试覆盖新安装、重复安装、两个旧叶证书版本、仅残留旧根证书、客户证书、
managed/custom 状态、叶证书或根证书不完整、未知根证书、迁移失败回滚，以及卸载
时的精确 CA 清理。测试通过临时目录和 mock 的 `systemctl`、
`update-ca-certificates` 执行，不会修改 `/etc/hosts`、系统 CA 或 systemd。
