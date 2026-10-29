def paramBool(def value) {
    if (value == null) {
        return false
    }
    if (value instanceof Boolean) {
        return value
    }
    return value.toString().trim().toLowerCase() in ['1', 'true', 'yes', 'on']
}

def supportedTargetPlatforms() {
    return ['all', 'linux', 'windows', 'macos']
}

def targetConfigs(String targetPlatform, String linuxArch = 'x86-64') {
    // vSwitchIds is a target-level override. Keep it empty to use the
    // pipeline-level V_SWITCH_IDS fallback, which is shared by x86-64 Linux
    // and Windows ECS creation. arm64 uses a verified ARM-capable VSwitch.
    def x86_64 = [
        target: 'linux-x86-64',
        arch: 'x86-64',
        imageId: 'm-2zect9nydk90b78aenpa',
        osName: 'dockerEnv',
        instanceType: 'ecs.g9i.xlarge',
        cefArchive: 'cef_binary_109.1.25+gcc56caa+chromium-109.0.5414.120_linux64_withoutSymbolTable.tar.bz2',
        cefSourceDir: 'cef_binary_109.1.25+gcc56caa+chromium-109.0.5414.120_linux64',
        cefTargetDir: 'linux64.109.x86-64',
        vSwitchIds: ''
    ]
    def arm64 = [
        target: 'linux-arm64',
        arch: 'arm64',
        imageId: 'm-2zeffeqkli7d2kc208b3',
        osName: 'dockerEnv',
        instanceType: 'ecs.g8y.xlarge',
        cefArchive: 'cef_binary_109.1.26+g988a710+chromium-109.0.5414.120_linuxarm64_withoutSymbolTable.tar.bz2',
        cefSourceDir: 'cef_binary_109.1.26+g988a710+chromium-109.0.5414.120_linuxarm64',
        cefTargetDir: 'linux64.109.arm64',
        vSwitchIds: 'vsw-2zejp2hdwhyg93pveuayo,vsw-2zexwmthgjsiyfo4f9d9j,vsw-2zeoxjhcysmx9shrzfd0e'
    ]

    if (!(targetPlatform in ['linux', 'all'])) {
        return []
    }

    switch (linuxArch) {
        case 'x86-64':
            return [x86_64]
        case 'arm64':
            return [arm64]
        case 'all':
            return [x86_64, arm64]
        default:
            return []
    }
}

def configsFromEnv(def scriptCtx, String target = '') {
    switch (target?.trim()) {
        case 'linux-x86-64':
            return targetConfigs('linux', 'x86-64')
        case 'linux-arm64':
            return targetConfigs('linux', 'arm64')
        default:
            return targetConfigs(scriptCtx.env.SDK_OPEN_TARGET_PLATFORM, scriptCtx.env.LINUX_ARCH ?: 'x86-64')
    }
}

def stateFile(String target, String name) {
    return "sdk-open-${target}-${name}.txt"
}

def writeState(def scriptCtx, String target, String name, String value) {
    scriptCtx.writeFile(file: stateFile(target, name), text: "${value ?: ''}\n")
}

def readState(def scriptCtx, String target, String name) {
    def file = stateFile(target, name)
    def value = scriptCtx.sh(
        script: "test -f '${file}' && sed -n '1p' '${file}' || true",
        returnStdout: true
    ).trim()
    return value
}

