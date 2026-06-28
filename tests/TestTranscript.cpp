// Bazarish project (c) 2026
#include "TranscriptStore.hpp"

#include <bazarish/Bytes.hpp>

#include <QCoreApplication>

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
    const QString db = QString::fromStdString((dir / "transcript.db").string());
    const QString blob = db + ".enc";

    // Encrypted mode: write one message, then close (flush on destruction).
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
    }

    // The cleartext database file is never created; only the sealed blob is.
    CHECK(!fs::exists(db.toStdString()));
    CHECK(fs::exists(blob.toStdString()));

    // The blob is CMS DER (begins with the SEQUENCE tag) and the plaintext
    // message body does not appear anywhere in it.
    {
        std::ifstream in(blob.toStdString(), std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        CHECK(!bytes.empty());
        CHECK(static_cast<unsigned char>(bytes[0]) == 0x30);
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

    // A wrong passphrase cannot unseal the blob (and must not clobber it).
    {
        TranscriptStore store;
        bool threw = false;
        try {
            store.open("p3", db, "wrong");
        } catch (const std::exception&) {
            threw = true;
        }
        CHECK(threw);
    }
    // The good blob still opens after the failed attempt.
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

        // failUnsentOnLoad: only outgoing status-0 rows become 4 (indices 0,2,4).
        CHECK(store.failUnsentOnLoad(0, 4) == 3);
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

    fs::remove_all(dir);
    std::fprintf(stderr, "TestTranscript passed\n");
    return 0;
}
