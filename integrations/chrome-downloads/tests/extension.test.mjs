import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import test from "node:test";
import vm from "node:vm";

const code = readFileSync(new URL("../extension/background.js", import.meta.url), "utf8");
function harness() {
  const listeners = {};
  const event = name => ({addListener: fn => listeners[name] = fn});
  const sent = [];
  const local = {};
  let items = [], badge = "", failure = false;
  const chrome = {
    downloads: {
      onCreated: event("created"), onChanged: event("changed"), onErased: event("erased"),
      search: async query => items.filter(item => query.id !== undefined ? item.id === query.id : item.state === query.state)
    },
    action: {setBadgeText: async value => badge = value.text, setBadgeBackgroundColor: async () => {}},
    storage: {local: {set: async value => Object.assign(local, value)}},
    runtime: {
      id: "test", onStartup: event("startup"), onInstalled: event("installed"), onMessage: event("message"),
      sendNativeMessage: async (host, message) => {
        if (failure) throw new Error("Host missing");
        sent.push({host, message}); return {ok: true};
      }
    }
  };
  const context = vm.createContext({chrome, console});
  vm.runInContext(code, context);
  return {listeners, sent, local, setItems: value => items = value, fail: () => failure = true,
    badge: () => badge, drain: () => vm.runInContext("queue", context)};
}

test("concurrent lifecycle events send only fixed names and maintain active badge", async () => {
  const h = harness();
  h.setItems([{id: 1, state: "in_progress", filename: "/private/file.zip", url: "https://secret"}]);
  h.listeners.created({id: 1}); await h.drain();
  assert.equal(h.badge(), "1");
  h.setItems([{id: 1, state: "complete"}, {id: 2, state: "interrupted"}]);
  h.listeners.changed({id: 1, state: {current: "complete"}});
  h.listeners.changed({id: 2, state: {current: "interrupted"}});
  await h.drain();
  assert.deepEqual(h.sent.map(x => JSON.parse(JSON.stringify(x.message))), [
    {version: 1, event: "download_started"}, {version: 1, event: "download_completed"},
    {version: 1, event: "download_interrupted"}
  ]);
  assert.equal(h.badge(), "");
});
test("incognito, pause and metadata changes do not emit lifecycle events", async () => {
  const h = harness();
  h.setItems([{id: 1, state: "complete", incognito: true}]);
  h.listeners.created({id: 1, incognito: true});
  h.listeners.changed({id: 1, state: {current: "complete"}});
  h.listeners.changed({id: 2, paused: {current: true}});
  await h.drain();
  assert.equal(h.sent.length, 0);
});
test("worker restart receives completion without relying on in-memory download state", async () => {
  const h = harness();
  h.setItems([{id: 9, state: "complete"}, {id: 10, state: "in_progress"}]);
  h.listeners.startup();
  h.listeners.changed({id: 9, state: {current: "complete"}});
  await h.drain();
  assert.equal(h.sent[0].message.event, "download_completed");
  assert.equal(h.badge(), "1");
});
test("missing host reports failure and later events still run", async () => {
  const h = harness(); h.fail();
  h.listeners.created({id: 1}); h.listeners.created({id: 2});
  await h.drain();
  assert.equal(h.local.bridge.ok, false);
  assert.equal(h.local.bridge.message, "Host missing");
});
test("connection check responds asynchronously and sends a ping", async () => {
  const h = harness(); let response;
  assert.equal(h.listeners.message({type: "check-connection"}, {id: "test"}, reply => response = reply), true);
  await h.drain();
  assert.equal(h.sent[0].message.event, "ping");
  assert.equal(response.done, true);
});
