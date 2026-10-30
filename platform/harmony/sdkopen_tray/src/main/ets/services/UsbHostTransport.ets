import usbManager from '@ohos.usbManager';
import { BusinessError } from '@kit.BasicServicesKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import sdkOpen from 'libsdkopen_harmony.so';

const MAX_CONTROL_READ_BYTES: number = 1024;
const MAX_CONTROL_WRITE_BYTES: number = 4096;

function isIsoEndpoint(endpoint: usbManager.USBEndpoint): boolean {
  return endpoint.type === usbManager.UsbEndpointTransferType.TRANSFER_TYPE_ISOCHRONOUS;
}

interface UsbHostRequest {
  id: number;
  operation: string;
  deviceId: number;
  session: number;
  controlInterface: number;
  streamingInterface: number;
  alternateSetting: number;
  endpoint: number;
  requestType: number;
  request: number;
  value: number;
  index: number;
  timeoutMs: number;
  length: number;
  packetSize: number;
  packetCount: number;
  inFlight: number;
  data?: number[];
}

interface EnumeratedUsbDevice {
  deviceId: number;
  rawDescriptor: number[];
}

interface EnumerateResult {
  devices: EnumeratedUsbDevice[];
}

interface NativeIsoResult {
  success: boolean;
  error: string;
}

interface TransferSlot {
  transfer: usbManager.UsbDataTransferParams;
  session: number;
  sequence: number;
  active: boolean;
}

enum UsbHostLifecycle {
  IDLE,
  STARTING,
  RUNNING,
  STOPPING
}

/**
 * Thin API 18+ USB Host adapter. USB requests stay on the ArkTS event loop;
 * UVC negotiation, payload parsing and JPEG assembly stay in C++.
 */
export class UsbHostTransport {
  private attached: boolean = false;
  private device?: Readonly<usbManager.USBDevice>;
  private pipe?: Readonly<usbManager.USBDevicePipe>;
  private controlInterface?: usbManager.USBInterface;
  private streamingInterface?: usbManager.USBInterface;
  private selectedInterface?: usbManager.USBInterface;
  private selectedEndpoint?: usbManager.USBEndpoint;
  private openReady: boolean = false;
  private poisonedPipe?: Readonly<usbManager.USBDevicePipe>;
  private poisonedDevice?: Readonly<usbManager.USBDevice>;
  private nativeIsoActive: boolean = false;
  private nativeStreamingInterface: number = 0;
  private readonly nativeIsoEnabled: boolean;
  private running: boolean = false;
  private stopping: boolean = false;
  private activeSession: number = 0;
  private nextSequence: number = 0;
  private transfers: TransferSlot[] = [];
  private stopWaiters: Array<() => void> = [];
  private diagnosticTimeMs: number = 0;
  private diagnosticBatches: number = 0;
  private diagnosticPacketErrors: number = 0;
  private diagnosticUvcErrors: number = 0;
  private lifecycle: Promise<void> = Promise.resolve();
  private lifecycleState: UsbHostLifecycle = UsbHostLifecycle.IDLE;
  private lifecycleGeneration: number = 0;

  constructor(nativeIsoEnabled: boolean = true) {
    this.nativeIsoEnabled = nativeIsoEnabled;
  }

  attach(): boolean {
    if (this.attached) {
      return true;
    }
    this.attached = sdkOpen.attachUsbHost((requestJson: string): void => {
      this.dispatch(requestJson);
    });
    return this.attached;
  }

  async shutdownAfterNativeStop(): Promise<void> {
    ++this.lifecycleGeneration;
    // Native Stop has already released DeviceManager while this dispatcher was
    // alive. Only local Host ownership remains at this point.
    await this.serializeLifecycle(async (): Promise<void> => {
      await this.stopTransfers();
      this.closePipe();
      this.cleanupPoisonedPipe();
    });
  }

