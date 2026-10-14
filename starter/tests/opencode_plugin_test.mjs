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
const hooks = await plugin({ directory: "/repo" });
const flush = () => new Promise(resolve => setTimeout(resolve, 30));
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
assert.deepEqual(sent.map(e => e.type), ["session.updated", "chat.message", "tool.execute.before", "tool.execute.before",
  "tool.error", "permission.asked", "tool.execute.after", "session.error"]);
assert.equal(sent.at(-1).aborted, true);
assert.equal(JSON.stringify(sent).includes("PRIVATE"), false);
await hooks.event({ event: { type: "session.created", properties: { info: { id: "child", parentID: "parent", title: "PRIVATE" } } } });
await hooks.event({ event: { type: "session.idle", properties: { sessionID: "child" } } });
await flush();
assert.equal(sent.at(-1).parentID, "parent");
await hooks["chat.message"]({ sessionID: "parent", messageID: "m2" });
await hooks.event({ event: { type: "session.idle", properties: { sessionID: "parent" } } });
await flush();
assert.equal(sent.at(-1).type, "session.idle");
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
console.log("OpenCode plugin metadata, ordering, child identity, interruption and absent executable checks passed");
