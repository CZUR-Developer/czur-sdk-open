# SDK Open macOS packaging

The macOS distribution is a signed, notarized PKG. It contains two component
packages:

- `com.czur.sdk-open.runtime`: user-session SDK Open runtime and LaunchAgent.
- `com.czur.device-helper`: the shared root LaunchDaemon used by both SDK Open
  and CapEditor.

The runtime is installed at `/Library/CZUR/sdk-open` and runs as the logged-in
user through `/Library/LaunchAgents/com.czur.sdk-open.plist`. The helper keeps
the fixed Mach service and executable identity `com.czur.device-helper` at
`/Library/PrivilegedHelperTools/com.czur.device-helper`.

Before starting the LaunchAgent, the runtime package ensures that
`tls/local-runtime-root-ca.crt` is present in the system keychain and trusted for SSL, and ensures that
`sdk-runtime.localhost` resolves to `127.0.0.1`. The hosts entry is marked as
managed by CZUR SDK Open and repeated installations do not duplicate it. An
existing active mapping for that host, including a customer-provided address,
is left unchanged. Repeated installations skip importing the same root CA only
when its SHA-256 fingerprint matches and native SSL trust evaluation succeeds.
A certificate that is present but not trusted is repaired. Before starting the
runtime, installation also checks the server certificate against the native
SSL policy for `sdk-runtime.localhost`. A first installation may ask macOS for
administrator authorization; if the trust change cannot be authorized
non-interactively, or does not complete within 30 seconds, installation fails
with instructions to provision the CA through Keychain Access, MDM, or a
configuration profile.
Certificate or hosts update failures fail the installation before SDK Open is
started.

The Jenkins smoke test checks the packaged root CA fingerprint before
installation, then validates the server chain, hostname, and live TLS endpoint
through a real request to `https://sdk-runtime.localhost:18082/healthz` with
that same packaged CA. It also checks the live HTTPS endpoint with native
`security verify-cert` so that Safari certificate policy failures are caught.
The check uses `/usr/bin/curl`, disables user curl configuration, removes CA
bundle environment overrides, and explicitly supplies the CA extracted from
the current PKG. The same CA is passed to SmartEye's Node processes through
`NODE_EXTRA_CA_CERTS`. System keychain trust remains a Mac host provisioning
requirement for native clients and browsers, but is not inferred from curl:
the macOS 12 system curl also uses a CA file instead of the System keychain.

When launched by this LaunchAgent with TLS enabled, an empty asset base URL
defaults to `https://sdk-runtime.localhost:<HTTPS port>`. An explicit asset URL
from the runtime configuration file or environment remains authoritative, as
does an explicitly configured HTTPS port. Direct launches and non-TLS runtime
configurations keep their existing behavior. Verify these rules with:

```bash
bash src/sdk_open/scripts/test_mac_runtime_config.sh
```

The same `sdk-runtime.localhost` certificate covers the default HTTPS asset
endpoint on port `18082` and the WSS command/video endpoints on ports `18090`
and `18091`. The demo site remains on HTTP and may continue using the existing
`ws://` channels; installing TLS does not force the demo frontend to switch to
WSS.

After installing the rebuilt PKG, verify the system keychain trust and the
HTTPS/WSS endpoints with:

```bash
python3 src/sdk_open/packaging/macos/verify-runtime-tls.py
```

This checks HTTPS health, command WSS upgrade plus `system.ping`, and video WSS
rejection of unauthenticated requests. To also check the authenticated video
upgrade and `video.ready`, provide an existing session and stream through
`SDK_VERIFY_SESSION_TOKEN` and `SDK_VERIFY_STREAM_ID`. The script does not
start a camera or verify image frames. `--ca-file` is for diagnosing transport
with an explicit CA; it does not prove that installation configured native
macOS trust. Run the isolated installer regression checks with
`bash src/sdk_open/packaging/macos/test_runtime_tls.sh`.

