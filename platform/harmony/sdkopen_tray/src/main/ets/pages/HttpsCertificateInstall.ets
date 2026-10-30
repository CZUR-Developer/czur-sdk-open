import { common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { certificateManagerDialog } from '@kit.DeviceCertificateKit';
import { installHttpsRootCertificate } from '../common/TrayServiceControl';

@Entry
@Component
struct HttpsCertificateInstall {
  @State private busy: boolean = false;
  @State private message: string = '安装根证书后，浏览器才能信任本机 HTTPS/WSS 服务。';

  private async install(): Promise<void> {
    if (this.busy) { return; }
    this.busy = true;
    this.message = '正在打开系统证书安装确认…';
    try {
      const context = getContext(this) as common.UIAbilityContext;
      await installHttpsRootCertificate(context);
      this.message = 'HTTPS 根证书已安装，可以关闭此窗口。';
    } catch (error) {
      const businessError = error as BusinessError;
      if (businessError.code === certificateManagerDialog.CertificateDialogErrorCode.ERROR_OPERATION_CANCELED) {
        this.message = '已取消安装 HTTPS 根证书。';
      } else {
        this.message = `证书安装失败：${String(error)}`;
      }
    } finally {
      this.busy = false;
    }
  }

  private async close(): Promise<void> {
    try {
      const context = getContext(this) as common.UIAbilityContext;
      await context.terminateSelf();
    } catch (error) {
      this.message = `窗口关闭失败：${String(error)}`;
    }
  }

  build() {
    Column({ space: 20 }) {
      Text('CZUR SDK Open HTTPS 证书').fontSize(22).fontWeight(FontWeight.Medium)
      Text(this.message).fontSize(16).fontColor('#555555').textAlign(TextAlign.Center)
      Button('安装证书').width('100%').enabled(!this.busy)
        .onClick(() => { this.install(); })
      Button('关闭').width('100%').enabled(!this.busy)
        .onClick(() => { this.close(); })
    }
    .width('100%').height('100%').padding(32)
    .justifyContent(FlexAlign.Center).alignItems(HorizontalAlign.Center)
    .backgroundColor('#ffffff')
  }
}
