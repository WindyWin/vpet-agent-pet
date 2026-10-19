// A small stand-in for the Codex app-server daemon with the behavior observed from a live 0.163.0 daemon
// (docs/design/agent-interactions-provider-proof.md): a WebSocket on a unix socket carrying one JSON-RPC
// message per text frame, thread status broadcast to every client, items and questions delivered only to
// the connections subscribed to a thread, a question replayed to a client that resumes the thread,
// turn/start steering a running thread, and clientUserMessageId recorded but never deduplicated.
// It has its own WebSocket codec so the bridge's is tested against an independent one.
import { createHash } from "node:crypto";
import net from "node:net";

const GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// Server frames are unmasked. `fragments` splits the payload into that many frames.
const frame = (opcode, payload, fin = true) => {
  const n = payload.length;
  const head = n < 126 ? Buffer.from([(fin ? 0x80 : 0) | opcode, n])
    : n < 65536 ? Buffer.from([(fin ? 0x80 : 0) | opcode, 126, n >> 8, n & 255])
    : Buffer.concat([Buffer.from([(fin ? 0x80 : 0) | opcode, 127]), (() => { const b = Buffer.alloc(8); b.writeBigUInt64BE(BigInt(n)); return b; })()]);
  return Buffer.concat([head, payload]);
};

