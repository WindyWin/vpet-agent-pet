#pragma once
// The Claude Code interaction bridge (docs/adr/0049-claude-interaction-hooks.md, docs/interactions.md).
// Claude Code has no local API for a running terminal session, so this works through an opt-in set of hooks:
//   - registration:  which hook entries go into settings.json (claude_registration.cpp, Qt Core only);
//   - hook runtime:  what each hook invocation reads, prints and returns (claude_hook.cpp);
//   - session host:  one detached process per Claude session that owns the interaction stream connection
//                    (claude_host.cpp), so short-lived hooks never fight over a conversation.
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace pet::claude {

constexpr auto interactionRegistration = "agent-pet-interact-v1";
constexpr int defaultQuestionWaitSeconds = 20;
constexpr int maxQuestionWaitSeconds = 120;

// ---- Registration -------------------------------------------------------------------------------------------------

// SessionStart, UserPromptSubmit, PreToolUse (AskUserQuestion), Stop, SessionEnd.
QStringList interactionEvents();
// The settings.json handler for one event. POSIX shell grammar only; empty with `error` set elsewhere.
QJsonObject interactionHandler(const QString &executable, const QString &event, int waitSeconds);
// Whether a handler is exactly one interactionHandler() emits (any executable, any wait). Never claims the
// content-free status hooks (`hook --provider claude --registration agent-pet-v1`), nor the reverse.
bool ownedInteractionHandler(const QJsonObject &handler);
// Adds (enable) or removes (always first) only this registration's entries; everything else is preserved.
bool mergeInteractions(const QJsonObject &input, const QString &executable, bool enable, int waitSeconds,
                       QJsonObject &output, int &owned, QString &error);

// ---- Hook runtime -------------------------------------------------------------------------------------------------

// What a hook invocation produced: the bytes for its standard streams and its exit code.
struct HookResult {
    int exitCode = 0;
    QByteArray out, err;
};

// The connection from one hook invocation to its session's host: newline-delimited JSON objects.
class Relay {
public:
    virtual ~Relay() = default;
    virtual bool send(const QJsonObject &message) = 0;
    // Waits up to timeoutMs for the next object; false on timeout or when the host is gone.
    virtual bool receive(QJsonObject &message, int timeoutMs) = 0;
};

// The question a PreToolUse hook may answer: one single-select question.
struct Question {
    QString toolUseId, text;
    QStringList labels;
    QJsonObject toolInput; // Returned whole in updatedInput, plus the answer.
};
bool parseQuestion(const QJsonObject &hookInput, Question &question);
// Exactly the JSON Claude Code expects from a PreToolUse hook that answers AskUserQuestion.
QByteArray questionAnswerJson(const Question &question, const QString &answer);
// What the woken model reads: the user's words, labelled as coming from the pet.
QString wakeText(const QString &followUp);
// Claude Code runs UserPromptSubmit for text it injects itself (a hook's wake feedback, background task notices): those are not
// something the user typed, so they never become messages.
bool injectedPrompt(const QString &prompt);
// A prompt or reply shortened with an ellipsis to fit a relay line and a message.
QString clipped(const QString &text);

// One hook invocation. `deliver` receives the result before the host is told it was delivered, so an answer or a
// wake has really been written when the host reports it accepted. Returns the same result.
HookResult handleHook(const QString &event, int waitMs, const QJsonObject &input, Relay &relay,
                      const std::function<void(const HookResult &)> &deliver);

// `agent-pet claude-interact --event E [--wait N] --registration …` (a hook) and `claude-interact host …`.
// Always 0 on every failure path and silent, except a follow-up wake (2, text on stderr).
int interactCommand(const QStringList &args);

}
