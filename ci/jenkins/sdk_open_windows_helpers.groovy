def paramBool(def value) {
    if (value == null) {
        return false
    }
    if (value instanceof Boolean) {
        return value
    }
    return value.toString().trim().toLowerCase() in ['1', 'true', 'yes', 'on']
}

def aliyun(def scriptCtx, String cmd, boolean returnStdout = true) {
    def script = """#!/usr/bin/env bash
set +x
/usr/local/bin/aliyun ${cmd}
"""
    if (returnStdout) {
        return scriptCtx.sh(script: script, returnStdout: true).trim()
    }
    scriptCtx.sh script
    return ""
}

/**
 * Query APIs can fail transiently when the ECS endpoint or local network is
 * unavailable. Retry the query itself before failing the surrounding polling
 * loop; a successful response is still handled by the normal status polling.
 */
def aliyunWithRetry(def scriptCtx, String cmd, int maxAttempts = 3, int backoffSeconds = 5) {
    Throwable lastError = null
    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        try {
            return aliyun(scriptCtx, cmd)
        }
        catch (err) {
            lastError = err
            if (attempt >= maxAttempts) {
                break
            }
            scriptCtx.echo "Aliyun API query failed (attempt ${attempt}/${maxAttempts}); retrying in ${backoffSeconds}s: ${err.getMessage()}"
            scriptCtx.sleep time: backoffSeconds, unit: 'SECONDS'
        }
    }
    throw lastError
}

def shellQuote(String value) {
    return "'${(value ?: "").replace("'", "'\"'\"'")}'"
}

def jqExtract(def scriptCtx, String rawJson, String jqExpr) {
    return scriptCtx.sh(
        script: """#!/usr/bin/env bash
set +x
printf '%s' ${shellQuote(rawJson)} | jq -r ${shellQuote(jqExpr)}
""",
        returnStdout: true
    ).trim()
}

def ecsResourceGroupArg(def ecsParams) {
    def resourceGroupId = ecsParams.RESOURCE_GROUP_ID?.trim()
    return resourceGroupId ? "--ResourceGroupId '${resourceGroupId}'" : ""
}

def psSingleQuote(String value) {
    return (value ?: '').replace("'", "''")
}

def decodeMaybeBase64(def scriptCtx, String value) {
    if (!value) {
        return ''
    }
    return scriptCtx.sh(
        script: """#!/usr/bin/env bash
set +x
printf '%s' ${shellQuote(value)} | base64 -d 2>/dev/null || printf '%s' ${shellQuote(value)}
""",
        returnStdout: true
    ).trim()
}

def extractJsonObject(String value) {
    def text = value ?: ''
    def start = text.indexOf('{')
    def end = text.lastIndexOf('}')
    if (start < 0 || end < start) {
        return ''
    }
    return text.substring(start, end + 1)
}

def aliyunServicePowerShellPrefix() {
    return '''function Resolve-AliyunService {
    $Command = Get-Command 'aliyun-service' -ErrorAction SilentlyContinue
    if ($Command) {
        return $Command.Source
    }

    $AssistRoot = 'C:\\ProgramData\\aliyun\\assist'
    $CandidateNames = @('aliyun-service.exe', 'aliyun-service', 'aliyun_assist_service.exe')
    foreach ($Name in $CandidateNames) {
        $Candidate = Join-Path $AssistRoot $Name
        if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
            return $Candidate
        }
    }

    if (Test-Path -LiteralPath $AssistRoot -PathType Container) {
        $Found = Get-ChildItem -LiteralPath $AssistRoot -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $CandidateNames -contains $_.Name } |
            Sort-Object FullName |
            Select-Object -First 1
        if ($Found) {
            return $Found.FullName
        }
    }

    throw "aliyun-service executable not found. Cloud Assistant Agent must be >= 2.x.3.398 and include data-encryption support."
}

$AliyunService = Resolve-AliyunService
'''
}

def aliyunServiceDataEncryptionPowerShell(String arguments) {
    return '''$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

__ALIYUN_SERVICE_PREFIX__

& $AliyunService data-encryption __ARGUMENTS__
if ($LASTEXITCODE -ne 0) {
    throw "aliyun-service data-encryption failed with exit code $LASTEXITCODE"
}
'''
        .replace('__ALIYUN_SERVICE_PREFIX__', aliyunServicePowerShellPrefix())
        .replace('__ARGUMENTS__', arguments)
}

