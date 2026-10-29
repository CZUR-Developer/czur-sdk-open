#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/sdk-open-linux-tls.XXXXXX")"
trap 'rm -rf "${TEST_ROOT}"' EXIT

LEGACY_ROOT="${SDK_ROOT}/packaging/tls/runtime/local-runtime-root-ca.crt"
LEGACY_CERTIFICATE_825D="${SDK_ROOT}/packaging/tls/runtime/sdk-runtime.localhost.fullchain.pem"
LEGACY_CERTIFICATE_5Y="${SDK_ROOT}/packaging/macos/tls/legacy-sdk-runtime.localhost.pem"
LEGACY_PRIVATE_KEY="${SDK_ROOT}/packaging/tls/runtime/sdk-runtime.localhost.key.pem"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

assert_file_contains() {
    grep -Fq -- "$2" "$1" || fail "${1} does not contain: ${2}"
}

assert_file_missing() {
    [ ! -e "$1" ] || fail "unexpected file: $1"
}

assert_provision_count() {
    local log_path="$1/provision.log"
    local expected="$2"
    local actual=0
    if [ -e "$log_path" ]; then
        actual=$(wc -l < "$log_path" | tr -d ' ')
    fi
    [ "$actual" = "$expected" ] || fail "expected ${expected} provisions for $1, got ${actual}"
}

file_sha256() {
    sha256sum "$1" | awk '{print $1}'
}

