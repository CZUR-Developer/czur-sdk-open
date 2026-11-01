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

const LAUNCHER_WINDOW_WIDTH = 520;
const LAUNCHER_WINDOW_HEIGHT = 800;
const TRAY_QUICK_OPERATION_HEIGHT = 360;
const LAUNCHER_WINDOW_POLICY_RETRY_DELAYS: number[] = [100, 500, 1000];

export default class SdkOpenTrayAbility extends UIAbility {
  private registered: boolean = false;
  private whiteIcon: image.PixelMap | null = null;
  private blackIcon: image.PixelMap | null = null;
  private resident: boolean = false;
  private mainWindow: window.Window | null = null;
  private launcherWindowPolicyGeneration: number = 0;
  private launcherWindowStatusListener = (status: window.WindowStatusType): void => {
    if (status !== window.WindowStatusType.MAXIMIZE || !this.mainWindow) {
      return;
    }
    // Title-bar movement must stay enabled so the user can drag the window.
    // If a platform still turns a double-click into maximize, immediately
    // restore the fixed-size launcher instead of leaving it maximized.
    this.mainWindow.restore().then(() => {
      this.scheduleLauncherWindowPolicy('restore-after-maximize');
    }).catch((error: Object): void => {
      hilog.warn(0, 'SdkOpenTray', 'Cannot restore launcher after maximize: %{public}s', String(error));
    });
  };
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
          title: 'CZUR Open SDK', height: TRAY_QUICK_OPERATION_HEIGHT,
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
      this.registerLauncherWindowStatusListener();
      this.scheduleLauncherWindowPolicy('window-stage-create');
      const launcherState: LauncherWindowState = { showWindowControls: true };
      windowStage.loadContent('pages/Index', new LocalStorage(launcherState), (error) => {
        if (error.code) {
          hilog.error(0, 'SdkOpenTray', 'Failed to load launcher page: %{public}s', String(error));
          return;
        }
        this.scheduleLauncherWindowPolicy('load-content');
        this.showLauncher();
      });
    }
    this.enterTray();
  }

  private registerLauncherWindowStatusListener(): void {
    const mainWindow = this.mainWindow;
    if (!mainWindow) {
      return;
    }
    try {
      mainWindow.on('windowStatusChange', this.launcherWindowStatusListener);
    } catch (error) {
      hilog.warn(0, 'SdkOpenTray', 'Cannot observe launcher window status: %{public}s', String(error));
    }
  }

  private scheduleLauncherWindowPolicy(stage: string): void {
    const generation = ++this.launcherWindowPolicyGeneration;
    this.applyLauncherWindowPolicy(stage);
    LAUNCHER_WINDOW_POLICY_RETRY_DELAYS.forEach((delay: number) => {
      setTimeout(() => {
        if (generation === this.launcherWindowPolicyGeneration) {
          this.applyLauncherWindowPolicy(`${stage}+${delay}ms`);
        }
      }, delay);
    });
  }

  private applyLauncherWindowPolicy(stage: string): void {
    const mainWindow = this.mainWindow;
    if (!mainWindow) {
      return;
    }

    try {
      // Harmony may create the title-bar decoration after loadContent. Apply
      // this again during the retry window so the maximize button cannot
      // reappear when the decoration is initialized late.
      mainWindow.setWindowTitleButtonVisible(false, true, true);
    } catch (error) {
      hilog.warn(0, 'SdkOpenTray', 'Cannot configure launcher title buttons at %{public}s: %{public}s',
        stage, String(error));
    }
    try {
      // Keep title-bar movement enabled so the user can reposition the window.
      // A maximize status is handled by launcherWindowStatusListener above.
      mainWindow.setWindowTitleMoveEnabled(true);
    } catch (error) {
      hilog.warn(0, 'SdkOpenTray', 'Cannot enable launcher title movement at %{public}s: %{public}s',
        stage, String(error));
    }
    try {
      mainWindow.setResizeByDragEnabled(false).catch((error: Object): void => {
        hilog.warn(0, 'SdkOpenTray', 'Cannot disable launcher resize at %{public}s: %{public}s',
          stage, String(error));
      });
    } catch (error) {
      hilog.warn(0, 'SdkOpenTray', 'Cannot disable launcher resize at %{public}s: %{public}s',
        stage, String(error));
    }
    try {
      mainWindow.resize(LAUNCHER_WINDOW_WIDTH, LAUNCHER_WINDOW_HEIGHT).catch((error: Object): void => {
        hilog.warn(0, 'SdkOpenTray', 'Cannot resize launcher window at %{public}s: %{public}s',
          stage, String(error));
      });
    } catch (error) {
      hilog.warn(0, 'SdkOpenTray', 'Cannot resize launcher window at %{public}s: %{public}s',
        stage, String(error));
    }
  }

  private async showLauncher(): Promise<void> {
    try {
      if (this.mainWindow) {
        if (this.mainWindow.getWindowStatus() === window.WindowStatusType.MINIMIZE) {
          await this.mainWindow.restore();
        }
        await this.mainWindow.showWindow();
        this.scheduleLauncherWindowPolicy('show-launcher');
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

  onForeground(): void {
    if (!this.resident) {
      this.scheduleLauncherWindowPolicy('foreground');
    }
  }

  onWindowStageDestroy(): void {
    this.launcherWindowPolicyGeneration++;
    const mainWindow = this.mainWindow;
    if (mainWindow) {
      try {
        mainWindow.off('windowStatusChange', this.launcherWindowStatusListener);
      } catch (error) {
        hilog.warn(0, 'SdkOpenTray', 'Cannot stop observing launcher window status: %{public}s', String(error));
      }
    }
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
