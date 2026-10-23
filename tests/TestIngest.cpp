// Bazarish project (c) 2026
#include "ContactBook.hpp"
#include "DeliveryStatus.hpp"
#include "MessageIngest.hpp"
#include "SystemNotes.hpp"
#include "TranscriptStore.hpp"

#include "AccountKey.hpp"
#include "Session.hpp"

#include <bazarish/Bytes.hpp>

#include <QCoreApplication>

#include <sqlcipher/sqlite3.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition);                                                        \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)

using namespace bazarish;
using namespace bazarish::app;

namespace {

const QString kMe = QStringLiteral("me");
const QString kBob = QStringLiteral("bob");
const QString kClub = QStringLiteral("club");
constexpr qint64 kSentAt = 1000;

QVariantMap incoming(const QString& peer, const QString& type, const QString& e2eId,
    const QString& text = {}, const QString& ref = {})
{
    return QVariantMap{{"peer", peer}, {"type", type}, {"e2eId", e2eId}, {"text", text},
        {"ref", ref}, {"sentAt", kSentAt}, {"sentByUs", false}};
}

QVariantMap ours(const QString& peer, const QString& type, const QString& e2eId,
    const QString& text = {})
{
    QVariantMap m = incoming(peer, type, e2eId, text);
    m["sentByUs"] = true;
    return m;
}

}  // namespace

int main(int argc, char** argv)
{
    const QCoreApplication app(argc, argv);
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("bz-ingest-" + toHex(randomBytes(8)));
    fs::create_directories(dir);
    const QString db = QString::fromStdString((dir / "account.db").string());

    TranscriptStore store;
    CHECK(store.open({}, db, {}));
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
    QObject::connect(&ingest, &MessageIngest::groupChanged, [&] { note("group"); });
    QObject::connect(&ingest, &MessageIngest::readElsewhere, [&] { note("read"); });
    QObject::connect(&ingest, &MessageIngest::editArrived, [&] { note("edit"); });
    QObject::connect(&ingest, &MessageIngest::messageArrived, [&] { note("notify"); });
    const auto took = [&](const QVariantMap& message) {
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

    CHECK(took(ours(kBob, "text", "o1", "from my other device")));
    CHECK(saw({"added"}));
    const qint64 o1 = store.idForE2e("o1");
    CHECK(store.messageById(o1).status == DeliveryStatus::AtRecipientServer);

    CHECK(took(incoming(kBob, "receipt", "r1", {}, "o2")));
    CHECK(saw({}));
    CHECK(took(ours(kBob, "text", "o2", "read before it arrived")));
    CHECK(saw({"delivered", "added"}));
    CHECK(store.messageById(store.idForE2e("o2")).status == DeliveryStatus::Delivered);

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

    QVariantMap read = ours(kBob, "device.read", "dr1", QString::number(kSentAt));
    read["ref"] = kBob;
    CHECK(took(read));
    CHECK(saw({"read"}));

    QVariantMap pin = ours(kMe, "device.chat-pin", "dp1", QStringLiteral("1"));
    pin["ref"] = kBob;
    CHECK(took(pin));
    CHECK(saw({"chats"}));
    CHECK(store.isPinned(kBob));

    CHECK(took(incoming(kBob, "chat.clear", "c1")));
    CHECK(saw({"reset"}));
    const QVector<StoredMessage> left = store.latestMessages(kBob, 10);
    CHECK(left.size() == 1);
    CHECK(left.front().type == QStringLiteral("system"));

    QVariantMap post = incoming(kClub, "text", "g1", "in the group");
    post["group"] = QVariantMap{{"kind", "group"}, {"members", 3}, {"write", true},
        {"admin", false}, {"at", kSentAt}};
    post["author"] = QStringLiteral("Ann");
    post["contactable"] = true;
    CHECK(took(post));
    CHECK(saw({"group", "added", "notify"}));
    CHECK(store.groupState(kClub).has_value());
    CHECK(store.messageByE2e("g1", kClub).author == QStringLiteral("Ann"));

    QVariantMap future = incoming(kBob, "unsupported", "u1");
    future["rawType"] = QStringLiteral("poll.vote");
    CHECK(took(future));
    CHECK(saw({"added"}));
    const StoredMessage unknown = store.messageByE2e("u1", kBob);
    CHECK(unknown.type == QStringLiteral("system"));
    CHECK(systemNoteText(unknown.text).contains(QStringLiteral("poll.vote")));
    CHECK(took(future));
    CHECK(saw({}));
    QVariantMap worded = incoming(kBob, "unsupported", "u2", "a question for everyone");
    worded["rawType"] = QStringLiteral("poll");
    CHECK(took(worded));
    CHECK(saw({"added"}));
    CHECK(store.messageByE2e("u2", kBob).type == QStringLiteral("system"));

    QVariantMap timer = incoming(kBob, "chat.timer", "t1");
    timer["timerSeconds"] = static_cast<qint64>(client::kSecondsPerDay);
    CHECK(took(timer));
    CHECK(saw({"timer", "added"}));
    CHECK(store.chatTimer(kBob) == client::kSecondsPerDay);
    const StoredMessage plate = store.messageByE2e("t1", kBob);
    CHECK(plate.type == QStringLiteral("system"));
    CHECK(systemNoteText(plate.text).contains(QStringLiteral("Bob")));
    CHECK(took(timer));
    CHECK(saw({}));
    QVariantMap off = ours(kBob, "chat.timer", "t2");
    off["timerSeconds"] = static_cast<qint64>(client::kChatTimerOff);
    CHECK(took(off));
    CHECK(saw({"timer", "added-own"}));
    CHECK(store.chatTimer(kBob) == client::kChatTimerOff);
    QVariantMap menu = incoming(kBob, "bot.menu", "bm1");
    menu["menuItems"] = QStringLiteral(R"([{"title":"Help","command":"help"}])");
    CHECK(took(menu));
    CHECK(saw({"menu"}));
    CHECK(store.botMenu(kBob).size() == 1);
    CHECK(store.messageByE2e("bm1", kBob).id == 0);
    QVariantMap gone = incoming(kBob, "bot.menu", "bm2");
    gone["menuItems"] = QStringLiteral("[]");
    CHECK(took(gone));
    CHECK(saw({"menu"}));
    CHECK(store.botMenu(kBob).isEmpty());

    QVariantMap fromHost = incoming(kClub, "chat.timer", "t3");
    fromHost["timerSeconds"] = static_cast<qint64>(client::kSecondsPerHour);
    CHECK(took(fromHost));
    CHECK(saw({}));
    CHECK(store.chatTimer(kClub) == client::kChatTimerOff);

    const Bytes key = client::accountkey::keyFor(db.toStdString(), {});
    sqlite3* writer = nullptr;
    CHECK(sqlite3_open(db.toUtf8().constData(), &writer) == SQLITE_OK);
    const std::string keying = "PRAGMA key = \"x'" + toHex(key) + "'\"";
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
    fs::remove_all(dir);
    std::fprintf(stderr, "TestIngest passed\n");
    return 0;
}
