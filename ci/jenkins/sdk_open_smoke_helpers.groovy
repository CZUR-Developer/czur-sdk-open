def paramBool(def value) {
    if (value == null) {
        return false
    }
    if (value instanceof Boolean) {
        return value
    }
    return value.toString().trim().toLowerCase() in ['1', 'true', 'yes', 'on']
}

def shellQuote(String value) {
    return "'${(value ?: '').replace("'", "'\"'\"'")}'"
}

def smokeApiKeyCredentialId(String target) {
    def credentialIds = [
        'linux-x86-64': 'OPEN_SDK_API_KEY_0001',
        'linux-arm64': 'OPEN_SDK_API_KEY_0002',
        'windows-x86': 'OPEN_SDK_API_KEY_0003',
        'windows-x64': 'OPEN_SDK_API_KEY_0004'
    ]
    def credentialId = credentialIds[target]
    if (!credentialId) {
        throw new IllegalArgumentException("No SDK Open smoke API Key credential is configured for ${target}")
    }
    return credentialId
}

def targetAutomationEnvFile(def scriptCtx, String target) {
    return "${scriptCtx.env.SMOKE_WORK_DIR}/smarteye-${target}.env"
}

def writeTargetAutomationEnv(def scriptCtx, String target) {
    def credentialId = smokeApiKeyCredentialId(target)
    def targetEnvFile = targetAutomationEnvFile(scriptCtx, target)
    scriptCtx.withCredentials([[
        $class: 'StringBinding',
        credentialsId: credentialId,
        variable: 'SDK_OPEN_SMOKE_API_KEY'
    ]]) {
        scriptCtx.withEnv(["SDK_OPEN_SMOKE_TARGET_ENV_FILE=${targetEnvFile}"]) {
            scriptCtx.sh '''#!/usr/bin/env bash
set -euo pipefail
umask 077
test -s "${SMOKE_AUTOMATION_ENV_FILE}"
case "${SDK_OPEN_SMOKE_API_KEY}" in
    '')
        echo 'Jenkins SDK Open smoke API Key credential is empty' >&2
        exit 1
        ;;
    *$'\n'*|*$'\r'*)
        echo 'Jenkins SDK Open smoke API Key credential must contain exactly one line' >&2
        exit 1
        ;;
esac
cp "${SMOKE_AUTOMATION_ENV_FILE}" "${SDK_OPEN_SMOKE_TARGET_ENV_FILE}"
printf '\nSDK_API_KEY=%s\n' "${SDK_OPEN_SMOKE_API_KEY}" >> "${SDK_OPEN_SMOKE_TARGET_ENV_FILE}"
chmod 0600 "${SDK_OPEN_SMOKE_TARGET_ENV_FILE}"
'''
        }
    }
    scriptCtx.echo "Prepared isolated SDK Open smoke credentials for ${target} from Jenkins credential ${credentialId}"
    return targetEnvFile
}

def removeTargetAutomationEnv(def scriptCtx, String targetEnvFile) {
    scriptCtx.sh(
        script: "rm -f ${shellQuote(targetEnvFile)}",
        returnStatus: true
    )
}

