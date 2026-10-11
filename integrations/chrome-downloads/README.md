# Chrome download tracker

An unpacked Manifest V3 Chrome extension tracks active downloads and recent download history.
Its popup shows filenames, bytes received, known-size progress, paused state and interruption reasons.
The toolbar badge counts active downloads (including paused downloads). A native messaging host
forwards download lifecycle events to Agent Pet's existing custom-event channel. This integration
requires Python 3 and an Agent Pet build with plugin custom-event support (0.15.0 or newer).
The host installer supports Linux and macOS; Windows native host registration is not implemented yet.

## Setup

1. Open `chrome://extensions`, enable **Developer mode**, choose **Load unpacked**, and select
   the `extension` directory beside this README. Copy the extension's ID.
2. Run the installer, substituting that ID and the full path to your installed executable:

   ```bash
   python3 native/install.py --extension-id YOUR_EXTENSION_ID --agent-pet /full/path/to/agent-pet
   ```

   On macOS the executable is normally `/Applications/Agent Pet.app/Contents/MacOS/agent-pet`;
   quote paths containing spaces. For Chromium add `--browser chromium`.
   Custom installations can override `--manifest-dir`, `--install-dir` and `--plugins-dir`.
3. In Agent Pet **Settings → Plugins**, enable **Chrome Downloads** and restart the pet.
   The supplied pack targets VPet. To use another pet, copy the pack and set `plugin.json`'s `pet`
   to that pet's ID; check that its catalog supplies the `celebrate` reaction cue.
4. Open the extension popup and click **Check connection** with Agent Pet running.
5. Download a file. Watch progress in the popup; the pet remarks on starts and interruptions and
   celebrates completions when its behavior rules allow it.

## Behavior and privacy

Only fixed event names (`download_started`, `download_completed`, `download_interrupted`) cross
the native bridge. Filenames, URLs, paths, download IDs and file contents are never sent to the pet.
The popup reads download metadata directly from Chrome and does not save it. Only the latest bridge
connection result is saved in extension-local storage. Incognito downloads are skipped.
There are no content scripts, page access permissions, network services, or download modifications.

Progress is queried once per second while the popup is open because Chrome's `onChanged` event
does not report received-byte changes. Download events wake the service worker even when the popup
is closed. Downloads already active when the extension is loaded appear in the popup and badge;
their past starts are not replayed. Failed bridge deliveries are reported in the popup and are not
retried or replayed. **Check connection** sends an unmatched custom event to verify delivery to the
running app; success acknowledges transport delivery, not that the pack is enabled or a reaction played.

The pet's custom-event cooldown, global reaction limit, mute setting and attention rules apply:
the pack does not create agent sessions or keep the pet in a working state for the whole download.
Cancellation is an interruption. Popup completion history comes from Chrome's download history.

The installer copies the host into per-user app data, so moving this source tree does not break it.
Moving the installed Agent Pet executable or changing the unpacked extension's ID requires rerunning
the installer. It copies three pack files and does not change preferences automatically.
To remove the integration, remove the extension in Chrome, disable the pack in Agent Pet, and remove
the installed native host manifest, the `agent-pet/chrome-downloads` host directory and the
`agent-pet/plugins/chrome-downloads` pack directory printed by the installer.

## Verification

```bash
node --test tests/extension.test.mjs
python3 -m unittest discover -s tests -p 'test_*.py'
```

Browser smoke check: use a large file to verify live progress, pause/resume it in Chrome's downloads
page, cancel another download, and complete two concurrent downloads. Close the popup while a
download completes to verify the native event still fires. Stop Agent Pet and use **Check connection**
to verify the failure status. Restart Chrome mid-download to check badge and popup recovery.

API references: [Chrome downloads](https://developer.chrome.com/docs/extensions/reference/api/downloads)
and [native messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging).
