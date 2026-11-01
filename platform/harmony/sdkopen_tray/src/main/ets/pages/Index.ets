import { ServiceState, trayServiceControl } from '../common/TrayServiceControl';

const TRAY_CONTENT_WIDTH = 360;

@Entry({ useSharedStorage: true })
@Component
struct Index {
  @LocalStorageProp('showWindowControls') private showWindowControls: boolean = false;
  @State private running: boolean = false;
  @State private busy: boolean = false;
  @State private message: string = '正在检查服务状态…';
  private timer: number = -1;
  private stateListener = (state: ServiceState): void => {
    this.running = state.running;
    this.busy = state.busy;
    this.message = state.message;
  };

  aboutToAppear(): void {
    trayServiceControl.subscribe(this.stateListener);
    trayServiceControl.refresh();
    this.timer = setInterval(() => { trayServiceControl.refresh(); }, 2000);
  }

  aboutToDisappear(): void {
    trayServiceControl.unsubscribe(this.stateListener);
    clearInterval(this.timer);
  }

  build() {
    Column({ space: 16 }) {
      if (this.showWindowControls) {
        Text('CZUR Open SDK').fontSize(24).fontWeight(FontWeight.Medium)
        Text('启动服务后可打开 Demo 或管理站点。窗口最小化后，仍可从系统托盘控制服务。')
          .fontSize(14).fontColor('#666666')
      }
      Text(this.message).fontSize(16).fontColor(this.running ? '#16803c' : '#666666')
      Column({ space: 12 }) {
        Button('启动服务')
          .width('100%')
          .enabled(!this.busy && !this.running)
          .onClick(() => { trayServiceControl.execute('start'); })
        Button('关闭服务')
          .width('100%')
          .enabled(!this.busy && this.running)
          .onClick(() => { trayServiceControl.execute('stop'); })
        Button('打开 HTTP Demo（调试）').width('100%').enabled(!this.busy && this.running)
          .onClick(() => { trayServiceControl.openDemo(); })
        Button('打开管理站点').width('100%').enabled(!this.busy && this.running)
          .onClick(() => { trayServiceControl.openAdmin(); })
        Button('安装本机 HTTPS 证书').width('100%').enabled(!this.busy && this.running)
          .onClick(() => { trayServiceControl.installHttpsCertificate(); })
      }
      .width('100%')
    }
    .width(this.showWindowControls ? '100%' : TRAY_CONTENT_WIDTH)
    .height('100%')
    .padding(this.showWindowControls ? 16 : 12)
    .justifyContent(FlexAlign.Center).alignItems(HorizontalAlign.Center)
    .backgroundColor('#ffffff')
  }
}