  private serializeLifecycle(action: () => Promise<void>): Promise<void> {
    const result: Promise<void> = this.lifecycle.then(action);
    this.lifecycle = result.catch((): void => {});
    return result;
  }

  private dispatch(requestJson: string): void {
    let request: UsbHostRequest;
    try {
      request = JSON.parse(requestJson) as UsbHostRequest;
    } catch (error) {
      sdkOpen.failUsbHost(this.activeSession, `Invalid USB Host request: ${this.errorText(error)}`);
      return;
    }
    if (request.operation === 'stop' || request.operation === 'close' || request.operation === 'release') {
      ++this.lifecycleGeneration;
    }
    this.serializeLifecycle((): Promise<void> => this.handleRequest(request)).catch((error: Error) => {
      if (request.operation === 'controlRead' || request.operation === 'controlWrite') {
        hilog.warn(0, 'SdkOpenUsbHost',
          'USB control failed operation=%{public}s value=%{public}d index=%{public}d length=%{public}d error=%{public}s',
          request.operation, request.value, request.index, request.length, this.errorText(error));
      }
      this.complete(request, false, this.errorText(error));
    });
  }

  private async handleRequest(request: UsbHostRequest): Promise<void> {
    switch (request.operation) {
      case 'initialize':
        this.complete(request, true, '{}');
        return;
      case 'enumerate':
        await this.enumerate(request);
        return;
      case 'open':
        await this.open(request);
        return;
      case 'close':
        await this.stopTransfers();
        this.closePipe();
        this.cleanupPoisonedPipe();
        this.complete(request, true, '{}');
        return;
      case 'select':
        this.select(request);
        return;
      case 'controlRead':
        await this.controlTransfer(request, true);
        return;
      case 'controlWrite':
        await this.controlTransfer(request, false);
        return;
      case 'start':
        await this.start(request);
        return;
      case 'stop':
        await this.stopTransfers();
        this.complete(request, true, '{}');
        return;
      case 'release':
        await this.stopTransfers();
        this.closePipe();
        this.cleanupPoisonedPipe();
        this.complete(request, true, '{}');
        return;
      default:
        this.complete(request, false, `Unsupported USB Host operation: ${request.operation}`);
    }
  }

  private async enumerate(request: UsbHostRequest): Promise<void> {
    this.cleanupPoisonedPipe();
    if (this.pipe && !this.openReady) {
      throw new Error('USB Host has a partially released pipe; close must succeed before enumerate');
    }
    const result: EnumerateResult = { devices: [] };
    const usbDevices = usbManager.getDevices();
    for (let i = 0; i < usbDevices.length; ++i) {
      const device = usbDevices[i];
      if (!this.isVideoDevice(device)) {
        continue;
      }
      if (!usbManager.hasRight(device.name)) {
        const granted = await usbManager.requestRight(device.name);
        if (!granted) {
          continue;
        }
      }
      let descriptorPipe: Readonly<usbManager.USBDevicePipe>;
      let temporaryPipe = false;
      if (this.pipe && this.device && this.deviceId(this.device) === this.deviceId(device)) {
        descriptorPipe = this.pipe;
      } else {
        descriptorPipe = usbManager.connectDevice(device);
        temporaryPipe = true;
      }
      try {
        const descriptor = usbManager.getRawDescriptor(descriptorPipe);
        result.devices.push({
          deviceId: this.deviceId(device),
          rawDescriptor: this.toNumberArray(descriptor)
        });
      } finally {
        if (temporaryPipe) {
          try {
            this.closeTemporaryPipe(descriptorPipe, device);
          } catch (error) {
            this.poisonedPipe = descriptorPipe;
            this.poisonedDevice = device;
            throw new Error(this.errorText(error));
          }
        }
      }
    }
    const resultJson: string = JSON.stringify(result);
    this.complete(request, true, resultJson);
  }

