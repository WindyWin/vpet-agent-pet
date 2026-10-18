#include "pack_controls.h"
#include "source_browser.h"
#include "packs/installer.h"
#include "animation/pet_library.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QUrl>
#include <QTimer>

namespace pet {
namespace {
void message(QWidget *parent, const QString &text) {
    QMessageBox box(QMessageBox::Information, QCoreApplication::translate("PackControls", "Pack installation"), text, QMessageBox::Ok, parent);
    box.setTextFormat(Qt::PlainText); box.exec();
}
}
void importPack(QWidget *parent, const QString &path) {
    QString error; auto candidate = packs::inspect(path, error);
    if (!candidate) { message(parent, error); return; }
    QDialog dialog(parent); dialog.setWindowTitle(QCoreApplication::translate("PackControls", "Import pack")); dialog.resize(560, 520);
    auto *layout = new QVBoxLayout(&dialog); const auto m = candidate->metadata;
    auto *label = new QLabel(QCoreApplication::translate("PackControls", "Unverified local content: %1\n%2 by %3\nVersion: %4\nLicence: %5\nSource: %6")
        .arg(path, m.value("name").toString(), m.value("author").toString(), m.value("version").toString(), m.value("license").toString(), m.value("source").toString()), &dialog);
    label->setTextFormat(Qt::PlainText); label->setWordWrap(true); layout->addWidget(label);
    if (!candidate->oldVersion.isEmpty()) {
        auto *replace = new QLabel(QCoreApplication::translate("PackControls", "Replace installed version %1 (%2) with %3. This also permits an equal version or a downgrade.")
            .arg(candidate->oldVersion, candidate->oldSource, m.value("version").toString()), &dialog);
        replace->setTextFormat(Qt::PlainText); replace->setWordWrap(true); layout->addWidget(replace);
    }
    auto *terms = new QPlainTextEdit(candidate->terms, &dialog); terms->setReadOnly(true); layout->addWidget(terms);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *accept = buttons->addButton(QCoreApplication::translate("PackControls", "Accept terms and install"), QDialogButtonBox::AcceptRole);
    QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    if (!packs::install(*candidate, error)) message(parent, error);
    else message(parent, QCoreApplication::translate("PackControls", "Pack change queued. Restart Agent Pet to apply it."));
}
QWidget *packControls(QWidget *parent, const QString &kind, std::function<void(const QString &, const QString &)> removed) {
    auto *widget = new QWidget(parent); auto *layout = new QVBoxLayout(widget);
    auto *browse = new QPushButton(QCoreApplication::translate("PackControls", "Browse pack sources…"), widget); layout->addWidget(browse);
    QObject::connect(browse, &QPushButton::clicked, widget, [widget, removed] {
        // The pet owns this separate window so rebuilding Settings cannot cancel it.
        auto *settings = widget->window();
        auto *owner = settings->parentWidget() ? settings->parentWidget() : settings;
        auto *browser = owner->findChild<SourceBrowser *>(QString(), Qt::FindDirectChildrenOnly);
        if (!browser) {
            browser = new SourceBrowser(owner);
            QObject::connect(browser, &SourceBrowser::packRemoved, owner, removed);
        }
        browser->show(); browser->raise(); browser->activateWindow();
    });
    auto *button = new QPushButton(QCoreApplication::translate("PackControls", "Import…"), widget); layout->addWidget(button);
    QObject::connect(button, &QPushButton::clicked, widget, [widget] {
        const auto path = QFileDialog::getOpenFileName(widget, QCoreApplication::translate("PackControls", "Import pack"), {}, QCoreApplication::translate("PackControls", "Agent Pet packs (*.agentpet)"));
        if (!path.isEmpty()) importPack(widget, path);
    });
    auto *choices = new QComboBox(widget); choices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    choices->setMinimumContentsLength(24); layout->addWidget(choices);
    auto &library = PetLibrary::shared();
    const auto sourceLabel = [&](const QString &id) {
        const auto origin = packs::packOrigin(kind, id);
        return origin.isEmpty() ? QCoreApplication::translate("PackControls", " — No source (manual updates only)")
            : QCoreApplication::translate("PackControls", " — Installed from a source");
    };
    if (kind == "pet") { for (const auto &p : library.pets()) if (!p.folder.isEmpty()) choices->addItem(p.name + " (" + p.id + ')' + sourceLabel(p.id), p.id); }
    else { for (const auto &p : library.plugins()) choices->addItem(p.name + " (" + p.id + ')' + sourceLabel(p.id), p.id); }
    auto *remove = new QPushButton(QCoreApplication::translate("PackControls", "Remove installed pack…"), widget); remove->setEnabled(choices->count()); layout->addWidget(remove);
    QObject::connect(remove, &QPushButton::clicked, widget, [widget, choices, remove, kind, removed] {
        const auto id = choices->currentData().toString();
        QMessageBox confirm(QMessageBox::Question, QCoreApplication::translate("PackControls", "Remove pack"), QCoreApplication::translate("PackControls", "Remove %1 on the next start? Removing the selected pet switches to VPet.").arg(id), QMessageBox::Yes | QMessageBox::Cancel, widget);
        confirm.setTextFormat(Qt::PlainText); confirm.setDefaultButton(QMessageBox::Cancel);
        if (confirm.exec() != QMessageBox::Yes) return;
        QString error; if (!packs::remove(kind, id, error)) { message(widget, error); return; }
        removed(kind, id); choices->removeItem(choices->currentIndex()); remove->setEnabled(choices->count());
        message(widget, QCoreApplication::translate("PackControls", "Pack change queued. Restart Agent Pet to apply it."));
    });
    return widget;
}
bool packDrop(QWidget *parent, QEvent *event) {
    if (event->type() != QEvent::DragEnter && event->type() != QEvent::Drop) return false;
    auto *drop = static_cast<QDropEvent *>(event);
    const auto urls = drop->mimeData()->urls();
    if (urls.size() != 1 || !urls[0].isLocalFile() || !urls[0].toLocalFile().endsWith(".agentpet", Qt::CaseInsensitive)) return false;
    drop->acceptProposedAction();
    if (event->type() == QEvent::Drop) {
        const auto path = urls[0].toLocalFile();
        QTimer::singleShot(0, parent, [parent, path] { importPack(parent, path); });
    }
    return true;
}
}
