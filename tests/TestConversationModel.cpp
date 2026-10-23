// Bazarish project (c) 2026
#include "Models.hpp"

#include <QGuiApplication>

#include <cstdio>
#include <cstdlib>

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition);                                                        \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)

using bazarish::app::ConversationModel;
using bazarish::app::StoredMessage;

namespace {

StoredMessage row(const qint64 id)
{
    StoredMessage message;
    message.id = id;
    message.orderKey = id;
    message.peer = QStringLiteral("peer");
    message.e2eId = QStringLiteral("e2e-%1").arg(id);
    return message;
}

}  // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    ConversationModel model;
    model.setMessages({row(2), row(3), row(4)});

    // The core's notice of a row can land after a page that already brought it.
    StoredMessage edited = row(4);
    edited.text = QStringLiteral("edited");
    CHECK(model.appendMessage(edited) == 2);
    CHECK(model.rowCount() == 3);
    CHECK(model.data(model.index(2), ConversationModel::TextRole).toString() == edited.text);

    model.appendMessages({row(4), row(5)});
    CHECK(model.rowCount() == 4);
    model.prependMessages({row(1), row(2)});
    CHECK(model.rowCount() == 5);
    for (int at = 0; at < model.rowCount(); ++at) {
        CHECK(model.rowForId(at + 1) == at);
    }

    std::printf("TestConversationModel: all checks passed\n");
    return 0;
}