prepare_case() {
    local case_dir="$1"
    mkdir -p \
        "${case_dir}/bin" \
        "${case_dir}/opt/apps/sdk-open/tls/default" \
        "${case_dir}/etc/czur/sdk-open/tls" \
        "${case_dir}/usr/local/share/ca-certificates"
    printf '%s\n' 'SDK_OPEN_COMMAND_PORT=18090' > \
        "${case_dir}/opt/apps/sdk-open/tls/default/runtime.env"
    : > "${case_dir}/hosts"

    sed \
        -e "s|/var/lib/czur/sdk-open|${case_dir}/var/lib/czur/sdk-open|g" \
        -e "s|/opt/apps/sdk-open|${case_dir}/opt/apps/sdk-open|g" \
        -e "s|/etc/czur/sdk-open|${case_dir}/etc/czur/sdk-open|g" \
        -e "s|/usr/local/share/ca-certificates/local-runtime-root-ca.crt|${case_dir}/usr/local/share/ca-certificates/local-runtime-root-ca.crt|g" \
        -e "s|/etc/hosts|${case_dir}/hosts|g" \
        -e 's/install -D -m 0644/install -m 0644/' \
        "${SCRIPT_DIR}/common/DEBIAN/postinst" > "${case_dir}/postinst"
    sed \
        -e "s|/opt/apps/sdk-open|${case_dir}/opt/apps/sdk-open|g" \
        -e "s|/etc/czur/sdk-open|${case_dir}/etc/czur/sdk-open|g" \
        -e "s|/usr/local/share/ca-certificates/local-runtime-root-ca.crt|${case_dir}/usr/local/share/ca-certificates/local-runtime-root-ca.crt|g" \
        -e "s|/etc/hosts|${case_dir}/hosts|g" \
        "${SCRIPT_DIR}/common/DEBIAN/postrm" > "${case_dir}/postrm"
    chmod 0755 "${case_dir}/postinst" "${case_dir}/postrm"

    cat > "${case_dir}/opt/apps/sdk-open/sdk_open_app" <<'EOF'
#!/bin/bash
set -euo pipefail
printf '%s\n' "$*" >> "${SDK_OPEN_TEST_CASE}/provision.log"
[ "$1" = "--provision-tls" ]
[ "$2" = "--provision-tls-dir" ]
tls_dir="$3"
mkdir -p "$tls_dir"

if [ -e "${SDK_OPEN_TEST_CASE}/fail-provision" ]; then
    printf '%s\n' partial-root > "$tls_dir/local-runtime-root-ca.crt"
    printf '%s\n' partial-key > "$tls_dir/local-runtime-root-ca.key.pem"
    printf '%s\n' partial-state > "$tls_dir/tls-state.json.next"
    printf '%s\n' partial-leaf > \
        "$tls_dir/sdk-runtime.localhost.fullchain.pem.rotation"
    printf '%s\n' partial-private-key > \
        "$tls_dir/sdk-runtime.localhost.key.pem.rotation.next"
    exit 1
fi

if [ -n "${SDK_OPEN_TEST_RUNTIME_BINARY:-}" ]; then
    exec "${SDK_OPEN_TEST_RUNTIME_BINARY}" "$@"
fi

if { [ -e "$tls_dir/local-runtime-root-ca.crt" ] && [ ! -e "$tls_dir/local-runtime-root-ca.key.pem" ]; } ||
   { [ ! -e "$tls_dir/local-runtime-root-ca.crt" ] && [ -e "$tls_dir/local-runtime-root-ca.key.pem" ]; }; then
    echo 'managed TLS root certificate and private key are incomplete' >&2
    exit 1
fi

if [ ! -e "$tls_dir/local-runtime-root-ca.crt" ]; then
    printf '%s\n' generated-root > "$tls_dir/local-runtime-root-ca.crt"
    printf '%s\n' generated-root-key > "$tls_dir/local-runtime-root-ca.key.pem"
fi

if [ -e "$tls_dir/tls-state.json" ] &&
   grep -Fq '"mode":"managed"' "$tls_dir/tls-state.json" &&
   [ -e "$tls_dir/sdk-runtime.localhost.fullchain.pem" ] &&
   [ -e "$tls_dir/sdk-runtime.localhost.key.pem" ]; then
    exit 0
elif [ -e "$tls_dir/sdk-runtime.localhost.fullchain.pem" ]; then
    printf '%s\n' '{"mode":"custom"}' > "$tls_dir/tls-state.json"
else
    printf '%s\n' generated-leaf > "$tls_dir/sdk-runtime.localhost.fullchain.pem"
    printf '%s\n' generated-leaf-key > "$tls_dir/sdk-runtime.localhost.key.pem"
    printf '%s\n' '{"mode":"managed"}' > "$tls_dir/tls-state.json"
fi
EOF
    chmod 0755 "${case_dir}/opt/apps/sdk-open/sdk_open_app"

    cat > "${case_dir}/bin/systemctl" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >> "${SDK_OPEN_TEST_CASE}/systemctl.log"
printf 'systemctl %s\n' "$*" >> "${SDK_OPEN_TEST_CASE}/events.log"
EOF
    cat > "${case_dir}/bin/update-ca-certificates" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >> "${SDK_OPEN_TEST_CASE}/update-ca-certificates.log"
printf '%s\n' update-ca-certificates >> "${SDK_OPEN_TEST_CASE}/events.log"
if [ -e "${SDK_OPEN_TEST_CASE}/fail-update-ca-certificates" ]; then
    exit 1
fi
EOF
    cat > "${case_dir}/bin/rm" <<'EOF'
#!/bin/bash
if [ -e "${SDK_OPEN_TEST_CASE}/fail-legacy-root-removal" ] &&
   [ ! -e "${SDK_OPEN_TEST_CASE}/legacy-root-removal-failed" ]; then
    for target in "$@"; do
        if [[ "$target" == */local-runtime-root-ca.crt ]]; then
            : > "${SDK_OPEN_TEST_CASE}/legacy-root-removal-failed"
            exit 1
        fi
    done
fi
exec /bin/rm "$@"
EOF
    chmod 0755 "${case_dir}/bin/systemctl" \
        "${case_dir}/bin/update-ca-certificates" "${case_dir}/bin/rm"
}

run_postinst() {
    local case_dir="$1"
    SDK_OPEN_TEST_CASE="$case_dir" \
        PATH="${case_dir}/bin:/usr/bin:/bin:/usr/sbin:/sbin" \
        bash "${case_dir}/postinst" configure
}

