// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "SessionShared.hpp"
#include "SystemNotes.hpp"

#include "I2pRouter.hpp"
#include "Invite.hpp"

#include <QFile>

#include <QJsonDocument>

#include "AvatarStore.hpp"
#include "PictureStore.hpp"
#include "DeliveryStatus.hpp"
#include "LetterAvatar.hpp"
#include "PhotoShot.hpp"
#include "Session.hpp"

#include <bazarish/Crypto.hpp>
#include <bazarish/Limits.hpp>
#include <bazarish/Descriptor.hpp>
#include <bazarish/Portal.hpp>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QBuffer>
#include <QByteArray>
#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaMethod>
#include <QSet>
#include <QImage>
#include <QMimeDatabase>
#include <QQuickItemGrabResult>
#include <chrono>
#include <QTimer>
#include <cstring>
#include <QRegularExpression>
#include <QUrl>

#if defined(Q_OS_LINUX) && defined(BAZARISH_HAVE_QTDBUS)
#endif

#include <algorithm>
#include <array>
#include <ctime>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace bazarish::app {

struct ResolvedContactAddQueue {
    std::mutex mutex;
    struct Entry {
        QString opId;
        bazarish::client::Session::ContactCardResolved resolved;
    };
    std::vector<Entry> results;
    std::vector<std::pair<QString, QString>> stages;
};

struct AliasErrandQueue {
    std::mutex mutex;
    std::vector<bazarish::client::Session::AliasErrandResult> results;
};

using bazarish::client::IncomingMessage;
using bazarish::client::ServerEndpoint;
using bazarish::client::Session;

