// Bazarish project (c) 2026
#include "Models.hpp"
#include "TestUtil.hpp"

#include <QGuiApplication>

#include <cstdio>
#include <cstdlib>

using bazarish::app::ContactListModel;
using bazarish::app::ConversationModel;
using bazarish::app::RowReactions;
using bazarish::app::StoredMessage;
using bazarish::app::ViewLanding;

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

constexpr qint64 kFirst = 10;
constexpr qint64 kUnread = 12;
constexpr qint64 kFound = 13;
constexpr qint64 kPastTheEnd = 99;
constexpr qint64 kNotLoaded = 50;
constexpr int kPageRows = 5;

QVector<StoredMessage> page()
{
    QVector<StoredMessage> rows;
    for (qint64 id = kFirst; id < kFirst + kPageRows; ++id) {
        rows << row(id);
    }
    return rows;
}

void landingTravelsWithTheReset()
{
    ConversationModel model;
    QVariantMap seenAtReset;
    QObject::connect(&model, &ConversationModel::modelReset, [&]() { seenAtReset = model.landing(); });

    model.setMessages(page(), {ViewLanding::eUnread, kUnread});
    CHECK(seenAtReset.value("row").toInt() == kUnread - kFirst);
    CHECK(seenAtReset.value("unreadFrom").toLongLong() == kUnread);
    CHECK(!seenAtReset.value("centre").toBool());

    model.setMessages(page(), {ViewLanding::eMessage, kFound});
    CHECK(seenAtReset.value("row").toInt() == kFound - kFirst);
    CHECK(seenAtReset.value("centre").toBool());
    CHECK(seenAtReset.value("highlight").toLongLong() == kFound);

    model.setMessages(page(), {ViewLanding::eRow, kPastTheEnd});
    CHECK(seenAtReset.value("row").toInt() == model.rowCount() - 1);
    model.setMessages(page(), {ViewLanding::eUnread, kPastTheEnd});
    CHECK(seenAtReset.value("row").toInt() == -1);
    CHECK(seenAtReset.value("unreadFrom").toLongLong() == -1);
    model.setMessages(page());
    CHECK(seenAtReset.value("row").toInt() == -1);

    model.setLanding({ViewLanding::eRow, 1});
    CHECK(model.landing().value("row").toInt() == 1);
}

void reactionsAndPicturesChangeOneRow()
{
    ConversationModel model;
    const RowReactions liked{{QVariantMap{{"emoji", "+"}, {"count", 1}, {"mine", false}}}, {}};
    const QString unreadE2e = row(kUnread).e2eId;
    model.putReactions(unreadE2e, liked);
    model.putReactions(row(kNotLoaded).e2eId, liked);
    model.setMessages(page());
    const int unreadRow = static_cast<int>(kUnread - kFirst);
    CHECK(model.data(model.index(unreadRow), ConversationModel::ReactionsRole).toList().size() == 1);
    CHECK(model.data(model.index(0), ConversationModel::ReactionsRole).toList().isEmpty());
    model.appendMessage(row(kNotLoaded));
    CHECK(model.data(model.index(model.rowCount() - 1), ConversationModel::ReactionsRole)
              .toList()
              .isEmpty());

    QList<int> changedRows;
    QObject::connect(&model, &ConversationModel::dataChanged,
        [&](const QModelIndex& from, const QModelIndex&, const QList<int>&) {
            changedRows << from.row();
        });
    model.putReactions(unreadE2e, liked);
    CHECK(changedRows.isEmpty());
    model.putReactions(unreadE2e, RowReactions{liked.summary, {QStringLiteral("+")}});
    CHECK(changedRows == QList<int>{unreadRow});
    CHECK(model.data(model.index(unreadRow), ConversationModel::FreshReactionsRole).toStringList()
          == QStringList{QStringLiteral("+")});

    changedRows.clear();
    model.notePictureChanged(unreadE2e);
    CHECK(changedRows == QList<int>{unreadRow});
    CHECK(model.data(model.index(unreadRow), ConversationModel::PictureVersionRole).toInt() == 1);
    CHECK(model.data(model.index(0), ConversationModel::PictureVersionRole).toInt() == 0);
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

    landingTravelsWithTheReset();
    reactionsAndPicturesChangeOneRow();

    std::printf("TestConversationModel: all checks passed\n");
    return 0;
}