def aliyunServiceRemoveKeyPairPowerShell(String keyPairId) {
    return '''$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

__ALIYUN_SERVICE_PREFIX__

$KeyPairId = '__KEY_PAIR_ID__'
$Help = (& $AliyunService data-encryption --help 2>&1 | Out-String)
if ($Help -notmatch 'remove-keypair') {
    Write-Host 'aliyun-service data-encryption does not support remove-keypair; skip key pair cleanup.'
    return
}

& $AliyunService data-encryption --remove-keypair -i $KeyPairId
if ($LASTEXITCODE -ne 0) {
    throw "aliyun-service data-encryption remove-keypair failed with exit code $LASTEXITCODE"
}
'''
        .replace('__ALIYUN_SERVICE_PREFIX__', aliyunServicePowerShellPrefix())
        .replace('__KEY_PAIR_ID__', psSingleQuote(keyPairId))
}

def waitForCloudAssistantInvocation(def scriptCtx, String regionId, String invokeId, int maxRetries = 60, int sleepSeconds = 5) {
    def lastDecodedOutput = ''
    for (int i = 0; i < maxRetries; i++) {
        def raw = aliyunWithRetry(scriptCtx, """
            ecs DescribeInvocationResults \
            --RegionId '${regionId}' \
            --InvokeId '${invokeId}'
        """.trim().replaceAll("\\s+", " "), 3, 15)

        def recordStatus = jqExtract(scriptCtx, raw, '.Invocation.InvocationResults.InvocationResult[0].InvokeRecordStatus // ""')
        def invocationStatus = jqExtract(scriptCtx, raw, '.Invocation.InvocationResults.InvocationResult[0].InvocationStatus // ""')
        def exitCode = jqExtract(scriptCtx, raw, '.Invocation.InvocationResults.InvocationResult[0].ExitCode // ""')
        def output = jqExtract(scriptCtx, raw, '.Invocation.InvocationResults.InvocationResult[0].Output // ""')
        def errorInfo = jqExtract(scriptCtx, raw, '.Invocation.InvocationResults.InvocationResult[0].ErrorInfo // ""')
        scriptCtx.echo "Cloud Assistant invocation ${invokeId}: record=${recordStatus ?: 'unknown'}, status=${invocationStatus ?: 'unknown'}, exitCode=${exitCode ?: 'unknown'}"

        def decodedOutput = decodeMaybeBase64(scriptCtx, output)
        if (decodedOutput && decodedOutput != lastDecodedOutput) {
            def outputDelta = decodedOutput.startsWith(lastDecodedOutput) ?
                decodedOutput.substring(lastDecodedOutput.size()) :
                (decodedOutput.size() > 4000 ? decodedOutput.substring(decodedOutput.size() - 4000) : decodedOutput)
            if (outputDelta?.trim()) {
                scriptCtx.echo "Cloud Assistant ${invokeId} output update:\n${outputDelta}"
            }
            lastDecodedOutput = decodedOutput
        }

        def recordFinished = recordStatus in ['Finished', 'Failed', 'Stopped']
        def invocationFinished = invocationStatus in ['Success', 'Failed', 'Stopped', 'Timeout', 'PartialFailed']
        if (recordFinished || invocationFinished) {
            if (invocationStatus == 'Success') {
                return decodedOutput
            }
            def decodedError = decodeMaybeBase64(scriptCtx, errorInfo)
            def outputTail = decodedOutput.size() > 4000 ? decodedOutput.substring(decodedOutput.size() - 4000) : decodedOutput
            scriptCtx.error("Cloud Assistant command failed: invokeId=${invokeId}, record=${recordStatus ?: 'unknown'}, status=${invocationStatus ?: 'unknown'}, exitCode=${exitCode ?: 'unknown'}, error=${decodedError ?: errorInfo ?: outputTail}")
        }

        scriptCtx.sleep sleepSeconds
    }
    scriptCtx.error("Cloud Assistant invocation ${invokeId} did not finish in time")
}

