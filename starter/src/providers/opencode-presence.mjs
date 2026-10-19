// Which OpenCode terminals show which session, for the Agent Pet interaction bridge.
//
// OpenCode 2 runs one background service that outlives every terminal and says nothing when one closes, so
// the bridge cannot tell a closed session from a quiet one. A small plugin in each terminal (opencode-tui.js,
// installed by `opencode-bridge.mjs --install-tui`) keeps a local socket open to the bridge and reports the
// session on screen, one JSON line per change: {"type":"view","session":"ses_…"|null}. The socket closing is
// the terminal closing, however it happened. Nothing but session IDs travels over it.
import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const MAX_LINE = 4096;
const SESSION_ID = /^[A-Za-z0-9_-]{1,128}$/;
export const PLUGIN_DIR = "agent-pet-opencode";
const PLUGIN_ENTRY = `./${PLUGIN_DIR}`;
const MARKER = "// installed by agent-pet";

// Keep in step with endpoint() in opencode-tui.js.
export const presenceEndpoint = () => {
  if (process.env.AGENT_PET_OPENCODE_PRESENCE) return process.env.AGENT_PET_OPENCODE_PRESENCE;
  if (process.platform === "win32") return `\\\\.\\pipe\\agent-pet-${process.env.USERNAME || "user"}-opencode-presence`;
  const root = process.env.XDG_RUNTIME_DIR || (process.platform === "darwin" ? os.tmpdir() : "/tmp");
  return path.join(root, `agent-pet-${process.getuid()}`, "opencode-presence.sock");
};

const alive = (endpoint) => new Promise((resolve) => {
  const s = net.connect(endpoint);
  s.on("connect", () => { s.destroy(); resolve(true); });
  s.on("error", () => resolve(false));
});

// Listens for terminals until `signal` aborts. Returns the server, or null when another bridge already listens.
export async function listenPresence({ endpoint = presenceEndpoint(), bridge, log = () => {}, signal }) {
  const pipe = process.platform === "win32";
  if (!pipe) {
    fs.mkdirSync(path.dirname(endpoint), { recursive: true, mode: 0o700 });
    if (fs.existsSync(endpoint)) {
      if (await alive(endpoint)) { log("another bridge already listens for OpenCode terminals"); return null; }
      fs.rmSync(endpoint, { force: true }); // A stale socket from a bridge that did not clean up.
    }
  }
  let next = 0;
  const sockets = new Set();
  const server = net.createServer((socket) => {
    const viewer = ++next;
    sockets.add(socket);
    let buffer = "";
    socket.setEncoding("utf8");
    socket.on("data", (chunk) => {
      buffer += chunk;
      if (buffer.length > MAX_LINE * 4) { socket.destroy(); return; }
      let end;
      while ((end = buffer.indexOf("\n")) >= 0) {
        const line = buffer.slice(0, end);
        buffer = buffer.slice(end + 1);
        if (line.length > MAX_LINE) { socket.destroy(); return; }
        let message;
        try { message = JSON.parse(line); } catch { continue; }
        if (message?.type !== "view") continue;
        const session = typeof message.session === "string" && SESSION_ID.test(message.session) ? message.session : null;
        bridge.viewing(viewer, session).catch((e) => log(`terminal ${viewer}: ${e.message}`));
      }
    });
    const gone = () => { sockets.delete(socket); bridge.viewerGone(viewer); };
    socket.on("close", gone);
    socket.on("error", () => {});
  });
  server.on("error", (error) => log(`terminal presence: ${error.message}`));
  await new Promise((resolve, reject) => { server.once("error", reject); server.listen(endpoint, () => { server.off("error", reject); resolve(); }); }).catch((e) => { log(`terminal presence unavailable: ${e.message}`); });
  if (!server.listening) return null;
  if (!pipe) try { fs.chmodSync(endpoint, 0o600); } catch { /* The directory is already private. */ }
  signal?.addEventListener("abort", () => { for (const socket of sockets) socket.destroy(); server.close(); if (!pipe) fs.rmSync(endpoint, { force: true }); }, { once: true });
  return server;
}

// ---- Installing the terminal plugin ------------------------------------------------------------------

export const configDir = () => process.env.OPENCODE_CONFIG_DIR
  || path.join(process.env.XDG_CONFIG_HOME || path.join(os.homedir(), ".config"), "opencode");

const readCli = (file) => {
  if (!fs.existsSync(file)) return { $schema: "https://opencode.ai/v2/cli.json" };
  const value = JSON.parse(fs.readFileSync(file, "utf8")); // Throws on comments or other non-JSON: left alone.
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("cli.json is not an object");
  return value;
};
const writeAtomic = (file, text) => {
  const temp = `${file}.agent-pet.tmp`;
  fs.writeFileSync(temp, text, { mode: 0o600 });
  fs.renameSync(temp, file);
};

// Copies the plugin next to cli.json and lists it there. Other entries and keys are kept as they are.
export function installTui(dir = configDir()) {
  const source = fs.readFileSync(fileURLToPath(new URL("./opencode-tui.js", import.meta.url)), "utf8");
  if (!source.startsWith(MARKER)) throw new Error("the plugin source is not the Agent Pet one");
  const cli = path.join(dir, "cli.json");
  const config = readCli(cli);
  if (config.plugins !== undefined && !Array.isArray(config.plugins)) throw new Error("cli.json plugins is not a list");
  const target = path.join(dir, PLUGIN_DIR, "tui.js");
  if (fs.existsSync(target) && !fs.readFileSync(target, "utf8").startsWith(MARKER)) throw new Error(`${target} is not an Agent Pet file; unchanged`);
  fs.mkdirSync(path.dirname(target), { recursive: true });
  writeAtomic(target, source);
  const plugins = config.plugins ?? [];
  if (!plugins.includes(PLUGIN_ENTRY)) writeAtomic(cli, JSON.stringify({ ...config, plugins: [...plugins, PLUGIN_ENTRY] }, null, 2) + "\n");
  return { plugin: target, config: cli };
}

export function uninstallTui(dir = configDir()) {
  const cli = path.join(dir, "cli.json");
  if (fs.existsSync(cli)) {
    const config = readCli(cli);
    if (Array.isArray(config.plugins) && config.plugins.includes(PLUGIN_ENTRY)) {
      const rest = config.plugins.filter((p) => p !== PLUGIN_ENTRY);
      const { plugins, ...others } = config;
      writeAtomic(cli, JSON.stringify(rest.length ? { ...others, plugins: rest } : others, null, 2) + "\n");
    }
  }
  const target = path.join(dir, PLUGIN_DIR, "tui.js");
  if (fs.existsSync(target) && fs.readFileSync(target, "utf8").startsWith(MARKER)) fs.rmSync(path.join(dir, PLUGIN_DIR), { recursive: true, force: true });
  return { plugin: target, config: cli };
}
