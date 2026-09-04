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

    // The database is encrypted in place: one file, written as it goes.
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

        // A bot's message carries its buttons in the row beside the text. The
        // column list and the indices that read it back are written out by hand,
        // so what goes in has to be shown to come out.
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

    // The file on disk is the database itself, and it carries neither the SQLite
    // header nor the message body in the clear.
    {
        CHECK(fs::exists(db.toStdString()));
        std::ifstream in(db.toStdString(), std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        CHECK(!bytes.empty());
        CHECK(bytes.rfind("SQLite format 3", 0) != 0);
        CHECK(bytes.find("secret-hello") == std::string::npos);
    }

    // Reopening with the right passphrase restores the history.
    {
        TranscriptStore store;
        CHECK(store.open("p2", db, "pw"));
        const QVector<StoredMessage> msgs = store.messagesFor("bob");
        CHECK(msgs.size() == 1);
        CHECK(msgs[0].text == "secret-hello");
        CHECK(msgs[0].ts == 42);
    }

    // A wrong passphrase does not open the database (and must not clobber it):
    // an unreadable file is a failed open, never an empty transcript.
    {
        TranscriptStore store;
        CHECK(!store.open("p3", db, "wrong"));
    }
    // The database still opens with the right passphrase after the failed attempt.
    {
        TranscriptStore store;
        CHECK(store.open("p4", db, "pw"));
        CHECK(store.messagesFor("bob").size() == 1);
    }

    // Paging, search and the load-time demote (plaintext store).
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
            m.outgoing = (i % 2 == 0);  // outgoing at 0,2,4; incoming at 1,3
            m.type = "text";
            m.text = QString::fromUtf8(texts[i]);
            m.ts = 100 + i;
            m.status = 0;  // all "sending" to exercise the load-time demote
            const qint64 id = store.append(m);
            CHECK(id > 0);
            ids.push_back(id);
        }

        // latestMessages: the newest 2, oldest-first.
        const QVector<StoredMessage> latest = store.latestMessages("carol", 2);
        CHECK(latest.size() == 2);
        CHECK(latest[0].id == ids[3] && latest[1].id == ids[4]);
        CHECK(store.hasMessagesBefore("carol", latest[0].id));
        CHECK(!store.hasMessagesAfter("carol", latest[1].id));

        // olderMessages before the oldest loaded: the previous 2.
        const QVector<StoredMessage> older = store.olderMessages("carol", latest[0].id, 2);
        CHECK(older.size() == 2);
        CHECK(older[0].id == ids[1] && older[1].id == ids[2]);
        CHECK(!store.hasMessagesBefore("carol", ids[0]));

        // newerMessages after a middle id, oldest-first.
        const QVector<StoredMessage> newer = store.newerMessages("carol", ids[2], 10);
        CHECK(newer.size() == 2);
        CHECK(newer[0].id == ids[3] && newer[1].id == ids[4]);

        // Case-insensitive full-text search, newest first, including Cyrillic.
        const QVector<SearchHit> alpha = store.searchInPeer("carol", "ALPHA");
        CHECK(alpha.size() == 2);
        CHECK(alpha[0].id == ids[4] && alpha[1].id == ids[0]);
        CHECK(store.searchInPeer("carol", QString::fromUtf8("\xd0\xb4\xd0\xb5\xd0\xbb\xd1\x8c\xd1\x82\xd0\xb0")).size() == 1);
        CHECK(store.searchInPeer("carol", QString::fromUtf8("\xd0\x94\xd0\x95\xd0\x9b\xd0\xac\xd0\xa2\xd0\x90")).size() == 1);
        CHECK(store.searchInPeer("carol", "nomatch").isEmpty());

        CHECK(store.sourcePathFor(ids[0]).isEmpty());

        // failUnsentOnLoad: an outgoing row a delivery was still carrying comes
        // back failed, whether it was preparing its address (0) or already on its
        // way (1). Incoming rows are left alone.
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

    // Reordering by orderKey: a conversation is returned sorted by the sentAt-based
    // sort position, not by insertion (arrival) order. Simulates an out-of-order
    // burst plus a long-delayed arrival pinned at the end (docs-main Messages.md).
    {
        const QString rdb = QString::fromStdString((dir / "reorder.db").string());
        TranscriptStore store;
        CHECK(store.open("ro", rdb, ""));
        struct In {
            const char* text;
            qint64 ts;
            qint64 orderKey;
        };
        // Inserted in arrival order (ascending id); orderKey is the send position.
        const In in[] = {
            {"second", 2000, 2000},  // sent 2nd, arrived 1st
            {"first", 1000, 1000},   // sent 1st, arrived 2nd (out of order)
            {"third", 3000, 3000},   // sent 3rd, arrived 3rd
            {"late", 500, 9000},     // sent long ago (ts 500), arrived late -> pinned last
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
        CHECK(ordered[0].text == "first");   // orderKey 1000
        CHECK(ordered[1].text == "second");  // orderKey 2000
        CHECK(ordered[2].text == "third");   // orderKey 3000
        CHECK(ordered[3].text == "late");    // orderKey 9000: pinned at the end
        CHECK(ordered[3].ts == 500);         // but still displays its own (old) sentAt
        // lastTime / lastText reflect the newest by orderKey (the late arrival).
        CHECK(store.lastTime("dave") == 500);
        CHECK(store.lastText("dave") == "late");
    }

    // Read state: persistent unread tracking (the unread badge + open-at-first-unread).
    {
        const QString sdb = QString::fromStdString((dir / "readstate.db").string());
        QVector<qint64> ids;
        {
            TranscriptStore store;
            CHECK(store.open("rs", sdb, "pw"));  // sealed, to exercise persistence too
            // 5 messages under "erin": outgoing at 0,2; incoming at 1,3,4.
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
            // Nothing read yet: all 3 incoming are unread; the first is index 1.
            CHECK(store.unreadCount("erin") == 3);
            CHECK(store.firstUnreadId("erin") == ids[1]);
            CHECK(store.lastReadId("erin") == 0);
            // An unknown peer has no unread.
            CHECK(store.unreadCount("nobody") == 0);
            CHECK(store.firstUnreadId("nobody") == 0);

            // Read through the incoming message at index 3: index 1 and 3 are now
            // read, leaving index 4 unread (the first unread advances to it).
            store.setLastReadId("erin", ids[3]);
            CHECK(store.unreadCount("erin") == 1);
            CHECK(store.firstUnreadId("erin") == ids[4]);

            // The high-water is monotonic: a lower id never lowers it (re-reading
            // older history must not resurrect newer messages as unread).
            store.setLastReadId("erin", ids[1]);
            CHECK(store.lastReadId("erin") == ids[3]);
            CHECK(store.unreadCount("erin") == 1);
        }
        // Persistence across a reopen: the high-water survives, so unread is stable.
        {
            TranscriptStore store;
            CHECK(store.open("rs2", sdb, "pw"));
            CHECK(store.lastReadId("erin") == ids[3]);
            CHECK(store.unreadCount("erin") == 1);
            CHECK(store.firstUnreadId("erin") == ids[4]);
            // Reading to the end clears it.
            store.setLastReadId("erin", ids[4]);
            CHECK(store.unreadCount("erin") == 0);
            CHECK(store.firstUnreadId("erin") == 0);
        }
    }

    // Service banners (type 'system', e.g. "X cleared the chat") are not messages
    // to be read: they never raise the unread count or the open-at-first-unread.
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
        // A real incoming message after it does count, and is the first unread.
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

    // --- A database of another schema version is refused, not read ---
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
        // Reopening the same file is fine: it carries the number this build writes.
        TranscriptStore again;
        CHECK(again.open("ver", sdb, QString()));
        CHECK(again.latestMessages("heidi", 10).size() == 1);
    }


    // --- What a conversation weighs, and trimming it ---
    //
    // Pictures and voice notes are not in the transcript's tables: the core keeps
    // them beside it in the same file, under "picture:<id>" / "voice:<id>". So the
    // account's own connection puts them there, exactly as the client does.
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
        // A system note carries no protocol id, and the sweep's condition has to
        // survive that: NOT IN over a set holding a NULL matches nothing at all.
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
            // A blob under a name no message carries.
            account.put("picture:gone", ghost);
        }

        const QVector<ChatWeight> weights = store.chatWeights();
        CHECK(weights.size() == 2);
        for (const ChatWeight& weight : weights) {
            CHECK(weight.rowBytes > 0);
            if (weight.peer == "alpha") {
                CHECK(weight.messages == 6);  // five messages and the system note
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

        // The newest two of alpha stay; everything older goes, whichever side it
        // is on and whether or not it carries an id.
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
        // Another conversation is not touched by one conversation's trim.
        CHECK(store.messagesFor("beta").size() == 2);
        // The media follows the messages: the removed one's picture is gone, the
        // kept one's is not, the other conversation's is not, and what a previous
        // clear abandoned is swept with them.
        {
            client::AccountDb account(file, "pw");
            CHECK(!account.has("picture:a1"));
            CHECK(account.has("picture:a5"));
            CHECK(account.has("voice:b1"));
            // Media goes with the message that names it, and nothing else looks
            // for media on its own: a blob no message ever named is not part of
            // any deletion.
            CHECK(account.has("picture:gone"));
        }
        // A reaction on a removed message goes; one on a message that stayed does
        // not.
        CHECK(store.reactionsFor("alpha", "a1").isEmpty());
        CHECK(store.reactionsFor("alpha", "a5").size() == 1);

        // Trimming to more than there is removes nothing.
        CHECK(store.pruneToLatest("alpha", 100) == 0);
        CHECK(store.messagesFor("alpha").size() == 2);

        // Every conversation at once, each to its own newest. Beta's older
        // message carried the voice note, so that goes with it.
        const qint64 beforeTrim = store.footprint().fileBytes;
        CHECK(store.pruneEveryChatToLatest(1) == 2);
        // A trim leaves the freed pages inside the file; the rewrite after it is
        // what returns them, and the window shows both figures.
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

        // A single message takes its own picture with it, and a message that had
        // none leaves everything else where it is.
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
            // Another conversation's message keeps its own.
            CHECK(account.has("voice:b2"));
        }

        // Clearing a conversation takes its media too - it never used to.
        store.clearPeer("beta");
        CHECK(store.messagesFor("beta").isEmpty());
        {
            client::AccountDb account(file, "pw");
            CHECK(!account.has("voice:b2"));
        }

        store.close();

        TranscriptStore reopened;
        CHECK(reopened.open("w", wdb, "pw"));
        // Everything above was removed, and the file reads as empty rather than
        // as unopenable.
        CHECK(reopened.conversationPeers().isEmpty());
    }


    // --- A read mark from another device of the same account ---
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

        // The other device read through the third message. Everything at or
        // before that moment counts as read here too; what came after does not.
        store.applyReadThrough("ivan", 3000);
        CHECK(store.lastReadId("ivan") == third);
        CHECK(store.unreadCount("ivan") == 1);

        // The mark is folded in once, not kept as a rule: a message that arrives
        // afterwards carrying an older stamp - which its sender writes - has not
        // been read by anybody, and must not be hidden.
        incoming("r5", 1500);
        CHECK(store.unreadCount("ivan") == 2);

        // It only ever advances, like every other write of this high-water.
        store.applyReadThrough("ivan", 1000);
        CHECK(store.lastReadId("ivan") == third);
        CHECK(store.unreadCount("ivan") == 2);

        // A moment nothing was sent at leaves the mark where it is; a moment
        // after everything reads the whole conversation.
        store.applyReadThrough("ivan", 9000);
        CHECK(store.unreadCount("ivan") == 0);
    }

    // What a second invitation is checked against: one plate per direction, and
    // the two directions do not answer for each other.
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
        // The other direction is still empty, and a text is not an invitation.
        CHECK(store.oldestOfType("asker", "contact.request", true) == 0);
        plate(true, "req-2");
        CHECK(store.oldestOfType("asker", "contact.request", true) != 0);
        // A second one on the same side is the first one's row, not a new plate.
        plate(false, "req-3");
        CHECK(store.oldestOfType("asker", "contact.request", false) == first);
        CHECK(store.oldestOfType("someone-else", "contact.request", false) == 0);
    }

    fs::remove_all(dir);
    std::fprintf(stderr, "TestTranscript passed\n");
    return 0;
}