def runEncryptedPowerShell(def scriptCtx, def ecsParams, String instanceId, String commandName, String scriptText,
                           int timeoutSeconds = 120, int maxRetries = 30, int sleepSeconds = 5,
                           Map invocationState = null) {
    def encoded = scriptCtx.sh(
        script: """#!/usr/bin/env bash
set +x
printf '%s' ${shellQuote(scriptText)} | base64 -w0
""",
        returnStdout: true
    ).trim()

    def commandRaw = aliyun(scriptCtx, """
        ecs RunCommand \
        --RegionId '${ecsParams.REGION_ID}' \
        ${ecsResourceGroupArg(ecsParams)} \
        --InstanceId.1 '${instanceId}' \
        --Type 'RunPowerShellScript' \
        --Name '${commandName}' \
        --ContentEncoding 'Base64' \
        --Timeout '${timeoutSeconds}' \
        --CommandContent '${encoded}'
    """.trim().replaceAll("\\s+", " "))

    def commandId = jqExtract(scriptCtx, commandRaw, '.CommandId // ""')
    def invokeId = jqExtract(scriptCtx, commandRaw, '.InvokeId // ""')
    if (!invokeId) {
        scriptCtx.error("RunCommand did not return InvokeId for ${commandName}: ${commandRaw}")
    }
    if (invocationState != null) {
        invocationState.commandId = commandId
        invocationState.invokeId = invokeId
    }

    return waitForCloudAssistantInvocation(scriptCtx, ecsParams.REGION_ID, invokeId, maxRetries, sleepSeconds)
}

def writeEncryptedFileViaCloudAssistant(def scriptCtx, def ecsParams, String instanceId, String keyPairId, String publicKeyFile, String sourcePath, String targetPath, String label) {
    scriptCtx.sh "test -f ${shellQuote(sourcePath)}"
    def encryptScript = "${scriptCtx.env.CHECKOUT_DIR}/build_and_pack/scripts/encrypt_cloud_assistant_file.sh"
    def encryptedPartsJson = scriptCtx.sh(
        script: """#!/usr/bin/env bash
set +x
bash ${shellQuote(encryptScript)} ${shellQuote(sourcePath)} ${shellQuote(publicKeyFile)}
""",
        returnStdout: true
    ).trim()

    def remoteScript = '''$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

__ALIYUN_SERVICE_PREFIX__

$KeyPairId = '__KEY_PAIR_ID__'
$TargetPath = '__TARGET_PATH__'
$EncryptedPartsJson = @'
__ENCRYPTED_PARTS_JSON__
'@

$EncryptedParts = $EncryptedPartsJson | ConvertFrom-Json
$PlainBase64Builder = [System.Text.StringBuilder]::new()

foreach ($Part in $EncryptedParts) {
    $DecryptedPart = & $AliyunService data-encryption -d -i $KeyPairId -T $Part
    if ($LASTEXITCODE -ne 0) {
        throw "Decrypt encrypted file part failed with exit code $LASTEXITCODE"
    }
    if (-not $DecryptedPart) {
        throw "Decrypt encrypted file part returned empty content"
    }
    [void]$PlainBase64Builder.Append(($DecryptedPart | Out-String).Trim())
}

$TargetDir = Split-Path -Parent $TargetPath
if (-not (Test-Path -LiteralPath $TargetDir)) {
    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
}

$Bytes = [Convert]::FromBase64String($PlainBase64Builder.ToString())
[System.IO.File]::WriteAllBytes($TargetPath, $Bytes)
Write-Host "Encrypted file written: $TargetPath"
'''
        .replace('__ALIYUN_SERVICE_PREFIX__', aliyunServicePowerShellPrefix())
        .replace('__KEY_PAIR_ID__', psSingleQuote(keyPairId))
        .replace('__TARGET_PATH__', psSingleQuote(targetPath))
        .replace('__ENCRYPTED_PARTS_JSON__', encryptedPartsJson)

    runEncryptedPowerShell(scriptCtx, ecsParams, instanceId, "czur-write-${label}-${scriptCtx.env.BUILD_NUMBER}", remoteScript, 120, 30, 5)
}

def waitForInstanceRunning(def scriptCtx, String regionId, String instanceId, int maxRetries = 24, int sleepSeconds = 10) {
    for (int i = 0; i < maxRetries; i++) {
        def raw = aliyun(scriptCtx, "ecs DescribeInstanceStatus --RegionId '${regionId}' --InstanceId.1 '${instanceId}'")
        def status = jqExtract(scriptCtx, raw, '.InstanceStatuses.InstanceStatus[0].Status // ""')
        scriptCtx.echo "ECS status: ${status ?: 'unknown'}"
        if (status == "Running") {
            return
        }
        scriptCtx.sleep sleepSeconds
    }
    scriptCtx.error("ECS instance ${instanceId} did not reach Running state in time")
}