run_postinst_without_system_ca_tool() {
    local case_dir="$1"
    rm -f "${case_dir}/bin/update-ca-certificates"
    SDK_OPEN_TEST_CASE="$case_dir" \
        PATH="${case_dir}/bin:/usr/bin:/bin" \
        bash "${case_dir}/postinst" configure
}

run_postrm() {
    local case_dir="$1"
    SDK_OPEN_TEST_CASE="$case_dir" \
        PATH="${case_dir}/bin:/usr/bin:/bin:/usr/sbin:/sbin" \
        bash "${case_dir}/postrm" remove
}

tls_dir_for() {
    printf '%s\n' "$1/etc/czur/sdk-open/tls"
}

fresh_case="${TEST_ROOT}/fresh"
prepare_case "$fresh_case"
run_postinst "$fresh_case"
fresh_tls_dir=$(tls_dir_for "$fresh_case")
fresh_root_sha256=$(file_sha256 "$fresh_tls_dir/local-runtime-root-ca.crt")
run_postinst "$fresh_case"
[ "$(file_sha256 "$fresh_tls_dir/local-runtime-root-ca.crt")" = "$fresh_root_sha256" ] ||
    fail 'repeat install replaced the generated root CA'
assert_provision_count "$fresh_case" 2
assert_file_contains "$fresh_tls_dir/tls-state.json" '"mode":"managed"'
assert_file_contains "$fresh_case/hosts" '127.0.0.1 sdk-runtime.localhost # Local SDK runtime'
[ "$(grep -Fc 'sdk-runtime.localhost' "$fresh_case/hosts")" = 1 ] ||
    fail 'repeat install duplicated the hosts entry'
[ -e "$fresh_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt" ] ||
    fail 'managed root CA was not copied to the system trust source'
if [ -n "${SDK_OPEN_TEST_RUNTIME_BINARY:-}" ]; then
    openssl verify -CAfile "$fresh_tls_dir/local-runtime-root-ca.crt" \
        "$fresh_tls_dir/sdk-runtime.localhost.fullchain.pem" >/dev/null ||
        fail 'the real runtime generated a leaf that does not verify against its root'
    openssl x509 -in "$fresh_tls_dir/sdk-runtime.localhost.fullchain.pem" \
        -noout -text | grep -Fq 'DNS:sdk-runtime.localhost' ||
        fail 'the real runtime leaf is missing sdk-runtime.localhost SAN'
    openssl x509 -in "$fresh_tls_dir/sdk-runtime.localhost.fullchain.pem" \
        -pubkey -noout > "$fresh_case/leaf-public-key.pem"
    openssl pkey -in "$fresh_tls_dir/sdk-runtime.localhost.key.pem" \
        -pubout > "$fresh_case/private-key-public.pem"
    cmp -s "$fresh_case/leaf-public-key.pem" "$fresh_case/private-key-public.pem" ||
        fail 'the real runtime generated a mismatched leaf certificate and private key'
fi

for legacy_name in 5y 825d; do
    legacy_case="${TEST_ROOT}/legacy-${legacy_name}"
    prepare_case "$legacy_case"
    legacy_tls_dir=$(tls_dir_for "$legacy_case")
    if [ "$legacy_name" = 5y ]; then
        cp "$LEGACY_CERTIFICATE_5Y" "$legacy_tls_dir/sdk-runtime.localhost.fullchain.pem"
    else
        cp "$LEGACY_CERTIFICATE_825D" "$legacy_tls_dir/sdk-runtime.localhost.fullchain.pem"
    fi
    cp "$LEGACY_PRIVATE_KEY" "$legacy_tls_dir/sdk-runtime.localhost.key.pem"
    cp "$LEGACY_ROOT" "$legacy_tls_dir/local-runtime-root-ca.crt"
    run_postinst "$legacy_case"
    assert_provision_count "$legacy_case" 2
    assert_file_contains "$legacy_tls_dir/tls-state.json" '"mode":"managed"'
    [ "$(file_sha256 "$legacy_tls_dir/local-runtime-root-ca.crt")" != "$(file_sha256 "$LEGACY_ROOT")" ] ||
        fail "${legacy_name} legacy root CA was not replaced"