  private async open(request: UsbHostRequest): Promise<void> {
    this.cleanupPoisonedPipe();
    if (this.pipe) {
      if (this.openReady && this.device && this.deviceId(this.device) === request.deviceId) {
        this.complete(request, true, '{}');
        return;
      }
      if (this.openReady) {
        throw new Error('Another physical USB device already owns the USB Host transport');
      }
      this.closePipe();
    }
    const device = this.findDevice(request.deviceId);
    if (!device) {
      throw new Error(`USB device ${request.deviceId} is no longer connected`);
    }
    if (!usbManager.hasRight(device.name)) {
      const granted = await usbManager.requestRight(device.name);
      if (!granted) {
        throw new Error('USB access was denied');
      }
    }
    const control = this.findInterface(device, request.controlInterface, 0);
    const streaming = this.findInterface(device, request.streamingInterface, 0);
    if (!control || !streaming) {
      throw new Error('Requested UVC VC/VS interfaces were not found');
    }
    const pipe = usbManager.connectDevice(device);
    this.device = device;
    this.pipe = pipe;
    this.openReady = false;
    this.controlInterface = undefined;
    this.streamingInterface = undefined;
    this.selectedInterface = undefined;
    this.selectedEndpoint = undefined;
    const controlResult = usbManager.claimInterface(pipe, control, true);
    if (controlResult !== 0) {
      try {
        this.closePipe();
      } catch (cleanupError) {
        throw new Error(`Failed to claim UVC control interface: ${controlResult}; cleanup failed: ${this.errorText(cleanupError)}`);
      }
      throw new Error(`Failed to claim UVC control interface: ${controlResult}`);
    }
    this.controlInterface = control;
    const streamingResult = usbManager.claimInterface(pipe, streaming, true);
    if (streamingResult !== 0) {
      try {
        this.closePipe();
      } catch (cleanupError) {
        throw new Error(`Failed to claim UVC streaming interface: ${streamingResult}; cleanup failed: ${this.errorText(cleanupError)}`);
      }
      throw new Error(`Failed to claim UVC streaming interface: ${streamingResult}`);
    }
    this.streamingInterface = streaming;
    this.selectedInterface = streaming;
    this.selectedEndpoint = undefined;
    this.openReady = true;
    this.complete(request, true, '{}');
  }

  private select(request: UsbHostRequest): void {
    const device = this.requireDevice();
    const pipe = this.requirePipe();
    if (request.alternateSetting === 0 && !this.findDevice(this.deviceId(device))) {
      this.selectedInterface = undefined;
      this.selectedEndpoint = undefined;
      this.complete(request, true, '{}');
      return;
    }
    const selected = this.findInterface(device, request.streamingInterface, request.alternateSetting);
    if (!selected) {
      throw new Error(`UVC alternate setting ${request.alternateSetting} was not found`);
    }
    const result = usbManager.setInterface(pipe, selected);
    if (result !== 0) {
      throw new Error(`Failed to select UVC alternate setting: ${result}`);
    }
    this.selectedEndpoint = undefined;
    this.selectedInterface = selected;
    this.complete(request, true, '{}');
  }

  private async controlTransfer(request: UsbHostRequest, read: boolean): Promise<void> {
    const pipe = this.requirePipe();
    const maxLength = read ? MAX_CONTROL_READ_BYTES : MAX_CONTROL_WRITE_BYTES;
    if (request.length < 0 || request.length > maxLength) {
      throw new Error(`Invalid USB control length: ${request.length}`);
    }
    const payload = new Uint8Array(request.length);
    if (!read && request.data) {
      const copyLength = Math.min(payload.byteLength, request.data.length);
      for (let i = 0; i < copyLength; ++i) {
        payload[i] = request.data[i];
      }
    }
    const transferred = await usbManager.usbControlTransfer(pipe, {
      bmRequestType: request.requestType,
      bRequest: request.request,
      wValue: request.value,
      wIndex: request.index,
      wLength: request.length,
      data: payload
    }, request.timeoutMs);
    if (transferred < 0 || transferred > request.length) {
      throw new Error(`USB control transfer failed: ${transferred}`);
    }
    if (!read && transferred !== request.length) {
      throw new Error(`Short USB control write: ${transferred}/${request.length}`);
    }
    const responseLength = read ? Math.min(transferred, payload.byteLength) : 0;
    const response = responseLength > 0 ? payload.slice(0, responseLength).buffer as ArrayBuffer : new ArrayBuffer(0);
    this.complete(request, true, JSON.stringify({ transferred: transferred }), response);
  }

