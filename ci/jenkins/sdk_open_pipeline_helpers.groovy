def paramBool(def value) {
    if (value == null) {
        return false
    }
    if (value instanceof Boolean) {
        return value
    }
    return value.toString().trim().toLowerCase() in ['1', 'true', 'yes', 'on']
}

def paramString(def scriptCtx, String name, String defaultValue = '') {
    def value = scriptCtx.params[name]
    if (value == null) {
        return defaultValue
    }
    def text = value.toString().trim()
    return text ? text : defaultValue
}

def configureBuildMetadata(def scriptCtx) {
    def releaseModeName = paramBool(scriptCtx.env.IS_OFFICIAL_RELEASE) ? 'official-release' : 'internal-test'
    def saveTargetSuffix = scriptCtx.env.SDK_OPEN_TARGET_PLATFORM
    if (scriptCtx.env.SDK_OPEN_TARGET_PLATFORM == 'linux') {
        saveTargetSuffix = "linux-${scriptCtx.env.LINUX_ARCH}"
    } else if (scriptCtx.env.SDK_OPEN_TARGET_PLATFORM == 'macos') {
        saveTargetSuffix = "macos-${scriptCtx.env.MACOS_ARCH}"
    }
    scriptCtx.env.SAVE_DIR_NAME = "${scriptCtx.env.TIMESTAMP}-${releaseModeName}-${scriptCtx.env.SDK_OPEN_VERSION}_${scriptCtx.env.SDK_OPEN_BUILD}_${scriptCtx.env.SDK_OPEN_STAGE}_${saveTargetSuffix}"
    scriptCtx.env.OSS_ARTIFACT_DIR = "oss://czur-capeditor-build/sdk-open/Package/Development/${scriptCtx.env.SAVE_DIR_NAME}/"
    scriptCtx.env.LINUX_ARTIFACT_OSS_DIR = "${scriptCtx.env.OSS_ARTIFACT_DIR}linux/"
    scriptCtx.env.MACOS_ARTIFACT_OSS_DIR = "${scriptCtx.env.OSS_ARTIFACT_DIR}macos/"
    scriptCtx.env.OSS_DEBUG_DIR = "${scriptCtx.env.LINUX_ARTIFACT_OSS_DIR}debug/"
    def releaseTagPrefix = scriptCtx.env.RELEASE_TAG?.trim() ? "${scriptCtx.env.RELEASE_TAG}-" : ''
    scriptCtx.env.RELEASE_SAVE_DIR_NAME = "${releaseTagPrefix}${scriptCtx.env.TIMESTAMP}-official-release-${scriptCtx.env.SDK_OPEN_VERSION}_${scriptCtx.env.SDK_OPEN_BUILD}_${scriptCtx.env.SDK_OPEN_STAGE}_${saveTargetSuffix}"
    scriptCtx.env.OSS_RELEASE_DIR = "oss://czur-capeditor-build/sdk-open/Package/Release/${scriptCtx.env.RELEASE_SAVE_DIR_NAME}/"
    scriptCtx.env.OSS_SYMBOL_DIR = "oss://czur-capeditor-build/sdk-open/Package/Symbols/${scriptCtx.env.RELEASE_SAVE_DIR_NAME}/"
    scriptCtx.env.RELEASE_DOWNLOAD_BASE_URL = paramString(scriptCtx, 'RELEASE_DOWNLOAD_BASE_URL', 'https://release.czur.com/sdk-open/Package/Release')
    scriptCtx.env.RELEASE_DOWNLOAD_URL = "${scriptCtx.env.RELEASE_DOWNLOAD_BASE_URL}/${scriptCtx.env.RELEASE_SAVE_DIR_NAME}/"
    scriptCtx.env.WINDOWS_SAVE_DIR_NAME = scriptCtx.env.SAVE_DIR_NAME
    scriptCtx.env.WINDOWS_ARTIFACT_OSS_PREFIX_DIR = "${scriptCtx.env.OSS_ARTIFACT_DIR}windows"
    scriptCtx.env.WINDOWS_SYMBOL_OSS_DIR = paramBool(scriptCtx.env.IS_OFFICIAL_RELEASE) ? "${scriptCtx.env.OSS_SYMBOL_DIR}windows/" : "${scriptCtx.env.WINDOWS_ARTIFACT_OSS_PREFIX_DIR}/debug/"
    scriptCtx.env.WINDOWS_REMOTE_LOG_OSS_PREFIX = "${scriptCtx.env.OSS_ARTIFACT_DIR}logs/windows"
    scriptCtx.env.WINDOWS_REMOTE_FAILURE_LOG_OSS_PREFIX = "${scriptCtx.env.WINDOWS_ARTIFACT_OSS_PREFIX}/${scriptCtx.env.TIMESTAMP}-failurelog-${releaseModeName}-${scriptCtx.env.SDK_OPEN_VERSION}_${scriptCtx.env.SDK_OPEN_BUILD}_${scriptCtx.env.SDK_OPEN_STAGE}_${saveTargetSuffix}/windows"
    // Keep generated scripts and short-lived credentials outside CHECKOUT_DIR.
    // Linux copies CHECKOUT_DIR to its temporary ECS while Linux and Windows
    // branches run in parallel, so mutable/sensitive Windows files must never
    // be created below that source tree.
    scriptCtx.env.WINDOWS_REMOTE_SCRIPT_RENDERED = "${scriptCtx.env.WORK_DIR}/remote_sdk_open_bootstrap.rendered.ps1"
}

