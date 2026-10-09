const HOST = "vn.agent_pet.chrome_downloads";
// Serialize state updates: download creation and completion can arrive together.
let queue = Promise.resolve();
function enqueue(task) {
  queue = queue.then(task).catch(error => console.error("Download tracker:", error.message));
}

async function send(event) {
  try {
    const reply = await chrome.runtime.sendNativeMessage(HOST, {version: 1, event});
    await chrome.storage.local.set({bridge: {
      ok: reply.ok === true, message: reply.ok ? "Connected to Agent Pet" : reply.error,
      updated: Date.now()
    }});
  } catch (error) {
    await chrome.storage.local.set({bridge: {ok: false, message: error.message, updated: Date.now()}});
  }
}

async function updateBadge() {
  const items = await chrome.downloads.search({state: "in_progress"});
  const count = items.filter(item => !item.incognito).length;
  await chrome.action.setBadgeBackgroundColor({color: "#5265d9"});
  await chrome.action.setBadgeText({text: count ? String(count) : ""});
}

chrome.downloads.onCreated.addListener(item => enqueue(async () => {
  if (!item.incognito) await send("download_started");
  await updateBadge();
}));
chrome.downloads.onChanged.addListener(delta => enqueue(async () => {
  // Chrome does not issue onChanged for bytesReceived; the popup polls search.
  if (delta.state) {
    const [item] = await chrome.downloads.search({id: delta.id});
    if (item && !item.incognito) {
      const event = {complete: "download_completed", interrupted: "download_interrupted"}[delta.state.current];
      if (event) await send(event);
    }
  }
  await updateBadge();
}));
chrome.downloads.onErased.addListener(() => enqueue(updateBadge));
chrome.runtime.onStartup.addListener(() => enqueue(updateBadge));
chrome.runtime.onInstalled.addListener(() => enqueue(updateBadge));
chrome.runtime.onMessage.addListener((message, sender, respond) => {
  if (sender.id === chrome.runtime.id && message?.type === "check-connection") {
    enqueue(async () => {await send("ping"); respond({done: true});});
    return true;
  }
});