done

root_only_case="${TEST_ROOT}/legacy-root-only"
prepare_case "$root_only_case"
root_only_tls_dir=$(tls_dir_for "$root_only_case")
cp "$LEGACY_ROOT" "$root_only_tls_dir/local-runtime-root-ca.crt"
run_postinst "$root_only_case"
assert_provision_count "$root_only_case" 2
assert_file_contains "$root_only_tls_dir/tls-state.json" '"mode":"managed"'

custom_case="${TEST_ROOT}/custom"
prepare_case "$custom_case"
custom_tls_dir=$(tls_dir_for "$custom_case")
printf '%s\n' customer-certificate > "$custom_tls_dir/sdk-runtime.localhost.fullchain.pem"
printf '%s\n' customer-private-key > "$custom_tls_dir/sdk-runtime.localhost.key.pem"
run_postinst "$custom_case"
assert_provision_count "$custom_case" 1
assert_file_contains "$custom_tls_dir/sdk-runtime.localhost.fullchain.pem" customer-certificate
assert_file_contains "$custom_tls_dir/tls-state.json" '"mode":"custom"'
assert_file_missing "$custom_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"

custom_key_case="${TEST_ROOT}/known-leaf-custom-key"
prepare_case "$custom_key_case"
custom_key_tls_dir=$(tls_dir_for "$custom_key_case")
cp "$LEGACY_CERTIFICATE_825D" "$custom_key_tls_dir/sdk-runtime.localhost.fullchain.pem"
printf '%s\n' customer-private-key > "$custom_key_tls_dir/sdk-runtime.localhost.key.pem"
run_postinst "$custom_key_case"
assert_provision_count "$custom_key_case" 1
[ "$(file_sha256 "$custom_key_tls_dir/sdk-runtime.localhost.fullchain.pem")" = \
  "$(file_sha256 "$LEGACY_CERTIFICATE_825D")" ] ||
    fail 'known leaf paired with a customer key was overwritten'
assert_file_contains "$custom_key_tls_dir/sdk-runtime.localhost.key.pem" customer-private-key
assert_file_contains "$custom_key_tls_dir/tls-state.json" '"mode":"custom"'
assert_file_missing "$custom_key_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"

custom_legacy_root_case="${TEST_ROOT}/custom-with-legacy-root"
prepare_case "$custom_legacy_root_case"
custom_legacy_root_tls_dir=$(tls_dir_for "$custom_legacy_root_case")
printf '%s\n' customer-certificate > \
    "$custom_legacy_root_tls_dir/sdk-runtime.localhost.fullchain.pem"
printf '%s\n' customer-private-key > \
    "$custom_legacy_root_tls_dir/sdk-runtime.localhost.key.pem"
cp "$LEGACY_ROOT" "$custom_legacy_root_tls_dir/local-runtime-root-ca.crt"
cp "$LEGACY_ROOT" \
    "$custom_legacy_root_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"
run_postinst "$custom_legacy_root_case"
assert_provision_count "$custom_legacy_root_case" 2
assert_file_contains \
    "$custom_legacy_root_tls_dir/sdk-runtime.localhost.fullchain.pem" \
    customer-certificate
assert_file_contains "$custom_legacy_root_tls_dir/sdk-runtime.localhost.key.pem" \
    customer-private-key
assert_file_contains "$custom_legacy_root_tls_dir/tls-state.json" '"mode":"custom"'
[ "$(file_sha256 "$custom_legacy_root_tls_dir/local-runtime-root-ca.crt")" != \
  "$(file_sha256 "$LEGACY_ROOT")" ] ||
    fail 'known legacy root next to customer credentials was not replaced'
assert_file_missing \
    "$custom_legacy_root_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"
update_event_line=$(grep -n '^update-ca-certificates$' \
    "$custom_legacy_root_case/events.log" | head -n 1 | cut -d: -f1)
start_event_line=$(grep -n '^systemctl start sdk-open.service$' \
    "$custom_legacy_root_case/events.log" | head -n 1 | cut -d: -f1)
