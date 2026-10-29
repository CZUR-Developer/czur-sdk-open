import { UIExtensionContentSession, Want } from '@kit.AbilityKit';
import { StatusBarViewExtensionAbility } from '@kit.DeskTopExtensionKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { trayServiceControl } from '../common/TrayServiceControl';

export default class SdkOpenStatusBarAbility extends StatusBarViewExtensionAbility {
  onSessionCreate(want: Want, session: UIExtensionContentSession): void {
    trayServiceControl.initializePopup(this.context);
    session.loadContent('pages/Index');
    hilog.info(0, 'SdkOpenTray', 'SDK Open system tray popup opened');
  }
}