The runtime package does not ship a reusable server private key. During
postinstall the signed runtime generates a per-install root CA and leaf
certificate, installs the root in the System keychain, and copies the managed
pair into the logged-in user's runtime directory. The leaf is issued for 397
days and is automatically renewed when fewer than 90 days remain. A five-year
server certificate under a private CA fails macOS SSL policy with
`OtherTrustValidityPeriod` / `CSSMERR_TP_CERT_SUSPENDED`, even if the root CA
is trusted. The packaging preflight checks the legacy seed certificate, and the
installed runtime certificate is checked again after provisioning.
On LaunchAgent startup, legacy default material is migrated only when the
certificate matches CZUR's packaged public legacy marker; the old private key is
never shipped or copied forward. Customer replacements are preserved. The
packaged `tls/legacy-sdk-runtime.localhost.pem` is a public migration marker,
not a server credential or an additional trusted certificate.

Upgrades also migrate legacy credentials in `/Library/CZUR/sdk-open/tls`
before provisioning. Old packages installed a shared public root and leaf pair
without a root private key or `tls-state.json`; passing that directory directly
to `--provision-tls` fails with an incomplete-root error. The installer recognizes
the exact SHA-256 hashes of the two previously shipped leaf versions, their
shared key, and the public root. It removes those defaults before generating
machine-local credentials. Managed installations keep their CA, and customer
leaf replacements are preserved as custom credentials.

If trust import fails with `SecTrustSettingsSetTrustSettings: The authorization
was denied since no user interaction was possible`, the CA may already exist
in the System keychain without SSL trust. On the affected Mac, open that
keychain in Keychain Access, identify the generated CA by the SHA-256
fingerprint of `/Library/CZUR/sdk-open/tls/local-runtime-root-ca.crt`, and set
its SSL trust through the macOS authorization UI. Retry installation afterwards;
it will keep the generated CA and skip importing it once native SSL verification
succeeds. Administrator elevation alone may not grant the separate trust-settings
authorization. Managed deployments should provision trust through MDM. Do not
relax `com.apple.trust-settings.admin` authorization rules to make installation
succeed.

The LaunchAgent executes the signed `sdk_open_app` Mach-O directly. Runtime
directory creation, TLS bootstrap, environment defaults, and working-directory
setup happen inside that executable before logging starts. Do not insert a
shell script in `ProgramArguments`: macOS Background Task Management attributes
the login item to that unsigned script and displays it as coming from an
unidentified developer instead of CZUR SDK Open and its Developer ID publisher.

The helper version in `packaging/mac/daemon_version.ini` is the minimum version
required by this SDK build. SDK Open checks it during provider startup. An
installed helper with the same or a higher numeric version is reused because
the helper protocol is backward-compatible. The helper component package also
preserves a same/newer shared helper only when its code signature is valid,
while a missing, invalidly signed, malformed, or older helper is stopped and
replaced by the PKG payload. This prevents installing an older SDK Open package
from downgrading a valid helper while still allowing repair of a damaged
same-version installation.

Build the two architecture-specific packages with:

```bash
src/sdk_open/packaging/macos/build-sdk-open-pkg.sh --arch all
```

Each architecture produces a PKG and a matching symbol archive in
`output/sdk-open/macos/` (or `--output-dir`):

```text
sdk-open_macos_<arch>_v<version>.<build>.<stage>.pkg
sdk-open_macos_<arch>_v<version>.<build>.<stage>.dSYM.zip
sdk-open_macos_<arch>_v<version>.<build>.<stage>.symbols.json
SHA256SUMS.txt
```

Like the normal macOS application build, Release builds retain `-O3 -DNDEBUG`
and add `-g` for C, C++, Objective-C, and Objective-C++. Both Release and Debug
packaging run `dsymutil` for `sdk_open_app`, `com.czur.device-helper`,
`libsdk_private_providers.dylib`, and `libsdk_private_auth_c_api.dylib`, verify
that debug information is present and UUIDs match the binaries, and archive
the bundles from `build/sdk-open-macos-<arch>-<build-type>/dSYMs/`. The symbols
remain outside the installed PKG payload; `SHA256SUMS.txt` covers both PKGs
and symbol archives. The per-architecture `.symbols.json` records the package
and symbol hashes, source commits, and each Mach-O UUID for crash-symbol lookup.

