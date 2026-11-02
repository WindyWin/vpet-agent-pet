#pragma once
#include <QDialog>
#include "packs/sources.h"
#include <QHash>
#include <QPixmap>
#include <QSet>
class QComboBox;
class QCheckBox;
class QListWidget;
class QLabel;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;
namespace pet {
class SourceBrowser : public QDialog {
    Q_OBJECT
public:
    explicit SourceBrowser(QWidget *parent = nullptr, const QString &sourceFolder = {});
    void reject() override;
protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
signals:
    void packRemoved(const QString &kind, const QString &id);
private:
    packs::SourceStore store_;
    QNetworkAccessManager network_;
    packs::Download transfer_, previewTransfer_;
    QComboBox *sources_;
    QCheckBox *automatic_;
    QLabel *background_;
    QListWidget *list_;
    QPlainTextEdit *details_;
    QLabel *preview_, *status_, *notice_;
    QPushButton *refresh_, *add_, *removeSource_, *install_, *remove_, *direct_, *cancel_, *close_;
    QProgressBar *progress_;
    QVector<packs::Entry> entries_;
    QString source_, mode_, revision_;
    QString previewFolder_, previewUrl_;
    QHash<QString, QPixmap> previews_;
    QSet<QString> failedPreviews_;
    packs::Entry downloading_;
    bool working_ = false, closing_ = false, suppressPreview_ = false;
    void loadSources(const QString &selected = {});
    void loadCatalog();
    void select();
    void queuePreview();
    void applyPreview(const QString &url, const QPixmap &image);
    QString previewPath(const QString &url) const;
    void busy(bool value);
    void refresh();
    void download();
    void direct();
    void received(const QString &path);
    bool confirm(const QString &title, const QString &text, const QString &action);
};
}
