#include "vpet_import.h"
#include "pack_controls.h"
#include "packs/installer.h"
#include "packs/steam_workshop.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>
#include <QCoreApplication>

namespace pet {
void showVPetImport(QWidget *parent) {
    auto *dialog = new QDialog(parent); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QCoreApplication::translate("VPetImport", "Import VPet mod")); dialog->resize(600, 620);
    auto *layout = new QVBoxLayout(dialog);
    auto *notice = new QLabel(QCoreApplication::translate("VPetImport", "Bring your own mod and its licence text. Only LPS metadata and PNG artwork are read; code plugins are ignored. Nothing is downloaded from Steam."), dialog);
    notice->setWordWrap(true); layout->addWidget(notice);
    auto *form = new QFormLayout; layout->addLayout(form);
    auto *path = new QLineEdit(dialog); path->setObjectName("vpetModPath"); form->addRow(QCoreApplication::translate("VPetImport", "Mod folder, ZIP or pet definition"), path);
    auto *folder = new QPushButton(QCoreApplication::translate("VPetImport", "Choose folder…"), dialog); auto *file = new QPushButton(QCoreApplication::translate("VPetImport", "Choose ZIP or LPS…"), dialog);
    form->addRow(folder, file);
    QObject::connect(folder, &QPushButton::clicked, dialog, [dialog, path] { const auto value = QFileDialog::getExistingDirectory(dialog, QCoreApplication::translate("VPetImport", "Choose mod folder")); if (!value.isEmpty()) path->setText(value); });
    QObject::connect(file, &QPushButton::clicked, dialog, [dialog, path] { const auto value = QFileDialog::getOpenFileName(dialog, QCoreApplication::translate("VPetImport", "Choose mod"), {}, QCoreApplication::translate("VPetImport", "VPet mods (*.zip *.lps)")); if (!value.isEmpty()) path->setText(value); });
    auto *workshop = new QComboBox(dialog); workshop->addItem(QCoreApplication::translate("VPetImport", "Choose an installed Steam workshop item…"), QString());
    for (const auto &item : packs::steamWorkshopItems(packs::steamRoots())) workshop->addItem(item.id + " — " + item.path, item.path);
    form->addRow(QCoreApplication::translate("VPetImport", "Installed workshop items"), workshop);
    QObject::connect(workshop, &QComboBox::currentIndexChanged, dialog, [workshop, path] { if (!workshop->currentData().toString().isEmpty()) path->setText(workshop->currentData().toString()); });
    auto *id = new QLineEdit("converted-pet", dialog); form->addRow(QCoreApplication::translate("VPetImport", "Pet id"), id);
    auto *author = new QLineEdit(dialog); author->setPlaceholderText(QCoreApplication::translate("VPetImport", "Use mod metadata if available")); form->addRow(QCoreApplication::translate("VPetImport", "Author"), author);
    auto *license = new QLineEdit(dialog); license->setPlaceholderText(QCoreApplication::translate("VPetImport", "Use mod metadata if available")); form->addRow(QCoreApplication::translate("VPetImport", "Licence"), license);
    auto *terms = new QLineEdit(dialog); terms->setObjectName("vpetModTerms"); auto *chooseTerms = new QPushButton(QCoreApplication::translate("VPetImport", "Choose licence text…"), dialog); form->addRow(chooseTerms, terms);
    QObject::connect(chooseTerms, &QPushButton::clicked, dialog, [dialog, terms] { const auto value = QFileDialog::getOpenFileName(dialog, QCoreApplication::translate("VPetImport", "Choose licence text"), {}, QCoreApplication::translate("VPetImport", "Text files (*.txt *.md);;All files (*)")); if (!value.isEmpty()) terms->setText(value); });
    auto *report = new QPlainTextEdit(dialog); report->setReadOnly(true); layout->addWidget(report);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    auto *convert = buttons->addButton(QCoreApplication::translate("VPetImport", "Convert and review…"), QDialogButtonBox::ActionRole); layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    QObject::connect(convert, &QPushButton::clicked, dialog, [=] {
        struct Result { std::unique_ptr<packs::Candidate> candidate; QJsonObject report; QString error; bool ok = false; };
        auto result = std::make_shared<Result>(); result->candidate = packs::stage(result->error);
        if (!result->candidate) { report->setPlainText(result->error); return; }
        const packs::VPetOptions options{id->text(), author->text(), license->text(), terms->text()}; const auto input = path->text();
        convert->setEnabled(false); report->setPlainText(QCoreApplication::translate("VPetImport", "Converting artwork… You can close this window to discard the result."));
        // The worker owns no widgets. Closing Settings or this dialog discards its result.
        auto *worker = QThread::create([result, input, options] { result->ok = packs::convertVPetMod(input, result->candidate->folder, options, result->report, result->error); });
        worker->setParent(QCoreApplication::instance()); worker->setObjectName("vpetConversion");
        QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        QObject::connect(worker, &QThread::finished, dialog, [=] {
            convert->setEnabled(true);
            if (!result->ok || !packs::inspectStaged(*result->candidate, result->error)) { report->setPlainText(result->error); return; }
            report->setPlainText(QString::fromUtf8(QJsonDocument(result->report).toJson()));
            reviewPack(dialog, input, std::move(result->candidate), result->report);
        });
        // Do not destroy a running QThread during shutdown; finish its bounded conversion.
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, worker, [worker] { worker->wait(); });
        worker->start();
    });
    dialog->show();
}
}
