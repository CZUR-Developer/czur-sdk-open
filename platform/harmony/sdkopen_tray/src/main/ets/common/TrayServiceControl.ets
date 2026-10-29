import { common, Want } from '@kit.AbilityKit';
import { http } from '@kit.NetworkKit';
import { hilog } from '@kit.PerformanceAnalysisKit';

const SERVICE_WANT: Want = {
  bundleName: 'com.czur.sdkopen', moduleName: 'sdkopen', abilityName: 'SdkOpenServiceExtension'
};

export interface ServiceState {
  running: boolean;
  busy: boolean;
  message: string;
}

// The popup runs in a separate process and routes commands to the hidden UIAbility.
class TrayServiceControl {
  private context: common.UIAbilityContext | null = null;
  private popupContext: common.UIExtensionContext | null = null;
  private state: ServiceState = { running: false, busy: false, message: '服务未启动' };
  private listeners: Array<(state: ServiceState) => void> = [];

  initialize(context: common.UIAbilityContext): void { this.context = context; }
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
    if (this.state.busy) { return; }
    const running = await this.readStatus();
    if (this.state.busy) { return; }
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
    if (!this.context) { return; }
    try {
      await this.context.stopAppServiceExtensionAbility(SERVICE_WANT);
    } catch (error) {
      if (await this.readStatus()) { throw new Error(String(error)); }
    }
    await this.waitForStatus(false, 5);
    hilog.info(0, 'SdkOpenTray', 'SDK Open service stopped before system tray exit');
  }

  async execute(command: string): Promise<void> {
    if (this.state.busy) { return; }
    this.state.busy = true;
    this.state.message = command === 'start' ? '正在启动服务…' : '正在停止服务…';
    this.notify();
    try {
      const context = this.context;
      if (!context && this.popupContext) {
        await this.popupContext.startAbility({
          bundleName: 'com.czur.sdkopen', moduleName: 'sdkopen_tray', abilityName: 'SdkOpenTrayAbility',
          parameters: { serviceCommand: command }
        });
        await this.waitForStatus(command === 'start');
      } else if (!context) {
        throw new Error('托盘控制入口尚未初始化');
      } else if (command === 'start') {
        await context.startAppServiceExtensionAbility(SERVICE_WANT);
        await this.waitForStatus(true);
      } else if (command === 'stop') {
        await context.stopAppServiceExtensionAbility(SERVICE_WANT);
        await this.waitForStatus(false);
      } else {
        throw new Error('未知的服务控制操作');
      }
      this.state.running = command === 'start';
      this.state.message = this.state.running ? '服务运行中' : '服务已停止';
      hilog.info(0, 'SdkOpenTray', 'SDK Open system tray command completed: %{public}s', command);
    } catch (error) {
      this.state.running = await this.readStatus();
      this.state.message = `操作失败：${String(error)}`;
      hilog.error(0, 'SdkOpenTray', 'SDK Open system tray command failed: %{public}s', String(error));
    } finally {
      this.state.busy = false;
      this.notify();
    }
  }
}

export const trayServiceControl = new TrayServiceControl();
