// Bazarish project (c) 2026
#include "Bot.hpp"
#include "Session.hpp"

#include <bazarish/Log.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>

namespace {

using bazarish::client::Bot;
using bazarish::client::InlineKeyboard;
using bazarish::client::Session;

constexpr const char* kVersion = "0.0.1";

// The at-rest passphrase for the key PEMs, taken from the environment (as the
// CLI client does) so the bot stays non-interactive.
std::string keyPassphrase()
{
    const char* const value = std::getenv("BAZARISH_PASSPHRASE");
    return value != nullptr ? std::string(value) : std::string();
}

// The demo bot's main menu: two callback buttons (each sends a bot.callback
// with its data) and one command button (sends a bot.command "/help").
InlineKeyboard mainMenu()
{
    return {
        {{"\xF0\x9F\x8F\x93 Ping", "ping", {}}, {"\xF0\x9F\x95\x91 Time", "time", {}}},
        {{"\xE2\x9D\x94 Help", {}, "help"}},
    };
}

void greet(Bot& bot, const std::string& peer)
{
    bot.replyWithKeyboard(peer,
        "Hi! I'm a Bazarish demo bot. Tap a button below, send /help, or just "
        "type anything and I'll echo it back.",
        mainMenu());
}

std::string serverTime()
{
    const std::time_t now = std::time(nullptr);
    char buffer[64];
    const std::size_t written
        = std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&now));
    return std::string(buffer, written);
}

void registerHandlers(Bot& bot)
{
    // A fresh contact (and /start) gets the welcome menu.
    bot.onContact([](Bot& bot, const std::string& peer, const std::string&) { greet(bot, peer); });
    bot.onCommand(
        "start", [](Bot& bot, const std::string& peer, const std::string&) { greet(bot, peer); });

    bot.onCommand("help", [](Bot& bot, const std::string& peer, const std::string&) {
        bot.reply(peer,
            "Commands:\n  /start - show the menu\n  /help - this help\n  /menu - the buttons "
            "again\nOr send any text and I'll echo it.");
    });
    bot.onCommand("menu", [](Bot& bot, const std::string& peer, const std::string&) {
        bot.replyWithKeyboard(peer, "Here you go:", mainMenu());
    });
    bot.onCommand("echo", [](Bot& bot, const std::string& peer, const std::string& args) {
        bot.reply(peer, args.empty() ? "(nothing to echo)" : args);
    });

    // Inline-keyboard button presses arrive as callbacks. The bot edits the
    // same message in place (Telegram-style), keeping the menu so the user can
    // tap again — and the in-place change is itself the visible feedback.
    bot.onCallback([](Bot& bot, const std::string& peer, const std::string& data,
                       const std::string& ref) {
        std::string body;
        if (data == "ping") {
            body = "pong \xF0\x9F\x8F\x93  (updated " + serverTime() + ")";
        } else if (data == "time") {
            body = "\xF0\x9F\x95\x91 " + serverTime();
        } else {
            body = "Unknown button: " + data;
        }
        if (!ref.empty()) {
            bot.editMessage(peer, ref, body + "\n\nTap a button to update this message:", mainMenu());
        } else {
            bot.reply(peer, body);
        }
    });

    bot.onUnknownCommand([](Bot& bot, const std::string& peer, const std::string& name) {
        bot.reply(peer, "Unknown command: /" + name + ". Try /help.");
    });

    // Anything else is echoed.
    bot.onText([](Bot& bot, const std::string& peer, const std::string& text) {
        bot.reply(peer, "You said: " + text);
    });
}

void printUsage()
{
    std::printf(
        "bazarish-bot %s\n"
        "A demo chat bot over a Bazarish identity (commands + inline keyboards).\n"
        "\n"
        "Usage:\n"
        "  bazarish-bot <state>          poll forever (stop with Ctrl-C)\n"
        "  bazarish-bot --once <state>   process one batch of updates and exit\n"
        "\n"
        "<state> is an existing, subscribed client profile (create it with\n"
        "bazarish-client init/subscribe; register a username with `alias` so\n"
        "people can add the bot by name). --once suits a cron-driven bot.\n"
        "\n"
        "Environment:\n"
        "  BAZARISH_PASSPHRASE  decrypts the key PEMs at rest (if encrypted)\n",
        kVersion);
}

}  // namespace

int main(const int argc, const char** argv)
{
    bazarish::log::setComponent("bot");
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("bazarish-bot %s\n", kVersion);
        return 0;
    }

    bool once = false;
    const char* state = nullptr;
    if (argc == 2) {
        state = argv[1];
    } else if (argc == 3 && std::strcmp(argv[1], "--once") == 0) {
        once = true;
        state = argv[2];
    } else {
        printUsage();
        return 2;
    }

    try {
        Session session = Session::open(state, keyPassphrase());
        if (!session.isConnected()) {
            bazarish::log::error("profile is not connected to a server; subscribe first");
            return 1;
        }
        Bot bot(session);
        registerHandlers(bot);
        if (once) {
            const std::size_t handled = bot.poll();
            bazarish::log::info(
                "polled once as {}: {} update(s) handled", session.fingerprint(), handled);
            return 0;
        }
        bazarish::log::info(
            "bot running as {}; polling for updates (Ctrl-C to stop)...", session.fingerprint());
        bot.run(2000);
    } catch (const std::exception& error) {
        bazarish::log::error("{}", error.what());
        return 1;
    }
}