namespace {
constexpr int kSchemeSeparatorLength = 3;

QString facadeHost(const QString& url)
{
    QString rest = url.trimmed();
    const int schemeEnd = rest.indexOf(QStringLiteral("://"));
    if (schemeEnd >= 0) {
        rest = rest.mid(schemeEnd + kSchemeSeparatorLength);
    }
    const int pathStart = rest.indexOf(QLatin1Char('/'));
    if (pathStart >= 0) {
        rest = rest.left(pathStart);
    }
    return rest;
}

// A reconnect this client asked for is not reported before this much of it.
constexpr int kLinkRebuildGraceMs = 1500;




constexpr int kPageSize = 50;

constexpr int kBusyPaintDelayMs = 32;

constexpr int kReadSyncIdleMs = 4000;

constexpr int kAvatarSide = 256;
constexpr int kAvatarFallbackSide = 128;
constexpr int kAvatarFirstQuality = 90;
constexpr int kAvatarQualityStep = 15;
constexpr int kAvatarLowestQuality = 30;
constexpr int kAvatarFallbackQuality = 80;

QByteArray compressAvatarJpeg(const QImage& img)
{
    const int side = std::min(img.width(), img.height());
    QImage square = img.copy((img.width() - side) / 2, (img.height() - side) / 2, side, side);
    if (square.width() > kAvatarSide) {
        square = square.scaled(
            kAvatarSide, kAvatarSide, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    constexpr int kCap = static_cast<int>(bazarish::kAvatarMaxBytes);
    const auto encode = [&square](int quality) {
        QByteArray out;
        QBuffer buffer(&out);
        buffer.open(QIODevice::WriteOnly);
        square.save(&buffer, "JPEG", quality);
        buffer.close();
        return out;
    };
    QByteArray bytes;
    int quality = kAvatarFirstQuality;
    do {
        bytes = encode(quality);
        quality -= kAvatarQualityStep;
    } while (bytes.size() > kCap && quality >= kAvatarLowestQuality);
    if (bytes.size() > kCap) {
        square = square.scaled(kAvatarFallbackSide, kAvatarFallbackSide, Qt::IgnoreAspectRatio,
            Qt::SmoothTransformation);
        bytes = encode(kAvatarFallbackQuality);
    }
    return bytes;
}

// A page is chosen by id and shown by order key, so its bounds are its lowest and highest id.
qint64 lowestId(const QVector<StoredMessage>& rows)
{
    return std::min_element(rows.begin(), rows.end(),
        [](const StoredMessage& a, const StoredMessage& b) { return a.id < b.id; })->id;
}

qint64 highestId(const QVector<StoredMessage>& rows)
{
    return std::max_element(rows.begin(), rows.end(),
        [](const StoredMessage& a, const StoredMessage& b) { return a.id < b.id; })->id;
}

}  // namespace


SessionController::SessionController(QObject* parent)
    : QObject(parent)
{
    contactsProxy_.setSourceModel(&contacts_);
    contactsProxy_.setFilterRole(ContactListModel::NameRole);
    contactsProxy_.setFilterCaseSensitivity(Qt::CaseInsensitive);


    connect(&events_, &AccountEvents::connectProgress, this, &SessionController::onConnectProgress);
    connect(&events_, &AccountEvents::addressMismatch, this,
        [this](const QString& served, const QString& ours) {
            emit addressNeedsChoice(served, ours);
        });
    connect(&events_, &AccountEvents::stopped, this, [this]() {
        store_.close();
    });
    connect(&events_, &AccountEvents::closed, this, &SessionController::closed);
    connect(&events_, &AccountEvents::downloadProgress, this,
        &SessionController::onDownloadProgress);
    connect(&events_, &AccountEvents::servedProgress, this, &SessionController::onServedProgress);
    connect(&events_, &AccountEvents::transferStage, this, &SessionController::onTransferStage);
    connect(&events_, &AccountEvents::servedFinished, this, &SessionController::onServedFinished);
    connect(&events_, &AccountEvents::downloadFinished, this,
        &SessionController::onDownloadFinished);
    connect(&events_, &AccountEvents::pairOfferReady, this, &SessionController::onPairOfferReady);
    connect(&events_, &AccountEvents::pairStage, this, &SessionController::onPairStage);
    connect(&events_, &AccountEvents::pairFinished, this, &SessionController::onPairFinished);
    connect(&events_, &AccountEvents::aliasHoldings, this,
        [this](const QVariantList& rows, const QString& note) {
            aliasHoldings_ = rows;
            aliasNote_ = note;
            emit aliasChanged();
        });
    connect(&events_, &AccountEvents::aliasActivationDone, this, [this](const bool ok,
                                                                   const QString& text) {
        aliasBusy_ = false;
        emit aliasChanged();
        if (ok) {
            emit actionOk(text);
        } else {
            emit actionFailed(text);
        }
    });

    connect(&events_, &AccountEvents::opened, this, &SessionController::onOpened);
    connect(&events_, &AccountEvents::accountSettings, this,
        [this](const bool acceptCalls, const bool sendReceipts) {
            if (sendReceipts_ != sendReceipts) {
                sendReceipts_ = sendReceipts;
                emit sendReceiptsChanged();
            }
            if (acceptCalls_ != acceptCalls) {
                acceptCalls_ = acceptCalls;
                emit acceptCallsChanged();
            }
        });
    connect(&events_, &AccountEvents::renamed, this, [this](const QString& newName) {
        if (newName != displayName_) {
            displayName_ = newName;
            emit identityChanged();
        }
    });
    connect(&events_, &AccountEvents::openFailed, this, &SessionController::openFailed);
    connect(&events_, &AccountEvents::connectionChanged, this,
        &SessionController::onConnectionChanged);
    connect(&events_, &AccountEvents::rowAdded, this, &SessionController::onRowAdded);
    connect(&events_, &AccountEvents::rowChanged, this, &SessionController::onRowChanged);
    connect(&events_, &AccountEvents::rowRemoved, this, &SessionController::onRowRemoved);
    connect(&events_, &AccountEvents::peerReset, this, &SessionController::onPeerReset);
    connect(&events_, &AccountEvents::peerForgotten, this, &SessionController::onPeerForgotten);
    connect(&events_, &AccountEvents::chatChanged, this, &SessionController::refreshChatRow);
    connect(&events_, &AccountEvents::chatsChanged, this, &SessionController::rebuildChatList);
    connect(&events_, &AccountEvents::deliveredThrough, this,
        &SessionController::onDeliveredThrough);
    connect(&events_, &AccountEvents::reactionsChanged, this, [this]() {
        ++reactionsRevision_;
        emit reactionsRevisionChanged();
    });
    connect(&events_, &AccountEvents::reactionsToFlash, this, [this](const QStringList& flashing) {
        if (reactionsToFlash_ == flashing) {
            return;
        }
        reactionsToFlash_ = flashing;
        ++reactionsRevision_;
        emit reactionsRevisionChanged();
    });
    connect(&events_, &AccountEvents::recentReactions, this, [this](const QStringList& recent) {
        recentReactions_ = recent;
        emit recentReactionsChanged();
    });
    connect(&events_, &AccountEvents::groupChanged, this, [this](const QString& peer) {
        if (peer == activePeer_) {
            refreshActiveGroup();
        }
    });
    connect(&events_, &AccountEvents::accountRenamed, this, [this](const QString& name) {
        if (name != displayName_) {
            displayName_ = name;
            emit identityChanged();
        }
    });
    connect(&events_, &AccountEvents::contactsRevised, this, [this]() {
        ++contactsRevision_;
        emit contactsRevisionChanged();
    });
    connect(&events_, &AccountEvents::contactsRefreshed, this,
        [this](const QVector<ContactState>& contacts, const QStringList& blocked) {
            book_.reset(contacts, blocked);
            rebuildChatList();
            emit activePeerNameChanged();
            ++contactsRevision_;
            emit contactsRevisionChanged();
        });
    connect(&events_, &AccountEvents::avatarReady, this, &SessionController::onAvatarReady);
    connect(&events_, &AccountEvents::uploadProgress, this, &SessionController::onUploadProgress);
    connect(&events_, &AccountEvents::messageError, this,
        [this](const qint64 localId, const QString& error) {
            conversation_.setErrorForId(localId, error);
        });
    connect(&events_, &AccountEvents::resendFilePickRequested, this,
        &SessionController::resendFilePickRequested);
    connect(&events_, &AccountEvents::showConversation, this, [this](const QString& peer) {
        if (peer != activePeer_) {
            openConversation(peer);
        }
    });
    connect(&events_, &AccountEvents::contactRetryExhausted, this,
        &SessionController::contactRetryExhausted);
    connect(&events_, &AccountEvents::transcriptOpened, this,
        [this](const QString& path, const QByteArray& key) {
            if (!store_.open(path, Bytes(key.begin(), key.end()))) {
                emit openFailed(tr("This profile could not be opened."));
            }
        });
    connect(&events_, &AccountEvents::storageWorkDone, this, [this]() {
        loadLatestWindow();
        rebuildChatList();
        endStorageWork();
    });
    connect(&events_, &AccountEvents::contactNamed, this,
        [this](const QString& peer, const QString& name) { book_.notePendingName(peer, name); });
    connect(&events_, &AccountEvents::contactAccepted, this,
        &SessionController::onContactAccepted);
    connect(&events_, &AccountEvents::opChanged, this, [this](const OperationRow& row) {
        operations_.upsert(row);
        emit operationsChanged();
    });
    connect(&events_, &AccountEvents::opRemoved, this, [this](const QString& id) {
        operations_.remove(id);
        emit operationsChanged();
    });
    connect(&events_, &AccountEvents::syncReachable, this, &SessionController::onSyncReachable);
    connect(&events_, &AccountEvents::approvalState, this, &SessionController::onApprovalState);
    connect(&events_, &AccountEvents::devicesReady, this, &SessionController::onDevicesReady);
    connect(&events_, &AccountEvents::accountClosed, this,
        &SessionController::accountClosedOnServer);
    connect(&events_, &AccountEvents::facadeInfo, this, &SessionController::onFacadeInfo);
    connect(&events_, &AccountEvents::actionOk, this, &SessionController::actionOk);
    connect(&events_, &AccountEvents::actionFailed, this, [this](const QString& reason) {
        setAvatarBusy(false);
        if (connecting_) {
            connecting_ = false;
            connectPhase_.clear();
            connectError_ = reason;
            emit connectStateChanged();
        }
        emit actionFailed(reason);
    });
    connect(&events_, &AccountEvents::inviteReady, this, [this](const QString& uri) {
        if (ownInvite_ != uri) {
            ownInvite_ = uri;
            emit ownInviteChanged();
        }
        emit inviteReady(uri);
    });
    connect(&events_, &AccountEvents::inviteUnavailable, this,
        &SessionController::inviteUnavailable);
    connect(&events_, &AccountEvents::loginSigned, this, &SessionController::loginSigned);
    connect(&events_, &AccountEvents::connectionLogReady, this,
        &SessionController::connectionLogUpdated);
    connect(&events_, &AccountEvents::serverHello, this, &SessionController::serverHello);
    connect(&events_, &AccountEvents::i2pStatus, this, &SessionController::onI2pStatus);
    connect(&events_, &AccountEvents::i2pKeyState, this, &SessionController::onI2pKeyState);
    connect(&events_, &AccountEvents::i2pServedAddress, this, [this](const QString& address) {
        if (i2pServedAddress_ == address) {
            return;
        }
        i2pServedAddress_ = address;
        emit i2pStatusChanged();
    });
    connect(&events_, &AccountEvents::storageUsageReady, this,
        &SessionController::onStorageUsageReady);
    connect(&events_, &AccountEvents::callStateChanged, this,
        &SessionController::onCallStateChanged);

    connect(&contacts_, &QAbstractItemModel::dataChanged, this,
        &SessionController::refreshUnreadTotal);
    connect(&contacts_, &QAbstractItemModel::rowsInserted, this,
        &SessionController::refreshUnreadTotal);
    connect(&contacts_, &QAbstractItemModel::modelReset, this,
        &SessionController::refreshUnreadTotal);

    rebuildGrace_.setSingleShot(true);
    rebuildGrace_.setInterval(kLinkRebuildGraceMs);
    connect(&rebuildGrace_, &QTimer::timeout, this, [this]() {
        linksRebuilding_ = false;
        onSyncReachable(false);
    });

    core_ = std::make_unique<CoreAccount>(commands_, events_);
    connect(core_.get(), &CoreAccount::messageNotification, this,
        &SessionController::messageNotification);
    connect(core_.get(), &CoreAccount::reactionNotification, this,
        &SessionController::reactionNotification);
}

void SessionController::refreshUnreadTotal()
{
    const int total = contacts_.totalUnread();
    if (total != unreadTotal_) {
        unreadTotal_ = total;
        emit unreadTotalChanged();
    }
}

SessionController::~SessionController()
{
    shutdown();
}

void SessionController::beginShutdown()
{
    if (shuttingDown_) {
        return;
    }
    shuttingDown_ = true;
    emit commands_.requestShutdown();
}

void SessionController::shutdown()
{
    shuttingDown_ = true;
    core_->shutdown();
    store_.close();
}

void SessionController::open(const QString& file, const QString& accountId,
    const QString& passphrase, const bool startOnline)
{
    startOnline_ = startOnline;
    accountId_ = accountId;
    emit commands_.requestOpen(file, passphrase, startOnline);
}

void SessionController::connectServer(const QStringList& facadeUrls, const QString& serverFp,
    const QStringList& reseedUrls)
{
    connecting_ = true;
    connectPercent_ = 0;
    connectPhase_ = tr("Starting…");
    connectError_.clear();
    emit connectStateChanged();
    emit commands_.requestConnect(facadeUrls, serverFp, reseedUrls);
}

void SessionController::onConnectProgress(const int percent, const QString& phase)
{
    if (percent < connectPercent_) {
        return;
    }
    connectPercent_ = percent;
    connectPhase_ = phase;
    emit connectStateChanged();
}

QString SessionController::activeFacadeHost() const
{
    return facadeHost(activeFacade_);
}

void SessionController::onFacadeInfo(const QString& activeUrl, const QStringList& configured,
    const QString& serverFp, const QStringList& reseeds)
{
    activeFacade_ = activeUrl;
    configuredFacades_ = configured;
    serverFp_ = serverFp;
    configuredReseeds_ = reseeds;
    emit facadeInfoChanged();
}

QVariantMap SessionController::parseServerLink(const QString& uri) const
{
    QVariantMap result;
    try {
        const bazarish::client::ServerLink link
            = bazarish::client::decodeServerLink(uri.trimmed().toStdString());
        result["serverFp"] = QString::fromStdString(link.serverFingerprint);
        QStringList facades;
        for (const std::string& url : link.facadeUrls) {
            facades << QString::fromStdString(url);
        }
        result["facades"] = facades;
        QStringList reseeds;
        for (const std::string& url : link.reseedUrls) {
            reseeds << QString::fromStdString(url);
        }
        result["reseeds"] = reseeds;
    } catch (const std::exception& error) {
        bazarish::log::debug("server link not parsed: {}", error.what());
    }
    return result;
}

void SessionController::activateConversation(const QString& peer)
{
    emit commands_.viewConversation(peer);
    activePeer_ = peer;
    emit activePeerChanged();
    emit activePeerNameChanged();
    refreshActiveGroup();
}

void SessionController::refreshActiveGroup()
{
    activeGroup_ = activePeer_.isEmpty() ? std::nullopt : store_.groupState(activePeer_);
    emit activeGroupChanged();
}

QString SessionController::letterOf(const QString& name) const
{
    return bazarish::app::letterOf(name);
}

QColor SessionController::letterFill(const QString& name) const
{
    return bazarish::app::letterFill(name);
}

QVariantList SessionController::invitesIn(const QString& text) const
{
    QVariantList found;
    static const QRegularExpression kInvite(QStringLiteral("bazarish://invite\\?\\S+"));
    for (QRegularExpressionMatchIterator it = kInvite.globalMatch(text); it.hasNext();) {
        const QString uri = it.next().captured(0);
        try {
            const bazarish::Descriptor descriptor = bazarish::parseDescriptor(uri.toStdString());
            QVariantMap plate;
            plate[QStringLiteral("uri")] = uri;
            plate[QStringLiteral("name")] = QString::fromStdString(descriptor.name);
            plate[QStringLiteral("fingerprint")] = QString::fromStdString(descriptor.fingerprint);
            found << plate;
        } catch (const std::exception&) {
            continue;
        }
    }
    return found;
}

QString SessionController::withoutInvites(const QString& text) const
{
    QString rest = text;
    for (const QVariant& plate : invitesIn(text)) {
        rest.remove(plate.toMap().value(QStringLiteral("uri")).toString());
    }
    return rest.trimmed();
}

void SessionController::loadLatestWindow()
{
    const QVector<StoredMessage> msgs = store_.latestMessages(activePeer_, kPageSize);
    oldestLoadedId_ = msgs.isEmpty() ? 0 : lowestId(msgs);
    newestLoadedId_ = msgs.isEmpty() ? 0 : highestId(msgs);
    hasMoreOlder_ = !msgs.isEmpty() && store_.hasMessagesBefore(activePeer_, oldestLoadedId_);
    hasMoreNewer_ = false;
    conversation_.setMessages(msgs);
    requestPicturesFor(msgs);
    replayTransfersForActivePeer();
    emit pagingChanged();
}

void SessionController::showInActiveView(const StoredMessage& m, bool isOwn)
{
    if (m.peer != activePeer_) {
        return;
    }
    if (hasMoreNewer_) {
        if (isOwn) {
            loadLatestWindow();
            emit scrollToBottom();
        }
        return;
    }
    conversation_.appendMessage(m);
    newestLoadedId_ = m.id;
}

void SessionController::closeConversation()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    emit commands_.viewConversation({});
    activePeer_.clear();
    conversation_.setMessages({});
    emit activePeerChanged();
    emit activePeerNameChanged();
}

void SessionController::openConversation(const QString& peer)
{
    activateConversation(peer);
    const qint64 firstUnread = store_.firstUnreadId(peer);
    loadLatestWindow();
    if (firstUnread > 0) {
        if (oldestLoadedId_ > 0 && firstUnread >= oldestLoadedId_) {
            emit scrollToUnread(firstUnread);
        } else {
            openWindowAtUnread(peer, firstUnread);
        }
    }
}

void SessionController::openWindowAtUnread(const QString& peer, qint64 firstUnread)
{
    const QVector<StoredMessage> win = store_.newerMessages(peer, firstUnread - 1, kPageSize);
    if (win.isEmpty()) {
        loadLatestWindow();
        return;
    }
    oldestLoadedId_ = lowestId(win);
    newestLoadedId_ = highestId(win);
    hasMoreOlder_ = store_.hasMessagesBefore(peer, oldestLoadedId_);
    hasMoreNewer_ = store_.hasMessagesAfter(peer, newestLoadedId_);
    conversation_.setMessages(win);
    requestPicturesFor(win);
    replayTransfersForActivePeer();
    emit pagingChanged();
    emit scrollToUnread(firstUnread);
}

void SessionController::saveScroll(const QString& peer, int anchorRow, bool stick)
{
    scrollPeer_ = peer;
    scrollAnchorRow_ = anchorRow;
    scrollStick_ = stick;
}

QVariantMap SessionController::scrollFor(const QString& peer) const
{
    QVariantMap m;
    const bool has = !peer.isEmpty() && peer == scrollPeer_;
    m[QStringLiteral("has")] = has;
    m[QStringLiteral("anchor")] = scrollAnchorRow_;
    m[QStringLiteral("stick")] = scrollStick_;
    return m;
}

void SessionController::openConversationAtMessage(const QString& peer, qint64 localId)
{
    activateConversation(peer);
    const QVector<StoredMessage> win = store_.olderMessages(peer, localId + 1, kPageSize);
    oldestLoadedId_ = win.isEmpty() ? 0 : lowestId(win);
    newestLoadedId_ = win.isEmpty() ? 0 : highestId(win);
    hasMoreOlder_ = !win.isEmpty() && store_.hasMessagesBefore(peer, oldestLoadedId_);
    hasMoreNewer_ = store_.hasMessagesAfter(peer, newestLoadedId_);
    conversation_.setMessages(win);
    requestPicturesFor(win);
    replayTransfersForActivePeer();
    emit pagingChanged();
    emit scrollToMessage(localId);
}

int SessionController::loadOlderMessages()
{
    if (!hasMoreOlder_ || activePeer_.isEmpty()) {
        return 0;
    }
    const QVector<StoredMessage> older
        = store_.olderMessages(activePeer_, oldestLoadedId_, kPageSize);
    if (older.isEmpty()) {
        hasMoreOlder_ = false;
        emit pagingChanged();
        return 0;
    }
    oldestLoadedId_ = lowestId(older);
    hasMoreOlder_ = store_.hasMessagesBefore(activePeer_, oldestLoadedId_);
    conversation_.prependMessages(older);
    requestPicturesFor(older);
    emit pagingChanged();
    return static_cast<int>(older.size());
}

int SessionController::loadNewerMessages()
{
    if (!hasMoreNewer_ || activePeer_.isEmpty()) {
        return 0;
    }
    const QVector<StoredMessage> newer
        = store_.newerMessages(activePeer_, newestLoadedId_, kPageSize);
    if (newer.isEmpty()) {
        hasMoreNewer_ = false;
        emit pagingChanged();
        return 0;
    }
    newestLoadedId_ = highestId(newer);
    hasMoreNewer_ = store_.hasMessagesAfter(activePeer_, newestLoadedId_);
    conversation_.appendMessages(newer);
    requestPicturesFor(newer);
    emit pagingChanged();
    return static_cast<int>(newer.size());
}

void SessionController::jumpToLatest()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    if (hasMoreNewer_) {
        loadLatestWindow();
    }
    emit scrollToBottom();
}

