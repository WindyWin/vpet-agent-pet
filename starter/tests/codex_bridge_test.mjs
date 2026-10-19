// The Codex interaction bridge against a fake Codex app-server daemon (a WebSocket on a unix socket) and a
// fake Agent Pet service (a framed socket that records frames and sends commands). The real C++ service is
// exercised separately by interaction_codex_bridge_tests.cpp. Run: node --test tests/codex_bridge_test.mjs
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { afterEach, beforeEach, describe, test } from "node:test";
import { FakeCodex } from "./fake_codex.mjs";
import {
  OP, WsDecoder, connectCodex, isSubAgent, itemMessage, requestFromQuestions, run, threadMessages, wsFrame,
} from "../src/providers/codex-bridge.mjs";
import { FrameDecoder, encodeFrame } from "../src/providers/opencode-bridge.mjs";

const until = async (predicate, what, ms = 3000) => {
  const end = Date.now() + ms;
  for (;;) {
    const value = predicate();
    if (value) return value;
    if (Date.now() > end) throw new Error(`timed out waiting for ${what}`);
    await new Promise((r) => setTimeout(r, 10));
  }
};
const settleDown = (ms = 120) => new Promise((r) => setTimeout(r, ms));

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

let dir, daemon, pet, controller, running, logs;
// The control socket is a symlink to the real one, as with the real daemon.
const link = () => path.join(dir, "control.sock");
const start = async ({ bridgeOptions = {}, codexSocket = link() } = {}) => {
  controller = new AbortController();
  running = run({
    endpoint: pet.path, codexSocket, signal: controller.signal, retryMs: 40, log: (m) => logs.push(m),
    bridgeOptions: { answerWaitMs: 150, requestMs: 400, ...bridgeOptions },
  });
};

beforeEach(async () => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "cbridge-"));
  fs.mkdirSync(path.join(dir, "real"));
  daemon = await new FakeCodex().listen(path.join(dir, "real", "daemon.sock"));
  fs.symlinkSync(path.join(dir, "real", "daemon.sock"), link());
  pet = await new FakePet().listen(path.join(dir, "i.sock"));
  logs = [];
});
afterEach(async () => {
  controller?.abort();
  await running?.catch(() => {});
  await pet.close();
  await daemon.close();
  fs.rmSync(dir, { recursive: true, force: true });
  controller = running = undefined;
});

const command = (id, key, body) => ({ type: "command", command_id: id, conversation: key, source: "desktop", ...body });
const answer = (id, key, request, option, text) => command(id, key, {
  action: "answer_request", request_id: request, request_revision: 1, payload: text === undefined ? { option_id: option } : { text },
});
const followUp = (id, key, text) => command(id, key, { action: "send_follow_up", payload: { text } });
const keyOf = (t) => ({ provider: "codex", session_id: t.id, conversation_epoch: String(t.createdAt) });
const clientIdOf = (commandId) => `apet_${createHash("sha256").update(commandId).digest("hex").slice(0, 40)}`;
const resultOf = (id) => pet.of("command_result").find((r) => r.command_id === id);
const snapshotOf = (t) => pet.of("snapshot").filter((s) => s.conversation.session_id === t.id).at(-1);

describe("WebSocket codec", () => {
  const message = (op, text, mask = false) => wsFrame(op, Buffer.from(text), mask);

  test("frames of every length form round-trip, byte by byte too", () => {
    for (const size of [0, 5, 125, 126, 300, 65535, 65536, 70000]) {
      const text = "x".repeat(size);
      for (const mask of [false, true]) {
        const whole = new WsDecoder().push(message(OP.text, text, mask));
        assert.equal(whole.length, 1);
        assert.equal(whole[0].payload.toString(), text);
      }
    }
    const decoder = new WsDecoder();
    const out = [];
    for (const byte of message(OP.text, "héllo", true)) out.push(...decoder.push(Buffer.from([byte])));
    assert.deepEqual(out.map((m) => m.payload.toString()), ["héllo"]);
  });

  test("fragments are joined and a ping between them is delivered on its own", () => {
    const first = Buffer.from([0x01, 3, ...Buffer.from("abc")]);          // text, not final
    const ping = Buffer.from([0x89, 2, ...Buffer.from("pp")]);
    const rest = Buffer.from([0x80, 3, ...Buffer.from("def")]);           // continuation, final
    const out = new WsDecoder().push(Buffer.concat([first, ping, rest]));
    assert.deepEqual(out.map((m) => [m.opcode, m.payload.toString()]), [[OP.ping, "pp"], [OP.text, "abcdef"]]);
  });

  test("protocol violations and oversized messages throw", () => {
    assert.throws(() => new WsDecoder().push(Buffer.from([0x80, 1, 0x61])), /continuation/);
    assert.throws(() => new WsDecoder().push(Buffer.from([0x09, 0])), /control/);          // fragmented control frame
    assert.throws(() => new WsDecoder().push(Buffer.from([0x88, 126, 0, 200])), /control/); // control frame too long
    assert.throws(() => new WsDecoder(100).push(message(OP.text, "x".repeat(101))), /too large/);
    const huge = Buffer.alloc(10);
    huge[0] = 0x81; huge[1] = 127; huge.writeBigUInt64BE(1n << 40n, 2);
    assert.throws(() => new WsDecoder().push(huge), /too large/);
    assert.throws(() => new WsDecoder(100).push(Buffer.concat([Buffer.from([0x01, 60]), Buffer.alloc(60, 97), Buffer.from([0x80, 60]), Buffer.alloc(60, 97)])), /too large/);
  });
});

