// Bazarish project (c) 2026
#pragma once

#include "Session.hpp"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QWidget>

class QListWidget;
class QTextEdit;
class QLineEdit;
class QLabel;

namespace bazarish::gui {

// The main chat window over a client Session: a contact list, the selected
// conversation, a message composer and periodic mailbox sync. Conversation
// history is kept in memory (the session persists only contacts and tokens).
class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(client::Session& session, QWidget* parent = nullptr);

private slots:
    void onSync();
    void onSend();
    void onAddContact();
    void onContactSelected();

private:
    void refreshContactList();
    void appendLine(const QString& fingerprint, const QString& line);
    void showConversation(const QString& fingerprint);
    QString selectedContact() const;

    client::Session& session_;
    QListWidget* contacts_;
    QTextEdit* transcript_;
    QLineEdit* input_;
    QLabel* status_;
    // fingerprint -> rendered conversation lines.
    QMap<QString, QStringList> history_;
};

}  // namespace bazarish::gui