def createTempEcsInstances(def scriptCtx, String target = '') {
    def releaseAt = scriptCtx.sh(script: 'date -u +%Y-%m-%dT%H:%M:00Z --date="+90 minute"', returnStdout: true).trim()

    configsFromEnv(scriptCtx, target).each { cfg ->
        writeState(scriptCtx, cfg.target, 'release-at', releaseAt)
        // Empty cfg.vSwitchIds intentionally falls back to the global
        // V_SWITCH_IDS candidate list; non-empty values are tried as-is.
        def vSwitchIds = (cfg.vSwitchIds ?: scriptCtx.env.V_SWITCH_IDS).split(',').collect { it.trim() }.findAll { it }
        boolean createSuccess = false
        def createResultFile = "createInstanceResult-${cfg.target}.txt"
        for (int i = 0; i < vSwitchIds.size(); i++) {
            def vSwitchId = vSwitchIds[i]
            scriptCtx.echo "尝试 ${cfg.target} 可用区 [${i + 1}/${vSwitchIds.size()}]: VSwitchId=${vSwitchId}"
            def createExitCode = scriptCtx.sh(script: """
            /usr/local/bin/aliyun ecs RunInstances --RegionId "cn-beijing" \\
            --ImageId "${cfg.imageId}" \\
            --InstanceType "${cfg.instanceType}" \\
            --SecurityGroupId "${scriptCtx.env.SECURITY_GROUP_ID}" \\
            --VSwitchId "${vSwitchId}" \\
            --InstanceChargeType "PostPaid" \\
            --SystemDisk.Size "60" \\
            --SystemDisk.Category "cloud_essd" \\
            --HostName "czur-cicd-temp-${cfg.osName}-sdk-open-${cfg.arch}" \\
            --PasswordInherit "true" \\
            --SpotStrategy "SpotAsPriceGo" \\
            --InternetChargeType "PayByTraffic" \\
            --InternetMaxBandwidthOut "20" \\
            --ResourceGroupId "${scriptCtx.env.RESOURCE_GROUP_ID}" \\
            --AutoReleaseTime "${releaseAt}" > ${createResultFile}
            """, returnStatus: true).toString()
            if (createExitCode == '0') {
                createSuccess = true
                scriptCtx.echo "${cfg.target} 实例创建成功，使用 VSwitchId=${vSwitchId}"
                break
            }
            scriptCtx.echo "${cfg.target} VSwitchId=${vSwitchId} 创建失败 (退出码: ${createExitCode})，尝试下一个可用区"
        }
        if (!createSuccess) {
            scriptCtx.error "${cfg.target} 所有可用区均创建失败"
        }
        def instanceId = scriptCtx.sh(script: "jq -r '.InstanceIdSets.InstanceIdSet[0] // empty' ${createResultFile}", returnStdout: true).trim()
        if (!instanceId) {
            scriptCtx.error "无法提取 ${cfg.target} 临时 ECS 实例 ID"
        }
        writeState(scriptCtx, cfg.target, 'instance-id', instanceId)
        scriptCtx.echo "${cfg.target} INSTANCE_ID=${instanceId}"
    }
}

def waitForInstances(def scriptCtx, String target = '') {
    configsFromEnv(scriptCtx, target).each { cfg ->
        def createResultFile = "createInstanceResult-${cfg.target}.txt"
        def describeStatusFile = "describeStatus-${cfg.target}.json"
        def describeInstanceFile = "describeInstance-${cfg.target}.json"
        def instanceId = readState(scriptCtx, cfg.target, 'instance-id') ?: scriptCtx.sh(script: "jq -r '.InstanceIdSets.InstanceIdSet[0] // empty' ${createResultFile}", returnStdout: true).trim()
        if (!instanceId) {
            scriptCtx.error "无法提取 ${cfg.target} 临时 ECS 实例 ID"
        }
        writeState(scriptCtx, cfg.target, 'instance-id', instanceId)
        scriptCtx.echo "${cfg.target} INSTANCE_ID=${instanceId}"

        scriptCtx.sh """
set -e
status_ok=0
for i in \$(seq 1 20); do
    /usr/local/bin/aliyun ecs DescribeInstanceStatus --RegionId "cn-beijing" --InstanceId.1 "${instanceId}" > ${describeStatusFile}
    status=\$(jq -r '.InstanceStatuses.InstanceStatus[0].Status // empty' ${describeStatusFile})
    echo "${cfg.target} [\$i/20] status: \${status}"
    if [ "\${status}" = "Running" ]; then
        status_ok=1
        break
    fi
    sleep 10
done
if [ "\${status_ok}" -ne 1 ]; then
    echo "${cfg.target} instance not ready, timeout"
    exit 1
fi
        """

        def instanceIp = scriptCtx.sh(script: """
/usr/local/bin/aliyun ecs DescribeInstances --RegionId "cn-beijing" --InstanceIds '["${instanceId}"]' > ${describeInstanceFile}
jq -r '.Instances.Instance[0].VpcAttributes.PrivateIpAddress.IpAddress[0] // empty' ${describeInstanceFile}
        """, returnStdout: true).trim()
        if (!instanceIp) {
            scriptCtx.error "无法获取 ${cfg.target} 临时 ECS 内网 IP"
        }
        writeState(scriptCtx, cfg.target, 'instance-ip', instanceIp)
        scriptCtx.echo "${cfg.target} INSTANCE_IP=${instanceIp}"
    }
}

