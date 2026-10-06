# 0022. Localization

- Status: Accepted
- Date: 2026-10-06

## Context

The pet should speak Vietnamese as well as English, follow the system language by
default, let the user pick one, and switch without a restart. Agent clients run the
hook, which must stay fast and silent, and the CLI and docs are written for English
readers. Translations should not need a download or a separate install.

## Decision

The interface is in English or Vietnamese. See [translations](../i18n.md) for the
workflow and conventions. English is the source text of every `tr()` call, and
`translations/agent-pet_vi.ts` is compiled by `qt_add_lrelease` and embedded as
`:/i18n/agent-pet_vi.qm` in the `pet_i18n` static library. Only `agent-pet` and
`agent-pet-updater` link it. There is no English `.qm`. The `.qm` is about 47 KiB, so
it is not split out the way `artwork.rcc` is.

- **Choice.** `preferences.json` keeps `language`: `auto` (the default), `en` or `vi`.
  An unknown value reads as `auto` without invalidating the file, so a newer version's
  language does not reset the other settings. `i18n::resolve` maps `auto` to the
  first of `QLocale::uiLanguages()` that is Vietnamese or English, which on Linux
  comes from `LANGUAGE`, `LC_ALL`, `LC_MESSAGES` and `LANG`. Anything else falls back
  to English.
- **Install.** `main.cpp` installs the translator before `PetWindow` is built. Runs
  with `--no-persist` and `--smoke-test` skip the saved choice and follow the system.
  The updater helper reads the same preference, so the result line it leaves for the
  pet ("Updated to 0.12.0.") is in the user's language. `check_update.py` checks this
  with `LANG` pinned to `C.UTF-8`.
- **Headless.** The hook, `emit`, `integration` and `autostart` never install a
  translator, and `QCoreApplication::translate` returns the source text without one.
  Shared code in `pet_events` can therefore call `Integrations::tr` and similar. The
  CLI prints English, and the settings dialog shows the same messages translated.
- **Live switch.** `PetWindow::setLanguage` saves the choice and swaps the translator.
  Qt then sends `QEvent::LanguageChange` to every widget. `PetWindow`, `AlertBubble`,
  `NoteBubble` and `SessionList` relabel themselves in `retranslate()`. Menu actions
  are created without text and labeled only there, so there is one copy of each
  label. The settings dialog closes and, on the next event loop turn, reopens at
  the same place and tab: it may be inside its own combo box's signal. Bubble titles,
  tray status and the session list are recomputed from the sessions on every 250 ms
  update, and the update `Controller` builds its texts when asked.
- **Persisted text.** `recap.json` stores English only ("Unknown project" is a
  sentinel translated for display), so switching languages never splits a day's
  counters.
- **Desktop entry.** `agent-pet.desktop` has Vietnamese `GenericName` and `Comment`,
  and the macOS `Info.plist` lists `CFBundleLocalizations` `en` and `vi`, so native
  dialogs follow the app's languages.

## Consequences

- Every user-visible string is wrapped for Qt Linguist, and the committed `.ts` must
  follow the sources: CI runs `update_translations` and `scripts/check_translations.py`.
- Qt LinguistTools is a build requirement of the application, not of the portable core.
- English plurals are written as singular/plural pairs, because there is no English
  `.qm` to resolve `%n`.
- Qt ships no Vietnamese catalog for its own dialogs, so buttons get explicit `tr()`
  text instead of standard labels.
- The preview and About windows keep their language until they are reopened.
- The pet's speech has its own `Pet` context, so a tone setting or generated
  conversation can replace it without touching interface labels.

## Validation

### Localization evidence — 2026-10-06

The Vietnamese translation covers all 302 strings in 14 contexts. With Qt 6.11.2, the
Release build passes all twelve CTest tests (prototype in four shards). Among them, the new `i18n` suite covers
language names, system resolution (vi_VN, en_US, fr_FR, C), installing and removing the
translator across contexts, and the marker check over every finished entry; a
deliberately broken `%1` makes it fail. The prototype test `languagePreference` switches
a running pet to Vietnamese from the settings combo and back. It checks the menu, tray
status, bubble title, the reopened dialog's title, tab and birthday month, and that
the choice persists and an unknown value reads as automatic. `update-install`
(`check_update.py`) checks the rollback result line in Vietnamese. Offscreen
screenshots of the three settings tabs, the menu, the alert bubble, the session list
and two notes in Vietnamese showed no clipped text. `desktop-file-validate` accepts the
localized entry. Still open: lupdate and lrelease from Qt 6.5.3 in CI, a review of the
Vietnamese wording by native speakers, and the macOS bundle with a Vietnamese system
language.
