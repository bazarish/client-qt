// Bazarish project (c) 2026
#include "ContactBook.hpp"
#include "DeliveryStatus.hpp"
#include "MessageIngest.hpp"
#include "SystemNotes.hpp"
#include "TranscriptStore.hpp"
#include "TestUtil.hpp"

#include "AccountDb.hpp"
#include "AccountKey.hpp"
#include "Session.hpp"

#include <bazarish/Bytes.hpp>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <sqlcipher/sqlite3.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace bazarish;
using namespace bazarish::app;

namespace {

const QString kMe = QStringLiteral("me");
const QString kBob = QStringLiteral("bob");
const QString kClub = QStringLiteral("club");
constexpr qint64 kSentAt = 1000;

client::IncomingMessage incoming(const QString& peer, const QString& type, const QString& e2eId,
    const QString& text = {}, const QString& ref = {})
{
    client::IncomingMessage m;
    m.fromFingerprint = peer.toStdString();
    m.contentType = type.toStdString();
    m.e2eId = e2eId.toStdString();
    m.text = text.toStdString();
    m.refId = ref.toStdString();
    m.sentAt = kSentAt;
    return m;
}

client::IncomingMessage ours(const QString& peer, const QString& type, const QString& e2eId,
    const QString& text = {})
{
    client::IncomingMessage m = incoming(peer, type, e2eId, text);
    m.sentByUs = true;
    return m;
}

}  // namespace

