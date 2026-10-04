# Agent Pet design target

Scope revised 2026-10-04: a floating animated pet with compact notifications and minimal settings. The complete interface consists of the pet, one alert bubble, an attention badge, and a context/tray menu.

## Current visual target

[Pet-only desktop design](agent-pet-desktop.png), generated with the built-in image tool using a bundled character sprite as reference. The image shows fictional alert data and an open context menu for review; the menu is normally hidden until invoked. It is a visual target, not a running application screenshot.

A small draggable pet stays on the desktop. A one-line toast beside it names the event and project; provider, short session ID and path are in its tooltip:

> ● Needs approval  fcis-web  +2  Open  ×

Other alert reasons include needs input, tool error, and turn finished. A finished turn does not establish task success. Clicking the toast or Open brings the session's terminal/editor forward, where users respond. Errors and finished turns fade on their own; requests stay. Clicking the pet (or +N) shows a small list of running sessions with status and host; clicking a row goes there.

Show one toast at a time with a bounded pending queue, duplicate aggregation, a pending count, and open/dismiss controls. Dismissal hides the alert without resolving its underlying request. Retain a small attention badge while observed requests remain unresolved. Provide mute and optional sound.

The pet's context/tray menu exposes size, position, notification preferences, integration setup and coverage, attribution, and quit. Closing settings keeps monitoring active; quitting stops it. Use bundled sprites for the pet.

## Internal monitoring

Track concurrent sessions through enabled integrations in the current user's local environment, independently of their supported terminal/editor hosts. The running-sessions list is read-only: it shows state and navigates to the host, without managing sessions. Prioritize approval/input/error attention, then completion, then ordinary working/idle animation.

Existing sessions can be observed on their next supported event; integration setup may require restarting a client. Silent sessions and earlier activity are not automatically discoverable. Show coverage in integration settings. Keep bounded state in memory; after restart, wait for fresh events without replaying old alerts. Persist preferences and integration configuration only.

## Validation

Prototype Linux transparency, dragging, animation-cache limits, compact alert placement near screen edges, concurrent-session alert identity, queue limits, duplicate events, dismissal, mute, and restart behavior. Verify provider/version event coverage before claiming support.

## Design assets

The pet-only design is the current implementation reference.

See the [generation prompt](agent-pet-desktop.prompt.txt). Character reference: `starter/assets/vpet/vup/Say/Serious/B/说话啊吧啊吧_000_125.png`. Existing artwork attribution and terms still apply; implementation uses the bundled original sprites rather than extracting artwork from the mockup.
