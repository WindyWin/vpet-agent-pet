// A small stand-in for the OpenCode 2 HTTP API with the response shapes observed from a live v2.0.29
// service (docs/design/agent-interactions-provider-proof.md): sessions, paged messages, question forms,
// prompts with client-chosen IDs, the active set and the server-sent event stream.
import http from "node:http";

export class FakeOpenCode {
  sessions = new Map();
  streams = new Set();
  replies = [];   // {sessionID, formID, answer} for every accepted reply
  prompts = [];   // Every prompt body received
  requests = [];  // "METHOD path" of every request
  failNext = null; // Set to "reply" or "prompt" to make that call drop the connection
  #clock = 1_700_000_000_000;
  #n = 0;
  password = "secret";

  id(prefix) { return `${prefix}_${String(++this.#n).padStart(6, "0")}`; }
  now() { return (this.#clock += 10); }

  session(directory = "/work/api-web", parentID) {
    const s = { id: this.id("ses"), directory, parentID, created: this.now(), items: [], forms: new Map(), running: false, prompted: new Map() };
    this.sessions.set(s.id, s);
    return s;
  }
  addUser(s, text, id = this.id("msg")) { s.items.push({ id, time: { created: this.now() }, type: "user", text }); return id; }
  addAssistant(s, text, extraParts = []) {
    const id = this.id("msg");
    s.items.push({ id, time: { created: this.now() }, type: "assistant", content: [...extraParts, { type: "text", text }] });
    return id;
  }
  addIdle(s) { s.items.push({ id: this.id("msg"), time: { created: this.now() }, type: "idle", outcome: "succeeded" }); }

  ask(s, { question = "Add tests?", options = ["Yes", "No"], custom = true, key = "q0", kind = "question", extraFields = [] } = {}) {
    const form = {
      id: this.id("frm"), sessionID: s.id, title: "Questions", metadata: { kind },
      fields: [{ key, title: "Tests", description: question, type: "string", options: options.map((o) => ({ value: o, label: o, description: `${o}!` })), custom }, ...extraFields],
      state: { status: "pending" },
    };
    s.forms.set(form.id, form);
    s.running = true; // The question tool blocks the turn.
    this.emit("form.created", { form: { id: form.id, sessionID: s.id, title: form.title, metadata: form.metadata, fields: form.fields } });
    return form;
  }
  cancel(s, formID) {
    s.forms.get(formID).state = { status: "cancelled" };
    this.emit("form.cancelled", { id: formID, sessionID: s.id });
  }
  answerElsewhere(s, formID, answer) {
    s.forms.get(formID).state = { status: "answered", answer };
    this.emit("form.replied", { id: formID, sessionID: s.id, answer });
  }
  start(s) { s.running = true; this.emit("session.execution.started", { sessionID: s.id }); }
  finish(s, outcome = "succeeded") { s.running = false; this.emit(`session.execution.${outcome}`, { sessionID: s.id }); }
  say(s, text) { const id = this.addAssistant(s, text); this.emit("session.text.ended", { sessionID: s.id, assistantMessageID: id, ordinal: 0, text }); return id; }
  remove(s) { this.sessions.delete(s.id); this.emit("session.deleted", { sessionID: s.id }); }

  emit(type, data) {
    const event = { id: this.id("evt"), created: this.now(), type, data };
    const line = `data: ${JSON.stringify(event)}\n\n`;
    for (const res of this.streams) res.write(line);
  }
  dropStreams() { for (const res of this.streams) res.destroy(); this.streams.clear(); }

  headers() { return { authorization: "Basic " + Buffer.from(`opencode:${this.password}`).toString("base64") }; }

  async listen() {
    this.server = http.createServer((req, res) => this.#handle(req, res));
    await new Promise((resolve) => this.server.listen(0, "127.0.0.1", resolve));
    this.url = `http://127.0.0.1:${this.server.address().port}`;
    return this;
  }
  async close() {
    this.dropStreams();
    await new Promise((resolve) => { this.server.close(resolve); this.server.closeAllConnections?.(); });
  }

  #send(res, status, body) {
    res.writeHead(status, { "content-type": "application/json" });
    res.end(body === undefined ? "" : JSON.stringify(body));
  }

  #info(s) {
    return { id: s.id, ...(s.parentID ? { parentID: s.parentID } : {}), title: "t", location: { directory: s.directory }, time: { created: s.created, updated: s.created } };
  }

  #handle(req, res) {
    const url = new URL(req.url, this.url);
    const route = url.pathname;
    this.requests.push(`${req.method} ${route}`);
    if (req.headers.authorization !== this.headers().authorization) return this.#send(res, 401, { _tag: "UnauthorizedError" });
    if (route === "/api/event") {
      res.writeHead(200, { "content-type": "text/event-stream" });
      res.write(`data: ${JSON.stringify({ id: this.id("evt"), type: "server.connected", data: {} })}\n\n`);
      this.streams.add(res);
      req.on("close", () => this.streams.delete(res));
      return;
    }
    let body = "";
    req.on("data", (c) => (body += c));
    req.on("end", () => {
      let json;
      try { json = body ? JSON.parse(body) : undefined; } catch { json = undefined; }
      this.#route(req.method, route, url, json, req, res);
    });
  }