def waitForCloudAssistantReady(def scriptCtx, String regionId, String instanceId, String resourceGroupId = '', int maxRetries = 24, int sleepSeconds = 10) {
    for (int i = 0; i < maxRetries; i++) {
        def raw = scriptCtx.sh(
            script: """#!/usr/bin/env bash
set +x
tmp=\$(mktemp)
if /usr/local/bin/aliyun ecs DescribeCloudAssistantStatus --RegionId '${regionId}' --InstanceId.1 '${instanceId}' >"\${tmp}" 2>&1; then
    printf 'ALIYUN_EXIT=0\\n'
else
    code=\$?
    printf 'ALIYUN_EXIT=%s\\n' "\${code}"
fi
cat "\${tmp}"
rm -f "\${tmp}"
""",
            returnStdout: true
        ).trim()
        def firstNewline = raw.indexOf('\n')
        def exitLine = (firstNewline >= 0 ? raw.substring(0, firstNewline) : raw).trim()
        if (!exitLine.startsWith('ALIYUN_EXIT=')) {
            exitLine = 'ALIYUN_EXIT=1'
        }
        def exitCode = exitLine.replace('ALIYUN_EXIT=', '').trim()
        if (exitCode == '0') {
            def status = jqExtract(scriptCtx, extractJsonObject(raw), '.InstanceCloudAssistantStatusSet.InstanceCloudAssistantStatus[0].CloudAssistantStatus // ""')
            scriptCtx.echo "Cloud Assistant status: ${status ?: 'unknown'}"
            if (status in ["true", "Running", "Online"]) {
                return
            }
        }
        else {
            if (!raw.contains('Forbidden.RAM')) {
                scriptCtx.error("Cloud Assistant status query failed: ${raw.take(2000)}")
            }
            scriptCtx.echo "Cloud Assistant status query is not permitted by RAM; probing readiness with RunCommand instead."
            runEncryptedPowerShell(
                scriptCtx,
                [REGION_ID: regionId, RESOURCE_GROUP_ID: resourceGroupId ?: ''],
                instanceId,
                "czur-sdk-open-cloud-assistant-ready-${scriptCtx.env.BUILD_NUMBER}",
                "Write-Host 'Cloud Assistant ready'",
                30,
                12,
                5
            )
            return
        }
        scriptCtx.sleep sleepSeconds
    }
    scriptCtx.error("Cloud Assistant on instance ${instanceId} did not become ready in time")
}

def runBuild(def scriptCtx) {
    prepareWorkspace(scriptCtx)
    createInstance(scriptCtx)
    waitReady(scriptCtx)
    renderBootstrapScript(scriptCtx)
    sendSystemAuthFiles(scriptCtx)
    dispatchBuild(scriptCtx)
    waitBuildResult(scriptCtx)
    publishArtifactLocation(scriptCtx)
}

def prepareWorkspace(def scriptCtx) {
    scriptCtx.sh '''
set -e
rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}"
    '''
}

def createInstance(def scriptCtx) {
    scriptCtx.env.WINDOWS_RELEASE_AT = scriptCtx.sh(
        script: "date -u +%Y-%m-%dT%H:%M:00Z --date='+${scriptCtx.env.WINDOWS_AUTO_RELEASE_MINUTES} minute'",
        returnStdout: true
    ).trim()

    def winParams = [
        REGION_ID: scriptCtx.env.WINDOWS_REGION_ID,
        RESOURCE_GROUP_ID: scriptCtx.env.WINDOWS_RESOURCE_GROUP_ID
    ]
    def resourceGroupArg = ecsResourceGroupArg(winParams)
    def windowsVSwitchIds = scriptCtx.env.WINDOWS_VSWITCH_IDS.split(',').collect { it.trim() }.findAll { it }
    if (windowsVSwitchIds.isEmpty()) {
        scriptCtx.error 'No Windows VSwitchId candidates are available'
    }

    def instanceId = ''
    def lastCreateError = ''
    for (int i = 0; i < windowsVSwitchIds.size(); i++) {
        def vSwitchId = windowsVSwitchIds[i]
        scriptCtx.echo "Trying Windows VSwitchId [${i + 1}/${windowsVSwitchIds.size()}]: ${vSwitchId}"
        try {
            def createRaw = aliyun(scriptCtx, """
                ecs RunInstances \
                --RegionId '${scriptCtx.env.WINDOWS_REGION_ID}' \
                --ImageId '${scriptCtx.env.WINDOWS_IMAGE_ID}' \
                --InstanceType '${scriptCtx.env.WINDOWS_INSTANCE_TYPE}' \
                --SecurityGroupId '${scriptCtx.env.WINDOWS_SECURITY_GROUP_ID}' \
                --VSwitchId '${vSwitchId}' \
                --InstanceChargeType 'PostPaid' \
                --SystemDisk.Category 'cloud_essd' \
                --SystemDisk.Size '120' \
                --HostName 'sdkopen-${scriptCtx.env.BUILD_NUMBER}' \
                --PasswordInherit 'true' \
                --SpotStrategy 'SpotAsPriceGo' \
                --InternetChargeType 'PayByTraffic' \
                --InternetMaxBandwidthOut '100' \
                --AutoReleaseTime '${scriptCtx.env.WINDOWS_RELEASE_AT}' \
                ${resourceGroupArg}
            """.trim().replaceAll("\\s+", " "))

            instanceId = jqExtract(scriptCtx, createRaw, '.InstanceIdSets.InstanceIdSet[0] // ""')
            if (instanceId) {
                scriptCtx.env.WINDOWS_VSWITCH_ID = vSwitchId
                scriptCtx.echo "Windows ECS instance created with VSwitchId=${vSwitchId}"
                break
            }
            lastCreateError = "RunInstances returned no instance id: ${createRaw}"
            scriptCtx.echo "VSwitchId=${vSwitchId} returned no instance id, trying next candidate"
        }
        catch (err) {
            lastCreateError = err.getMessage()
            scriptCtx.echo "VSwitchId=${vSwitchId} failed: ${lastCreateError}"
        }
    }

    if (!instanceId) {
        scriptCtx.error("All Windows VSwitchId candidates failed. Last error: ${lastCreateError}")
    }
    scriptCtx.env.WINDOWS_INSTANCE_ID = instanceId
    scriptCtx.echo "Created Windows ECS instance: ${scriptCtx.env.WINDOWS_INSTANCE_ID}"
}

