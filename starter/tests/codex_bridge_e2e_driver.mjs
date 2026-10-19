// Drives the Codex bridge for interaction_codex_bridge_tests.cpp: a fake Codex daemon with one thread that has
// history and a question a terminal asked, the real bridge connected to the endpoint in argv, and line-oriented
// JSON on stdout (observations) and stdin (commands: "finish", "quit").
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import readline from "node:readline";
import { FakeCodex } from "./fake_codex.mjs";
import { run } from "../src/providers/codex-bridge.mjs";

const endpoint = process.argv[2];
const out = (object) => process.stdout.write(JSON.stringify(object) + "\n");

const dir = fs.mkdtempSync(path.join(os.tmpdir(), "codex-e2e-"));
const daemon = await new FakeCodex().listen(path.join(dir, "daemon.sock"));
const t = daemon.thread({ cwd: "/home/me/api-web" });
daemon.addTurn(t, "fix the handler", "I updated the handler.");
daemon.addTurn(t, "add tests", undefined, { status: "inProgress" });
const q = daemon.ask(t, { question: "Add tests before continuing?", options: ["tests", "skip"], isOther: true });

const controller = new AbortController();
const running = run({
  endpoint, codexSocket: path.join(dir, "daemon.sock"), signal: controller.signal, retryMs: 50, log: (m) => process.stderr.write(m + "\n"),
});
out({ ready: true, session: t.id, epoch: String(t.createdAt), request: q.itemId });

let seenAnswers = 0, seenStarts = 0;
const poll = setInterval(() => {
  for (; seenAnswers < daemon.answers.length; seenAnswers++) out({ answer: daemon.answers[seenAnswers] });
  for (; seenStarts < daemon.turnStarts().length; seenStarts++) out({ turnStart: daemon.turnStarts()[seenStarts].params });
}, 20);

readline.createInterface({ input: process.stdin }).on("line", async (line) => {
  if (line === "finish") { daemon.say(t, "Tests added."); daemon.finish(t); out({ finished: true }); }
  if (line === "quit") {
    clearInterval(poll);
    controller.abort();
    await running.catch(() => {});
    await daemon.close();
    fs.rmSync(dir, { recursive: true, force: true });
    process.exit(0);
  }
});
