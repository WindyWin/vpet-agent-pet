// Agent Pet OpenCode bridge v2. Managed by agent-pet integration.
// Serves the OpenCode 2 setup(ctx) API; OpenCode 1 (>= 1.18.29) ignores setup()
// and loads the V1 hook object from server(). Both paths forward only this
// metadata contract to "agent-pet hook --provider opencode", never content.
import { spawn } from "node:child_process";

const parents = new Map();
const failed = new Set();
// Serialize callbacks without holding up the agent. Bound both queue size and child lifetime.
let queue = Promise.resolve();
let pending = 0;
// One server reconciles this plugin once per active location, and every instance
// subscribes to the server-wide event stream, so identical events arrive several
// times. Module state is shared across instances; seen keeps one forwarding each.
const seen = new Set();

const send = (type, sessionID, fields = {}, directory = "") => {
  if (typeof sessionID !== "string" || !sessionID || pending >= 128) return;
  const data = JSON.stringify({ type, sessionID, directory, parentID: parents.get(sessionID), ...fields });
  if (Buffer.byteLength(data) > 8192) return;
  pending++;
  queue = queue.then(() => new Promise((resolve) => {
    let child;
    let timer;
    const done = () => { clearTimeout(timer); resolve(); };
    try {
      child = spawn(executable, ["hook", "--provider", "opencode", "--registration", "agent-pet-v1"],
        { stdio: ["pipe", "ignore", "ignore"], windowsHide: true });
      timer = setTimeout(() => { child.kill(); done(); }, 500);
      child.once("error", done);
      child.once("close", done);
      child.stdin.on("error", () => {});
      child.stdin.end(data);
    } catch { done(); }
  })).catch(() => {}).finally(() => { pending--; });
};

const observeMessage = (sessionID, messageID, directory) => {
  failed.delete(sessionID);
  send("session.updated", sessionID, {}, directory); // Restored sessions can autostart the pet too.
  send("chat.message", sessionID, { eventID: messageID }, directory);
};

// Every failure lands in one bounded set; the oldest entry goes first.
const markFailed = (sessionID) => {
  if (failed.size >= 1024) failed.delete(failed.values().next().value);
  failed.add(sessionID);
};

// One dispatch for both public event shapes: OpenCode 2 wraps payload fields in
// `data` and covers session lifecycle/errors with the execution events; OpenCode 1
// uses `properties` plus session.created/updated/idle/error and question.asked.
const dispatch = (event, fallbackDirectory) => {
  if (!event || typeof event.type !== "string") return;
  if (typeof event.id === "string" && event.id) {
    if (seen.has(event.id)) return;
    seen.add(event.id);
    if (seen.size > 1024) seen.delete(seen.keys().next().value);
  }
  const p = event.data ?? event.properties ?? {};
  const sessionID = p.sessionID ?? p.info?.id;
  const directory = event.location?.directory ?? fallbackDirectory;
  if (event.type === "session.created" || event.type === "session.updated") {
    const parentID = p.info?.parentID ?? p.parentID;
    if (typeof parentID === "string" && parentID) {
      if (parents.size >= 1024) parents.delete(parents.keys().next().value);
      parents.set(sessionID, parentID);
    }
    send(event.type, sessionID, { eventID: event.id }, directory);
  } else if (event.type === "session.deleted") {
    send(event.type, sessionID, { eventID: event.id }, directory);
    parents.delete(sessionID); failed.delete(sessionID);
  } else if (event.type === "session.error") {
    markFailed(sessionID);
    send(event.type, sessionID, { aborted: p.error?.name === "MessageAbortedError", eventID: event.id }, directory);
  } else if (event.type === "session.idle") {
    // An idle notification after an error/abort must not announce successful completion.
    if (!failed.has(sessionID)) send(event.type, sessionID, { eventID: event.id }, directory);
  } else if (event.type === "session.execution.succeeded") {
    if (!failed.has(sessionID)) send("session.idle", sessionID, { eventID: event.id }, directory);
  } else if (event.type === "session.execution.failed") {
    markFailed(sessionID);
    send("session.error", sessionID, { aborted: false, eventID: event.id }, directory);
  } else if (event.type === "session.execution.interrupted") {
    markFailed(sessionID);
    send("session.error", sessionID, { aborted: true, eventID: event.id }, directory);
  } else if (event.type === "permission.asked" || event.type === "permission.updated") {
    // The legacy SDK calls permission requests "updated" and keeps callID at the top
    // level; OpenCode 2 puts it under source.id for tool actions.
    send(event.type === "permission.updated" ? "permission.asked" : event.type, sessionID,
      { callID: p.tool?.callID ?? p.source?.id ?? p.callID, eventID: event.id }, directory);
  } else if (event.type === "permission.replied") {
    // Approval or rejection happens through later activity; the pet does not need the reply.
  } else if (event.type === "question.asked") {
    send(event.type, sessionID, { callID: p.tool?.callID ?? p.callID, eventID: event.id }, directory);
  } else if (event.type === "form.created") {
    // OpenCode 2 merges questions into the form API; the pet keeps the V1 meaning.
    send("question.asked", p.sessionID ?? p.form?.sessionID, { callID: p.form?.callID, eventID: event.id }, directory);
  } else if (event.type === "message.part.updated") {
    const part = p.part;
    if (part?.type === "tool" && part.state?.status === "error")
      send("tool.error", part.sessionID, { callID: part.callID }, directory);
  }
};

export default {
  id: "agent-pet",
  async setup(ctx) {
    const directory = (ctx && ctx.location && ctx.location.directory) || "";
    await ctx.session.hook("prompt", (event) => {
      observeMessage(event.sessionID, event.messageID, directory);
    });
    await ctx.tool.hook("execute.before", (event) => {
      send("tool.execute.before", event.sessionID, { callID: event.id, tool: event.tool }, directory);
    });
    await ctx.tool.hook("execute.after", (event) => {
      // Mirror the V1 order: a failed tool reports the error before the completion note.
      if (event.error) send("tool.error", event.sessionID, { callID: event.id }, directory);
      send("tool.execute.after", event.sessionID, { callID: event.id }, directory);
    });
    const controller = new AbortController();
    void (async () => {
      for await (const event of ctx.event.subscribe({ signal: controller.signal })) dispatch(event, directory);
    })().catch(() => {});
    return () => { controller.abort(); };
  },
  server(input) {
    const directory = (input && input.directory) || "";
    return {
      "chat.message": async (message) => {
        observeMessage(message.sessionID, message.messageID, directory);
      },
      "tool.execute.before": async (call) => {
        send("tool.execute.before", call.sessionID, { callID: call.callID, tool: call.tool }, directory);
      },
      "tool.execute.after": async (call) => {
        send("tool.execute.after", call.sessionID, { callID: call.callID }, directory);
      },
      event: async ({ event }) => dispatch(event, directory),
    };
  },
};
