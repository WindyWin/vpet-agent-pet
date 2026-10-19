// installed by agent-pet (opencode-bridge.mjs --install-tui)
// Agent Pet terminal presence for OpenCode 2. It tells the Agent Pet interaction bridge which session this
// terminal shows, over a local socket that stays open while the terminal runs: when the terminal closes (or
// crashes) the socket closes, and the bridge lets that conversation go. It reads no message text and sends
// only a session ID. It does nothing unless the bridge is listening.
import net from "node:net";
import os from "node:os";
import path from "node:path";

const POLL_MS = 250;
const RETRY_MS = 2000;

// Keep in step with presenceEndpoint() in opencode-presence.mjs.
const endpoint = () => {
  if (process.env.AGENT_PET_OPENCODE_PRESENCE) return process.env.AGENT_PET_OPENCODE_PRESENCE;
  if (process.platform === "win32") return `\\\\.\\pipe\\agent-pet-${process.env.USERNAME || "user"}-opencode-presence`;
  const root = process.env.XDG_RUNTIME_DIR || (process.platform === "darwin" ? os.tmpdir() : "/tmp");
  return path.join(root, `agent-pet-${process.getuid()}`, "opencode-presence.sock");
};

function start(read) {
  let socket = null;
  let shown; // What the bridge last heard from this socket.
  let stopped = false;
  let retry;

  const publish = () => {
    if (!socket || socket.destroyed || !socket.writable) return;
    let session = null;
    try { session = read() ?? null; } catch { session = null; }
    if (typeof session !== "string") session = null;
    if (session === shown) return;
    shown = session;
    socket.write(JSON.stringify({ type: "view", session }) + "\n");
  };

  const connect = () => {
    if (stopped) return;
    const s = net.connect(endpoint());
    s.on("connect", () => { socket = s; shown = undefined; publish(); });
    s.on("error", () => {});
    s.on("close", () => {
      if (socket === s) socket = null;
      if (stopped) return;
      retry = setTimeout(connect, RETRY_MS);
      retry.unref?.();
    });
    s.unref?.();
  };

  const poll = setInterval(publish, POLL_MS);
  poll.unref?.();
  connect();
  return () => {
    stopped = true;
    clearInterval(poll); clearTimeout(retry);
    socket?.destroy();
  };
}

export default {
  id: "agent-pet.presence",
  // OpenCode 2 terminal plugin: the route says which session is on screen.
  setup(api) {
    return start(() => {
      const route = api?.ui?.router?.current?.();
      return route?.type === "session" ? route.sessionID : null;
    });
  },
};