describe("mapping", () => {
  test("messages: user text, final and unphased agent text; not commentary, reasoning or empty text", () => {
    assert.deepEqual(itemMessage({ type: "userMessage", id: "u", content: [{ type: "text", text: "a" }, { type: "image", url: "x" }, { type: "text", text: "b" }] }, 5),
      { id: "u", role: "user", text: "a\nb", created: 5 });
    assert.equal(itemMessage({ type: "userMessage", id: "u", content: [{ type: "image", url: "x" }] }), null);
    assert.equal(itemMessage({ type: "agentMessage", id: "a", text: "hi", phase: "final_answer" })?.role, "agent");
    assert.equal(itemMessage({ type: "agentMessage", id: "a", text: "hi", phase: null })?.text, "hi");
    assert.equal(itemMessage({ type: "agentMessage", id: "a", text: "hi", phase: "commentary" }), null);
    assert.equal(itemMessage({ type: "agentMessage", id: "a", text: "  " }), null);
    assert.equal(itemMessage({ type: "reasoning", id: "r" }), null);
    assert.equal(itemMessage({ type: "somethingNew", id: "n", text: "x" }), null);
    assert.equal(itemMessage(null), null);
  });

  test("the streaming last agent message of a running turn waits, unless the thread is idle", () => {
    const thread = { turns: [{ status: "inProgress", startedAt: 7, items: [
      { type: "userMessage", id: "u", content: [{ type: "text", text: "go" }] },
      { type: "agentMessage", id: "a1", text: "done step", phase: "final_answer" },
      { type: "commandExecution", id: "c" },
      { type: "agentMessage", id: "a2", text: "partial…", phase: "final_answer" }] }] };
    assert.deepEqual(threadMessages(thread).map((m) => m.id), ["u", "a1"]);
    assert.deepEqual(threadMessages(thread, true).map((m) => m.id), ["u", "a1", "a2"]);
    assert.equal(threadMessages(thread)[0].created, 7000);
  });

  test("questions: one question with options or free text; not secret, several, or ambiguous", () => {
    const base = { itemId: "call_1", questions: [{ id: "color", header: "Color", question: "Pick", isOther: true, isSecret: false, options: [{ label: "Red", description: "r" }, { label: "Blue" }] }] };
    const ok = requestFromQuestions(base);
    assert.deepEqual(ok.request, { request_id: "call_1", revision: 1, question: "Pick", options: [{ id: "Red", label: "Red" }, { id: "Blue", label: "Blue" }], custom_answer: true, state: "pending" });
    assert.equal(ok.qid, "color");
    const strict = structuredClone(base); strict.questions[0].isOther = false;
    assert.equal(requestFromQuestions(strict).request.custom_answer, false);
    const free = structuredClone(base); free.questions[0].options = []; free.questions[0].isOther = false;
    assert.equal(requestFromQuestions(free).request.custom_answer, true);
    const noText = structuredClone(base); delete noText.questions[0].question;
    assert.equal(requestFromQuestions(noText).request.question, "Color"); // The header stands in.
    const secret = structuredClone(base); secret.questions[0].isSecret = true;
    assert.equal(requestFromQuestions(secret), null);
    assert.equal(requestFromQuestions({ ...base, questions: [...base.questions, ...base.questions] }), null);
    const dup = structuredClone(base); dup.questions[0].options.push({ label: "Red" });
    assert.equal(requestFromQuestions(dup), null);
    assert.equal(requestFromQuestions({ ...base, itemId: undefined }), null);
    assert.equal(requestFromQuestions({ itemId: "x", questions: [] }), null);
    assert.equal(requestFromQuestions(null), null);
  });

  test("sub-agent threads are recognized by parent or source", () => {
    assert.equal(isSubAgent({ parentThreadId: "p" }), true);
    assert.equal(isSubAgent({ source: { subAgent: { thread_spawn: {} } } }), true);
    assert.equal(isSubAgent({ source: "cli", parentThreadId: null }), false);
  });
});