def prepareAutomationSuite(def scriptCtx) {
    scriptCtx.env.SMARTEYE_REPO_URL = 'git@gitee.com:czur_dl/SmartEye.git'
    scriptCtx.env.SMARTEYE_BRANCH = '3.2AI'
    scriptCtx.env.SMARTEYE_CHECKOUT_DIR = "${scriptCtx.env.SMOKE_WORK_DIR}/SmartEye"
    scriptCtx.env.SMARTEYE_AUTOMATION_DIR = "${scriptCtx.env.SMARTEYE_CHECKOUT_DIR}/src/workspace/api-output/tests/czur-sdk-open-api-automation"
    scriptCtx.env.SMOKE_AUTOMATION_ARCHIVE = "${scriptCtx.env.SMOKE_WORK_DIR}/smarteye-automation.tar.gz"
    scriptCtx.env.SMOKE_AUTOMATION_SHA256_FILE = "${scriptCtx.env.SMOKE_WORK_DIR}/smarteye-automation.tar.gz.sha256"
    scriptCtx.env.SMOKE_AUTOMATION_ENV_FILE = "${scriptCtx.env.SMOKE_WORK_DIR}/smarteye.env"
    scriptCtx.env.SMOKE_INPUT_OSS_DIR = "${scriptCtx.env.OSS_ARTIFACT_DIR}smoke-tests/input/"

    scriptCtx.sh '''
set -e
rm -rf "${SMOKE_WORK_DIR}" "${SMOKE_REPORT_STAGING_DIR}"
mkdir -p "${SMOKE_WORK_DIR}" "${SMOKE_REPORT_STAGING_DIR}"

git clone --depth 1 --branch "${SMARTEYE_BRANCH}" --single-branch \
    "${SMARTEYE_REPO_URL}" "${SMARTEYE_CHECKOUT_DIR}"

test -f "${SMARTEYE_AUTOMATION_DIR}/package-lock.json"
test -f "${SMARTEYE_AUTOMATION_DIR}/scripts/bootstrap-linux.sh"
test -f "${SMARTEYE_AUTOMATION_DIR}/scripts/bootstrap-windows.ps1"
test -f "${SMARTEYE_AUTOMATION_DIR}/.env"

umask 077
awk '
    /^[[:space:]]*(export[[:space:]]+)?SDK_API_KEY[[:space:]]*=/ { next }
    { print }
' "${SMARTEYE_AUTOMATION_DIR}/.env" > "${SMOKE_AUTOMATION_ENV_FILE}"
chmod 0600 "${SMOKE_AUTOMATION_ENV_FILE}"
rm -f "${SMARTEYE_AUTOMATION_DIR}/.env"
tar -czf "${SMOKE_AUTOMATION_ARCHIVE}" \
    --exclude='./.env' \
    --exclude='./artifacts' \
    --exclude='./node_modules' \
    -C "${SMARTEYE_AUTOMATION_DIR}" .
sha256sum "${SMOKE_AUTOMATION_ARCHIVE}" > "${SMOKE_AUTOMATION_SHA256_FILE}"

ossutil cp "${SMOKE_AUTOMATION_ARCHIVE}" "${SMOKE_INPUT_OSS_DIR}smarteye-automation.tar.gz" \
    -f --config-file ~/oss/.ossutilnewconfig
ossutil cp "${SMOKE_AUTOMATION_SHA256_FILE}" "${SMOKE_INPUT_OSS_DIR}smarteye-automation.tar.gz.sha256" \
    -f --config-file ~/oss/.ossutilnewconfig
    '''

    scriptCtx.env.SMARTEYE_COMMIT = scriptCtx.sh(
        script: 'git -C "${SMARTEYE_CHECKOUT_DIR}" rev-parse HEAD',
        returnStdout: true
    ).trim()
    scriptCtx.env.SMOKE_AUTOMATION_SHA256 = scriptCtx.sh(
        script: 'sha256sum "${SMOKE_AUTOMATION_ARCHIVE}" | awk \'{print $1}\'',
        returnStdout: true
    ).trim()
    scriptCtx.env.SMOKE_AUTOMATION_OSS_URI = "${scriptCtx.env.SMOKE_INPUT_OSS_DIR}smarteye-automation.tar.gz"
    scriptCtx.echo "SmartEye smoke suite prepared: branch=${scriptCtx.env.SMARTEYE_BRANCH}, commit=${scriptCtx.env.SMARTEYE_COMMIT}, archiveSha256=${scriptCtx.env.SMOKE_AUTOMATION_SHA256}"
}

def runAllowingFailure(def scriptCtx, String label, Closure action) {
    try {
        action.call()
    }
    catch (err) {
        scriptCtx.env.SDK_OPEN_SMOKE_FAILURE_DETECTED = 'true'
        def currentDescription = scriptCtx.currentBuild.description ?: ''
        if (currentDescription.contains('SDK smoke: ALLOW_FAILURE')) {
            scriptCtx.currentBuild.description = currentDescription.replace(
                'SDK smoke: ALLOW_FAILURE',
                'SDK smoke: FAILED (allowed)'
            )
        }
        scriptCtx.echo "${label} failed but release will continue because SKIP_SDK_OPEN_SMOKE_TEST=allow-failure: ${err.getMessage()}"
        scriptCtx.catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE') {
            throw err
        }
    }
}

def uploadReportDirectory(def scriptCtx, String localReportDir, String target) {
    def reportOssDir = "${scriptCtx.env.OSS_ARTIFACT_DIR}smoke-tests/${target}/"
    scriptCtx.sh """#!/usr/bin/env bash
set -euo pipefail
cd ${shellQuote(localReportDir)}
test -n \"\$(find . -type f -print -quit)\"
while IFS= read -r -d '' report_file; do
    relative_path=\"\${report_file#./}\"
    ossutil cp \"\${report_file}\" \"${reportOssDir}\${relative_path}\" \\
        -f --config-file ~/oss/.ossutilnewconfig
done < <(find . -type f -print0)
"""
    scriptCtx.echo "SDK Open ${target} smoke report uploaded: ${reportOssDir}"
}