bool SessionController::atNewest() const
{
    return !hasMoreNewer_;
}

bool SessionController::hasMoreOlder() const
{
    return hasMoreOlder_;
}

QVariantList SessionController::searchMessages(const QString& query)
{
    QVariantList results;
    if (activePeer_.isEmpty()) {
        return results;
    }
    for (const SearchHit& hit : store_.searchInPeer(activePeer_, query)) {
        QVariantMap row;
        row["id"] = hit.id;
        row["text"] = hit.text;
        row["time"] = hit.ts;
        row["outgoing"] = hit.outgoing;
        row["author"] = hit.outgoing ? tr("You") : peerName(activePeer_);
        results.push_back(row);
    }
    return results;
}

void SessionController::rebuildChatList()
{
    QVector<ContactRow> rows;
    QSet<QString> known;
    if (!savedPeer().isEmpty()) {
        ContactRow saved{savedPeer(), savedChatName(), chatPreview(savedPeer()),
            store_.lastTime(savedPeer()), 0, store_.isPinned(savedPeer())};
        saved.saved = true;
        rows.push_back(saved);
        known.insert(savedPeer());
    }
    for (const QString& fp : book_.fingerprints()) {
        rows.push_back(ContactRow{fp, peerName(fp), chatPreview(fp), store_.lastTime(fp),
            store_.unreadCount(fp), store_.isPinned(fp)});
        known.insert(fp);
    }
    for (const QString& peer : store_.conversationPeers()) {
        if (peer.isEmpty() || known.contains(peer)) {
            continue;
        }
        rows.push_back(ContactRow{peer, peerName(peer), chatPreview(peer),
            store_.lastTime(peer), store_.unreadCount(peer), store_.isPinned(peer)});
    }
    contacts_.setContacts(std::move(rows));
}