describe("daemon connection", () => {
  test("initializes with the experimental API, then confirms, through a symlinked socket", async () => {
    daemon.noisy = true; // Pings and fragmented messages on every reply.
    const conn = await connectCodex({ socketPath: link() });
    assert.deepEqual(daemon.log[0].method, "initialize");
    assert.equal(daemon.log[0].params.capabilities.experimentalApi, true);
    assert.equal(daemon.log[0].params.clientInfo.name, "agent-pet");
    await until(() => daemon.notifications.includes("initialized"), "initialized");
    assert.deepEqual(await conn.call("thread/loaded/list", {}), { data: [], nextCursor: null });
    await until(() => daemon.pongs >= 2, "pongs"); // Every ping was answered.
    await assert.rejects(() => conn.call("nope", {}), /unknown method/);
    conn.close();
    await conn.closed;
    await assert.rejects(() => conn.call("thread/loaded/list", {}), /closed/);
  });

  test("a socket that is not a WebSocket server is refused, and an absent one is ENOENT", async () => {
    const plain = path.join(dir, "plain.sock");
    const server = net.createServer((s) => s.end("HTTP/1.1 404 Not Found\r\n\r\n"));
    await new Promise((r) => server.listen(plain, r));
    await assert.rejects(() => connectCodex({ socketPath: plain }), /refused|closed/);
    server.close();
    await assert.rejects(() => connectCodex({ socketPath: path.join(dir, "absent.sock") }), { code: "ENOENT" });
  });
});

