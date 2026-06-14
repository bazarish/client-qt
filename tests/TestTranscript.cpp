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

    fs::remove_all(dir);
    std::fprintf(stderr, "TestTranscript passed\n");
    return 0;
}