def writeLinuxInfrastructureFailureReport(def scriptCtx, String target, String localContentDir,
                                          String packagePath, int exitCode, String phase) {
    def packageName = packagePath.tokenize('/').last()
    def packageSha256 = scriptCtx.sh(
        script: "sha256sum ${shellQuote(packagePath)} | cut -d ' ' -f 1",
        returnStdout: true
    ).trim()
    def finishedAt = scriptCtx.sh(
        script: "date -u '+%Y-%m-%dT%H:%M:%SZ'",
        returnStdout: true
    ).trim()

    scriptCtx.sh "mkdir -p ${shellQuote(localContentDir)}"
    scriptCtx.writeFile(
        file: "${localContentDir}/smoke-test.log",
        text: "SDK Open ${target} smoke infrastructure failure during ${phase}; exit code=${exitCode}.\n" +
            'The remote smoke report could not be collected. Inspect the Jenkins console and ECS lifecycle logs.\n'
    )
    scriptCtx.writeFile(
        file: "${localContentDir}/metadata.json",
        text: """{
  \"target\": \"${target}\",
  \"sdk_version\": \"${scriptCtx.env.SDK_OPEN_VERSION}\",
  \"sdk_build\": \"${scriptCtx.env.SDK_OPEN_BUILD}\",
  \"sdk_package\": \"${packageName}\",
  \"sdk_package_sha256\": \"${packageSha256}\",
  \"smarteye_branch\": \"${scriptCtx.env.SMARTEYE_BRANCH}\",
  \"smarteye_commit\": \"${scriptCtx.env.SMARTEYE_COMMIT}\",
  \"finished_at\": \"${finishedAt}\",
  \"exit_code\": ${exitCode},
  \"status\": \"infrastructure_failed\",
  \"failure_phase\": \"${phase}\"
}
"""
    )
}

