// Bazarish project (c) 2026
#include "MainWindow.hpp"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTextEdit>
#include <QVBoxLayout>

#include <exception>
#include <vector>

namespace bazarish::gui {

namespace {

// A short, human-friendly form of a 52-char fingerprint for list display.
QString shortFingerprint(const QString& fingerprint)
{
    if (fingerprint.size() <= 12) {
        return fingerprint;
    }
    return fingerprint.left(8) + "…" + fingerprint.right(4);
}

}  // namespace

MainWindow::MainWindow(client::Session& session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
    , contacts_(new QListWidget(this))
    , transcript_(new QTextEdit(this))
    , input_(new QLineEdit(this))
    , status_(new QLabel(this))
{
    setWindowTitle("Bazarish — " + QString::fromStdString(session_.fingerprint()));
    resize(720, 480);

    transcript_->setReadOnly(true);

    auto* addButton = new QPushButton("Add contact…", this);
    auto* syncButton = new QPushButton("Sync", this);
    auto* sendButton = new QPushButton("Send", this);

    // Left column: contacts plus the add button.
    auto* left = new QVBoxLayout();
    left->addWidget(new QLabel("Contacts", this));
    left->addWidget(contacts_, 1);
    left->addWidget(addButton);

    // Right column: transcript, composer, sync.
    auto* composer = new QHBoxLayout();
    composer->addWidget(input_, 1);
    composer->addWidget(sendButton);

    auto* right = new QVBoxLayout();
    right->addWidget(transcript_, 1);
    right->addLayout(composer);
    right->addWidget(syncButton);

    auto* columns = new QHBoxLayout();
    columns->addLayout(left, 1);
    columns->addLayout(right, 2);

    auto* root = new QVBoxLayout(this);
    root->addLayout(columns, 1);
    root->addWidget(status_);

    connect(addButton, &QPushButton::clicked, this, &MainWindow::onAddContact);
    connect(syncButton, &QPushButton::clicked, this, &MainWindow::onSync);
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::onSend);
    connect(input_, &QLineEdit::returnPressed, this, &MainWindow::onSend);
    connect(contacts_, &QListWidget::itemSelectionChanged, this,
        &MainWindow::onContactSelected);

    refreshContactList();
    status_->setText("Ready. " + QString::fromStdString(session_.fingerprint()));

    // Periodic background sync so incoming messages appear on their own.
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::onSync);
    timer->start(3000);
}

QString MainWindow::selectedContact() const
{
    const QListWidgetItem* item = contacts_->currentItem();
    if (item == nullptr) {
        return QString();
    }
    return item->data(Qt::UserRole).toString();
}

void MainWindow::refreshContactList()
{
    const QString selected = selectedContact();
    contacts_->clear();
    for (const std::string& fingerprint : session_.contactFingerprints()) {
        const QString full = QString::fromStdString(fingerprint);
        auto* item = new QListWidgetItem(shortFingerprint(full), contacts_);
        item->setData(Qt::UserRole, full);
        item->setToolTip(full);
        if (full == selected) {
            contacts_->setCurrentItem(item);
        }
    }
}

void MainWindow::appendLine(const QString& fingerprint, const QString& line)
{
    history_[fingerprint].append(line);
    if (fingerprint == selectedContact()) {
        transcript_->append(line);
    }
}

void MainWindow::showConversation(const QString& fingerprint)
{
    transcript_->clear();
    for (const QString& line : history_.value(fingerprint)) {
        transcript_->append(line);
    }
}

void MainWindow::onContactSelected()
{
    showConversation(selectedContact());
}

void MainWindow::onAddContact()
{
    bool ok = false;
    const QString fingerprint = QInputDialog::getText(this, "Add contact",
        "Contact fingerprint:", QLineEdit::Normal, QString(), &ok);
    if (!ok || fingerprint.isEmpty()) {
        return;
    }
    const QString text = QInputDialog::getText(this, "Add contact",
        "Introduction message:", QLineEdit::Normal, "Hi, add me?", &ok);
    if (!ok) {
        return;
    }
    try {
        session_.sendContactRequest(fingerprint.toStdString(), text.toStdString());
        appendLine(fingerprint, "→ (contact request) " + text);
        refreshContactList();
        status_->setText("Contact request sent.");
    } catch (const std::exception& error) {
        QMessageBox::warning(this, "Contact request failed", error.what());
    }
}

void MainWindow::onSend()
{
    const QString fingerprint = selectedContact();
    if (fingerprint.isEmpty()) {
        status_->setText("Select a contact first.");
        return;
    }
    const QString text = input_->text();
    if (text.isEmpty()) {
        return;
    }
    try {
        session_.sendMessage(fingerprint.toStdString(), text.toStdString());
        appendLine(fingerprint, "→ " + text);
        input_->clear();
        status_->setText("Sent.");
    } catch (const std::exception& error) {
        QMessageBox::warning(this, "Send failed", error.what());
    }
}

void MainWindow::onSync()
{
    std::vector<client::IncomingMessage> messages;
    try {
        messages = session_.sync();
    } catch (const std::exception& error) {
        status_->setText(QString("Sync error: ") + error.what());
        return;
    }
    for (const client::IncomingMessage& message : messages) {
        const QString from = QString::fromStdString(message.fromFingerprint);
        const QString prefix = message.kind == "contact-request" ? "← (request) " : "← ";
        appendLine(from, prefix + QString::fromStdString(message.text));
    }
    if (!messages.empty()) {
        refreshContactList();
        status_->setText(QString("Received %1 item(s).").arg(messages.size()));
    }
}

}  // namespace bazarish::gui