describe("registration and snapshots", () => {
  test("loaded threads are tracked with their history; sub-agents and saved threads are not", async () => {
    const t = daemon.thread({ cwd: "/home/me/api-web/" });
    daemon.addTurn(t, "fix the handler", "I updated it.");
    const turn = daemon.addTurn(t, "and tests?", "Added.");
    turn.items.splice(1, 0, { type: "agentMessage", id: "narration", text: "looking…", phase: "commentary" }, { type: "reasoning", id: "r1" });
    const sub = daemon.thread({ parentThreadId: t.id });
    daemon.addTurn(sub, "child work", "child done");
    const saved = daemon.thread({ loaded: false, status: { type: "notLoaded" } });
    daemon.addTurn(saved, "old", "old answer");
    await start();
    const hello = await until(() => pet.last("hello"), "hello");
    assert.equal(hello.provider, "codex");
    assert.ok(hello.generation);
    const entry = await until(() => [...hello.conversations, ...pet.of("conversation_open")].find((c) => c.conversation.session_id === t.id), "entry");
    assert.deepEqual(entry.conversation, keyOf(t));
    assert.equal(entry.title, "api-web");
    assert.equal(entry.ready, true);
    assert.deepEqual(entry.capabilities, ["messages", "single_choice", "custom_answer", "follow_up", "reconcile"]);
    const snapshot = await until(() => snapshotOf(t), "snapshot");
    assert.deepEqual(snapshot.messages.map((m) => [m.sequence, m.role, m.text]), [[1, "user", "fix the handler"], [2, "agent", "I updated it."], [3, "user", "and tests?"], [4, "agent", "Added."]]);
    assert.ok(snapshot.messages.every((m) => m.history === true));
    assert.equal(snapshot.watermark, 4);
    assert.equal(snapshot.first_sequence, 1);
    assert.equal(snapshot.ready, true);
    await settleDown();
    const opened = [...hello.conversations, ...pet.of("conversation_open")].map((c) => c.conversation.session_id);
    assert.deepEqual(opened, [t.id]);
    // Saved threads are neither read nor resumed; sub-agents are metadata-only until they ask.
    assert.ok(!daemon.log.some((l) => l.params?.threadId === saved.id));
    assert.ok(!daemon.log.some((l) => l.params?.threadId === sub.id && l.params.includeTurns));
    assert.equal(daemon.of("thread/resume").length, 0);
    assert.equal(logs.length, 0);
  });

  test("only the newest window is sent, numbered from the true start", async () => {
    const t = daemon.thread();
    for (let i = 1; i <= 70; i++) daemon.addTurn(t, `m${i}`, `a${i}`);
    await start();
    const snapshot = await until(() => snapshotOf(t), "snapshot");
    assert.equal(snapshot.messages.length, 50);
    assert.equal(snapshot.first_sequence, 91);
    assert.equal(snapshot.watermark, 140);
    assert.equal(snapshot.messages.at(-1).text, "a70");
  });

  test("a running thread is not ready; a question already pending is resumed, replayed and in the snapshot", async () => {
    const t = daemon.thread();
    daemon.addTurn(t, "go", undefined, { status: "inProgress" });
    const q = daemon.ask(t, { question: "Add tests?", options: ["Yes", "No"] });
    await start();
    const snapshot = await until(() => snapshotOf(t)?.requests.length && snapshotOf(t), "snapshot with request");
    assert.equal(snapshot.ready, false);
    assert.deepEqual(snapshot.requests[0], { request_id: q.itemId, revision: 1, question: "Add tests?", options: [{ id: "Yes", label: "Yes" }, { id: "No", label: "No" }], custom_answer: true, state: "pending" });
    const resume = daemon.of("thread/resume");
    assert.equal(resume.length, 1);
    assert.equal(resume[0].params.threadId, t.id);
  });

  test("a thread without a first turn yet is tracked empty; resuming it fails quietly", async () => {
    const t = daemon.thread();
    await start();
    const snapshot = await until(() => snapshotOf(t), "snapshot");
    assert.deepEqual(snapshot.messages, []);
    daemon.setStatus(t, { type: "active", activeFlags: ["waitingOnUserInput"] }); // Nothing to resume into yet.
    await until(() => daemon.of("thread/resume").length, "resume attempt");
    await settleDown();
    assert.equal(pet.of("request").length, 0);
    assert.ok(logs.some((l) => l.includes("resume failed")));
  });

  test("a sub-agent shows up only when it asks a question", async () => {
    const parent = daemon.thread();
    daemon.addTurn(parent, "go", "ok");
    await start();
    await until(() => snapshotOf(parent), "snapshot");
    const child = daemon.thread({ parentThreadId: parent.id });
    daemon.addTurn(child, "child task", undefined, { status: "inProgress" });
    daemon.setStatus(child, { type: "active", activeFlags: [] });
    await settleDown();
    const opened = () => pet.of("conversation_open").some((f) => f.conversation.session_id === child.id);
    assert.equal(opened(), false);
    const q = daemon.ask(child, { question: "Child asks?" });
    await until(opened, "conversation_open");
    const snapshot = await until(() => snapshotOf(child), "child snapshot");
    assert.equal(snapshot.requests[0].request_id, q.itemId);
    assert.deepEqual(snapshot.messages.map((m) => m.text), ["child task"]);
  });
});

describe("live events", () => {
  test("a terminal turn publishes the prompt, then the final answer, then ready", async () => {
    const t = daemon.thread();
    daemon.addTurn(t, "first", "one");
    await start();
    await until(() => snapshotOf(t), "snapshot");
    daemon.addTurn(t, "second", undefined, { status: "inProgress" });
    daemon.setStatus(t, { type: "active", activeFlags: [] });
    const prompt = await until(() => pet.of("message").find((m) => m.message.text === "second"), "prompt");
    assert.equal(prompt.message.sequence, 3);
    assert.equal(prompt.message.role, "user");
    assert.equal(prompt.message.history, undefined);
    await until(() => pet.of("ready").some((r) => r.ready === false), "not ready");
    daemon.say(t, "two");
    daemon.finish(t);
    const answerFrame = await until(() => pet.of("message").find((m) => m.message.text === "two"), "answer");
    assert.equal(answerFrame.message.sequence, 4);
    const ready = await until(() => pet.of("ready").filter((r) => r.ready === true).at(-1), "ready");
    assert.ok(pet.frames.indexOf(answerFrame) < pet.frames.indexOf(ready)); // Answer first.
    assert.equal(pet.of("message").filter((m) => m.message.text === "two").length, 1); // Not repeated.
  });

  test("a thread that appears after startup is opened and snapshotted", async () => {
    await start();
    await until(() => pet.last("hello"), "hello");
    const t = daemon.thread();
    daemon.addTurn(t, "hello there", undefined, { status: "inProgress" });
    daemon.setStatus(t, { type: "active", activeFlags: [] });
    const open = await until(() => pet.last("conversation_open"), "conversation_open");
    assert.deepEqual(open.conversation, keyOf(t));
    const snapshot = await until(() => snapshotOf(t), "snapshot");
    assert.equal(snapshot.messages[0].text, "hello there");
    assert.equal(snapshot.ready, false);
  });

  test("archiving ends the conversation; unloading only makes it not ready", async () => {
    const a = daemon.thread();
    daemon.addTurn(a, "x", "y");
    const b = daemon.thread();
    daemon.addTurn(b, "x", "y");
    await start();
    await until(() => snapshotOf(a) && snapshotOf(b), "snapshots");
    daemon.setStatus(b, { type: "notLoaded" });
    await until(() => pet.of("ready").some((r) => r.conversation.session_id === b.id && r.ready === false), "b not ready");
    assert.equal(pet.of("conversation_ended").length, 0);
    for (const c of daemon.clients) c.write({ method: "thread/archived", params: { threadId: a.id } });
    const ended = await until(() => pet.last("conversation_ended"), "ended");
    assert.deepEqual(ended.conversation, keyOf(a));
  });
});