def validateParameters(def scriptCtx, def linuxTargets) {
    scriptCtx.env.SDK_OPEN_DISTRO = paramString(scriptCtx, 'SDK_OPEN_DISTRO', 'ubuntu')
    scriptCtx.env.JENKINS_BUILD_TYPE = paramString(scriptCtx, 'JENKINS_BUILD_TYPE', 'Release')

    def requestedTargetPlatform = paramString(scriptCtx, 'SDK_OPEN_TARGET_PLATFORM')
    def hasWindowsEcsParams = (scriptCtx.params.WINDOWS_IMAGE_ID != null && scriptCtx.params.WINDOWS_IMAGE_ID.toString().trim()) ||
        (scriptCtx.env.WINDOWS_IMAGE_ID != null && scriptCtx.env.WINDOWS_IMAGE_ID.toString().trim())
    scriptCtx.env.SDK_OPEN_TARGET_PLATFORM = (requestedTargetPlatform ?: (hasWindowsEcsParams ? 'windows' : 'linux')).toLowerCase()
    scriptCtx.env.LINUX_ARCH = paramString(scriptCtx, 'LINUX_ARCH', 'x86-64').toLowerCase()
    scriptCtx.env.MACOS_ARCH = paramString(scriptCtx, 'MACOS_ARCH', 'arm64').toLowerCase()

    if (!(scriptCtx.env.SDK_OPEN_TARGET_PLATFORM in linuxTargets.supportedTargetPlatforms())) {
        scriptCtx.error "Unsupported SDK_OPEN_TARGET_PLATFORM: ${scriptCtx.env.SDK_OPEN_TARGET_PLATFORM}. Expected all, linux, windows, or macos."
    }
    if (!(scriptCtx.env.LINUX_ARCH in ['x86-64', 'arm64', 'all'])) {
        scriptCtx.error "Unsupported LINUX_ARCH: ${scriptCtx.env.LINUX_ARCH}. Expected x86-64, arm64, or all."
    }
    if (!(scriptCtx.env.MACOS_ARCH in ['x86-64', 'arm64', 'all'])) {
        scriptCtx.error "Unsupported MACOS_ARCH: ${scriptCtx.env.MACOS_ARCH}. Expected x86-64, arm64, or all."
    }

    def linuxTargetConfigs = linuxTargets.targetConfigs(scriptCtx.env.SDK_OPEN_TARGET_PLATFORM, scriptCtx.env.LINUX_ARCH)
    scriptCtx.env.SDK_OPEN_LINUX_TARGETS = linuxTargetConfigs.collect { it.target }.join(',')
    scriptCtx.env.SDK_OPEN_LINUX_ARCHES = linuxTargetConfigs.collect { it.arch }.join(',')
    scriptCtx.env.RUN_LINUX_SDK_OPEN = (!linuxTargetConfigs.isEmpty()).toString()
    scriptCtx.env.RUN_WINDOWS_SDK_OPEN = (scriptCtx.env.SDK_OPEN_TARGET_PLATFORM in ['windows', 'all']).toString()
    scriptCtx.env.RUN_MACOS_SDK_OPEN = (scriptCtx.env.SDK_OPEN_TARGET_PLATFORM in ['macos', 'all']).toString()
    scriptCtx.env.RUN_IMPLEMENTED_SDK_OPEN =
        (paramBool(scriptCtx.env.RUN_LINUX_SDK_OPEN) ||
         paramBool(scriptCtx.env.RUN_WINDOWS_SDK_OPEN) ||
         paramBool(scriptCtx.env.RUN_MACOS_SDK_OPEN)).toString()
    scriptCtx.env.MACOS_SDK_OPEN_JOB = paramString(
        scriptCtx,
        'MACOS_SDK_OPEN_JOB',
        'client_czur_capeditor_sdk_open_package_for_macos'
    )

    scriptCtx.env.RELEASE_TAG = paramString(scriptCtx, 'RELEASE_TAG')
    scriptCtx.env.GITHUB_REPO = paramString(scriptCtx, 'GITHUB_REPO', 'CZUR-Developer/czur-sdk-open')
    scriptCtx.env.GITHUB_TOKEN_CREDENTIAL_ID = paramString(scriptCtx, 'GITHUB_TOKEN_CREDENTIAL_ID', 'github-release-token')
    scriptCtx.env.GITEE_REPO = paramString(scriptCtx, 'GITEE_REPO')
    scriptCtx.env.GITEE_TOKEN_CREDENTIAL_ID = paramString(scriptCtx, 'GITEE_TOKEN_CREDENTIAL_ID', 'gitee-release-token')
    scriptCtx.env.RUNTIME_MANIFEST_PRIVATE_KEY_CREDENTIAL_ID = paramString(scriptCtx, 'RUNTIME_MANIFEST_PRIVATE_KEY_CREDENTIAL_ID', 'runtime-manifest-signing-key')
    scriptCtx.env.RUNTIME_MANIFEST_PUBLIC_KEY_CREDENTIAL_ID = paramString(scriptCtx, 'RUNTIME_MANIFEST_PUBLIC_KEY_CREDENTIAL_ID', 'runtime-manifest-signing-public-key')
    scriptCtx.env.RUNTIME_MANIFEST_KEY_ID = 'czur-runtime-release-2026'
    scriptCtx.env.RELEASE_PUBLISH_TARGET = paramString(scriptCtx, 'RELEASE_PUBLISH_TARGET', paramBool(scriptCtx.params.IS_OFFICIAL_RELEASE == null ? true : scriptCtx.params.IS_OFFICIAL_RELEASE) ? 'github' : 'none').toLowerCase()
    if (!(scriptCtx.env.RELEASE_PUBLISH_TARGET == 'none' || scriptCtx.env.RELEASE_PUBLISH_TARGET == 'ossonly' || scriptCtx.env.RELEASE_PUBLISH_TARGET == 'github' || scriptCtx.env.RELEASE_PUBLISH_TARGET == 'gitee' || scriptCtx.env.RELEASE_PUBLISH_TARGET == 'all')) {
        scriptCtx.error "Unsupported RELEASE_PUBLISH_TARGET: ${scriptCtx.env.RELEASE_PUBLISH_TARGET}. Expected none, ossonly, github, gitee, or all."
    }
    scriptCtx.env.PUBLISH_OSS_RELEASE = (scriptCtx.env.RELEASE_PUBLISH_TARGET != 'none').toString()
    scriptCtx.env.PUBLISH_GITHUB_RELEASE = (scriptCtx.env.RELEASE_PUBLISH_TARGET == 'github' || scriptCtx.env.RELEASE_PUBLISH_TARGET == 'all').toString()
    scriptCtx.env.PUBLISH_GITEE_RELEASE = (scriptCtx.env.RELEASE_PUBLISH_TARGET == 'gitee' || scriptCtx.env.RELEASE_PUBLISH_TARGET == 'all').toString()
    scriptCtx.env.RELEASE_REQUIRES_TAG =
        (paramBool(scriptCtx.env.RUN_IMPLEMENTED_SDK_OPEN) &&
         (paramBool(scriptCtx.env.PUBLISH_GITHUB_RELEASE) || paramBool(scriptCtx.env.PUBLISH_GITEE_RELEASE))).toString()
    scriptCtx.env.IS_OFFICIAL_RELEASE = paramBool(scriptCtx.env.PUBLISH_OSS_RELEASE).toString()
    scriptCtx.env.JENKINS_SKIP_BUILD_IMAGE = paramBool(scriptCtx.params.JENKINS_SKIP_BUILD_IMAGE).toString()
    scriptCtx.env.KEEP_ECS_ON_FAILURE = paramBool(scriptCtx.params.KEEP_ECS_ON_FAILURE).toString()
    def smokeTestMode = paramString(scriptCtx, 'SKIP_SDK_OPEN_SMOKE_TEST', 'false').toLowerCase()
    if (!(smokeTestMode in ['false', 'true', 'allow-failure'])) {
        scriptCtx.error "Unsupported SKIP_SDK_OPEN_SMOKE_TEST: ${smokeTestMode}. Expected false, true, or allow-failure."
    }
    scriptCtx.env.SKIP_SDK_OPEN_SMOKE_TEST = smokeTestMode
    def smokeTargetInScope = scriptCtx.env.RELEASE_PUBLISH_TARGET in ['ossonly', 'github', 'gitee', 'all']
    def hasParentSmokeTestTarget =
        paramBool(scriptCtx.env.RUN_LINUX_SDK_OPEN) || paramBool(scriptCtx.env.RUN_WINDOWS_SDK_OPEN)
    def hasSmokeTestTarget = hasParentSmokeTestTarget || paramBool(scriptCtx.env.RUN_MACOS_SDK_OPEN)
    scriptCtx.env.RUN_SDK_OPEN_SMOKE_TEST =
        (smokeTargetInScope && hasSmokeTestTarget && smokeTestMode != 'true').toString()
    scriptCtx.env.RUN_PARENT_SDK_OPEN_SMOKE_TEST =
        (smokeTargetInScope && hasParentSmokeTestTarget && smokeTestMode != 'true').toString()
    scriptCtx.env.SDK_OPEN_SMOKE_ALLOW_FAILURE = (smokeTestMode == 'allow-failure').toString()
    scriptCtx.env.SDK_OPEN_SMOKE_FAILURE_DETECTED = 'false'
    scriptCtx.env.SDK_OPEN_SMOKE_TEST_STATUS = !hasSmokeTestTarget ?
        'NOT_IMPLEMENTED_FOR_MACOS' :
        (!smokeTargetInScope ?
            'NOT_REQUIRED_FOR_TARGET' :
            (smokeTestMode == 'true' ?
                'SKIPPED_BY_PARAMETER' :
                (smokeTestMode == 'allow-failure' ? 'ALLOW_FAILURE' : 'REQUIRED')))

    scriptCtx.env.WINDOWS_REGION_ID = 'cn-beijing'
    scriptCtx.env.WINDOWS_IMAGE_ID = paramString(scriptCtx, 'WINDOWS_IMAGE_ID', scriptCtx.env.WINDOWS_IMAGE_ID ?: '')
    scriptCtx.env.WINDOWS_INSTANCE_TYPE = scriptCtx.env.INSTANCE_TYPE
    scriptCtx.env.WINDOWS_SECURITY_GROUP_ID = scriptCtx.env.SECURITY_GROUP_ID
    scriptCtx.env.WINDOWS_VSWITCH_IDS = scriptCtx.env.V_SWITCH_IDS
    scriptCtx.env.WINDOWS_RESOURCE_GROUP_ID = scriptCtx.env.RESOURCE_GROUP_ID
    scriptCtx.env.WINDOWS_WORKSPACE = paramString(scriptCtx, 'WINDOWS_WORKSPACE', scriptCtx.env.WINDOWS_WORKSPACE ?: 'C:\\czur-workspace')
    scriptCtx.env.WINDOWS_BUILD_CONFIG = paramString(scriptCtx, 'WINDOWS_BUILD_CONFIG', paramString(scriptCtx, 'BUILD_CONFIG', scriptCtx.env.JENKINS_BUILD_TYPE))
    // Windows 架构与目标操作系统分开选择；未配置新参数时保持原有 x64 行为。
    scriptCtx.env.WINDOWS_ARCH = paramString(scriptCtx, 'WINDOWS_ARCH', 'x64').toLowerCase()
    scriptCtx.env.WINDOWS_ARTIFACT_OSS_PREFIX = paramString(scriptCtx, 'WINDOWS_ARTIFACT_OSS_PREFIX', 'oss://czur-capeditor-build/sdk-open/Package/Development')
    scriptCtx.env.WINDOWS_AUTO_RELEASE_MINUTES = paramString(scriptCtx, 'AUTO_RELEASE_MINUTES', scriptCtx.env.WINDOWS_AUTO_RELEASE_MINUTES ?: '120')
    scriptCtx.env.WINDOWS_REMOTE_COMMAND_TIMEOUT_SECONDS = paramString(scriptCtx, 'WINDOWS_REMOTE_COMMAND_TIMEOUT_SECONDS', paramString(scriptCtx, 'REMOTE_COMMAND_TIMEOUT_SECONDS', '7200'))
    scriptCtx.env.WINDOWS_SMOKE_BOOTSTRAP_TIMEOUT_SECONDS = paramString(scriptCtx, 'WINDOWS_SMOKE_BOOTSTRAP_TIMEOUT_SECONDS', '3600')
    scriptCtx.env.WINDOWS_SMOKE_TEST_TIMEOUT_SECONDS = paramString(scriptCtx, 'WINDOWS_SMOKE_TEST_TIMEOUT_SECONDS', '1200')
    scriptCtx.env.WINDOWS_VERIFY_INSTALLER = paramBool(scriptCtx.params.WINDOWS_VERIFY_INSTALLER == null ? true : scriptCtx.params.WINDOWS_VERIFY_INSTALLER).toString()
    scriptCtx.env.WINDOWS_CODE_SIGNING_ENABLED = paramBool(scriptCtx.params.ENABLE_WINDOWS_CODE_SIGNING == null ? true : scriptCtx.params.ENABLE_WINDOWS_CODE_SIGNING).toString()
    scriptCtx.env.WSSC_BROKER_URL = paramString(scriptCtx, 'WSSC_BROKER_URL')
    scriptCtx.env.WSSC_PROJECT = paramString(scriptCtx, 'WSSC_PROJECT', 'default')
    scriptCtx.env.WSSC_JENKINS_TOKEN_CREDENTIAL_ID = paramString(scriptCtx, 'WSSC_JENKINS_TOKEN_CREDENTIAL_ID', 'wssc-jenkins-token')
    scriptCtx.env.WSSC_SIGN_TIMEOUT_SECONDS = paramString(scriptCtx, 'WSSC_SIGN_TIMEOUT_SECONDS', '1800')
    scriptCtx.env.WSSC_SIGN_POLL_INTERVAL_SECONDS = paramString(scriptCtx, 'WSSC_SIGN_POLL_INTERVAL_SECONDS', '5')

    scriptCtx.echo "SDK Open target platform: ${scriptCtx.env.SDK_OPEN_TARGET_PLATFORM} (linux=${scriptCtx.env.RUN_LINUX_SDK_OPEN}, linuxArch=${scriptCtx.env.LINUX_ARCH}, linuxTargets=${scriptCtx.env.SDK_OPEN_LINUX_TARGETS ?: 'none'}, windows=${scriptCtx.env.RUN_WINDOWS_SDK_OPEN}, windowsArch=${scriptCtx.env.WINDOWS_ARCH}, macos=${scriptCtx.env.RUN_MACOS_SDK_OPEN}, macosArch=${scriptCtx.env.MACOS_ARCH}, publishTarget=${scriptCtx.env.RELEASE_PUBLISH_TARGET}, official=${scriptCtx.env.IS_OFFICIAL_RELEASE})"
    scriptCtx.echo "SDK Open smoke test: ${scriptCtx.env.SDK_OPEN_SMOKE_TEST_STATUS} (mode=${scriptCtx.env.SKIP_SDK_OPEN_SMOKE_TEST}, run=${scriptCtx.env.RUN_SDK_OPEN_SMOKE_TEST}, allowFailure=${scriptCtx.env.SDK_OPEN_SMOKE_ALLOW_FAILURE})"
    def existingDescription = scriptCtx.currentBuild.description?.trim()
    def smokeDescription = "SDK smoke: ${scriptCtx.env.SDK_OPEN_SMOKE_TEST_STATUS}"
    scriptCtx.currentBuild.description = existingDescription ? "${existingDescription} | ${smokeDescription}" : smokeDescription

    if (paramBool(scriptCtx.env.RELEASE_REQUIRES_TAG) && !scriptCtx.env.RELEASE_TAG?.trim()) {
        scriptCtx.error "RELEASE_TAG is required for RELEASE_PUBLISH_TARGET=${scriptCtx.env.RELEASE_PUBLISH_TARGET}."
    }
    if (paramBool(scriptCtx.env.RUN_IMPLEMENTED_SDK_OPEN) && paramBool(scriptCtx.env.PUBLISH_OSS_RELEASE) && scriptCtx.env.RELEASE_TAG?.trim() && !(scriptCtx.env.RELEASE_TAG ==~ /^[A-Za-z0-9._-]+$/)) {
        scriptCtx.error 'RELEASE_TAG may only contain letters, digits, dot, underscore, and hyphen.'
    }
    if (paramBool(scriptCtx.env.RUN_IMPLEMENTED_SDK_OPEN) && paramBool(scriptCtx.env.PUBLISH_GITHUB_RELEASE) && !scriptCtx.env.GITHUB_REPO?.trim()) {
        scriptCtx.error 'GITHUB_REPO is required, format: owner/repo'
    }
    if (paramBool(scriptCtx.env.RUN_IMPLEMENTED_SDK_OPEN) && paramBool(scriptCtx.env.PUBLISH_GITHUB_RELEASE) && !scriptCtx.env.GITHUB_TOKEN_CREDENTIAL_ID?.trim()) {
        scriptCtx.error 'GITHUB_TOKEN_CREDENTIAL_ID is required'
    }
    if (paramBool(scriptCtx.env.RUN_IMPLEMENTED_SDK_OPEN) && paramBool(scriptCtx.env.PUBLISH_GITEE_RELEASE) && !scriptCtx.env.GITEE_REPO?.trim()) {
        scriptCtx.error 'GITEE_REPO is required, format: owner/repo'
    }
    if (paramBool(scriptCtx.env.RUN_IMPLEMENTED_SDK_OPEN) && paramBool(scriptCtx.env.PUBLISH_GITEE_RELEASE) && !(scriptCtx.env.GITEE_REPO ==~ /^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/)) {
        scriptCtx.error 'GITEE_REPO must use owner/repo format and may only contain letters, digits, dot, underscore, and hyphen.'
    }
    if (paramBool(scriptCtx.env.RUN_IMPLEMENTED_SDK_OPEN) && paramBool(scriptCtx.env.PUBLISH_GITEE_RELEASE) && !scriptCtx.env.GITEE_TOKEN_CREDENTIAL_ID?.trim()) {
        scriptCtx.error 'GITEE_TOKEN_CREDENTIAL_ID is required'
    }
    if (paramBool(scriptCtx.env.RUN_IMPLEMENTED_SDK_OPEN) && scriptCtx.env.RELEASE_PUBLISH_TARGET == 'all' && !scriptCtx.env.RUNTIME_MANIFEST_PRIVATE_KEY_CREDENTIAL_ID?.trim()) {
        scriptCtx.error 'RUNTIME_MANIFEST_PRIVATE_KEY_CREDENTIAL_ID is required for RELEASE_PUBLISH_TARGET=all'
    }
    if (paramBool(scriptCtx.env.RUN_IMPLEMENTED_SDK_OPEN) && scriptCtx.env.RELEASE_PUBLISH_TARGET == 'all' && !scriptCtx.env.RUNTIME_MANIFEST_PUBLIC_KEY_CREDENTIAL_ID?.trim()) {
        scriptCtx.error 'RUNTIME_MANIFEST_PUBLIC_KEY_CREDENTIAL_ID is required for RELEASE_PUBLISH_TARGET=all'
    }

    if (paramBool(scriptCtx.env.RUN_WINDOWS_SDK_OPEN)) {
        validateWindowsParameters(scriptCtx)
    }
    if (paramBool(scriptCtx.env.RUN_MACOS_SDK_OPEN)) {
        if (!scriptCtx.env.MACOS_SDK_OPEN_JOB?.trim()) {
            scriptCtx.error 'MACOS_SDK_OPEN_JOB is required for SDK_OPEN_TARGET_PLATFORM=macos/all.'
        }
    }
}