`--skip-build` also exports symbols and requires the original build objects.
For older Release outputs built without debug information, run the packaging
script once without `--skip-build` to rebuild with `-g`.

The signing defaults follow `packaging/mac/build.sh`: Team
`C2UTRKBSL6`, `Developer ID Application: Dalian CZUR TECH CO.,LTD
(C2UTRKBSL6)`, `Developer ID Installer: Dalian CZUR TECH CO.,LTD
(C2UTRKBSL6)`, and notary profile `CZUR_DMG_PWD`. The SDK script accepts the
same `CZUR_MAC_CODESIGN_IDENTITY`, `CZUR_MAC_DEBUG_CODE_SIGN_IDENTITY`,
`CZUR_MAC_DEVELOPMENT_TEAM`, and `CZUR_MAC_NOTARY_PROFILE` overrides, plus
`CZUR_MAC_INSTALLER_SIGN_IDENTITY` when the Installer certificate name needs
to be overridden.

Unlike the application's DMG, Apple's PKG format additionally requires a
`Developer ID Installer` certificate. Set its exact keychain identity through
`CZUR_MAC_INSTALLER_SIGN_IDENTITY`; a `Developer ID Application` certificate
cannot be passed to `productsign`. For local validation without that
certificate, keep Mach-O signing enabled and only skip the outer PKG signature:

```bash
src/sdk_open/packaging/macos/build-sdk-open-pkg.sh \
  --arch arm64 --build-type Release --skip-web-build \
  --skip-pkg-sign --skip-notary
```

The repository must contain OpenSSL 3.0.16 headers under
`third_party_repo/openssl3/include/mac` and architecture-specific
`libssl.3.dylib`/`libcrypto.3.dylib` files under `lib/mac-arm64` and
`lib/mac-x86-64`. Configuration fails early when any of these release inputs is
missing. Build both sets directly from the repository source archive before
packaging with:

```bash
third_party_repo/src/all_build_bat/openssl-OpenSSL_3_0_16/build_mac.sh all
```

SDK Open being resident, enumerating devices, and connecting to XPC do not own
a camera. The shared helper grants one process connection a lease only while a
device is opening/open. Both AVFoundation and helper/libuvc opens use the same
lease, so a second application can open a device after the first application
closes its current device, but never concurrently.

The PKG does not request Camera privacy access during installation. macOS only
allows the logged-in process that will use the camera to trigger that prompt,
so SDK Open requests access on the first AVFoundation `video.start`. Device
enumeration and resolution queries do not prompt. The packaged `sdk_open_app`
is signed with `com.apple.security.device.camera` and embeds an Info.plist with
`NSCameraUsageDescription`; packaging fails if the signed executable loses the
Camera entitlement. When testing after an older build was already denied by
TCC, reset that stale decision before starting the new package:

```bash
tccutil reset Camera com.czur.sdk-open
```

Do not run the reset as part of installation because it would erase a user's
explicit Camera choice on every upgrade.

`/Library/CZUR/sdk-open/uninstall.sh` removes only SDK Open and its marked hosts
entry. It intentionally preserves the shared helper, per-user application data,
and the trusted root CA because that CA may be shared by other CZUR products.

The shared helper identity is fixed as `com.czur.device-helper` for both SDK
Open and CapEditor. If the helper must be removed manually, stop the exact
system LaunchDaemon and remove its matching files as root:

```bash
sudo launchctl bootout system/com.czur.device-helper
sudo rm -f /Library/LaunchDaemons/com.czur.device-helper.plist \
  /Library/PrivilegedHelperTools/com.czur.device-helper
```

Always use this hyphenated label in launchctl, filesystem and package
commands; it is the only current service name shared by the two products.