describe("questions", () => {
  const withQuestion = async (options = {}) => {
    const t = daemon.thread();
    daemon.addTurn(t, "go", "starting");
    await start();
    await until(() => snapshotOf(t), "snapshot");
    daemon.addTurn(t, "next", undefined, { status: "inProgress" });
    const q = daemon.ask(t, options);
    const request = await until(() => pet.last("request"), "request");
    return { t, q, request, key: keyOf(t) };
  };

  test("a question from a terminal turn is resumed, replayed and published with its options", async () => {
    const { t, q, request } = await withQuestion({ question: "Pick one", options: ["x", "y"], isOther: false });
    assert.equal(request.request.request_id, q.itemId);
    assert.equal(request.request.custom_answer, false);
    assert.deepEqual(request.request.options, [{ id: "x", label: "x" }, { id: "y", label: "y" }]);
    assert.equal(daemon.of("thread/resume").length, 1);
    assert.equal(daemon.of("thread/resume")[0].params.threadId, t.id);
    assert.equal(pet.of("ready").at(-1).ready, false);
  });

  test("free text is a custom-answer question without options", async () => {
    const { request } = await withQuestion({ question: "Name?", options: [], isOther: false });
    assert.equal(request.request.custom_answer, true);
    assert.deepEqual(request.request.options, []);
  });

  test("an answer becomes the JSON-RPC response with the question ID and label, accepted, then the request closes", async () => {
    const { q, key } = await withQuestion();
    pet.send(answer("c1", key, q.itemId, "No"));
    const result = await until(() => resultOf("c1"), "result");
    assert.equal(result.outcome, "accepted");
    assert.deepEqual(daemon.answers, [{ answers: { q0: { answers: ["No"] } } }]);
    const state = await until(() => pet.last("request_state"), "request_state");
    assert.deepEqual([state.request_id, state.state], [q.itemId, "answered"]);
    assert.ok(pet.frames.indexOf(result) < pet.frames.indexOf(state)); // Result first.
    await until(() => pet.of("ready").at(-1).ready === false, "still running");
  });

  test("a custom answer is sent as the typed text", async () => {
    const { q, key } = await withQuestion();
    pet.send(answer("c1", key, q.itemId, undefined, "something else"));
    await until(() => resultOf("c1"), "result");
    assert.deepEqual(daemon.answers[0], { answers: { q0: { answers: ["something else"] } } });
  });

  test("a question answered in the terminal closes the request", async () => {
    const { t, q } = await withQuestion();
    daemon.answerElsewhere(t, q.itemId, { answers: { q0: { answers: ["Yes"] } } });
    const state = await until(() => pet.last("request_state"), "request_state");
    assert.deepEqual([state.request_id, state.state], [q.itemId, "answered"]);
    const other = daemon.ask(t, { question: "Again?" });
    await until(() => pet.of("request").some((r) => r.request.request_id === other.itemId), "second request");
  });

  test("a second question replaces an unresolved first", async () => {
    const { t, q } = await withQuestion();
    const next = daemon.ask(t, { question: "Instead?" });
    const replaced = await until(() => pet.of("request_state").find((r) => r.state === "replaced"), "replaced");
    assert.deepEqual([replaced.request_id, replaced.replaced_by], [q.itemId, next.itemId]);
  });

  test("an invalid option, an unknown request and a stale conversation are rejected without touching the daemon", async () => {
    const { q, key } = await withQuestion({ isOther: false });
    pet.send(answer("c1", key, q.itemId, "Maybe"));
    assert.deepEqual([(await until(() => resultOf("c1"), "c1")).outcome, resultOf("c1").reason, resultOf("c1").still_answerable], ["rejected", "invalid_answer", true]);
    pet.send(answer("c2", key, q.itemId, undefined, "free text")); // Custom text is not allowed here.
    assert.equal((await until(() => resultOf("c2"), "c2")).reason, "invalid_answer");
    pet.send(answer("c3", key, "call_missing", "Yes"));
    assert.deepEqual([(await until(() => resultOf("c3"), "c3")).reason, resultOf("c3").still_answerable], ["unknown_request", false]);
    pet.send(answer("c4", { ...key, conversation_epoch: "stale" }, q.itemId, "Yes"));
    assert.equal((await until(() => resultOf("c4"), "c4")).reason, "unknown_conversation");
    assert.equal(daemon.responses.length, 0);
  });

  test("an answer the daemon never confirms is unknown, and a later resolution settles it as accepted", async () => {
    const { t, q, key } = await withQuestion();
    daemon.swallowAnswers = true;
    pet.send(answer("c1", key, q.itemId, "Yes"));
    const unknown = await until(() => resultOf("c1"), "unknown");
    assert.deepEqual([unknown.outcome, unknown.reason], ["unknown", "not_confirmed"]);
    assert.equal(pet.of("request_state").length, 0);
    daemon.answerElsewhere(t, q.itemId, { answers: { q0: { answers: ["Yes"] } } });
    await until(() => pet.of("command_result").filter((r) => r.command_id === "c1").length === 2, "late result");
    assert.equal(pet.of("command_result").at(-1).outcome, "accepted");
    await until(() => pet.last("request_state"), "state");
  });

  test("the same answer command twice answers once", async () => {
    const { q, key } = await withQuestion();
    pet.send(answer("c1", key, q.itemId, "Yes"));
    await until(() => resultOf("c1"), "result");
    pet.send(answer("c1", key, q.itemId, "Yes"));
    await until(() => pet.of("command_result").length === 2, "second result");
    assert.deepEqual(pet.of("command_result").map((r) => r.outcome), ["accepted", "accepted"]);
    assert.equal(daemon.responses.length, 1);
  });

  test("secret and multi-question forms stay with the terminal, and approvals are never answered", async () => {
    const t = daemon.thread();
    daemon.addTurn(t, "go", "ok");
    await start();
    await until(() => snapshotOf(t), "snapshot");
    daemon.addTurn(t, "next", undefined, { status: "inProgress" });
    daemon.ask(t, { isSecret: true });
    await until(() => daemon.of("thread/resume").length === 1, "resume");
    daemon.answerElsewhere(t, t.pending[0].itemId, { answers: {} });
    daemon.ask(t, { extra: [{ id: "q1", header: "H", question: "Second?", isOther: true, isSecret: false, options: [] }] });
    await settleDown();
    daemon.approval(t);
    await settleDown();
    assert.equal(pet.of("request").length, 0);
    assert.equal(daemon.responses.length, 0); // Not even a refusal: the terminal may still want to answer.
    assert.ok(logs.every((l) => !l.includes("Second?") && !l.includes("go")));
  });
});

