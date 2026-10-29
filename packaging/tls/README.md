# SDK Open 本机 TLS 默认凭据

此目录维护 SDK Open 本机 runtime 的默认 TLS 凭据，默认服务名为
`sdk-runtime.localhost`。

- `authority/` 保存本地根 CA 的证书和签发私钥；它用于维护后续的默认服务
  证书，**不应进入任何平台安装包**。
- `runtime/` 保存构建前置检查和旧版本迁移所需的兼容素材。新的 Windows、macOS
  和 Linux 安装器会调用 `sdk_open_app --provision-tls` 在客户机器上生成根 CA
  私钥和叶证书；根私钥、服务端私钥和固定叶证书都不会进入安装包，生成的根证书
  再由平台安装器加入系统信任库。

默认服务端证书只包含 `DNS:sdk-runtime.localhost`。安装器会将此名称映射到
`127.0.0.1`，所以它只适用于 SDK 与调用方同机的本机 runtime 场景。

默认根 CA 有效期至 2036-07-24，其 SHA-256 指纹为：

```text
52:34:9D:09:1C:7C:51:EA:B3:63:A7:68:8E:3F:AA:F5:86:CF:1C:67:A9:3F:9F:C7:7D:E7:14:DA:0D:A7:93:CD
```

旧版默认服务端证书签发期限为 825 天，有效期至 2029-01-15。
原来有效期至 2031-07-26 的五年证书会被 macOS / Safari 的原生 SSL 策略以
`OtherTrustValidityPeriod` 拒绝，即使根 CA 已安装并受信任；仅用 OpenSSL 或
`curl --cacert` 校验无法发现此限制。发布维护时应在到期前重新签发并更新默认
服务端证书，macOS 打包脚本会执行原生策略校验。新安装生成的叶证书有效期为
397 天，并在剩余 90 天时由运行时自动轮换。
`runtime/sdk-runtime.localhost.fullchain.pem`
在默认私有 CA 没有中间证书时仅含服务端证书；根证书由安装器单独写入系统信任库。

客户如需使用自己的证书，可以在安装后替换 `fullchain.pem` 和 `privkey.pem`，
重启 `sdk-open` 服务即可；如替换证书使用了新的私有 CA，还需要自行将该 CA
安装到调用方的信任库。检测到客户证书后，SDK 不会覆盖它们，也不会自动签发
新的叶证书。

安装验证脚本校验本机生成的根 CA、叶证书和私钥均存在且权限正确。根 CA 私钥及
默认服务端私钥均为本机 runtime 的产品凭据，不得用于互联网或跨客户部署。自动轮换状态文件为
`tls-state.json`，根私钥为 `local-runtime-root-ca.key.pem`，两者都必须限制为
SDK Open 运行账户可访问。
