// The desktop side of agent conversations: the collapsed preview, the expanded panel and the coordinator that
// decides what to show. Everything runs against the real service with a scriptable fake bridge, offscreen.
#include "desktop/conversations.h"
#include "desktop/conversation_text.h"
#include "desktop/pet_window.h"
#include "fake_bridge.h"
#include <QApplication>
#include <QMenu>
#include <QRadioButton>
#include <QStyle>
#include <QStyleOptionButton>
#include <QSignalSpy>
#include <QTest>

using namespace pet;
using namespace pet::interactions;
using namespace pet::interactions::testing;

struct Rig {
    Service service;
    FakeBridge bridge{service, 1};
    ConversationKey a = key("session-aaaa"), b = key("session-bbbb");
    Rig() { bridge.connect(); bridge.hello({a}); }
    const Conversation &conv(const ConversationKey &k) const { return *service.find(k); }
};

class ConversationTests : public QObject {
    Q_OBJECT
private slots:
    // ---- Panel ---------------------------------------------------------------------------------

    void panelShowsIdentityMessagesAndQuestion() {
        Rig rig;
        rig.bridge.message(rig.a, 1, "m1", "I updated the handler.");
        rig.bridge.request(rig.a, "q1", 1, "Add tests before continuing?", {"tests", "skip"}, true);
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        QCOMPARE(panel.heading(), QString("api-web · Fake · aaaa"));
        QCOMPARE(panel.status(), QString("Connected · Waiting for your answer"));
        QCOMPARE(panel.messageCount(), 1);
        QVERIFY(panel.requestShown());
        QCOMPARE(panel.question(), QString("Add tests before continuing?"));
        QCOMPARE(panel.optionCount(), 2);
        QVERIFY(panel.customOption());
        QVERIFY(!panel.sendEnabled()); // Nothing chosen yet.
    }

    void choosingAnOptionDoesNotSendUntilSendIsPressed() {
        Rig rig;
        rig.bridge.request(rig.a, "q1", 1, "Pick", {"x", "y"});
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        panel.chooseOption(1);
        QVERIFY(rig.bridge.commands.isEmpty());
        QVERIFY(panel.sendEnabled());
        panel.send();
        QCOMPARE(rig.bridge.commands.size(), 1);
        const auto &sent = rig.bridge.commands[0];
        QCOMPARE(sent.action, Action::AnswerRequest);
        QCOMPARE(sent.optionId, QString("y"));
        QCOMPARE(sent.requestRevision, 1);
        QCOMPARE(sent.source, QString("desktop"));
        QCOMPARE(panel.feedback(), QString("Sending…"));
        QVERIFY(!panel.sendEnabled()); // No second submission while one is out.
        panel.send();
        QCOMPARE(rig.bridge.commands.size(), 1);
        rig.bridge.accept(sent);
        QCOMPARE(panel.feedback(), QString("Sent"));
        QCOMPARE(rig.conv(rig.a).requests.value("q1").state, RequestState::Answered);
        QVERIFY(!panel.requestShown());
    }

    void aCustomAnswerNeedsItsOptionAndText() {
        Rig rig;
        rig.bridge.request(rig.a, "q1", 1, "Pick", {"x"}, true);
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        panel.chooseCustom();
        QVERIFY(panel.composerEnabled());
        QVERIFY(!panel.sendEnabled());
        panel.typeText("  something else  ");
        QVERIFY(panel.sendEnabled());
        panel.send();
        QCOMPARE(rig.bridge.commands[0].text, QString("something else"));
        QVERIFY(rig.bridge.commands[0].optionId.isEmpty());
    }

    void aFreeTextQuestionShowsJustTheField() {
        Rig rig;
        rig.bridge.request(rig.a, "q1", 1, "What name?", {}, true);
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        QCOMPARE(panel.optionCount(), 0);
        QVERIFY(!panel.customOption());
        panel.typeText("Rex");
        panel.send();
        QCOMPARE(rig.bridge.commands[0].text, QString("Rex"));
    }

