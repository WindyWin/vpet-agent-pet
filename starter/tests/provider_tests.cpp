#include "providers/adapters.h"
#include "providers/integrations.h"
#include "sessions/state.h"
#include "ipc/local.h"
#include <QTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>
#include <QTemporaryDir>
#include <QProcess>
#include <QSet>
#include <QDateTime>

class ProviderTests : public QObject {
    Q_OBJECT
    const qint64 now = 1791072000000;
    QJsonObject payload(QString name, QString tool = {}) {
        QJsonObject in{{"session_id", "parent"}, {"cwd", "/projects/example"}, {"hook_event_name", name}};
        if (!tool.isEmpty()) { in["tool_use_id"] = tool; in["tool_name"] = "Read"; }
        return in;
    }
    pet::Event event(QString provider, QJsonObject input, qint64 stamp) {
        pet::Event e; QString error;
        if (!pet::Event::parse(QJsonDocument(pet::normalizeHook(provider, input, stamp)).toJson(), e, error)) QTest::qFail(qPrintable(error), __FILE__, __LINE__);
        return e;
    }
private slots:
    void fixtures() {
        QFile file(PROVIDER_FIXTURE_PATH); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto rows = QJsonDocument::fromJson(file.readAll()).array(); QVERIFY(rows.size() > 20);
        for (const auto &value : rows) {
            const auto row = value.toObject();
            const auto result = pet::normalizeHook(row["provider"].toString(), row["input"].toObject(), now);
            QCOMPARE(result["kind"].toString(), row["kind"].toString());
            QCOMPARE(result["reason"].toString(), row["reason"].toString());
            if (!result.isEmpty()) {
                pet::Event e; QString error;
                QVERIFY2(pet::Event::parse(QJsonDocument(result).toJson(), e, error), qPrintable(error));
                const auto bytes = QJsonDocument(result).toJson();
                QVERIFY(!bytes.contains("PRIVATE")); QVERIFY(!bytes.contains("tool_response"));
            }
        }
    }
    void identityAndValidation() {
        const auto input = payload("PreToolUse", "call");
        auto first = pet::normalizeHook("claude", input, now);
        auto retry = pet::normalizeHook("claude", input, now + 20);
        QCOMPARE(first["event_id"], retry["event_id"]);
        QVERIFY(first["event_id"] != pet::normalizeHook("claude", payload("PostToolUse", "call"), now)["event_id"]);
        QVERIFY(pet::normalizeHook("claude", payload("PreToolUse"), now).isEmpty());
        auto bad = input; bad["session_id"] = 12; QVERIFY(pet::normalizeHook("claude", bad, now).isEmpty());
        bad = input; bad["session_id"] = "bad\nidentity"; QVERIFY(pet::normalizeHook("claude", bad, now).isEmpty());
        QVERIFY(pet::normalizeHook("unknown", input, now).isEmpty());
        auto large = payload("UserPromptSubmit"); large["prompt"] = QString(20000, 'x');
        QVERIFY(!pet::normalizeHook("codex", large, now).isEmpty());
    }
    void destructiveCommands() {
        for (const auto *command : {"rm -rf build", "rm -fr /tmp/x", "rm -r -f x", "rm --recursive --force x", "sudo rm -Rf /",
                                    "cd repo && rm -rf node_modules", "find . -name '*.o' | xargs rm -rf", "echo ok; /bin/rm -rf ~",
                                    "git push --force", "git push -f origin main", "git push origin +main",
                                    "git -C repo push --force-with-lease", "git reset --hard HEAD~3", "git clean -fdx",
                                    "mkfs.ext4 /dev/sdb1", "dd if=image.iso of=/dev/sda bs=4M", "psql -c 'DROP TABLE users'",
                                    "sqlite3 app.db \"truncate table logs\"", "LANG=C rm -rf out", "bash -lc 'rm -rf out'"})
            QVERIFY2(pet::destructiveCommand(command), command);
        for (const auto *command : {"", "rm file.txt", "rm -r build", "rm -f file", "ls -rf", "grep -rf patterns.txt .",
                                    "git push", "git push -u origin feature-fix", "git push --follow-tags",
                                    "git reset --soft HEAD~1", "git clean -n", "git commit -m 'force push later'",
                                    "dd if=/dev/zero of=disk.img", "echo 'select * from dropbox'", "cmake --build build",
                                    "bash -lc 'make test'", "sh scripts/build.sh"})
            QVERIFY2(!pet::destructiveCommand(command), command);
        // The hook flags a tool start; the command itself never leaves it.
        for (const auto &provider : {QString("claude"), QString("codex")}) {
            auto start = payload("PreToolUse", "call"); start["tool_name"] = "Bash";
            start["tool_input"] = QJsonObject{{"command", "rm -rf build-output"}};
            auto result = pet::normalizeHook(provider, start, now);
            QCOMPARE(result["risky"], QJsonValue(true)); QVERIFY(event(provider, start, now).risky);
            const auto bytes = QJsonDocument(result).toJson();
            QVERIFY(!bytes.contains("rm -rf")); QVERIFY(!bytes.contains("build-output"));
            start["tool_input"] = QJsonObject{{"command", QJsonArray{"git", "push", "--force"}}};
            QCOMPARE(pet::normalizeHook(provider, start, now)["risky"], QJsonValue(true));
            start["tool_input"] = QJsonObject{{"command", QJsonArray{"bash", "-lc", "git reset --hard"}}};
            QCOMPARE(pet::normalizeHook(provider, start, now)["risky"], QJsonValue(true));
            start["tool_input"] = QJsonObject{{"command", "cargo test"}};
            QVERIFY(!pet::normalizeHook(provider, start, now).contains("risky"));
            auto end = payload("PostToolUse", "call"); end["tool_input"] = QJsonObject{{"command", "rm -rf build-output"}};
            QVERIFY(!pet::normalizeHook(provider, end, now).contains("risky"));
        }
    }
    void concurrentAndChildren() {
        for (const auto &provider : {QString("claude"), QString("codex")}) {
            pet::Sessions sessions; qint64 stamp = now;
            auto apply = [&](QJsonObject in) { const auto e = event(provider, in, ++stamp); return sessions.apply(e, stamp); };
            QVERIFY(apply(payload("PreToolUse", "a"))); // missed SessionStart
            auto work = payload("PreToolUse", "b"); work["tool_name"] = "Bash";
            QVERIFY(apply(work)); QCOMPARE(sessions.aggregate(stamp), "working");
            QVERIFY(!apply(work)); // duplicate delivery
            QVERIFY(apply(payload("PostToolUse", "a"))); QCOMPARE(sessions.aggregate(stamp), "working");
            auto failure = payload(provider == "claude" ? "PostToolUseFailure" : "PostToolUse", "b");
            failure["tool_response"] = QJsonObject{{"isError", true}};
            QVERIFY(apply(failure)); QCOMPARE(sessions.aggregate(stamp), "error");
            sessions.expire(stamp + 4001); QCOMPARE(sessions.aggregate(stamp), "thinking");
            QVERIFY(sessions.records().first().tools.isEmpty());
            auto child = payload("SubagentStart"); child["agent_id"] = "child";
            QVERIFY(apply(child)); QCOMPARE(sessions.records().size(), 2);
            child["hook_event_name"] = "SubagentStop";
            QVERIFY(apply(child)); QCOMPARE(sessions.records().size(), 1);
            QCOMPARE(sessions.aggregate(stamp), "thinking");
            QVERIFY(sessions.pending().size() == 1); // error only, no child finish alert
            QVERIFY(apply(payload("PermissionRequest"))); QCOMPARE(sessions.aggregate(stamp), "attention");
            QVERIFY(apply(payload("UserPromptSubmit"))); QCOMPARE(sessions.aggregate(stamp), "thinking");
            QVERIFY(apply(payload("Stop"))); QCOMPARE(sessions.aggregate(stamp), "turn-finished");
            QVERIFY(apply(payload("SessionEnd"))); QVERIFY(sessions.records().isEmpty());
            pet::Sessions restarted;
            QVERIFY(restarted.apply(event(provider, payload("PostToolUse", "missed"), ++stamp), stamp));
            QCOMPARE(restarted.aggregate(stamp), "thinking");
        }
    }
    void codexApprovalWithoutPreToolUseClears() {
        // apply_patch and MCP approvals can arrive with no PreToolUse; the tool's PostToolUse is the answer.
        pet::Sessions sessions; qint64 stamp = now;
        auto apply = [&](QJsonObject in) { return sessions.apply(event("codex", in, ++stamp), stamp); };
        QVERIFY(apply(payload("UserPromptSubmit")));
        QVERIFY(apply(payload("PermissionRequest"))); QCOMPARE(sessions.aggregate(stamp), "attention");
        QVERIFY(apply(payload("PostToolUse", "patch"))); QCOMPARE(sessions.aggregate(stamp), "thinking");
    }
#ifdef PET_TEST_NATIVE
    void configurationPreservation() {
        for (const auto &provider : {QString("claude"), QString("codex")}) {
#ifdef PET_TEST_WINDOWS
            const QString executable = "C:/Users/Pet/AgentPet/agent-pet-cli.exe"; // Expressible in Codex's cmd.exe grammar.
#else
            const QString executable = "/tmp/Pet's folder/$(do-not-run)`x`/agent-pet";
#endif
            const QJsonObject foreign{{"type", "command"}, {"command", "echo agent-pet is not ours"}};
            const QJsonObject original{{"permissions", QJsonObject{{"allow", QJsonArray{"Read"}}}},
                {"hooks", QJsonObject{{"Stop", QJsonArray{QJsonObject{{"matcher", "*"}, {"hooks", QJsonArray{foreign}}}}}}}};
            QJsonObject enabled, again, disabled; int owned; QString error;
            QVERIFY2(pet::mergeIntegration(original, provider, executable, true, enabled, owned, error), qPrintable(error)); QCOMPARE(owned, 0);
            QVERIFY(pet::mergeIntegration(enabled, provider, executable, true, again, owned, error));
            QCOMPARE(owned, pet::hookEvents(provider).size()); QCOMPARE(again, enabled);
            QVERIFY(pet::mergeIntegration(enabled, provider, executable, false, disabled, owned, error)); QCOMPARE(disabled, original);
            QVERIFY(pet::mergeIntegration(disabled, provider, executable, false, again, owned, error)); QCOMPARE(disabled, again);
            // An unrelated handler sharing one of our groups survives removal.
            auto hooks = enabled["hooks"].toObject(); auto groups = hooks["PreToolUse"].toArray();
            auto group = groups[0].toObject(); auto handlers = group["hooks"].toArray(); handlers.append(foreign);
            group["hooks"] = handlers; groups[0] = group; hooks["PreToolUse"] = groups; enabled["hooks"] = hooks;
            QVERIFY(pet::mergeIntegration(enabled, provider, executable, false, disabled, owned, error));
            QCOMPARE(disabled["hooks"].toObject()["PreToolUse"].toArray()[0].toObject()["hooks"].toArray(), QJsonArray{foreign});
            QVERIFY(!pet::mergeIntegration(QJsonObject{{"hooks", "bad"}}, provider, executable, true, again, owned, error));
        }
    }
#endif
#ifdef PET_TEST_WINDOWS
    void windowsHookHandlers() {
        // Claude Code spawns exec-form handlers without a shell: nothing is quoted.
        QString error;
        const auto claude = pet::hookHandler("C:/Program Files/Agent Pet (x86)/agent-pet-cli.exe", "claude", error);
        QCOMPARE(claude["command"].toString(), QString("C:\\Program Files\\Agent Pet (x86)\\agent-pet-cli.exe"));
        QCOMPARE(claude["args"].toArray(), (QJsonArray{"hook", "--provider", "claude", "--registration", "agent-pet-v1"}));
        QVERIFY(pet::ownedHookHandler(claude, "claude")); QVERIFY(!pet::ownedHookHandler(claude, "codex"));
        auto extra = claude; extra["args"] = QJsonArray{"hook", "--provider", "claude", "--registration", "agent-pet-v1", "-x"};
        QVERIFY(!pet::ownedHookHandler(extra, "claude"));
        // Codex runs cmd.exe /c, where a quoted program never starts: a path with spaces needs a short name.
        const auto codex = pet::hookHandler("C:/Users/Pet/agent-pet-cli.exe", "codex", error);
        QCOMPARE(codex["command"].toString(), QString("C:\\Users\\Pet\\agent-pet-cli.exe hook --provider codex --registration agent-pet-v1"));
        QVERIFY(pet::ownedHookHandler(codex, "codex")); QVERIFY(!pet::ownedHookHandler(codex, "claude"));
        QVERIFY(pet::hookHandler("C:/No Such Folder/agent-pet-cli.exe", "codex", error).isEmpty()); QVERIFY(!error.isEmpty());
        QVERIFY(!pet::ownedHookHandler({{"type", "command"}, {"command", "\"C:\\x.exe\" hook --provider codex --registration agent-pet-v1"}}, "codex"));
        QVERIFY(!pet::ownedHookHandler({{"type", "command"}, {"command", "x.exe & y hook --provider codex --registration agent-pet-v1"}}, "codex"));
        // Hooks run the console companion; the pet itself is the GUI program.
        QTemporaryDir temp; QVERIFY(temp.isValid());
        for (const auto *name : {"agent-pet.exe", "agent-pet-cli.exe"}) { QFile file(temp.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly)); }
        QCOMPARE(pet::hookExecutable(temp.filePath("agent-pet.exe")), temp.filePath("agent-pet-cli.exe"));
        QCOMPARE(pet::hookExecutable(temp.filePath("agent-pet-cli.exe")), temp.filePath("agent-pet-cli.exe"));
    }