def waitReady(def scriptCtx) {
    waitForInstanceRunning(scriptCtx, scriptCtx.env.WINDOWS_REGION_ID, scriptCtx.env.WINDOWS_INSTANCE_ID)
    waitForCloudAssistantReady(scriptCtx, scriptCtx.env.WINDOWS_REGION_ID, scriptCtx.env.WINDOWS_INSTANCE_ID, scriptCtx.env.WINDOWS_RESOURCE_GROUP_ID)
}

def renderBootstrapScript(def scriptCtx) {
    def template = scriptCtx.readFile("${scriptCtx.env.CHECKOUT_DIR}/build_and_pack/scripts/windows/remote_sdk_open_bootstrap.ps1")
    def rendered = template
        .replace('__WINDOWS_WORKSPACE__', scriptCtx.env.WINDOWS_WORKSPACE)
        .replace('__GIT_REPO_URL__', scriptCtx.env.GIT_REPO_URL)
        .replace('__GIT_REF__', scriptCtx.env.GIT_REF ?: '')
        .replace('__GIT_COMMIT__', scriptCtx.env.GIT_COMMIT_SHA)
        .replace('__ARTIFACT_OSS_URI__', scriptCtx.env.WINDOWS_ARTIFACT_OSS_PREFIX_DIR)
        .replace('__SYMBOL_OSS_URI__', scriptCtx.env.WINDOWS_SYMBOL_OSS_DIR)
        .replace('__LOG_OSS_URI__', scriptCtx.env.WINDOWS_REMOTE_LOG_OSS_PREFIX)
        .replace('__FAILURE_LOG_OSS_URI__', scriptCtx.env.WINDOWS_REMOTE_FAILURE_LOG_OSS_PREFIX)
        .replace('__OSSUTIL_CONFIG_PATH__', scriptCtx.env.REMOTE_OSSUTIL_CONFIG_PATH)
        .replace('__BUILD_CONFIG__', scriptCtx.env.WINDOWS_BUILD_CONFIG)
        .replace('__WINDOWS_ARCH__', scriptCtx.env.WINDOWS_ARCH)
        .replace('__BUILD_NUMBER__', scriptCtx.env.BUILD_NUMBER ?: 'local')
        .replace('__VERIFY_INSTALLER__', scriptCtx.env.WINDOWS_VERIFY_INSTALLER)
        .replace('__WSSC_SIGNING_ENABLED__', scriptCtx.env.WINDOWS_CODE_SIGNING_ENABLED ?: 'false')
        .replace('__WSSC_BROKER_URL__', scriptCtx.env.WSSC_BROKER_URL ?: '')
        .replace('__WSSC_PROJECT__', scriptCtx.env.WSSC_PROJECT ?: '')
        .replace('__WSSC_TOKEN_PATH__', scriptCtx.env.REMOTE_WSSC_TOKEN_PATH ?: '')
        .replace('__WSSC_VERSION__', scriptCtx.env.WINDOWS_SAVE_DIR_NAME ?: '')
        .replace('__WSSC_SUBMITTED_BY__', 'jenkins')
        .replace('__WSSC_SIGN_TIMEOUT_SECONDS__', scriptCtx.env.WSSC_SIGN_TIMEOUT_SECONDS ?: '1800')
        .replace('__WSSC_SIGN_POLL_INTERVAL_SECONDS__', scriptCtx.env.WSSC_SIGN_POLL_INTERVAL_SECONDS ?: '5')
    scriptCtx.writeFile(file: scriptCtx.env.WINDOWS_REMOTE_SCRIPT_RENDERED, text: rendered)
}