  private async start(request: UsbHostRequest): Promise<void> {
    if (this.lifecycleState !== UsbHostLifecycle.IDLE || this.running || this.stopping || this.transfers.length > 0) {
      throw new Error('USB Host transfers are already active or stopping');
    }
    const pipe = this.requirePipe();
    const selected = this.selectedInterface;
    if (!selected) {
      throw new Error('USB Host start requires a selected streaming alternate setting');
    }
    const endpoint = selected.endpoints.find((candidate: usbManager.USBEndpoint): boolean => {
      const supportedType = candidate.type === usbManager.UsbEndpointTransferType.TRANSFER_TYPE_ISOCHRONOUS ||
        candidate.type === usbManager.UsbEndpointTransferType.TRANSFER_TYPE_BULK;
      return candidate.address === request.endpoint && supportedType && (candidate.address & 0x80) !== 0;
    });
    if (!endpoint) {
      throw new Error(`USB Host endpoint ${request.endpoint} was not found on the selected alternate setting`);
    }
    this.selectedEndpoint = endpoint;
    const generation: number = this.lifecycleGeneration;
    this.lifecycleState = UsbHostLifecycle.STARTING;

    if (isIsoEndpoint(endpoint) && this.nativeIsoEnabled) {
      const streaming = this.streamingInterface;
      if (!streaming) {
        throw new Error('Native ISO requires Host VS ownership before handoff');
      }
      const released = usbManager.releaseInterface(pipe, streaming);
      if (released !== 0) {
        throw new Error(`Failed to hand off Host VS interface: ${released}`);
      }
      this.streamingInterface = undefined;
      this.nativeIsoActive = true;
      this.nativeStreamingInterface = request.streamingInterface;
      this.activeSession = request.session;
      const fd: number = usbManager.getFileDescriptor(pipe);
      let responseJson: string = '';
      try {
        responseJson = await sdkOpen.startUsbHostNativeIso(fd, request.session);
      } finally {
        // The IPC result is a caller-owned fd. Core retains its own dup, so the
        // temporary descriptor must be closed even when native startup fails.
        if (fd >= 0 && !sdkOpen.releaseUsbHostFd(fd)) {
          throw new Error('Failed to release temporary USB Host descriptor');
        }
      }
      const response: NativeIsoResult = JSON.parse(responseJson) as NativeIsoResult;
      console.info(`CZUR_NATIVE_ISO_START ${JSON.stringify(response)}`);
      if (!response.success) {
        throw new Error(`Native USB Host ISO start failed: ${response.error}`);
      }
      if (generation !== this.lifecycleGeneration ||
        this.lifecycleState !== UsbHostLifecycle.STARTING || this.activeSession !== request.session) {
        await this.stopTransfers();
        throw new Error('Native USB Host ISO start was cancelled by lifecycle shutdown');
      }
      this.running = true;
      this.lifecycleState = UsbHostLifecycle.RUNNING;
      this.complete(request, true, '{}');
      return;
    }

    const isIso = endpoint.type === usbManager.UsbEndpointTransferType.TRANSFER_TYPE_ISOCHRONOUS;
    const packetCount = isIso ? Math.max(1, request.packetCount) : 0;
    const packetSize = Math.max(1, request.packetSize);
    const bufferLength = isIso ? packetSize * packetCount : packetSize;
    const inFlight = Math.max(1, request.inFlight);
    console.info(`CZUR_USB_STREAM packets=${packetCount} inFlight=${inFlight} packetSize=${packetSize}`);
    this.activeSession = request.session;
    this.nextSequence = 0;
    this.diagnosticTimeMs = Date.now();
    this.diagnosticBatches = 0;
    this.diagnosticPacketErrors = 0;
    this.diagnosticUvcErrors = 0;
    this.running = true;
    this.stopping = false;
    this.lifecycleState = UsbHostLifecycle.RUNNING;
    for (let i = 0; i < inFlight; ++i) {
      const buffer = new Uint8Array(bufferLength);
      const slot: TransferSlot = {
        session: request.session,
        sequence: 0,
        active: false,
        transfer: {
          devPipe: pipe,
          flags: 0,
          endpoint: endpoint.address,
          type: endpoint.type as usbManager.UsbEndpointTransferType,
          timeout: request.timeoutMs,
          length: buffer.byteLength,
          callback: (error: BusinessError, result: usbManager.SubmitTransferCallback): void => {
            this.onTransferComplete(slot, error, result);
          },
          userData: new Uint8Array(0),
          buffer: buffer,
          isoPacketCount: packetCount
        }
      };
      this.transfers.push(slot);
    }
    for (let i = 0; i < this.transfers.length; ++i) {
      this.submit(this.transfers[i]);
      if (!this.running) {
        await this.stopTransfers();
        throw new Error('Failed to submit initial USB Host transfer');
      }
    }
    this.complete(request, true, '{}');
  }

