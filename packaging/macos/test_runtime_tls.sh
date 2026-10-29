#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/sdk-open-macos-tls.XXXXXX")"
trap 'rm -rf "${TEST_ROOT}"' EXIT

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

assert_contains() {
    grep -Fq -- "$2" "$1" || fail "${1} does not contain: ${2}"
}

assert_not_contains() {
    if grep -Fq -- "$2" "$1"; then
        fail "${1} unexpectedly contains: ${2}"
    fi
}

prepare_case() {
    local case_dir="$1"
    mkdir -p "${case_dir}/bin" "${case_dir}/runtime/tls" "${case_dir}/LaunchAgents"
    cat > "${case_dir}/runtime/sdk_open_app" <<'EOF'
#!/bin/bash
set -euo pipefail
printf '%s\n' "$*" >> "${SDK_OPEN_TEST_CASE}/provision.log"
if [[ "$1" == "--provision-tls" ]]; then
    tls_dir=""
    if [[ "$2" == "--provision-tls-dir" ]]; then
        tls_dir="$3"
    fi
    mkdir -p "$tls_dir"
    if [[ -e "$tls_dir/local-runtime-root-ca.crt" && ! -e "$tls_dir/local-runtime-root-ca.key.pem" ]]; then
        echo 'managed TLS root certificate and private key are incomplete' >&2
        exit 1
    fi
    mode=managed
    if [[ -e "$tls_dir/sdk-runtime.localhost.fullchain.pem" ]]; then
        mode=custom
    fi
    : > "$tls_dir/local-runtime-root-ca.crt"
    : > "$tls_dir/local-runtime-root-ca.key.pem"
    printf '{"mode":"%s"}\n' "$mode" > "$tls_dir/tls-state.json"
    if [[ "$mode" == managed ]]; then
        : > "$tls_dir/sdk-runtime.localhost.fullchain.pem"
        : > "$tls_dir/sdk-runtime.localhost.key.pem"
    fi
fi
exit 0
EOF
    : > "${case_dir}/LaunchAgents/com.czur.sdk-open.plist"
    : > "${case_dir}/security.log"
    : > "${case_dir}/launchctl.log"

    sed \
        -e "s|/Library/CZUR/sdk-open|${case_dir}/runtime|g" \
        -e "s|/Library/LaunchAgents|${case_dir}/LaunchAgents|g" \
        -e "s|/Library/Keychains/System.keychain|${case_dir}/System.keychain|g" \
        -e "s|/etc/hosts|${case_dir}/hosts|g" \
        -e "s|/usr/bin/security|${case_dir}/bin/security|g" \
        -e "s|/usr/bin/openssl|${case_dir}/bin/openssl|g" \
        -e "s|/usr/bin/shasum|${case_dir}/bin/shasum|g" \
        -e "s|/usr/bin/grep|${case_dir}/bin/grep|g" \
        -e "s|/bin/cp|${case_dir}/bin/cp-hosts|g" \
        "${SCRIPT_DIR}/scripts/runtime/postinstall" > "${case_dir}/postinstall"
    sed \
        -e "s|/Library/CZUR/sdk-open|${case_dir}/runtime|g" \
        -e "s|/Library/LaunchAgents|${case_dir}/LaunchAgents|g" \
        -e "s|/etc/hosts|${case_dir}/hosts|g" \
        -e "s|/bin/cp|${case_dir}/bin/cp-hosts|g" \
        "${SCRIPT_DIR}/uninstall.sh" > "${case_dir}/uninstall"
    chmod 755 "${case_dir}/postinstall" "${case_dir}/uninstall"

    cat > "${case_dir}/bin/security" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >> "${SDK_OPEN_TEST_CASE}/security.log"
if [[ "$1" == "find-certificate" ]]; then
    if [[ -e "${SDK_OPEN_TEST_CASE}/present" ]]; then
        echo "SHA-256 hash: AABBCCDD"
        exit 0
    fi
    exit 1
fi
if [[ "$1" == "verify-cert" ]]; then
    if [[ "$3" == */local-runtime-root-ca.crt ]]; then
        [[ -e "${SDK_OPEN_TEST_CASE}/trusted" ]]
    else
        [[ ! -e "${SDK_OPEN_TEST_CASE}/fail-leaf-verification" ]]
    fi
    exit $?
fi
if [[ -e "${SDK_OPEN_TEST_CASE}/hang-security" ]]; then
    exec sleep 30
fi
[[ ! -e "${SDK_OPEN_TEST_CASE}/fail-security" ]] || exit 1
touch "${SDK_OPEN_TEST_CASE}/present"
[[ ! -e "${SDK_OPEN_TEST_CASE}/fail-trust-setting" ]] || exit 0
touch "${SDK_OPEN_TEST_CASE}/trusted"
EOF
    cat > "${case_dir}/bin/openssl" <<'EOF'
#!/bin/bash
printf 'certificate-der'
EOF
    cat > "${case_dir}/bin/shasum" <<'EOF'
#!/bin/bash
if [[ "$#" -gt 2 ]]; then
    exec /usr/bin/shasum "$@"
fi
cat >/dev/null
echo "AABBCCDD  -"
EOF
    cat > "${case_dir}/bin/grep" <<'EOF'
#!/bin/bash
exec /usr/bin/grep "$@"
EOF
    cat > "${case_dir}/bin/launchctl" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >> "${SDK_OPEN_TEST_CASE}/launchctl.log"
EOF
    cat > "${case_dir}/bin/stat" <<'EOF'
#!/bin/bash
printf '%s\n' console-user
EOF
    cat > "${case_dir}/bin/id" <<'EOF'
#!/bin/bash
if [[ "$*" == "-u" ]]; then
    printf '%s\n' 0
else
    printf '%s\n' 501
fi
EOF
    cat > "${case_dir}/bin/chown" <<'EOF'
#!/bin/bash
exit 0
EOF
    cat > "${case_dir}/bin/cp-hosts" <<'EOF'
#!/bin/bash
if [[ -e "${SDK_OPEN_TEST_CASE}/fail-hosts-write" ]]; then
    exit 1
fi
exec /bin/cp "$@"
EOF
    cat > "${case_dir}/bin/pkgutil" <<'EOF'
#!/bin/bash
exit 0
EOF
    chmod 755 "${case_dir}/bin/"* "${case_dir}/runtime/sdk_open_app"
}