def copySourceToTempEcs(def scriptCtx, String target = '') {
    configsFromEnv(scriptCtx, target).each { cfg ->
        def instanceIp = readState(scriptCtx, cfg.target, 'instance-ip')
        def archive = scriptCtx.env.LINUX_SOURCE_ARCHIVE
        if (!archive?.trim()) {
            scriptCtx.error 'LINUX_SOURCE_ARCHIVE is required for Linux SDK Open source transfer'
        }
        if (!scriptCtx.fileExists(archive)) {
            scriptCtx.error "Linux SDK Open source archive not found: ${archive}"
        }
        scriptCtx.retry(10) {
            scriptCtx.sleep 5
            scriptCtx.sh "scp ${scriptCtx.env.SSH_OPTS} '${archive}' root@${instanceIp}:/tmp/sdk-open-source.tar.gz"
            scriptCtx.sh "ssh ${scriptCtx.env.SSH_OPTS} root@${instanceIp} \"set -e; rm -rf '${scriptCtx.env.REMOTE_CHECKOUT_DIR}'; mkdir -p '${scriptCtx.env.REMOTE_CHECKOUT_DIR}'; tar xzf /tmp/sdk-open-source.tar.gz -C '${scriptCtx.env.REMOTE_CHECKOUT_DIR}'; rm -f /tmp/sdk-open-source.tar.gz\""
        }
    }
}

def copyDependencies(def scriptCtx, String target = '') {
    configsFromEnv(scriptCtx, target).each { cfg ->
        def instanceIp = readState(scriptCtx, cfg.target, 'instance-ip')
        scriptCtx.sh "ssh ${scriptCtx.env.SSH_OPTS} root@${instanceIp} \"tar xf Downloads/${cfg.cefArchive} -C ${scriptCtx.env.REMOTE_CHECKOUT_DIR}/third_party_repo/cef/\""
        scriptCtx.sh "ssh ${scriptCtx.env.SSH_OPTS} root@${instanceIp} \"mv ${scriptCtx.env.REMOTE_CHECKOUT_DIR}/third_party_repo/cef/${cfg.cefSourceDir} ${scriptCtx.env.REMOTE_CHECKOUT_DIR}/third_party_repo/cef/${cfg.cefTargetDir}\""
        scriptCtx.sh "ssh ${scriptCtx.env.SSH_OPTS} root@${instanceIp} \"tar xf Downloads/gtest_v1.13.0.tar.gz -C ${scriptCtx.env.REMOTE_CHECKOUT_DIR}/third_party_repo/\""
    }
}

