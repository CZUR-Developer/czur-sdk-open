!ifndef RUNTIME_DIR
  !error "RUNTIME_DIR is required"
!endif
!ifndef OUTPUT_FILE
  !error "OUTPUT_FILE is required"
!endif
!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "1.0.0.0"
!endif
!ifndef SDK_ARCH
  !define SDK_ARCH "x64"
!endif
!ifndef INSTALL_HELPER_SCRIPT
  !error "INSTALL_HELPER_SCRIPT is required"
!endif
!ifndef VC_REDIST_PATH
  !error "VC_REDIST_PATH is required"
!endif
!ifndef VC_REDIST_FILE_NAME
  !error "VC_REDIST_FILE_NAME is required"
!endif

RequestExecutionLevel admin

!include "MUI2.nsh"

!define PRODUCT_NAME "SDK Open Runtime"
!define PRODUCT_PUBLISHER "CZUR TECH CO.,LTD"
!define SERVICE_NAME "sdk-open"
!define SERVICE_DISPLAY_NAME "SDK Open Runtime"
!define SERVICE_DESCRIPTION "CZUR SDK Open Runtime"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\sdk-open"
!define QUICK_START_PAGE "http://127.0.0.1:17081/quick-start"
; SetShellVarContext all makes $APPDATA resolve to C:\ProgramData and
; $DOCUMENTS resolve to C:\Users\Public\Documents for this all-users service.
; NSIS has no built-in $COMMONDOCUMENTS; under SetShellVarContext all,
; $DOCUMENTS resolves to the Common Documents (Public Documents) directory.
!define TLS_CONFIG_DIR "$APPDATA\CZUR\sdk-open"
!define TLS_DIR "$APPDATA\CZUR\sdk-open\tls"
!define TLS_RUNTIME_CONFIG "$APPDATA\CZUR\sdk-open\runtime.env"
!define TWAIN_RUNTIME_DIR "$APPDATA\CZUR\sdk-open\twain-runtime"
!define PUBLIC_ROOT_CA_DIR "$DOCUMENTS\CZUR"
!define PUBLIC_ROOT_CA_FILE "${PUBLIC_ROOT_CA_DIR}\local-runtime-root-ca.cer"

Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_FILE}"
; x86/x64 使用各自的 Program Files 和注册表视图，不支持两个架构并行安装。
!if "${SDK_ARCH}" == "x64"
  InstallDir "$PROGRAMFILES64\CZUR\sdk-open"
!else
  InstallDir "$PROGRAMFILES32\CZUR\sdk-open"
!endif
InstallDirRegKey HKLM "${UNINSTALL_KEY}" "InstallLocation"
ShowInstDetails show
ShowUnInstDetails show

!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Open Quick Start"
!define MUI_FINISHPAGE_RUN_FUNCTION OpenQuickStart
!define MUI_FINISHPAGE_RUN_NOTCHECKED

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function OpenQuickStart
  Sleep 3000
  ExecShell "open" "${QUICK_START_PAGE}"
FunctionEnd

Function .onInit
; 在读取 InstallDirRegKey 前设置注册表视图，确保升级安装命中同架构目录。
SetShellVarContext all
!if "${SDK_ARCH}" == "x64"
  SetRegView 64
!else
  SetRegView 32
!endif
FunctionEnd

Function un.onInit
SetShellVarContext all
!if "${SDK_ARCH}" == "x64"
  SetRegView 64
!else
  SetRegView 32
!endif
FunctionEnd

!macro ExecToLogOrAbort COMMAND DESCRIPTION
  nsExec::ExecToStack '${COMMAND}'
  Pop $0
  Pop $1
  DetailPrint "${DESCRIPTION}: exit=$0"
  DetailPrint "$1"
  StrCmp $0 0 +4
    SetErrorLevel 1
    MessageBox MB_OK|MB_ICONSTOP "${DESCRIPTION} failed.$\r$\nExit code: $0$\r$\n$1" /SD IDOK
    Abort
!macroend

!macro ExecToLogAllow1060 COMMAND DESCRIPTION
  nsExec::ExecToStack '${COMMAND}'
  Pop $0
  Pop $1
  DetailPrint "${DESCRIPTION}: exit=$0"
  DetailPrint "$1"
  StrCmp $0 0 +5
  StrCmp $0 1060 +4
    SetErrorLevel 1
    MessageBox MB_OK|MB_ICONSTOP "${DESCRIPTION} failed.$\r$\nExit code: $0$\r$\n$1" /SD IDOK
    Abort
!macroend