[ -n "$update_event_line" ] && [ -n "$start_event_line" ] &&
    [ "$update_event_line" -lt "$start_event_line" ] ||
    fail 'service started before the legacy root CA trust was refreshed'

custom_state_case="${TEST_ROOT}/custom-state"
prepare_case "$custom_state_case"
custom_state_tls_dir=$(tls_dir_for "$custom_state_case")
printf '%s\n' customer-certificate > "$custom_state_tls_dir/sdk-runtime.localhost.fullchain.pem"
printf '%s\n' customer-private-key > "$custom_state_tls_dir/sdk-runtime.localhost.key.pem"
printf '%s\n' customer-root > "$custom_state_tls_dir/local-runtime-root-ca.crt"
printf '%s\n' customer-root-key > "$custom_state_tls_dir/local-runtime-root-ca.key.pem"
printf '%s\n' '{"mode": "custom"}' > "$custom_state_tls_dir/tls-state.json"
run_postinst "$custom_state_case"
assert_provision_count "$custom_state_case" 1
assert_file_missing "$custom_state_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"

unknown_root_case="${TEST_ROOT}/unknown-root"
prepare_case "$unknown_root_case"
unknown_root_tls_dir=$(tls_dir_for "$unknown_root_case")
cp "$LEGACY_CERTIFICATE_825D" "$unknown_root_tls_dir/sdk-runtime.localhost.fullchain.pem"
cp "$LEGACY_PRIVATE_KEY" "$unknown_root_tls_dir/sdk-runtime.localhost.key.pem"
printf '%s\n' unknown-root > "$unknown_root_tls_dir/local-runtime-root-ca.crt"
unknown_root_sha256=$(file_sha256 "$unknown_root_tls_dir/local-runtime-root-ca.crt")
if run_postinst "$unknown_root_case" >"${unknown_root_case}/output.log" 2>&1; then
    fail 'unknown incomplete root CA was accepted'
fi
[ "$(file_sha256 "$unknown_root_tls_dir/local-runtime-root-ca.crt")" = "$unknown_root_sha256" ] ||
    fail 'unknown root CA was modified'
[ "$(file_sha256 "$unknown_root_tls_dir/sdk-runtime.localhost.fullchain.pem")" = "$(file_sha256 "$LEGACY_CERTIFICATE_825D")" ] ||
    fail 'legacy leaf was modified when the root CA was unknown'
assert_provision_count "$unknown_root_case" 0
assert_file_contains "$unknown_root_case/output.log" 'existing files were preserved'

partial_case="${TEST_ROOT}/partial-leaf"
prepare_case "$partial_case"
partial_tls_dir=$(tls_dir_for "$partial_case")
printf '%s\n' orphan-certificate > "$partial_tls_dir/sdk-runtime.localhost.fullchain.pem"
if run_postinst "$partial_case" >"${partial_case}/output.log" 2>&1; then
    fail 'orphan leaf certificate was accepted'
fi
assert_file_contains "$partial_tls_dir/sdk-runtime.localhost.fullchain.pem" orphan-certificate
assert_provision_count "$partial_case" 0

managed_partial_case="${TEST_ROOT}/managed-partial"
prepare_case "$managed_partial_case"
managed_partial_tls_dir=$(tls_dir_for "$managed_partial_case")
printf '%s\n' managed-leaf > "$managed_partial_tls_dir/sdk-runtime.localhost.fullchain.pem"
printf '%s\n' managed-key > "$managed_partial_tls_dir/sdk-runtime.localhost.key.pem"
printf '%s\n' managed-root > "$managed_partial_tls_dir/local-runtime-root-ca.crt"
printf '%s\n' '{"mode":"managed"}' > "$managed_partial_tls_dir/tls-state.json"
if run_postinst "$managed_partial_case" >"${managed_partial_case}/output.log" 2>&1; then
    fail 'managed state with an incomplete root pair was accepted'
fi
assert_file_contains "$managed_partial_tls_dir/local-runtime-root-ca.crt" managed-root
assert_provision_count "$managed_partial_case" 0