int main(int argc, char** argv)
try {
    const QCoreApplication app(argc, argv);
    namespace fs = std::filesystem;
    const testutil::ScratchDir scratch("ingest");
    const fs::path& dir = scratch.path();
    const QString db = QString::fromStdString((dir / "account.db").string());

    {
        const client::AccountDb account(dir / "account.db", {});
    }
    TranscriptStore store;
    CHECK(store.open(db));
    ContactBook book;
    book.setOwnFingerprint(kMe);
    ContactState bob;
    bob.fingerprint = kBob;
    bob.name = QStringLiteral("Bob");
    book.reset({bob}, {});
    MessageIngest ingest(store, book);

    std::vector<std::string> seen;
    const auto note = [&seen](const std::string& what) { seen.push_back(what); };
    QObject::connect(&ingest, &MessageIngest::rowAdded, [&](const QString&, qint64, bool own) {
        note(own ? "added-own" : "added");
    });
    QObject::connect(&ingest, &MessageIngest::rowChanged, [&] { note("changed"); });
    QObject::connect(&ingest, &MessageIngest::rowRemoved, [&] { note("removed"); });
    QObject::connect(&ingest, &MessageIngest::peerReset, [&] { note("reset"); });
    QObject::connect(&ingest, &MessageIngest::chatTimerChanged, [&] { note("timer"); });
    QObject::connect(&ingest, &MessageIngest::botMenuChanged, [&] { note("menu"); });
    QObject::connect(&ingest, &MessageIngest::chatsChanged, [&] { note("chats"); });
    QObject::connect(&ingest, &MessageIngest::deliveredThrough, [&] { note("delivered"); });
    QObject::connect(&ingest, &MessageIngest::reactionArrived, [&] { note("reaction"); });
    QString countedEmoji;
    QObject::connect(&ingest, &MessageIngest::groupReactionArrived,
        [&](const QString&, const QString&, const QString& emoji) {
            note("group-reaction");
            countedEmoji = emoji;
        });
    QObject::connect(&ingest, &MessageIngest::groupChanged, [&] { note("group"); });
    QObject::connect(&ingest, &MessageIngest::readElsewhere, [&] { note("read"); });
    QObject::connect(&ingest, &MessageIngest::editArrived, [&] { note("edit"); });
    QObject::connect(&ingest, &MessageIngest::messageArrived, [&] { note("notify"); });
    QObject::connect(&ingest, &MessageIngest::memberListWanted, [&] { note("ask"); });
    QObject::connect(&ingest, &MessageIngest::memberListChanged, [&] { note("members"); });
    qint64 arrivedId = 0;
    QObject::connect(&ingest, &MessageIngest::messageArrived,
        [&](const QString&, const qint64 id) { arrivedId = id; });
    const auto took = [&](const client::IncomingMessage& message) {
        seen.clear();
        return ingest.takeIn(message);
    };
    const auto saw = [&](const std::vector<std::string>& expected) { return seen == expected; };

    CHECK(took(incoming(kBob, "text", "m1", "hello")));
    CHECK(saw({"added", "notify"}));
    CHECK(store.latestMessages(kBob, 10).size() == 1);

    CHECK(took(incoming(kBob, "text", "m1", "hello")));
    CHECK(saw({}));
    CHECK(store.latestMessages(kBob, 10).size() == 1);

    CHECK(store.keepMedia(QStringLiteral("file:f1"), QByteArrayLiteral("carried")));
    client::IncomingMessage carried = incoming(kBob, "file", "f1");
    carried.attachmentName = "a.txt";
    CHECK(took(carried));
    CHECK(store.messageByE2e("f1", kBob).hasPicture);
    client::IncomingMessage offered = incoming(kBob, "file", "f2");
    offered.attachmentName = "b.txt";
    CHECK(took(offered));
    CHECK(!store.messageByE2e("f2", kBob).hasPicture);
    CHECK(store.latestMessages(kBob, 10).size() == 3);

    CHECK(took(ours(kBob, "text", "o1", "from my other device")));
    CHECK(saw({"added"}));
    const qint64 o1 = store.idForKey(kBob, "o1", true);
    CHECK(store.messageById(o1).status == DeliveryStatus::AtRecipientServer);

    CHECK(took(incoming(kBob, "receipt", "r1", {}, "o2")));
    CHECK(saw({}));
    CHECK(took(ours(kBob, "text", "o2", "read before it arrived")));
    CHECK(saw({"delivered", "added"}));
    CHECK(store.messageById(store.idForKey(kBob, "o2", true)).status == DeliveryStatus::Delivered);

    CHECK(took(incoming(kBob, "receipt", "r2", {}, "o1")));
    CHECK(saw({"delivered"}));
    CHECK(store.messageById(o1).status == DeliveryStatus::Delivered);

    CHECK(took(incoming(kBob, "edit", "e1", "hello again", "m1")));
    CHECK(saw({"changed", "edit"}));
    const StoredMessage edited = store.messageByE2e("m1", kBob);
    CHECK(edited.text == QStringLiteral("hello again"));
    CHECK(edited.edited);

    CHECK(took(incoming(kBob, "reaction", "x1", QStringLiteral("+"), "o1")));
    CHECK(saw({"reaction"}));
    CHECK(store.reactionsFor(kBob, "o1").size() == 1);

    CHECK(took(incoming(kBob, "delete", "d1", {}, "m1")));
    CHECK(saw({"removed"}));
    CHECK(store.messageByE2e("m1", kBob).id == 0);

    client::IncomingMessage read = ours(kBob, "device.read", "dr1", QString::number(kSentAt));
    read.refId = kBob.toStdString();
    CHECK(took(read));
    CHECK(saw({"read"}));

    client::IncomingMessage pin = ours(kMe, "device.chat-pin", "dp1", QStringLiteral("1"));
    pin.refId = kBob.toStdString();
    CHECK(took(pin));
    CHECK(saw({"chats"}));
    CHECK(store.isPinned(kBob));

    CHECK(took(incoming(kBob, "chat.clear", "c1")));
    CHECK(saw({"reset"}));
    const QVector<StoredMessage> left = store.latestMessages(kBob, 10);
    CHECK(left.size() == 1);
    CHECK(left.front().type == QStringLiteral("system"));

    client::IncomingMessage post = incoming(kClub, "text", "g1", "in the group");
    post.group = client::GroupBlock{client::GroupKind::eGroup, 3, true, false, kSentAt};
    post.author = "Ann";
    post.contactable = true;
    CHECK(took(post));
    CHECK(saw({"group", "ask", "added", "notify"}));
    CHECK(store.groupState(kClub).has_value());
    CHECK(store.messageByE2e("g1", kClub).author == QStringLiteral("Ann"));
    CHECK(arrivedId == store.messageByE2e("g1", kClub).id);
    CHECK(!store.messageById(arrivedId).mentionsMe);

    client::IncomingMessage roster = incoming(kClub, "group.members", "gm1");
    roster.group = post.group;
    client::MemberListLetter letter;
    letter.op = client::MemberListOp::eFull;
    letter.names = {"Ann", "Me Too"};
    letter.total = 2;
    letter.part = 1;
    letter.parts = 1;
    letter.you = "Me Too";
    roster.memberList = letter;
    CHECK(took(roster));
    CHECK(saw({"group", "members"}));
    CHECK(store.memberListState(kClub) == client::MemberListState::eHeld);
    CHECK(store.groupMembers(kClub) == QStringList({QStringLiteral("Ann"), QStringLiteral("Me Too")}));
    CHECK(store.ownGroupName(kClub) == QStringLiteral("Me Too"));

    client::IncomingMessage called = post;
    called.e2eId = "g2";
    called.text = "hi @Me_Too!";
    CHECK(took(called));
    CHECK(saw({"group", "added", "notify"}));
    CHECK(arrivedId == store.messageByE2e("g2", kClub).id);
    CHECK(store.messageById(arrivedId).mentionsMe);
    CHECK(store.unreadMentionCount(kClub) == 1);
    CHECK(store.firstUnreadMentionId(kClub) == arrivedId);
    client::IncomingMessage named = post;
    named.e2eId = "g3";
    named.text = "hi Me Too";
    CHECK(took(named));
    CHECK(!store.messageById(arrivedId).mentionsMe);

    client::IncomingMessage counted = incoming(kClub, "group.reactions", "gr1");
    counted.group = post.group;
    counted.groupReactionsJson = R"([{"ref":"g1","counts":{"👍":1}}])";
    CHECK(took(counted));
    CHECK(saw({"group", "group-reaction"}));
    CHECK(countedEmoji == QStringLiteral("👍"));

    client::IncomingMessage future = incoming(kBob, "unsupported", "u1");
    future.rawType = "poll.vote";
    const QByteArray kept = QByteArray::fromBase64("oWR0eXBlaXBvbGwudm90ZQ==");
    future.rawBody = Bytes(kept.begin(), kept.end());
    CHECK(took(future));
    CHECK(saw({"added"}));
    const StoredMessage unknown = store.messageByE2e("u1", kBob);
    CHECK(unknown.type == QStringLiteral("unsupported"));
    CHECK(!unknown.outgoing);
    CHECK(systemNoteText(unknown.text).contains(QStringLiteral("poll.vote")));
    CHECK(QJsonDocument::fromJson(unknown.text.toUtf8()).object().value(QStringLiteral("kept"))
        == QStringLiteral("oWR0eXBlaXBvbGwudm90ZQ=="));
    CHECK(took(future));
    CHECK(saw({}));
    client::IncomingMessage worded = incoming(kBob, "unsupported", "u2", "a question for everyone");
    worded.rawType = "poll";
    CHECK(took(worded));
    CHECK(saw({"added"}));
    CHECK(store.messageByE2e("u2", kBob).type == QStringLiteral("unsupported"));

    CHECK(took(incoming(kBob, "unreadable", "x1")));
    CHECK(saw({"added"}));
    const StoredMessage unreadable = store.messageByE2e("x1", kBob);
    CHECK(unreadable.type == QStringLiteral("system"));
    CHECK(!unreadable.outgoing);
    CHECK(systemNoteText(unreadable.text) == QStringLiteral("A message could not be read"));

    client::IncomingMessage timer = incoming(kBob, "chat.timer", "t1");
    timer.timerSeconds = bazarish::kSecondsPerDay;
    CHECK(took(timer));
    CHECK(saw({"timer", "added"}));
    CHECK(store.chatTimer(kBob) == bazarish::kSecondsPerDay);
    const StoredMessage plate = store.messageByE2e("t1", kBob);
    CHECK(plate.type == QStringLiteral("system"));
    CHECK(systemNoteText(plate.text).contains(QStringLiteral("Bob")));
    CHECK(took(timer));
    CHECK(saw({}));
    client::IncomingMessage off = ours(kBob, "chat.timer", "t2");
    off.timerSeconds = client::kChatTimerOff;
    CHECK(took(off));
    CHECK(saw({"timer", "added-own"}));
    CHECK(store.chatTimer(kBob) == client::kChatTimerOff);
    client::IncomingMessage menu = incoming(kBob, "bot.menu", "bm1");
    menu.menuItemsJson = R"([{"title":"Help","command":"help"}])";
    CHECK(took(menu));
    CHECK(saw({"menu"}));
    CHECK(store.botMenu(kBob).size() == 1);
    CHECK(store.messageByE2e("bm1", kBob).id == 0);
    client::IncomingMessage gone = incoming(kBob, "bot.menu", "bm2");
    gone.menuItemsJson = "[]";
    CHECK(took(gone));
    CHECK(saw({"menu"}));
    CHECK(store.botMenu(kBob).isEmpty());

    // A reply stamped by a clock behind ours still goes under what we had written.
    const QString carl = QStringLiteral("carl");
    constexpr qint64 kOwnAt = 5 * kSentAt;
    StoredMessage asked;
    asked.peer = carl;
    asked.outgoing = true;
    asked.type = QStringLiteral("text");
    asked.text = QStringLiteral("/help");
    asked.e2eId = QStringLiteral("q1");
    asked.ts = kOwnAt;
    asked.orderKey = kOwnAt;
    CHECK(store.append(asked) != 0);
    CHECK(took(incoming(carl, "text", "a1", "help text")));
    const StoredMessage answer = store.messageByE2e("a1", carl);
    CHECK(answer.orderKey == kOwnAt + 1 && answer.ts == kSentAt);
    client::IncomingMessage later = incoming(carl, "text", "a2", "later");
    later.sentAt = 2 * kOwnAt;
    CHECK(took(later));
    CHECK(store.messageByE2e("a2", carl).orderKey == 2 * kOwnAt);
    CHECK(took(ours(carl, "text", "q2", "from another device")));
    CHECK(store.messageByE2e("q2", carl).orderKey == 2 * kOwnAt + 1);

    StoredMessage newer = asked;
    newer.e2eId = QStringLiteral("q3");
    newer.ts = 3 * kOwnAt;
    newer.orderKey = 3 * kOwnAt;
    CHECK(store.append(newer) != 0);
    client::IncomingMessage anchored = incoming(carl, "text", "a3", "late");
    anchored.after = "q1";
    CHECK(took(anchored));
    CHECK(store.messageByE2e("a3", carl).orderKey == kOwnAt + 1);
    client::IncomingMessage stranger = incoming(carl, "text", "a4", "after a stranger");
    stranger.after = "gone";
    CHECK(took(stranger));
    CHECK(store.messageByE2e("a4", carl).orderKey == 3 * kOwnAt + 1);
    client::IncomingMessage echoed = ours(carl, "text", "q4", "under a2");
    echoed.after = "a2";
    CHECK(took(echoed));
    CHECK(store.messageByE2e("q4", carl).orderKey == 2 * kOwnAt + 1);

    client::IncomingMessage fromHost = incoming(kClub, "chat.timer", "t3");
    fromHost.timerSeconds = bazarish::kSecondsPerHour;
    CHECK(took(fromHost));
    CHECK(saw({}));
    CHECK(store.chatTimer(kClub) == client::kChatTimerOff);

    const SecretBytes key = client::accountkey::keyFor(db.toStdString(), {});
    sqlite3* writer = nullptr;
    CHECK(sqlite3_open(db.toUtf8().constData(), &writer) == SQLITE_OK);
    const std::string keying = "PRAGMA key = \"x'" + toHex(key.bytes()) + "'\"";
    CHECK(sqlite3_exec(writer, keying.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    CHECK(sqlite3_exec(writer, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr) == SQLITE_OK);
    CHECK(!took(incoming(kBob, "text", "m9", "while the file is held")));
    CHECK(saw({}));
    CHECK(!store.lastError().isEmpty());
    CHECK(sqlite3_exec(writer, "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK);
    CHECK(sqlite3_close(writer) == SQLITE_OK);
    CHECK(took(incoming(kBob, "text", "m9", "while the file is held")));
    CHECK(saw({"added", "notify"}));

    store.close();
    std::fprintf(stderr, "TestIngest passed\n");
    return 0;
} catch (...) {
    return testutil::failed();
}