class Client {
  constructor(server, socket) {
    this.server = server;
    this.socket = socket;
    this.buffer = Buffer.alloc(0);
    this.upgraded = false;
    this.initialized = false;
    this.initializeSeen = false;
    this.nextId = 0;
    this.requests = new Map(); // our server-request id → {thread, itemId}
  }
  write(object) {
    if (this.socket.destroyed) return;
    const payload = Buffer.from(JSON.stringify(object));
    if (this.server.noisy) {
      this.socket.write(frame(9, Buffer.from("hi"))); // A ping before every message.
      const cut = Math.max(1, Math.floor(payload.length / 3));
      this.socket.write(Buffer.concat([frame(1, payload.subarray(0, cut), false), frame(0, payload.subarray(cut, 2 * cut), false), frame(0, payload.subarray(2 * cut))]));
    } else this.socket.write(frame(1, payload));
  }
  serverRequest(method, params, meta) {
    const id = this.nextId++;
    this.requests.set(id, meta);
    this.write({ id, method, params });
    return id;
  }
  data(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    if (!this.upgraded) {
      const end = this.buffer.indexOf("\r\n\r\n");
      if (end < 0) return;
      const head = this.buffer.subarray(0, end).toString("latin1");
      this.buffer = this.buffer.subarray(end + 4);
      const key = head.match(/sec-websocket-key:\s*(\S+)/i)?.[1];
      if (!key) return this.socket.destroy();
      this.socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${createHash("sha1").update(key + GUID).digest("base64")}\r\n\r\n`);
      this.upgraded = true;
    }
    for (;;) {
      if (this.buffer.length < 2) return;
      const op = this.buffer[0] & 0x0f;
      let len = this.buffer[1] & 0x7f;
      let at = 2;
      if (len === 126) { if (this.buffer.length < 4) return; len = this.buffer.readUInt16BE(2); at = 4; }
      else if (len === 127) { if (this.buffer.length < 10) return; len = Number(this.buffer.readBigUInt64BE(2)); at = 10; }
      if (!(this.buffer[1] & 0x80)) return this.socket.destroy(); // Clients must mask.
      if (this.buffer.length < at + 4 + len) return;
      const key = this.buffer.subarray(at, at + 4);
      const payload = Buffer.from(this.buffer.subarray(at + 4, at + 4 + len).map((b, i) => b ^ key[i & 3]));
      this.buffer = this.buffer.subarray(at + 4 + len);
      if (op === 1) this.server.message(this, JSON.parse(payload.toString("utf8")));
      else if (op === 10) this.server.pongs++;
      else if (op === 9) this.socket.write(frame(10, payload));
      else if (op === 8) { this.socket.write(frame(8, payload.subarray(0, 2))); this.socket.end(); }
    }
  }
}

export class FakeCodex {
  threads = new Map();
  clients = new Set();
  log = [];          // {method, params} of every client request, in order
  notifications = []; // Client notifications (initialized)
  responses = [];     // Every client response to a server request: {threadId, itemId, result}
  answers = [];       // The answers given to questions (result objects)
  pongs = 0;
  steered = 0;        // turn/start calls that landed on a running thread
  noisy = false;
  swallowAnswers = false;
  failTurnStart = null; // "drop" (lose it before applying), "apply-then-drop" or "apply-then-hang"
  #n = 0;
  #clock = 1_700_000_000;

  id(prefix) { return `${prefix}-${String(++this.#n).padStart(4, "0")}`; }

  async listen(socketPath) {
    this.path = socketPath;
    this.server = net.createServer((socket) => {
      const client = new Client(this, socket);
      this.clients.add(client);
      for (const t of this.threads.values()) t.subscribers.delete(client);
      socket.on("data", (chunk) => { try { client.data(chunk); } catch { socket.destroy(); } });
      socket.on("close", () => { this.clients.delete(client); for (const t of this.threads.values()) t.subscribers.delete(client); });
      socket.on("error", () => {});
    });
    await new Promise((resolve) => this.server.listen(socketPath, resolve));
    return this;
  }
  // Every client dropped, as when the daemon restarts; the listener stays.
  dropAll() { for (const c of this.clients) c.socket.destroy(); }
  async close() {
    this.dropAll();
    await new Promise((resolve) => { this.server.close(resolve); });
  }

  // ---- Scripting ----------------------------------------------------------------------------------------

  thread({ cwd = "/home/me/api-web", parentThreadId = null, source = "cli", status = { type: "idle" }, loaded = true } = {}) {
    const t = { id: this.id("thr"), createdAt: (this.#clock += 10), updatedAt: this.#clock, cwd, parentThreadId, source, status, loaded,
                turns: [], subscribers: new Set(), pending: [] };
    this.threads.set(t.id, t);
    return t;
  }
  // A completed turn with one user message and, optionally, the agent's final answer.
  addTurn(t, user, answer, { clientId = null, phase = "final_answer", status = "completed" } = {}) {
    const turn = { id: this.id("turn"), status, startedAt: (this.#clock += 10), completedAt: status === "inProgress" ? null : this.#clock,
                   items: [{ type: "userMessage", id: this.id("item"), clientId, content: [{ type: "text", text: user, text_elements: [] }] }] };
    if (answer !== undefined) turn.items.push({ type: "agentMessage", id: this.id("item"), text: answer, phase, memoryCitation: null, delivery: null, questions: null });
    t.turns.push(turn);
    return turn;
  }
  // The agent says something in the running turn.
  say(t, text, phase = "final_answer") {
    const turn = t.turns.at(-1);
    const item = { type: "agentMessage", id: this.id("item"), text, phase, memoryCitation: null, delivery: null, questions: null };
    turn.items.push(item);
    this.#toSubscribers(t, "item/completed", { threadId: t.id, turnId: turn.id, item, completedAtMs: Date.now() });
    return item;
  }
  finish(t, status = "completed") {
    const turn = t.turns.at(-1);
    turn.status = status;
    turn.completedAt = this.#clock += 1;
    this.setStatus(t, { type: "idle" });
    this.#toSubscribers(t, "turn/completed", { threadId: t.id, turn });
  }
  setStatus(t, status) {
    t.status = status;
    t.updatedAt = this.#clock += 1;
    this.#broadcast("thread/status/changed", { threadId: t.id, status });
  }
  // A question the thread's own client (a terminal, not the bridge) raised: it is pending in the daemon and
  // goes to whoever is subscribed. A client that resumes the thread later gets it replayed.
  ask(t, { question = "Add tests?", options = ["Yes", "No"], isOther = true, isSecret = false, extra = [] } = {}) {
    if (!t.turns.length) this.addTurn(t, "go", undefined, { status: "inProgress" });
    const params = {
      threadId: t.id, turnId: t.turns.at(-1).id, itemId: this.id("call"), isBlocking: false, autoResolutionMs: null,
      questions: [{ id: "q0", header: "Tests", question, isOther, isSecret, options: options.map((label) => ({ label, description: `${label}!` })) }, ...extra],
    };
    t.pending.push(params);
    this.setStatus(t, { type: "active", activeFlags: ["waitingOnUserInput"] });
    for (const c of t.subscribers) c.serverRequest("item/tool/requestUserInput", params, { thread: t, itemId: params.itemId });
    return params;
  }
  // An approval request, which a bridge must never answer.
  approval(t) {
    for (const c of t.subscribers) c.serverRequest("item/commandExecution/requestApproval", { threadId: t.id, turnId: t.turns.at(-1)?.id, itemId: this.id("call"), command: "rm -rf x" }, { thread: t, itemId: "approval" });
  }
  // The terminal answered it (the bridge sees only the notifications).
  answerElsewhere(t, itemId, result) {
    const params = t.pending.find((p) => p.itemId === itemId);
    this.#resolve(t, params, result);
  }
  turnStarts() { return this.log.filter((l) => l.method === "turn/start"); }
  of(method) { return this.log.filter((l) => l.method === method); }

  #toSubscribers(t, method, params) { for (const c of t.subscribers) c.write({ method, params }); }
  #broadcast(method, params) { for (const c of this.clients) if (c.initialized) c.write({ method, params }); }

  #resolve(t, params, result) {
    t.pending = t.pending.filter((p) => p !== params);
    this.answers.push(result);
    for (const c of t.subscribers) {
      const rpc = [...c.requests].find(([, m]) => m.itemId === params.itemId)?.[0];
      if (rpc !== undefined) { c.requests.delete(rpc); c.write({ method: "serverRequest/resolved", params: { requestId: rpc, threadId: t.id } }); }
    }
    this.setStatus(t, { type: "active", activeFlags: [] });
  }

  // ---- Protocol -----------------------------------------------------------------------------------------

  message(client, m) {
    if (typeof m.method === "string" && m.id === undefined) { this.notifications.push(m.method); if (m.method === "initialized" && client.initializeSeen) client.initialized = true; return; }
    if (typeof m.method !== "string") { // A response to a server request.
      const meta = client.requests.get(m.id);
      this.responses.push({ id: m.id, meta: meta?.itemId, result: m.result });
      if (this.swallowAnswers) return; // Received but never acted on.
      if (!meta || meta.itemId === "approval") return;
      const params = meta.thread.pending.find((p) => p.itemId === meta.itemId);
      if (params) this.#resolve(meta.thread, params, m.result);
      return;
    }
    this.log.push({ method: m.method, params: m.params });
    const reply = (result) => client.write({ id: m.id, result });
    const fail = (code, message) => client.write({ id: m.id, error: { code, message } });
    if (m.method === "initialize") {
      if (client.initializeSeen) return fail(-32600, "Already initialized");
      client.initializeSeen = true;
      return reply({ userAgent: "fake/0.163.0", platformFamily: "unix", platformOs: "linux" });
    }
    if (!client.initialized) return fail(-32002, "Not initialized");
    const t = this.threads.get(m.params?.threadId);
    switch (m.method) {
      case "thread/loaded/list": return reply({ data: [...this.threads.values()].filter((x) => x.loaded).map((x) => x.id), nextCursor: null });
      case "thread/read": {
        if (!t) return fail(-32600, `thread not found: ${m.params?.threadId}`);
        if (m.params.includeTurns && !t.turns.length) return fail(-32601, "list_turns is not supported yet");
        return reply({ thread: this.#view(t, m.params.includeTurns) });
      }
      case "thread/resume": {
        if (!t) return fail(-32600, `thread not found: ${m.params?.threadId}`);
        if (!t.turns.length) return fail(-32600, `no rollout found for thread id ${t.id}`);
        t.loaded = true;
        t.subscribers.add(client);
        reply({ thread: this.#view(t, !m.params.excludeTurns) });
        for (const p of t.pending) client.serverRequest("item/tool/requestUserInput", p, { thread: t, itemId: p.itemId });
        return;
      }
      case "turn/start": {
        if (!t) return fail(-32600, `thread not found: ${m.params?.threadId}`);
        if (this.failTurnStart === "drop") { this.failTurnStart = null; return client.socket.destroy(); }
        const text = m.params.input?.[0]?.text ?? "";
        const clientId = m.params.clientUserMessageId ?? null;
        let turn;
        if (t.status.type === "idle") {
          turn = this.addTurn(t, text, undefined, { clientId, status: "inProgress" });
          t.subscribers.add(client);
          this.setStatus(t, { type: "active", activeFlags: [] });
        } else { // A running thread is steered, not refused.
          this.steered++;
          turn = t.turns.at(-1);
          turn.items.push({ type: "userMessage", id: this.id("item"), clientId, content: [{ type: "text", text, text_elements: [] }] });
        }
        if (this.failTurnStart === "apply-then-drop") { this.failTurnStart = null; return client.socket.destroy(); }
        if (this.failTurnStart === "apply-then-hang") { this.failTurnStart = null; return; }
        return reply({ turn: { id: turn.id, items: [], itemsView: "notLoaded", status: "inProgress", error: null } });
      }
      default: return fail(-32601, `unknown method ${m.method}`);
    }
  }

  #view(t, includeTurns) {
    return { id: t.id, createdAt: t.createdAt, updatedAt: t.updatedAt, cwd: t.cwd, status: t.status, source: t.source, parentThreadId: t.parentThreadId,
             name: null, preview: "", ephemeral: false, turns: includeTurns ? structuredClone(t.turns) : [] };
  }
}
