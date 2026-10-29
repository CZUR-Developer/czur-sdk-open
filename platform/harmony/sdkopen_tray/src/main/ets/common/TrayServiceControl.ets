import { common, contextConstant } from '@kit.AbilityKit';
import { http } from '@kit.NetworkKit';
import { hilog } from '@kit.PerformanceAnalysisKit';

export interface TrayRuntimeControl {
  start(): Promise<void>;
  stop(): Promise<void>;
  stopForExit(): Promise<void>;
}

export interface ServiceState {
  running: boolean;
  busy: boolean;
  message: string;
}

// The popup imports only this control module, never the native runtime. Its
// commands are delivered to the resident UIAbility in the tray's owner process.
class TrayServiceControl {
  private context: common.UIAbilityContext | null = null;
  private popupContext: common.UIExtensionContext | null = null;
  private runtime: TrayRuntimeControl | null = null;
  private exiting: boolean = false;
  private state: ServiceState = { running: false, busy: false, message: '服务未启动' };
  private listeners: Array<(state: ServiceState) => void> = [];

  initialize(context: common.UIAbilityContext, runtime: TrayRuntimeControl): void {
    this.context = context;
    this.runtime = runtime;
  }

  initializePopup(context: common.UIExtensionContext): void { this.popupContext = context; }

  async openDemo(): Promise<void> {
    const context = this.popupContext;
    if (!context) {
      hilog.error(0, 'SdkOpenTray', 'Cannot open SDK Open demo before popup initialization');
      return;
    }
    try {
      await context.startAbility({
        action: 'ohos.want.action.viewData',
        uri: 'http://127.0.0.1:17081/',
        entities: ['entity.system.browsable']
      });
      hilog.info(0, 'SdkOpenTray', 'SDK Open demo page opened');
    } catch (error) {
      hilog.error(0, 'SdkOpenTray', 'SDK Open demo page failed to open: %{public}s', String(error));
    }
  }

  subscribe(listener: (state: ServiceState) => void): void {
    this.listeners.push(listener);
    listener(this.state);
  }

  unsubscribe(listener: (state: ServiceState) => void): void {
    this.listeners = this.listeners.filter((item) => item !== listener);
  }

  private notify(): void {
    for (const listener of this.listeners) { listener(this.state); }
  }

  private async readStatus(): Promise<boolean> {
    const request = http.createHttp();
    try {
      const response = await request.request('http://127.0.0.1:17080/api/status', {
        method: http.RequestMethod.GET, connectTimeout: 1000, readTimeout: 1000, usingProxy: false
      });
      return response.responseCode === 200 && String(response.result).includes('"running":true');
    } catch (_) {
      return false;
    } finally {
      request.destroy();
    }
  }

  async refresh(): Promise<void> {
    if (this.state.busy || this.exiting) { return; }
    const running = await this.readStatus();
    if (this.state.busy || this.exiting) { return; }
    this.state.running = running;
    this.state.message = running ? '服务运行中' : '服务未启动';
    this.notify();
  }

  private async waitForStatus(expected: boolean, attempts: number = 60): Promise<void> {
    for (let attempt = 0; attempt < attempts; attempt++) {
      if (await this.readStatus() === expected) { return; }
      await new Promise<void>((resolve) => { setTimeout(resolve, 1000); });
    }
    throw new Error(expected ? '服务启动超时' : '服务端口仍可访问');
  }

  async stopForExit(): Promise<void> {
    const runtime = this.runtime;
    if (!runtime) { return; }
    this.exiting = true;
    this.state.busy = true;
    this.notify();
    try {
      await runtime.stopForExit();
      this.state.running = false;
      this.state.message = '服务已停止';
      hilog.info(0, 'SdkOpenTray', 'SDK Open runtime stopped before system tray exit');
    } catch (error) {
      this.exiting = false;
      this.state.message = `退出清理失败：${String(error)}`;
      throw new Error(String(error));
    } finally {
      this.state.busy = false;
      this.notify();
    }
  }

  async execute(command: string): Promise<void> {
    if (this.state.busy || this.exiting) { return; }
    this.state.busy = true;
    this.state.message = command === 'start' ? '正在启动服务…' : '正在停止服务…';
    this.notify();
    try {
      const runtime = this.runtime;
      if (command !== 'start' && command !== 'stop') {
        throw new Error('未知的服务控制操作');
      }
      if (runtime && this.context) {
        if (command === 'start') {
          await runtime.start();
        } else {
          await runtime.stop();
        }
      } else if (this.popupContext) {
        await this.popupContext.startAbility({
          bundleName: 'com.czur.opensdk', moduleName: 'sdkopen_tray', abilityName: 'SdkOpenTrayAbility',
          parameters: { serviceCommand: command }
        }, {
          processMode: contextConstant.ProcessMode.ATTACH_TO_STATUS_BAR_ITEM,
          startupVisibility: contextConstant.StartupVisibility.STARTUP_HIDE
        });
        await this.waitForStatus(command === 'start');
      } else {
        throw new Error('托盘控制入口尚未初始化');
      }
      if (this.exiting) { return; }
      this.state.running = command === 'start';
      this.state.message = this.state.running ? '服务运行中' : '服务已停止';
      hilog.info(0, 'SdkOpenTray', 'SDK Open system tray command completed: %{public}s', command);
    } catch (error) {
      if (this.exiting) { return; }
      this.state.running = await this.readStatus();
      this.state.message = `操作失败：${String(error)}`;
      hilog.error(0, 'SdkOpenTray', 'SDK Open system tray command failed: %{public}s', String(error));
    } finally {
      if (!this.exiting) {
        this.state.busy = false;
        this.notify();
      }
    }
  }
}

export const trayServiceControl = new TrayServiceControl();