def sendSystemAuthFiles(def scriptCtx) {
    def winParams = [
        REGION_ID: scriptCtx.env.WINDOWS_REGION_ID,
        RESOURCE_GROUP_ID: scriptCtx.env.WINDOWS_RESOURCE_GROUP_ID
    ]
    def remoteSystemProfileDir = scriptCtx.env.REMOTE_OSSUTIL_CONFIG_PATH.replaceAll('\\\\[^\\\\]+$', '')
    def keyPairOutput = runEncryptedPowerShell(
        scriptCtx,
        winParams,
        scriptCtx.env.WINDOWS_INSTANCE_ID,
        "czur-sdk-open-generate-encryption-key-${scriptCtx.env.BUILD_NUMBER}",
        aliyunServiceDataEncryptionPowerShell('-g --json -t 1800'),
        60,
        24,
        5
    )
    def keyPairJson = extractJsonObject(keyPairOutput)
    def keyPairId = jqExtract(scriptCtx, keyPairJson, '.id // ""')
    def publicKey = jqExtract(scriptCtx, keyPairJson, '.publicKey // ""')
    if (!keyPairId?.trim() || !publicKey?.trim()) {
        scriptCtx.error("Cloud Assistant encryption key pair output is invalid: ${keyPairOutput.take(1000)}")
    }

    def publicKeyFile = "${scriptCtx.env.WORK_DIR}/cloud_assistant_${scriptCtx.env.BUILD_NUMBER}.public.pem"
    scriptCtx.writeFile(file: publicKeyFile, text: publicKey)

    try {
        def authFiles = [
            [name: 'id_rsa', path: scriptCtx.env.JENKINS_SSH_PRIVATE_KEY_PATH, targetDir: scriptCtx.env.REMOTE_SYSTEM_SSH_DIR],
            [name: 'id_rsa.pub', path: scriptCtx.env.JENKINS_SSH_PUBLIC_KEY_PATH, targetDir: scriptCtx.env.REMOTE_SYSTEM_SSH_DIR],
            [name: '.ossutilconfig', path: scriptCtx.env.JENKINS_OSSUTIL_CONFIG_PATH, targetDir: remoteSystemProfileDir]
        ]

        authFiles.each { authFile ->
            if (!authFile.path?.trim()) {
                scriptCtx.error("Auth file path is empty for ${authFile.name}")
            }
            def targetPath = "${authFile.targetDir}\\${authFile.name}"
            writeEncryptedFileViaCloudAssistant(scriptCtx, winParams, scriptCtx.env.WINDOWS_INSTANCE_ID, keyPairId, publicKeyFile, authFile.path, targetPath, authFile.name.replaceAll('[^A-Za-z0-9_-]', '-'))
        }

        if (paramBool(scriptCtx.env.WINDOWS_CODE_SIGNING_ENABLED)) {
            scriptCtx.withCredentials([[$class: 'StringBinding', credentialsId: "${scriptCtx.env.WSSC_JENKINS_TOKEN_CREDENTIAL_ID}", variable: 'WSSC_JENKINS_TOKEN']]) {
                def remoteSystemProfileDirForToken = scriptCtx.env.REMOTE_WSSC_TOKEN_PATH.replaceAll('\\\\[^\\\\]+$', '')
                def tokenFileName = scriptCtx.env.REMOTE_WSSC_TOKEN_PATH.replaceAll('^.*\\\\', '')
                def tokenTempFile = "${scriptCtx.env.WORK_DIR}/wssc_${scriptCtx.env.BUILD_NUMBER}.token"
                try {
                    scriptCtx.sh(
                        script: """#!/usr/bin/env bash
set +x
umask 077
printf '%s' "\$WSSC_JENKINS_TOKEN" > ${shellQuote(tokenTempFile)}
"""
                    )
                    writeEncryptedFileViaCloudAssistant(
                        scriptCtx,
                        winParams,
                        scriptCtx.env.WINDOWS_INSTANCE_ID,
                        keyPairId,
                        publicKeyFile,
                        tokenTempFile,
                        "${remoteSystemProfileDirForToken}\\${tokenFileName}",
                        tokenFileName.replaceAll('[^A-Za-z0-9_-]', '-')
                    )
                }
                finally {
                    scriptCtx.sh(
                        script: """#!/usr/bin/env bash
set +x
rm -f ${shellQuote(tokenTempFile)}
"""
                    )
                }
            }
        }
    }
    finally {
        try {
            runEncryptedPowerShell(
                scriptCtx,
                winParams,
                scriptCtx.env.WINDOWS_INSTANCE_ID,
                "czur-sdk-open-remove-encryption-key-${scriptCtx.env.BUILD_NUMBER}",
                aliyunServiceRemoveKeyPairPowerShell(keyPairId),
                60,
                12,
                5
            )
        }
        catch (err) {
            scriptCtx.echo "Cloud Assistant encryption key pair cleanup failed: ${err.getMessage()}"
        }
    }
}