def runLinuxSmoke(def scriptCtx, def linuxTargets, String target) {
    def instanceIp = linuxTargets.readState(scriptCtx, target, 'instance-ip')
    if (!instanceIp?.trim()) {
        scriptCtx.error "Missing Linux smoke ECS IP for ${target}"
    }

    def packagePath = scriptCtx.sh(
        script: """
set -e
find ${shellQuote("${scriptCtx.env.LINUX_ARTIFACT_STAGING_DIR}/${target}")} -maxdepth 1 -type f -name 'sdk-open_*.deb' -print
""",
        returnStdout: true
    ).trim().split('\n').findAll { it?.trim() }
    if (packagePath.size() != 1) {
        scriptCtx.error "Expected exactly one SDK Open package for ${target}, found ${packagePath.size()}"
    }

    def remoteWorkDir = "/tmp/czur-sdk-open-smoke-${scriptCtx.env.BUILD_NUMBER}-${target}"
    def remotePackage = "${remoteWorkDir}/${packagePath[0].tokenize('/').last()}"
    def remoteArchive = "${remoteWorkDir}/smarteye-automation.tar.gz"
    def remoteEnv = "${remoteWorkDir}/smarteye.env"
    def remoteScript = "${scriptCtx.env.REMOTE_CHECKOUT_DIR}/src/sdk_open/ci/scripts/jenkins/run_sdk_open_linux_smoke.sh"
    def localTargetDir = "${scriptCtx.env.SMOKE_REPORT_STAGING_DIR}/${target}"
    def localReportArchive = "${localTargetDir}/smoke-report-${target}.tar.gz"
    def targetEnvFile = targetAutomationEnvFile(scriptCtx, target)

    int transferStatus = 1
    try {
        writeTargetAutomationEnv(scriptCtx, target)
        transferStatus = scriptCtx.sh(
            script: """
set -e
rm -rf ${shellQuote(localTargetDir)}
mkdir -p ${shellQuote(localTargetDir)}
ssh ${scriptCtx.env.SSH_OPTS} root@${instanceIp} 'rm -rf ${remoteWorkDir} && mkdir -p ${remoteWorkDir}'
scp ${scriptCtx.env.SSH_OPTS} ${shellQuote(packagePath[0])} root@${instanceIp}:${shellQuote(remotePackage)}
scp ${scriptCtx.env.SSH_OPTS} ${shellQuote(scriptCtx.env.SMOKE_AUTOMATION_ARCHIVE)} root@${instanceIp}:${shellQuote(remoteArchive)}
scp ${scriptCtx.env.SSH_OPTS} ${shellQuote(targetEnvFile)} root@${instanceIp}:${shellQuote(remoteEnv)}
""",
            returnStatus: true
        )
    }
    finally {
        removeTargetAutomationEnv(scriptCtx, targetEnvFile)
    }

    int smokeStatus = transferStatus
    if (transferStatus == 0) {
        smokeStatus = scriptCtx.sh(
            script: """
ssh ${scriptCtx.env.SSH_OPTS} root@${instanceIp} \\
    /usr/bin/bash ${shellQuote(remoteScript)} \\
    ${shellQuote(target)} \\
    ${shellQuote(remoteWorkDir)} \\
    ${shellQuote(remotePackage)} \\
    ${shellQuote(remoteArchive)} \\
    ${shellQuote(remoteEnv)} \\
    ${shellQuote(scriptCtx.env.SMOKE_AUTOMATION_SHA256)} \\
    ${shellQuote(scriptCtx.env.SDK_OPEN_VERSION)} \\
    ${shellQuote(scriptCtx.env.SDK_OPEN_BUILD)} \\
    ${shellQuote(scriptCtx.env.SMARTEYE_COMMIT)}
""",
            returnStatus: true
        )
    }

    int reportCollectStatus = 1
    if (transferStatus == 0) {
        reportCollectStatus = scriptCtx.sh(
            script: """
set -e
for attempt in 1 2 3; do
    if scp ${scriptCtx.env.SSH_OPTS} root@${instanceIp}:${shellQuote("${remoteWorkDir}/smoke-report-${target}.tar.gz")} ${shellQuote(localReportArchive)}; then
        break
    fi
    if [ \"\${attempt}\" -eq 3 ]; then
        exit 1
    fi
    sleep 5
done
test -s ${shellQuote(localReportArchive)}
mkdir -p ${shellQuote("${localTargetDir}/content")}
tar -xzf ${shellQuote(localReportArchive)} -C ${shellQuote("${localTargetDir}/content")}
""",
            returnStatus: true
        )
    }

    def localContentDir = "${localTargetDir}/content"
    if (reportCollectStatus != 0) {
        scriptCtx.sh "rm -rf ${shellQuote(localContentDir)}"
        def failurePhase = transferStatus == 0 ? 'report_collection' : 'input_transfer'
        writeLinuxInfrastructureFailureReport(
            scriptCtx, target, localContentDir, packagePath[0], smokeStatus, failurePhase
        )
    }
    int secretCleanupStatus = scriptCtx.sh(
        script: "ssh ${scriptCtx.env.SSH_OPTS} root@${instanceIp} rm -f ${shellQuote(remoteEnv)} ${shellQuote("${remoteWorkDir}/automation/.env")}",
        returnStatus: true
    )
    if (secretCleanupStatus != 0) {
        scriptCtx.echo "Unable to confirm remote SmartEye .env cleanup for ${target}; ECS cleanup remains the fallback"
    }
    uploadReportDirectory(scriptCtx, localContentDir, target)

    if (smokeStatus != 0) {
        scriptCtx.error "SDK Open ${target} smoke failed with exit code ${smokeStatus}. Report: ${scriptCtx.env.OSS_ARTIFACT_DIR}smoke-tests/${target}/"
    }
    if (reportCollectStatus != 0) {
        scriptCtx.error "SDK Open ${target} smoke report collection failed. Infrastructure report: ${scriptCtx.env.OSS_ARTIFACT_DIR}smoke-tests/${target}/"
    }
    scriptCtx.echo "SDK Open ${target} smoke passed"
}

def windowsSmokeStateFile(def scriptCtx, String architecture, String name) {
    return "${scriptCtx.env.SMOKE_WORK_DIR}/windows-${architecture}-${name}.txt"
}

def writeWindowsSmokeState(def scriptCtx, String architecture, String name, String value) {
    scriptCtx.writeFile(file: windowsSmokeStateFile(scriptCtx, architecture, name), text: "${value ?: ''}\n")
}

def readWindowsSmokeState(def scriptCtx, String architecture, String name) {
    def path = windowsSmokeStateFile(scriptCtx, architecture, name)
    return scriptCtx.sh(
        script: "test -f ${shellQuote(path)} && sed -n '1p' ${shellQuote(path)} || true",
        returnStdout: true
    ).trim()
}

def jsonEscape(String value) {
    return (value ?: '')
        .replace('\\', '\\\\')
        .replace('"', '\\"')
        .replace('\r', '\\r')
        .replace('\n', '\\n')
        .replace('\t', '\\t')
}

