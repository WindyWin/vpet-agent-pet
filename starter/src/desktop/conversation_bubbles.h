#pragma once
#include "interactions/service.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace pet {
// The collapsed preview: a compact two-line card beside the pet, "● Needs your answer · api-web  +2" over the
// first line of the message, with View and Hide. It never takes focus and never expands by itself.
class ConversationPreview : public QWidget {
    Q_OBJECT
public:
    struct Content {
        QString title, name, body, tooltip;
        bool attention = false;
        int more = 0;
    };
    explicit ConversationPreview(QWidget *parent = nullptr);
    void present(const Content &content);
    void place(const QRect &pet, const QVector<QRect> &screens, const QVector<QRect> &avoid = {});
    QString title() const { return title_->text(); }
    QString name() const { return name_->text(); }
    QString body() const { return body_->text(); }
    QString more() const { return more_->isVisible() ? more_->text() : QString(); }
    void view() { emit viewRequested(); }
    void hideMessage() { emit hideRequested(); }
signals:
    void viewRequested();
    void hideRequested();
    void listRequested();
protected:
    void changeEvent(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    void retranslate();
    QLabel *title_, *name_, *body_;
    QPushButton *more_, *view_, *hide_;
    QString fullBody_;
    QColor accent_;
};

// The popup listing every conversation, most recent first, with what each needs; a row opens its panel.
class ConversationList : public QWidget {
    Q_OBJECT
public:
    static constexpr int maxRows = 10;
    explicit ConversationList(QWidget *parent = nullptr);
    void present(const interactions::Service &service);
    void place(const QRect &pet, const QVector<QRect> &screens, const QVector<QRect> &avoid = {});
    int rowCount() const { return buttons_.size(); }
    QString rowText(int row) const { return buttons_.value(row) ? buttons_[row]->text() : QString(); }
    void activateRow(int row) { if (buttons_.value(row)) buttons_[row]->click(); }
signals:
    void conversationRequested(const interactions::ConversationKey &key);
protected:
    void changeEvent(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
private:
    void retranslate();
    QVBoxLayout *rows_;
    QLabel *header_, *empty_;
    QVector<QPushButton *> buttons_;
    QString shown_;
    bool presented_ = false;
};
}