  private submit(slot: TransferSlot): void {
    if (!this.running || this.stopping || slot.session !== this.activeSession) {
      return;
    }
    slot.sequence = this.nextSequence++;
    slot.active = true;
    try {
      usbManager.usbSubmitTransfer(slot.transfer);
    } catch (error) {
      slot.active = false;
      this.failRunningSession(`usbSubmitTransfer failed: ${this.errorText(error)}`);
    }
  }

  private onTransferComplete(slot: TransferSlot, error: BusinessError,
    result: usbManager.SubmitTransferCallback): void {
    slot.active = false;
    this.notifyStopWaitersIfIdle();
    if (slot.session !== this.activeSession || !this.running || this.stopping) {
      return;
    }
    if (error) {
      this.failRunningSession(`USB transfer callback failed: ${this.errorText(error)}`);
      return;
    }
    if (result.status === usbManager.UsbTransferStatus.TRANSFER_NO_DEVICE) {
      this.failRunningSession('USB device was disconnected');
      return;
    }
    const lengths: number[] = [];
    const actualLengths: number[] = [];
    const statuses: number[] = [];
    ++this.diagnosticBatches;
    let diagnosticOffset = 0;
    if (slot.transfer.type === usbManager.UsbEndpointTransferType.TRANSFER_TYPE_ISOCHRONOUS) {
      const overallStatus = result.status === usbManager.UsbTransferStatus.TRANSFER_COMPLETED ?
        usbManager.UsbTransferStatus.TRANSFER_ERROR : result.status;
      if (result.isoPacketDescs.length === 0) {
        lengths.push(Math.min(slot.transfer.length, Math.max(1,
          Math.floor(slot.transfer.length / Math.max(1, slot.transfer.isoPacketCount)))));
        actualLengths.push(0);
        statuses.push(overallStatus);
      } else {
        for (let i = 0; i < result.isoPacketDescs.length; ++i) {
          const descriptor = result.isoPacketDescs[i];
          if (descriptor.status !== usbManager.UsbTransferStatus.TRANSFER_COMPLETED) {
            ++this.diagnosticPacketErrors;
          } else if (descriptor.actualLength >= 2 &&
            diagnosticOffset + descriptor.actualLength <= slot.transfer.buffer.byteLength &&
            (slot.transfer.buffer[diagnosticOffset + 1] & 0x40) !== 0) {
            ++this.diagnosticUvcErrors;
          }
          diagnosticOffset += descriptor.length;
          lengths.push(descriptor.length);
          actualLengths.push(descriptor.actualLength);
          statuses.push(result.status === usbManager.UsbTransferStatus.TRANSFER_COMPLETED ?
            descriptor.status : overallStatus);
        }
      }
    } else {
      lengths.push(slot.transfer.length);
      actualLengths.push(Math.max(0, result.actualLength));
      statuses.push(result.status);
    }
    if (Date.now() - this.diagnosticTimeMs >= 5000) {
      console.info(`CZUR_USB_HEALTH session=${slot.session} seq=${slot.sequence} batches=${this.diagnosticBatches} packetErrors=${this.diagnosticPacketErrors} uvcErrors=${this.diagnosticUvcErrors}`);
      this.diagnosticTimeMs = Date.now();
    }
    const accepted: boolean = sdkOpen.submitUsbHostTransfer(slot.session, slot.sequence,
      slot.transfer.buffer.buffer as ArrayBuffer, new Uint32Array(lengths),
      new Uint32Array(actualLengths), new Uint32Array(statuses));
    if (!accepted) {
      if (!this.running || this.stopping || slot.session !== this.activeSession) {
        return;
      }
      this.failRunningSession('C++ USB transfer queue overflowed or rejected an active transfer');
      return;
    }
    this.submit(slot);
  }