def writeWindowsInfrastructureFailureReport(def scriptCtx, String target, String architecture,
                                            String instanceId, Map invocationState, def failure) {
    def reportOssUri = "${scriptCtx.env.OSS_ARTIFACT_DIR}smoke-tests/${target}/"
    def remoteMetadataUri = "${reportOssUri}metadata.json"
    int metadataExists = scriptCtx.sh(
        script: """#!/usr/bin/env bash
set +x
ossutil stat ${shellQuote(remoteMetadataUri)} --config-file ~/oss/.ossutilnewconfig >/dev/null 2>&1
""",
        returnStatus: true
    )
    if (metadataExists == 0) {
        scriptCtx.echo "Windows ${architecture} smoke report already exists; skip Jenkins fallback report"
        return
    }

    def failureText = failure?.getMessage() ?: failure?.toString() ?: 'Unknown Windows smoke infrastructure failure'
    if (failureText.size() > 12000) {
        failureText = failureText.substring(0, 12000)
    }
    def finishedAt = scriptCtx.sh(
        script: "date -u '+%Y-%m-%dT%H:%M:%SZ'",
        returnStdout: true
    ).trim()
    def failurePhase = invocationState?.invokeId ? 'cloud_assistant_command' : 'windows_smoke_setup'
    def localContentDir = "${scriptCtx.env.SMOKE_REPORT_STAGING_DIR}/${target}/jenkins-fallback"
    scriptCtx.sh "rm -rf ${shellQuote(localContentDir)} && mkdir -p ${shellQuote(localContentDir)}"
    scriptCtx.writeFile(
        file: "${localContentDir}/smoke-test.log",
        text: """SDK Open ${target} smoke infrastructure failure.
failure_phase=${failurePhase}
ecs_instance_id=${instanceId ?: ''}
cloud_assistant_command_id=${invocationState?.commandId ?: ''}
cloud_assistant_invoke_id=${invocationState?.invokeId ?: ''}
failure=${failureText}
The remote process did not publish a complete report. Inspect the Jenkins console and retained ECS when available.
"""
    )
    scriptCtx.writeFile(
        file: "${localContentDir}/metadata.json",
        text: """{
  \"target\": \"${jsonEscape(target)}\",
  \"architecture\": \"${jsonEscape(architecture)}\",
  \"sdk_version\": \"${jsonEscape(scriptCtx.env.SDK_OPEN_VERSION)}\",
  \"sdk_build\": \"${jsonEscape(scriptCtx.env.SDK_OPEN_BUILD)}\",
  \"sdk_stage\": \"${jsonEscape(scriptCtx.env.SDK_OPEN_STAGE)}\",
  \"smarteye_branch\": \"${jsonEscape(scriptCtx.env.SMARTEYE_BRANCH)}\",
  \"smarteye_commit\": \"${jsonEscape(scriptCtx.env.SMARTEYE_COMMIT)}\",
  \"finished_at\": \"${jsonEscape(finishedAt)}\",
  \"status\": \"infrastructure_failed\",
  \"failure_phase\": \"${failurePhase}\",
  \"ecs_instance_id\": \"${jsonEscape(instanceId)}\",
  \"cloud_assistant_command_id\": \"${jsonEscape(invocationState?.commandId)}\",
  \"cloud_assistant_invoke_id\": \"${jsonEscape(invocationState?.invokeId)}\",
  \"failure\": \"${jsonEscape(failureText)}\"
}
"""
    )
    uploadReportDirectory(scriptCtx, localContentDir, target)
    scriptCtx.echo "Uploaded Jenkins fallback report for Windows ${architecture} smoke failure: ${reportOssUri}"
}

// Windows ECS host names are limited to 15 characters. Keep the build suffix
// short while retaining enough information to distinguish recent runs and
// architectures (for example: sdk-131-x86).
def windowsSmokeHostName(def scriptCtx, String architecture) {
    def buildNumber = "${scriptCtx.env.BUILD_NUMBER ?: 'local'}"
    def compactBuildNumber = buildNumber.size() > 6
        ? buildNumber.substring(buildNumber.size() - 6)
        : buildNumber
    return "sdk-${compactBuildNumber}-${architecture}"
}