managed_missing_case="${TEST_ROOT}/managed-missing-root-and-leaf"
prepare_case "$managed_missing_case"
managed_missing_tls_dir=$(tls_dir_for "$managed_missing_case")
printf '%s\n' '{"mode":"managed"}' > "$managed_missing_tls_dir/tls-state.json"
if run_postinst "$managed_missing_case" >"${managed_missing_case}/output.log" 2>&1; then
    fail 'managed state without root or leaf credentials was accepted'
fi
assert_provision_count "$managed_missing_case" 0
assert_file_contains "$managed_missing_case/output.log" 'existing files were preserved'

custom_missing_leaf_case="${TEST_ROOT}/custom-missing-leaf"
prepare_case "$custom_missing_leaf_case"
custom_missing_leaf_tls_dir=$(tls_dir_for "$custom_missing_leaf_case")
printf '%s\n' customer-root > "$custom_missing_leaf_tls_dir/local-runtime-root-ca.crt"
printf '%s\n' customer-root-key > \
    "$custom_missing_leaf_tls_dir/local-runtime-root-ca.key.pem"
printf '%s\n' '{"mode":"custom"}' > "$custom_missing_leaf_tls_dir/tls-state.json"
if run_postinst "$custom_missing_leaf_case" \
    >"${custom_missing_leaf_case}/output.log" 2>&1; then
    fail 'custom state without a leaf certificate pair was accepted'
fi
assert_provision_count "$custom_missing_leaf_case" 0
assert_file_contains "$custom_missing_leaf_case/output.log" \
    'custom TLS state is missing its certificate pair'

custom_missing_root_case="${TEST_ROOT}/custom-missing-root"
prepare_case "$custom_missing_root_case"
custom_missing_root_tls_dir=$(tls_dir_for "$custom_missing_root_case")
printf '%s\n' customer-certificate > \
    "$custom_missing_root_tls_dir/sdk-runtime.localhost.fullchain.pem"
printf '%s\n' customer-private-key > \
    "$custom_missing_root_tls_dir/sdk-runtime.localhost.key.pem"
printf '%s\n' '{"mode":"custom"}' > "$custom_missing_root_tls_dir/tls-state.json"
run_postinst "$custom_missing_root_case"
assert_provision_count "$custom_missing_root_case" 1
assert_file_contains "$custom_missing_root_tls_dir/tls-state.json" '"mode":"custom"'
assert_file_missing \
    "$custom_missing_root_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"

rollback_case="${TEST_ROOT}/rollback"
prepare_case "$rollback_case"
rollback_tls_dir=$(tls_dir_for "$rollback_case")
cp "$LEGACY_CERTIFICATE_825D" "$rollback_tls_dir/sdk-runtime.localhost.fullchain.pem"
cp "$LEGACY_PRIVATE_KEY" "$rollback_tls_dir/sdk-runtime.localhost.key.pem"
cp "$LEGACY_ROOT" "$rollback_tls_dir/local-runtime-root-ca.crt"
printf '%s\n' existing-state-next > "$rollback_tls_dir/tls-state.json.next"
printf '%s\n' existing-leaf-previous > \
    "$rollback_tls_dir/sdk-runtime.localhost.fullchain.pem.previous"
touch "$rollback_case/fail-provision"
if run_postinst "$rollback_case" >"${rollback_case}/output.log" 2>&1; then
    fail 'failed legacy migration unexpectedly succeeded'
fi
[ "$(file_sha256 "$rollback_tls_dir/sdk-runtime.localhost.fullchain.pem")" = "$(file_sha256 "$LEGACY_CERTIFICATE_825D")" ] ||
    fail 'legacy leaf was not restored after provisioning failure'
[ "$(file_sha256 "$rollback_tls_dir/sdk-runtime.localhost.key.pem")" = "$(file_sha256 "$LEGACY_PRIVATE_KEY")" ] ||
    fail 'legacy private key was not restored after provisioning failure'