    void rejectionKeepsTheTextAndSaysWhy() {
        Rig rig;
        rig.bridge.request(rig.a, "q1", 1, "Pick", {}, true);
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        panel.typeText("my answer");
        panel.send();
        rig.bridge.result(rig.bridge.commands[0].commandId, "rejected", "invalid_answer", true);
        QCOMPARE(panel.feedback(), QString("The agent did not accept that answer. Edit it and try again."));
        QCOMPARE(panel.composerText(), QString("my answer"));
        QVERIFY(panel.sendEnabled()); // Still answerable: correct it and retry.
    }

    // The result belongs to the conversation the command went to, not to whichever one the panel shows when it arrives.
    void aSendStaysInFlightWhileTheUserLooksAtAnotherConversation() {
        Rig rig;
        rig.bridge.open(rig.b);
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        panel.typeText("run the tests");
        panel.send();
        QCOMPARE(rig.bridge.commands.size(), 1);
        panel.open(rig.b);
        QVERIFY(panel.composerText().isEmpty());
        panel.open(rig.a);
        QVERIFY(!panel.sendEnabled()); // Still waiting for the bridge: a second click would duplicate the follow-up.
        panel.open(rig.b);
        rig.bridge.accept(rig.bridge.commands[0]); // Accepted while the other conversation is on screen.
        panel.open(rig.a);
        QCOMPARE(panel.composerText(), QString());
        QCOMPARE(rig.service.draft(rig.a, QString()), QString()); // What was sent is not kept as a draft.
        QCOMPARE(panel.feedback(), QString());
        QCOMPARE(rig.bridge.commands.size(), 1);
    }

    void unknownDeliveryIsNotSuccessAndBlocksAResend() {
        Rig rig;
        rig.bridge.request(rig.a, "q1", 1, "Pick", {}, true);
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        panel.typeText("answer");
        panel.send();
        rig.bridge.result(rig.bridge.commands[0].commandId, "unknown", "bridge_unreachable");
        QCOMPARE(panel.feedback(), QString("Delivery not confirmed. Check the agent before sending again."));
        QCOMPARE(panel.composerText(), QString("answer"));
        QVERIFY(!panel.sendEnabled()); // Reconcile before offering a resend.
        QCOMPARE(rig.bridge.commands.size(), 1);
    }

    void timeoutShowsUnknownNotSuccess() {
        qint64 now = 1700000000000;
        Service service([&] { return now; });
        FakeBridge bridge(service, 1);
        bridge.connect(); bridge.hello({key("s-1111")});
        bridge.request(key("s-1111"), "q1", 1, "Pick", {}, true);
        ConversationPanel panel(service);
        panel.open(key("s-1111"));
        panel.typeText("answer"); panel.send();
        now += service.commandTimeoutMs + 1;
        service.expire();
        QCOMPARE(panel.feedback(), QString("Delivery not confirmed. Check the agent before sending again."));
    }

    void followUpOnlyWhenReadyAndNeverAutomatic() {
        Rig rig;
        rig.bridge.ready(rig.a, false);
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        QCOMPARE(panel.placeholder(), QString("Follow up with this agent…"));
        panel.typeText("now add tests");
        QVERIFY(!panel.sendEnabled()); // The agent is working: the draft waits.
        rig.bridge.ready(rig.a, true);
        QVERIFY(panel.sendEnabled());
        QVERIFY(rig.bridge.commands.isEmpty()); // Becoming ready sends nothing by itself.
        panel.send();
        QCOMPARE(rig.bridge.commands[0].action, Action::SendFollowUp);
        QCOMPARE(rig.bridge.commands[0].text, QString("now add tests"));
        rig.bridge.accept(rig.bridge.commands[0]);
        QCOMPARE(panel.feedback(), QString("Sent"));
        QVERIFY(panel.composerText().isEmpty());
    }

