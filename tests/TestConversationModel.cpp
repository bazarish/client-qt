// Bazarish project (c) 2026
#include "Models.hpp"
#include "TestUtil.hpp"

#include <QGuiApplication>

#include <cstdio>
#include <cstdlib>

using bazarish::app::ContactListModel;
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

    StoredMessage edited = row(4);
    edited.text = QStringLiteral("edited");
    CHECK(model.appendMessage(edited) == 2);
    CHECK(model.rowCount() == 3);
    CHECK(model.data(model.index(2), ConversationModel::TextRole).toString() == edited.text);
    StoredMessage fromTablet = row(3);
    fromTablet.device = QStringLiteral("tabdev2345");
    CHECK(model.appendMessage(fromTablet) == 1);
    CHECK(model.data(model.index(1), ConversationModel::DeviceRole).toString() == fromTablet.device);

    model.appendMessages({row(4), row(5)});
    CHECK(model.rowCount() == 4);
    model.prependMessages({row(1), row(2)});
    CHECK(model.rowCount() == 5);
    for (int at = 0; at < model.rowCount(); ++at) {
        CHECK(model.rowForId(at + 1) == at);
    }

    ContactListModel chats;
    const QString peer = QStringLiteral("peer");
    constexpr qint64 kSentAt = 1000;
    chats.touch(peer, QStringLiteral("Peer"), QStringLiteral("hello"), kSentAt);
    chats.touch(peer, QStringLiteral("Peer"), QString(), 0);
    CHECK(chats.data(chats.index(0), ContactListModel::LastTextRole).toString().isEmpty());
    CHECK(chats.data(chats.index(0), ContactListModel::LastTimeRole).toLongLong() == 0);

    std::printf("TestConversationModel: all checks passed\n");
    return 0;
}
