/**
 * Named pipe client — talks to debug_bridge.asi inside GTA SA.
 * Protocol: newline-delimited JSON.
 *   → {"id":N,"cmd":"...","arg1":...}
 *   ← {"id":N,"ok":true,"data":{...}}
 *   ← {"id":N,"ok":false,"error":"..."}
 */

import { connect } from "node:net";

const PIPE_NAME = "\\\\.\\pipe\\gtasa_mcp_bridge";
// Queued gameplay commands may legitimately wait up to the bridge's 15s queue
// TTL (loading screens, cutscenes) — give them headroom.
const DEFAULT_TIMEOUT = 20000;

let _idCounter = 1;
const _pending = new Map<number, { resolve: (v: unknown) => void; reject: (e: Error) => void }>();

let _socket: ReturnType<typeof connect> | null = null;
let _buf = "";
let _connectPromise: Promise<void> | null = null;

function onData(chunk: Buffer) {
  _buf += chunk.toString("utf8");
  const lines = _buf.split("\n");
  _buf = lines.pop() ?? "";
  for (const line of lines) {
    if (!line.trim()) continue;
    try {
      const msg = JSON.parse(line) as { id: number; ok: boolean; data?: unknown; error?: string };
      const pending = _pending.get(msg.id);
      if (!pending) continue;
      _pending.delete(msg.id);
      if (msg.ok) pending.resolve(msg.data ?? null);
      else pending.reject(new Error(msg.error ?? "bridge error"));
    } catch {
      // malformed line — ignore
    }
  }
}

function onClose() {
  _socket = null;
  _connectPromise = null;
  for (const [, p] of _pending) p.reject(new Error("pipe disconnected"));
  _pending.clear();
}

export function isConnected(): boolean {
  return _socket !== null && !_socket.destroyed;
}

export async function ensureConnected(timeoutMs = DEFAULT_TIMEOUT): Promise<void> {
  if (isConnected()) return;
  if (_connectPromise) return _connectPromise;

  _connectPromise = new Promise<void>((resolve, reject) => {
    const sock = connect(PIPE_NAME);
    const timer = setTimeout(() => {
      sock.destroy();
      _connectPromise = null;
      reject(new Error("Pipe connect timeout — is debug_bridge.asi loaded and mcp_debug.lock present?"));
    }, timeoutMs);

    sock.once("connect", () => {
      clearTimeout(timer);
      _socket = sock;
      sock.on("data", onData);
      sock.once("close", onClose);
      sock.once("error", onClose);
      resolve();
    });

    sock.once("error", (err) => {
      clearTimeout(timer);
      _connectPromise = null;
      reject(new Error(`Pipe connect failed: ${err.message}`));
    });
  });

  return _connectPromise;
}

export function disconnect() {
  _socket?.destroy();
  _socket = null;
  _connectPromise = null;
}

// ---- background game-state cache ------------------------------------------
// Polled every 100ms while connected so status reads are instant and stale
// connections are noticed proactively. Poll failures simply invalidate the
// cache (null = no fresh data) — no data is invented on failure.

interface GameStateData {
  scene: string;
  menuVisible: boolean;
  playerReady: boolean;
  cutsceneActive?: boolean;
}

let _cachedState: { data: GameStateData; at: number } | null = null;

// Fire-and-forget by design: a failed poll means "no fresh state", represented
// by the cache being null — errors are intentionally not propagated.
const _statePollTimer = setInterval(() => {
  if (!isConnected()) {
    _cachedState = null;
    return;
  }
  send<GameStateData>("game_state", {}, 2000)
    .then((data) => { _cachedState = { data, at: Date.now() }; })
    .catch(() => { _cachedState = null; });
}, 100);
if (typeof _statePollTimer.unref === "function") _statePollTimer.unref();

export function getCachedGameState(maxAgeMs = 1000): (GameStateData & { ageMs: number }) | null {
  if (!_cachedState) return null;
  const ageMs = Date.now() - _cachedState.at;
  if (ageMs > maxAgeMs) return null;
  return { ..._cachedState.data, ageMs };
}

export async function send<T = unknown>(
  cmd: string,
  params: Record<string, unknown> = {},
  timeoutMs = DEFAULT_TIMEOUT
): Promise<T> {
  await ensureConnected(timeoutMs);

  const id = _idCounter++;
  const msg = JSON.stringify({ id, cmd, ...params }) + "\n";

  return new Promise<T>((resolve, reject) => {
    const timer = setTimeout(() => {
      _pending.delete(id);
      reject(new Error(`Command '${cmd}' timed out after ${timeoutMs}ms`));
    }, timeoutMs);

    _pending.set(id, {
      resolve: (v) => { clearTimeout(timer); resolve(v as T); },
      reject: (e) => { clearTimeout(timer); reject(e); },
    });

    _socket!.write(msg);
  });
}
