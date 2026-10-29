import { AbilityConstant, AbilityStage } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { trayServiceControl } from './common/TrayServiceControl';

export default class TrayAbilityStage extends AbilityStage {
  async onPrepareTerminationAsync(): Promise<AbilityConstant.PrepareTermination> {
    try {
      await trayServiceControl.stopForExit();
      return AbilityConstant.PrepareTermination.TERMINATE_IMMEDIATELY;
    } catch (error) {
      hilog.error(0, 'SdkOpenTray', 'Cannot exit before service stops: %{public}s', String(error));
      return AbilityConstant.PrepareTermination.CANCEL;
    }
  }
}