def createWindowsSmokeInstance(def scriptCtx, def windowsHelpers, String architecture) {
    def ecsParams = [
        REGION_ID: scriptCtx.env.WINDOWS_REGION_ID,
        RESOURCE_GROUP_ID: scriptCtx.env.WINDOWS_RESOURCE_GROUP_ID
    ]
    def resourceGroupArg = windowsHelpers.ecsResourceGroupArg(ecsParams)
    def vSwitchIds = scriptCtx.env.WINDOWS_VSWITCH_IDS.split(',').collect { it.trim() }.findAll { it }
    def instanceId = ''
    def lastError = ''

    for (int i = 0; i < vSwitchIds.size(); i++) {
        def vSwitchId = vSwitchIds[i]
        try {
            def raw = windowsHelpers.aliyun(scriptCtx, """
                ecs RunInstances \
                --RegionId '${scriptCtx.env.WINDOWS_REGION_ID}' \
                --ImageId '${scriptCtx.env.WINDOWS_IMAGE_ID}' \
                --InstanceType '${scriptCtx.env.WINDOWS_INSTANCE_TYPE}' \
                --SecurityGroupId '${scriptCtx.env.WINDOWS_SECURITY_GROUP_ID}' \
                --VSwitchId '${vSwitchId}' \
                --InstanceChargeType 'PostPaid' \
                --SystemDisk.Category 'cloud_essd' \
                --SystemDisk.Size '120' \
                --HostName '${windowsSmokeHostName(scriptCtx, architecture)}' \
                --PasswordInherit 'true' \
                --SpotStrategy 'SpotAsPriceGo' \
                --InternetChargeType 'PayByTraffic' \
                --InternetMaxBandwidthOut '100' \
                --AutoReleaseTime '${scriptCtx.env.SMOKE_WINDOWS_RELEASE_AT}' \
                ${resourceGroupArg}
            """.trim().replaceAll('\\s+', ' '))
            instanceId = windowsHelpers.jqExtract(scriptCtx, raw, '.InstanceIdSets.InstanceIdSet[0] // ""')
            if (instanceId) {
                writeWindowsSmokeState(scriptCtx, architecture, 'instance-id', instanceId)
                writeWindowsSmokeState(scriptCtx, architecture, 'release-at', scriptCtx.env.SMOKE_WINDOWS_RELEASE_AT)
                scriptCtx.echo "Created Windows ${architecture} smoke ECS: ${instanceId}, VSwitchId=${vSwitchId}"
                break
            }
            lastError = "RunInstances returned no instance id: ${raw}"
        }
        catch (err) {
            lastError = err.getMessage()
            scriptCtx.echo "Windows ${architecture} smoke ECS VSwitchId=${vSwitchId} failed: ${lastError}"
        }
    }
    if (!instanceId) {
        scriptCtx.error "Unable to create Windows ${architecture} smoke ECS. Last error: ${lastError}"
    }
    return instanceId
}

def sendWindowsSmokeFiles(def scriptCtx, def windowsHelpers, String architecture, String instanceId,
                          String targetEnvFile) {
    def ecsParams = [
        REGION_ID: scriptCtx.env.WINDOWS_REGION_ID,
        RESOURCE_GROUP_ID: scriptCtx.env.WINDOWS_RESOURCE_GROUP_ID
    ]
    def keyPairOutput = windowsHelpers.runEncryptedPowerShell(
        scriptCtx,
        ecsParams,
        instanceId,
        "czur-sdk-open-smoke-key-${scriptCtx.env.BUILD_NUMBER}-${architecture}",
        windowsHelpers.aliyunServiceDataEncryptionPowerShell('-g --json -t 1800'),
        60,
        24,
        5
    )
    def keyPairJson = windowsHelpers.extractJsonObject(keyPairOutput)
    def keyPairId = windowsHelpers.jqExtract(scriptCtx, keyPairJson, '.id // ""')
    def publicKey = windowsHelpers.jqExtract(scriptCtx, keyPairJson, '.publicKey // ""')
    if (!keyPairId?.trim() || !publicKey?.trim()) {
        scriptCtx.error "Cloud Assistant encryption key pair output is invalid for Windows ${architecture} smoke ECS"
    }

    def publicKeyFile = "${scriptCtx.env.SMOKE_WORK_DIR}/windows-${architecture}.public.pem"
    scriptCtx.writeFile(file: publicKeyFile, text: publicKey)
    try {
        windowsHelpers.writeEncryptedFileViaCloudAssistant(
            scriptCtx, ecsParams, instanceId, keyPairId, publicKeyFile,
            scriptCtx.env.JENKINS_OSSUTIL_CONFIG_PATH,
            scriptCtx.env.REMOTE_OSSUTIL_CONFIG_PATH,
            "smoke-ossutil-${architecture}"
        )
        windowsHelpers.writeEncryptedFileViaCloudAssistant(
            scriptCtx, ecsParams, instanceId, keyPairId, publicKeyFile,
            targetEnvFile,
            "C:\\czur-smoke\\smarteye.env",
            "smoke-env-${architecture}"
        )
    }
    finally {
        try {
            windowsHelpers.runEncryptedPowerShell(
                scriptCtx, ecsParams, instanceId,
                "czur-sdk-open-smoke-remove-key-${scriptCtx.env.BUILD_NUMBER}-${architecture}",
                windowsHelpers.aliyunServiceRemoveKeyPairPowerShell(keyPairId),
                60, 12, 5
            )
        }
        catch (err) {
            scriptCtx.echo "Windows ${architecture} smoke encryption key cleanup failed: ${err.getMessage()}"
        }
    }
}