  private async stopTransfers(): Promise<void> {
    this.lifecycleState = UsbHostLifecycle.STOPPING;
    if (this.nativeIsoActive) {
      this.running = false;
      const response: NativeIsoResult = JSON.parse(
        await sdkOpen.stopUsbHostNativeIso(this.activeSession)) as NativeIsoResult;
      console.info(`CZUR_NATIVE_ISO_STOP ${JSON.stringify(response)}`);
      if (!response.success) {
        throw new Error(`Native USB Host ISO stop failed: ${response.error}`);
      }
      const pipe = this.requirePipe();
      const device = this.requireDevice();
      // Reclaim only after native has drained every URB and released VS.
      // A physically removed device has no interface left to reclaim.
      if (this.findDevice(this.deviceId(device))) {
        const streaming = this.findInterface(device, this.nativeStreamingInterface, 0);
        if (!streaming) {
          throw new Error('VS interface missing after native handoff');
        }
        const claimed = usbManager.claimInterface(pipe, streaming, true);
        if (claimed !== 0) {
          throw new Error(`Failed to return VS ownership to USB Host: ${claimed}`);
        }
        this.streamingInterface = streaming;
        this.selectedInterface = streaming;
      } else {
        this.streamingInterface = undefined;
        this.selectedInterface = undefined;
      }
      this.selectedEndpoint = undefined;
      this.nativeIsoActive = false;
      this.activeSession = 0;
      this.lifecycleState = UsbHostLifecycle.IDLE;
      return;
    }

    if (this.transfers.length === 0) {
      this.running = false;
      this.stopping = false;
      this.activeSession = 0;
      this.lifecycleState = UsbHostLifecycle.IDLE;
      return;
    }
    this.running = false;
    this.stopping = true;
    // Let submitted requests complete without resubmitting first. On some
    // HarmonyOS versions usbCancelTransfer suppresses the completion callback,
    // so canceling immediately leaves the slot's ownership unprovable.
    let drained = await this.waitUntilTransfersIdle(500);
    if (drained) {
      this.transfers = [];
      this.stopping = false;
      this.activeSession = 0;
      this.lifecycleState = UsbHostLifecycle.IDLE;
      return;
    }
    for (let i = 0; i < this.transfers.length; ++i) {
      const slot = this.transfers[i];
      if (!slot.active) {
        continue;
      }
      try {
        usbManager.usbCancelTransfer(slot.transfer);
      } catch (_) {
        // A completion may already be queued. Keep the slot until its callback.
      }
    }
    drained = await this.waitUntilTransfersIdle(2000);
    if (!drained) {
      sdkOpen.failUsbHost(this.activeSession, 'Timed out waiting for canceled USB transfers');
      throw new Error('Timed out waiting for canceled USB transfers');
    }
    this.transfers = [];
    this.stopping = false;
    this.activeSession = 0;
    this.lifecycleState = UsbHostLifecycle.IDLE;
  }