QString SessionController::savedPeer() const
{
    return fingerprint_;
}

QString SessionController::savedChatName()
{
    return ContactBook::savedChatName();
}

void SessionController::retranslate()
{
    rebuildChatList();
    conversation_.retranslate();
}

QString SessionController::attachmentLabel(const QString& type)
{
    if (type == QStringLiteral("audio")) {
        return tr("[voice]");
    }
    if (type == QStringLiteral("image")) {
        return tr("[image]");
    }
    return tr("[file]");
}

QString SessionController::chatPreview(const QString& peer) const
{
    const TranscriptStore::LastMessage last = store_.lastMessage(peer);
    QString preview;
    if (!last.text.isEmpty()) {
        preview = isServiceMessage(last.type) ? systemNoteText(last.text) : last.text;
    } else if (last.type.isEmpty()) {
        return {};
    } else if (last.attachment.isEmpty()) {
        preview = attachmentLabel(last.type);
    } else {
        preview = attachmentLabel(last.type) + QLatin1Char(' ') + last.attachment;
    }
    return last.author.isEmpty() ? preview : tr("%1: %2").arg(last.author, preview);
}

bool SessionController::activeAliasLookup() const
{
    return hasAliasSigil(activePeer_);
}

QString SessionController::peerName(const QString& id) const
{
    return book_.peerName(id);
}