def runWindowsSmokeArch(def scriptCtx, def windowsHelpers, String architecture) {
    def target = "windows-${architecture}"
    def reportOssUri = "${scriptCtx.env.OSS_ARTIFACT_DIR}smoke-tests/${target}/"
    def instanceId = ''
    def invocationState = [:]
    try {
        instanceId = createWindowsSmokeInstance(scriptCtx, windowsHelpers, architecture)
        windowsHelpers.waitForInstanceRunning(scriptCtx, scriptCtx.env.WINDOWS_REGION_ID, instanceId)
        windowsHelpers.waitForCloudAssistantReady(
            scriptCtx,
            scriptCtx.env.WINDOWS_REGION_ID,
            instanceId,
            scriptCtx.env.WINDOWS_RESOURCE_GROUP_ID
        )
        def targetEnvFile = targetAutomationEnvFile(scriptCtx, target)
        try {
            writeTargetAutomationEnv(scriptCtx, target)
            sendWindowsSmokeFiles(scriptCtx, windowsHelpers, architecture, instanceId, targetEnvFile)
        }
        finally {
            removeTargetAutomationEnv(scriptCtx, targetEnvFile)
        }

        def smokeScript = scriptCtx.readFile("${scriptCtx.env.CHECKOUT_DIR}/src/sdk_open/ci/scripts/windows/remote_sdk_open_smoke.ps1")
        def invocation = '''

& {
__SCRIPT_BODY__
} -Architecture '__ARCH__' `
  -WorkspaceRoot 'C:/czur-smoke/__ARCH__' `
  -AutomationArchiveOssUri '__AUTOMATION_OSS_URI__' `
  -AutomationArchiveSha256 '__AUTOMATION_SHA256__' `
  -AutomationEnvPath 'C:/czur-smoke/smarteye.env' `
  -ArtifactOssUri '__ARTIFACT_OSS_URI__' `
  -ReportOssUri '__REPORT_OSS_URI__' `
  -OssutilConfigPath '__OSSUTIL_CONFIG_PATH__' `
  -SdkVersion '__SDK_VERSION__' `
  -SdkBuild '__SDK_BUILD__' `
  -SdkStage '__SDK_STAGE__' `
  -SmartEyeCommit '__SMARTEYE_COMMIT__' `
  -SkipToolInstall `
  -BootstrapTimeoutSeconds __BOOTSTRAP_TIMEOUT_SECONDS__ `
  -TestTimeoutSeconds __TEST_TIMEOUT_SECONDS__
'''
            .replace('__SCRIPT_BODY__', smokeScript)
            .replace('__ARCH__', windowsHelpers.psSingleQuote(architecture))
            .replace('__AUTOMATION_OSS_URI__', windowsHelpers.psSingleQuote(scriptCtx.env.SMOKE_AUTOMATION_OSS_URI))
            .replace('__AUTOMATION_SHA256__', windowsHelpers.psSingleQuote(scriptCtx.env.SMOKE_AUTOMATION_SHA256))
            .replace('__ARTIFACT_OSS_URI__', windowsHelpers.psSingleQuote(scriptCtx.env.WINDOWS_ARTIFACT_OSS_PREFIX_DIR))
            .replace('__REPORT_OSS_URI__', windowsHelpers.psSingleQuote(reportOssUri))
            .replace('__OSSUTIL_CONFIG_PATH__', windowsHelpers.psSingleQuote(scriptCtx.env.REMOTE_OSSUTIL_CONFIG_PATH))
            .replace('__SDK_VERSION__', windowsHelpers.psSingleQuote(scriptCtx.env.SDK_OPEN_VERSION))
            .replace('__SDK_BUILD__', windowsHelpers.psSingleQuote(scriptCtx.env.SDK_OPEN_BUILD))
            .replace('__SDK_STAGE__', windowsHelpers.psSingleQuote(scriptCtx.env.SDK_OPEN_STAGE))
            .replace('__SMARTEYE_COMMIT__', windowsHelpers.psSingleQuote(scriptCtx.env.SMARTEYE_COMMIT))
            .replace('__BOOTSTRAP_TIMEOUT_SECONDS__', scriptCtx.env.WINDOWS_SMOKE_BOOTSTRAP_TIMEOUT_SECONDS)
            .replace('__TEST_TIMEOUT_SECONDS__', scriptCtx.env.WINDOWS_SMOKE_TEST_TIMEOUT_SECONDS)

        int smokeCommandTimeout = scriptCtx.env.WINDOWS_REMOTE_COMMAND_TIMEOUT_SECONDS.toInteger()
        int pollIntervalSeconds = 10
        int maxPollRetries = (int) Math.ceil(smokeCommandTimeout / (double) pollIntervalSeconds) + 12
        windowsHelpers.runEncryptedPowerShell(
            scriptCtx,
            [REGION_ID: scriptCtx.env.WINDOWS_REGION_ID, RESOURCE_GROUP_ID: scriptCtx.env.WINDOWS_RESOURCE_GROUP_ID],
            instanceId,
            "czur-sdk-open-smoke-${scriptCtx.env.BUILD_NUMBER}-${architecture}",
            invocation,
            smokeCommandTimeout,
            maxPollRetries,
            pollIntervalSeconds,
            invocationState
        )
    }
    catch (err) {
        try {
            writeWindowsInfrastructureFailureReport(
                scriptCtx, target, architecture, instanceId, invocationState, err
            )
        }
        catch (reportErr) {
            scriptCtx.echo "Unable to upload Jenkins fallback report for Windows ${architecture} smoke: ${reportErr.getMessage()}"
        }
        throw err
    }
    scriptCtx.echo "SDK Open Windows ${architecture} smoke passed. Report: ${reportOssUri}"
}