  private waitUntilTransfersIdle(timeoutMs: number): Promise<boolean> {
    if (!this.hasActiveTransfers()) {
      return Promise.resolve(true);
    }
    return new Promise<boolean>((resolve: (value: boolean) => void): void => {
      let completed = false;
      const waiter = (): void => {
        if (completed) {
          return;
        }
        completed = true;
        resolve(true);
      };
      this.stopWaiters.push(waiter);
      setTimeout((): void => {
        if (completed) {
          return;
        }
        completed = true;
        const index = this.stopWaiters.indexOf(waiter);
        if (index >= 0) {
          this.stopWaiters.splice(index, 1);
        }
        resolve(false);
      }, timeoutMs);
    });
  }

  private notifyStopWaitersIfIdle(): void {
    if (this.hasActiveTransfers()) {
      return;
    }
    const waiters = this.stopWaiters;
    this.stopWaiters = [];
    for (let i = 0; i < waiters.length; ++i) {
      waiters[i]();
    }
  }

  private hasActiveTransfers(): boolean {
    return this.transfers.some((slot: TransferSlot): boolean => slot.active);
  }

  private failRunningSession(message: string): void {
    const session = this.activeSession;
    if (!this.running && !this.stopping) {
      return;
    }
    this.running = false;
    this.stopping = true;
    sdkOpen.failUsbHost(session, message);
    for (let i = 0; i < this.transfers.length; ++i) {
      if (this.transfers[i].active) {
        try {
          usbManager.usbCancelTransfer(this.transfers[i].transfer);
        } catch (_) {
        }
      }
    }
  }

  private closePipe(): void {
    if (!this.pipe) {
      return;
    }
    if (this.nativeIsoActive || this.hasActiveTransfers()) {
      throw new Error('Cannot close USB pipe while transfer ownership is pending');
    }
    this.openReady = false;
    const failures: string[] = [];
    if (this.device && !this.findDevice(this.deviceId(this.device))) {
      // Native/Host requests were drained above; disconnected interfaces no
      // longer exist. Still close the service pipe to release its cached fd.
      this.streamingInterface = undefined;
      this.controlInterface = undefined;
    }

    if (this.streamingInterface) {
      try {
        const result = usbManager.releaseInterface(this.pipe, this.streamingInterface);
        if (result !== 0) {
          failures.push(`release VS=${result}`);
        } else {
          this.streamingInterface = undefined;
          this.selectedInterface = undefined;
          this.selectedEndpoint = undefined;
        }
      } catch (error) {
        failures.push(`release VS threw ${this.errorText(error)}`);
      }
    }
    if (this.controlInterface) {
      try {
        const result = usbManager.releaseInterface(this.pipe, this.controlInterface);
        if (result !== 0) {
          failures.push(`release VC=${result}`);
        } else {
          this.controlInterface = undefined;
        }
      } catch (error) {
        failures.push(`release VC threw ${this.errorText(error)}`);
      }
    }
    if (!this.streamingInterface && !this.controlInterface) {
      try {
        this.closeServicePipe(this.pipe, this.device);
      } catch (error) {
        failures.push(`close pipe threw ${this.errorText(error)}`);
      }
    }
    if (failures.length > 0) {
      throw new Error(`USB Host cleanup failed: ${failures.join(', ')}`);
    }
    this.pipe = undefined;
    this.device = undefined;
    this.openReady = false;
    this.selectedInterface = undefined;
    this.selectedEndpoint = undefined;
    this.transfers = [];
    this.running = false;
    this.stopping = false;
    this.activeSession = 0;
  }

