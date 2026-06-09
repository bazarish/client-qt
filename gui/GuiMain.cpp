// Bazarish project (c) 2026
#include "MainWindow.hpp"
#include "Session.hpp"

#include <QApplication>
#include <QStringList>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <stdexcept>

using bazarish::client::ServerEndpoint;
using bazarish::client::Session;

namespace {

// Opens the session at stateDir, creating and subscribing it first when it
// does not exist yet (requires the connection arguments).
Session openOrCreate(const std::filesystem::path& stateDir, const QStringList& args)
{
    if (std::filesystem::exists(stateDir / "meta.json")) {
        return Session::open(stateDir);
    }
    if (args.size() != 5) {
        throw std::runtime_error(
            "client not initialized: provide host port server-fp to create it");
    }
    ServerEndpoint endpoint;
    endpoint.host = args[2].toStdString();
    endpoint.port = args[3].toInt();
    endpoint.serverFingerprint = args[4].toStdString();
    Session session = Session::create(stateDir, endpoint);
    session.subscribe(14);
    return session;
}

}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QStringList args = app.arguments();
    if (args.size() != 2 && args.size() != 5) {
        std::fprintf(stderr,
            "usage: bazarish-gui <state> [host port server-fp]\n"
            "  <state> alone opens an existing client; the connection\n"
            "  arguments create and subscribe a new one.\n");
        return 2;
    }

    const std::filesystem::path stateDir(args[1].toStdString());
    try {
        Session session = openOrCreate(stateDir, args);
        bazarish::gui::MainWindow window(session);
        window.show();
        return app.exec();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