def macosArchitectures(def scriptCtx) {
    switch (scriptCtx.env.MACOS_ARCH) {
        case 'arm64':
            return ['arm64']
        case 'x86-64':
            return ['x86_64']
        case 'all':
            return ['arm64', 'x86_64']
        default:
            scriptCtx.error "Unsupported MACOS_ARCH: ${scriptCtx.env.MACOS_ARCH}"
    }
}

def runMacosBuild(def scriptCtx) {
    def macosWorkspaceWrapper =
        "${scriptCtx.env.CHECKOUT_DIR}/src/sdk_open/ci/scripts/macos/jenkins_build_sdk_open_from_workspace.sh"
    if (!scriptCtx.fileExists(macosWorkspaceWrapper)) {
        scriptCtx.error "PARENT_BUILD_COMMIT must contain the macOS SDK Open child runtime files; missing ${macosWorkspaceWrapper}. Merge the child pipeline implementation before enabling macOS in the parent Job."
    }

    def parentBranch = paramString(scriptCtx, 'BRANCH_NAME')
    def macosSmokeMode = paramBool(scriptCtx.env.RUN_SDK_OPEN_SMOKE_TEST) ?
        scriptCtx.env.SKIP_SDK_OPEN_SMOKE_TEST : 'true'
    macosArchitectures(scriptCtx).each { architecture ->
        def ossTargetDir = "${scriptCtx.env.MACOS_ARTIFACT_OSS_DIR}${architecture}/"
        scriptCtx.echo "Triggering SDK Open macOS downstream build: job=${scriptCtx.env.MACOS_SDK_OPEN_JOB}, branch=${parentBranch}, arch=${architecture}, target=${ossTargetDir}"
        def downstreamBuild = scriptCtx.build(
            job: scriptCtx.env.MACOS_SDK_OPEN_JOB,
            wait: true,
            propagate: false,
            parameters: [
                scriptCtx.string(name: 'BRANCH_NAME', value: parentBranch),
                scriptCtx.string(name: 'MAC_ARCH', value: architecture),
                scriptCtx.string(name: 'MAC_BUILD_TYPE', value: scriptCtx.env.JENKINS_BUILD_TYPE),
                scriptCtx.booleanParam(name: 'USE_PARENT_BUILD_METADATA', value: true),
                scriptCtx.string(name: 'PARENT_BUILD_COMMIT', value: scriptCtx.env.BUILD_COMMIT),
                scriptCtx.string(name: 'OSS_TARGET_DIR', value: ossTargetDir),
                scriptCtx.string(name: 'PARENT_JOB_NAME', value: scriptCtx.env.JOB_NAME),
                scriptCtx.string(name: 'PARENT_BUILD_NUMBER', value: scriptCtx.env.BUILD_NUMBER),
                scriptCtx.string(name: 'SKIP_SDK_OPEN_SMOKE_TEST', value: macosSmokeMode)
            ]
        )
        scriptCtx.echo "SDK Open macOS downstream build completed: ${downstreamBuild.absoluteUrl} result=${downstreamBuild.result}"
        if (downstreamBuild.result == 'UNSTABLE' && scriptCtx.env.SDK_OPEN_SMOKE_ALLOW_FAILURE == 'true') {
            scriptCtx.env.SDK_OPEN_SMOKE_FAILURE_DETECTED = 'true'
            def currentDescription = scriptCtx.currentBuild.description ?: ''
            scriptCtx.currentBuild.description = currentDescription.replace(
                'SDK smoke: ALLOW_FAILURE',
                'SDK smoke: FAILED (allowed)'
            )
            scriptCtx.echo "SDK Open macOS ${architecture} smoke failed in allow-failure mode; release will continue: ${downstreamBuild.absoluteUrl}"
        } else if (downstreamBuild.result != 'SUCCESS') {
            scriptCtx.error "SDK Open macOS downstream build failed: arch=${architecture}, result=${downstreamBuild.result}, url=${downstreamBuild.absoluteUrl}"
        }
    }
}

