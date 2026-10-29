#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[5]
INSTALLER_SCRIPT = (
    REPO_ROOT / "src/sdk_open/packaging" / "windows" / "sdk-open-minimal.nsi"
)
INSTALL_HELPER_SCRIPT = (
    REPO_ROOT / "src/sdk_open/packaging" / "windows" / "sdk-open-install-helper.ps1"
)
BUILD_INSTALLER_SCRIPT = (
    REPO_ROOT / "src/sdk_open/packaging" / "windows" / "build-sdk-open-nsis.ps1"
)
VERIFIER_SCRIPT = (
    REPO_ROOT / "src/sdk_open/packaging" / "windows" / "verify-sdk-open-install.ps1"
)


class SdkOpenWindowsInstallerTest(unittest.TestCase):
    def test_build_script_removes_link_and_debug_artifacts_from_runtime_staging(self):
        build_script = BUILD_INSTALLER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("function Remove-SdkOpenBuildArtifacts", build_script)
        self.assertIn("Remove-SdkOpenBuildArtifacts -Directory $stagingDir", build_script)
        for extension in (
            ".lib",
            ".exp",
            ".pdb",
            ".ilk",
            ".obj",
            ".iobj",
            ".ipdb",
            ".map",
        ):
            self.assertIn(extension, build_script)

        # PDBs must still be copied before staging cleanup so symbol upload is
        # unaffected by removing debug files from the installer payload.
        self.assertIn("$PdbOutputDir", build_script)
        self.assertIn("-Filter '*.pdb'", build_script)
        self.assertIn("Copy-Item -LiteralPath $_.FullName", build_script)

    def test_installed_runtime_verifier_rejects_build_artifacts(self):
        verifier = VERIFIER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("function Assert-NoSdkOpenBuildArtifacts", verifier)
        self.assertIn("Assert-NoSdkOpenBuildArtifacts -Directory $InstallDir", verifier)
        for extension in (
            ".lib",
            ".exp",
            ".pdb",
            ".ilk",
            ".obj",
            ".iobj",
            ".ipdb",
            ".map",
        ):
            self.assertIn(extension, verifier)

    def test_build_script_selects_only_matching_vc_redist_architecture(self):
        build_script = BUILD_INSTALLER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("VC_redist.x64 14.44.35211.exe", build_script)
        self.assertIn("VC_redist.x86 14.44.35211.exe", build_script)
        self.assertIn("$VcRedistFileNames[$Architecture]", build_script)
        self.assertIn("Require-File $VcRedistPath", build_script)
        self.assertIn('"/DVC_REDIST_PATH=$VcRedistPath"', build_script)
        self.assertIn('"/DVC_REDIST_FILE_NAME=$VcRedistFileName"', build_script)
        self.assertNotIn("VC_redist.x86 14.44.35211.exe', 'VC_redist.x64", build_script)

    def test_installer_retains_and_installs_vc_redist_before_service(self):
        installer = INSTALLER_SCRIPT.read_text(encoding="utf-8")
        install_helper = INSTALL_HELPER_SCRIPT.read_text(encoding="utf-8")

        payload = 'File "/oname=${VC_REDIST_FILE_NAME}" "${VC_REDIST_PATH}"'
        install_action = (
            '-Action InstallVCRedist -InstallerPath '
            '$\\"$INSTDIR\\${VC_REDIST_FILE_NAME}$\\"'
        )
        self.assertIn(payload, installer)
        self.assertIn(install_action, installer)
        self.assertLess(installer.index(payload), installer.index(install_action))
        self.assertLess(
            installer.index(install_action),
            installer.index('"$INSTDIR\\sdk_open_app.exe" --install-service'),
        )
        self.assertIn("'/install', '/quiet', '/norestart'", install_helper)

    def test_installer_removes_stale_build_artifacts_before_runtime_setup(self):
        installer = INSTALLER_SCRIPT.read_text(encoding="utf-8")
        install_helper = INSTALL_HELPER_SCRIPT.read_text(encoding="utf-8")

        cleanup_action = (
            '-Action RemoveBuildArtifacts -RuntimeDirectory '
            '$\\"$INSTDIR$\\"'
        )
        self.assertIn("'RemoveBuildArtifacts'", install_helper)
        self.assertIn("function Remove-BuildArtifacts", install_helper)
        self.assertIn("Remove-BuildArtifacts -Directory $RuntimeDirectory", install_helper)
        self.assertIn(cleanup_action, installer)
        self.assertLess(
            installer.index(cleanup_action),
            installer.index("'Install Microsoft VC++ Redistributable'"),
        )

    def test_vc_redist_helper_accepts_compatible_success_codes(self):
        install_helper = INSTALL_HELPER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("'InstallVCRedist'", install_helper)
        self.assertIn("[string]$InstallerPath = ''", install_helper)
        self.assertIn("$acceptedExitCodes = @(0, 3010, 1638, -2147023258)", install_helper)
        self.assertIn("$acceptedExitCodes -contains $process.ExitCode", install_helper)
        self.assertIn("throw \"VC++ Redistributable installer failed with exit code", install_helper)
        self.assertRegex(install_helper, r"(?s)catch\s*\{.*?exit 1")

    def test_stop_service_accepts_already_stopped_exit_code(self):
        script = INSTALLER_SCRIPT.read_text(encoding="utf-8")
        macro_match = re.search(
            r"!macro ExecToLogAllowServiceNotRunning\b(?P<body>.*?)!macroend",
            script,
            flags=re.DOTALL,
        )

        self.assertIsNotNone(
            macro_match,
            "installer must define a service-stop macro that handles non-running services",
        )
        self.assertRegex(
            macro_match.group("body"),
            r"StrCmp\s+\$0\s+1062\b",
            "ERROR_SERVICE_NOT_ACTIVE (1062) must not abort install or uninstall",
        )

        stop_calls = re.findall(
            r"!insertmacro ExecToLogAllowServiceNotRunning\s+'sc\.exe stop ",
            script,
        )
        self.assertEqual(
            len(stop_calls),
            2,
            "both upgrade install and uninstall must tolerate an already stopped service",
        )

    def test_silent_error_dialogs_have_defaults_and_nonzero_exit(self):
        script = INSTALLER_SCRIPT.read_text(encoding="utf-8")
        error_dialogs = [
            line.strip()
            for line in script.splitlines()
            if "MessageBox" in line and "MB_ICONSTOP" in line
        ]

        self.assertGreater(len(error_dialogs), 0)
        for dialog in error_dialogs:
            self.assertIn("/SD IDOK", dialog)
        self.assertIn("SetErrorLevel 1", script)

    def test_hosts_management_uses_file_script_instead_of_inline_command(self):
        installer = INSTALLER_SCRIPT.read_text(encoding="utf-8")
        install_helper = INSTALL_HELPER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn('File "/oname=sdk-open-install-helper.ps1" "${INSTALL_HELPER_SCRIPT}"', installer)
        quoted_helper_path = '$\\"$INSTDIR\\sdk-open-install-helper.ps1$\\"'
        self.assertIn(f"-File {quoted_helper_path} -Action InstallHost", installer)
        self.assertIn(f"-File {quoted_helper_path} -Action RemoveHost", installer)
        self.assertIn(
            f'-File {quoted_helper_path} -Action WaitQuickStart -Url $\\"${{QUICK_START_PAGE}}$\\"',
            installer,
        )
        self.assertNotRegex(installer, r"-Command .*sdk-runtime\\?\.localhost")
        self.assertNotIn("-Command", installer)
        helper_commands = [
            line
            for line in installer.splitlines()
            if "sdk-open-install-helper.ps1" in line and "powershell.exe" in line
        ]
        self.assertTrue(helper_commands)
        for command in helper_commands:
            self.assertIn(quoted_helper_path, command)
            self.assertNotIn('$"', command)
        self.assertIn("[IO.File]::AppendAllText", install_helper)
        self.assertIn("# Local SDK runtime", install_helper)
        self.assertIn("install.log", install_helper)

    def test_hosts_helper_recovers_missing_file_and_allows_missing_file_removal(self):
        install_helper = INSTALL_HELPER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("[string]$HostsPath = ''", install_helper)
        self.assertIn("function Ensure-HostsFile", install_helper)
        self.assertIn("[IO.File]::WriteAllText($Path, '', [Text.UTF8Encoding]::new($false))", install_helper)
        self.assertIn("Created missing Windows hosts file: $Path", install_helper)
        self.assertIn("Ensure-HostsFile -Path $hostsPath", install_helper)
        self.assertIn(
            "Managed hosts mapping is already absent because the hosts file does not exist",
            install_helper,
        )

    def test_installer_waits_for_service_deletion_before_reinstall_or_cleanup(self):
        installer = INSTALLER_SCRIPT.read_text(encoding="utf-8")
        function_match = re.search(
            r"Function WaitForServiceDeleted\b(?P<body>.*?)FunctionEnd",
            installer,
            flags=re.DOTALL,
        )

        self.assertIsNotNone(function_match)
        function_body = function_match.group("body")
        self.assertIn('sc.exe query "${SERVICE_NAME}"', function_body)
        self.assertIn("StrCmp $0 1060 service_deleted", function_body)
        self.assertIn("StrCmp $0 1072 service_delete_retry", function_body)
        self.assertIn("Sleep 500", function_body)
        self.assertIn("IntCmp $2 60 service_delete_timeout 0 0", function_body)
        self.assertIn("Function un.WaitForServiceDeleted", installer)
        self.assertIn("Call WaitForServiceDeleted", installer)
        self.assertIn("Call un.WaitForServiceDeleted", installer)
        self.assertNotIn("Sleep 1000", installer)

    def test_installer_verifier_enforces_timeout(self):
        verifier = VERIFIER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("[int]$InstallerTimeoutSeconds = 600", verifier)
        self.assertIn("$process.WaitForExit($TimeoutSeconds * 1000)", verifier)
        self.assertIn("taskkill.exe /PID $process.Id /T /F", verifier)
        self.assertIn("$process.Kill()", verifier)

    def test_tls_acl_grants_direct_file_access(self):
        installer = INSTALLER_SCRIPT.read_text(encoding="utf-8")

        self.assertNotRegex(
            installer,
            r'icacls\.exe "\$\{TLS_DIR\}"[^\r\n]*/t\b',
            "directory inheritance-only ACEs must not be recursively applied to files",
        )
        for file_name in (
            "local-runtime-root-ca.crt",
            "sdk-runtime.localhost.fullchain.pem",
            "sdk-runtime.localhost.key.pem",
        ):
            self.assertRegex(
                installer,
                re.escape(f'icacls.exe "${{TLS_DIR}}\\{file_name}"')
                + r'[^\r\n]*"\*S-1-5-18:F"[^\r\n]*"\*S-1-5-32-544:F"',
            )

    def test_installer_verifier_reads_tls_files(self):
        verifier = VERIFIER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("function Assert-FileReadable", verifier)
        tls_function = re.search(
            r"function Assert-DefaultTlsConfiguration\b(?P<body>.*?)(?=\nfunction |\Z)",
            verifier,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(tls_function)
        self.assertEqual(
            tls_function.group("body").count("Assert-FileReadable -Path"),
            6,
            "TLS files and the public root CA export must all be opened for reading",
        )
        self.assertIn("CommonDocuments", verifier)
        self.assertIn("CZUR\\local-runtime-root-ca.cer", verifier)

    def test_installer_publishes_only_root_ca_for_browser_import(self):
        installer = INSTALLER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn('!define PUBLIC_ROOT_CA_DIR "$DOCUMENTS\\CZUR"', installer)
        self.assertIn(
            '!define PUBLIC_ROOT_CA_FILE "${PUBLIC_ROOT_CA_DIR}\\local-runtime-root-ca.cer"',
            installer,
        )
        helper = INSTALL_HELPER_SCRIPT.read_text(encoding="utf-8")
        self.assertIn('-Action InstallTlsTrust', installer)
        self.assertIn('Copy-Item -LiteralPath $rootCertificatePath -Destination $PublicRootCaPath -Force', helper)
        self.assertIn("$state.mode -eq 'custom'", helper)
        self.assertNotIn('File /oname=local-runtime-root-ca.cer "${TLS_ASSET_DIR}', installer)
        self.assertIn('Delete "${PUBLIC_ROOT_CA_FILE}"', installer)
        self.assertNotIn(
            'File /oname=sdk-runtime.localhost.key.pem "${TLS_ASSET_DIR}',
            installer,
        )

    def test_uninstaller_matches_root_ca_store_thumbprint_to_certificate_der(self):
        install_helper = INSTALL_HELPER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("function Get-CertificateThumbprint", install_helper)
        self.assertIn("X509Certificate2]::new($der)", install_helper)
        self.assertIn(
            "$expectedThumbprint = Get-CertificateThumbprint -Path $rootCertificatePath",
            install_helper,
        )
        self.assertNotIn(
            "$expectedThumbprint = (Get-FileHash -LiteralPath $rootCertificatePath -Algorithm SHA256).Hash",
            install_helper,
        )
        self.assertIn("$_.Thumbprint -eq $expectedThumbprint", install_helper)

    def test_installer_verifier_waits_for_service_deletion(self):
        verifier = VERIFIER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("function Wait-ServiceDeleted", verifier)
        self.assertIn("$exitCode -eq 1060", verifier)
        self.assertIn("$exitCode -ne 0", verifier)
        self.assertIn("Start-Sleep -Milliseconds 500", verifier)
        self.assertIn("Wait-ServiceDeleted -Name $ServiceName", verifier)
        self.assertNotIn(
            "Invoke-Native -FilePath 'sc.exe' -Arguments @('query', $ServiceName)",
            verifier,
        )

    def test_installer_verifier_exercises_packaged_host_helper(self):
        verifier = VERIFIER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("function Test-InstalledHostHelper", verifier)
        self.assertIn("sdk-open-hosts-verification-", verifier)
        self.assertIn("-HostsPath $testHostsPath", verifier)
        self.assertIn("Host helper did not create missing hosts file", verifier)
        self.assertIn("Host helper added a duplicate managed mapping", verifier)
        self.assertIn("Test-InstalledHostHelper -HelperPath $installedHelperPath", verifier)
        self.assertIn("Assert-ActiveHostMapping", verifier)
        self.assertIn("Assert-ManagedHostMappingRemoved", verifier)

    def test_installer_verifier_hosts_patterns_accept_crlf_and_lf(self):
        verifier = VERIFIER_SCRIPT.read_text(encoding="utf-8")
        managed_line_patterns = re.findall(
            r"\$managedLinePattern\s*=\s*'([^']+)'",
            verifier,
        )

        self.assertEqual(
            len(managed_line_patterns),
            2,
            "host mapping presence and removal checks must use the managed-line pattern",
        )
        for pattern in managed_line_patterns:
            for line_ending in ("\r\n", "\n"):
                hosts_text = (
                    "127.0.0.1 sdk-runtime.localhost # Local SDK runtime"
                    + line_ending
                )
                self.assertEqual(
                    len(re.findall(pattern, hosts_text)),
                    1,
                    f"managed hosts mapping must match {line_ending!r} line endings",
                )

    def test_installer_verifier_waits_for_uninstall_directories(self):
        verifier = VERIFIER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("function Wait-PathRemoved", verifier)
        self.assertIn("Get-ChildItem -LiteralPath $Path -Force -Recurse", verifier)
        self.assertIn("Select-Object -First 50 -ExpandProperty FullName", verifier)
        self.assertIn(
            "Wait-PathRemoved -Path $InstallDir -Description 'Install directory'",
            verifier,
        )
        self.assertIn(
            "Wait-PathRemoved -Path $tlsConfigDir "
            "-Description 'SDK Open TLS configuration directory'",
            verifier,
        )
        self.assertIn(
            "Wait-PathRemoved -Path $publicRootCaPath "
            "-Description 'public SDK Open root CA export'",
            verifier,
        )
        self.assertNotIn("Install directory still exists after uninstall", verifier)
        self.assertNotIn(
            "SDK Open TLS configuration directory still exists after uninstall",
            verifier,
        )

    def test_tls_setup_restores_installer_output_directory(self):
        installer = INSTALLER_SCRIPT.read_text(encoding="utf-8")
        function_match = re.search(
            r"Function InstallDefaultTlsConfiguration\b(?P<body>.*?)FunctionEnd",
            installer,
            flags=re.DOTALL,
        )

        self.assertIsNotNone(function_match)
        function_body = function_match.group("body")
        self.assertIn('SetOutPath "$PLUGINSDIR\\sdk-open-tls"', function_body)
        restore_index = function_body.rfind('SetOutPath "$INSTDIR"')
        return_index = function_body.rfind("Return")
        self.assertGreater(
            restore_index,
            function_body.find('SetOutPath "$PLUGINSDIR\\sdk-open-tls"'),
            "TLS setup must restore the output directory after extracting temporary assets",
        )
        self.assertLess(
            restore_index,
            return_index,
            "TLS setup must restore $INSTDIR before returning to signed uninstaller extraction",
        )

    def test_tls_regression_overrides_certutil_without_changing_system_root(self):
        helper = INSTALL_HELPER_SCRIPT.read_text(encoding="utf-8")
        regression = (
            REPO_ROOT / "src/sdk_open/packaging" / "windows" / "test-runtime-tls.ps1"
        ).read_text(encoding="utf-8")

        self.assertIn("[string]$CertutilPath = ''", helper)
        self.assertIn("missing-certutil.exe", regression)
        self.assertNotIn("missing-system-root", regression)


if __name__ == "__main__":
    unittest.main()