!macro ExecToLogAllowServiceNotRunning COMMAND DESCRIPTION
  nsExec::ExecToStack '${COMMAND}'
  Pop $0
  Pop $1
  DetailPrint "${DESCRIPTION}: exit=$0"
  DetailPrint "$1"
  StrCmp $0 0 +6
  StrCmp $0 1060 +5
  StrCmp $0 1062 +4
    SetErrorLevel 1
    MessageBox MB_OK|MB_ICONSTOP "${DESCRIPTION} failed.$\r$\nExit code: $0$\r$\n$1" /SD IDOK
    Abort
!macroend

Function WaitForServiceRunning
  StrCpy $2 0
  loop:
    nsExec::ExecToStack 'cmd.exe /c sc.exe query "${SERVICE_NAME}" | find "RUNNING"'
    Pop $0
    Pop $1
    DetailPrint "Wait for ${SERVICE_NAME} RUNNING: exit=$0"
    DetailPrint "$1"
    StrCmp $0 0 done
    IntOp $2 $2 + 1
    IntCmp $2 60 fail 0 0
    Sleep 500
    Goto loop
  done:
    Return
  fail:
    SetErrorLevel 1
    MessageBox MB_OK|MB_ICONSTOP "${SERVICE_NAME} did not reach RUNNING state.$\r$\n$1" /SD IDOK
    Abort
FunctionEnd

Function WaitForServiceDeleted
  StrCpy $2 0
  wait_service_deleted:
    nsExec::ExecToStack 'sc.exe query "${SERVICE_NAME}"'
    Pop $0
    Pop $1
    DetailPrint "Wait for ${SERVICE_NAME} deletion: exit=$0"
    DetailPrint "$1"
    StrCmp $0 1060 service_deleted
    StrCmp $0 0 service_delete_retry
    ; ERROR_SERVICE_MARKED_FOR_DELETE means SCM has accepted the delete but has
    ; not released the service entry yet. Keep waiting instead of re-registering
    ; the service against a stale entry.
    StrCmp $0 1072 service_delete_retry
    SetErrorLevel 1
    MessageBox MB_OK|MB_ICONSTOP "Unable to confirm ${SERVICE_NAME} service deletion.$\r$\nExit code: $0$\r$\n$1" /SD IDOK
    Abort
  service_delete_retry:
    IntOp $2 $2 + 1
    IntCmp $2 60 service_delete_timeout 0 0
    Sleep 500
    Goto wait_service_deleted
  service_deleted:
    Return
  service_delete_timeout:
    SetErrorLevel 1
    MessageBox MB_OK|MB_ICONSTOP "${SERVICE_NAME} service was not deleted within 30 seconds.$\r$\n$1" /SD IDOK
    Abort
FunctionEnd

Function un.WaitForServiceDeleted
  StrCpy $2 0
  un_wait_service_deleted:
    nsExec::ExecToStack 'sc.exe query "${SERVICE_NAME}"'
    Pop $0
    Pop $1
    DetailPrint "Wait for ${SERVICE_NAME} deletion: exit=$0"
    DetailPrint "$1"
    StrCmp $0 1060 un_service_deleted
    StrCmp $0 0 un_service_delete_retry
    ; ERROR_SERVICE_MARKED_FOR_DELETE means SCM has accepted the delete but has
    ; not released the service entry yet. Keep waiting before removing files.
    StrCmp $0 1072 un_service_delete_retry
    SetErrorLevel 1
    MessageBox MB_OK|MB_ICONSTOP "Unable to confirm ${SERVICE_NAME} service deletion.$\r$\nExit code: $0$\r$\n$1" /SD IDOK
    Abort
  un_service_delete_retry:
    IntOp $2 $2 + 1
    IntCmp $2 60 un_service_delete_timeout 0 0
    Sleep 500
    Goto un_wait_service_deleted
  un_service_deleted:
    Return
  un_service_delete_timeout:
    SetErrorLevel 1
    MessageBox MB_OK|MB_ICONSTOP "${SERVICE_NAME} service was not deleted within 30 seconds.$\r$\n$1" /SD IDOK
    Abort
FunctionEnd

Function WaitForQuickStart
  !insertmacro ExecToLogOrAbort 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File $\"$INSTDIR\sdk-open-install-helper.ps1$\" -Action WaitQuickStart -Url $\"${QUICK_START_PAGE}$\" -TimeoutSeconds 45' 'Wait for Quick Start HTTP endpoint'
FunctionEnd

