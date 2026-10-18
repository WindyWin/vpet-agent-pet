import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import childProcess from "node:child_process";
import { syncBuiltinESMExports } from "node:module";
import { EventEmitter } from "node:events";
import { Writable } from "node:stream";

const sent = [];
let unavailable = false;
childProcess.spawn = (executable, args, options) => {
  assert.equal(executable, "/a path/'with $symbols/pet");
  assert.deepEqual(args, ["hook", "--provider", "opencode", "--registration", "agent-pet-v1"]);
  assert.equal(options.shell, undefined);
  const child = new EventEmitter();
  child.kill = () => child.emit("close");
  child.stdin = new Writable({ write(data, encoding, done) {
    sent.push(JSON.parse(data)); done();
    queueMicrotask(() => unavailable ? child.emit("error", new Error("missing")) : child.emit("close"));
  } });
  return child;
};
syncBuiltinESMExports();
const source = "const executable = " + JSON.stringify("/a path/'with $symbols/pet") + ";\n" + await readFile(process.argv[2], "utf8");
const { default: plugin } = await import("data:text/javascript;base64," + Buffer.from(source).toString("base64"));
assert.equal(plugin.id, "agent-pet");
assert.equal(typeof plugin.setup, "function");
assert.equal(typeof plugin.server, "function");
const flush = () => new Promise(resolve => setTimeout(resolve, 30));
let slice = 0;
const types = () => sent.slice(slice).map(e => e.type);

// V1 hook object (server()) — unchanged surface for OpenCode 1.
const hooks = await plugin.server({ directory: "/repo" });
await hooks["chat.message"]({ sessionID: "parent", messageID: "m1" }, { message: "PRIVATE" });
await hooks["tool.execute.before"]({ sessionID: "parent", callID: "a", tool: "read" }, { args: "PRIVATE" });
await hooks["tool.execute.before"]({ sessionID: "parent", callID: "b", tool: "bash" }, { args: "PRIVATE" });
await hooks.event({ event: { type: "message.part.updated", properties: {
  part: { type: "tool", sessionID: "parent", callID: "a", state: { status: "error", error: "PRIVATE" } },
} } });
await hooks.event({ event: { type: "permission.asked", properties: { sessionID: "parent", tool: { callID: "b" }, patterns: ["PRIVATE"] } } });
await hooks["tool.execute.after"]({ sessionID: "parent", callID: "b", args: "PRIVATE" }, { output: "PRIVATE" });
await hooks.event({ event: { type: "session.error", properties: { sessionID: "parent", error: { name: "MessageAbortedError", data: "PRIVATE" } } } });
await hooks.event({ event: { type: "session.idle", properties: { sessionID: "parent" } } });
await flush();
assert.deepEqual(types(), ["session.updated", "chat.message", "tool.execute.before", "tool.execute.before",
  "tool.error", "permission.asked", "tool.execute.after", "session.error"]);
assert.equal(sent.at(-1).aborted, true);
assert.equal(JSON.stringify(sent).includes("PRIVATE"), false);
assert.equal(sent.at(-1).directory, "/repo");
await hooks.event({ event: { type: "session.created", properties: { info: { id: "child", parentID: "parent", title: "PRIVATE" } } } });
await hooks.event({ event: { type: "session.idle", properties: { sessionID: "child" } } });
await flush();
assert.equal(sent.at(-1).parentID, "parent");
await hooks["chat.message"]({ sessionID: "parent", messageID: "m2" });
await hooks.event({ event: { type: "session.idle", properties: { sessionID: "parent" } } });
await flush();
assert.equal(sent.at(-1).type, "session.idle");
slice = sent.length;
unavailable = true;
await hooks["chat.message"]({ sessionID: "parent" });
await flush(); // Failed sends never reject callbacks or stop the queue.
assert.equal(sent.at(-1).type, "chat.message");
const count = sent.length;
await hooks.event({ event: { type: "message.part.updated", properties: { part: { type: "text", text: "PRIVATE" } } } });
await hooks["chat.message"]({});
await flush();
assert.equal(sent.length, count);
await hooks.event({ event: { type: "permission.updated", properties: { sessionID: "legacy", callID: "c", title: "PRIVATE" } } });
await flush();
assert.equal(sent.at(-1).type, "permission.asked");
assert.equal(sent.at(-1).callID, "c");
assert.equal(JSON.stringify(sent).includes("PRIVATE"), false);
slice = sent.length;
unavailable = false;

