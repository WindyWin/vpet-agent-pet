#!/usr/bin/env node
// Agent Pet interaction bridge for OpenCode 2 (docs/design/agent-interactions-spec.md, starter/docs/interactions.md).
// Unlike the status plugin, this shares message text and answers questions, so it is started only by an
// explicit opt-in. It talks to OpenCode's HTTP API and to Agent Pet's private interaction stream, and
// uses no dependencies beyond Node 20.
//
//   node opencode-bridge.mjs [--endpoint <socket path or pipe>] [--opencode-url <url>]
//
// OpenCode is found through its service registration file (OPENCODE_SERVICE_FILE, or the standard state
// directory); its password is never logged or forwarded.
import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { createHash, randomUUID } from "node:crypto";

export const PROVIDER = "opencode";
export const MAX_FRAME = 64 * 1024;
const SAFE_FRAME = 60 * 1024; // Leave headroom under the frame limit.
const MAX_TEXT_BYTES = 16 * 1024;
const TAIL = 50;         // Messages sent per snapshot.
const PAGE = 200;
const MAX_PAGES = 50;    // 10,000 messages scanned to number a session.
const CAPABILITIES = ["messages", "single_choice", "custom_answer", "follow_up", "reconcile", "idempotent_commands"];

// ---- Framing ----------------------------------------------------------------------------------------

export const encodeFrame = (object) => {
  const payload = Buffer.from(JSON.stringify(object));
  const header = Buffer.alloc(4);
  header.writeUInt32BE(payload.length);
  return Buffer.concat([header, payload]);
};

