#!/usr/bin/env node
// Agent Pet interaction bridge for Codex (docs/design/agent-interactions-provider-proof.md, starter/docs/interactions.md).
// Unlike the status hooks, this shares message text and answers questions, so it is started only by an
// explicit opt-in. It talks to the shared Codex app-server daemon over its control socket (JSON-RPC in
// WebSocket frames) and to Agent Pet's private interaction stream, and uses no dependencies beyond Node 20.
//
//   node codex-bridge.mjs [--endpoint <socket path or pipe>] [--codex-socket <path>]
//
// The daemon socket defaults to $CODEX_HOME/app-server-control/app-server-control.sock (~/.codex when unset).
// Message text is never logged.
import { createHash, randomBytes, randomUUID } from "node:crypto";
import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { FrameDecoder, MAX_FRAME, defaultEndpoint, encodeFrame, fitText } from "./opencode-bridge.mjs";

export const PROVIDER = "codex";
const TAIL = 50;          // Messages sent per snapshot.
const MAX_TRACKED = 16;   // Loaded threads tracked at startup (plus any asking a question).
const MAX_MESSAGE = 64 * 1024 * 1024; // A whole thread read can be large; anything beyond is hostile.
const QUESTION_METHOD = "item/tool/requestUserInput";
// No idempotent_commands: the daemon does not deduplicate turn/start, so a retry is only as safe as this
// process's memory of command IDs (which a restart loses). The bridge still never retries by itself.
const CAPABILITIES = ["messages", "single_choice", "custom_answer", "follow_up", "reconcile"];

// ---- WebSocket (RFC 6455, just enough for the daemon) -------------------------------------------------

const GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
export const OP = { cont: 0, text: 1, binary: 2, close: 8, ping: 9, pong: 10 };

// One final frame; client frames are masked, which the server requires.
export const wsFrame = (opcode, payload = Buffer.alloc(0), mask = true) => {
  const n = payload.length;
  const flag = mask ? 0x80 : 0;
  let head;
  if (n < 126) head = Buffer.from([0x80 | opcode, flag | n]);
  else if (n < 65536) head = Buffer.from([0x80 | opcode, flag | 126, n >> 8, n & 255]);
  else { head = Buffer.alloc(10); head[0] = 0x80 | opcode; head[1] = flag | 127; head.writeBigUInt64BE(BigInt(n), 2); }
  if (!mask) return Buffer.concat([head, payload]);
  const key = randomBytes(4);
  const body = Buffer.alloc(n);
  for (let i = 0; i < n; i++) body[i] = payload[i] ^ key[i & 3];
  return Buffer.concat([head, key, body]);
};

// Reassembles frames into messages: data messages arrive whole (fragments joined), control frames as they
// come. Throws on a protocol violation or a message past `limit`.
export class WsDecoder {
  #chunks = [];
  #length = 0;
  #need = 2;      // Bytes the next parse step needs; avoids re-joining a large frame for every chunk.
  #parts = [];
  #partsSize = 0;
  #opcode = 0;
  constructor(limit = MAX_MESSAGE) { this.limit = limit; }