def buildPackages(def scriptCtx, String target = '') {
    configsFromEnv(scriptCtx, target).each { cfg ->
        def instanceIp = readState(scriptCtx, cfg.target, 'instance-ip')
        def skipBuildImageArg = paramBool(scriptCtx.env.JENKINS_SKIP_BUILD_IMAGE) ? '--skip-build-image' : ''
        def disableGitArg = '--cmake-arg -DCMAKE_DISABLE_FIND_PACKAGE_Git=TRUE'
        scriptCtx.sh """
ssh ${scriptCtx.env.SSH_OPTS} root@${instanceIp} 'cd ${scriptCtx.env.REMOTE_CHECKOUT_DIR} &&
/usr/bin/bash ./src/sdk_open/scripts/package-sdkopen.sh ${skipBuildImageArg} ${disableGitArg} --arch ${cfg.arch} --distro ${scriptCtx.env.SDK_OPEN_DISTRO} --build-type ${scriptCtx.env.JENKINS_BUILD_TYPE}'
        """
    }
}

def collectArtifacts(def scriptCtx, String target = '') {
    configsFromEnv(scriptCtx, target).each { cfg ->
        def instanceIp = readState(scriptCtx, cfg.target, 'instance-ip')
        def stagingDir = "${scriptCtx.env.LINUX_ARTIFACT_STAGING_DIR}/${cfg.target}"
        scriptCtx.sh """
set -e
mkdir -p "${stagingDir}"
scp ${scriptCtx.env.SSH_OPTS} -r "root@${instanceIp}:${scriptCtx.env.REMOTE_CHECKOUT_DIR}/sdk-open_*.deb" "${stagingDir}/" || true
scp ${scriptCtx.env.SSH_OPTS} -r "root@${instanceIp}:${scriptCtx.env.REMOTE_CHECKOUT_DIR}/scanner_*.deb" "${stagingDir}/" || true
scp ${scriptCtx.env.SSH_OPTS} -r "root@${instanceIp}:${scriptCtx.env.REMOTE_CHECKOUT_DIR}/sdk_open_app_${cfg.arch}_*.debug" "${stagingDir}/" || true
        """
    }
}

def cleanupTempEcsInstances(def scriptCtx, String currentResult) {
    def linuxTargets = (scriptCtx.env.SDK_OPEN_LINUX_TARGETS ?: '').split(',').collect { it.trim() }.findAll { it }
    if (linuxTargets.isEmpty()) {
        scriptCtx.echo '未创建 Linux 临时 ECS，跳过实例删除'
        return
    }

    linuxTargets.each { target ->
        def instanceId = readState(scriptCtx, target, 'instance-id')
        if (instanceId?.trim()) {
            boolean keepInstance = (currentResult != 'SUCCESS' && paramBool(scriptCtx.env.KEEP_ECS_ON_FAILURE))
            if (keepInstance) {
                scriptCtx.echo "构建失败，按参数保留 ${target} 临时 ECS，不执行手动删除: ${instanceId}"
                def releaseAt = readState(scriptCtx, target, 'release-at')
                scriptCtx.echo "${target} 实例仍会按 AutoReleaseTime 自动释放: ${releaseAt ?: 'unknown'}"
            } else {
                scriptCtx.echo "删除 ${target} 临时 ECS: ${instanceId}"
                boolean deleted = false
                for (int i = 1; i <= 12; i++) {
                    int deleteExitCode = scriptCtx.sh(
                        script: "/usr/local/bin/aliyun ecs DeleteInstances --Force true --RegionId cn-beijing --InstanceId.1 ${instanceId}",
                        returnStatus: true
                    )
                    if (deleteExitCode == 0) {
                        scriptCtx.echo "${target} 临时 ECS 已删除"
                        deleted = true
                        break
                    }
                    scriptCtx.echo "[${i}/12] ${target} 删除失败，等待后重试"
                    scriptCtx.sleep time: 10, unit: 'SECONDS'
                }
                if (!deleted) {
                    scriptCtx.echo "删除 ${target} 临时 ECS 失败，请手动清理: aliyun ecs DeleteInstances --Force true --RegionId cn-beijing --InstanceId.1 ${instanceId}"
                }
            }
        } else {
            scriptCtx.echo "${target} 未创建临时 ECS，跳过实例删除"
        }
    }
}

return this