    void ctrlEnterSendsAndPlainEnterAddsALine() {
        Rig rig;
        ConversationPanel panel(rig.service);
        panel.show();
        panel.open(rig.a);
        auto *edit = panel.findChild<ComposerEdit *>();
        QVERIFY(edit);
        QTest::keyClicks(edit, "line one");
        QTest::keyClick(edit, Qt::Key_Return);
        QTest::keyClicks(edit, "line two");
        QCOMPARE(panel.composerText(), QString("line one\nline two"));
        QVERIFY(rig.bridge.commands.isEmpty());
        QTest::keyClick(edit, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(rig.bridge.commands.size(), 1);
        QCOMPARE(rig.bridge.commands[0].text, QString("line one\nline two"));
    }

    void escapeCollapsesAndKeepsTheDraft() {
        Rig rig;
        ConversationPanel panel(rig.service);
        QSignalSpy collapsed(&panel, &ConversationPanel::collapsed);
        panel.show();
        panel.open(rig.a);
        panel.typeText("half a thought");
        QTest::keyClick(&panel, Qt::Key_Escape);
        QVERIFY(!panel.isVisible());
        QCOMPARE(collapsed.count(), 1);
        panel.open(rig.a);
        QCOMPARE(panel.composerText(), QString("half a thought"));
        QCOMPARE(rig.service.draft(rig.a, ""), QString("half a thought"));
    }

    void draftsAndSelectionsNeverCrossConversations() {
        Rig rig;
        rig.bridge.open(rig.b, true, "api-web"); // The same project name, a different session.
        rig.bridge.request(rig.a, "q1", 1, "Pick", {}, true);
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        panel.typeText("for A");
        panel.open(rig.b);
        QVERIFY(panel.composerText().isEmpty());
        QCOMPARE(panel.heading(), QString("api-web · Fake · bbbb"));
        panel.typeText("for B");
        panel.open(rig.a);
        QCOMPARE(panel.composerText(), QString("for A"));
        panel.send();
        QCOMPARE(rig.bridge.commands.size(), 1);
        QCOMPARE(rig.bridge.commands[0].conversation, rig.a);
        QCOMPARE(rig.bridge.commands[0].text, QString("for A"));
        QCOMPARE(rig.service.draft(rig.b, ""), QString("for B"));
    }

    void feedbackBelongsToItsConversation() {
        Rig rig;
        rig.bridge.open(rig.b);
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        panel.typeText("hi"); panel.send();
        QCOMPARE(panel.feedback(), QString("Sending…"));
        panel.open(rig.b);
        QVERIFY(panel.feedback().isEmpty());
    }

    void disconnectDisablesSendingButKeepsHistoryAndDraft() {
        Rig rig;
        rig.bridge.message(rig.a, 1, "m1", "hello");
        rig.bridge.request(rig.a, "q1", 1, "Pick", {"x"}, true);
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        panel.chooseCustom(); panel.typeText("draft");
        rig.bridge.disconnect();
        QCOMPARE(panel.status(), QString("Disconnected"));
        QVERIFY(!panel.sendEnabled());
        QCOMPARE(panel.messageCount(), 1);
        QCOMPARE(panel.composerText(), QString("draft"));
    }

    void anEndedSessionIsReadableButClosed() {
        Rig rig;
        rig.bridge.message(rig.a, 1, "m1", "bye");
        rig.bridge.ended(rig.a);
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        QCOMPARE(panel.status(), QString("Session ended"));
        QCOMPARE(panel.messageCount(), 1);
        QVERIFY(!panel.composerEnabled());
        QVERIFY(!panel.sendEnabled());
    }

    void aStaleQuestionIsAnswerableOnlyAfterReview() {
        Rig rig;
        rig.bridge.request(rig.a, "q1", 1, "Pick", {"x", "y"});
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        panel.chooseOption(0);
        // The question is replaced under the user's hands: the choice must not carry over.
        rig.bridge.requestState(rig.a, "q1", "replaced");
        rig.bridge.request(rig.a, "q2", 1, "Pick again", {"x", "y"});
        QCOMPARE(panel.question(), QString("Pick again"));
        QVERIFY(!panel.sendEnabled());
    }

    void severalQuestionsAreAnsweredOneAtATime() {
        Rig rig;
        rig.bridge.request(rig.a, "q1", 1, "First?", {"x", "y"});
        rig.bridge.request(rig.a, "q2", 1, "Second?", {"x", "y"});
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        QCOMPARE(panel.requestChoices(), 2);
        panel.chooseRequest(1);
        QCOMPARE(panel.question(), QString("Second?"));
        panel.chooseOption(0);
        panel.send();
        QCOMPARE(rig.bridge.commands[0].requestId, QString("q2"));
    }

    void longMessagesFoldIndividually() {
        Rig rig;
        rig.bridge.message(rig.a, 1, "m1", QString("line\n").repeated(20));
        rig.bridge.message(rig.a, 2, "m2", "short");
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        QVERIFY(panel.foldable(0));
        QVERIFY(!panel.foldable(1));
        QVERIFY(panel.folded(0));
        panel.toggleFold(0);
        QVERIFY(!panel.folded(0));
        rig.bridge.message(rig.a, 3, "m3", "another"); // A rebuild keeps the user's choice.
        QVERIFY(!panel.folded(0));
    }

    void messagesAreMarkdownButNeverRawHtmlOrImages() {
        Rig rig;
        rig.bridge.message(rig.a, 1, "m1", "**bold** and `code`\n\n- one\n- two\n\n<b>raw</b> ![chart](http://example.invalid/x.png) [site](http://example.invalid/)", "agent");
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        QLabel *label = nullptr;
        for (auto *l : panel.findChildren<QLabel *>())
            if (l->text().contains("bold")) label = l;
        QVERIFY(label);
        QCOMPARE(label->textFormat(), Qt::RichText);
        QVERIFY(label->text().contains("font-weight")); // Formatted, not shown with its asterisks.
        QVERIFY(!label->text().contains("**"));
        QVERIFY(label->text().contains("&lt;b&gt;raw")); // Raw HTML is text.
        QVERIFY(!label->text().contains("<img"));       // An image is never loaded.
        QVERIFY(label->text().contains("http://example.invalid/x.png"));
        QVERIFY(!label->openExternalLinks());
    }

    void markdownIsPlainWordsInPreviewsAndTheList() {
        QCOMPARE(conversation::firstLine("## Plan\nmore"), QString("Plan"));
        QCOMPARE(conversation::firstLine("**Done** with `x`"), QString("Done with x"));
        QCOMPARE(conversation::firstLine("```cpp\nint x;\n```"), QString("int x;"));
        QCOMPARE(conversation::firstLine("\n\n  - first item\n- second"), QString("first item"));
        QCOMPARE(conversation::firstLine("Add tests before continuing?"), QString("Add tests before continuing?"));
    }

    void anEndedOrQuietSessionLeavesTheList() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        rig.bridge.open(rig.b);
        rig.bridge.message(rig.a, 1, "m1", "hello", "agent");
        rig.bridge.message(rig.b, 1, "m2", "hi", "agent");
        hub.list().present(rig.service);
        QCOMPARE(hub.list().rowCount(), 2);
        rig.bridge.ended(rig.b);
        hub.list().present(rig.service);
        QCOMPARE(hub.list().rowCount(), 1);
        // Quiet for long, nothing waiting: taken to be closed. Active again: back.
        auto &c = rig.conv(rig.a);
        QVERIFY(conversation::listed(c, c.lastActivity + 1000));
        rig.bridge.message(rig.a, 2, "m3", "x", "user");
        QVERIFY(conversation::listed(rig.conv(rig.a), rig.conv(rig.a).lastActivity));
        rig.service.markRead(rig.a, "m1");
        rig.service.markRead(rig.a, "m3");
        auto copy = rig.conv(rig.a);
        copy.ready = true;
        QVERIFY(!conversation::listed(copy, copy.lastActivity + conversation::idleListMs + 1));
        rig.bridge.disconnect();
        QVERIFY(!conversation::listed(rig.conv(rig.a), rig.conv(rig.a).lastActivity));
    }

