// The OpenCode interaction bridge against a fake OpenCode API and a fake Agent Pet service (a framed
// socket that records frames and sends commands). The real C++ service is exercised separately by
// interaction_bridge_tests.cpp. Run: node --test tests/opencode_bridge_test.mjs
import assert from "node:assert/strict";
import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { after, afterEach, beforeEach, describe, test } from "node:test";
import { FakeOpenCode } from "./fake_opencode.mjs";
import { FrameDecoder, encodeFrame, fitSnapshot, fitText, openCodeApi, requestFromForm, run, visibleMessages } from "../src/providers/opencode-bridge.mjs";
import { installTui, uninstallTui } from "../src/providers/opencode-presence.mjs";

const until = async (predicate, what, ms = 3000) => {
  const end = Date.now() + ms;
  for (;;) {
    const value = predicate();
    if (value) return value;
    if (Date.now() > end) throw new Error(`timed out waiting for ${what}`);
    await new Promise((r) => setTimeout(r, 10));
  }
};

// The Agent Pet side: accepts bridge connections, answers hello with a welcome that asks for snapshots.
class FakePet {
  frames = [];
  connections = 0;
  #sockets = new Set();
  autoResync = true;
  async listen(socketPath) {
    this.path = socketPath;
    this.server = net.createServer((socket) => {
      this.connections++;
      this.#sockets.add(socket);
      const decoder = new FrameDecoder();
      socket.on("data", (chunk) => {
        for (const frame of decoder.push(chunk)) {
          this.frames.push(frame);
          if (frame.type === "hello") this.send({ version: 1, type: "welcome", resync: this.autoResync ? frame.conversations.map((c) => c.conversation) : [] }, socket);
          if (frame.type === "conversation_open") this.send({ version: 1, type: "resync", conversation: frame.conversation }, socket);
        }
      });
      socket.on("close", () => this.#sockets.delete(socket));
      socket.on("error", () => {});
    });
    await new Promise((resolve) => this.server.listen(socketPath, resolve));
    return this;
  }
  send(frame, socket = [...this.#sockets].at(-1)) { socket.write(encodeFrame({ version: 1, ...frame })); }
  drop() { for (const s of this.#sockets) s.destroy(); }
  of(type) { return this.frames.filter((f) => f.type === type); }
  last(type) { return this.of(type).at(-1); }
  async close() { this.drop(); await new Promise((r) => { this.server.close(r); }); }
}

let dir, oc, pet, controller, running, logs, presencePath;
const start = async () => {
  controller = new AbortController();
  running = run({
    endpoint: pet.path, signal: controller.signal, retryMs: 40, log: (m) => logs.push(m),
    service: { url: oc.url, headers: oc.headers() },
    bridgeOptions: { now: () => oc.now(), recentMs: 60_000, goneMs: 80 }, // The fake's clock is its own.
    presence: presencePath,
  });
};

beforeEach(async () => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "bridge-"));
  oc = await new FakeOpenCode().listen();
  const socketPath = process.platform === "win32" ? `\\\\.\\pipe\\agent-pet-bridge-test-${path.basename(dir)}` : path.join(dir, "i.sock");
  pet = await new FakePet().listen(socketPath);
  logs = [];
  presencePath = process.platform === "win32" ? `\\\\.\\pipe\\agent-pet-presence-test-${path.basename(dir)}` : path.join(dir, "p.sock");
});
afterEach(async () => {
  for (const socket of terminals.splice(0)) socket.destroy();
  controller?.abort();
  await running?.catch(() => {});
  await pet.close();
  await oc.close();
  fs.rmSync(dir, { recursive: true, force: true });
  controller = running = undefined;
});

const command = (id, key, body) => ({ type: "command", command_id: id, conversation: key, source: "desktop", ...body });
const answer = (id, key, request, option, text) => command(id, key, {
  action: "answer_request", request_id: request, request_revision: 1, payload: text === undefined ? { option_id: option } : { text },
});
const followUp = (id, key, text) => command(id, key, { action: "send_follow_up", payload: { text } });
const keyOf = (s) => ({ provider: "opencode", session_id: s.id, conversation_epoch: String(s.created) });

describe("mapping", () => {
  test("messages: user text, assistant text parts, nothing else", () => {
    assert.deepEqual(visibleMessages({ id: "msg_1", type: "user", text: "hi", time: { created: 5 } }), [{ id: "msg_1", role: "user", text: "hi", created: 5 }]);
    const parts = visibleMessages({ id: "msg_2", type: "assistant", content: [{ type: "reasoning", text: "…" }, { type: "text", text: "a" }, { type: "tool" }, { type: "text", text: "  " }, { type: "text", text: "b" }] });
    assert.deepEqual(parts.map((m) => [m.id, m.text]), [["msg_2:0", "a"], ["msg_2:2", "b"]]);
    assert.deepEqual(visibleMessages({ id: "msg_3", type: "idle" }), []);
    assert.deepEqual(visibleMessages({ id: "msg_4", type: "user", text: "  " }), []);
    assert.deepEqual(visibleMessages(null), []);
  });

  test("forms: only a single-field question with options or a custom answer", () => {
    const base = { id: "frm_1", metadata: { kind: "question" }, title: "T" };
    const field = { key: "q0", type: "string", description: "Which?", options: [{ value: "a", label: "A" }], custom: true };
    const ok = requestFromForm({ ...base, fields: [field] });
    assert.deepEqual(ok.request, { request_id: "frm_1", revision: 1, question: "Which?", options: [{ id: "a", label: "A" }], custom_answer: true, state: "pending" });
    assert.equal(ok.fieldKey, "q0");
    assert.equal(requestFromForm({ ...base, fields: [{ ...field, custom: false }] }).request.custom_answer, false);
    assert.equal(requestFromForm({ ...base, fields: [{ key: "q0", type: "string", description: "Name?" }] }).request.custom_answer, true); // Free text.
    assert.equal(requestFromForm({ ...base, fields: [field, field] }), null);
    assert.equal(requestFromForm({ ...base, fields: [{ ...field, type: "multiselect" }] }), null);
    assert.equal(requestFromForm({ ...base, metadata: { kind: "other" }, fields: [field] }), null);
    assert.equal(requestFromForm({ ...base, fields: [{ ...field, description: undefined, title: undefined }], title: undefined }), null);
  });

  test("long message text is shortened to fit a frame, and short text is untouched", () => {
    assert.equal(fitText("short"), "short");
    const long = "é".repeat(30000);
    const fitted = fitText(long);
    assert.ok(Buffer.byteLength(fitted) <= 16 * 1024);
    assert.ok(fitted.endsWith("…"));
    const escapes = '"'.repeat(16000); // Small as text, large once JSON-escaped.
    assert.ok(Buffer.byteLength(JSON.stringify(fitText(escapes))) < 60 * 1024);
  });

  test("a snapshot of many long messages drops the oldest to fit one frame, and says where it starts", () => {
    const messages = Array.from({ length: 50 }, (_, i) => ({ message_id: `m${i}`, sequence: i + 1, role: "agent", text: fitText("x".repeat(20000)), created_at: 1 }));
    const pending = { request_id: "q", revision: 1, question: "Pick", options: [], custom_answer: true, state: "pending" };
    const answered = { ...pending, request_id: "old", state: "answered" };
    const frame = fitSnapshot({ type: "snapshot", watermark: 50, first_sequence: 1, requests: [answered, pending], messages });
    assert.ok(Buffer.byteLength(JSON.stringify({ version: 1, ...frame })) <= 60 * 1024);
    assert.ok(frame.messages.length > 0 && frame.messages.length < 50);
    assert.equal(frame.messages.at(-1).sequence, 50); // The newest are kept.
    assert.equal(frame.first_sequence, frame.messages[0].sequence);
    assert.deepEqual(frame.requests.map((r) => r.request_id), ["old", "q"]); // Messages go before requests do.
    const small = { type: "snapshot", watermark: 1, first_sequence: 1, requests: [], messages: messages.slice(0, 1) };
    assert.deepEqual(fitSnapshot(structuredClone(small)), small);
    // Past the messages, settled requests go before pending ones, and the frame always fits.
    const big = (id, state) => ({ ...pending, request_id: id, state, question: "q".repeat(18000) });
    const crowded = fitSnapshot({ type: "snapshot", watermark: 0, first_sequence: 0, messages: [],
      requests: [big("p1", "pending"), big("s1", "answered"), big("p2", "pending"), big("p3", "pending"), big("p4", "pending")] });
    assert.ok(Buffer.byteLength(JSON.stringify({ version: 1, ...crowded })) <= 60 * 1024);
    assert.deepEqual(crowded.requests.map((r) => r.request_id), ["p2", "p3", "p4"]);
  });

  test("a question too large for the pet stays with OpenCode", () => {
    const form = (description, options = []) => ({ id: "f", metadata: { kind: "question" }, fields: [{ type: "string", key: "k", description, options }] });
    assert.ok(requestFromForm(form("Pick")));
    assert.equal(requestFromForm(form("q".repeat(17 * 1024))), null);
    assert.equal(requestFromForm(form("Pick", Array.from({ length: 21 }, (_, i) => ({ value: `v${i}` })))), null);
    assert.equal(requestFromForm(form("Pick", [{ value: "v", label: "l".repeat(257) }])), null);
  });

  test("framing round-trips and rejects hostile sizes", () => {
    const decoder = new FrameDecoder();
    const bytes = Buffer.concat([encodeFrame({ a: 1 }), encodeFrame({ b: "é" })]);
    const frames = [];
    for (const byte of bytes) frames.push(...decoder.push(Buffer.from([byte])));
    assert.deepEqual(frames, [{ a: 1 }, { b: "é" }]);
    assert.throws(() => new FrameDecoder().push(Buffer.from([0, 1, 0, 1])));
    assert.throws(() => new FrameDecoder().push(Buffer.from([0, 0, 0, 0])));
  });
});

describe("registration and snapshots", () => {
  test("hello lists tracked sessions; the snapshot numbers messages and carries the open question", async () => {
    const s = oc.session("/home/me/api-web");
    oc.addUser(s, "fix the handler");
    oc.addAssistant(s, "I updated it.", [{ type: "reasoning", text: "thinking" }]);
    oc.addIdle(s);
    oc.addAssistant(s, "Tests next?");
    const form = oc.ask(s);
    await start();
    const hello = await until(() => pet.last("hello"), "hello");
    assert.equal(hello.provider, "opencode");
    assert.ok(hello.generation);
    // Discovery races the connection: the session is in the hello or opened right after it.
    const entry = await until(() => [...hello.conversations, ...pet.of("conversation_open")].find((c) => c.conversation.session_id === s.id), "entry");
    assert.deepEqual(entry.conversation, keyOf(s));
    assert.equal(entry.title, "api-web");
    assert.equal(entry.ready, false); // The question holds the turn.
    assert.deepEqual(entry.capabilities, ["messages", "single_choice", "custom_answer", "follow_up", "reconcile", "idempotent_commands"]);
    const snapshot = await until(() => pet.last("snapshot"), "snapshot");
    assert.deepEqual(snapshot.messages.map((m) => [m.sequence, m.role, m.text]), [[1, "user", "fix the handler"], [2, "agent", "I updated it."], [3, "agent", "Tests next?"]]);
    assert.equal(snapshot.watermark, 3);
    assert.equal(snapshot.first_sequence, 1);
    assert.equal(snapshot.requests.length, 1);
    assert.equal(snapshot.requests[0].request_id, form.id);
    assert.deepEqual(snapshot.requests[0].options, [{ id: "Yes", label: "Yes" }, { id: "No", label: "No" }]);
  });

  test("unsupported forms are left to OpenCode", async () => {
    const s = oc.session();
    oc.ask(s, { extraFields: [{ key: "q1", type: "string", description: "Second?" }] });
    await start();
    await until(() => pet.last("snapshot") || pet.last("hello")?.conversations.length, "hello");
    await new Promise((r) => setTimeout(r, 100));
    assert.equal(pet.last("snapshot")?.requests?.length ?? 0, 0);
  });

  test("only the newest window is sent, numbered from the true start", async () => {
    const s = oc.session();
    for (let i = 1; i <= 130; i++) oc.addUser(s, `m${i}`);
    await start();
    const snapshot = await until(() => pet.last("snapshot"), "snapshot");
    assert.equal(snapshot.messages.length, 50);
    assert.equal(snapshot.first_sequence, 81);
    assert.equal(snapshot.watermark, 130);
    assert.equal(snapshot.messages.at(-1).text, "m130");
  });

  test("a session that appears after startup is opened and snapshotted on demand", async () => {
    await start();
    await until(() => pet.last("hello"), "hello");
    const s = oc.session();
    oc.addUser(s, "hello there");
    oc.start(s);
    const open = await until(() => pet.last("conversation_open"), "conversation_open");
    assert.deepEqual(open.conversation, keyOf(s));
    const snapshot = await until(() => pet.last("snapshot"), "snapshot");
    assert.equal(snapshot.messages[0].text, "hello there");
    // The start can be handled before or after the snapshot is sent; either way the pet ends up told it is busy.
    await until(() => pet.frames.filter((f) => f.type === "snapshot" || f.type === "ready").at(-1)?.ready === false, "not ready");
  });
});

describe("live events", () => {
  test("agent text becomes a numbered message, and the turn ending makes the conversation ready", async () => {
    const s = oc.session();
    oc.addUser(s, "go");
    oc.start(s);
    await start();
    await until(() => pet.last("snapshot"), "snapshot");
    oc.say(s, "All done.");
    oc.finish(s);
    const message = await until(() => pet.last("message"), "message");
    assert.equal(message.message.sequence, 2);
    assert.equal(message.message.role, "agent");
    assert.equal(message.message.text, "All done.");
    assert.equal(message.message.message_id, `${s.items[1].id}:0`);
    await until(() => pet.of("ready").some((r) => r.ready === true), "ready");
    assert.equal(pet.frames.filter((f) => f.type === "message").length, 1); // Not repeated by later events.
  });

  test("a new question is published, and answering it elsewhere closes it", async () => {
    const s = oc.session();
    oc.addUser(s, "go");
    await start();
    await until(() => pet.last("snapshot"), "snapshot");
    const form = oc.ask(s, { question: "Pick one", options: ["x", "y"], custom: false });
    const request = await until(() => pet.last("request"), "request");
    assert.equal(request.request.request_id, form.id);
    assert.equal(request.request.custom_answer, false);
    oc.answerElsewhere(s, form.id, { q0: "x" });
    const state = await until(() => pet.last("request_state"), "request_state");
    assert.deepEqual([state.request_id, state.state], [form.id, "answered"]);
    const other = oc.ask(s);
    await until(() => pet.of("request").length === 2, "second request");
    oc.cancel(s, other.id);
    await until(() => pet.of("request_state").some((r) => r.state === "cancelled"), "cancelled");
  });

  test("a deleted session ends its conversation", async () => {
    const s = oc.session();
    oc.addUser(s, "x");
    await start();
    await until(() => pet.last("snapshot"), "snapshot");
    oc.remove(s);
    const ended = await until(() => pet.last("conversation_ended"), "ended");
    assert.deepEqual(ended.conversation, keyOf(s));
  });

  test("a session deleted while the event stream was down ends its conversation on reconcile", async () => {
    const s = oc.session();
    oc.addUser(s, "x");
    await start();
    await until(() => pet.last("snapshot"), "snapshot");
    oc.sessions.delete(s.id); // No event: the stream missed it.
    oc.dropStreams();
    const ended = await until(() => pet.last("conversation_ended"), "ended");
    assert.deepEqual(ended.conversation, keyOf(s));
  });

  test("sub-agent sessions show up only when they ask a question", async () => {
    const parent = oc.session();
    const child = oc.session("/work/api-web", parent.id);
    await start();
    await until(() => pet.last("hello"), "hello");
    oc.start(child);
    oc.say(child, "working");
    await new Promise((r) => setTimeout(r, 100));
    const opened = () => pet.of("conversation_open").some((f) => f.conversation.session_id === child.id);
    assert.equal(opened(), false);
    oc.ask(child);
    await until(opened, "conversation_open");
  });

  test("once a sub-agent is shown, its later events keep it current", async () => {
    const parent = oc.session();
    const child = oc.session("/work/api-web", parent.id);
    await start();
    await until(() => pet.last("hello") && oc.streams.size > 0, "hello and the event stream"); // Events sent earlier are lost.
    oc.start(child);
    const form = oc.ask(child);
    const snapshot = await until(() => pet.of("snapshot").find((f) => f.conversation.session_id === child.id), "snapshot");
    const request = snapshot.requests[0];
    assert.equal(request.state, "pending");
    // Answered in the OpenCode terminal: the pet's card must close, not wait for a reconcile.
    oc.answerElsewhere(child, form.id, "Yes");
    const closed = await until(() => pet.of("request_state").find((f) => f.conversation.session_id === child.id && f.request_id === request.request_id), "request_state");
    assert.equal(closed.state, "answered");
    // And its turn finishing makes it ready again.
    oc.finish(child);
    await until(() => pet.of("ready").find((f) => f.conversation.session_id === child.id && f.ready === true), "ready");
  });
});

describe("commands", () => {
  const setup = async () => {
    const s = oc.session();
    oc.addUser(s, "go");
    const form = oc.ask(s);
    await start();
    await until(() => pet.last("snapshot"), "snapshot");
    return { s, form, key: keyOf(s) };
  };

  test("an answer reaches the form and is accepted, then the request closes", async () => {
    const { s, form, key } = await setup();
    pet.send(answer("c1", key, form.id, "Yes"));
    const result = await until(() => pet.last("command_result"), "result");
    assert.equal(result.command_id, "c1");
    assert.equal(result.outcome, "accepted");
    assert.deepEqual(oc.replies, [{ sessionID: s.id, formID: form.id, answer: { q0: "Yes" } }]);
    await until(() => pet.last("request_state"), "request_state");
    assert.equal(pet.last("request_state").state, "answered");
    assert.ok(pet.frames.indexOf(pet.last("command_result")) < pet.frames.indexOf(pet.last("request_state"))); // Result first.
  });

  test("a custom answer is sent as the typed text", async () => {
    const { form, key } = await setup();
    pet.send(answer("c1", key, form.id, undefined, "something else"));
    await until(() => pet.last("command_result"), "result");
    assert.equal(oc.replies[0].answer.q0, "something else");
  });

  test("a form answered in the meantime is rejected, unless the stored answer is ours", async () => {
    const { s, form, key } = await setup();
    oc.answerElsewhere(s, form.id, { q0: "No" });
    await until(() => pet.last("request_state"), "request_state");
    pet.send(answer("c1", key, form.id, "Yes"));
    const rejected = await until(() => pet.of("command_result").find((r) => r.command_id === "c1"), "rejected");
    assert.equal(rejected.outcome, "rejected");
    assert.equal(rejected.reason, "unknown_request"); // Already closed locally by the replied event.
    assert.equal(rejected.still_answerable, false);
    assert.equal(oc.replies.length, 0);
  });

  test("a stale 409 is told apart from a retry of our own answer", async () => {
    const { s, form, key } = await setup();
    // Settle on the server without the event reaching the bridge, so only the 409 reveals it.
    s.forms.get(form.id).state = { status: "answered", answer: { q0: "Yes" } };
    pet.send(answer("c1", key, form.id, "Yes"));
    assert.equal((await until(() => pet.of("command_result").find((r) => r.command_id === "c1"), "retry")).outcome, "accepted");
    const second = oc.ask(s);
    await until(() => pet.of("request").length >= 1, "request");
    s.forms.get(second.id).state = { status: "answered", answer: { q0: "No" } };
    pet.send(answer("c2", key, second.id, "Yes"));
    const lost = await until(() => pet.of("command_result").find((r) => r.command_id === "c2"), "lost race");
    assert.deepEqual([lost.outcome, lost.reason, lost.still_answerable], ["rejected", "already_answered", false]);
  });

  test("a dropped connection is unknown, never success", async () => {
    const { form, key } = await setup();
    oc.failNext = "reply";
    pet.send(answer("c1", key, form.id, "Yes"));
    const result = await until(() => pet.last("command_result"), "result");
    assert.equal(result.outcome, "unknown");
    assert.ok(logs.some((l) => l.includes("reply failed")));
    assert.equal(oc.replies.length, 0);
  });

  test("an unknown conversation or request is refused", async () => {
    const { key } = await setup();
    pet.send(answer("c1", { ...key, conversation_epoch: "stale" }, "frm_x", "Yes"));
    assert.equal((await until(() => pet.last("command_result"), "result")).reason, "unknown_conversation");
    pet.send(answer("c2", key, "frm_missing", "Yes"));
    await until(() => pet.of("command_result").length === 2, "second");
    assert.equal(pet.last("command_result").reason, "unknown_request");
  });

  test("a follow-up becomes a prompt with a derived ID and is accepted", async () => {
    const s = oc.session();
    oc.addUser(s, "go");
    await start();
    await until(() => pet.last("snapshot"), "snapshot");
    pet.send(followUp("cmd-1", keyOf(s), "now add tests"));
    const result = await until(() => pet.last("command_result"), "result");
    assert.equal(result.outcome, "accepted");
    assert.equal(oc.prompts.length, 1);
    assert.equal(oc.prompts[0].text, "now add tests");
    assert.match(oc.prompts[0].id, /^msg_apet_[0-9a-f]{40}$/);
    assert.equal(result.receipt, oc.prompts[0].id);
    pet.send(followUp("cmd-1", keyOf(s), "now add tests")); // The same command again.
    await until(() => pet.of("command_result").length === 2, "second result");
    assert.equal(s.items.filter((i) => i.type === "user").length, 2); // No second message was created.
  });

  test("a follow-up is never sent into a running turn", async () => {
    const s = oc.session();
    oc.addUser(s, "go");
    oc.start(s);
    await start();
    await until(() => pet.last("snapshot"), "snapshot");
    pet.send(followUp("cmd-1", keyOf(s), "interrupt!"));
    const result = await until(() => pet.last("command_result"), "result");
    assert.deepEqual([result.outcome, result.reason], ["rejected", "not_ready"]);
    assert.equal(oc.prompts.length, 0);
    assert.equal(pet.last("ready").ready, false);
  });

  test("a follow-up whose request is lost is unknown", async () => {
    const s = oc.session();
    await start();
    await until(() => pet.last("hello"), "hello");
    oc.addUser(s, "x");
    oc.start(s);
    await until(() => pet.last("snapshot"), "snapshot");
    oc.finish(s);
    await until(() => pet.of("ready").some((r) => r.ready), "ready");
    oc.failNext = "prompt";
    pet.send(followUp("cmd-1", keyOf(s), "again"));
    assert.equal((await until(() => pet.last("command_result"), "result")).outcome, "unknown");
  });
});

describe("reconnecting", () => {
  test("after the link drops, a new hello and snapshot carry what happened meanwhile", async () => {
    const s = oc.session();
    oc.addUser(s, "go");
    const form = oc.ask(s);
    await start();
    await until(() => pet.last("snapshot"), "snapshot");
    const key = keyOf(s);
    pet.autoResync = false;
    pet.drop();
    await until(() => pet.connections >= 2 && pet.of("hello").length >= 2, "second hello");
    // While the service is disconnected-but-reconnecting: a command we executed earlier, an answer in the
    // terminal, and a new message. The second connection asks for snapshots only when we tell it to.
    pet.autoResync = true;
    pet.send({ type: "command", command_id: "late", conversation: key, source: "desktop", action: "answer_request", request_id: form.id, request_revision: 1, payload: { option_id: "No" } });
    await until(() => pet.of("command_result").some((r) => r.command_id === "late"), "late result");
    oc.say(s, "continuing");
    pet.drop();
    await until(() => pet.of("hello").length >= 3, "third hello");
    const snapshot = await until(() => pet.of("snapshot").filter((x) => x.command_outcomes.length).at(-1), "snapshot with outcomes");
    assert.deepEqual(snapshot.command_outcomes, [{ command_id: "late", outcome: "accepted" }]);
    assert.ok(snapshot.messages.some((m) => m.text === "continuing"));
    const closed = snapshot.requests.find((r) => r.request_id === form.id);
    assert.equal(closed.state, "answered");
    assert.notEqual(pet.last("hello").generation, pet.of("hello")[0].generation); // A new generation per connection.
  });

  test("a lost OpenCode event stream is recovered by re-reading state", async () => {
    const s = oc.session();
    oc.addUser(s, "go");
    await start();
    await until(() => pet.last("snapshot"), "snapshot");
    oc.dropStreams();
    oc.addAssistant(s, "said while the stream was down"); // Its event was lost.
    const message = await until(() => pet.of("message").find((m) => m.message.text.includes("while the stream")), "recovered message", 5000);
    assert.equal(message.message.sequence, 2);
  });
});

describe("OpenCode API client", () => {
  test("authenticates, pages with a cursor and no order, and reports status codes", async () => {
    const s = oc.session();
    for (let i = 0; i < 5; i++) oc.addUser(s, `m${i}`);
    const api = openCodeApi({ url: oc.url, headers: oc.headers() });
    const first = await api.messages(s.id, { limit: 2, order: "asc" });
    assert.equal(first.data.length, 2);
    const second = await api.messages(s.id, { limit: 2, cursor: first.cursor.next });
    assert.deepEqual(second.data.map((m) => m.text), ["m2", "m3"]);
    await assert.rejects(() => openCodeApi({ url: oc.url, headers: {} }).active(), /401/);
    assert.equal((await api.reply(s.id, "frm_none", { q0: "x" })).status, 404);
  });
});

after(() => {});

// A terminal as the plugin behaves: one socket per terminal, one JSON line per change.
const terminals = [];
const terminal = async () => {
  await until(() => fs.existsSync(presencePath) || process.platform === "win32", "presence socket");
  const socket = net.connect(presencePath);
  await new Promise((resolve, reject) => { socket.once("connect", resolve); socket.once("error", reject); });
  terminals.push(socket);
  return { show: (session) => socket.write(JSON.stringify({ type: "view", session }) + "\n"), close: () => socket.destroy() };
};
const epochOf = (frame) => frame.conversation.conversation_epoch;

describe("terminals", () => {
  test("a session a terminal shows is tracked, and ends when the terminal closes", async () => {
    const s = oc.session();
    oc.addUser(s, "hello");
    s.created = 1; // Long quiet: only the terminal makes it worth tracking.
    await start();
    const t = await terminal();
    t.show(s.id);
    const opened = await until(() => pet.last("conversation_open"), "open");
    assert.equal(opened.conversation.session_id, s.id);
    t.close();
    const ended = await until(() => pet.last("conversation_ended"), "ended");
    assert.deepEqual(ended.conversation, opened.conversation);
  });

  test("another terminal showing it keeps it; changing session lets the old one go", async () => {
    const a = oc.session(); const b = oc.session();
    oc.addUser(a, "a"); oc.addUser(b, "b");
    await start();
    const t1 = await terminal(); const t2 = await terminal();
    t1.show(a.id); t2.show(a.id);
    await until(() => pet.of("conversation_open").some((f) => f.conversation.session_id === a.id), "open a");
    t1.close();
    await new Promise((r) => setTimeout(r, 250));
    assert.equal(pet.of("conversation_ended").length, 0); // t2 still shows it.
    t2.show(b.id);
    const ended = await until(() => pet.last("conversation_ended"), "ended a");
    assert.equal(ended.conversation.session_id, a.id);
    t2.close();
  });

  test("a sub-agent view counts as its root", async () => {
    const root = oc.session(); const child = oc.session("/work/api-web", root.id);
    oc.addUser(root, "go");
    await start();
    const t = await terminal();
    t.show(child.id);
    const opened = await until(() => pet.last("conversation_open"), "open");
    assert.equal(opened.conversation.session_id, root.id);
    t.show(null);
    await until(() => pet.last("conversation_ended"), "ended");
  });

  test("showing it again after it ended opens a new conversation", async () => {
    const s = oc.session();
    oc.addUser(s, "hello");
    await start();
    const t = await terminal();
    t.show(s.id);
    const first = await until(() => pet.last("conversation_open"), "open");
    t.show(null);
    await until(() => pet.last("conversation_ended"), "ended");
    t.show(s.id);
    await until(() => pet.of("conversation_open").length === 2, "open again");
    assert.notEqual(epochOf(pet.of("conversation_open")[1]), epochOf(first));
    t.close();
  });

  test("a session nobody ever showed is left to the quiet-time rule", async () => {
    const s = oc.session();
    oc.addUser(s, "hello");
    await start();
    await until(() => pet.last("snapshot"), "snapshot");
    await new Promise((r) => setTimeout(r, 250));
    assert.equal(pet.of("conversation_ended").length, 0);
  });

  test("a release from the pet stops tracking until a new prompt or a terminal brings it back", async () => {
    const s = oc.session();
    oc.addUser(s, "hello");
    await start();
    const open = await until(() => pet.last("snapshot"), "snapshot");
    pet.send({ type: "release", conversation: open.conversation });
    await new Promise((r) => setTimeout(r, 100));
    const before = pet.of("conversation_open").length + pet.of("snapshot").length;
    oc.start(s); oc.finish(s); oc.say(s, "still working"); // Activity alone does not bring it back.
    await new Promise((r) => setTimeout(r, 200));
    assert.equal(pet.of("conversation_open").length + pet.of("snapshot").length, before);
    assert.equal(pet.of("conversation_ended").length, 0); // The pet already ended it.
    oc.emit("session.inbox.enqueued", { sessionID: s.id });
    const again = await until(() => pet.of("conversation_open").at(-1) && pet.of("conversation_open").length > 0 && pet.of("conversation_open").at(-1).conversation.conversation_epoch !== open.conversation.conversation_epoch ? pet.of("conversation_open").at(-1) : null, "revived");
    assert.equal(again.conversation.session_id, s.id);
  });

  test("a conversation_ended error for a conversation lets it go without announcing it", async () => {
    const s = oc.session();
    oc.addUser(s, "hello");
    await start();
    const open = await until(() => pet.last("snapshot"), "snapshot");
    pet.send({ type: "error", code: "conversation_ended", message: "conversation has ended", conversation: open.conversation });
    await new Promise((r) => setTimeout(r, 100));
    oc.say(s, "more");
    await new Promise((r) => setTimeout(r, 200));
    assert.equal(pet.of("conversation_ended").length, 0);
  });
});

describe("installing the terminal plugin", () => {
  test("adds one plugin entry, keeps foreign keys and entries, and removes only its own", () => {
    const cli = path.join(dir, "cli.json");
    fs.writeFileSync(cli, JSON.stringify({ theme: { name: "x" }, plugins: ["./herdr-opencode"] }));
    installTui(dir);
    installTui(dir); // Again: no duplicate.
    let config = JSON.parse(fs.readFileSync(cli, "utf8"));
    assert.deepEqual(config.plugins, ["./herdr-opencode", "./agent-pet-opencode"]);
    assert.deepEqual(config.theme, { name: "x" });
    assert.ok(fs.readFileSync(path.join(dir, "agent-pet-opencode", "tui.js"), "utf8").startsWith("// installed by agent-pet"));
    uninstallTui(dir);
    config = JSON.parse(fs.readFileSync(cli, "utf8"));
    assert.deepEqual(config.plugins, ["./herdr-opencode"]);
    assert.ok(!fs.existsSync(path.join(dir, "agent-pet-opencode")));
  });

  test("creates cli.json when absent, and refuses a file that is not plain JSON or not ours", () => {
    installTui(dir);
    assert.deepEqual(JSON.parse(fs.readFileSync(path.join(dir, "cli.json"), "utf8")).plugins, ["./agent-pet-opencode"]);
    uninstallTui(dir);
    assert.deepEqual(JSON.parse(fs.readFileSync(path.join(dir, "cli.json"), "utf8")), { $schema: "https://opencode.ai/v2/cli.json" });
    fs.writeFileSync(path.join(dir, "cli.json"), "{ // comment\n}");
    assert.throws(() => installTui(dir));
    fs.writeFileSync(path.join(dir, "cli.json"), "{}");
    fs.mkdirSync(path.join(dir, "agent-pet-opencode"));
    fs.writeFileSync(path.join(dir, "agent-pet-opencode", "tui.js"), "// somebody else's");
    assert.throws(() => installTui(dir), /not an Agent Pet file/);
    assert.equal(fs.readFileSync(path.join(dir, "agent-pet-opencode", "tui.js"), "utf8"), "// somebody else's");
  });
});
