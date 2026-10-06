#pragma once
#include <QCoreApplication>

// Translation contexts for code without a QObject of its own; each groups related strings for translators.
// Header-only and Qt Core only, so headless libraries use it too: without an installed translator (the CLI,
// the hook, tests) QCoreApplication::translate returns the English source text.
struct Alerts { Q_DECLARE_TR_FUNCTIONS(Alerts) };             // Alert titles, session statuses and rows
struct Pet { Q_DECLARE_TR_FUNCTIONS(Pet) };                   // What the pet says; Vietnamese speaks as "em"
struct Updater { Q_DECLARE_TR_FUNCTIONS(Updater) };           // Update checks, downloads and installation
struct Integrations { Q_DECLARE_TR_FUNCTIONS(Integrations) }; // Agent hook configuration
struct Startup { Q_DECLARE_TR_FUNCTIONS(Startup) };           // Start at login
struct Focus { Q_DECLARE_TR_FUNCTIONS(Focus) };               // Window focus requirements
struct Settings { Q_DECLARE_TR_FUNCTIONS(Settings) };         // Preferences file errors