  push(chunk) {
    this.#chunks.push(chunk);
    this.#length += chunk.length;
    const out = [];
    if (this.#length < this.#need) return out;
    let buf = Buffer.concat(this.#chunks);
    for (;;) {
      this.#need = 2;
      if (buf.length < 2) break;
      const fin = (buf[0] & 0x80) !== 0;
      const op = buf[0] & 0x0f;
      const masked = (buf[1] & 0x80) !== 0;
      let len = buf[1] & 0x7f;
      let offset = 2;
      if (len === 126) {
        if (buf.length < 4) { this.#need = 4; break; }
        len = buf.readUInt16BE(2);
        offset = 4;
      } else if (len === 127) {
        if (buf.length < 10) { this.#need = 10; break; }
        const big = buf.readBigUInt64BE(2);
        if (big > BigInt(this.limit)) throw new Error("websocket frame too large");
        len = Number(big);
        offset = 10;
      }
      if (len > this.limit) throw new Error("websocket frame too large");
      if (op >= 8 && (!fin || len > 125)) throw new Error("invalid websocket control frame");
      const total = offset + (masked ? 4 : 0) + len;
      if (buf.length < total) { this.#need = total; break; }
      let payload = buf.subarray(offset + (masked ? 4 : 0), total);
      if (masked) {
        const key = buf.subarray(offset, offset + 4);
        payload = Buffer.from(payload.map((b, i) => b ^ key[i & 3]));
      }
      buf = buf.subarray(total);
      if (op >= 8) {
        out.push({ opcode: op, payload });
        continue;
      }
      if (op === OP.cont) { if (this.#parts.length === 0) throw new Error("unexpected continuation frame"); }
      else {
        if (this.#parts.length) throw new Error("interleaved websocket message");
        this.#opcode = op;
      }
      this.#parts.push(payload);
      this.#partsSize += payload.length;
      if (this.#partsSize > this.limit) throw new Error("websocket message too large");
      if (fin) {
        out.push({ opcode: this.#opcode, payload: Buffer.concat(this.#parts) });
        this.#parts = [];
        this.#partsSize = 0;
      }
    }
    this.#chunks = buf.length ? [buf] : [];
    this.#length = buf.length;
    return out;
  }
}

// ---- Codex app-server connection ------------------------------------------------------------------------

export const defaultCodexSocket = () =>
  path.join(process.env.CODEX_HOME || path.join(os.homedir(), ".codex"), "app-server-control", "app-server-control.sock");

class RpcError extends Error {
  constructor(message, code) { super(message); this.name = "RpcError"; this.code = code; }
}

// Opens the control socket, upgrades it to a WebSocket and runs the JSON-RPC handshake. The socket path is
// usually a symlink to a long /tmp path, so it is resolved first (a unix socket path is limited to ~108 bytes).
// handlers: { notification(method, params), request(id, method, params) }, called synchronously per message.
export async function connectCodex({ socketPath = defaultCodexSocket(), handlers = {}, timeoutMs = 20000, connectMs = 5000,
                                     clientInfo = { name: "agent-pet", title: "Agent Pet", version: "1" } } = {}) {
  const real = fs.realpathSync(socketPath);
  const socket = net.connect(real);
  const decoder = new WsDecoder();
  const pending = new Map();
  let nextId = 0;
  let closed = false;
  let closeResolve;
  const closedPromise = new Promise((resolve) => { closeResolve = resolve; });

  const fail = (error) => {
    for (const p of pending.values()) { clearTimeout(p.timer); p.reject(error); }
    pending.clear();
  };
  const finish = () => {
    if (closed) return;
    closed = true;
    fail(new RpcError("codex connection closed", "closed"));
    socket.destroy();
    closeResolve();
  };
  socket.on("error", () => {});
  socket.on("close", finish);

  const dispatch = (message) => {
    if (message === null || typeof message !== "object") return;
    const { id, method } = message;
    if (typeof method === "string") {
      if (id !== undefined && id !== null) handlers.request?.(id, method, message.params ?? {});
      else handlers.notification?.(method, message.params ?? {});
    } else if (id !== undefined && pending.has(id)) {
      const p = pending.get(id);
      pending.delete(id);
      clearTimeout(p.timer);
      if (message.error) p.reject(new RpcError(String(message.error.message ?? "error").slice(0, 300), message.error.code));
      else p.resolve(message.result);
    }
  };

  const onMessages = (messages) => {
    for (const { opcode, payload } of messages) {
      if (opcode === OP.text) {
        let parsed;
        try { parsed = JSON.parse(payload.toString("utf8")); } catch { continue; }
        try { dispatch(parsed); } catch { /* A handler's problem never takes the connection down. */ }
      } else if (opcode === OP.ping) socket.write(wsFrame(OP.pong, payload));
      else if (opcode === OP.close) { socket.write(wsFrame(OP.close, payload.subarray(0, 2))); finish(); }
    }
  };

  // Upgrade: request, then the response head; whatever follows the head is already WebSocket data.
  const key = randomBytes(16).toString("base64");
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => { reject(new Error("codex handshake timed out")); socket.destroy(); }, connectMs);
    let head = Buffer.alloc(0);
    socket.once("error", (error) => { clearTimeout(timer); reject(error); });
    socket.once("close", () => { clearTimeout(timer); reject(new Error("codex socket closed during handshake")); });
    socket.once("connect", () => {
      socket.write(`GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`);
    });
    const onData = (chunk) => {
      head = Buffer.concat([head, chunk]);
      const end = head.indexOf("\r\n\r\n");
      if (end < 0) { if (head.length > 16 * 1024) { reject(new Error("codex handshake too long")); socket.destroy(); } return; }
      socket.off("data", onData);
      clearTimeout(timer);
      const lines = head.subarray(0, end).toString("latin1").split("\r\n");
      const accept = lines.find((l) => /^sec-websocket-accept:/i.test(l))?.split(":")[1]?.trim();
      const expected = createHash("sha1").update(key + GUID).digest("base64");
      if (!/^HTTP\/1\.1 101/.test(lines[0]) || accept !== expected) { reject(new Error(`codex upgrade refused: ${lines[0]}`)); socket.destroy(); return; }
      resolve();
      const rest = head.subarray(end + 4);
      socket.on("data", (data) => {
        try { onMessages(decoder.push(data)); } catch { finish(); }
      });
      if (rest.length) { try { onMessages(decoder.push(rest)); } catch { finish(); } }
    };
    socket.on("data", onData);
  });

  const write = (object) => {
    if (closed) throw new RpcError("codex connection closed", "closed");
    socket.write(wsFrame(OP.text, Buffer.from(JSON.stringify(object))));
  };
  const connection = {
    closed: closedPromise,
    get alive() { return !closed; },
    call(method, params, ms = timeoutMs) {
      return new Promise((resolve, reject) => {
        const id = ++nextId;
        const timer = setTimeout(() => { pending.delete(id); reject(new RpcError(`${method} timed out`, "timeout")); }, ms);
        pending.set(id, { resolve, reject, timer });
        try { write({ id, method, ...(params === undefined ? {} : { params }) }); }
        catch (error) { pending.delete(id); clearTimeout(timer); reject(error); }
      });
    },
    notify(method, params) { write({ method, ...(params === undefined ? {} : { params }) }); },
    respond(id, result) { write({ id, result }); },
    close() { try { socket.write(wsFrame(OP.close)); } catch { /* Already gone. */ } finish(); },
  };
  try {
    await connection.call("initialize", { clientInfo, capabilities: { experimentalApi: true } });
    connection.notify("initialized", {});
  } catch (error) { connection.close(); throw error; }
  return connection;
}

// ---- Mapping ----------------------------------------------------------------------------------------

const basename = (directory) => (directory ? path.basename(String(directory).replace(/[\\/]+$/, "")) || String(directory) : "");

export const statusOf = (status) => ({
  type: typeof status?.type === "string" ? status.type : "notLoaded",
  waiting: Array.isArray(status?.activeFlags) && status.activeFlags.includes("waitingOnUserInput"),
});

// Sub-agent threads are not conversations the user opened; they appear only when they ask something.
export const isSubAgent = (thread) => Boolean(thread?.parentThreadId) || (typeof thread?.source === "object" && thread.source !== null && "subAgent" in thread.source);

// One thread item → the message it shows, or null. Reasoning, commands, tool calls and commentary
// (mid-turn narration, phase "commentary") are not messages; an unknown phase is shown.
export const itemMessage = (item, created = 0) => {
  if (!item || typeof item.id !== "string") return null;
  if (item.type === "userMessage") {
    const text = (item.content ?? []).filter((p) => p?.type === "text" && typeof p.text === "string").map((p) => p.text).join("\n");
    return text.trim() ? { id: item.id, role: "user", text, created } : null;
  }
  if (item.type === "agentMessage" && item.phase !== "commentary")
    return typeof item.text === "string" && item.text.trim() ? { id: item.id, role: "agent", text: item.text, created } : null;
  return null;
};

// All visible messages of a thread read with turns, in order. The last item of a turn still in progress
// may be streaming, so an agent message there waits for the turn to finish (a user message never streams),
// unless the thread is known to be idle.
export const threadMessages = (thread, settled = false) => {
  const out = [];
  for (const turn of thread?.turns ?? []) {
    const created = Math.round((turn.startedAt ?? turn.completedAt ?? 0) * 1000);
    const items = turn.items ?? [];
    items.forEach((item, i) => {
      if (!settled && turn.status === "inProgress" && i === items.length - 1 && item?.type === "agentMessage") return;
      const m = itemMessage(item, created);
      if (m) out.push(m);
    });
  }
  return out;
};

// A single-question form with options, or free text, is the one shape the first release can answer. Several
// questions and secret input stay with the terminal. Answers are keyed by question ID and carry the label.
export const requestFromQuestions = (params) => {
  const questions = params?.questions;
  if (!params || typeof params.itemId !== "string" || !Array.isArray(questions) || questions.length !== 1) return null;
  const q = questions[0];
  if (!q || typeof q.id !== "string" || q.isSecret === true) return null;
  const question = typeof q.question === "string" && q.question.trim() ? q.question : typeof q.header === "string" ? q.header : "";
  if (!question.trim()) return null;
  const options = [];
  for (const o of q.options ?? []) {
    if (typeof o?.label !== "string" || !o.label || options.some((x) => x.id === o.label)) return null;
    options.push({ id: o.label, label: o.label });
  }
  const custom = options.length === 0 || q.isOther === true;
  return { qid: q.id, request: { request_id: params.itemId, revision: 1, question, options, custom_answer: custom, state: "pending" } };
};

// ---- Bridge core ------------------------------------------------------------------------------------
//
// send(frame) → boolean: false when the Agent Pet link is down (state is rebuilt by snapshot on reconnect).
// The Codex connection is attached with codexUp(connection) and removed with codexLost(); between the two
// the bridge keeps what it knows and refuses commands that would need the daemon.
export function createBridge({ send, log = () => {}, tail = TAIL, maxTracked = MAX_TRACKED, generation = () => randomUUID(),
                              answerWaitMs = 5000, requestMs = 20000 }) {
  const conversations = new Map(); // threadId → state
  const chains = new Map();
  const ensuring = new Map();
  let rpc = null;
  let rpcEpoch = 0; // Counts Codex connections: a question's RPC ID and a sent command belong to one.
  let linked = false;
  let linkGeneration = generation();

  const serial = (threadId, work) => {
    const next = (chains.get(threadId) ?? Promise.resolve()).then(work).catch((error) => log(`thread ${threadId}: ${error.message}`));
    chains.set(threadId, next);
    return next;
  };
  const call = (method, params, ms) => {
    if (!rpc) throw new RpcError("codex unavailable", "unavailable");
    return rpc.call(method, params, ms);
  };

  const keyOf = (c) => ({ provider: PROVIDER, session_id: c.id, conversation_epoch: c.epoch });
  const pendingRequests = (c) => [...c.requests.values()].filter((e) => e.state === "pending");
  const readyOf = (c) => !c.ended && rpc !== null && c.status === "idle" && pendingRequests(c).length === 0;
  const entryOf = (c) => ({ conversation: keyOf(c), title: c.title, ready: readyOf(c), capabilities: CAPABILITIES });
  const emit = (frame) => (linked ? send({ version: 1, ...frame }) : false);
  // Tells the service when readiness changed since it last heard (a snapshot or hello counts as hearing).
  const syncReady = (c) => {
    const ready = readyOf(c);
    if (c.registered && c.lastReady !== ready) emit({ type: "ready", conversation: keyOf(c), ready });
    c.lastReady = ready;
  };

  const remember = (map, key, value, limit = 200) => {
    map.delete(key);
    map.set(key, value);
    while (map.size > limit) map.delete(map.keys().next().value);
  };

  // ---- Threads ---------------------------------------------------------------------------------------

  const newConversation = (thread) => ({
    id: thread.id, epoch: String(thread.createdAt ?? "1"), title: basename(thread.cwd) || thread.name || thread.id,
    sub: isSubAgent(thread), status: statusOf(thread.status).type, waiting: statusOf(thread.status).waiting,
    registered: false, opening: false, ended: false, synced: false, attached: 0, lastReady: undefined,
    known: new Map(), last: 0, window: [], requests: new Map(), resolved: new Map(), commands: new Map(),
  });

  const readThread = async (threadId, includeTurns) => (await call("thread/read", { threadId, includeTurns }, 30000))?.thread;

  // Registers a thread once, even when several callers ask at the same time: it enters the map only after
  // its history is numbered. Content follows through the service's resync → snapshot.
  const adopt = async (thread) => {
    const known = conversations.get(thread.id);
    if (known) return known;
    const c = newConversation(thread);
    if (!c.sub) await readMessages(c);
    conversations.set(c.id, c);
    return c;
  };
  const ensure = (threadId) => {
    const known = conversations.get(threadId);
    if (known) return Promise.resolve(known);
    if (!ensuring.has(threadId)) ensuring.set(threadId, (async () => {
      const thread = await readThread(threadId, false);
      return thread?.id ? adopt(thread) : null;
    })().finally(() => ensuring.delete(threadId)));
    return ensuring.get(threadId);
  };

  // Gives an unseen message the next sequence number; returns it, or null if already known.
  const absorb = (c, m, history) => {
    if (c.known.has(m.id)) return null;
    const message = { ...m, seq: ++c.last, history };
    c.known.set(m.id, message.seq);
    c.window.push(message);
    if (c.window.length > tail) c.window.shift();
    return message;
  };

  const messageFrame = (c, m) => ({
    type: "message", conversation: keyOf(c),
    message: { message_id: m.id, sequence: m.seq, role: m.role, text: fitText(m.text), created_at: m.created, ...(m.history ? { history: true } : {}) },
  });

  // Reads the whole thread (messages are numbered from its start) and publishes what is new. Returns the
  // thread. The first read marks what it finds as history. A thread without a first turn has nothing to read.
  const readMessages = async (c) => {
    let thread;
    try { thread = await readThread(c.id, true); }
    catch (error) {
      if (error.code === "closed" || error.code === "unavailable" || error.code === "timeout") throw error;
      if (!c.synced) { c.synced = true; return null; } // Not materialized yet: empty so far.
      throw error;
    }
    const fresh = [];
    for (const m of threadMessages(thread, statusOf(thread?.status).type === "idle")) {
      const added = absorb(c, m, !c.synced);
      if (added) fresh.push(added);
    }
    c.synced = true;
    if (c.registered) for (const m of fresh) emit(messageFrame(c, m));
    return thread;
  };

  // A burst of item notifications costs one read.
  const scheduleRefresh = (c) => {
    if (c.refreshQueued) return;
    c.refreshQueued = true;
    serial(c.id, async () => { c.refreshQueued = false; if (!c.ended && rpc && (!c.sub || c.requests.size)) await readMessages(c); });
  };

  // ---- Questions -------------------------------------------------------------------------------------

  const publishState = (c, entry) => {
    if (c.registered) emit({ type: "request_state", conversation: keyOf(c), request_id: entry.request.request_id, state: entry.state, ...(entry.replacedBy ? { replaced_by: entry.replacedBy } : {}) });
  };

  // Ends a pending question; whoever waits for it is woken. Our own answer in flight publishes the new state
  // itself, after its command result.
  const settle = (c, entry, state, replacedBy) => {
    if (entry.state !== "pending") return;
    entry.state = state;
    entry.request = { ...entry.request, state };
    if (replacedBy) entry.replacedBy = replacedBy;
    remember(c.resolved, entry.request.request_id, entry, 20);
    c.requests.delete(entry.request.request_id);
    for (const wake of entry.waiters.splice(0)) wake(true);
    if (!entry.answering) publishState(c, entry);
    syncReady(c);
  };

  // A pending question is answered by whoever resolves it; that is "answered" unless the turn was cut short.
  const settleAll = (c, state = "expired") => { for (const e of pendingRequests(c)) settle(c, e, e.sent ? "answered" : state); };

  const lateResult = (c, entry) => {
    // Our answer was reported unknown; the question has now been resolved, which is what we sent it for.
    if (entry.unconfirmed && c.commands.get(entry.unconfirmed)?.outcome === "unknown") {
      result(c, entry.unconfirmed, "accepted");
    }
    entry.unconfirmed = undefined;
  };

  const adoptQuestion = async (c, rpcId, params) => {
    const mapped = requestFromQuestions(params);
    if (!mapped) { log(`thread ${c.id}: a question this bridge cannot show was left to the terminal`); return; }
    const id = mapped.request.request_id;
    const known = c.requests.get(id);
    if (known) { known.rpcId = rpcId; known.rpcEpoch = rpcEpoch; return; } // Replayed after a reconnect.
    if (c.resolved.has(id)) return;
    for (const other of pendingRequests(c)) settle(c, other, "replaced", id);
    const entry = { qid: mapped.qid, request: mapped.request, state: "pending", rpcId, rpcEpoch, waiters: [] };
    c.requests.set(id, entry);
    if (c.sub && !c.synced) await readMessages(c).catch((e) => log(`thread ${c.id}: ${e.message}`));
    if (c.registered) { emit({ type: "request", conversation: keyOf(c), request: entry.request }); syncReady(c); }
    else announce(c);
  };

  // Joins the thread as a subscriber: Codex sends a question only to the connection that started the turn,
  // and replays a pending one to a connection that resumes the thread. Only threads the daemon has loaded
  // and that are waiting for input are resumed, never saved ones.
  const attach = async (c) => {
    if (!rpc || c.attached === rpcEpoch) return;
    c.attached = rpcEpoch;
    try { await call("thread/resume", { threadId: c.id, excludeTurns: true }, 30000); }
    catch (error) {
      if (error.code !== "closed") c.attached = 0; // Try again at the next status change.
      log(`thread ${c.id}: resume failed: ${error.message}`);
    }
  };

  // ---- Status ----------------------------------------------------------------------------------------

  const setStatus = (c, status) => {
    const s = statusOf(status);
    c.status = s.type;
    c.waiting = s.waiting;
    syncReady(c);
  };

  const announce = (c) => {
    if (c.registered || c.opening || c.ended || !linked) return;
    if (c.sub && c.requests.size === 0) return; // Sub-agents appear only when they ask something.
    c.opening = true;
    emit({ type: "conversation_open", ...entryOf(c) }); // The service answers with a resync → snapshot.
  };

  // The thread's new status, applied after reading what it says: the final answer is published before the
  // thread counts as ready again.
  const applyStatus = async (c, status) => {
    const idle = statusOf(status).type === "idle";
    const reads = !c.sub || c.requests.size > 0;
    if (idle && reads) await readMessages(c);
    setStatus(c, status);
    if (c.waiting) await attach(c);
    else if (c.requests.size) settleAll(c, c.turnStatus === "interrupted" || c.turnStatus === "failed" ? "cancelled" : "expired");
    if (!idle && (!c.sub || c.requests.size > 0)) await readMessages(c);
    announce(c);
  };

  const endConversation = (c) => {
    if (c.ended) return;
    c.ended = true;
    if (c.registered) emit({ type: "conversation_ended", conversation: keyOf(c) });
    conversations.delete(c.id);
  };

  // ---- Notifications and server requests ----------------------------------------------------------------

  const handleNotification = async (method, params) => {
    if (!rpc) return;
    const threadId = params?.threadId ?? params?.thread?.id;
    if (typeof threadId !== "string") return;
    const tracked = () => conversations.get(threadId);
    if (method === "thread/archived" || method === "thread/deleted") {
      const c = tracked();
      if (c) await serial(threadId, async () => endConversation(c));
    } else if (method === "thread/closed") {
      const c = tracked(); // Unloaded from the daemon, not deleted: not ready, but it may come back.
      if (c) await serial(threadId, async () => { setStatus(c, { type: "notLoaded" }); settleAll(c); });
    } else if (method === "thread/started") {
      await serial(threadId, async () => { const c = await ensure(threadId); if (c) { announce(c); } });
    } else if (method === "thread/status/changed") {
      await serial(threadId, async () => {
        const s = statusOf(params.status);
        let c = tracked();
        if (!c && (s.type === "active" || s.type === "idle")) c = await ensure(threadId);
        if (!c) return;
        await applyStatus(c, params.status);
      });
    } else if (method === "turn/started" || method === "turn/completed") {
      const c = tracked();
      if (!c) return;
      c.turnStatus = params.turn?.status;
      if (method === "turn/completed") scheduleRefresh(c);
    } else if (method === "item/completed") {
      const c = tracked();
      if (c && (params.item?.type === "userMessage" || params.item?.type === "agentMessage")) scheduleRefresh(c);
    } else if (method === "serverRequest/resolved") {
      // Not queued behind the thread's other work: an answer in flight is waiting for exactly this.
      const c = tracked();
      const entry = c && pendingRequests(c).find((e) => e.rpcId === params.requestId && e.rpcEpoch === rpcEpoch);
      if (!entry) return;
      settle(c, entry, c.turnStatus === "interrupted" || c.turnStatus === "failed" ? "cancelled" : "answered");
      lateResult(c, entry);
    }
    // Everything else (deltas, token usage, plans, diffs, …) is ignored; unknown methods are tolerated.
  };

  // Only a single-question user-input request is ours. Approvals and everything else are never answered,
  // not even to refuse: another client may be waiting to answer them.
  const handleServerRequest = async (rpcId, method, params) => {
    if (!rpc || method !== QUESTION_METHOD || typeof params?.threadId !== "string") return;
    await serial(params.threadId, async () => {
      const c = await ensure(params.threadId);
      if (c) await adoptQuestion(c, rpcId, params);
    });
  };

  // ---- Snapshots -------------------------------------------------------------------------------------

  const snapshotFrame = (c) => {
    const requests = [...c.requests.values(), ...c.resolved.values()].map((e) => e.request);
    const outcomes = [...c.commands].filter(([, r]) => r.outcome !== "unknown")
      .map(([command_id, r]) => ({ command_id, outcome: r.outcome, ...(r.reason ? { reason: r.reason } : {}), ...(r.still_answerable === false ? { still_answerable: false } : {}) }));
    return {
      type: "snapshot", conversation: keyOf(c), watermark: c.last, first_sequence: c.window[0]?.seq ?? 0,
      ready: readyOf(c), capabilities: CAPABILITIES, requests,
      messages: c.window.map((m) => messageFrame(c, m).message), command_outcomes: outcomes,
    };
  };

  const sendSnapshot = async (c) => {
    // Fresh state first: whatever happened while the link was down is part of the snapshot.
    if (rpc) {
      await resolveUncertain(c, false);
      await readMessages(c).catch((error) => log(`thread ${c.id}: ${error.message}`));
    }
    c.registered = true;
    c.lastReady = readyOf(c);
    emit(snapshotFrame(c));
  };

  // ---- Commands --------------------------------------------------------------------------------------

  const result = (c, commandId, outcome, reason, stillAnswerable = true, receipt) => {
    const prior = c.commands.get(commandId) ?? {};
    const record = { ...prior, outcome, reason, still_answerable: stillAnswerable, receipt };
    remember(c.commands, commandId, record, 100);
    emitResult(commandId, record);
  };
  const emitResult = (commandId, r) => emit({ type: "command_result", command_id: commandId, outcome: r.outcome,
    ...(r.reason ? { reason: r.reason } : {}), ...(r.receipt ? { receipt: r.receipt } : {}), still_answerable: r.still_answerable !== false });

  const waitSettled = (entry, ms) => new Promise((resolve) => {
    if (entry.state !== "pending") return resolve(true);
    const timer = setTimeout(() => { entry.waiters = entry.waiters.filter((w) => w !== wake); resolve(false); }, ms);
    const wake = (value) => { clearTimeout(timer); resolve(value); };
    entry.waiters.push(wake);
  });

  const answerRequest = async (c, frame) => {
    const entry = c.requests.get(frame.request_id);
    if (!entry || entry.state !== "pending") return result(c, frame.command_id, "rejected", "unknown_request", false);
    if (!rpc || entry.rpcId === undefined || entry.rpcEpoch !== rpcEpoch) return result(c, frame.command_id, "rejected", "provider_unavailable", true);
    const optionId = frame.payload?.option_id;
    const text = frame.payload?.text;
    const value = optionId ?? text;
    const valid = typeof optionId === "string" ? entry.request.options.some((o) => o.id === optionId)
      : typeof text === "string" && text.trim() !== "" && entry.request.custom_answer;
    if (!valid) return result(c, frame.command_id, "rejected", "invalid_answer", true);
    entry.answering = true;
    entry.sent = true;
    try { rpc.respond(entry.rpcId, { answers: { [entry.qid]: { answers: [value] } } }); }
    catch (error) {
      entry.answering = false;
      log(`answer failed for ${c.id}: ${error.message}`);
      return result(c, frame.command_id, "unknown", "provider_unreachable");
    }
    // There is no reply to an answer; the daemon confirms by resolving the request.
    let confirmed = await waitSettled(entry, answerWaitMs);
    if (!confirmed) {
      try {
        const thread = await readThread(c.id, false);
        if (!statusOf(thread?.status).waiting) { settle(c, entry, "answered"); confirmed = true; }
      } catch { /* Stays unconfirmed. */ }
    }
    entry.answering = false;
    if (confirmed) {
      result(c, frame.command_id, "accepted");
      return publishState(c, entry);
    }
    entry.unconfirmed = frame.command_id;
    return result(c, frame.command_id, "unknown", "not_confirmed");
  };

  // Settles follow-ups whose outcome was not known by looking for the user message the daemon recorded under
  // the client ID we chose. Found means accepted. Not found is final only when the connection the command
  // went out on is gone; on a live one the daemon may just be slow. Nothing is ever sent again.
  const resolveUncertain = async (c, announceResults = true) => {
    const open = [...c.commands].filter(([, r]) => r.outcome === "unknown" && r.clientId);
    if (!open.length) return;
    let thread;
    try { thread = await readThread(c.id, true); } catch { return; }
    for (const [commandId, r] of open) {
      const turn = (thread?.turns ?? []).find((t) => (t.items ?? []).some((i) => i?.type === "userMessage" && i.clientId === r.clientId));
      let next;
      if (turn) next = { outcome: "accepted", receipt: turn.id, still_answerable: true };
      else if (r.epoch !== rpcEpoch) next = { outcome: "rejected", reason: "not_delivered", still_answerable: true };
      else continue;
      const record = { ...r, ...next };
      remember(c.commands, commandId, record, 100);
      if (announceResults) emitResult(commandId, record);
    }
  };

  const followUp = async (c, frame) => {
    // A turn started on a running thread would steer it mid-turn; the design forbids interrupting.
    let thread;
    try { thread = await readThread(c.id, false); }
    catch { return result(c, frame.command_id, "rejected", "provider_unavailable", true); }
    if (statusOf(thread?.status).type !== "idle" || pendingRequests(c).length) {
      setStatus(c, thread?.status);
      return result(c, frame.command_id, "rejected", "not_ready", true);
    }
    const clientId = `apet_${createHash("sha256").update(String(frame.command_id)).digest("hex").slice(0, 40)}`;
    remember(c.commands, frame.command_id, { outcome: "unknown", clientId, epoch: rpcEpoch }, 100);
    let response;
    try {
      response = await call("turn/start", { threadId: c.id, input: [{ type: "text", text: frame.payload?.text ?? "" }], clientUserMessageId: clientId }, requestMs);
    } catch (error) {
      if (error.name === "RpcError" && typeof error.code === "number") {
        c.commands.delete(frame.command_id);
        log(`turn/start refused for ${c.id}: ${error.message}`);
        if (/not found|no rollout/i.test(error.message)) return result(c, frame.command_id, "rejected", "unknown_conversation", false);
        return result(c, frame.command_id, "rejected", "invalid_command", true);
      }
      // Timed out or the connection dropped: the daemon may have started the turn. Look, never resend.
      log(`turn/start outcome unknown for ${c.id}: ${error.message}`);
      remember(c.commands, frame.command_id, { outcome: "unknown", reason: "provider_unreachable", clientId, epoch: rpcEpoch }, 100);
      await resolveUncertain(c, false);
      return emitResult(frame.command_id, c.commands.get(frame.command_id));
    }
    setStatus(c, { type: "active", activeFlags: [] }); // Admitted; the turn starts now.
    return result(c, frame.command_id, "accepted", undefined, true, response?.turn?.id);
  };

  const execute = (frame) => {
    const c = conversations.get(frame.conversation?.session_id);
    if (!c || frame.conversation?.conversation_epoch !== c.epoch || c.ended) {
      return emit({ type: "command_result", command_id: frame.command_id, outcome: "rejected", reason: "unknown_conversation", still_answerable: false });
    }
    return serial(c.id, async () => {
      const prior = c.commands.get(frame.command_id);
      if (prior) { // The same command again: the known state, never a second execution.
        if (prior.outcome === "unknown" && prior.clientId) await resolveUncertain(c);
        return emitResult(frame.command_id, c.commands.get(frame.command_id));
      }
      if (frame.action === "answer_request") return answerRequest(c, frame);
      if (frame.action === "send_follow_up") return followUp(c, frame);
      return result(c, frame.command_id, "rejected", "unsupported_action", false);
    });
  };

  // ---- Link to Agent Pet ---------------------------------------------------------------------------

  const handleFrame = async (frame) => {
    if (frame?.type === "welcome") {
      for (const key of frame.resync ?? []) {
        const c = conversations.get(key.session_id);
        if (c && c.epoch === key.conversation_epoch) await serial(c.id, () => sendSnapshot(c));
      }
    } else if (frame?.type === "resync") {
      const c = conversations.get(frame.conversation?.session_id);
      if (c) await serial(c.id, () => sendSnapshot(c));
    } else if (frame?.type === "command") {
      await execute(frame);
    } else if (frame?.type === "error") {
      // Never log content: the code and message the service chose are all there is.
      log(`service error ${frame.code}: ${frame.message}${frame.request_id ? ` (request ${frame.request_id})` : ""}`);
    }
  };

  const announceAll = () => { for (const c of conversations.values()) announce(c); };

  const loadedIds = async () => {
    const ids = [];
    let cursor = null;
    for (let page = 0; page < 20; page++) {
      const r = await call("thread/loaded/list", cursor ? { cursor } : {});
      ids.push(...(r?.data ?? []));
      cursor = r?.nextCursor;
      if (!cursor) break;
    }
    return ids;
  };

  // What the daemon has loaded is worth tracking: threads running or asking something, then the most
  // recently updated ones. Saved threads that are not loaded are never touched; they appear when the
  // daemon reports activity on them. Run at every (re)connect, so it also rebuilds what a gap lost.
  const discover = async () => {
    const threads = [];
    for (const id of await loadedIds()) {
      try { const t = await readThread(id, false); if (t?.id) threads.push(t); } catch (error) { if (error.code === "closed") throw error; }
    }
    const loaded = new Set(threads.map((t) => t.id));
    for (const c of conversations.values()) {
      if (loaded.has(c.id)) continue;
      await serial(c.id, async () => { setStatus(c, { type: "notLoaded" }); settleAll(c); });
    }
    const wanted = threads.filter((t) => !conversations.has(t.id) && (statusOf(t.status).waiting || (!isSubAgent(t) && statusOf(t.status).type === "active")));
    const rest = threads.filter((t) => !conversations.has(t.id) && !wanted.includes(t) && !isSubAgent(t))
      .sort((a, b) => (b.updatedAt ?? 0) - (a.updatedAt ?? 0)).slice(0, Math.max(0, maxTracked - wanted.length));
    for (const thread of threads) {
      if (!conversations.has(thread.id) && !wanted.includes(thread) && !rest.includes(thread)) continue;
      await serial(thread.id, async () => {
        const c = conversations.get(thread.id) ?? await adopt(thread);
        await applyStatus(c, thread.status);
        await resolveUncertain(c);
      });
    }
    for (const c of conversations.values()) syncReady(c);
    announceAll();
  };

  return {
    conversations,
    discover,
    handleNotification,
    handleServerRequest,
    handleFrame,
    codexUp(connection) { rpc = connection; rpcEpoch++; },
    codexLost() {
      const was = rpc;
      rpc = null;
      if (!was) return;
      for (const c of conversations.values()) {
        // Questions stay pending (the daemon still holds them) but cannot be answered until replayed.
        syncReady(c);
        c.attached = 0;
      }
    },
    hello() {
      linked = true;
      linkGeneration = generation();
      for (const c of conversations.values()) { c.registered = false; c.opening = false; c.lastReady = undefined; }
      return {
        version: 1, type: "hello", provider: PROVIDER, generation: linkGeneration,
        conversations: [...conversations.values()].filter((c) => !c.ended && (!c.sub || c.requests.size)).map(entryOf),
      };
    },
    linkLost() {
      linked = false;
      for (const c of conversations.values()) { c.registered = false; c.opening = false; }
    },
  };
}

// ---- Process: wiring --------------------------------------------------------------------------------

const sleep = (ms, signal) => new Promise((resolve) => { const t = setTimeout(resolve, ms); signal?.addEventListener("abort", () => { clearTimeout(t); resolve(); }, { once: true }); });

// Runs until aborted: one loop keeps the Agent Pet link up, another keeps the Codex daemon connection up.
// The daemon need not exist yet; it is retried quietly (a state change is logged once, not every attempt).
export async function run({ endpoint = defaultEndpoint(), codexSocket = defaultCodexSocket(), log = (m) => console.error(`[agent-pet-bridge] ${m}`),
                            signal, connect = connectCodex, retryMs, bridgeOptions = {} } = {}) {
  let socket = null;
  const bridge = createBridge({
    ...bridgeOptions, log,
    send: (frame) => {
      if (!socket || socket.destroyed) return false;
      const bytes = encodeFrame(frame);
      if (bytes.length - 4 > MAX_FRAME) { log(`dropped an oversized ${frame.type} frame`); return false; }
      return socket.write(bytes) || true;
    },
  });

  const link = async () => {
    for (let delay = retryMs ?? 500; !signal?.aborted; delay = Math.min(delay * 2, retryMs ? retryMs * 4 : 15000)) {
      await new Promise((resolve) => {
        const s = net.connect(endpoint);
        const decoder = new FrameDecoder();
        let opened = false;
        s.on("connect", () => { opened = true; delay = retryMs ?? 250; socket = s; s.write(encodeFrame(bridge.hello())); });
        s.on("data", (chunk) => {
          try { for (const frame of decoder.push(chunk)) bridge.handleFrame(frame).catch((e) => log(e.message)); }
          catch { s.destroy(); }
        });
        s.on("error", () => {});
        s.on("close", () => { if (socket === s) socket = null; bridge.linkLost(); if (opened) log("Agent Pet link closed"); resolve(); });
        signal?.addEventListener("abort", () => s.destroy(), { once: true });
      });
      await sleep(delay, signal);
    }
  };

  const daemon = async () => {
    const base = retryMs ?? 1000;
    let lastProblem = "";
    let delay = base;
    while (!signal?.aborted) {
      let connection;
      try {
        connection = await connect({
          socketPath: codexSocket,
          handlers: {
            notification: (method, params) => bridge.handleNotification(method, params).catch((e) => log(e.message)),
            request: (id, method, params) => bridge.handleServerRequest(id, method, params).catch((e) => log(e.message)),
          },
        });
        bridge.codexUp(connection);
        signal?.addEventListener("abort", () => connection.close(), { once: true });
        if (lastProblem) log("Codex daemon connected");
        lastProblem = "";
        delay = base;
        await bridge.discover();
        await connection.closed;
      } catch (error) {
        const problem = error.code === "ENOENT" ? "Codex daemon is not running" : `Codex daemon: ${error.message}`;
        if (!signal?.aborted && problem !== lastProblem) { log(problem); lastProblem = problem; }
        connection?.close();
        delay = Math.min(delay * 2, retryMs ? retryMs * 4 : 30000);
      }
      bridge.codexLost();
      await sleep(delay, signal);
    }
  };

  await Promise.all([link(), daemon()]);
}

if (process.argv[1] && import.meta.url === new URL(`file://${path.resolve(process.argv[1])}`).href) {
  const arg = (name) => { const i = process.argv.indexOf(name); return i > 0 ? process.argv[i + 1] : undefined; };
  const controller = new AbortController();
  for (const s of ["SIGINT", "SIGTERM"]) process.on(s, () => controller.abort());
  run({
    endpoint: arg("--endpoint") || process.env.AGENT_PET_INTERACTION_ENDPOINT || defaultEndpoint(),
    codexSocket: arg("--codex-socket") || defaultCodexSocket(), signal: controller.signal,
  }).catch((error) => { console.error(`[agent-pet-bridge] ${error.message}`); process.exitCode = 1; });
}
