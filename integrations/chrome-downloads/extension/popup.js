const list = document.querySelector("#downloads");
const status = document.querySelector("#connection");
const bytes = value => value < 0 ? "Unknown size" : `${(value / 1048576).toFixed(1)} MB`;
async function refresh() {
  try {
    const [active, recent, {bridge}] = await Promise.all([
      chrome.downloads.search({state: "in_progress", orderBy: ["-startTime"]}),
      chrome.downloads.search({limit: 20, orderBy: ["-startTime"]}),
      chrome.storage.local.get("bridge")
    ]);
    status.textContent = bridge?.message || "Use Check connection to connect your pet.";
    const items = [...new Map([...active, ...recent].filter(item => !item.incognito).map(item => [item.id, item])).values()];
    list.replaceChildren();
    for (const item of items) {
      const row = document.querySelector("#download").content.cloneNode(true);
      row.querySelector("strong").textContent = item.filename.split(/[\\/]/).pop() || "Preparing download…";
      const state = item.paused ? "Paused" : {in_progress: "Downloading", complete: "Complete", interrupted: "Interrupted"}[item.state];
      row.querySelector("p").textContent = `${state} · ${bytes(item.bytesReceived)} / ${bytes(item.totalBytes)}${item.error ? ` · ${item.error}` : ""}`;
      const progress = row.querySelector("progress");
      if (item.state === "complete") progress.value = 100;
      else if (item.totalBytes > 0) progress.value = Math.min(100, item.bytesReceived / item.totalBytes * 100);
      list.append(row);
    }
    if (!items.length) list.textContent = "No downloads yet.";
  } catch (error) { status.textContent = error.message; }
}
document.querySelector("#check").addEventListener("click", async () => {
  try {await chrome.runtime.sendMessage({type: "check-connection"});}
  catch (error) {status.textContent = error.message;}
  await refresh();
});
await refresh();
// Poll only while this popup is open. No permanently running worker is needed.
setInterval(refresh, 1000);