run_script() {
    local case_dir="$1"
    local script="$2"
    SDK_OPEN_TEST_CASE="${case_dir}" PATH="${case_dir}/bin:${PATH}" \
        "${case_dir}/${script}"
}

idempotent_case="${TEST_ROOT}/idempotent"
prepare_case "${idempotent_case}"
printf '127.0.0.1 localhost\n' > "${idempotent_case}/hosts"
run_script "${idempotent_case}" postinstall
run_script "${idempotent_case}" postinstall
[[ "$(grep -Fc 'CZUR SDK Open managed host' "${idempotent_case}/hosts")" -eq 1 ]] || \
    fail "repeated installation duplicated the managed hosts entry"
assert_contains "${idempotent_case}/security.log" \
    "add-trusted-cert -d -r trustRoot -p ssl -k ${idempotent_case}/System.keychain ${idempotent_case}/runtime/tls/local-runtime-root-ca.crt"
[[ "$(grep -Fc 'add-trusted-cert' "${idempotent_case}/security.log")" -eq 1 ]] || \
    fail "repeated installation imported the same root CA more than once"

assert_contains "${idempotent_case}/launchctl.log" "bootstrap gui/501"
assert_contains "${idempotent_case}/provision.log" "--renew-tls"

legacy_case="${TEST_ROOT}/legacy-upgrade"
prepare_case "${legacy_case}"
printf '127.0.0.1 localhost\n' > "${legacy_case}/hosts"
for file in local-runtime-root-ca.crt sdk-runtime.localhost.key.pem; do
    cp "${SCRIPT_DIR}/../tls/runtime/${file}" "${legacy_case}/runtime/tls/${file}"
done
cp "${SCRIPT_DIR}/tls/legacy-sdk-runtime.localhost.pem" \
    "${legacy_case}/runtime/tls/sdk-runtime.localhost.fullchain.pem"
# Optional integration run uses the installed signed runtime, with trust and
# launchd still mocked so this test never changes the host keychain or service.
if [[ -n "${SDK_OPEN_TEST_RUNTIME_BINARY:-}" ]]; then
    printf '#!/bin/bash\nexec "%s" "$@"\n' "${SDK_OPEN_TEST_RUNTIME_BINARY}" > "${legacy_case}/runtime/sdk_open_app"
fi
run_script "${legacy_case}" postinstall
assert_contains "${legacy_case}/runtime/tls/tls-state.json" '"mode":"managed"'
[[ -f "${legacy_case}/runtime/tls/local-runtime-root-ca.key.pem" ]] || fail "legacy upgrade did not generate a local CA private key"
if [[ -n "${SDK_OPEN_TEST_RUNTIME_BINARY:-}" ]]; then
    /usr/bin/security verify-cert -c "${legacy_case}/runtime/tls/sdk-runtime.localhost.fullchain.pem" \
        -r "${legacy_case}/runtime/tls/local-runtime-root-ca.crt" -p ssl -s sdk-runtime.localhost >/dev/null || \
        fail "migrated certificate failed native macOS SSL policy"
fi
root_before="$(cksum < "${legacy_case}/runtime/tls/local-runtime-root-ca.crt")"
run_script "${legacy_case}" postinstall
[[ "$(cksum < "${legacy_case}/runtime/tls/local-runtime-root-ca.crt")" == "${root_before}" ]] || \
    fail "repair installation replaced the machine-local CA"