  private complete(request: UsbHostRequest, success: boolean, resultJson: string,
    data: ArrayBuffer = new ArrayBuffer(0)): void {
    const response = success ? resultJson : JSON.stringify({ error: resultJson });
    sdkOpen.completeUsbHostRequest(request.id, success, response, data);
  }

  private closeTemporaryPipe(pipe: Readonly<usbManager.USBDevicePipe>,
    device: Readonly<usbManager.USBDevice>): void {
    this.closeServicePipe(pipe, device);
  }

  private cleanupPoisonedPipe(): void {
    if (!this.poisonedPipe) {
      return;
    }
    const pipe = this.poisonedPipe;
    this.closeServicePipe(pipe, this.poisonedDevice);
    this.poisonedPipe = undefined;
    this.poisonedDevice = undefined;
  }

  private closeServicePipe(pipe: Readonly<usbManager.USBDevicePipe>,
    device?: Readonly<usbManager.USBDevice>): void {
    const result: number = usbManager.closePipe(pipe);
    if (result === 0) {
      return;
    }
    // UsbService::DelDevice closes the service handle before deleting the
    // device and its access rights. A later Close is mapped to ERR_INVALID_VALUE
    // (22). Accept that result only for the old, absent identity and drained I/O.
    if (result === 22 && device && !this.findDevice(this.deviceId(device)) &&
      !this.nativeIsoActive && !this.hasActiveTransfers()) {
      console.info(`CZUR_USB_DETACHED_PIPE_ALREADY_CLOSED device=${this.deviceId(device)} result=${result}`);
      return;
    }
    throw new Error(`close pipe=${result}`);
  }

  private findDevice(deviceId: number): Readonly<usbManager.USBDevice> | undefined {
    const usbDevices = usbManager.getDevices();
    return usbDevices.find((device: Readonly<usbManager.USBDevice>): boolean => {
      return this.deviceId(device) === deviceId && this.isVideoDevice(device);
    });
  }

  private isVideoDevice(device: Readonly<usbManager.USBDevice>): boolean {
    for (let configIndex = 0; configIndex < device.configs.length; ++configIndex) {
      const interfaces = device.configs[configIndex].interfaces;
      for (let interfaceIndex = 0; interfaceIndex < interfaces.length; ++interfaceIndex) {
        if (interfaces[interfaceIndex].clazz === 0x0e) {
          return true;
        }
      }
    }
    return false;
  }

  private findInterface(device: Readonly<usbManager.USBDevice>, interfaceId: number,
    alternateSetting: number): usbManager.USBInterface | undefined {
    for (let configIndex = 0; configIndex < device.configs.length; ++configIndex) {
      const interfaces = device.configs[configIndex].interfaces;
      for (let interfaceIndex = 0; interfaceIndex < interfaces.length; ++interfaceIndex) {
        const iface = interfaces[interfaceIndex];
        if (iface.id === interfaceId && iface.alternateSetting === alternateSetting) {
          return iface;
        }
      }
    }
    return undefined;
  }

  private deviceId(device: Readonly<usbManager.USBDevice>): number {
    return device.busNum * 65536 + device.devAddress;
  }

  private requireDevice(): Readonly<usbManager.USBDevice> {
    if (!this.device) {
      throw new Error('USB Host device is not open');
    }
    return this.device;
  }

  private requirePipe(): Readonly<usbManager.USBDevicePipe> {
    if (!this.pipe) {
      throw new Error('USB Host pipe is not open');
    }
    return this.pipe;
  }

  private toNumberArray(bytes: Uint8Array): number[] {
    const result: number[] = [];
    for (let i = 0; i < bytes.byteLength; ++i) {
      result.push(bytes[i]);
    }
    return result;
  }

  private errorText(error: Object): string {
    if (error instanceof Error) {
      return error.message;
    }
    return JSON.stringify(error);
  }
}
