// Agent Pet OpenCode bridge v1. Managed by agent-pet integration.
import { spawn } from "node:child_process";

export default async function AgentPet({ directory }) {
  const parents = new Map();
  const failed = new Set();
  // Serialize callbacks without holding up the agent. Bound both queue size and child lifetime.
  let queue = Promise.resolve();
  let pending = 0;
  const send = (type, sessionID, fields = {}) => {
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
  return {
    "chat.message": async (input) => {
      failed.delete(input.sessionID);
      send("session.updated", input.sessionID); // Restored sessions can autostart the pet too.
      send("chat.message", input.sessionID, { eventID: input.messageID });
    },
    "tool.execute.before": async (input) => {
      send("tool.execute.before", input.sessionID, { callID: input.callID, tool: input.tool });
    },
    "tool.execute.after": async (input) => {
      send("tool.execute.after", input.sessionID, { callID: input.callID });
    },
    event: async ({ event }) => {
      const p = event.properties ?? {};
      const sessionID = p.sessionID ?? p.info?.id;
      if (event.type === "session.created" || event.type === "session.updated") {
        if (p.info?.parentID) {
          if (parents.size >= 1024) parents.delete(parents.keys().next().value);
          parents.set(sessionID, p.info.parentID);
        }
        send(event.type, sessionID, { eventID: event.id });
      } else if (event.type === "session.deleted") {
        send(event.type, sessionID, { eventID: event.id });
        parents.delete(sessionID); failed.delete(sessionID);
      } else if (event.type === "session.error") {
        if (failed.size >= 1024) failed.delete(failed.values().next().value);
        failed.add(sessionID);
        send(event.type, sessionID, { aborted: p.error?.name === "MessageAbortedError", eventID: event.id });
      } else if (event.type === "session.idle") {
        // An idle notification after an error/abort must not announce successful completion.
        if (!failed.has(sessionID)) send(event.type, sessionID, { eventID: event.id });
      } else if (event.type === "permission.asked" || event.type === "permission.updated" || event.type === "question.asked") {
        // The legacy SDK calls permission requests "updated" and keeps callID at the top level.
        send(event.type === "permission.updated" ? "permission.asked" : event.type, sessionID,
          { callID: p.tool?.callID ?? p.callID, eventID: event.id });
      } else if (event.type === "message.part.updated") {
        const part = p.part;
        if (part?.type === "tool" && part.state?.status === "error")
          send("tool.error", part.sessionID, { callID: part.callID });
      }
    },
  };
}
