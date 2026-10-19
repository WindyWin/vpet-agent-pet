// Drives the OpenCode bridge for interaction_bridge_tests.cpp: a fake OpenCode with one session that has
// history and an open question, the real bridge connected to the endpoint in argv, and line-oriented JSON
// on stdout (observations) and stdin (commands: "finish", "quit").
import readline from "node:readline";
import { FakeOpenCode } from "./fake_opencode.mjs";
import { run } from "../src/providers/opencode-bridge.mjs";

const endpoint = process.argv[2];
const out = (object) => process.stdout.write(JSON.stringify(object) + "\n");

const oc = await new FakeOpenCode().listen();
const s = oc.session("/home/me/api-web");
oc.addUser(s, "fix the handler");
oc.addAssistant(s, "I updated the handler.");
const form = oc.ask(s, { question: "Add tests before continuing?", options: ["tests", "skip"], custom: true });

const controller = new AbortController();
const running = run({
  endpoint, signal: controller.signal, retryMs: 50, log: (m) => process.stderr.write(m + "\n"),
  service: { url: oc.url, headers: oc.headers() },
  bridgeOptions: { now: () => oc.now(), recentMs: 60_000 },
});
out({ ready: true, session: s.id, epoch: String(s.created), form: form.id });

let seenReplies = 0, seenPrompts = 0;
const poll = setInterval(() => {
  for (; seenReplies < oc.replies.length; seenReplies++) out({ reply: oc.replies[seenReplies] });
  for (; seenPrompts < oc.prompts.length; seenPrompts++) out({ prompt: oc.prompts[seenPrompts] });
}, 20);

readline.createInterface({ input: process.stdin }).on("line", async (line) => {
  if (line === "finish") { oc.say(s, "Tests added."); oc.finish(s); out({ finished: true }); }
  if (line === "quit") {
    clearInterval(poll);
    controller.abort();
    await running.catch(() => {});
    await oc.close();
    process.exit(0);
  }
});