def dispatchBuild(def scriptCtx) {
    def winParams = [
        REGION_ID: scriptCtx.env.WINDOWS_REGION_ID,
        RESOURCE_GROUP_ID: scriptCtx.env.WINDOWS_RESOURCE_GROUP_ID
    ]
    def resourceGroupArg = ecsResourceGroupArg(winParams)
    def encoded = scriptCtx.sh(
        script: """#!/usr/bin/env bash
set +x
base64 -w0 ${shellQuote(scriptCtx.env.WINDOWS_REMOTE_SCRIPT_RENDERED)}
""",
        returnStdout: true
    ).trim()
    def commandRaw = aliyun(scriptCtx, """
        ecs RunCommand \
        --RegionId '${scriptCtx.env.WINDOWS_REGION_ID}' \
        ${resourceGroupArg} \
        --InstanceId.1 '${scriptCtx.env.WINDOWS_INSTANCE_ID}' \
        --Type 'RunPowerShellScript' \
        --Name 'czur-sdk-open-windows-build-${scriptCtx.env.BUILD_NUMBER}' \
        --ContentEncoding 'Base64' \
        --Timeout '${scriptCtx.env.WINDOWS_REMOTE_COMMAND_TIMEOUT_SECONDS}' \
        --CommandContent '${encoded}'
    """.trim().replaceAll("\\s+", " "))

    scriptCtx.env.WINDOWS_COMMAND_ID = jqExtract(scriptCtx, commandRaw, '.CommandId // ""')
    scriptCtx.env.WINDOWS_INVOKE_ID = jqExtract(scriptCtx, commandRaw, '.InvokeId // ""')
    if (!scriptCtx.env.WINDOWS_COMMAND_ID || !scriptCtx.env.WINDOWS_INVOKE_ID) {
        scriptCtx.error("RunCommand did not return Windows command/invoke id: ${commandRaw}")
    }
    scriptCtx.echo "Dispatched sdk-open Windows build: COMMAND_ID=${scriptCtx.env.WINDOWS_COMMAND_ID}, INVOKE_ID=${scriptCtx.env.WINDOWS_INVOKE_ID}"
    scriptCtx.echo "sdk-open Windows artifact OSS prefix: ${scriptCtx.env.WINDOWS_ARTIFACT_OSS_PREFIX_DIR}"
    scriptCtx.echo "sdk-open Windows failure log OSS prefix: ${scriptCtx.env.WINDOWS_REMOTE_FAILURE_LOG_OSS_PREFIX}"
}