  #route(method, route, url, json, req, res) {
    if (route === "/api/session/active" && method === "GET") {
      const data = {};
      for (const s of this.sessions.values()) if (s.running) data[s.id] = { type: "running" };
      return this.#send(res, 200, { data });
    }
    if (route === "/api/session" && method === "GET") {
      const limit = Number(url.searchParams.get("limit") ?? 100);
      return this.#send(res, 200, { data: [...this.sessions.values()].reverse().slice(0, limit).map((s) => this.#info(s)) });
    }
    const m = route.match(/^\/api\/session\/([^/]+)(?:\/(message|form|prompt)(?:\/([^/]+?)(?:\/(reply))?)?)?$/);
    if (!m) return this.#send(res, 404, { _tag: "NotFound" });
    const [, sid, sub, subId, action] = m;
    const s = this.sessions.get(sid);
    if (!s) return this.#send(res, 404, { _tag: "SessionNotFoundError" });
    if (!sub && method === "GET") return this.#send(res, 200, { data: this.#info(s) });

    if (sub === "message" && method === "GET") {
      const limit = Number(url.searchParams.get("limit") ?? 1000);
      const cursor = url.searchParams.get("cursor");
      const orderParam = url.searchParams.get("order");
      if (cursor && orderParam) return this.#send(res, 400, { _tag: "InvalidCursorError", message: "Cursor cannot be combined with order" });
      let order = orderParam ?? "asc";
      let offset = 0;
      if (cursor) { const c = JSON.parse(Buffer.from(cursor, "base64url").toString()); order = c.order; offset = c.offset; }
      const ordered = order === "desc" ? s.items.slice().reverse() : s.items.slice();
      const page = ordered.slice(offset, offset + limit);
      const next = Buffer.from(JSON.stringify({ order, offset: offset + limit })).toString("base64url");
      return this.#send(res, 200, { data: page, cursor: { next, previous: next } });
    }

    if (sub === "form" && method === "GET" && !subId) {
      return this.#send(res, 200, { data: [...s.forms.values()].filter((f) => f.state.status === "pending").map(({ state, ...info }) => info) });
    }
    if (sub === "form" && method === "GET") {
      const f = s.forms.get(subId);
      return f ? this.#send(res, 200, { data: f }) : this.#send(res, 404, { _tag: "FormNotFoundError" });
    }
    if (sub === "form" && action === "reply" && method === "POST") {
      if (this.failNext === "reply") { this.failNext = null; return req.socket.destroy(); }
      const f = s.forms.get(subId);
      if (!f) return this.#send(res, 404, { _tag: "FormNotFoundError" });
      if (f.state.status !== "pending") return this.#send(res, 409, { _tag: "FormAlreadySettledError" });
      const answer = json?.answer ?? {};
      for (const field of f.fields) if (typeof answer[field.key] !== "string") return this.#send(res, 400, { _tag: "FormInvalidAnswerError" });
      f.state = { status: "answered", answer };
      this.replies.push({ sessionID: sid, formID: subId, answer });
      if ([...s.forms.values()].every((x) => x.state.status !== "pending")) s.running = true; // The turn resumes.
      this.emit("form.replied", { id: subId, sessionID: sid, answer });
      return this.#send(res, 204);
    }
    if (sub === "prompt" && method === "POST") {
      if (this.failNext === "prompt") { this.failNext = null; return req.socket.destroy(); }
      this.prompts.push(json);
      const id = json?.id ?? this.id("msg");
      if (s.prompted.has(id)) return this.#send(res, 200, { data: s.prompted.get(id) }); // Same ID: the original, not a second message.
      if (typeof json?.text !== "string") return this.#send(res, 400, { _tag: "InvalidRequestError" });
      this.addUser(s, json.text, id);
      const item = { id, sessionID: sid, type: "user", payload: { text: json.text }, delivery: s.running ? "steer" : "queue" };
      s.prompted.set(id, item);
      this.emit("session.inbox.enqueued", { inboxID: id, sessionID: sid, item });
      this.start(s);
      return this.#send(res, 200, { data: item });
    }
    return this.#send(res, 404, { _tag: "NotFound" });
  }
}