QString SessionController::contactName(const QString& fp) const
{
    return book_.state(fp).name;
}

void SessionController::setAvatarFromGrab(QObject* const grab)
{
    auto* const grabbed = qobject_cast<QQuickItemGrabResult*>(grab);
    const QByteArray jpeg = (grabbed == nullptr || grabbed->image().isNull())
        ? QByteArray()
        : compressAvatarJpeg(grabbed->image());
    if (jpeg.isEmpty()) {
        emit actionFailed(tr("The cropped avatar could not be read."));
        return;
    }
    setAvatarBusy(true);
    emit commands_.requestSetAvatar(jpeg);
}

void SessionController::setAvatarBusy(const bool busy)
{
    if (avatarBusy_ == busy) {
        return;
    }
    avatarBusy_ = busy;
    emit avatarChanged();
}

bool SessionController::hasAvatar() const
{
    return !AvatarStore::instance().image(fingerprint_).isNull();
}

void SessionController::clearAvatar()
{
    setAvatarBusy(true);
    emit commands_.requestClearAvatar();
}

void SessionController::setDisplayName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == displayName_) {
        return;
    }
    emit commands_.requestSetDisplayName(trimmed);
}

bool SessionController::canWriteTo(const QString& fp) const
{
    return book_.state(fp).writable;
}

QString SessionController::contactInvite(const QString& fp) const
{
    return book_.state(fp).invite;
}

void SessionController::renameContact(const QString& fp, const QString& name)
{
    if (fp.isEmpty()) {
        return;
    }
    const QString trimmed = name.trimmed();
    book_.edit(fp).name = trimmed;
    rebuildChatList();
    emit activePeerNameChanged();
    emit commands_.requestRenameContact(fp, trimmed);
}

void SessionController::clearChat(bool forEveryone)
{
    if (!activePeer_.isEmpty()) {
        emit commands_.clearChat(activePeer_, forEveryone);
    }
}

void SessionController::deleteContact()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QString peer = activePeer_;
    book_.remove(peer);
    dropChat(peer);
    if (!hasAliasSigil(peer)) {
        emit commands_.requestRemoveContact(peer);
    }
}

void SessionController::dropChat(const QString& peer)
{
    emit commands_.forgetChat(peer);
}

void SessionController::clearSavedEverywhere()
{
    emit commands_.clearSavedEverywhere();
}

bool SessionController::isBlocked(const QString& peer) const
{
    return book_.isBlocked(peer);
}

QVariantList SessionController::blockedList() const
{
    QVariantList rows;
    for (const QString& peer : book_.blocked()) {
        rows.push_back(QVariantMap{{QStringLiteral("fingerprint"), peer},
            {QStringLiteral("name"), peerName(peer)}});
    }
    return rows;
}

void SessionController::setBlocked(const QString& peer, const bool blocked)
{
    if (peer.isEmpty() || isSavedChat(peer)) {
        return;
    }
    book_.setBlocked(peer, blocked);
    ++contactsRevision_;
    emit contactsRevisionChanged();
    emit commands_.requestSetBlocked(peer, blocked);
}

bool SessionController::contactNotifications(const QString& peer) const
{
    return book_.state(peer).notifications;
}

bool SessionController::contactCalls(const QString& peer) const
{
    return book_.state(peer).calls;
}

void SessionController::setContactNotifications(const QString& peer, const bool on)
{
    if (peer.isEmpty()) {
        return;
    }
    book_.edit(peer).notifications = on;
    ++contactsRevision_;
    emit contactsRevisionChanged();
    emit commands_.requestSetContactNotifications(peer, on);
}

void SessionController::setContactCalls(const QString& peer, const bool allowed)
{
    if (peer.isEmpty()) {
        return;
    }
    book_.edit(peer).calls = allowed;
    ++contactsRevision_;
    emit contactsRevisionChanged();
    emit commands_.requestSetContactCalls(peer, allowed);
}

void SessionController::unblockBeforeWriting(const QString& peer)
{
    if (peer.isEmpty() || !isBlocked(peer)) {
        return;
    }
    setBlocked(peer, false);
}

void SessionController::sendText(const QString& text)
{
    if (activePeer_.isEmpty() || text.isEmpty()) {
        return;
    }
    const QString replyTo = replying_ ? replyingE2eId_ : QString();
    if (replying_) {
        cancelReply();
    }
    deliverText(text, replyTo);
}

void SessionController::sendOffered(const QString& text)
{
    if (activePeer_.isEmpty() || text.isEmpty()) {
        return;
    }
    deliverText(text, QString());
}

void SessionController::deliverText(const QString& text, const QString& replyTo)
{
    unblockBeforeWriting(activePeer_);
    emit commands_.sendText(activePeer_, text, replyTo, newE2eId());
}

QString SessionController::takeReply()
{
    const QString replyTo = replying_ ? replyingE2eId_ : QString();
    if (replying_) {
        cancelReply();
    }
    return replyTo;
}

void SessionController::sendFile(const QString& fileUrl)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (localPath.isEmpty() || activePeer_.isEmpty()) {
        return;
    }
    unblockBeforeWriting(activePeer_);
    emit commands_.sendFile(activePeer_, localPath, takeReply(), newE2eId());
}

void SessionController::sendPictureFile(const QString& fileUrl)
{
    const QUrl url(fileUrl);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : fileUrl;
    const PreparedPicture picture
        = preparePicture(QImage(localPath), QFileInfo(localPath).completeBaseName());
    if (picture.isEmpty()) {
        emit actionFailed(tr("that file is not a picture this can send"));
        return;
    }
    sendPreparedPicture(picture);
}

void SessionController::sendClipboardPicture()
{
    const QClipboard* const clipboard = QGuiApplication::clipboard();
    const PreparedPicture picture = preparePicture(
        clipboard == nullptr ? QImage() : clipboard->image(), QStringLiteral("pasted"));
    if (picture.isEmpty()) {
        emit actionFailed(tr("there is no picture in the clipboard"));
        return;
    }
    sendPreparedPicture(picture);
}