def validateWindowsParameters(def scriptCtx) {
    if (!scriptCtx.env.WINDOWS_IMAGE_ID?.trim()) {
        scriptCtx.error 'WINDOWS_IMAGE_ID is required for SDK_OPEN_TARGET_PLATFORM=windows/all. Other Windows ECS creation parameters intentionally reuse the Linux temp ECS settings from this Jenkinsfile.'
    }
    if (!(scriptCtx.env.WINDOWS_REMOTE_COMMAND_TIMEOUT_SECONDS ==~ /^[1-9][0-9]*$/)) {
        scriptCtx.error 'WINDOWS_REMOTE_COMMAND_TIMEOUT_SECONDS must be a positive integer'
    }
    if (paramBool(scriptCtx.env.RUN_SDK_OPEN_SMOKE_TEST)) {
        if (!(scriptCtx.env.WINDOWS_SMOKE_BOOTSTRAP_TIMEOUT_SECONDS ==~ /^[1-9][0-9]*$/)) {
            scriptCtx.error 'WINDOWS_SMOKE_BOOTSTRAP_TIMEOUT_SECONDS must be a positive integer'
        }
        if (!(scriptCtx.env.WINDOWS_SMOKE_TEST_TIMEOUT_SECONDS ==~ /^[1-9][0-9]*$/)) {
            scriptCtx.error 'WINDOWS_SMOKE_TEST_TIMEOUT_SECONDS must be a positive integer'
        }
        def smokeStepTimeoutTotal = scriptCtx.env.WINDOWS_SMOKE_BOOTSTRAP_TIMEOUT_SECONDS.toInteger() +
            scriptCtx.env.WINDOWS_SMOKE_TEST_TIMEOUT_SECONDS.toInteger()
        def smokeCommandTimeout = scriptCtx.env.WINDOWS_REMOTE_COMMAND_TIMEOUT_SECONDS.toInteger()
        if (smokeCommandTimeout < smokeStepTimeoutTotal + 600) {
            scriptCtx.error 'WINDOWS_REMOTE_COMMAND_TIMEOUT_SECONDS must leave at least 600 seconds beyond the sum of Windows smoke bootstrap and test timeouts'
        }
    }
    if (!(scriptCtx.env.WINDOWS_ARCH in ['x64', 'x86', 'all'])) {
        scriptCtx.error "Unsupported WINDOWS_ARCH: ${scriptCtx.env.WINDOWS_ARCH}. Expected x64, x86, or all."
    }
    if (paramBool(scriptCtx.env.WINDOWS_CODE_SIGNING_ENABLED)) {
        if (!scriptCtx.env.WSSC_BROKER_URL?.trim()) {
            scriptCtx.error 'ENABLE_WINDOWS_CODE_SIGNING=true requires WSSC_BROKER_URL for SDK Open Windows signing.'
        }
        if (!scriptCtx.env.WSSC_PROJECT?.trim()) {
            scriptCtx.error 'ENABLE_WINDOWS_CODE_SIGNING=true requires WSSC_PROJECT for SDK Open Windows signing.'
        }
        if (!scriptCtx.env.WSSC_JENKINS_TOKEN_CREDENTIAL_ID?.trim()) {
            scriptCtx.error 'ENABLE_WINDOWS_CODE_SIGNING=true requires WSSC_JENKINS_TOKEN_CREDENTIAL_ID for SDK Open Windows signing.'
        }
        if (!(scriptCtx.env.WSSC_SIGN_TIMEOUT_SECONDS ==~ /^[1-9][0-9]*$/)) {
            scriptCtx.error 'WSSC_SIGN_TIMEOUT_SECONDS must be a positive integer when SDK Open Windows signing is enabled.'
        }
        if (!(scriptCtx.env.WSSC_SIGN_POLL_INTERVAL_SECONDS ==~ /^[1-9][0-9]*$/)) {
            scriptCtx.error 'WSSC_SIGN_POLL_INTERVAL_SECONDS must be a positive integer when SDK Open Windows signing is enabled.'
        }
    }
    scriptCtx.env.GIT_REPO_URL = scriptCtx.sh(script: "git -C ${scriptCtx.env.CHECKOUT_DIR} config --get remote.origin.url", returnStdout: true).trim()
    scriptCtx.env.GIT_COMMIT_SHA = scriptCtx.sh(script: "git -C ${scriptCtx.env.CHECKOUT_DIR} rev-parse HEAD", returnStdout: true).trim()
    scriptCtx.env.GIT_REF = scriptCtx.sh(script: "git -C ${scriptCtx.env.CHECKOUT_DIR} symbolic-ref --quiet --short HEAD || true", returnStdout: true).trim()
    def selectedBranch = paramString(scriptCtx, 'BRANCH_NAME').replaceFirst(/^origin\//, '')
    if (selectedBranch) {
        scriptCtx.env.GIT_REF = selectedBranch
    }
    if (!scriptCtx.env.GIT_REF?.trim()) {
        scriptCtx.env.GIT_REF = scriptCtx.env.CHANGE_BRANCH ?: scriptCtx.env.BRANCH_NAME ?: ''
    }
}

def preflightReleasePublish(def scriptCtx) {
    scriptCtx.dir("${scriptCtx.env.CHECKOUT_DIR}") {
        scriptCtx.sh '''
set -e
python3 src/sdk_open/ci/scripts/sdk_open_release.py preflight-oss
        '''

        if (paramBool(scriptCtx.env.PUBLISH_GITHUB_RELEASE)) {
            scriptCtx.withCredentials([[$class: 'StringBinding', credentialsId: "${scriptCtx.env.GITHUB_TOKEN_CREDENTIAL_ID}", variable: 'GITHUB_TOKEN']]) {
                scriptCtx.sh '''
set -e
python3 src/sdk_open/ci/scripts/sdk_open_release.py preflight-github
                '''
            }
        } else {
            scriptCtx.echo 'Skip GitHub Release preflight'
        }

        if (paramBool(scriptCtx.env.PUBLISH_GITEE_RELEASE)) {
            scriptCtx.withCredentials([[$class: 'StringBinding', credentialsId: "${scriptCtx.env.GITEE_TOKEN_CREDENTIAL_ID}", variable: 'GITEE_TOKEN']]) {
                scriptCtx.sh '''
set -e
python3 src/sdk_open/ci/scripts/sdk_open_release.py preflight-gitee
                '''
            }
        } else {
            scriptCtx.echo 'Skip Gitee Release preflight'
        }
        if (scriptCtx.env.RELEASE_PUBLISH_TARGET == 'all') {
            scriptCtx.withCredentials([
                [$class: 'FileBinding', credentialsId: "${scriptCtx.env.RUNTIME_MANIFEST_PRIVATE_KEY_CREDENTIAL_ID}", variable: 'RUNTIME_MANIFEST_PRIVATE_KEY_FILE'],
                [$class: 'FileBinding', credentialsId: "${scriptCtx.env.RUNTIME_MANIFEST_PUBLIC_KEY_CREDENTIAL_ID}", variable: 'RUNTIME_MANIFEST_PUBLIC_KEY_FILE']
            ]) {
                scriptCtx.sh '''
set -eu
set +x
src/sdk_open/scripts/release/run_runtime_manifest_signer.sh --check-key-pair
                '''
            }
        } else {
            scriptCtx.echo 'Skip Runtime Manifest signing credential preflight'
        }
        scriptCtx.echo "Release publish preflight passed for target: ${scriptCtx.env.RELEASE_PUBLISH_TARGET}"
    }
}

// Release downloads and metadata live in the checkout, not the workspace root.
// Run for every platform before reserving a Build number or creating cloud resources.
def cleanPreviousArtifacts(def scriptCtx) {
    if (!scriptCtx.env.CHECKOUT_DIR?.trim()) {
        scriptCtx.error 'CHECKOUT_DIR must be set before cleaning SDK Open artifacts'
    }
    scriptCtx.dir("${scriptCtx.env.CHECKOUT_DIR}") {
        scriptCtx.sh '''
set -e
test -e .git
echo "Cleaning previous SDK Open artifacts in $(pwd)"
rm -f -- sdk-open_*.deb scanner_*.deb sdk_open_app_*.debug
rm -f -- sdk-open_Windows_*.exe sdk-open_macos_*.pkg sdk-open_macos_*.dSYM.zip
rm -f -- SHA256SUMS.txt release_note.md gitee_release_note.md
rm -f -- release-assets.txt release-downloads.md release_manifest.json esa-urls.txt
rm -f -- deb-files.txt windows-files.txt macos-files.txt
rm -f -- release-create.json release-update.json release-update-result.json
rm -f -- upload-SHA256SUMS.txt.json upload-sdk-open_*.json upload-scanner_*.json
rm -f -- manifest.json manifest.json.sig
rm -rf -- build_package release-assets
# Legacy Windows staging must not enter the Linux source archive.
rm -rf -- build_and_pack/tmp/sdk-open-windows-ecs
        '''
    }
}

def prepareLinuxWorkspace(def scriptCtx) {
    scriptCtx.sh '''
set -e
rm -rf "${LINUX_ARTIFACT_STAGING_DIR}"
mkdir -p "${LINUX_ARTIFACT_STAGING_DIR}"
rm -f "${LINUX_SOURCE_ARCHIVE}"
mkdir -p "$(dirname "${LINUX_SOURCE_ARCHIVE}")"
    '''
    scriptCtx.dir("${scriptCtx.env.CHECKOUT_DIR}") {
        scriptCtx.sh '''
set -e
echo "Validating sdk_open checkout..."
git submodule status --recursive src/sdk_open || true
for changelog_file in "${SDK_OPEN_CHANGELOG_EN}" "${SDK_OPEN_CHANGELOG_ZH}"; do
    if [ ! -f "${changelog_file}" ]; then
        echo "Missing changelog file: ${changelog_file}"
        echo "Please make sure Jenkins checks out the src/sdk_open submodule recursively and the selected submodule commit contains SDK Open changelog files."
        exit 1
    fi
done

mkdir -p build_package/
        '''
    }
    scriptCtx.sh '''
set -euo pipefail
archive="${LINUX_SOURCE_ARCHIVE}"
checkout_dir="${CHECKOUT_DIR}"
echo "Creating Linux SDK Open source archive: ${archive}"
tar -czf "${archive}" \
    --exclude='./.git' \
    --exclude='./*/.git' \
    --exclude='./build' \
    --exclude='./*/build' \
    --exclude='./output' \
    --exclude='./*/output' \
    --exclude='./build_package' \
    --exclude='./*/build_package' \
    --exclude='./views.build' \
    --exclude='./docs.build' \
    --exclude='./node_modules' \
    --exclude='./*/node_modules' \
    --exclude='./build_and_pack/tmp' \
    --exclude='./*/build_and_pack/tmp' \
    -C "${checkout_dir}" .
test -s "${archive}"
echo "Linux SDK Open source archive size: $(du -h "${archive}" | awk '{print $1}')"
    '''
}

def buildLinuxPackage(def scriptCtx, def linuxTargets, String target) {
    linuxTargets.createTempEcsInstances(scriptCtx, target)
    linuxTargets.waitForInstances(scriptCtx, target)
    linuxTargets.copySourceToTempEcs(scriptCtx, target)
    linuxTargets.copyDependencies(scriptCtx, target)
    linuxTargets.buildPackages(scriptCtx, target)
    linuxTargets.collectArtifacts(scriptCtx, target)
}

def collectAndCheckLinuxArtifacts(def scriptCtx) {
    scriptCtx.sh '''
set -e
find "${LINUX_ARTIFACT_STAGING_DIR}" -mindepth 2 -maxdepth 2 -type f \\
    \\( -name 'sdk-open_*.deb' -o -name 'scanner_*.deb' -o -name 'sdk_open_app_*.debug' \\) \\
    -exec cp -f '{}' "${CHECKOUT_DIR}/" \\;
    '''
    scriptCtx.dir("${scriptCtx.env.CHECKOUT_DIR}") {
        scriptCtx.sh '''
set -e
deb_count=$(find . -maxdepth 1 -type f \\( -name 'sdk-open_*.deb' -o -name 'scanner_*.deb' \\) | wc -l)
expected_deb_count=$(printf '%s' "${SDK_OPEN_LINUX_ARCHES}" | awk -F',' '{print NF}')
if [ "${deb_count}" -ne "${expected_deb_count}" ]; then
    echo "Expected ${expected_deb_count} sdk_open deb(s), found ${deb_count}"
    find . -maxdepth 1 -type f -name '*.deb' -print
    exit 1
fi

if [ "${JENKINS_BUILD_TYPE}" = "Release" ]; then
    old_ifs="${IFS}"
    IFS=','
    for arch in ${SDK_OPEN_LINUX_ARCHES}; do
        debug_count=$(find . -maxdepth 1 -type f -name "sdk_open_app_${arch}_*.debug" | wc -l)
        if [ "${debug_count}" -lt 1 ]; then
            echo "Expected sdk_open_app debug symbol for Release build arch=${arch}"
            exit 1
        fi
    done
    IFS="${old_ifs}"
fi

find . -maxdepth 1 -type f \\( -name 'sdk-open_*.deb' -o -name 'scanner_*.deb' \\) -printf '%f\\n' | sort > deb-files.txt
xargs -r sha256sum < deb-files.txt > SHA256SUMS.txt
echo "Collected deb files:"
cat deb-files.txt
cat SHA256SUMS.txt
        '''
    }
}

def generateReleaseNote(def scriptCtx) {
    scriptCtx.dir("${scriptCtx.env.CHECKOUT_DIR}") {
        scriptCtx.sh '''
set -e
python3 src/sdk_open/ci/scripts/generate_sdk_open_release_note.py \
    --version "${SDK_OPEN_VERSION}" \
    --build "${SDK_OPEN_BUILD}" \
    --stage "${SDK_OPEN_STAGE}" \
    --english-changelog "${SDK_OPEN_CHANGELOG_EN}" \
    --chinese-changelog "${SDK_OPEN_CHANGELOG_ZH}" \
    --output release_note.md

echo "Generated release_note.md:"
cat release_note.md
        '''
    }
}

def uploadDevelopmentArtifacts(def scriptCtx) {
    scriptCtx.dir("${scriptCtx.env.CHECKOUT_DIR}") {
        if (paramBool(scriptCtx.env.RUN_LINUX_SDK_OPEN) && scriptCtx.env.JENKINS_BUILD_TYPE == 'Release') {
            scriptCtx.env.LINUX_SYMBOL_OSS_DIR = paramBool(scriptCtx.env.IS_OFFICIAL_RELEASE) ? "${scriptCtx.env.OSS_SYMBOL_DIR}linux/" : scriptCtx.env.OSS_DEBUG_DIR
            scriptCtx.sh '''
set -e
for debug_file in sdk_open_app_*_*.debug; do
    if [ -f "${debug_file}" ]; then
        echo "Uploading debug symbol to OSS: ${debug_file} -> ${LINUX_SYMBOL_OSS_DIR}"
        ossutil cp "${debug_file}" "${LINUX_SYMBOL_OSS_DIR}" --config-file ~/oss/.ossutilnewconfig
    fi
done
            '''
        }

        if (paramBool(scriptCtx.env.RUN_LINUX_SDK_OPEN) && !paramBool(scriptCtx.env.IS_OFFICIAL_RELEASE)) {
            scriptCtx.sh '''
set -e
find . -maxdepth 1 -type f \\( -name 'sdk-open_*.deb' -o -name 'scanner_*.deb' \\) -printf '%f\\n' | sort > deb-files.txt
if [ ! -s deb-files.txt ]; then
    echo "No deb file found for OSS upload"
    exit 1
fi

while IFS= read -r asset; do
    [ -n "${asset}" ] || continue
    if [ ! -f "${asset}" ]; then
        echo "Missing OSS upload asset: ${asset}"
        exit 1
    fi
    echo "Uploading internal test artifact to OSS: ${asset} -> ${LINUX_ARTIFACT_OSS_DIR}"
    ossutil cp "${asset}" "${LINUX_ARTIFACT_OSS_DIR}" --config-file ~/oss/.ossutilnewconfig
done < deb-files.txt

for asset in SHA256SUMS.txt release_note.md; do
    if [ ! -f "${asset}" ]; then
        echo "Missing OSS upload asset: ${asset}"
        exit 1
    fi
    echo "Uploading internal test artifact to OSS: ${asset} -> ${LINUX_ARTIFACT_OSS_DIR}"
    ossutil cp "${asset}" "${LINUX_ARTIFACT_OSS_DIR}" --config-file ~/oss/.ossutilnewconfig
done

echo "Internal test artifacts uploaded to: ${LINUX_ARTIFACT_OSS_DIR}"
            '''
        }
    }
}

def publishOssReleaseAssets(def scriptCtx) {
    scriptCtx.dir("${scriptCtx.env.CHECKOUT_DIR}") {
        scriptCtx.sh '''
set -e
python3 src/sdk_open/ci/scripts/sdk_open_release.py publish-oss-release-assets
        '''
    }
}

def publishGithubRelease(def scriptCtx) {
    scriptCtx.dir("${scriptCtx.env.CHECKOUT_DIR}") {
        scriptCtx.withCredentials([[$class: 'StringBinding', credentialsId: "${scriptCtx.env.GITHUB_TOKEN_CREDENTIAL_ID}", variable: 'GITHUB_TOKEN']]) {
            scriptCtx.sh '''
set -e
python3 src/sdk_open/ci/scripts/sdk_open_release.py publish-github
            '''
        }
    }
}

def publishGiteeRelease(def scriptCtx) {
    scriptCtx.dir("${scriptCtx.env.CHECKOUT_DIR}") {
        scriptCtx.withCredentials([[$class: 'StringBinding', credentialsId: "${scriptCtx.env.GITEE_TOKEN_CREDENTIAL_ID}", variable: 'GITEE_TOKEN']]) {
            scriptCtx.sh '''
set -e
python3 src/sdk_open/ci/scripts/sdk_open_release.py publish-gitee
            '''
        }
    }
}

def publishSignedRuntimeManifest(def scriptCtx) {
    scriptCtx.dir("${scriptCtx.env.CHECKOUT_DIR}") {
        scriptCtx.withCredentials([
            [$class: 'FileBinding', credentialsId: "${scriptCtx.env.RUNTIME_MANIFEST_PRIVATE_KEY_CREDENTIAL_ID}", variable: 'RUNTIME_MANIFEST_PRIVATE_KEY_FILE'],
            [$class: 'FileBinding', credentialsId: "${scriptCtx.env.RUNTIME_MANIFEST_PUBLIC_KEY_CREDENTIAL_ID}", variable: 'RUNTIME_MANIFEST_PUBLIC_KEY_FILE']
        ]) {
            scriptCtx.sh '''
set -euo pipefail
set +x

python3 src/sdk_open/ci/scripts/sdk_open_release.py generate-runtime-manifest \
    --output manifest.json
src/sdk_open/scripts/release/run_runtime_manifest_signer.sh \
    --manifest manifest.json \
    --output manifest.json.sig \
    --key-id "${RUNTIME_MANIFEST_KEY_ID}"

archive_manifest="${OSS_ARTIFACT_DIR}manifest.json"
archive_signature="${OSS_ARTIFACT_DIR}manifest.json.sig"
oss_config="${HOME}/oss/.ossutilnewconfig"

retry_command() {
    max_attempts="$1"
    shift
    attempt=1
    until "$@"; do
        if [ "${attempt}" -ge "${max_attempts}" ]; then
            echo "Command failed after ${max_attempts} attempts: $1" >&2
            return 1
        fi
        attempt=$((attempt + 1))
        sleep 2
    done
}

# Keep a per-build recovery copy before touching the stable entry point.
retry_command 3 ossutil cp -f manifest.json.sig "${archive_signature}" --config-file "${oss_config}"
retry_command 3 ossutil cp -f manifest.json "${archive_manifest}" --config-file "${oss_config}"

# The manifest is the commit point: publish its signature first and the bytes last.
OSSUTIL_CONFIG_FILE="${oss_config}" src/sdk_open/scripts/release/upload_runtime_manifest.sh manifest.json manifest.json.sig

echo "Signed Runtime Manifest archived at ${OSS_ARTIFACT_DIR} and published to the Release entry point"
            '''
        }
    }
}

def cleanupPublishedDevelopmentStaging(def scriptCtx) {
    scriptCtx.dir("${scriptCtx.env.CHECKOUT_DIR}") {
        scriptCtx.sh '''
set -e
python3 src/sdk_open/ci/scripts/sdk_open_release.py cleanup-development-staging
        '''
    }
}

def updateOpenDocsChangelog(def scriptCtx) {
    scriptCtx.dir("${scriptCtx.env.CHECKOUT_DIR}") {
        scriptCtx.sh '''
set -euo pipefail
docs_dir="${WORKSPACE}/sdk-open-docs"
rm -rf "${docs_dir}"
git clone --branch main git@gitee.com:czur_dl/czur-open-docs.git "${docs_dir}"
python3 src/sdk_open/ci/scripts/update_sdk_open_docs_changelog.py \\
    --version "${RELEASE_TAG}" \\
    --date "$(date '+%Y-%m-%d')" \\
    --english-source "${SDK_OPEN_CHANGELOG_EN}" \\
    --chinese-source "${SDK_OPEN_CHANGELOG_ZH}" \\
    --english-target "${docs_dir}/docs/en/changelog/index.md" \\
    --chinese-target "${docs_dir}/docs/changelog/index.md"
git -C "${docs_dir}" config user.name "Jenkins"
git -C "${docs_dir}" config user.email "jenkins@czur.invalid"
git -C "${docs_dir}" add docs/changelog/index.md docs/en/changelog/index.md
git -C "${docs_dir}" commit -m "docs(changelog): update SDK Open ${RELEASE_TAG}"
git -C "${docs_dir}" push origin HEAD:main
rm -rf "${docs_dir}"
'''
        scriptCtx.build job: 'czur-open-docs_Html_prod', wait: false,
            parameters: [scriptCtx.string(name: 'BRANCH_NAME', value: 'origin/main')]
    }
}

return this