    void anOpenPanelClosesWhenItsSessionEnds() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        rig.bridge.message(rig.a, 1, "m1", "hi", "agent");
        hub.open(rig.a);
        hub.panel().show();
        QVERIFY(hub.panel().isVisible());
        rig.bridge.ended(rig.a);
        QVERIFY(!hub.panel().isVisible());
        // With a draft typed it stays, so nothing written is lost.
        rig.bridge.open(rig.b);
        hub.open(rig.b);
        hub.panel().typeText("keep me");
        rig.bridge.ended(rig.b);
        QVERIFY(hub.panel().isVisible());
    }

    void messagesAreReadOnlyOnceVisible() {
        Rig rig;
        rig.bridge.message(rig.a, 1, "m1", "hello");
        QCOMPARE(rig.conv(rig.a).unreadCount(), 1);
        ConversationPanel panel(rig.service);
        panel.open(rig.a); // Not shown yet: opening a hidden panel reads nothing.
        QCOMPARE(rig.conv(rig.a).unreadCount(), 1);
        panel.show();
        QTRY_COMPARE(rig.conv(rig.a).unreadCount(), 0);
    }

    void readingAQuestionDoesNotResolveIt() {
        Rig rig;
        rig.bridge.request(rig.a, "q1", 1, "Pick", {"x"});
        ConversationPanel panel(rig.service);
        panel.show(); panel.open(rig.a);
        QTest::qWait(50);
        QCOMPARE(rig.conv(rig.a).requests.value("q1").state, RequestState::Pending);
        QCOMPARE(rig.service.attentionCount(), 1);
    }

    void historyGapAndCapacityAreSaid() {
        Rig rig;
        ConversationPanel panel(rig.service);
        panel.open(rig.a);
        rig.bridge.message(rig.a, 1, "m1", "first");
        // The next snapshot starts after what we hold: a gap the panel must admit to.
        rig.bridge.snapshot(rig.a, 50, {}, QJsonArray{QJsonObject{{"message_id", "m50"}, {"sequence", 50}, {"role", "agent"}, {"text", "later"}}}, {}, 50);
        QVERIFY(rig.conv(rig.a).historyGap);
        bool said = false;
        for (auto *label : panel.findChildren<QLabel *>()) said = said || (!label->isHidden() && label->text().contains("could not be loaded"));
        QVERIFY(said);
    }

    void panelClampsToASmallScreen() {
        Rig rig;
        ConversationPanel panel(rig.service);
        panel.place(QRect(100, 100, 160, 160), {QRect(0, 0, 360, 300)});
        QVERIFY(panel.width() <= 360 && panel.height() <= 300);
    }

    // ---- Coordinator -----------------------------------------------------------------------------

    void aQuestionPreviewsAndHidingKeepsItPending() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        rig.bridge.request(rig.a, "q1", 1, "Add tests before continuing?\nsecond line", {"tests", "skip"});
        QVERIFY(hub.preview().isVisible());
        QCOMPARE(hub.preview().title(), QString("Needs your answer"));
        QCOMPARE(hub.preview().name(), QString("api-web"));
        QCOMPARE(hub.preview().body(), QString("Add tests before continuing?")); // The real first line.
        QCOMPARE(hub.attention(), 1);
        hub.preview().hideMessage();
        QVERIFY(!hub.preview().isVisible());
        QCOMPARE(rig.service.attentionCount(), 1); // Hiding never answers it.
        QCOMPARE(hub.attention(), 1);
    }

    void aMessagePreviewFadesAndStaysUnread() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        hub.setFadeMs(30);
        hub.hovering = [] { return false; }; // The offscreen pointer may sit on the preview.
        rig.bridge.message(rig.a, 1, "m1", "All done.");
        QVERIFY(hub.preview().isVisible());
        QCOMPARE(hub.preview().title(), QString("New message"));
        QTRY_VERIFY(!hub.preview().isVisible());
        QCOMPARE(rig.conv(rig.a).unreadCount(), 1);
        // The same message delivered again does not bring it back or restart anything.
        rig.bridge.message(rig.a, 1, "m1", "All done.");
        QVERIFY(!hub.preview().isVisible());
        rig.bridge.message(rig.a, 2, "m2", "One more thing.");
        QVERIFY(hub.preview().isVisible());
    }

    void aNewMessageNeverOpensThePanelOrTakesFocus() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        rig.bridge.message(rig.a, 1, "m1", "hello");
        QVERIFY(!hub.panel().isVisible());
        QVERIFY(hub.preview().testAttribute(Qt::WA_ShowWithoutActivating));
        QVERIFY(hub.preview().windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
    }

    void aQuestionOutranksAMessageAndTheRestAreCounted() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        rig.bridge.open(rig.b, true, "other");
        rig.bridge.message(rig.a, 1, "m1", "plain news");
        rig.bridge.request(rig.b, "q1", 1, "Which one?", {"x"});
        QCOMPARE(hub.preview().title(), QString("Needs your answer"));
        QCOMPARE(hub.preview().name(), QString("other"));
        QCOMPARE(hub.preview().more(), QString("+1"));
    }

    void anOpenPanelNeedsNoPreviewForItsOwnConversation() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        rig.bridge.request(rig.a, "q1", 1, "Pick", {"x"});
        QVERIFY(hub.preview().isVisible());
        hub.open(rig.a);
        QVERIFY(hub.panel().isVisible());
        QVERIFY(!hub.preview().isVisible());
                hub.panel().collapse();
        QVERIFY(hub.preview().isVisible()); // Still pending: the preview is back.
    }

    void answeringElsewhereClosesThePreview() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        rig.bridge.request(rig.a, "q1", 1, "Pick", {"x"});
        QVERIFY(hub.preview().isVisible());
        rig.bridge.requestState(rig.a, "q1", "answered");
        QVERIFY(!hub.preview().isVisible());
        QCOMPARE(hub.attention(), 0);
    }

    void hiddenExcerptsShowOnlyIdentityAndState() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        hub.setHideExcerpts(true);
        rig.bridge.message(rig.a, 1, "m1", "secret plans");
        QVERIFY(hub.preview().isVisible());
        QVERIFY(hub.preview().body().isEmpty());
        QCOMPARE(hub.preview().title(), QString("New message"));
    }

    void listShowsEachConversationAndOpensItsPanel() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        rig.bridge.open(rig.b, true, "other");
        rig.bridge.request(rig.b, "q1", 1, "Which one?", {"x"});
        hub.list().present(rig.service);
        QCOMPARE(hub.list().rowCount(), 2);
        QVERIFY(hub.list().rowText(0).contains("Needs your answer"));
        hub.list().activateRow(0);
        QVERIFY(hub.panel().isVisible());
        QCOMPARE(hub.panel().current(), rig.b);
    }

    // Not a check: with PET_SCREENSHOT_DIR set it writes the states the specification asks to see.
    void screenshots() {
        const auto dir = qEnvironmentVariable("PET_SCREENSHOT_DIR");
        if (dir.isEmpty()) QSKIP("Set PET_SCREENSHOT_DIR to write screenshots");
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        hub.hovering = [] { return false; };
        rig.bridge.message(rig.a, 1, "m0", "Fix the handler in api-web");
        rig.bridge.message(rig.a, 2, "m1", "## Handler updated\n\nI changed **api-web** so it *validates* the payload:\n\n```cpp\nif (!valid(body)) return error(400);\n```\n\n- add focused tests\n- run `ctest`\n\nSee [the notes](http://example.invalid/notes) for more.", "agent");
        rig.bridge.request(rig.a, "q1", 1, "Add tests before continuing?", {"Add focused tests", "Continue without tests"}, true);
        hub.preview().adjustSize();
        hub.preview().grab().save(dir + "/1-collapsed-question.png");
        hub.open(rig.a);
        hub.panel().show();
        QTest::qWait(100);
        hub.panel().grab().save(dir + "/2-expanded-question.png");
        hub.panel().chooseOption(0);
        hub.panel().grab().save(dir + "/2b-option-chosen.png");
        hub.panel().send();
        hub.panel().grab().save(dir + "/3-sending.png");
        rig.bridge.accept(rig.bridge.commands[0]);
        QTest::qWait(50);
        hub.panel().grab().save(dir + "/4-answered.png");
        hub.panel().typeText("Please also update the changelog");
        hub.panel().send();
        rig.bridge.result(rig.bridge.commands[1].commandId, "unknown", "bridge_unreachable");
        hub.panel().grab().save(dir + "/5-unknown-delivery.png");
        rig.bridge.disconnect();
        QTest::qWait(50);
        hub.panel().grab().save(dir + "/6-disconnected.png");
    }

    void theMenuOffersConversationsOnlyWhileTheServiceRuns() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        const auto visible = [&] {
            for (auto *menu : window.findChildren<QMenu *>())
                for (auto *action : menu->actions())
                    if (action->text() == "Agent conversations…") return action->isVisible();
            return false;
        };
        QVERIFY(!visible());
        {
            ConversationHub hub(window, rig.service);
            QVERIFY(visible());
        }
    }

    void thePreviewOpensTheHostWithoutOpeningThePanel() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        ConversationKey asked;
        hub.focusHost = [&](const ConversationKey &k) { asked = k; return true; };
        rig.bridge.request(rig.a, "q1", 1, "Add tests?", {"Yes", "No"}, false);
        QVERIFY(hub.preview().isVisible());
        hub.preview().openHost();
        QCOMPARE(asked, rig.a);
        QVERIFY(!hub.panel().isVisible());
    }

    void thePanelIsABubbleAndAChoiceLooksDifferentFromTheRest() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        QVERIFY(hub.panel().windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(hub.panel().testAttribute(Qt::WA_TranslucentBackground));
        rig.bridge.request(rig.a, "q1", 1, "Pick one", {"One", "Two"}, true);
        hub.open(rig.a);
        QTest::qWait(50);
        const auto indicator = [&](int index) {
            auto *button = hub.panel().findChildren<QRadioButton *>().value(index);
            QStyleOptionButton option;
            option.initFrom(button);
            const auto area = button->style()->subElementRect(QStyle::SE_RadioButtonIndicator, &option, button);
            return button->grab().toImage().pixelColor(area.center());
        };
        hub.panel().chooseOption(1);
        QTest::qWait(50);
        QVERIFY(indicator(0) != indicator(1));
    }

    void aFailedOpenOffersToDetachAndOnlyAYesLetsGo() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        rig.bridge.message(rig.a, 1, "m1", "hi", "agent");
        int asked = 0;
        bool answer = false;
        hub.confirmDetach = [&](const ConversationKey &) { ++asked; return answer; };
        bool raised = true;
        hub.focusHost = [&](const ConversationKey &) { return raised; };

        hub.open(rig.a);
        const auto openHost = [&] { for (auto *b : hub.panel().findChildren<QPushButton *>()) if (b->text() == "Open") b->click(); };
        openHost();
        QCOMPARE(asked, 0); // It worked: nothing to ask.

        raised = false;
        openHost(); // Failed, and the answer is no.
        QCOMPARE(asked, 1);
        QCOMPARE(rig.conv(rig.a).link, Conversation::Link::Connected);
        for (const auto &f : rig.bridge.inbox) QVERIFY(f.value("type") != "release");

        answer = true;
        openHost(); // Failed, and the answer is yes: the bridge is told and the conversation ends.
        QCOMPARE(asked, 2);
        QCOMPARE(rig.conv(rig.a).link, Conversation::Link::Ended);
        QVERIFY(std::any_of(rig.bridge.inbox.begin(), rig.bridge.inbox.end(), [&](const QJsonObject &f) {
            return f.value("type") == "release" && f.value("conversation").toObject() == rig.a.toJson(); }));
        QVERIFY(!hub.panel().isVisible()); // Nothing was typed, so the panel went with it.
        hub.list().present(rig.service);
        QCOMPARE(hub.list().rowCount(), 0);
    }

    void aFailedOpenFromThePreviewOffersToDetachToo() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        int asked = 0;
        hub.confirmDetach = [&](const ConversationKey &) { ++asked; return true; };
        hub.focusHost = [](const ConversationKey &) { return false; };
        rig.bridge.request(rig.a, "q1", 1, "Add tests?", {"Yes", "No"}, false);
        QVERIFY(hub.preview().isVisible());
        hub.preview().openHost();
        QCOMPARE(asked, 1);
        QCOMPARE(rig.conv(rig.a).link, Conversation::Link::Ended);
        QVERIFY(!hub.preview().isVisible());
    }

    void releasingAnEndedOrUnknownConversationDoesNothing() {
        Rig rig;
        QVERIFY(!rig.service.release(rig.b));
        QVERIFY(rig.service.release(rig.a));
        QVERIFY(!rig.service.release(rig.a));
        int releases = 0;
        for (const auto &f : rig.bridge.inbox) releases += f.value("type") == "release";
        QCOMPARE(releases, 1);
    }

    void openingTheHostUsesTheConversationsOwnSession() {
        Rig rig;
        PetWindow window(nullptr, {}, false);
        ConversationHub hub(window, rig.service);
        ConversationKey asked;
        hub.focusHost = [&](const ConversationKey &k) { asked = k; return true; };
        hub.open(rig.a);
        auto buttons = hub.panel().findChildren<QPushButton *>();
        for (auto *button : buttons) if (button->text() == "Open") button->click();
        QCOMPARE(asked, rig.a);
    }
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setApplicationName("agent-pet-test");
    ConversationTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "conversation_tests.moc"