void SessionController::sendShot(QObject* const shot)
{
    auto* const held = qobject_cast<PhotoShot*>(shot);
    if (held == nullptr || held->picture().isEmpty()) {
        emit actionFailed(tr("there is no photograph to send"));
        return;
    }
    sendPreparedPicture(held->picture());
    held->discard();
}

void SessionController::sendPreparedPicture(const PreparedPicture& picture)
{
    if (activePeer_.isEmpty()) {
        return;
    }
    unblockBeforeWriting(activePeer_);
    const QString e2eId = newE2eId();
    PictureStore::instance().put(e2eId, picture.bytes);
    emit commands_.sendPicture(
        activePeer_, picture.bytes, picture.name, picture.mime, takeReply(), e2eId);
}

void SessionController::sendCallback(
    const QString& data, const QString& refMsgId, const QString& label)
{
    if (!activePeer_.isEmpty()) {
        emit commands_.sendCallback(activePeer_, data, refMsgId, label);
    }
}

void SessionController::sendCommand(
    const QString& command, const QString& args, const QString& label)
{
    if (!activePeer_.isEmpty() && !command.isEmpty()) {
        emit commands_.sendCommand(activePeer_, command, args, label);
    }
}

void SessionController::beginEdit(qint64 localId, const QString& e2eId, const QString& text)
{
    if (replying_) {
        cancelReply();
    }
    editing_ = true;
    editingLocalId_ = localId;
    editingE2eId_ = e2eId;
    editingText_ = text;
    emit editingChanged();
}

void SessionController::beginReply(
    const QString& e2eId, const QString& previewText, const QString& sender)
{
    if (e2eId.isEmpty()) {
        return;
    }
    if (editing_) {
        cancelEdit();
    }
    replying_ = true;
    replyingE2eId_ = e2eId;
    replyingText_ = previewText;
    replyingSender_ = sender;
    emit replyingChanged();
}

void SessionController::cancelReply()
{
    if (!replying_) {
        return;
    }
    replying_ = false;
    replyingE2eId_.clear();
    replyingText_.clear();
    replyingSender_.clear();
    emit replyingChanged();
}

QVariantMap SessionController::replyPreview(const QString& e2eId) const
{
    QVariantMap info;
    info[QStringLiteral("found")] = false;
    info[QStringLiteral("localId")] = 0;
    info[QStringLiteral("text")] = QString();
    info[QStringLiteral("sender")] = QString();
    if (e2eId.isEmpty() || activePeer_.isEmpty()) {
        return info;
    }
    const StoredMessage m = store_.messageByE2e(e2eId, activePeer_);
    if (m.id == 0) {
        return info;
    }
    info[QStringLiteral("found")] = true;
    info[QStringLiteral("localId")] = m.id;
    QString preview = m.text;
    if (preview.isEmpty() && !m.attName.isEmpty()) {
        preview = QStringLiteral("\xF0\x9F\x93\x8E ") + m.attName;
    }
    info[QStringLiteral("text")] = preview;
    if (m.outgoing) {
        info[QStringLiteral("sender")] = QStringLiteral("You");
    } else if (!m.author.isEmpty()) {
        info[QStringLiteral("sender")] = m.author;
    } else {
        info[QStringLiteral("sender")] = peerName(activePeer_);
    }
    return info;
}

void SessionController::commitEdit(const QString& newText)
{
    if (!editing_) {
        return;
    }
    const QString trimmed = newText.trimmed();
    if (!trimmed.isEmpty() && trimmed != editingText_) {
        emit commands_.editMessage(activePeer_, editingLocalId_, editingE2eId_, trimmed);
    }
    cancelEdit();
}

void SessionController::cancelEdit()
{
    if (!editing_) {
        return;
    }
    editing_ = false;
    editingLocalId_ = 0;
    editingE2eId_.clear();
    editingText_.clear();
    emit editingChanged();
}

void SessionController::deleteMessage(qint64 localId, const QString& e2eId, bool outgoing)
{
    if (activePeer_.isEmpty() || localId == 0) {
        return;
    }
    emit commands_.deleteMessage(activePeer_, localId, e2eId, outgoing);
}

void SessionController::keepThisDeviceAddress()
{
    emit commands_.requestPublishThisDeviceAddress();
}

void SessionController::useFreshAddress()
{
    emit commands_.requestPublishFreshAddress();
}

void SessionController::signLogin(const QString& challenge)
{
    emit commands_.requestSignLoginSig(challenge);
}

void SessionController::askForContacts()
{
    emit commands_.requestContactsFromDevices();
}

void SessionController::refreshConnectionLog()
{
    emit commands_.requestConnectionLog();
}

void SessionController::clearConnectionLog()
{
    emit commands_.requestClearConnectionLog();
}

QVariantMap SessionController::describeLoginChallenge(const QString& challenge) const
{
    QVariantMap described;
    try {
        const bazarish::service::LoginConsumer consumer
            = bazarish::service::readLoginConsumer(challenge.trimmed().toStdString());
        described["ok"] = true;
        described["name"] = QString::fromStdString(consumer.name);
        QStringList places;
        for (const std::string& place : consumer.place) {
            places << QString::fromStdString(place);
        }
        described["place"] = places;
        described["role"] = QString::fromStdString(consumer.role);
    } catch (const std::exception& error) {
        described["ok"] = false;
        described["problem"] = QString::fromUtf8(error.what());
    }
    return described;
}

void SessionController::changePassphrase(const QString& passphrase)
{
    emit commands_.requestChangePassphrase(passphrase);
}

void SessionController::startPairing()
{
    pairUri_.clear();
    pairCode_.clear();
    pairStatus_ = tr("Making an address");
    pairProgress_ = kProgressUnknown;
    pairing_ = true;
    emit pairingChanged();
    emit commands_.requestStartPairing();
}

void SessionController::stopPairing()
{
    pairing_ = false;
    pairUri_.clear();
    pairCode_.clear();
    pairStatus_.clear();
    pairProgress_ = kProgressUnknown;
    emit pairingChanged();
    emit commands_.requestStopPairing();
}

void SessionController::onPairOfferReady(const QString& uri, const QString& code)
{
    pairUri_ = uri;
    pairCode_ = code;
    emit pairingChanged();
}

void SessionController::onPairStage(const QString& status, const double progress)
{
    if (!pairing_) {
        return;
    }
    pairStatus_ = status;
    pairProgress_ = progress;
    emit pairingChanged();
}

void SessionController::onPairFinished(const bool ok, const QString& status)
{
    if (!pairing_) {
        return;
    }
    pairing_ = false;
    pairUri_.clear();
    pairCode_.clear();
    pairStatus_ = status;
    pairProgress_ = kProgressUnknown;
    emit pairingChanged();
    emit pairingFinished(ok);
}

