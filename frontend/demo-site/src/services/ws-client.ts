import type { CommandEvent, CommandRequest, CommandResponse } from './protocol';

export class CommandWsConnectError extends Error {
  readonly closeCode?: number;
  readonly closeReason?: string;

  constructor(message: string, options: { closeCode?: number; closeReason?: string } = {}) {
    super(message);
    this.name = 'CommandWsConnectError';
    this.closeCode = options.closeCode;
    this.closeReason = options.closeReason;
  }
}

interface PendingRequest {
  resolve: (response: CommandResponse<Record<string, unknown>>) => void;
  reject: (error: Error) => void;
}

type EventListener = (event: CommandEvent<Record<string, unknown>>) => void;
const MAX_BUFFERED_EVENTS = 8;

export class CommandWsClient {
  private readonly url: string;
  private socket: WebSocket | null = null;
  private connectPromise: Promise<void> | null = null;
  private pendingRequests = new Map<string, PendingRequest>();
  private eventListeners = new Set<EventListener>();
  private bufferedEvents: CommandEvent<Record<string, unknown>>[] = [];

  constructor(url: string) {
    this.url = url;
  }

  async connect(): Promise<void> {
    if (this.socket?.readyState === WebSocket.OPEN) {
      return;
    }
    if (this.connectPromise) {
      return this.connectPromise;
    }

    this.connectPromise = new Promise<void>((resolve, reject) => {
      const socket = new WebSocket(this.url);
      let settled = false;

      socket.addEventListener('open', () => {
        this.socket = socket;
        settled = true;
        resolve();
      });

      socket.addEventListener('message', (event) => {
        this.handleMessage(event.data);
      });

      socket.addEventListener('error', () => {
        if (!settled) {
          settled = true;
          reject(new CommandWsConnectError('websocket connect failed'));
        }
      });

      socket.addEventListener('close', (event) => {
        if (this.socket === socket) {
          this.socket = null;
        }
        this.rejectPendingRequests(new Error(event.reason || 'websocket closed'));
        if (!settled) {
          settled = true;
          reject(
            new CommandWsConnectError(event.reason || 'websocket connect failed', {
              closeCode: event.code,
              closeReason: event.reason,
            }),
          );
        }
      });
    }).finally(() => {
      this.connectPromise = null;
    });

    return this.connectPromise;
  }

  disconnect(reason = 'manual disconnect'): void {
    const socket = this.socket;
    this.socket = null;
    if (socket && socket.readyState < WebSocket.CLOSING) {
      socket.close(1000, reason);
    }
    this.rejectPendingRequests(new Error(reason));
  }

  async send(request: CommandRequest): Promise<CommandResponse<Record<string, unknown>>> {
    if (this.socket?.readyState !== WebSocket.OPEN) {
      throw new Error('websocket not connected');
    }

    return new Promise<CommandResponse<Record<string, unknown>>>((resolve, reject) => {
      this.pendingRequests.set(request.request_id, { resolve, reject });
      this.socket?.send(JSON.stringify(request));
    });
  }

  onEvent(listener: EventListener): () => void {
    this.eventListeners.add(listener);
    if (this.bufferedEvents.length > 0) {
      const bufferedEvents = [...this.bufferedEvents];
      this.bufferedEvents = [];
      for (const event of bufferedEvents) {
        listener(event);
      }
    }
    return () => {
      this.eventListeners.delete(listener);
    };
  }

  private handleMessage(data: unknown): void {
    if (typeof data !== 'string') {
      return;
    }

    let payload: Record<string, unknown> | null = null;
    try {
      payload = JSON.parse(data) as Record<string, unknown>;
    } catch {
      return;
    }

    if (payload && typeof payload.event === 'string') {
      this.emitEvent(payload as CommandEvent<Record<string, unknown>>);
      return;
    }

    const requestId =
      typeof payload.request_id === 'string'
        ? payload.request_id
        : '';
    if (!requestId) {
      return;
    }

    const pendingRequest = this.pendingRequests.get(requestId);
    if (!pendingRequest) {
      return;
    }

    this.pendingRequests.delete(requestId);
    pendingRequest.resolve(payload as CommandResponse<Record<string, unknown>>);
  }

  private rejectPendingRequests(error: Error): void {
    for (const [, pendingRequest] of this.pendingRequests) {
      pendingRequest.reject(error);
    }
    this.pendingRequests.clear();
  }

  private emitEvent(event: CommandEvent<Record<string, unknown>>): void {
    if (this.eventListeners.size === 0) {
      this.bufferedEvents.push(event);
      if (this.bufferedEvents.length > MAX_BUFFERED_EVENTS) {
        this.bufferedEvents.shift();
      }
      return;
    }
    for (const listener of this.eventListeners) {
      listener(event);
    }
  }
}
