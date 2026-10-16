// Bazarish project (c) 2026
#pragma once

#include "AccountCommands.hpp"
#include "AccountEvents.hpp"

#include <QObject>
#include <QSet>
#include <QThread>

#include <memory>

namespace bazarish::client {
class LoginSigner;
}

namespace bazarish::app {

class SessionWorker;

class CoreAccount : public QObject {
    Q_OBJECT
public:
    CoreAccount(AccountCommands& commands, AccountEvents& events, QObject* parent = nullptr);
    ~CoreAccount() override;

    CoreAccount(const CoreAccount&) = delete;
    CoreAccount& operator=(const CoreAccount&) = delete;

    void shutdown();

private:
    void beginShutdown();
    void signLogin(const QString& challenge);

    AccountCommands& commands_;
    AccountEvents& events_;
    QThread thread_;
    SessionWorker* worker_ = nullptr;
    std::shared_ptr<client::LoginSigner> loginSigner_;
};

}  // namespace bazarish::app