describe("follow-ups", () => {
  const idle = async () => {
    const t = daemon.thread();
    daemon.addTurn(t, "go", "done");
    await start();
    await until(() => snapshotOf(t), "snapshot");
    return { t, key: keyOf(t) };
  };

  test("a follow-up on an idle thread starts a turn with a derived client ID and is accepted", async () => {
    const { t, key } = await idle();
    pet.send(followUp("cmd-1", key, "now add tests"));
    const result = await until(() => resultOf("cmd-1"), "result");
    assert.equal(result.outcome, "accepted");
    const starts = daemon.turnStarts();
    assert.equal(starts.length, 1);
    assert.deepEqual(starts[0].params, { threadId: t.id, input: [{ type: "text", text: "now add tests" }], clientUserMessageId: clientIdOf("cmd-1") });
    assert.match(starts[0].params.clientUserMessageId, /^apet_[0-9a-f]{40}$/);
    assert.equal(result.receipt, t.turns.at(-1).id);
    await until(() => pet.of("ready").some((r) => r.ready === false), "not ready");
    const prompt = await until(() => pet.of("message").find((m) => m.message.text === "now add tests"), "prompt");
    assert.equal(prompt.message.sequence, 3);
    daemon.say(t, "Tests added.");
    daemon.finish(t);
    await until(() => pet.of("message").find((m) => m.message.text === "Tests added."), "answer");
    await until(() => pet.of("ready").at(-1).ready === true, "ready");
  });

  test("a follow-up is never sent into a running turn", async () => {
    const { t, key } = await idle();
    daemon.addTurn(t, "busy", undefined, { status: "inProgress" });
    daemon.setStatus(t, { type: "active", activeFlags: [] }); // Started in a terminal; the bridge has not heard yet.
    pet.send(followUp("cmd-1", key, "interrupt!"));
    const result = await until(() => resultOf("cmd-1"), "result");
    assert.deepEqual([result.outcome, result.reason], ["rejected", "not_ready"]);
    assert.equal(daemon.turnStarts().length, 0);
    assert.equal(daemon.steered, 0);
  });

  test("a follow-up is refused while a question is open, even if the thread reads as idle", async () => {
    const { t, key } = await idle();
    daemon.addTurn(t, "next", undefined, { status: "inProgress" });
    daemon.ask(t);
    await until(() => pet.last("request"), "request");
    pet.send(followUp("cmd-1", key, "hello?"));
    assert.equal((await until(() => resultOf("cmd-1"), "result")).reason, "not_ready");
    assert.equal(daemon.turnStarts().length, 0);
  });

  test("the same follow-up command twice starts one turn", async () => {
    const { key } = await idle();
    pet.send(followUp("cmd-1", key, "once"));
    await until(() => resultOf("cmd-1"), "result");
    pet.send(followUp("cmd-1", key, "once"));
    await until(() => pet.of("command_result").length === 2, "second result");
    assert.equal(pet.of("command_result")[1].outcome, "accepted");
    assert.equal(pet.of("command_result")[1].receipt, pet.of("command_result")[0].receipt);
    assert.equal(daemon.turnStarts().length, 1);
  });

  test("a daemon refusal is rejected, not unknown", async () => {
    const { t, key } = await idle();
    daemon.threads.delete(t.id);
    pet.send(followUp("cmd-1", key, "hi"));
    const result = await until(() => resultOf("cmd-1"), "result");
    assert.equal(result.outcome, "rejected");
  });

  test("a follow-up lost with the connection is unknown, then found by its client ID after reconnecting, never resent", async () => {
    const { t, key } = await idle();
    daemon.failTurnStart = "apply-then-drop";
    pet.send(followUp("cmd-1", key, "did it land"));
    const unknown = await until(() => resultOf("cmd-1"), "unknown");
    assert.equal(unknown.outcome, "unknown");
    const settled = await until(() => pet.of("command_result").find((r) => r.command_id === "cmd-1" && r.outcome === "accepted"), "resolved");
    assert.equal(settled.receipt, t.turns.at(-1).id);
    assert.equal(daemon.turnStarts().length, 1);
    pet.send(followUp("cmd-1", key, "did it land")); // The same command again changes nothing.
    await until(() => pet.of("command_result").length === 3, "repeat");
    assert.equal(daemon.turnStarts().length, 1);
  });

  test("a follow-up that never arrived is rejected as not delivered once the old connection is gone", async () => {
    const { t, key } = await idle();
    daemon.failTurnStart = "drop";
    pet.send(followUp("cmd-1", key, "lost"));
    assert.equal((await until(() => resultOf("cmd-1"), "unknown")).outcome, "unknown");
    const final = await until(() => pet.of("command_result").find((r) => r.command_id === "cmd-1" && r.outcome === "rejected"), "rejected");
    assert.equal(final.reason, "not_delivered");
    assert.equal(final.still_answerable, true);
    assert.ok(!t.turns.some((turn) => turn.items.some((i) => i.clientId)));
    assert.equal(daemon.turnStarts().length, 1);
  });

  test("a follow-up the daemon is slow to answer is resolved by looking, not by resending", async () => {
    const { t, key } = await idle();
    daemon.failTurnStart = "apply-then-hang";
    pet.send(followUp("cmd-1", key, "slow"));
    const result = await until(() => resultOf("cmd-1"), "result", 4000);
    assert.equal(result.outcome, "accepted"); // Found by client ID after the timeout.
    assert.equal(result.receipt, t.turns.at(-1).id);
    assert.equal(daemon.turnStarts().length, 1);
  });
});

