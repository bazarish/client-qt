// Bazarish project (c) 2026
#include "MainWindow.hpp"

#include "Qr.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
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

// Draws one QR symbol as a black-on-white pixmap: scale pixels per module
// plus a quiet-zone border so a scanner can lock onto it.
QPixmap qrToPixmap(const client::QrSymbol& symbol, int scale, int quiet)
{
    const int dim = (symbol.width + 2 * quiet) * scale;
    QImage image(dim, dim, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    for (int y = 0; y < symbol.width; ++y) {
        for (int x = 0; x < symbol.width; ++x) {
            if (symbol.modules[y * symbol.width + x]) {
                painter.fillRect((x + quiet) * scale, (y + quiet) * scale, scale, scale,
                    Qt::black);
            }
        }
    }
    painter.end();
    return QPixmap::fromImage(image);
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

    auto* inviteButton = new QPushButton("My invite (link + QR)…", this);
    auto* addInviteButton = new QPushButton("Add by invite…", this);
    auto* addUserButton = new QPushButton("Add by username…", this);
    auto* addButton = new QPushButton("Add by fingerprint…", this);
    auto* exportButton = new QPushButton("Export backup…", this);
    auto* syncButton = new QPushButton("Sync", this);
    auto* sendButton = new QPushButton("Send", this);

    // Left column: contacts plus the add/share buttons.
    auto* left = new QVBoxLayout();
    left->addWidget(new QLabel("Contacts", this));
    left->addWidget(contacts_, 1);
    left->addWidget(inviteButton);
    left->addWidget(addInviteButton);
    left->addWidget(addUserButton);
    left->addWidget(addButton);
    left->addWidget(exportButton);

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

    connect(inviteButton, &QPushButton::clicked, this, &MainWindow::onShowInvite);
    connect(addInviteButton, &QPushButton::clicked, this, &MainWindow::onAddByInvite);
    connect(addUserButton, &QPushButton::clicked, this, &MainWindow::onAddByUsername);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::onAddContact);
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::onExport);
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

void MainWindow::onShowInvite()
{
    std::string uri;
    try {
        uri = session_.inviteUri();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, "No invite",
            QString("Subscribe first.\n") + error.what());
        return;
    }
    const QString link = QString::fromStdString(uri);

    QDialog dialog(this);
    dialog.setWindowTitle("My invite");
    dialog.resize(560, 640);
    auto* layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel(
        "Share this link or the QR codes. It carries your full, self-verifying\n"
        "trust chain — a contact needs no server trust to add you.", &dialog));

    auto* linkEdit = new QTextEdit(&dialog);
    linkEdit->setReadOnly(true);
    linkEdit->setPlainText(link);
    linkEdit->setMaximumHeight(110);
    layout->addWidget(linkEdit);

    auto* copyButton = new QPushButton("Copy link to clipboard", &dialog);
    connect(copyButton, &QPushButton::clicked, &dialog,
        [link]() { QApplication::clipboard()->setText(link); });
    layout->addWidget(copyButton);

    // The full post-quantum chain spans several QR symbols; show them stacked
    // in a scroll area so a scanner can read each frame in turn.
    const std::vector<client::QrSymbol> symbols = client::encodeQrSymbols(uri);
    auto* frames = new QWidget(&dialog);
    auto* framesLayout = new QVBoxLayout(frames);
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        framesLayout->addWidget(new QLabel(
            QString("QR %1 / %2").arg(i + 1).arg(symbols.size()), frames));
        auto* qrLabel = new QLabel(frames);
        qrLabel->setPixmap(qrToPixmap(symbols[i], 3, 2));
        framesLayout->addWidget(qrLabel);
    }
    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidget(frames);
    scroll->setWidgetResizable(true);
    layout->addWidget(scroll, 1);

    dialog.exec();
}

void MainWindow::onAddByInvite()
{
    bool ok = false;
    const QString link = QInputDialog::getMultiLineText(this, "Add by invite",
        "Paste the bazarish:// invite link:", QString(), &ok);
    if (!ok || link.trimmed().isEmpty()) {
        return;
    }
    const QString text = QInputDialog::getText(this, "Add by invite",
        "Introduction message:", QLineEdit::Normal, "Hi, found your invite!", &ok);
    if (!ok) {
        return;
    }
    try {
        session_.addByInvite(link.trimmed().toStdString(), text.toStdString());
        refreshContactList();
        status_->setText("Contact request sent from invite.");
    } catch (const std::exception& error) {
        QMessageBox::warning(this, "Add by invite failed", error.what());
    }
}

void MainWindow::onAddByUsername()
{
    bool ok = false;
    const QString alias = QInputDialog::getText(this, "Add by username",
        "Username (resolved on this server; the resolver is trusted for the\n"
        "name → fingerprint mapping):",
        QLineEdit::Normal, QString(), &ok);
    if (!ok || alias.trimmed().isEmpty()) {
        return;
    }
    const QString text = QInputDialog::getText(this, "Add by username",
        "Introduction message:", QLineEdit::Normal, "Hi, add me?", &ok);
    if (!ok) {
        return;
    }
    try {
        session_.addByUsername(alias.trimmed().toStdString(), text.toStdString());
        refreshContactList();
        status_->setText("Contact request sent (resolver-trusted mapping).");
    } catch (const std::exception& error) {
        QMessageBox::warning(this, "Add by username failed", error.what());
    }
}

void MainWindow::onExport()
{
    const QString path = QFileDialog::getSaveFileName(this, "Export encrypted backup",
        "bazarish-backup.baz");
    if (path.isEmpty()) {
        return;
    }
    bool ok = false;
    const QString password = QInputDialog::getText(this, "Export backup",
        "Bundle password:", QLineEdit::Password, QString(), &ok);
    if (!ok || password.isEmpty()) {
        return;
    }
    try {
        session_.exportState(path.toStdString(), password.toStdString());
        status_->setText("Exported encrypted backup.");
    } catch (const std::exception& error) {
        QMessageBox::warning(this, "Export failed", error.what());
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