void SessionController::exportAccount(const QString& fileUrl, const QString& password)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (localPath.isEmpty()) {
        return;
    }
    emit commands_.requestExport(localPath, password);
}

QString SessionController::shortFingerprint(const QString& fp) const
{
    return ContactBook::shortFingerprint(fp);
}

void SessionController::setChatFilter(const QString& text)
{
    contactsProxy_.setFilterFixedString(text.trimmed());
}

void SessionController::pinChat(const QString& peer, bool pinned)
{
    if (!peer.isEmpty()) {
        emit commands_.pinChat(peer, pinned);
    }
}

void SessionController::generatePersonalKey()
{
    emit commands_.requestGeneratePersonalKey();
}

void SessionController::loadPersonalKey(const QString& fileUrl)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (!localPath.isEmpty()) {
        emit commands_.requestLoadPersonalKey(localPath);
    }
}

void SessionController::replacePersonalKey()
{
    emit commands_.requestReplacePersonalKey();
}

void SessionController::setAliasBinding(const QString& alias, const bool on)
{
    emit commands_.requestSetAliasBinding(alias, on);
}

void SessionController::retryRoutingTo(const QString& peer)
{
    emit commands_.requestRetryRoutingTo(peer);
}

void SessionController::setDelegationDays(const int days)
{
    const int bounded = std::clamp(days, minDelegationDays(), maxDelegationDays());
    if (delegationDays_ == bounded) {
        return;
    }
    delegationDays_ = bounded;
    emit commands_.requestSetDelegationDays(bounded);
    emit delegationDaysChanged();
}

void SessionController::setAcceptCalls(const bool on)
{
    if (acceptCalls_ == on) {
        return;
    }
    acceptCalls_ = on;
    emit commands_.requestSetAcceptCalls(on);
    emit acceptCallsChanged();
}

void SessionController::setSendReceipts(const bool on)
{
    if (sendReceipts_ == on) {
        return;
    }
    sendReceipts_ = on;
    emit commands_.requestSetSendReceipts(on);
    emit sendReceiptsChanged();
}

void SessionController::publishPersonalDest()
{
    i2pBusy_ = true;
    flashOnNextStatus_ = true;
    emit i2pStatusChanged();
    emit commands_.requestPublishPersonalDest();
}

void SessionController::disablePersonalDest()
{
    i2pBusy_ = true;
    flashOnNextStatus_ = true;
    emit i2pStatusChanged();
    emit commands_.requestDisablePersonalDest();
}

void SessionController::refreshI2pStatus()
{
    emit commands_.requestRefreshI2pStatus();
}

void SessionController::onI2pKeyState(const bool hasKey, const QString& address)
{
    i2pHasKey_ = hasKey;
    i2pAddress_ = address;
    emit i2pStatusChanged();
}

void SessionController::onI2pStatus(const bool hasKey, const bool delegated, const bool live,
    const QString& address, const QString& summary, const qint64 transientExpires,
    const QString& serverState)
{
    i2pBusy_ = false;
    const bool flash = flashOnNextStatus_;
    flashOnNextStatus_ = false;
    i2pServerState_ = serverState;
    i2pHasKey_ = hasKey;
    i2pEnabled_ = delegated;
    i2pActive_ = live;
    i2pAddress_ = address;
    i2pStatusText_ = summary;
    i2pTransientExpires_ = transientExpires;
    emit i2pStatusChanged();
    if (flash) {
        emit i2pAnswered();
    }
}

void SessionController::refreshStorageUsage()
{
    emit commands_.requestRefreshStorageUsage();
}

void SessionController::refreshDevices()
{
    emit commands_.requestRefreshDevices();
}

void SessionController::forgetDevice(const QString& clientId)
{
    emit commands_.requestForgetDevice(clientId);
}

void SessionController::closeAccountOnServer()
{
    emit commands_.requestCloseAccountOnServer();
}

void SessionController::forwardMessage(const QString& e2eId, const QString& toPeer)
{
    if (e2eId.isEmpty() || toPeer.isEmpty()) {
        return;
    }
    unblockBeforeWriting(toPeer);
    emit commands_.forwardMessage(activePeer_, e2eId, toPeer, newE2eId());
}

void SessionController::requestPicturesFor(const QList<StoredMessage>& messages)
{
    for (const StoredMessage& message : messages) {
        if (!message.hasPicture || message.e2eId.isEmpty()) {
            continue;
        }
        pictureOwners_.insert(message.e2eId, message.id);
        if (PictureStore::instance().has(message.e2eId)) {
            continue;
        }
        const QByteArray bytes = store_.media(QStringLiteral("picture:") + message.e2eId);
        if (bytes.isEmpty() || !PictureStore::instance().put(message.e2eId, bytes)) {
            conversation_.setPictureReadyForId(message.id, false);
        }
    }
}

void SessionController::savePictureAs(const QString& e2eId, const QString& fileUrl)
{
    const QString path = QUrl(fileUrl).toLocalFile();
    const QByteArray bytes = PictureStore::instance().bytes(e2eId);
    if (path.isEmpty() || bytes.isEmpty()) {
        emit actionFailed(tr("This picture is not here to save."));
        return;
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size()) {
        emit actionFailed(tr("Could not write %1").arg(path));
        return;
    }
    emit actionOk(tr("Picture saved"));
}

void SessionController::copyPicture(const QString& e2eId)
{
    const QImage picture = PictureStore::instance().image(e2eId);
    if (picture.isNull()) {
        emit actionFailed(tr("This picture is not here to copy."));
        return;
    }
    QClipboard* const clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        emit actionFailed(tr("There is no clipboard to copy to."));
        return;
    }
    clipboard->setImage(picture);
    emit actionOk(tr("Picture copied"));
}

QUrl SessionController::defaultPictureSaveUrl(const QString& e2eId, const QString& name) const
{
    const QByteArray bytes = PictureStore::instance().bytes(e2eId);
    const QString suffix = bytes.startsWith(QByteArray::fromHex("89504E47"))
        ? QStringLiteral(".png") : QStringLiteral(".jpg");
    const QString base = name.isEmpty() ? QStringLiteral("picture") : QFileInfo(name).completeBaseName();
    return defaultSaveUrl(base + suffix);
}

void SessionController::onDevicesReady(const QVariantList& devices)
{
    devices_ = devices;
    emit devicesChanged();
}

void SessionController::onStorageUsageReady(
    const bool mailboxOk, const qulonglong mailboxUsed, const qulonglong mailboxQuota)
{
    if (!mailboxOk) {
        return;
    }
    storageMailboxOk_ = true;
    storageMailboxUsed_ = mailboxUsed;
    storageMailboxQuota_ = mailboxQuota;
    storageUpdatedAtMs_ = nowMillis();
    emit storageChanged();
}

