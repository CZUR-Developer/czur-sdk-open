import { contextConstant, UIAbility, Want } from '@kit.AbilityKit';
import { BusinessError, emitter } from '@kit.BasicServicesKit';
import { statusBarManager } from '@kit.DeskTopExtensionKit';
import { image } from '@kit.ImageKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { window } from '@kit.ArkUI';
import { trayServiceControl } from '../common/TrayServiceControl';
import { sdkOpenRuntime } from '../services/SdkOpenRuntime';

interface LauncherWindowState {
  showWindowControls: boolean;
}

export default class SdkOpenTrayAbility extends UIAbility {
  private registered: boolean = false;
  private whiteIcon: image.PixelMap | null = null;
  private blackIcon: image.PixelMap | null = null;
  private resident: boolean = false;
  private mainWindow: window.Window | null = null;
  private rightMenuListener = (event: emitter.EventData): void => {
    const code = event.data?.['menuCode'] as string;
    if (code === 'start' || code === 'stop') { trayServiceControl.execute(code); }
  };

  onCreate(want: Want): void {
    this.resident = want.abilityName === 'SdkOpenTrayAbility';
    if (this.resident) {
      sdkOpenRuntime.initialize(this.context);
      trayServiceControl.initialize(this.context, sdkOpenRuntime);
      statusBarManager.on('rightMenuClick', this.rightMenuListener);
      this.handleCommand(want);
    }
  }

  private async createIcon(name: string): Promise<image.PixelMap> {
    const bytes = this.context.resourceManager.getRawFileContentSync(name);
    const source = image.createImageSource(bytes.buffer);
    try {
      return await source.createPixelMap();
    } finally {
      source.release();
    }
  }

  private async registerTray(): Promise<void> {
    if (this.registered) { return; }
    const white = await this.createIcon('tray_white.svg');
    const black = await this.createIcon('tray_black.svg');
    this.whiteIcon = white;
    this.blackIcon = black;
    await new Promise<void>((resolve, reject) => {
      statusBarManager.addToStatusBar(this.context, {
        icons: { white, black },
        quickOperation: {
          title: 'CZUR Open SDK', height: 420,
          moduleName: 'sdkopen_tray', abilityName: 'SdkOpenStatusBarAbility'
        },
        // Keep the system tray context menu limited to the same service lifecycle
        // controls exposed by the popup. Demo navigation remains a popup action.
        statusBarGroupMenu: [[
          { title: '启动服务', menuAction: {
            abilityName: 'SdkOpenTrayAbility', moduleName: 'sdkopen_tray', notifyOnly: true, menuCode: 'start'
          } },
          { title: '关闭服务', menuAction: {
            abilityName: 'SdkOpenTrayAbility', moduleName: 'sdkopen_tray', notifyOnly: true, menuCode: 'stop'
          } }
        ]]
      }, (error) => {
        if (error) { reject(error); } else { resolve(); }
      });
    });
    this.registered = true;
    hilog.info(0, 'SdkOpenTray', 'SDK Open registered in system status bar');
  }

  private async enterTray(): Promise<void> {
    try {
      if (this.resident) {
        await this.context.hideAbility();
      } else {
        await this.registerTray();
        try {
          await this.context.startAbility({
            bundleName: 'com.czur.opensdk', moduleName: 'sdkopen_tray', abilityName: 'SdkOpenTrayAbility'
          }, {
            processMode: contextConstant.ProcessMode.ATTACH_TO_STATUS_BAR_ITEM,
            startupVisibility: contextConstant.StartupVisibility.STARTUP_HIDE
          });
        } catch (error) {
          // Reopening the launcher must leave the existing hidden resident intact.
          if ((error as BusinessError).code !== 16000068) { throw new Error(String(error)); }
        }
        // Keep the launcher in the foreground. The hidden resident owns the
        // service, while the launcher remains a normal, user-visible window.
      }
      hilog.info(0, 'SdkOpenTray', 'SDK Open launcher attached to system tray');
    } catch (error) {
      hilog.error(0, 'SdkOpenTray', 'System tray initialization failed: %{public}s', String(error));
    }
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    if (!this.resident) {
      trayServiceControl.initializePopup(this.context);
      this.mainWindow = windowStage.getMainWindowSync();
      this.mainWindow.resize(520, 600).catch((error: Object): void => {
        hilog.warn(0, 'SdkOpenTray', 'Cannot resize launcher window: %{public}s', String(error));
      });
      const launcherState: LauncherWindowState = { showWindowControls: true };
      windowStage.loadContent('pages/Index', new LocalStorage(launcherState), (error) => {
        if (error.code) {
          hilog.error(0, 'SdkOpenTray', 'Failed to load launcher page: %{public}s', String(error));
          return;
        }
        this.showLauncher();
      });
    }
    this.enterTray();
  }

  private async showLauncher(): Promise<void> {
    try {
      if (this.mainWindow) {
        if (this.mainWindow.getWindowStatus() === window.WindowStatusType.MINIMIZE) {
          await this.mainWindow.restore();
        }
        await this.mainWindow.showWindow();
      }
    } catch (error) {
      hilog.error(0, 'SdkOpenTray', 'Cannot show launcher window: %{public}s', String(error));
    }
  }

  onNewWant(want: Want): void {
    if (this.resident) {
      this.enterTray();
    } else {
      this.showLauncher();
    }
    this.handleCommand(want);
  }

  onWindowStageDestroy(): void {
    this.mainWindow = null;
  }

  private handleCommand(want: Want): void {
    const command = want.parameters?.['serviceCommand'] as string;
    if (this.resident && (command === 'start' || command === 'stop')) {
      trayServiceControl.execute(command);
    }
  }

  onDestroy(): void {
    if (this.resident) {
      statusBarManager.off('rightMenuClick', this.rightMenuListener);
      // Normal exit awaits cleanup in AbilityStage. System teardown may skip
      // that path; native async cleanup remains the final ownership fallback.
      sdkOpenRuntime.stop().catch((error: Object): void => {
        hilog.warn(0, 'SdkOpenTray', 'Tray runtime teardown failed: %{public}s', String(error));
      });
    }
    this.whiteIcon?.release();
    this.blackIcon?.release();
  }
}