Function InstallDefaultTlsConfiguration
  InitPluginsDir
  SetOutPath "$PLUGINSDIR\sdk-open-tls"

  CreateDirectory "${TLS_CONFIG_DIR}"
  CreateDirectory "${TLS_DIR}"
  !insertmacro ExecToLogOrAbort 'icacls.exe "${TLS_DIR}" /inheritance:r /grant:r "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F" /c' 'Restrict SDK Open TLS directory access'
  ; The helper validates both sides of a pair, including an orphan private key,
  ; migrates known defaults and records custom/managed mode before ACL setup.
  !insertmacro ExecToLogOrAbort 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File $\"$INSTDIR\sdk-open-install-helper.ps1$\" -Action ProvisionTls -ExecutablePath $\"$INSTDIR\sdk_open_app.exe$\" -TlsDirectory $\"${TLS_DIR}$\"' 'Provision or migrate SDK Open TLS credentials'

tls_write_config:
  IfFileExists "${TLS_RUNTIME_CONFIG}" tls_apply_acl tls_create_config

tls_create_config:
  FileOpen $0 "${TLS_RUNTIME_CONFIG}" w
  IfErrors tls_config_write_failed
  FileWrite $0 "# SDK Open local runtime TLS configuration.$\r$\n"
  FileWrite $0 "# Environment variables with the same names take precedence.$\r$\n"
  FileWrite $0 "SDK_TLS_ENABLED=1$\r$\n"
  FileWrite $0 "SDK_TLS_BIND_HOST=127.0.0.1$\r$\n"
  FileWrite $0 "SDK_TLS_CERT_FILE=${TLS_DIR}\sdk-runtime.localhost.fullchain.pem$\r$\n"
  FileWrite $0 "SDK_TLS_KEY_FILE=${TLS_DIR}\sdk-runtime.localhost.key.pem$\r$\n"
  FileWrite $0 "SDK_ASSET_BASE_URL=https://sdk-runtime.localhost:18082$\r$\n"
  FileWrite $0 "SDK_ASSET_HTTPS_PORT=18082$\r$\n"
  FileWrite $0 "SDK_COMMAND_WSS_PORT=18090$\r$\n"
  FileWrite $0 "SDK_VIDEO_WSS_PORT=18091$\r$\n"
  FileClose $0
  Goto tls_apply_acl

tls_config_write_failed:
  SetErrorLevel 1
  MessageBox MB_OK|MB_ICONSTOP "Unable to write SDK Open TLS configuration: ${TLS_RUNTIME_CONFIG}" /SD IDOK
  Abort

tls_apply_acl:
  ; Apply inheritable permissions to the directory itself, then direct permissions
  ; to each file. With /t, icacls applies (OI)(CI)-only ACEs to plain files and
  ; /inheritance:r leaves those files without an effective read permission.
  !insertmacro ExecToLogOrAbort 'icacls.exe "${TLS_DIR}\local-runtime-root-ca.crt" /inheritance:r /grant:r "*S-1-5-18:F" "*S-1-5-32-544:F" /c' 'Restrict local runtime root CA access'
  !insertmacro ExecToLogOrAbort 'icacls.exe "${TLS_DIR}\local-runtime-root-ca.key.pem" /inheritance:r /grant:r "*S-1-5-18:F" "*S-1-5-32-544:F" /c' 'Restrict local runtime root CA private key access'
  !insertmacro ExecToLogOrAbort 'icacls.exe "${TLS_DIR}\sdk-runtime.localhost.fullchain.pem" /inheritance:r /grant:r "*S-1-5-18:F" "*S-1-5-32-544:F" /c' 'Restrict local runtime TLS certificate access'
  !insertmacro ExecToLogOrAbort 'icacls.exe "${TLS_DIR}\sdk-runtime.localhost.key.pem" /inheritance:r /grant:r "*S-1-5-18:F" "*S-1-5-32-544:F" /c' 'Restrict local runtime TLS private key access'
  !insertmacro ExecToLogOrAbort 'icacls.exe "${TLS_DIR}\tls-state.json" /inheritance:r /grant:r "*S-1-5-18:F" "*S-1-5-32-544:F" /c' 'Restrict local runtime TLS state access'

  !insertmacro ExecToLogOrAbort 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File $\"$INSTDIR\sdk-open-install-helper.ps1$\" -Action InstallTlsTrust -TlsDirectory $\"${TLS_DIR}$\" -PublicRootCaPath $\"${PUBLIC_ROOT_CA_FILE}$\"' 'Trust and export managed SDK Open root CA'
  ; File commands after this function must install into the application directory,
  ; not the temporary TLS plug-in directory or the public CA directory selected above.
  SetOutPath "$INSTDIR"
  Return

FunctionEnd

Function ConfigureLocalSdkHost
  !insertmacro ExecToLogOrAbort 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File $\"$INSTDIR\sdk-open-install-helper.ps1$\" -Action InstallHost' 'Configure sdk-runtime.localhost'
FunctionEnd

