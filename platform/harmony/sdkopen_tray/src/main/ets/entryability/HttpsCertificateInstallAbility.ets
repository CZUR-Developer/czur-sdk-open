import { UIAbility } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import { hilog } from '@kit.PerformanceAnalysisKit';

export default class HttpsCertificateInstallAbility extends UIAbility {
  onWindowStageCreate(windowStage: window.WindowStage): void {
    windowStage.loadContent('pages/HttpsCertificateInstall', (error) => {
      if (error.code) {
        hilog.error(0, 'SdkOpenTray', 'Failed to load HTTPS certificate page: %{public}s', String(error));
        return;
      }
      hilog.info(0, 'SdkOpenTray', 'SDK Open HTTPS certificate page loaded');
    });
  }
}
