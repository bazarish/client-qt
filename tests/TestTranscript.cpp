// Bazarish project (c) 2026
#include "TranscriptStore.hpp"

#include "AccountDb.hpp"

#include <bazarish/Bytes.hpp>

#include <QCoreApplication>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

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

int main(int argc, char** argv)
{
    const QCoreApplication app(argc, argv);
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() / ("bz-transcript-" + toHex(randomBytes(8)));
    fs::create_directories(dir);
    const QString db = QString::fromStdString((dir / "account.db").string());

    {
        TranscriptStore store;
        CHECK(store.open("p1", db, "pw"));
        StoredMessage m;
        m.peer = "bob";
        m.outgoing = true;
        m.type = "text";
        m.text = "secret-hello";
        m.ts = 42;
        m.status = 1;
        CHECK(store.append(m) > 0);

        StoredMessage withKeyboard;
        withKeyboard.peer = "botpeer";
        withKeyboard.outgoing = false;
        withKeyboard.type = "text";
        withKeyboard.text = "pick one";
        withKeyboard.e2eId = "abc123";
        withKeyboard.keyboard
            = R"([[{"text":"Ping","data":"ping"}],[{"text":"Help","command":"help"}]])";
        withKeyboard.ts = 43;
        withKeyboard.status = 1;
        CHECK(store.append(withKeyboard) > 0);

        const QVector<StoredMessage> loaded
            = store.latestMessages(QStringLiteral("botpeer"), 10);
        CHECK(loaded.size() == 1);
        const StoredMessage& back = loaded.last();
        CHECK(back.text == QStringLiteral("pick one"));
        CHECK(back.keyboard == withKeyboard.keyboard);
        CHECK(back.e2eId == QStringLiteral("abc123"));
        CHECK(!back.outgoing);
    }

    {
        CHECK(fs::exists(db.toStdString()));
        std::ifstream in(db.toStdString(), std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        CHECK(!bytes.empty());
        CHECK(bytes.rfind("SQLite format 3", 0) != 0);
        CHECK(bytes.find("secret-hello") == std::string::npos);
    }

    {
        TranscriptStore store;
        CHECK(store.open("p2", db, "pw"));
        const QVector<StoredMessage> msgs = store.messagesFor("bob");
        CHECK(msgs.size() == 1);
        CHECK(msgs[0].text == "secret-hello");
        CHECK(msgs[0].ts == 42);
    }

    {
        TranscriptStore store;
        CHECK(!store.open("p3", db, "wrong"));
    }
    {
        TranscriptStore store;
        CHECK(store.open("p4", db, "pw"));
        CHECK(store.messagesFor("bob").size() == 1);
    }

    {
        const QString pdb = QString::fromStdString((dir / "paged.db").string());
        TranscriptStore store;
        CHECK(store.open("pg", pdb, ""));
        QVector<qint64> ids;
        const char* const texts[]
            = {"alpha one", "beta two", "Gamma THREE", "\xd0\xb4\xd0\xb5\xd0\xbb\xd1\x8c\xd1\x82\xd0\xb0 four", "alpha five"};
        for (int i = 0; i < 5; ++i) {
            StoredMessage m;
            m.peer = "carol";
            m.outgoing = (i % 2 == 0);
            m.type = "text";
            m.text = QString::fromUtf8(texts[i]);
            m.ts = 100 + i;
            m.status = 0;
            const qint64 id = store.append(m);
            CHECK(id > 0);
            ids.push_back(id);
        }

        const QVector<StoredMessage> latest = store.latestMessages("carol", 2);
        CHECK(latest.size() == 2);
        CHECK(latest[0].id == ids[3] && latest[1].id == ids[4]);
        CHECK(store.hasMessagesBefore("carol", latest[0].id));
        CHECK(!store.hasMessagesAfter("carol", latest[1].id));

        const QVector<StoredMessage> older = store.olderMessages("carol", latest[0].id, 2);
        CHECK(older.size() == 2);
        CHECK(older[0].id == ids[1] && older[1].id == ids[2]);
        CHECK(!store.hasMessagesBefore("carol", ids[0]));

        const QVector<StoredMessage> newer = store.newerMessages("carol", ids[2], 10);
        CHECK(newer.size() == 2);
        CHECK(newer[0].id == ids[3] && newer[1].id == ids[4]);

        const QVector<SearchHit> alpha = store.searchInPeer("carol", "ALPHA");
        CHECK(alpha.size() == 2);
        CHECK(alpha[0].id == ids[4] && alpha[1].id == ids[0]);
        CHECK(store.searchInPeer("carol", QString::fromUtf8("\xd0\xb4\xd0\xb5\xd0\xbb\xd1\x8c\xd1\x82\xd0\xb0")).size() == 1);
        CHECK(store.searchInPeer("carol", QString::fromUtf8("\xd0\x94\xd0\x95\xd0\x9b\xd0\xac\xd0\xa2\xd0\x90")).size() == 1);
        CHECK(store.searchInPeer("carol", "nomatch").isEmpty());

        CHECK(store.sourcePathFor(ids[0]).isEmpty());

        StoredMessage onItsWay;
        onItsWay.peer = "carol";
        onItsWay.outgoing = true;
        onItsWay.type = "text";
        onItsWay.text = "left mid-flight";
        onItsWay.ts = 200;
        onItsWay.orderKey = onItsWay.ts;
        onItsWay.status = 1;
        CHECK(store.append(onItsWay) > 0);
        CHECK(store.failUnsentOnLoad(0, 1, 4) == 4);
        for (const StoredMessage& m : store.messagesFor("carol")) {
            CHECK(m.status == (m.outgoing ? 4 : 0));
        }
    }

    {
        const QString rdb = QString::fromStdString((dir / "reorder.db").string());
        TranscriptStore store;
        CHECK(store.open("ro", rdb, ""));
        struct In {
            const char* text;
            qint64 ts;
            qint64 orderKey;
        };
        const In in[] = {
            {"second", 2000, 2000},
            {"first", 1000, 1000},
            {"third", 3000, 3000},
            {"late", 500, 9000},
        };
        for (const In& e : in) {
            StoredMessage m;
            m.peer = "dave";
            m.type = "text";
            m.text = QString::fromUtf8(e.text);
            m.ts = e.ts;
            m.orderKey = e.orderKey;
            CHECK(store.append(m) > 0);
        }
        const QVector<StoredMessage> ordered = store.messagesFor("dave");
        CHECK(ordered.size() == 4);
        CHECK(ordered[0].text == "first");
        CHECK(ordered[1].text == "second");
        CHECK(ordered[2].text == "third");
        CHECK(ordered[3].text == "late");
        CHECK(ordered[3].ts == 500);
        CHECK(store.lastTime("dave") == 500);
        CHECK(store.lastMessage("dave").text == "late");
    }

    {
        const QString sdb = QString::fromStdString((dir / "readstate.db").string());
        QVector<qint64> ids;
        {
            TranscriptStore store;
            CHECK(store.open("rs", sdb, "pw"));
            for (int i = 0; i < 5; ++i) {
                StoredMessage m;
                m.peer = "erin";
                m.outgoing = (i == 0 || i == 2);
                m.type = "text";
                m.text = QString("m%1").arg(i);
                m.ts = 200 + i;
                m.orderKey = m.ts;
                const qint64 id = store.append(m);
                CHECK(id > 0);
                ids.push_back(id);
            }
            CHECK(store.unreadCount("erin") == 3);
            CHECK(store.firstUnreadId("erin") == ids[1]);
            CHECK(store.lastReadId("erin") == 0);
            CHECK(store.unreadCount("nobody") == 0);
            CHECK(store.firstUnreadId("nobody") == 0);

            store.setLastReadId("erin", ids[3]);
            CHECK(store.unreadCount("erin") == 1);
            CHECK(store.firstUnreadId("erin") == ids[4]);

            store.setLastReadId("erin", ids[1]);
            CHECK(store.lastReadId("erin") == ids[3]);
            CHECK(store.unreadCount("erin") == 1);
        }
        {
            TranscriptStore store;
            CHECK(store.open("rs2", sdb, "pw"));
            CHECK(store.lastReadId("erin") == ids[3]);
            CHECK(store.unreadCount("erin") == 1);
            CHECK(store.firstUnreadId("erin") == ids[4]);
            store.setLastReadId("erin", ids[4]);
            CHECK(store.unreadCount("erin") == 0);
            CHECK(store.firstUnreadId("erin") == 0);
        }
    }

    {
        const QString sdb = QString::fromStdString((dir / "sysunread.db").string());
        TranscriptStore store;
        CHECK(store.open("su", sdb, ""));
        StoredMessage note;
        note.peer = "frank";
        note.outgoing = false;
        note.type = "system";
        note.text = "frank cleared the chat.";
        note.ts = 300;
        note.orderKey = note.ts;
        CHECK(store.append(note) > 0);
        CHECK(store.unreadCount("frank") == 0);
        CHECK(store.firstUnreadId("frank") == 0);
        StoredMessage real;
        real.peer = "frank";
        real.outgoing = false;
        real.type = "text";
        real.text = "hi again";
        real.ts = 301;
        real.orderKey = real.ts;
        const qint64 realId = store.append(real);
        CHECK(realId > 0);
        CHECK(store.unreadCount("frank") == 1);
        CHECK(store.firstUnreadId("frank") == realId);
    }

    {
        const QString sdb = QString::fromStdString((dir / "versioned.db").string());
        {
            TranscriptStore fresh;
            CHECK(fresh.open("ver", sdb, QString()));
            StoredMessage m;
            m.peer = "heidi";
            m.type = "text";
            m.text = "written at version 1";
            m.ts = 500;
            m.orderKey = m.ts;
            CHECK(fresh.append(m) > 0);
        }
        TranscriptStore again;
        CHECK(again.open("ver", sdb, QString()));
        CHECK(again.latestMessages("heidi", 10).size() == 1);
    }

    {
        const fs::path file = dir / "weights.db";
        const QString wdb = QString::fromStdString(file.string());
        TranscriptStore store;
        CHECK(store.open("w", wdb, "pw"));

        const auto write = [&store](const char* const peer, const char* const id,
                               const char* const text) {
            StoredMessage m;
            m.peer = QString::fromUtf8(peer);
            m.type = "text";
            m.e2eId = QString::fromUtf8(id);
            m.text = QString::fromUtf8(text);
            m.ts = 1000;
            m.orderKey = m.ts;
            const qint64 id64 = store.append(m);
            CHECK(id64 > 0);
            return id64;
        };
        write("alpha", "a1", "one");
        write("alpha", "a2", "two");
        write("alpha", "a3", "three");
        {
            StoredMessage note;
            note.peer = "alpha";
            note.type = "system";
            note.text = "a note with no id";
            note.ts = 1000;
            note.orderKey = note.ts;
            CHECK(store.append(note) > 0);
        }
        const qint64 a4 = write("alpha", "a4", "four");
        const qint64 a5 = write("alpha", "a5", "five");
        write("beta", "b1", "beta one");
        write("beta", "b2", "beta two");
        store.setReaction("alpha", "a1", "carol", "+1");
        store.setReaction("alpha", "a5", "carol", "+1");

        const Bytes doomedPicture(4096, 7);
        const Bytes keptPicture(2048, 9);
        const Bytes betaVoice(1024, 3);
        const Bytes ghost(512, 1);
        {
            client::AccountDb account(file, "pw");
            account.put("picture:a1", doomedPicture);
            account.put("picture:a5", keptPicture);
            account.put("voice:b1", betaVoice);
            account.put("picture:gone", ghost);
        }

        const QVector<ChatWeight> weights = store.chatWeights();
        CHECK(weights.size() == 2);
        for (const ChatWeight& weight : weights) {
            CHECK(weight.rowBytes > 0);
            if (weight.peer == "alpha") {
                CHECK(weight.messages == 6);
                CHECK(weight.mediaCount == 2);
                CHECK(weight.mediaBytes
                    == static_cast<qint64>(doomedPicture.size() + keptPicture.size()));
            } else {
                CHECK(weight.peer == "beta");
                CHECK(weight.messages == 2);
                CHECK(weight.mediaCount == 1);
                CHECK(weight.mediaBytes == static_cast<qint64>(betaVoice.size()));
            }
        }

        CHECK(store.pruneToLatest("alpha", 2) == 4);
        const QVector<StoredMessage> kept = store.messagesFor("alpha");
        CHECK(kept.size() == 2);
        QVector<qint64> keptIds;
        for (const StoredMessage& message : kept) {
            keptIds.append(message.id);
        }
        std::sort(keptIds.begin(), keptIds.end());
        CHECK(keptIds.front() == a4);
        CHECK(keptIds.back() == a5);
        CHECK(store.messagesFor("beta").size() == 2);
        {
            client::AccountDb account(file, "pw");
            CHECK(!account.has("picture:a1"));
            CHECK(account.has("picture:a5"));
            CHECK(account.has("voice:b1"));
            CHECK(account.has("picture:gone"));
        }
        CHECK(store.reactionsFor("alpha", "a1").isEmpty());
        CHECK(store.reactionsFor("alpha", "a5").size() == 1);

        CHECK(store.pruneToLatest("alpha", 100) == 0);
        CHECK(store.messagesFor("alpha").size() == 2);

        const qint64 beforeTrim = store.footprint().fileBytes;
        CHECK(store.pruneEveryChatToLatest(1) == 2);
        CHECK(store.footprint().freeBytes > 0);
        {
            QString reason;
            CHECK(store.rebuild(reason));
        }
        CHECK(store.footprint().fileBytes < beforeTrim);
        CHECK(store.footprint().freeBytes == 0);
        CHECK(store.messagesFor("alpha").size() == 1);
        CHECK(store.messagesFor("beta").size() == 1);
        {
            client::AccountDb account(file, "pw");
            CHECK(!account.has("voice:b1"));
            account.put("voice:b2", betaVoice);
        }

        {
            client::AccountDb account(file, "pw");
            account.put("picture:a5", keptPicture);
        }
        const QVector<StoredMessage> alpha = store.messagesFor("alpha");
        CHECK(alpha.size() == 1);
        CHECK(alpha.front().e2eId == "a5");
        store.removeById(alpha.front().id);
        {
            client::AccountDb account(file, "pw");
            CHECK(!account.has("picture:a5"));
            CHECK(account.has("voice:b2"));
        }

        store.clearPeer("beta");
        CHECK(store.messagesFor("beta").isEmpty());
        {
            client::AccountDb account(file, "pw");
            CHECK(!account.has("voice:b2"));
        }

        store.close();

        TranscriptStore reopened;
        CHECK(reopened.open("w", wdb, "pw"));
        CHECK(reopened.conversationPeers().isEmpty());
    }

    {
        const QString rdb = QString::fromStdString((dir / "readmark.db").string());
        TranscriptStore store;
        CHECK(store.open("r", rdb, "pw"));
        const auto incoming = [&store](const char* const id, const qint64 sentAt) {
            StoredMessage m;
            m.peer = "ivan";
            m.outgoing = false;
            m.type = "text";
            m.e2eId = QString::fromUtf8(id);
            m.text = "hello";
            m.ts = sentAt;
            m.orderKey = sentAt;
            const qint64 row = store.append(m);
            CHECK(row > 0);
            return row;
        };
        incoming("r1", 1000);
        incoming("r2", 2000);
        const qint64 third = incoming("r3", 3000);
        incoming("r4", 4000);
        CHECK(store.unreadCount("ivan") == 4);

        store.applyReadThrough("ivan", 3000);
        CHECK(store.lastReadId("ivan") == third);
        CHECK(store.unreadCount("ivan") == 1);

        incoming("r5", 1500);
        CHECK(store.unreadCount("ivan") == 2);

        store.applyReadThrough("ivan", 1000);
        CHECK(store.lastReadId("ivan") == third);
        CHECK(store.unreadCount("ivan") == 2);

        store.applyReadThrough("ivan", 9000);
        CHECK(store.unreadCount("ivan") == 0);
    }

    {
        TranscriptStore store;
        CHECK(store.open("p1", db, "pw"));
        store.setReaction("bob", "m1", "them", "\xF0\x9F\x91\x8D");
        store.setReaction("bob", "m1", "us", "\xE2\x9D\xA4");
        const QVector<Reaction> both = store.reactionsFor("bob", "m1");
        CHECK(both.size() == 2);
        int mine = 0;
        for (const Reaction& reaction : both) {
            if (reaction.reactor == QStringLiteral("us")) {
                ++mine;
            }
        }
        CHECK(mine == 1);
        store.setReaction("bob", "m1", "them", "\xF0\x9F\x94\xA5");
        CHECK(store.reactionsFor("bob", "m1").size() == 2);
        store.setReaction("bob", "m1", "them", QString());
        const QVector<Reaction> left = store.reactionsFor("bob", "m1");
        CHECK(left.size() == 1);
        CHECK(left.first().reactor == QStringLiteral("us"));
    }

    {
        TranscriptStore store;
        CHECK(store.open("p1", db, "pw"));
        const auto plate = [&](const bool outgoing, const QString& id) {
            StoredMessage m;
            m.peer = "asker";
            m.outgoing = outgoing;
            m.type = "contact.request";
            m.e2eId = id;
            m.text = "let me in";
            m.ts = 1;
            m.orderKey = 1;
            CHECK(store.append(m) > 0);
        };
        CHECK(store.oldestOfType("asker", "contact.request", false) == 0);
        plate(false, "req-1");
        const qint64 first = store.oldestOfType("asker", "contact.request", false);
        CHECK(first != 0);
        CHECK(store.oldestOfType("asker", "contact.request", true) == 0);
        plate(true, "req-2");
        CHECK(store.oldestOfType("asker", "contact.request", true) != 0);
        plate(false, "req-3");
        CHECK(store.oldestOfType("asker", "contact.request", false) == first);
        CHECK(store.oldestOfType("someone-else", "contact.request", false) == 0);
    }

    {
        TranscriptStore store;
        CHECK(store.open("p-cols", QString::fromStdString((dir / "columns.db").string()), "pw"));
        StoredMessage full;
        full.peer = "dana";
        full.outgoing = true;
        full.type = "audio";
        full.e2eId = "e-full";
        full.text = "body";
        full.attName = "note.opus";
        full.attMime = "audio/opus";
        full.attSize = 4321;
        full.attRef = "ref-9";
        full.attSrcPath = "/tmp/source.opus";
        full.keyboard = "[button]";
        full.edited = true;
        full.ts = 1700;
        full.status = 3;
        full.orderKey = 1701;
        full.replyTo = "e-parent";
        full.attDurationMs = 2500;
        full.attWave = "abcdef";
        full.forwarded = true;
        CHECK(store.append(full) > 0);

        const QVector<StoredMessage> back = store.latestMessages("dana", 10);
        CHECK(back.size() == 1);
        const StoredMessage& r = back.first();
        CHECK(r.peer == full.peer);
        CHECK(r.outgoing == full.outgoing);
        CHECK(r.type == full.type);
        CHECK(r.e2eId == full.e2eId);
        CHECK(r.text == full.text);
        CHECK(r.attName == full.attName);
        CHECK(r.attMime == full.attMime);
        CHECK(r.attSize == full.attSize);
        CHECK(r.attRef == full.attRef);
        CHECK(r.attSrcPath == full.attSrcPath);
        CHECK(r.keyboard == full.keyboard);
        CHECK(r.edited == full.edited);
        CHECK(r.ts == full.ts);
        CHECK(r.status == full.status);
        CHECK(r.orderKey == full.orderKey);
        CHECK(r.replyTo == full.replyTo);
        CHECK(r.attDurationMs == full.attDurationMs);
        CHECK(r.attWave == full.attWave);
        CHECK(r.forwarded == full.forwarded);
        CHECK(!r.hasPicture);
        CHECK(r.savedPath.isEmpty());
    }

    {
        TranscriptStore store;
        CHECK(store.open("p-del", QString::fromStdString((dir / "delete.db").string()), "pw"));
        const auto write = [&](const QString& peer, const QString& e2eId) {
            StoredMessage m;
            m.peer = peer;
            m.e2eId = e2eId;
            m.outgoing = false;
            m.type = "text";
            m.text = "hello";
            m.ts = 100;
            const qint64 id = store.append(m);
            CHECK(id > 0);
            return id;
        };

        const qint64 one = write("carol", "e-one");
        write("carol", "e-two");
        store.setReaction("carol", "e-one", "carol", "\xf0\x9f\x91\x8d");
        store.setReaction("carol", "e-two", "carol", "\xf0\x9f\x91\x8e");
        store.setLastReadId("carol", one);
        store.setPinned("carol", true);
        CHECK(store.reactionsFor("carol", "e-one").size() == 1);

        store.removeById(one);
        CHECK(store.reactionsFor("carol", "e-one").isEmpty());
        CHECK(store.reactionsFor("carol", "e-two").size() == 1);

        store.clearPeer("carol");
        CHECK(store.reactionsFor("carol", "e-two").isEmpty());
        CHECK(store.lastReadId("carol") == 0);
        CHECK(store.isPinned("carol"));

        store.forgetPeer("carol");
        CHECK(!store.isPinned("carol"));
        CHECK(store.pinnedPeers().isEmpty());
    }

    fs::remove_all(dir);
    std::fprintf(stderr, "TestTranscript passed\n");
    return 0;
}