Function ConfigureSharedTwainRuntime
  !insertmacro ExecToLogOrAbort 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File $\"$INSTDIR\sdk-open-install-helper.ps1$\" -Action EnsureTwainRuntime -RuntimeConfigPath $\"${TLS_RUNTIME_CONFIG}$\" -TwainRuntimeDirectory $\"${TWAIN_RUNTIME_DIR}$\"' 'Configure shared TWAIN runtime directory'
FunctionEnd

Function un.RemoveLocalSdkHost
  nsExec::ExecToStack 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File $\"$INSTDIR\sdk-open-install-helper.ps1$\" -Action RemoveHost'
  Pop $0
  Pop $1
  DetailPrint "Remove sdk-runtime.localhost: exit=$0"
  DetailPrint "$1"
FunctionEnd

Section "Install"
!ifdef GENERATE_UNINSTALLER_ONLY
  SetOutPath "$INSTDIR"
  CreateDirectory "$INSTDIR"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  Goto sdk_open_install_done
!endif

  !insertmacro ExecToLogAllowServiceNotRunning 'sc.exe stop "${SERVICE_NAME}"' 'Stop existing ${SERVICE_NAME} service'
  !insertmacro ExecToLogAllow1060 'sc.exe delete "${SERVICE_NAME}"' 'Delete existing ${SERVICE_NAME} service'
  Call WaitForServiceDeleted

  SetOutPath "$INSTDIR"
  File /r "${RUNTIME_DIR}\*.*"
  File "/oname=sdk-open-install-helper.ps1" "${INSTALL_HELPER_SCRIPT}"
  File "/oname=${VC_REDIST_FILE_NAME}" "${VC_REDIST_PATH}"

  !insertmacro ExecToLogOrAbort 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File $\"$INSTDIR\sdk-open-install-helper.ps1$\" -Action RemoveBuildArtifacts -RuntimeDirectory $\"$INSTDIR$\"' 'Remove stale SDK Open build artifacts'
  !insertmacro ExecToLogOrAbort 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File $\"$INSTDIR\sdk-open-install-helper.ps1$\" -Action InstallVCRedist -InstallerPath $\"$INSTDIR\${VC_REDIST_FILE_NAME}$\"' 'Install Microsoft VC++ Redistributable'

  Call InstallDefaultTlsConfiguration
  Call ConfigureSharedTwainRuntime
  Call ConfigureLocalSdkHost

!ifdef SIGNED_UNINSTALLER_PATH
  File "/oname=uninstall.exe" "${SIGNED_UNINSTALLER_PATH}"
!else
  WriteUninstaller "$INSTDIR\uninstall.exe"
!endif
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1

  !insertmacro ExecToLogOrAbort '"$INSTDIR\sdk_open_app.exe" --install-service --service-name "${SERVICE_NAME}" --display-name "${SERVICE_DISPLAY_NAME}" --config "${TLS_RUNTIME_CONFIG}"' 'Create ${SERVICE_NAME} service'
  !insertmacro ExecToLogOrAbort 'sc.exe description "${SERVICE_NAME}" "${SERVICE_DESCRIPTION}"' 'Set ${SERVICE_NAME} description'
  !insertmacro ExecToLogOrAbort 'sc.exe failure "${SERVICE_NAME}" reset= 86400 actions= restart/5000/restart/5000/none/0' 'Set ${SERVICE_NAME} failure policy'
  !insertmacro ExecToLogOrAbort 'sc.exe failureflag "${SERVICE_NAME}" 1' 'Set ${SERVICE_NAME} failure flag'
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Services\${SERVICE_NAME}" "AppDirectory" "$INSTDIR"
  !insertmacro ExecToLogOrAbort 'sc.exe start "${SERVICE_NAME}"' 'Start ${SERVICE_NAME} service'
  Call WaitForServiceRunning
  Call WaitForQuickStart
sdk_open_install_done:
SectionEnd

Section "Uninstall"
  !insertmacro ExecToLogAllowServiceNotRunning 'sc.exe stop "${SERVICE_NAME}"' 'Stop ${SERVICE_NAME} service'
  !insertmacro ExecToLogAllow1060 'sc.exe delete "${SERVICE_NAME}"' 'Delete ${SERVICE_NAME} service'
  Call un.WaitForServiceDeleted

  !insertmacro ExecToLogOrAbort 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File $\"$INSTDIR\sdk-open-install-helper.ps1$\" -Action RemoveTrustedRoot -TlsDirectory $\"${TLS_DIR}$\"' 'Remove local runtime root CA trust'
  Call un.RemoveLocalSdkHost
  RMDir /r "${TLS_CONFIG_DIR}"
  Delete "${PUBLIC_ROOT_CA_FILE}"
  RMDir "${PUBLIC_ROOT_CA_DIR}"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  RMDir /r "$INSTDIR"
SectionEnd