def waitBuildResult(def scriptCtx) {
    scriptCtx.timeout(time: 120, unit: 'MINUTES') {
        scriptCtx.waitUntil(initialRecurrencePeriod: 10000) {
            def raw = aliyunWithRetry(scriptCtx, """
                ecs DescribeInvocationResults \
                --RegionId '${scriptCtx.env.WINDOWS_REGION_ID}' \
                --InvokeId '${scriptCtx.env.WINDOWS_INVOKE_ID}'
            """.trim().replaceAll("\\s+", " "), 3, 15)
            def recordStatus = jqExtract(scriptCtx, raw, '.Invocation.InvocationResults.InvocationResult[0].InvokeRecordStatus // ""')
            def invocationStatus = jqExtract(scriptCtx, raw, '.Invocation.InvocationResults.InvocationResult[0].InvocationStatus // ""')
            def exitCode = jqExtract(scriptCtx, raw, '.Invocation.InvocationResults.InvocationResult[0].ExitCode // ""')
            def output = jqExtract(scriptCtx, raw, '.Invocation.InvocationResults.InvocationResult[0].Output // ""')
            def errorInfo = jqExtract(scriptCtx, raw, '.Invocation.InvocationResults.InvocationResult[0].ErrorInfo // ""')
            scriptCtx.echo "sdk-open Windows invocation status: record=${recordStatus ?: 'unknown'}, status=${invocationStatus ?: 'unknown'}, exitCode=${exitCode ?: 'unknown'}"

            def recordFinished = recordStatus in ['Finished', 'Failed', 'Stopped']
            def invocationFinished = invocationStatus in ['Success', 'Failed', 'Stopped', 'Timeout', 'PartialFailed']
            if (recordFinished || invocationFinished) {
                if (invocationStatus == 'Success') {
                    return true
                }
                def decodedOutput = decodeMaybeBase64(scriptCtx, output)
                def decodedError = decodeMaybeBase64(scriptCtx, errorInfo)
                if (decodedOutput?.trim()) {
                    scriptCtx.echo "sdk-open Windows command output:\n${decodedOutput.take(12000)}"
                }
                if (decodedError?.trim()) {
                    scriptCtx.echo "sdk-open Windows command error:\n${decodedError.take(12000)}"
                }
                scriptCtx.echo "sdk-open Windows failure log OSS prefix: ${scriptCtx.env.WINDOWS_REMOTE_FAILURE_LOG_OSS_PREFIX}"
                scriptCtx.error("sdk-open Windows build failed: invokeId=${scriptCtx.env.WINDOWS_INVOKE_ID}, record=${recordStatus ?: 'unknown'}, status=${invocationStatus ?: 'unknown'}, exitCode=${exitCode ?: 'unknown'}, error=${decodedError ?: errorInfo ?: 'unknown'}")
            }

            scriptCtx.sleep 60
            return false
        }
    }
}

def publishArtifactLocation(def scriptCtx) {
    scriptCtx.echo "sdk-open Windows artifacts uploaded to OSS: ${scriptCtx.env.WINDOWS_ARTIFACT_OSS_PREFIX_DIR}"
    scriptCtx.echo "sdk-open Windows symbols uploaded to OSS: ${scriptCtx.env.WINDOWS_SYMBOL_OSS_DIR}"
    scriptCtx.echo "sdk-open Windows remote logs uploaded to OSS prefix: ${scriptCtx.env.WINDOWS_REMOTE_LOG_OSS_PREFIX}"
}

def cleanupInstance(def scriptCtx, String currentResult) {
    if (!scriptCtx.env.WINDOWS_INSTANCE_ID?.trim()) {
        scriptCtx.echo '未创建 Windows 临时 ECS，跳过实例删除'
        return
    }

    boolean keepWindowsInstance = (currentResult != 'SUCCESS' && paramBool(scriptCtx.env.KEEP_ECS_ON_FAILURE))
    if (keepWindowsInstance) {
        scriptCtx.echo "构建失败，按参数保留 Windows 临时 ECS，不执行手动删除: ${scriptCtx.env.WINDOWS_INSTANCE_ID}"
        scriptCtx.echo "Windows 实例仍会按 AutoReleaseTime 自动释放: ${scriptCtx.env.WINDOWS_RELEASE_AT}"
        return
    }

    scriptCtx.echo "删除 Windows 临时 ECS: ${scriptCtx.env.WINDOWS_INSTANCE_ID}"
    boolean deletedWindows = false
    for (int i = 1; i <= 12; i++) {
        int deleteWindowsExitCode = scriptCtx.sh(
            script: "/usr/local/bin/aliyun ecs DeleteInstance --Force true --RegionId ${scriptCtx.env.WINDOWS_REGION_ID} --InstanceId ${scriptCtx.env.WINDOWS_INSTANCE_ID}",
            returnStatus: true
        )
        if (deleteWindowsExitCode == 0) {
            scriptCtx.echo 'Windows 临时 ECS 已删除'
            deletedWindows = true
            break
        }
        scriptCtx.echo "[${i}/12] Windows 临时 ECS 删除失败，等待后重试"
        scriptCtx.sleep time: 10, unit: 'SECONDS'
    }
    if (!deletedWindows) {
        scriptCtx.echo "删除 Windows 临时 ECS 失败，请手动清理: aliyun ecs DeleteInstance --Force true --RegionId ${scriptCtx.env.WINDOWS_REGION_ID} --InstanceId ${scriptCtx.env.WINDOWS_INSTANCE_ID}"
    }
}

return this