export class FrameDecoder {
  #buffer = Buffer.alloc(0);
  push(chunk) {
    this.#buffer = Buffer.concat([this.#buffer, chunk]);
    const frames = [];
    for (;;) {
      if (this.#buffer.length < 4) return frames;
      const size = this.#buffer.readUInt32BE(0);
      if (size === 0 || size > MAX_FRAME) throw new Error("invalid frame size");
      if (this.#buffer.length < 4 + size) return frames;
      frames.push(JSON.parse(this.#buffer.subarray(4, 4 + size).toString("utf8")));
      this.#buffer = this.#buffer.subarray(4 + size);
    }
  }
}

// ---- Mapping ----------------------------------------------------------------------------------------

const byteLength = (text) => Buffer.byteLength(text);

// A message is display text: when it will not fit it is shortened with a marker, never sent whole into
// a frame the service would drop. (Questions are different: they are never shortened.)
export const fitText = (text, extra = 400) => {
  let out = text;
  while (byteLength(out) > MAX_TEXT_BYTES || byteLength(JSON.stringify(out)) + extra > SAFE_FRAME) {
    out = out.slice(0, Math.floor(out.length * 0.75)).trimEnd() + "…";
    if (out.length < 8) break;
  }
  return out;
};

// One OpenCode message list item → the visible messages it holds, in order.
export const visibleMessages = (item) => {
  if (!item || typeof item.id !== "string") return [];
  const created = item.time?.created ?? 0;
  if (item.type === "user") {
    const text = typeof item.text === "string" ? item.text : item.payload?.text;
    return typeof text === "string" && text.trim() ? [{ id: item.id, role: "user", text, created }] : [];
  }
  if (item.type === "assistant") {
    const out = [];
    let nth = 0;
    for (const part of item.content ?? []) {
      if (part?.type !== "text") continue;
      if (typeof part.text === "string" && part.text.trim()) out.push({ id: `${item.id}:${nth}`, role: "agent", text: part.text, created });
      nth++;
    }
    return out;
  }
  return [];
};

// A single-field question form with options and/or a custom answer is the one shape the first release
// can answer. Anything else (several questions, multiselect, external, other kinds) is left to OpenCode.
export const requestFromForm = (form) => {
  if (!form || typeof form.id !== "string" || form.metadata?.kind !== "question") return null;
  const fields = form.fields ?? [];
  if (fields.length !== 1) return null;
  const field = fields[0];
  if (field.type !== "string" || typeof field.key !== "string") return null;
  const options = (field.options ?? []).map((o) => ({ id: String(o.value), label: String(o.label ?? o.value) }));
  const custom = field.custom === true || options.length === 0;
  const question = field.description || field.title || form.title;
  if (!question) return null;
  return {
    fieldKey: field.key,
    request: { request_id: form.id, revision: 1, question, options, custom_answer: custom, state: "pending" },
  };
};

const basename = (directory) => (directory ? path.basename(directory.replace(/[\\/]+$/, "")) || directory : "");

// ---- Bridge core ------------------------------------------------------------------------------------
//
// api: { session(id), active(), messages(id, {order, limit, cursor}), forms(id), form(id, sessionID),
//        reply(sessionID, formID, answer) → {status}, prompt(sessionID, body) → {status, body}, sessions(limit) }
// send(frame) → boolean: false when the Agent Pet link is down (state is rebuilt by snapshot on reconnect).
export function createBridge({ api, send, log = () => {}, tail = TAIL, scanPages = MAX_PAGES, generation = () => randomUUID(),
                              now = Date.now, recentSessions = 8, recentMs = 30 * 60 * 1000 }) {
  const startedAt = now();
  const conversations = new Map(); // sessionID → state
  const chains = new Map();
  const ensuring = new Map();
  let linked = false;
  let linkGeneration = generation();

  const serial = (sessionID, work) => {
    const next = (chains.get(sessionID) ?? Promise.resolve()).then(work).catch((error) => log(`session ${sessionID}: ${error.message}`));
    chains.set(sessionID, next);
    return next;
  };

  const keyOf = (c) => ({ provider: PROVIDER, session_id: c.id, conversation_epoch: c.epoch });
  const entryOf = (c) => ({ conversation: keyOf(c), title: c.title, ready: !c.running, capabilities: CAPABILITIES });

  const emit = (frame) => (linked ? send({ version: 1, ...frame }) : false);

  const remember = (map, key, value, limit = 200) => {
    map.delete(key);
    map.set(key, value);
    while (map.size > limit) map.delete(map.keys().next().value);
  };

  // Registers a session on first sight, once even when several callers ask at the same time: it is added to
  // the map only after its history is numbered. Content follows through the service's resync → snapshot.
  const ensure = (sessionID) => {
    const known = conversations.get(sessionID);
    if (known) return Promise.resolve(known);
    if (!ensuring.has(sessionID)) {
      ensuring.set(sessionID, (async () => {
        const body = await api.session(sessionID);
        const info = body?.data ?? body;
        if (!info?.id) return null;
        const c = {
          id: sessionID, epoch: String(info.time?.created ?? "1"), title: basename(info.location?.directory) || info.title || sessionID,
          parentID: info.parentID, running: false, registered: false, ended: false,
          known: new Map(), last: 0, window: [], forms: new Map(), resolved: new Map(), outcomes: new Map(),
        };
        await scan(c);
        conversations.set(sessionID, c);
        return c;
      })().finally(() => ensuring.delete(sessionID)));
    }
    return ensuring.get(sessionID);
  };

  // Numbers every visible message of the session in order, keeping the newest `tail` as the window.
  const scan = async (c) => {
    const all = [];
    let cursor;
    let capped = true;
    for (let page = 0; page < scanPages; page++) {
      const result = await api.messages(c.id, cursor ? { limit: PAGE, cursor } : { limit: PAGE, order: "asc" });
      const items = result?.data ?? [];
      for (const item of items) all.push(...visibleMessages(item));
      cursor = result?.cursor?.next;
      if (items.length < PAGE || !cursor) { capped = false; break; }
    }
    c.known = new Map();
    c.last = 0;
    c.window = [];
    if (capped) {
      // Too long to number from the start: number the newest messages only, which the service shows as a gap.
      const result = await api.messages(c.id, { limit: tail, order: "desc" });
      all.length = 0;
      for (const item of (result?.data ?? []).slice().reverse()) all.push(...visibleMessages(item));
    }
    for (const m of all) absorb(c, m);
  };

  // Gives an unseen message the next sequence number; returns it, or null if already known.
  const absorb = (c, m) => {
    if (c.known.has(m.id)) return null;
    const message = { ...m, seq: ++c.last };
    c.known.set(m.id, message.seq);
    c.window.push(message);
    if (c.window.length > tail) c.window.shift();
    return message;
  };

  const messageFrame = (c, m) => ({
    type: "message", conversation: keyOf(c),
    message: { message_id: m.id, sequence: m.seq, role: m.role, text: fitText(m.text), created_at: m.created,
               ...(m.created < startedAt ? { history: true } : {}) },
  });

  // Publishes whatever the newest page of the session holds that has not been announced.
  const refreshMessages = async (c) => {
    const result = await api.messages(c.id, { limit: 30, order: "desc" });
    const fresh = [];
    for (const item of (result?.data ?? []).slice().reverse())
      for (const m of visibleMessages(item)) {
        const added = absorb(c, m);
        if (added) fresh.push(added);
      }
    if (c.registered) for (const m of fresh) emit(messageFrame(c, m));
  };

  const requestJson = (c, formID) => c.forms.get(formID);

  const adoptForm = (c, form) => {
    const mapped = requestFromForm(form);
    if (!mapped) return false;
    if (!c.forms.has(form.id)) c.forms.set(form.id, { ...mapped, state: "pending" });
    return true;
  };

  const settleForm = (c, formID, state) => {
    const entry = c.forms.get(formID);
    if (!entry || entry.state !== "pending") return;
    entry.state = state;
    entry.request = { ...entry.request, state };
    remember(c.resolved, formID, entry, 20);
    c.forms.delete(formID);
    if (c.registered) emit({ type: "request_state", conversation: keyOf(c), request_id: formID, state });
  };

  // The forms OpenCode still lists decide what is pending; any we held that it no longer lists are settled
  // from their own state.
  const syncForms = async (c, publish) => {
    const listed = (await api.forms(c.id))?.data ?? [];
    const ids = new Set();
    for (const form of listed) {
      ids.add(form.id);
      const isNew = !c.forms.has(form.id);
      if (adoptForm(c, form) && isNew && publish && c.registered) emit({ type: "request", conversation: keyOf(c), request: c.forms.get(form.id).request });
    }
    for (const formID of [...c.forms.keys()]) {
      if (ids.has(formID)) continue;
      let state = "expired";
      try {
        const detail = (await api.form(formID, c.id))?.data;
        if (detail?.state?.status === "answered") state = "answered";
        else if (detail?.state?.status === "cancelled") state = "cancelled";
      } catch { /* Gone: closed without a result. */ }
      settleForm(c, formID, state);
    }
  };

  const snapshotFrame = (c) => {
    const requests = [...c.forms.values(), ...c.resolved.values()].map((e) => e.request);
    const outcomes = [...c.outcomes].map(([command_id, o]) => ({ command_id, ...o }));
    return {
      type: "snapshot", conversation: keyOf(c), watermark: c.last, first_sequence: c.window[0]?.seq ?? 0,
      ready: !c.running, capabilities: CAPABILITIES, requests,
      messages: c.window.map((m) => messageFrame(c, m).message), command_outcomes: outcomes,
    };
  };

  const sendSnapshot = async (c) => {
    // Fresh state first: whatever happened while the link was down is part of the snapshot.
    await refreshMessages(c);
    await syncForms(c, false);
    c.registered = true;
    emit(snapshotFrame(c));
  };

  // ---- Commands ------------------------------------------------------------------------------------

  const answerOf = (value) => value;

  const result = (c, commandId, outcome, reason, stillAnswerable = true, receipt) => {
    const entry = { outcome, ...(reason ? { reason } : {}), ...(stillAnswerable === false ? { still_answerable: false } : {}) };
    if (outcome !== "unknown") remember(c.outcomes, commandId, entry);
    emit({ type: "command_result", command_id: commandId, outcome, ...(reason ? { reason } : {}),
           ...(receipt ? { receipt } : {}), still_answerable: stillAnswerable });
  };

  const answerRequest = async (c, frame) => {
    const entry = c.forms.get(frame.request_id);
    if (!entry) return result(c, frame.command_id, "rejected", "unknown_request", false);
    const value = frame.payload?.option_id ?? frame.payload?.text;
    let response;
    try {
      response = await api.reply(c.id, frame.request_id, { [entry.fieldKey]: answerOf(value) });
    } catch (error) {
      log(`reply failed for ${c.id}: ${error.message}`);
      return result(c, frame.command_id, "unknown", "bridge_unreachable");
    }
    if (response.status >= 200 && response.status < 300) {
      result(c, frame.command_id, "accepted");
      return settleForm(c, frame.request_id, "answered");
    }
    if (response.status === 409) {
      // Already settled: ours if the stored answer is what we sent (a retry), otherwise someone else's.
      let mine = false;
      try {
        const detail = (await api.form(frame.request_id, c.id))?.data;
        mine = detail?.state?.status === "answered" && detail.state.answer?.[entry.fieldKey] === value;
      } catch { /* Fall through to rejected. */ }
      if (mine) result(c, frame.command_id, "accepted");
      else result(c, frame.command_id, "rejected", "already_answered", false);
      return settleForm(c, frame.request_id, "answered");
    }
    if (response.status === 404) { result(c, frame.command_id, "rejected", "unknown_request", false); return settleForm(c, frame.request_id, "cancelled"); }
    if (response.status === 400) return result(c, frame.command_id, "rejected", "invalid_answer", true);
    return result(c, frame.command_id, "unknown", `http_${response.status}`);
  };

  const followUp = async (c, frame) => {
    // A prompt to a running session would steer it mid-turn; the design forbids interrupting.
    let running;
    try { running = Boolean((await api.active())?.data?.[c.id]); } catch { running = c.running; }
    if (running) {
      c.running = true;
      emit({ type: "ready", conversation: keyOf(c), ready: false });
      return result(c, frame.command_id, "rejected", "not_ready", true);
    }
    const id = `msg_apet_${createHash("sha256").update(String(frame.command_id)).digest("hex").slice(0, 40)}`;
    let response;
    try {
      response = await api.prompt(c.id, { id, text: frame.payload?.text ?? "" });
    } catch (error) {
      log(`prompt failed for ${c.id}: ${error.message}`);
      return result(c, frame.command_id, "unknown", "bridge_unreachable");
    }
    if (response.status >= 200 && response.status < 300) {
      c.running = true; // Admitted; the turn starts now.
      return result(c, frame.command_id, "accepted", undefined, true, response.body?.data?.id);
    }
    if (response.status === 404) return result(c, frame.command_id, "rejected", "unknown_conversation", false);
    if (response.status === 409) return result(c, frame.command_id, "rejected", "conflict", true);
    if (response.status === 400) return result(c, frame.command_id, "rejected", "invalid_command", true);
    return result(c, frame.command_id, "unknown", `http_${response.status}`);
  };

  const execute = (frame) => {
    const sessionID = frame.conversation?.session_id;
    const c = conversations.get(sessionID);
    if (!c || frame.conversation?.conversation_epoch !== c.epoch || c.ended) {
      return emit({ type: "command_result", command_id: frame.command_id, outcome: "rejected", reason: "unknown_conversation", still_answerable: false });
    }
    return serial(sessionID, () => (frame.action === "answer_request" ? answerRequest(c, frame)
      : frame.action === "send_follow_up" ? followUp(c, frame)
      : result(c, frame.command_id, "rejected", "unsupported_action", false)));
  };

  // ---- OpenCode events -----------------------------------------------------------------------------

  const handleEvent = async (event) => {
    const type = event?.type;
    const data = event?.data ?? {};
    if (typeof type !== "string") return;
    const sessionID = data.sessionID ?? data.form?.sessionID;
    if (!sessionID) return;
    if (type === "session.deleted") {
      const c = conversations.get(sessionID);
      if (c) await serial(sessionID, async () => { c.ended = true; if (c.registered) emit({ type: "conversation_ended", conversation: keyOf(c) }); conversations.delete(sessionID); });
      return;
    }
    const relevant = type === "form.created" || type === "form.replied" || type === "form.cancelled" || type === "session.text.ended"
      || type === "session.inbox.enqueued" || type.startsWith("session.execution.");
    if (!relevant) return;
    await serial(sessionID, async () => {
      const c = await ensure(sessionID);
      if (!c) return;
      if (c.parentID && type !== "form.created") return; // Sub-agent sessions appear only when they ask something.
      const announce = async () => {
        if (!c.registered && linked && !c.opening) {
          c.opening = true;
          emit({ type: "conversation_open", ...entryOf(c) }); // The service answers with a resync → snapshot.
        }
      };
      if (type === "form.created") {
        if (adoptForm(c, data.form) && c.registered) emit({ type: "request", conversation: keyOf(c), request: c.forms.get(data.form.id).request });
        else await announce();
      } else if (type === "form.replied") settleForm(c, data.id, "answered");
      else if (type === "form.cancelled") settleForm(c, data.id ?? data.formID, "cancelled");
      else if (type === "session.execution.started") {
        c.running = true;
        if (c.registered) emit({ type: "ready", conversation: keyOf(c), ready: false }); else await announce();
      } else if (type.startsWith("session.execution.")) { // succeeded, failed or interrupted
        c.running = false;
        await refreshMessages(c);
        if (c.registered) emit({ type: "ready", conversation: keyOf(c), ready: true }); else await announce();
      } else if (c.registered) await refreshMessages(c);
      else await announce();
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

  // Everything OpenCode shows as running or waiting on a question is worth tracking from the start.
  const discover = async () => {
    const active = (await api.active())?.data ?? {};
    for (const [id, state] of Object.entries(active)) {
      const c = await ensure(id).catch((e) => { log(`discover ${id}: ${e.message}`); return null; });
      if (c) c.running = state?.type === "running" || c.running;
    }
    // Sessions that predate the bridge: any with an open question, and the few touched recently, so a
    // follow-up to a turn that just finished is possible after a restart. Older ones appear on activity.
    const sessions = (await api.sessions(30))?.data ?? [];
    let recent = 0;
    for (const s of sessions) {
      if (conversations.has(s.id) || s.parentID) continue;
      let forms = [];
      try { forms = (await api.forms(s.id))?.data ?? []; } catch { continue; }
      const asking = forms.some((f) => requestFromForm(f));
      const fresh = recent < recentSessions && now() - (s.time?.updated ?? 0) <= recentMs;
      if (!asking && !fresh) continue;
      if (!asking) recent++;
      await serial(s.id, async () => { const c = await ensure(s.id).catch(() => null); if (c) await syncForms(c, false); });
    }
    for (const c of conversations.values()) {
      c.running = Boolean(active[c.id]);
      await serial(c.id, () => syncForms(c, false));
    }
    announceAll();
  };

  // Sessions found while the link was already up are opened now; the ones found before it came up are
  // in its hello.
  const announceAll = () => {
    if (!linked) return;
    for (const c of conversations.values()) {
      if (c.registered || c.opening || c.ended) continue;
      c.opening = true;
      emit({ type: "conversation_open", ...entryOf(c) });
    }
  };

  return {
    conversations,
    discover,
    handleEvent,
    handleFrame,
    // After an OpenCode event-stream gap: events were missed, so re-read everything tracked.
    async reconcile() {
      await discover();
      for (const c of conversations.values()) {
        if (!c.registered) continue;
        await serial(c.id, async () => {
          await refreshMessages(c);
          emit({ type: "ready", conversation: keyOf(c), ready: !c.running });
        });
      }
    },
    hello() {
      linked = true;
      linkGeneration = generation();
      for (const c of conversations.values()) { c.registered = false; c.opening = false; }
      return {
        version: 1, type: "hello", provider: PROVIDER, generation: linkGeneration,
        conversations: [...conversations.values()].filter((c) => !c.ended).map(entryOf),
      };
    },
    linkLost() {
      linked = false;
      for (const c of conversations.values()) { c.registered = false; c.opening = false; }
    },
  };
}

// ---- OpenCode HTTP API ------------------------------------------------------------------------------

export function openCodeApi({ url, headers, fetchImpl = fetch, timeoutMs = 10000 }) {
  const call = async (method, route, body) => {
    const response = await fetchImpl(url + route, {
      method, headers: { ...headers, "content-type": "application/json" },
      body: body === undefined ? undefined : JSON.stringify(body), signal: AbortSignal.timeout(timeoutMs),
    });
    const text = await response.text();
    let json;
    try { json = text ? JSON.parse(text) : undefined; } catch { json = undefined; }
    return { status: response.status, body: json };
  };
  const get = async (route) => {
    const r = await call("GET", route);
    if (r.status < 200 || r.status >= 300) throw new Error(`GET ${route.split("?")[0]} → ${r.status}`);
    return r.body;
  };
  const query = (params) => {
    const q = new URLSearchParams();
    for (const [k, v] of Object.entries(params)) if (v !== undefined) q.set(k, String(v));
    return q.toString();
  };
  return {
    session: (id) => get(`/api/session/${id}`),
    sessions: (limit) => get(`/api/session?${query({ limit })}`),
    active: () => get("/api/session/active"),
    messages: (id, params) => get(`/api/session/${id}/message?${query(params)}`),
    forms: (id) => get(`/api/session/${id}/form`),
    form: (formID, sessionID) => get(`/api/session/${sessionID}/form/${formID}`),
    reply: (sessionID, formID, answer) => call("POST", `/api/session/${sessionID}/form/${formID}/reply`, { answer }),
    prompt: (sessionID, body) => call("POST", `/api/session/${sessionID}/prompt`, body),
    async *events(signal) {
      const response = await fetchImpl(url + "/api/event", { headers, signal });
      if (!response.ok) throw new Error(`event stream → ${response.status}`);
      const decoder = new TextDecoder();
      let buffer = "";
      for await (const chunk of response.body) {
        buffer += decoder.decode(chunk, { stream: true }).replace(/\r\n?/g, "\n");
        if (buffer.length > 1024 * 1024) throw new Error("event too large");
        let end;
        while ((end = buffer.indexOf("\n\n")) >= 0) {
          const raw = buffer.slice(0, end);
          buffer = buffer.slice(end + 2);
          const data = raw.split("\n").filter((l) => l.startsWith("data:")).map((l) => l.slice(5).trim()).join("");
          if (!data) continue;
          try { yield JSON.parse(data); } catch { /* Ignore a malformed event. */ }
        }
      }
    },
  };
}

// ---- Process: wiring --------------------------------------------------------------------------------

export const defaultEndpoint = () => {
  if (process.platform === "win32") return `\\\\.\\pipe\\agent-pet-${process.env.USERNAME || "user"}-interactions`;
  const root = process.env.XDG_RUNTIME_DIR || (process.platform === "darwin" ? os.tmpdir() : "/tmp");
  return path.join(root, `agent-pet-${process.getuid()}`, "interactions.sock");
};

// OpenCode 2 keeps two files called service.json. The one in the state directory is the running service's
// registration ({ url, ... }); the one in the config directory is `opencode service set …`: { hostname, port,
// password, … }, where the URL is implied and defaults to 127.0.0.1:49374. Older builds put url and password in
// the state file. Each file contributes what it has; the password may also come from the environment.
const DEFAULT_SERVICE_PORT = 49374;

const readJson = (file) => {
  try { return JSON.parse(fs.readFileSync(file, "utf8")); } catch { return undefined; }
};

export const readService = ({ env = process.env, home = os.homedir() } = {}) => {
  const state = path.join(env.XDG_STATE_HOME || path.join(home, ".local", "state"), "opencode", "service.json");
  const config = path.join(env.XDG_CONFIG_HOME || path.join(home, ".config"), "opencode", "service.json");
  const files = (env.OPENCODE_SERVICE_FILE ? [env.OPENCODE_SERVICE_FILE] : [state, config]).map(readJson).filter((f) => f && typeof f === "object");
  const password = env.OPENCODE_SERVER_PASSWORD || env.OPENCODE_PASSWORD || files.find((f) => typeof f.password === "string" && f.password)?.password;
  if (!password) throw new Error("OpenCode's service password was not found; set OPENCODE_SERVER_PASSWORD or run `opencode service set password …`");
  let url = files.find((f) => typeof f.url === "string" && f.url)?.url;
  if (!url) {
    const configured = files.find((f) => f.hostname || f.port);
    const host = configured?.hostname && configured.hostname !== "0.0.0.0" && configured.hostname !== "::" ? configured.hostname : "127.0.0.1";
    url = `http://${host.includes(":") && !host.startsWith("[") ? `[${host}]` : host}:${configured?.port || DEFAULT_SERVICE_PORT}`;
  }
  return { url: url.replace(/\/+$/, ""), headers: { authorization: "Basic " + Buffer.from(`opencode:${password}`).toString("base64") } };
};

const sleep = (ms, signal) => new Promise((resolve) => { const t = setTimeout(resolve, ms); signal?.addEventListener("abort", () => { clearTimeout(t); resolve(); }, { once: true }); });

// Runs until aborted: one loop keeps the Agent Pet link up, another keeps the OpenCode event stream up.
export async function run({ endpoint = defaultEndpoint(), service = readService(), log = (m) => console.error(`[agent-pet-bridge] ${m}`), signal, api = openCodeApi(service), retryMs, bridgeOptions = {} } = {}) {
  let socket = null;
  const bridge = createBridge({
    ...bridgeOptions, api, log,
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

  const events = async () => {
    let first = true;
    for (let delay = retryMs ?? 1000; !signal?.aborted; delay = Math.min(delay * 2, retryMs ? retryMs * 4 : 30000)) {
      try {
        // Events during a gap are lost, so every (re)connect starts by re-reading what is tracked.
        if (first) await bridge.discover(); else await bridge.reconcile();
        first = false;
        for await (const event of api.events(signal)) { delay = retryMs ?? 1000; bridge.handleEvent(event).catch((e) => log(e.message)); }
      } catch (error) { if (!signal?.aborted) log(`OpenCode event stream: ${error.message}`); }
      await sleep(delay, signal);
    }
  };

  await Promise.all([link(), events()]);
}

if (process.argv[1] && import.meta.url === new URL(`file://${path.resolve(process.argv[1])}`).href) {
  const arg = (name) => { const i = process.argv.indexOf(name); return i > 0 ? process.argv[i + 1] : undefined; };
  const controller = new AbortController();
  for (const s of ["SIGINT", "SIGTERM"]) process.on(s, () => controller.abort());
  try {
    const service = readService();
    if (arg("--opencode-url")) service.url = arg("--opencode-url");
    run({ endpoint: arg("--endpoint") || process.env.AGENT_PET_INTERACTION_ENDPOINT || defaultEndpoint(), service, signal: controller.signal })
      .catch((error) => { console.error(`[agent-pet-bridge] ${error.message}`); process.exitCode = 1; });
  } catch (error) {
    console.error(`[agent-pet-bridge] ${error.message}`);
    process.exitCode = 1;
  }
}
