# Plugin packs

A plugin pack adds animations to an installed pet without rebuilding the app: new states drawn from its
own PNG frames, more ways to play the pet's reactions and fidgets, and, when it says so explicitly,
replacements for the pet's own states and cue mappings. Packs hold only data (JSON and PNG). They never
run code, and the app loads them only when the user turns them on.

A pack extends one pet. To make a new character instead, see [the pet guide](pets.md). A pack plays
through the pet's existing cues (celebrations, snacks, reminders, fidgets and so on), and its
[`events.json`](#eventsjson) can also react to events from scripts, such as `deploy_succeeded` from CI,
or to the agent's own (a finished turn).

## Installing and turning on

Each pack is a folder named by its id in the per-user plugins folder:

| Platform | Folder |
| --- | --- |
| Linux | `~/.local/share/agent-pet/plugins/<id>/` |
| macOS | `~/Library/Application Support/agent-pet/plugins/<id>/` |
| Windows | `%APPDATA%\agent-pet\plugins\<id>\` |

Settings → **Plugins** lists every pack in that folder, with its version, author, license and status.
**Open plugins folder** opens it. Check a pack to turn it on. Packs load when Agent Pet starts, so a
change applies on the next start. Each line says what the next start will do. The enabled ids are kept
in `preferences.json` under `plugins`.

A pack that fails validation is left out and the pet starts without it. Settings shows the reason
(*Not loaded: …* or *Cannot be used: …*), and so does the log (`Plugin <id> is not loaded: …`).

## A minimal pack

`tests/fixtures/plugins/example/` is a complete pack for VPet, and the `plugins` test suite checks that
it still loads:

```text
example/
  plugin.json
  animations.json
  events.json            (optional)
  frames/heart/0.png … 3.png
```

`plugin.json`:

```json
{
  "schema_version": 1,
  "id": "example",
  "name": "Example pack",
  "version": "1.0.0",
  "author": "Agent Pet contributors",
  "license": "Apache-2.0",
  "pet": "vpet"
}
```

`animations.json` adds one state, `example.heart`, a one-shot that returns to idle, and offers it as a
celebration and as an idle fidget:

```json
{
  "schema_version": 2,
  "sequences": [
    { "path": "heart", "duration_ms": 600, "frames": [
      { "path": "frames/heart/0.png", "duration_ms": 150 },
      { "path": "frames/heart/1.png", "duration_ms": 150 },
      { "path": "frames/heart/2.png", "duration_ms": 150 },
      { "path": "frames/heart/3.png", "duration_ms": 150 }
    ] }
  ],
  "states": { "example.heart": ["heart"] },
  "playback": { "example.heart": { "mode": "once", "after": "idle" } },
  "cues": { "celebrate": [{ "state": "example.heart", "weight": 1 }] },
  "ambient": { "fidgets": [{ "state": "example.heart", "weight": 1 }] }
}
```

Its `events.json` makes the heart play, with a remark, when a script runs
`agent-pet emit --custom deploy_succeeded`, and gives the pet a remark alone for `tests_failed`:

```json
{
  "schema_version": 1,
  "rules": [
    { "on": "custom:deploy_succeeded", "state": "example.heart", "say": "Shipped!", "cooldown_ms": 10000 },
    { "on": "custom:tests_failed", "say": "Oh no, the tests failed." }
  ]
}
```

Copy the folder into the plugins folder, turn it on in Settings, restart, and the heart sometimes plays
when a turn finishes or while the pet idles. The developer animation preview (`--preview`) lists the
new state too.

## plugin.json

| Key | Required | Rule |
| --- | --- | --- |
| `schema_version` | yes | `1` |
| `id` | yes | `[a-z0-9-]{1,32}`, the same as the folder name |
| `name` | yes | Up to 64 characters, shown in Settings |
| `version` | yes | Up to 32 of `0-9 A-Z a-z . + -`, such as `1.0.0` |
| `author` | yes | Up to 128 characters |
| `license` | yes | Up to 64 characters, such as an SPDX id. The pack's art is under these terms, not the app's |
| `pet` | yes | The id of the pet the pack extends, such as `vpet`. The pack loads only while that pet runs |
| `url` | no | An `https://` link, shown in the pack's tooltip |
| `min_app_version` | no | The oldest Agent Pet version the pack works with, such as `0.16.0` |

Any other key makes the file invalid, so a typo is reported instead of silently ignored.

## animations.json

A fragment of the [catalog format](pets.md) with `schema_version` 2. A pack may use only these sections:
`sequences`, `states`, `playback`, `variants`, `moods`, `cues`, `ambient` (only `fidgets`) and
`overrides`. Touch regions, moves, activity decoration, `asset_root` and the sleep delay depend too much
on the pet's own geometry and timing, so a pack cannot set them.

Each enabled pack is merged into the pet's catalog in id order, and the result goes through the same
validation as a pet's own catalog, including the [cue contract](pets.md#cues). A pack that breaks
anything is left out as a whole. The pet and every other pack load as if it were not installed.

### Names

- **Sequences** are the pack's own. `path` is a name local to the pack, and frame paths are relative to
  the pack folder. States, variants and mood art in the pack name only the pack's own sequences, by those
  local names. A pack can never use or shadow the pet's sequences or another pack's.
- **New states** are named `<id>.<name>`, such as `example.heart` (`<name>` is `[a-z0-9_-]{1,64}`). So two
  packs can never define the same state.
- Reactions, fidgets and cue mappings may name any state: the pack's own or the pet's.

### Adding

These need no declaration:

- **New states**, each with a `playback` entry, exactly as in a pet's catalog.
- **`variants`** for any state, the pet's or the pack's. They join the state's existing choices. Each
  variant has the same number of phases as the state.
- **`moods`** art (`happy` or `poor`) for a state that has none yet.
- **Reaction cues** (`celebrate`, `snack`, `danger`, `late-night`, …): the pack's entries join the
  pet's pool for that cue, or start one where the pet has none. A reaction must end by itself and return
  to idle (`once`, or `phased` with `loops`, and `after: idle`).
- **`ambient.fidgets`**: more one-shots the idle pet may play.

### Replacing

Changing anything the pet already has needs an entry in `overrides`:

| Override | Lets the pack |
| --- | --- |
| `states.<state>` | Redefine one of the pet's states (`states` and `playback`). Its variants and mood art go with it, since they must match its shape. |
| `cues.<cue>` | Map a state cue (`thinking`, `turn-finished`, …) to another state, or replace a reaction cue's whole pool instead of joining it |
| `moods.<mood>.<state>` | Replace the pet's mood art for that state |

```json
{
  "schema_version": 2,
  "overrides": ["cues.turn-finished"],
  "cues": { "turn-finished": "example.heart" }
}
```

An override that the pack does not use is an error (most likely a typo). Two enabled packs cannot
replace the same thing: the second one in id order is left out and names the first. Replacing must
still meet the cue contract, so a state cue keeps its playback shape: `turn-finished` stays a `once`
state that returns to idle.

## events.json

Optional rules that make the pet react to something that happened. A reaction is a *surprise* for the
[behavior runtime](adr/0031-behavior-runtime.md), like the danger startle or the Konami code: it plays when the
pet is free, and it is dropped, never queued, while a session needs the user (attention, out of quota,
a failed turn), while the pet is held or hidden, or while another surprise plays. A pack cannot make its
reaction urgent, and nothing in `events.json` changes what the sessions show.

```json
{
  "schema_version": 1,
  "rules": [
    { "on": "custom:deploy_succeeded", "state": "example.heart", "say": "Shipped!", "weight": 2, "cooldown_ms": 30000 }
  ]
}
```

| Key | Required | Rule |
| --- | --- | --- |
| `on` | yes | What to react to: `custom:<name>`, a [custom event](events.md#custom-events) sent with `agent-pet emit --custom <name>` (`[a-z0-9_-]{1,64}`), or an agent event: `session_start`, `prompt`, `attention`, `error`, `turn_finished`, `turn_failed`, `interrupt` or `session_end` |
| `state` | one of `state` and `say` | A state to play, the pack's own or the pet's. It must end by itself and return to idle, like a reaction pool's entries (`once`, or `phased` with `loops`, and `after: idle`) |
| `say` | one of `state` and `say` | A remark for the speech bubble, 1–120 characters. It is plain text in the pack's own language, so it is not translated, and it is not shown while the pet is muted |
| `weight` | no | 1–1000, default 1. When several rules (from any packs) match one event, one is drawn by weight |
| `cooldown_ms` | no | A whole number, 1,000–3,600,000, default 10,000. After a reaction plays, the same `on` rests this long |

Up to 128 rules and 64 KiB per pack, and any other key is an error. The rules are checked against the
merged catalog when the pack loads, so a state that does not exist or does not end is reported in Settings and the
log, and the whole pack is left out, rules and animations alike.

- **Agent events** match after the sessions accept them. A duplicate or stale event does not react, a
  `turn_finished` with background work still running (`waiting`) is not a finish, and tool events cannot be
  matched.
- **Custom events** are not sessions and never match agent rules. An event whose time is more than a minute
  from the pet's clock is ignored.
- **Rate limits** keep a noisy script from holding the pet in reactions: each `on` rests for its
  `cooldown_ms` after it reacted, at most 12 reactions play a minute, and a reaction that was held off
  does not start the rest.
- A toast through the tray, sound and rules that depend on conditions (the project, the day) are not
  supported yet.

## Limits

| Limit | Value |
| --- | --- |
| `plugin.json` | 64 KiB |
| `animations.json` | 1 MiB |
| `events.json` | 64 KiB, 128 rules |
| Frames per pack | 2,000 (1,000 per sequence) |
| One frame | 4 MiB, at most 2,048 pixels on a side |
| All frames of a pack | 64 MiB |
| Frame duration | 1–60,000 ms |
| Packs | 32 folders scanned, 32 enabled |

Frames must be PNG files that resolve inside the pack folder, so a symbolic link that leads out of it
counts as missing. The pack folder itself may be a link, for example to a working copy. Only each
frame's header is read at startup; frames are decoded when they play, like the pet's own.

## Licensing

A pack carries its own art under its own terms, named by `license` and shown in Settings. Pack art is
not part of the app or of VPet's art, and VPet's [artwork terms](../licenses/VPET-ARTWORK-TERMS.md) stay unchanged. Include
the license text in the pack folder if its terms ask for it.