def runWindowsSmoke(def scriptCtx, def windowsHelpers) {
    def architectures = scriptCtx.env.WINDOWS_ARCH == 'all' ? ['x86', 'x64'] : [scriptCtx.env.WINDOWS_ARCH]
    scriptCtx.env.SMOKE_WINDOWS_RELEASE_AT = scriptCtx.sh(
        script: "date -u +%Y-%m-%dT%H:%M:00Z --date='+${scriptCtx.env.WINDOWS_AUTO_RELEASE_MINUTES} minute'",
        returnStdout: true
    ).trim()

    if (architectures.size() == 1) {
        runWindowsSmokeArch(scriptCtx, windowsHelpers, architectures[0])
        return
    }

    def branches = [:]
    architectures.each { item ->
        def architecture = item
        branches["windows ${architecture} smoke"] = {
            runWindowsSmokeArch(scriptCtx, windowsHelpers, architecture)
        }
    }
    branches.failFast = false
    scriptCtx.parallel branches
}

def cleanupWindowsSmokeInstances(def scriptCtx, def windowsHelpers, String currentResult) {
    ['x86', 'x64'].each { architecture ->
        def instanceId = readWindowsSmokeState(scriptCtx, architecture, 'instance-id')
        if (!instanceId?.trim()) {
            return
        }
        def releaseAt = readWindowsSmokeState(scriptCtx, architecture, 'release-at')
        if (currentResult != 'SUCCESS' && paramBool(scriptCtx.env.KEEP_ECS_ON_FAILURE)) {
            scriptCtx.echo "Build failed; preserving Windows ${architecture} smoke ECS until AutoReleaseTime: instance=${instanceId}, releaseAt=${releaseAt ?: 'unknown'}"
            return
        }

        boolean deleted = false
        for (int i = 1; i <= 12; i++) {
            int status = scriptCtx.sh(
                script: "/usr/local/bin/aliyun ecs DeleteInstance --Force true --RegionId ${scriptCtx.env.WINDOWS_REGION_ID} --InstanceId ${instanceId}",
                returnStatus: true
            )
            if (status == 0) {
                deleted = true
                scriptCtx.echo "Deleted Windows ${architecture} smoke ECS: ${instanceId}"
                break
            }
            scriptCtx.echo "[${i}/12] Failed to delete Windows ${architecture} smoke ECS ${instanceId}; retrying"
            scriptCtx.sleep time: 10, unit: 'SECONDS'
        }
        if (!deleted) {
            scriptCtx.echo "Unable to delete Windows ${architecture} smoke ECS; AutoReleaseTime remains the cleanup fallback: instance=${instanceId}, releaseAt=${releaseAt ?: 'unknown'}"
        }
    }
}

return this