updated_legacy_case="${TEST_ROOT}/updated-legacy-upgrade"
prepare_case "${updated_legacy_case}"
printf '127.0.0.1 localhost\n' > "${updated_legacy_case}/hosts"
for file in local-runtime-root-ca.crt sdk-runtime.localhost.fullchain.pem sdk-runtime.localhost.key.pem; do
    cp "${SCRIPT_DIR}/../tls/runtime/${file}" "${updated_legacy_case}/runtime/tls/${file}"
done
run_script "${updated_legacy_case}" postinstall
assert_contains "${updated_legacy_case}/runtime/tls/tls-state.json" '"mode":"managed"'

custom_case="${TEST_ROOT}/custom-upgrade"
prepare_case "${custom_case}"
printf '127.0.0.1 localhost\n' > "${custom_case}/hosts"
cp "${SCRIPT_DIR}/../tls/runtime/local-runtime-root-ca.crt" "${custom_case}/runtime/tls/"
printf 'customer certificate\n' > "${custom_case}/runtime/tls/sdk-runtime.localhost.fullchain.pem"
printf 'customer private key\n' > "${custom_case}/runtime/tls/sdk-runtime.localhost.key.pem"
run_script "${custom_case}" postinstall
run_script "${custom_case}" postinstall
assert_contains "${custom_case}/runtime/tls/tls-state.json" '"mode":"custom"'
assert_contains "${custom_case}/runtime/tls/sdk-runtime.localhost.fullchain.pem" 'customer certificate'
assert_contains "${custom_case}/runtime/tls/sdk-runtime.localhost.key.pem" 'customer private key'
[[ ! -s "${custom_case}/security.log" ]] || fail "customer certificate caused a CZUR root trust import"

custom_key_case="${TEST_ROOT}/custom-key-upgrade"
prepare_case "${custom_key_case}"
printf '127.0.0.1 localhost\n' > "${custom_key_case}/hosts"
cp "${SCRIPT_DIR}/../tls/runtime/local-runtime-root-ca.crt" "${custom_key_case}/runtime/tls/"
cp "${SCRIPT_DIR}/tls/legacy-sdk-runtime.localhost.pem" "${custom_key_case}/runtime/tls/sdk-runtime.localhost.fullchain.pem"
printf 'customer private key\n' > "${custom_key_case}/runtime/tls/sdk-runtime.localhost.key.pem"
run_script "${custom_key_case}" postinstall
assert_contains "${custom_key_case}/runtime/tls/tls-state.json" '"mode":"custom"'
assert_contains "${custom_key_case}/runtime/tls/sdk-runtime.localhost.key.pem" 'customer private key'
cmp "${custom_key_case}/runtime/tls/sdk-runtime.localhost.fullchain.pem" \
    "${SCRIPT_DIR}/tls/legacy-sdk-runtime.localhost.pem" || fail "installer overwrote a legacy certificate with a customer key"

unknown_root_case="${TEST_ROOT}/unknown-incomplete-root"
prepare_case "${unknown_root_case}"
printf '127.0.0.1 localhost\n' > "${unknown_root_case}/hosts"
printf 'customer root\n' > "${unknown_root_case}/runtime/tls/local-runtime-root-ca.crt"
if run_script "${unknown_root_case}" postinstall; then
    fail "installation overwrote an unknown incomplete root CA"
fi
assert_contains "${unknown_root_case}/runtime/tls/local-runtime-root-ca.crt" 'customer root'
[[ ! -s "${unknown_root_case}/launchctl.log" ]] || fail "LaunchAgent started after provisioning failure"

untrusted_case="${TEST_ROOT}/present-without-trust"
prepare_case "${untrusted_case}"
printf '127.0.0.1 localhost\n' > "${untrusted_case}/hosts"
touch "${untrusted_case}/present"
run_script "${untrusted_case}" postinstall
assert_contains "${untrusted_case}/security.log" "add-trusted-cert"
[[ -e "${untrusted_case}/trusted" ]] || fail "existing untrusted root CA was not repaired"

trust_setting_failure_case="${TEST_ROOT}/missing-trust-setting"
prepare_case "${trust_setting_failure_case}"
printf '127.0.0.1 localhost\n' > "${trust_setting_failure_case}/hosts"
touch "${trust_setting_failure_case}/fail-trust-setting"
if run_script "${trust_setting_failure_case}" postinstall; then
    fail "installation succeeded without working root CA trust"
fi
[[ ! -s "${trust_setting_failure_case}/launchctl.log" ]] || \
    fail "LaunchAgent started without working root CA trust"