describe("reconnecting", () => {
  test("the daemon may start after the bridge; the wait is logged once", async () => {
    const t = daemon.thread();
    daemon.addTurn(t, "go", "done");
    await start({ codexSocket: path.join(dir, "later.sock") });
    await until(() => pet.last("hello"), "hello");
    await settleDown(250);
    assert.equal(logs.filter((l) => l.includes("not running")).length, 1);
    fs.symlinkSync(path.join(dir, "real", "daemon.sock"), path.join(dir, "later.sock"));
    await until(() => snapshotOf(t), "snapshot");
    assert.ok(logs.some((l) => l.includes("connected")));
  });

  test("a daemon restart keeps a pending question, which is answerable again once it is replayed", async () => {
    const t = daemon.thread();
    daemon.addTurn(t, "go", undefined, { status: "inProgress" });
    const q = daemon.ask(t);
    await start();
    await until(() => snapshotOf(t)?.requests.length, "request");
    const key = keyOf(t);
    daemon.dropAll();
    await until(() => daemon.of("thread/resume").length >= 2, "resumed again");
    await until(() => daemon.clients.size === 1 && [...daemon.clients][0].requests.size === 1, "replayed");
    pet.send(answer("c1", key, q.itemId, "Yes"));
    assert.equal((await until(() => resultOf("c1"), "result")).outcome, "accepted");
    assert.equal(daemon.answers.length, 1);
  });

  test("while the daemon is away an answer is rejected as answerable later, and nothing is sent", async () => {
    const t = daemon.thread();
    daemon.addTurn(t, "go", undefined, { status: "inProgress" });
    const q = daemon.ask(t);
    await start();
    await until(() => snapshotOf(t)?.requests.length, "snapshot");
    fs.unlinkSync(link()); // The path now dangles for new connections too.
    daemon.dropAll();
    await until(() => logs.some((l) => l.includes("not running")), "daemon gone");
    pet.send(answer("c1", keyOf(t), q.itemId, "Yes"));
    const result = await until(() => resultOf("c1"), "result");
    assert.deepEqual([result.outcome, result.reason, result.still_answerable], ["rejected", "provider_unavailable", true]);
    pet.send(followUp("c2", keyOf(t), "hello"));
    assert.equal((await until(() => resultOf("c2"), "c2")).reason, "provider_unavailable");
    assert.equal(daemon.responses.length, 0);
  });

  test("after the Agent Pet link drops, a new hello and snapshot carry what happened meanwhile", async () => {
    const t = daemon.thread();
    daemon.addTurn(t, "go", "one");
    await start();
    await until(() => snapshotOf(t), "snapshot");
    const key = keyOf(t);
    pet.autoResync = false;
    pet.drop();
    await until(() => pet.of("hello").length >= 2, "second hello");
    // While the service is away: a command it sent before, executed now, and a message.
    pet.send({ type: "command", command_id: "late", conversation: key, source: "desktop", action: "send_follow_up", payload: { text: "meanwhile" } });
    await until(() => resultOf("late"), "late result");
    daemon.say(t, "reply while away");
    daemon.finish(t);
    pet.autoResync = true;
    pet.drop();
    await until(() => pet.of("hello").length >= 3, "third hello");
    const third = pet.frames.indexOf(pet.of("hello")[2]);
    const snapshot = await until(() => pet.of("snapshot").find((s) => pet.frames.indexOf(s) > third), "snapshot after the third hello");
    assert.deepEqual(snapshot.command_outcomes, [{ command_id: "late", outcome: "accepted" }]);
    assert.ok(snapshot.messages.some((m) => m.text === "reply while away"));
    assert.notEqual(pet.last("hello").generation, pet.of("hello")[0].generation);
    assert.equal(snapshot.ready, true);
  });

  test("messages that arrived while the daemon connection was down are recovered on reconnect", async () => {
    const t = daemon.thread();
    daemon.addTurn(t, "go", "one");
    await start();
    await until(() => snapshotOf(t), "snapshot");
    daemon.dropAll();
    daemon.addTurn(t, "typed in the terminal meanwhile", "two");
    const message = await until(() => pet.of("message").find((m) => m.message.text === "two"), "recovered", 5000);
    assert.equal(message.message.sequence, 4);
    await until(() => pet.of("ready").at(-1).ready === true, "ready again");
  });
});