[ "$(file_sha256 "$rollback_tls_dir/local-runtime-root-ca.crt")" = "$(file_sha256 "$LEGACY_ROOT")" ] ||
    fail 'legacy root CA was not restored after provisioning failure'
assert_file_missing "$rollback_tls_dir/local-runtime-root-ca.key.pem"
assert_file_missing "$rollback_tls_dir/tls-state.json"
assert_file_contains "$rollback_tls_dir/tls-state.json.next" existing-state-next
assert_file_contains "$rollback_tls_dir/sdk-runtime.localhost.fullchain.pem.previous" \
    existing-leaf-previous
assert_file_missing "$rollback_tls_dir/sdk-runtime.localhost.fullchain.pem.rotation"
assert_file_missing "$rollback_tls_dir/sdk-runtime.localhost.key.pem.rotation.next"
assert_file_contains "$rollback_case/output.log" 'original credentials restored'

remove_failure_case="${TEST_ROOT}/remove-failure-rollback"
prepare_case "$remove_failure_case"
remove_failure_tls_dir=$(tls_dir_for "$remove_failure_case")
cp "$LEGACY_CERTIFICATE_825D" \
    "$remove_failure_tls_dir/sdk-runtime.localhost.fullchain.pem"
cp "$LEGACY_PRIVATE_KEY" "$remove_failure_tls_dir/sdk-runtime.localhost.key.pem"
cp "$LEGACY_ROOT" "$remove_failure_tls_dir/local-runtime-root-ca.crt"
touch "$remove_failure_case/fail-legacy-root-removal"
if run_postinst "$remove_failure_case" >"${remove_failure_case}/output.log" 2>&1; then
    fail 'legacy migration unexpectedly succeeded after root removal failed'
fi
[ "$(file_sha256 "$remove_failure_tls_dir/sdk-runtime.localhost.fullchain.pem")" = \
  "$(file_sha256 "$LEGACY_CERTIFICATE_825D")" ] ||
    fail 'legacy leaf was not restored after root removal failure'
[ "$(file_sha256 "$remove_failure_tls_dir/sdk-runtime.localhost.key.pem")" = \
  "$(file_sha256 "$LEGACY_PRIVATE_KEY")" ] ||
    fail 'legacy key was not restored after root removal failure'
[ "$(file_sha256 "$remove_failure_tls_dir/local-runtime-root-ca.crt")" = \
  "$(file_sha256 "$LEGACY_ROOT")" ] ||
    fail 'legacy root was not restored after root removal failure'
assert_provision_count "$remove_failure_case" 0
if compgen -G "$remove_failure_tls_dir/.legacy-migration.*" >/dev/null; then
    fail 'successful rollback left a legacy migration backup directory behind'
fi

missing_ca_tool_case="${TEST_ROOT}/missing-ca-tool"
prepare_case "$missing_ca_tool_case"
if run_postinst_without_system_ca_tool "$missing_ca_tool_case" \
    >"${missing_ca_tool_case}/output.log" 2>&1; then
    fail 'managed install succeeded without update-ca-certificates'
fi
assert_file_contains "$missing_ca_tool_case/output.log" \
    'update-ca-certificates is required'
assert_file_missing "$missing_ca_tool_case/systemctl.log"

failed_custom_ca_cleanup_case="${TEST_ROOT}/failed-custom-ca-cleanup"
prepare_case "$failed_custom_ca_cleanup_case"
failed_custom_ca_tls_dir=$(tls_dir_for "$failed_custom_ca_cleanup_case")
printf '%s\n' customer-certificate > \
    "$failed_custom_ca_tls_dir/sdk-runtime.localhost.fullchain.pem"
printf '%s\n' customer-private-key > \
    "$failed_custom_ca_tls_dir/sdk-runtime.localhost.key.pem"
cp "$LEGACY_ROOT" "$failed_custom_ca_tls_dir/local-runtime-root-ca.crt"
cp "$LEGACY_ROOT" \
    "$failed_custom_ca_cleanup_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"