invalid_leaf_case="${TEST_ROOT}/invalid-leaf"
prepare_case "${invalid_leaf_case}"
printf '127.0.0.1 localhost\n' > "${invalid_leaf_case}/hosts"
touch "${invalid_leaf_case}/fail-leaf-verification"
if run_script "${invalid_leaf_case}" postinstall; then
    fail "installation succeeded with a server certificate rejected by macOS"
fi
[[ ! -s "${invalid_leaf_case}/launchctl.log" ]] || \
    fail "LaunchAgent started with an invalid server certificate"
assert_not_contains "${invalid_leaf_case}/hosts" "CZUR SDK Open managed host"

existing_case="${TEST_ROOT}/existing"
prepare_case "${existing_case}"
printf '192.0.2.10 localhost SDK-RUNTIME.LOCALHOST # locally managed\n' > "${existing_case}/hosts"
hosts_before="$(cksum < "${existing_case}/hosts")"
run_script "${existing_case}" postinstall
[[ "$(cksum < "${existing_case}/hosts")" == "${hosts_before}" ]] || \
    fail "an existing valid mapping was modified"
assert_not_contains "${existing_case}/hosts" "CZUR SDK Open managed host"

certificate_failure_case="${TEST_ROOT}/certificate-failure"
prepare_case "${certificate_failure_case}"
printf '127.0.0.1 localhost\n' > "${certificate_failure_case}/hosts"
touch "${certificate_failure_case}/fail-security"
if run_script "${certificate_failure_case}" postinstall; then
    fail "installation succeeded after certificate trust failure"
fi
[[ ! -s "${certificate_failure_case}/launchctl.log" ]] || \
    fail "LaunchAgent started after certificate trust failure"
assert_not_contains "${certificate_failure_case}/hosts" "CZUR SDK Open managed host"

certificate_timeout_case="${TEST_ROOT}/certificate-timeout"
prepare_case "${certificate_timeout_case}"
printf '127.0.0.1 localhost\n' > "${certificate_timeout_case}/hosts"
touch "${certificate_timeout_case}/hang-security"
if SDK_OPEN_TRUST_INSTALL_TIMEOUT_SECONDS=1 run_script "${certificate_timeout_case}" postinstall; then
    fail "installation succeeded after certificate trust timeout"
fi
[[ ! -s "${certificate_timeout_case}/launchctl.log" ]] || \
    fail "LaunchAgent started after certificate trust timeout"

hosts_failure_case="${TEST_ROOT}/hosts-failure"
prepare_case "${hosts_failure_case}"
printf '127.0.0.1 localhost\n' > "${hosts_failure_case}/hosts"
touch "${hosts_failure_case}/fail-hosts-write"
if run_script "${hosts_failure_case}" postinstall; then
    fail "installation succeeded after hosts update failure"
fi
[[ ! -s "${hosts_failure_case}/launchctl.log" ]] || \
    fail "LaunchAgent started after hosts update failure"
assert_not_contains "${hosts_failure_case}/hosts" "CZUR SDK Open managed host"
if compgen -G "${hosts_failure_case}/hosts.czur-sdk-open.*" >/dev/null; then
    fail "hosts update failure left a temporary file behind"
fi

uninstall_case="${TEST_ROOT}/uninstall"
prepare_case "${uninstall_case}"
printf '%s\n' \
    '127.0.0.1 localhost' \
    '127.0.0.1 sdk-runtime.localhost # CZUR SDK Open managed host' \
    '127.0.0.1 retained.example # user mentioned CZUR SDK Open managed host' \
    '127.0.0.1 retained.example # locally managed' > "${uninstall_case}/hosts"
run_script "${uninstall_case}" uninstall
assert_not_contains "${uninstall_case}/hosts" \
    "127.0.0.1 sdk-runtime.localhost # CZUR SDK Open managed host"
assert_contains "${uninstall_case}/hosts" \
    "127.0.0.1 retained.example # user mentioned CZUR SDK Open managed host"
assert_contains "${uninstall_case}/hosts" "127.0.0.1 retained.example # locally managed"
[[ ! -e "${uninstall_case}/runtime" ]] || fail "runtime directory was not removed"
[[ ! -s "${uninstall_case}/security.log" ]] || fail "uninstall modified the root CA"

# This check uses the real native evaluator, not the installer mocks above.
# Explicit roots avoid depending on this developer/CI host's trust settings.
if [[ "$(uname -s)" == Darwin ]]; then
    /usr/bin/security verify-cert \
        -c "${SCRIPT_DIR}/../tls/runtime/sdk-runtime.localhost.fullchain.pem" \
        -r "${SCRIPT_DIR}/../tls/runtime/local-runtime-root-ca.crt" \
        -p ssl -s sdk-runtime.localhost >/dev/null || \
        fail "packaged certificate does not meet native macOS TLS requirements"
fi

echo "macOS runtime TLS installer tests passed"