// V2 setup(ctx) — OpenCode 2 plugin surface with a fake context.
const registrations = { prompt: null, before: null, after: null };
const signals = [];
const feed = [];
const ctx = {
  location: { directory: "/repo" },
  session: { hook: async (name, callback) => { registrations.prompt = callback; } },
  tool: { hook: async (name, callback) => {
    if (name === "execute.before") registrations.before = callback;
    else registrations.after = callback;
  } },
  event: { subscribe: (options) => {
    signals.push(options.signal);
    return (async function* () {
      for (;;) {
        while (!feed.length) {
          if (options.signal.aborted) return;
          await new Promise(resolve => setImmediate(resolve));
        }
        if (options.signal.aborted) return;
        yield feed.shift();
      }
    })();
  } },
};
const cleanup = await plugin.setup(ctx);
assert.equal(typeof cleanup, "function");
const push = async (event) => { feed.push(event); await flush(); await flush(); };
await push({ id: "e1", type: "session.created", location: { directory: "/vault" },
  data: { sessionID: "v2s", parentID: "root", title: "PRIVATE" } });
await push({ id: "e1", type: "session.created", location: { directory: "/vault" },
  data: { sessionID: "v2s", parentID: "root", title: "PRIVATE" } }); // A duplicate event id forwards once.
await push({ id: "e2", type: "session.execution.succeeded", data: { sessionID: "v2root" } });
await push({ id: "e3", type: "session.execution.failed", data: { sessionID: "v2fail", error: { type: "provider.quota" } } });
await push({ id: "e4", type: "session.execution.succeeded", data: { sessionID: "v2fail" } }); // Errors suppress idle.
await push({ id: "e5", type: "session.execution.interrupted", data: { sessionID: "v2int" } });
await push({ id: "e6", type: "permission.asked", data: { sessionID: "v2s", source: { id: "call9" }, resources: ["PRIVATE"] } });
await push({ id: "e7", type: "form.created", data: { sessionID: "v2s", form: { callID: "callf" } } });
await registrations.prompt({ sessionID: "v2s", messageID: "msg9" });
await registrations.before({ sessionID: "v2s", id: "call9", tool: "read", input: "PRIVATE" });
await registrations.after({ sessionID: "v2s", id: "call9", error: "PRIVATE" });
await flush();
assert.deepEqual(types(), ["session.created", "session.idle", "session.error", "session.error", "permission.asked",
  "question.asked", "session.updated", "chat.message", "tool.execute.before", "tool.error", "tool.execute.after"]);
assert.equal(JSON.stringify(sent).includes("PRIVATE"), false);
const first = (type) => sent.slice(slice).find(e => e.type === type);
const last = (type) => [...sent].reverse().find(e => e.type === type);
assert.equal(first("session.created").directory, "/vault"); // The session location overrides the plugin location.
assert.equal(first("session.created").parentID, "root");
assert.equal(first("session.idle").sessionID, "v2root");
assert.equal(first("session.error").aborted, false); // Failed execution is a failure, not an interruption.
const errors = sent.slice(slice).filter(e => e.type === "session.error");
assert.equal(errors.at(-1).aborted, true); // Interrupted execution reports aborts like the V1 flag.
assert.equal(errors.at(-1).sessionID, "v2int");
assert.equal(first("permission.asked").callID, "call9"); // The V2 source id carries the tool call id.
assert.equal(first("question.asked").callID, "callf");
assert.equal(first("session.updated").directory, "/repo");
assert.equal(last("tool.execute.before").tool, "read");
assert.equal(last("tool.execute.before").callID, "call9");
assert.equal(last("tool.execute.after").callID, "call9");
slice = sent.length;
unavailable = true;
await registrations.prompt({ sessionID: "v2s" });
await flush(); // Failed sends never reject callbacks or stop the queue.
assert.equal(sent.at(-1).type, "chat.message");
unavailable = false;
await cleanup();
assert.equal(signals.length, 1);
assert.equal(signals[0].aborted, true);
console.log("OpenCode plugin metadata, ordering, child identity, interruption and absent executable checks passed");