touch "$failed_custom_ca_cleanup_case/fail-update-ca-certificates"
if run_postinst "$failed_custom_ca_cleanup_case" \
    >"${failed_custom_ca_cleanup_case}/output.log" 2>&1; then
    fail 'custom install started after the CA trust refresh failed'
fi
[ "$(file_sha256 "$failed_custom_ca_cleanup_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt")" = \
  "$(file_sha256 "$LEGACY_ROOT")" ] ||
    fail 'legacy trust source was not restored after CA refresh failure'
assert_file_missing "$failed_custom_ca_cleanup_case/systemctl.log"
assert_file_contains "$failed_custom_ca_cleanup_case/output.log" \
    'original trust source restored'

managed_remove_case="${TEST_ROOT}/managed-remove"
prepare_case "$managed_remove_case"
managed_remove_tls_dir=$(tls_dir_for "$managed_remove_case")
printf '%s\n' managed-leaf > "$managed_remove_tls_dir/sdk-runtime.localhost.fullchain.pem"
printf '%s\n' managed-key > "$managed_remove_tls_dir/sdk-runtime.localhost.key.pem"
printf '%s\n' managed-root > "$managed_remove_tls_dir/local-runtime-root-ca.crt"
printf '%s\n' managed-root-key > "$managed_remove_tls_dir/local-runtime-root-ca.key.pem"
printf '%s\n' '{"mode":"managed"}' > "$managed_remove_tls_dir/tls-state.json"
cp "$managed_remove_tls_dir/local-runtime-root-ca.crt" \
    "$managed_remove_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"
rm -f "$managed_remove_case/hosts"
run_postrm "$managed_remove_case"
assert_file_missing "$managed_remove_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"
[ -s "$managed_remove_case/update-ca-certificates.log" ] ||
    fail 'CA trust database was not refreshed after removing the managed root'

customer_trust_case="${TEST_ROOT}/customer-trust-remove"
prepare_case "$customer_trust_case"
customer_trust_tls_dir=$(tls_dir_for "$customer_trust_case")
printf '%s\n' managed-root > "$customer_trust_tls_dir/local-runtime-root-ca.crt"
printf '%s\n' managed-root-key > "$customer_trust_tls_dir/local-runtime-root-ca.key.pem"
printf '%s\n' '{"mode":"managed"}' > "$customer_trust_tls_dir/tls-state.json"
printf '%s\n' customer-replaced-trust > \
    "$customer_trust_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"
rm -f "$customer_trust_case/hosts"
run_postrm "$customer_trust_case"
assert_file_contains \
    "$customer_trust_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt" \
    customer-replaced-trust
assert_file_missing "$customer_trust_case/update-ca-certificates.log"

legacy_remove_case="${TEST_ROOT}/legacy-remove"
prepare_case "$legacy_remove_case"
cp "$LEGACY_ROOT" \
    "$legacy_remove_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"
rm -f "$legacy_remove_case/hosts"
run_postrm "$legacy_remove_case"
assert_file_missing "$legacy_remove_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"
[ -s "$legacy_remove_case/update-ca-certificates.log" ] ||
    fail 'legacy root removal did not refresh the CA trust database'

custom_legacy_remove_case="${TEST_ROOT}/custom-legacy-remove"
prepare_case "$custom_legacy_remove_case"
custom_legacy_remove_tls_dir=$(tls_dir_for "$custom_legacy_remove_case")
printf '%s\n' customer-root > \
    "$custom_legacy_remove_tls_dir/local-runtime-root-ca.crt"
printf '%s\n' customer-root-key > \
    "$custom_legacy_remove_tls_dir/local-runtime-root-ca.key.pem"
printf '%s\n' '{"mode":"custom"}' > \
    "$custom_legacy_remove_tls_dir/tls-state.json"
cp "$LEGACY_ROOT" \
    "$custom_legacy_remove_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"
rm -f "$custom_legacy_remove_case/hosts"
run_postrm "$custom_legacy_remove_case"
assert_file_missing \
    "$custom_legacy_remove_case/usr/local/share/ca-certificates/local-runtime-root-ca.crt"

echo 'PASS: Linux SDK Open TLS installer migration tests'