#endif
#ifdef PET_TEST_NATIVE
    void commandAndTransport() {
#ifdef PET_TEST_POSIX
        // Short, so the socket path fits sockaddr_un (104 bytes on macOS) under any TMPDIR.
        QTemporaryDir temp("/tmp/agent-pet-XXXXXX"); QVERIFY(temp.isValid());
#else
        QTemporaryDir temp; QVERIFY(temp.isValid()); // Selects a separate pipe, away from a running pet.
#endif
        const auto previous = qgetenv("XDG_RUNTIME_DIR"); qputenv("XDG_RUNTIME_DIR", temp.path().toUtf8());
        pet::Receiver receiver; QString error; QVERIFY2(receiver.start(error), qPrintable(error));
        QVector<pet::Event> received;
        receiver.received = [&](const pet::Event &e) { received.append(e); };
        const auto config = temp.path() + "/settings.json";
        auto run = [&](QStringList args, QByteArray input = {}) {
            QProcess proc; proc.start(APP_PATH, args);
            if (!proc.waitForStarted()) return QByteArray("FAILED");
            proc.write(input); proc.closeWriteChannel();
            if (!proc.waitForFinished(2000)) { proc.kill(); proc.waitForFinished(); return QByteArray("TIMEOUT"); }
            if (proc.exitCode() != 0) return QByteArray("REJECTED");
            return proc.readAllStandardOutput();
        };
        for (const auto &provider : {QString("claude"), QString("codex")}) {
            auto input = payload("UserPromptSubmit"); input["prompt"] = QString(20000, 'x');
            QCOMPARE(run({"hook", "--provider", provider}, QJsonDocument(input).toJson()), QByteArray());
        }
        QTRY_COMPARE(received.size(), 2);
        // Events queued while the receiver is busy may arrive in either order on Windows, where each
        // waits on its own pipe instance; sessions order them by timestamp.
        QCOMPARE(received[0].kind, "prompt"); QCOMPARE(received[1].kind, "prompt");
        QCOMPARE((QSet<QString>{received[0].provider, received[1].provider}), (QSet<QString>{"claude", "codex"}));
        QCOMPARE(run({"hook", "--provider", "claude"}, "invalid"), QByteArray());
        auto result = run({"integration", "preview", "--provider", "claude", "--config", config});
        QVERIFY(QJsonDocument::fromJson(result).isObject()); QVERIFY(!QFile::exists(config));
        result = run({"integration", "enable", "--provider", "claude", "--config", config});
        QVERIFY(QJsonDocument::fromJson(result).object()["changed"].toBool());
        QFile file(config); QVERIFY(file.open(QIODevice::ReadOnly)); const auto original = file.readAll(); file.close();
        result = run({"integration", "enable", "--provider", "claude", "--config", config});
        QVERIFY(!QJsonDocument::fromJson(result).object()["changed"].toBool());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), original); file.close();
        result = run({"integration", "inspect", "--provider", "claude", "--config", config});
        QCOMPARE(QJsonDocument::fromJson(result).object()["owned_handlers"].toInt(), pet::hookEvents("claude").size());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write("{broken"); file.close();
        QCOMPARE(run({"integration", "disable", "--provider", "claude", "--config", config}), QByteArray("REJECTED"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("{broken")); file.close();
        // Disabling where no client is configured creates neither a file nor its directory.
        result = run({"integration", "disable", "--provider", "codex", "--config", temp.path() + "/absent/hooks.json"});
        QVERIFY(!QJsonDocument::fromJson(result).object()["changed"].toBool()); QVERIFY(!QFile::exists(temp.path() + "/absent"));
#ifdef PET_TEST_POSIX
        // Execute generated shell syntax with spaces, apostrophes and metacharacters.
        const auto link = temp.path() + "/Pet's $(false) `false`";
        QVERIFY(QFile::link(APP_PATH, link));
        const auto command = pet::hookHandler(link, "codex", error)["command"].toString();
        QProcess shell; shell.start("/bin/sh", {"-c", command}); QVERIFY(shell.waitForStarted());
        shell.write(QJsonDocument(payload("SessionStart")).toJson()); shell.closeWriteChannel(); QVERIFY(shell.waitForFinished(2000));
        QCOMPARE(shell.exitCode(), 0); QCOMPARE(shell.readAllStandardOutput(), QByteArray());
        QTRY_COMPARE(received.size(), 3);
#else
        // Run each handler the way its agent does, from a folder with spaces and an apostrophe:
        // Claude Code spawns the exec form directly, Codex runs cmd.exe /e:ON /v:OFF /d /c "<command>".
        QVERIFY(QDir(temp.path()).mkdir("Pet's folder"));
        const auto copy = temp.path() + "/Pet's folder/agent-pet-cli.exe";
        QVERIFY(QFile::copy(APP_PATH, copy));
        auto deliver = [&](const QString &program, const QStringList &arguments) {
            QProcess process; process.start(program, arguments); QVERIFY(process.waitForStarted());
            process.write(QJsonDocument(payload("SessionStart")).toJson()); process.closeWriteChannel(); QVERIFY(process.waitForFinished(5000));
            QCOMPARE(process.exitCode(), 0); QCOMPARE(process.readAllStandardOutput(), QByteArray());
        };
        const auto claude = pet::hookHandler(copy, "claude", error);
        QStringList arguments;
        for (const auto &argument : claude["args"].toArray()) arguments << argument.toString();
        deliver(claude["command"].toString(), arguments);
        QTRY_COMPARE(received.size(), 3);
        auto codex = pet::hookHandler(copy, "codex", error);
        if (codex.isEmpty()) { // This volume keeps no 8.3 names; such a folder is refused, a plain one works.
            QVERIFY(!error.isEmpty());
            QVERIFY(QFile::copy(APP_PATH, temp.path() + "/plain-agent-pet-cli.exe"));
            codex = pet::hookHandler(temp.path() + "/plain-agent-pet-cli.exe", "codex", error);
        }
        deliver("cmd.exe", {"/e:ON", "/v:OFF", "/d", "/c", codex["command"].toString()});
        QTRY_COMPARE(received.size(), 4);
#endif
        if (previous.isNull()) qunsetenv("XDG_RUNTIME_DIR"); else qputenv("XDG_RUNTIME_DIR", previous);
    }
#endif
};
QTEST_GUILESS_MAIN(ProviderTests)
#include "provider_tests.moc"