QVariantMap SessionController::storageInfo() const
{
    QVariantMap m;
    m[QStringLiteral("mailboxOk")] = storageMailboxOk_;
    m[QStringLiteral("mailboxUsed")] = static_cast<qulonglong>(storageMailboxUsed_);
    m[QStringLiteral("mailboxQuota")] = static_cast<qulonglong>(storageMailboxQuota_);
    m[QStringLiteral("updatedAt")] = storageUpdatedAtMs_;
    m[QStringLiteral("everFetched")] = (storageUpdatedAtMs_ > 0);
    return m;
}

void SessionController::measureDeviceStorage()
{
    const QVector<ChatWeight> weights = store_.chatWeights();
    QVariantList chats;
    QVector<ChatWeight> ordered = weights;
    std::sort(ordered.begin(), ordered.end(), [](const ChatWeight& a, const ChatWeight& b) {
        return a.rowBytes + a.mediaBytes > b.rowBytes + b.mediaBytes;
    });
    for (const ChatWeight& weight : ordered) {
        QVariantMap chat;
        chat[QStringLiteral("peer")] = weight.peer;
        chat[QStringLiteral("name")] = peerName(weight.peer);
        chat[QStringLiteral("messages")] = weight.messages;
        chat[QStringLiteral("bytes")] = weight.rowBytes + weight.mediaBytes;
        chat[QStringLiteral("mediaCount")] = weight.mediaCount;
        chats << chat;
    }
    const DatabaseFootprint footprint = store_.footprint();
    deviceStorage_.clear();
    deviceStorage_[QStringLiteral("busy")] = deviceStorageBusy_;
    deviceStorage_[QStringLiteral("measuredAt")] = nowMillis();
    deviceStorage_[QStringLiteral("fileBytes")] = footprint.fileBytes;
    deviceStorage_[QStringLiteral("freeBytes")] = footprint.freeBytes;
    deviceStorage_[QStringLiteral("chats")] = chats;
    emit deviceStorageChanged();
}

void SessionController::trimChat(const QString& peer, const int keep)
{
    if (peer.isEmpty()) {
        return;
    }
    runTrim(peer, keep);
}

void SessionController::trimEveryChat(const int keep)
{
    runTrim({}, keep);
}

void SessionController::beginStorageWork(const QString& what, const std::function<void()>& work)
{
    deviceStorageBusy_ = true;
    deviceStorage_[QStringLiteral("busy")] = true;
    deviceStorage_[QStringLiteral("busyWhat")] = what;
    emit deviceStorageChanged();
    QTimer::singleShot(kBusyPaintDelayMs, this, work);
}

void SessionController::endStorageWork()
{
    deviceStorageBusy_ = false;
    measureDeviceStorage();
}

void SessionController::compactDatabase()
{
    if (deviceStorageBusy_) {
        return;
    }
    beginStorageWork(tr("Compacting the database"), [this]() { emit commands_.compact(); });
}

void SessionController::runTrim(const QString& peer, const int keep)
{
    if (deviceStorageBusy_) {
        return;
    }
    beginStorageWork(tr("Trimming and compacting the database"),
        [this, peer, keep]() { emit commands_.trim(peer, keep); });
}

void SessionController::onOpened(
    const QString& fingerprint, const QString& displayName, bool connected)
{
    fingerprint_ = fingerprint;
    book_.setOwnFingerprint(fingerprint);
    displayName_ = displayName;
    connected_ = connected;
    emit identityChanged();
    emit connectedChanged();
    if (const bool nowOnline = connected && startOnline_; online_ != nowOnline) {
        online_ = nowOnline;
        emit onlineChanged();
    }
}

void SessionController::onConnectionChanged(bool connected, const QString& connectionNote)
{
    connecting_ = false;
    connectPhase_.clear();
    connectError_ = connected ? QString() : connectionNote;
    emit connectStateChanged();
    connected_ = connected;
    emit connectedChanged();
    if (online_ != connected) {
        online_ = connected;
        emit onlineChanged();
    }
}

void SessionController::goOnline()
{
    startOnline_ = true;
    if (!online_) {
        online_ = true;
        emit onlineChanged();
    }
    emit commands_.requestSetSync(true);
}

void SessionController::rebuildI2pLinks()
{
    linksRebuilding_ = true;
    rebuildGrace_.start();
    emit commands_.requestRebuildI2p();
}

void SessionController::goOffline()
{
    if (online_) {
        online_ = false;
        emit onlineChanged();
    }
    if (reachable_) {
        reachable_ = false;
        emit reachableChanged();
    }
    emit commands_.requestSetSync(false);
}

void SessionController::onSyncReachable(const bool ok)
{
    if (ok) {
        linksRebuilding_ = false;
        rebuildGrace_.stop();
    } else if (linksRebuilding_) {
        return;
    }
    if (reachable_ == ok) {
        return;
    }
    reachable_ = ok;
    emit reachableChanged();
}

void SessionController::onApprovalState(const bool pending, const QString& note)
{
    if (awaitingApproval_ == pending && approvalNote_ == note) {
        return;
    }
    awaitingApproval_ = pending;
    approvalNote_ = note;
    emit approvalChanged();
}

void SessionController::resendText(qint64 localId, const QString& text, const QString& e2eId)
{
    Q_UNUSED(text);
    Q_UNUSED(e2eId);
    if (!activePeer_.isEmpty()) {
        emit commands_.resend(activePeer_, localId);
    }
}

void SessionController::resendFile(qint64 localId, const QString& e2eId)
{
    Q_UNUSED(e2eId);
    if (!activePeer_.isEmpty()) {
        emit commands_.resend(activePeer_, localId);
    }
}

void SessionController::resendVoice(qint64 localId, const QString& e2eId)
{
    Q_UNUSED(e2eId);
    if (!activePeer_.isEmpty()) {
        emit commands_.resend(activePeer_, localId);
    }
}

void SessionController::resendPicture(qint64 localId, const QString& e2eId)
{
    Q_UNUSED(e2eId);
    if (!activePeer_.isEmpty()) {
        emit commands_.resend(activePeer_, localId);
    }
}

void SessionController::markReadThroughRow(int row)
{
    if (activePeer_.isEmpty() || row < 0) {
        return;
    }
    qint64 id = 0;
    qint64 sentAt = 0;
    QString e2eId;
    if (!conversation_.newestIncomingThrough(row, id, e2eId, sentAt)) {
        return;
    }
    emit commands_.markRead(activePeer_, id, e2eId, sentAt);
}


}  // namespace bazarish::app
