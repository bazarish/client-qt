// Bazarish project (c) 2026
#include "Session.hpp"

#include "Invite.hpp"

#include <bazarish/Cms.hpp>
#include <bazarish/Tokens.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <fstream>
#include <set>
#include <stdexcept>
#include <thread>

namespace bazarish::client {

namespace {

namespace fs = std::filesystem;

// Tokens minted per batch handed to a contact. When a peer's stash of our
// tokens drops to kRefillThreshold, they signal it and we mint another batch
// (see Contacts.md refill) — so a conversation never runs dry.
constexpr int kTokenBatchSize = 64;
constexpr std::size_t kRefillThreshold = 16;

// Inner end-to-end payload format version (see docs Messages.md).
constexpr int kMessageFormatVersion = 1;

std::int64_t nowSeconds()
{
    return static_cast<std::int64_t>(std::time(nullptr));
}

// Applies a bootstrap block (the peer's sealing key, serving server, server
// card and a fresh token batch) carried by a contact request, a first reply
// or a token refill. Orthogonal to the message's content type.
void applyBootstrap(Contact& contact, const nlohmann::json& bootstrap)
{
    if (bootstrap.contains("sealing")) {
        contact.sealingPublicB64 = bootstrap.at("sealing").get<std::string>();
    }
    if (bootstrap.contains("server")) {
        contact.server = bootstrap.at("server").get<std::string>();
    }
    if (bootstrap.contains("serverCard")) {
        const ServerCard card
            = ServerCard::verify(fromBase64(bootstrap.at("serverCard").get<std::string>()));
        contact.serverSealingB64 = toBase64(card.sealingPublicKeyDer);
    }
    if (bootstrap.contains("replyTokens")) {
        for (const nlohmann::json& token : bootstrap.at("replyTokens")) {
            contact.sendTokens.push_back(token.get<std::string>());
        }
    }
}

std::string readFileText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeFileText(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to open " + path.string());
    }
    out << text;
    if (!out) {
        throw std::runtime_error("failed to write " + path.string());
    }
}

Bytes readFileBytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open " + path.string());
    }
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeFileBytes(const fs::path& path, const Bytes& data)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to open " + path.string());
    }
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) {
        throw std::runtime_error("failed to write " + path.string());
    }
}

// Builds the JSON wire form of an inline keyboard: an array of rows, each row
// an array of buttons. A button carries its label plus a callback "data" or a
// "command"; empty actions are omitted so the shape stays minimal.
nlohmann::json keyboardToJson(const InlineKeyboard& keyboard)
{
    nlohmann::json rows = nlohmann::json::array();
    for (const std::vector<InlineButton>& row : keyboard) {
        nlohmann::json jsonRow = nlohmann::json::array();
        for (const InlineButton& button : row) {
            nlohmann::json jsonButton = {{"text", button.text}};
            if (!button.data.empty()) {
                jsonButton["data"] = button.data;
            } else if (!button.command.empty()) {
                jsonButton["command"] = button.command;
            }
            jsonRow.push_back(std::move(jsonButton));
        }
        rows.push_back(std::move(jsonRow));
    }
    return rows;
}

// A best-effort MIME guess from the extension. Content type rendering is a
// client concern; the server never sees this.
std::string guessMime(const fs::path& path)
{
    const std::string ext = path.extension().string();
    if (ext == ".jpg" || ext == ".jpeg") {
        return "image/jpeg";
    }
    if (ext == ".png") {
        return "image/png";
    }
    if (ext == ".gif") {
        return "image/gif";
    }
    if (ext == ".pdf") {
        return "application/pdf";
    }
    if (ext == ".txt") {
        return "text/plain";
    }
    if (ext == ".ogg" || ext == ".opus") {
        return "audio/ogg";
    }
    if (ext == ".mp3") {
        return "audio/mpeg";
    }
    return "application/octet-stream";
}

}  // namespace

std::string inlineKeyboardJson(const InlineKeyboard& keyboard)
{
    return keyboardToJson(keyboard).dump();
}

Session::Session(fs::path stateDir, std::unique_ptr<Client> client, Key sealingKey,
    std::map<std::string, Contact> contacts)
    : stateDir_(std::move(stateDir))
    , client_(std::move(client))
    , sealingKey_(std::move(sealingKey))
    , contacts_(std::move(contacts))
{
}

Session Session::create(
    const fs::path& stateDir, const std::string& passphrase, const std::string& name)
{
    fs::create_directories(stateDir);

    Identity identity = Identity::generate();
    writeFileText(stateDir / "identity.pem", identity.privatePem(passphrase));

    Key sealing = Key::generateSealing();
    writeFileText(stateDir / "sealing.pem", sealing.privatePem(passphrase));

    const std::string clientId = toHex(randomBytes(8));
    const bool encrypted = !passphrase.empty();
    const std::string fingerprint = identity.fingerprint();

    // A fresh profile has no server yet: an empty host marks "unconnected".
    ServerEndpoint endpoint;
    endpoint.host.clear();
    endpoint.port = 0;

    const nlohmann::json meta = {
        {"clientId", clientId},
        {"name", name},
        {"fingerprint", fingerprint},
        {"endpoint",
            {
                {"tls", endpoint.tls},
                {"host", endpoint.host},
                {"port", endpoint.port},
                {"basePath", endpoint.basePath},
                {"serverFingerprint", endpoint.serverFingerprint},
                {"facades", nlohmann::json::array()},
            }},
        {"serverCard", ""},
        {"subscriptionCert", ""},
        {"encrypted", encrypted},
    };
    writeFileText(stateDir / "meta.json", meta.dump(2));

    auto client = std::make_unique<Client>(std::move(identity), clientId, endpoint);
    Session session(stateDir, std::move(client), std::move(sealing), {});
    session.encrypted_ = encrypted;
    session.passphrase_ = passphrase;
    session.name_ = name;
    return session;
}

Session Session::create(const fs::path& stateDir, const ServerEndpoint& endpoint,
    const std::string& passphrase)
{
    Session session = create(stateDir, passphrase);
    session.connectServer(endpoint);
    return session;
}

void Session::connectServer(const ServerEndpoint& endpoint)
{
    // Rebind the transport to the new server, reusing the identity and client
    // id. The in-memory identity PEM is unencrypted, so this is independent of
    // the at-rest passphrase.
    client_ = std::make_unique<Client>(
        Identity::fromPrivatePem(client_->identity().privatePem()), client_->clientId(), endpoint);
    persistMeta();
}

bool Session::isConnected() const
{
    return !client_->endpoint().host.empty();
}

const ServerEndpoint& Session::endpoint() const
{
    return client_->endpoint();
}

std::string Session::activeFacadeUrl() const
{
    return client_->activeFacadeUrl();
}

std::vector<std::string> Session::facadeUrls() const
{
    const ServerEndpoint& endpoint = client_->endpoint();
    std::vector<std::string> urls;
    if (endpoint.facades.empty()) {
        if (!endpoint.host.empty()) {
            urls.push_back(
                facadeToUrl(Facade{endpoint.tls, endpoint.host, endpoint.port, endpoint.basePath}));
        }
    } else {
        for (const Facade& facade : endpoint.facades) {
            urls.push_back(facadeToUrl(facade));
        }
    }
    return urls;
}

Session Session::open(const fs::path& stateDir, const std::string& passphrase)
{
    const nlohmann::json meta = nlohmann::json::parse(readFileText(stateDir / "meta.json"));
    ServerEndpoint endpoint;
    const nlohmann::json& endpointJson = meta.at("endpoint");
    endpoint.tls = endpointJson.value("tls", false);
    endpoint.host = endpointJson.at("host").get<std::string>();
    endpoint.port = endpointJson.at("port").get<int>();
    endpoint.basePath = endpointJson.at("basePath").get<std::string>();
    endpoint.serverFingerprint = endpointJson.at("serverFingerprint").get<std::string>();
    if (endpointJson.contains("facades")) {
        for (const nlohmann::json& url : endpointJson.at("facades")) {
            endpoint.facades.push_back(parseFacadeUrl(url.get<std::string>()));
        }
    }

    const bool encrypted = meta.value("encrypted", false);
    if (encrypted && passphrase.empty()) {
        throw std::runtime_error(
            "session keys are encrypted: set BAZARISH_PASSPHRASE");
    }

    Identity identity
        = Identity::fromPrivatePem(readFileText(stateDir / "identity.pem"), passphrase);
    Key sealing = Key::fromPrivatePem(readFileText(stateDir / "sealing.pem"), passphrase);
    const std::string clientId = meta.at("clientId").get<std::string>();

    std::map<std::string, Contact> contacts;
    const fs::path contactsPath = stateDir / "contacts.json";
    if (fs::exists(contactsPath)) {
        const std::string raw = readFileText(contactsPath);
        // When the profile is encrypted the file is a CMS PWRI blob holding
        // the contacts JSON; otherwise it is the JSON itself.
        const nlohmann::json stored = encrypted
            ? nlohmann::json::parse(cms::unsealWithPassword(Bytes(raw.begin(), raw.end()), passphrase))
            : nlohmann::json::parse(raw);
        for (const auto& [fingerprint, entry] : stored.items()) {
            Contact contact;
            contact.sealingPublicB64 = entry.at("sealingPublicB64").get<std::string>();
            contact.server = entry.at("server").get<std::string>();
            contact.serverSealingB64 = entry.at("serverSealingB64").get<std::string>();
            contact.sendTokens = entry.at("sendTokens").get<std::vector<std::string>>();
            contact.issuedToThem = entry.at("issuedToThem").get<bool>();
            contacts.emplace(fingerprint, std::move(contact));
        }
    }

    auto client = std::make_unique<Client>(std::move(identity), clientId, endpoint);
    Session session(stateDir, std::move(client), std::move(sealing), std::move(contacts));
    session.serverCardB64_ = meta.at("serverCard").get<std::string>();
    session.subscriptionCertB64_ = meta.value("subscriptionCert", std::string{});
    session.encrypted_ = encrypted;
    session.passphrase_ = passphrase;
    session.name_ = meta.value("name", std::string{});

    // Load groups (mirrors contacts: a sealed blob when the profile is encrypted).
    const fs::path groupsPath = stateDir / "groups.json";
    if (fs::exists(groupsPath)) {
        const std::string raw = readFileText(groupsPath);
        const nlohmann::json stored = encrypted
            ? nlohmann::json::parse(
                  cms::unsealWithPassword(Bytes(raw.begin(), raw.end()), passphrase))
            : nlohmann::json::parse(raw);
        for (const auto& [groupId, entry] : stored.items()) {
            Group group;
            group.name = entry.value("name", std::string());
            group.epoch = entry.value("epoch", std::int64_t{0});
            group.iAmAdmin = entry.value("iAmAdmin", false);
            group.myPoolHashes
                = entry.value("myPoolHashes", std::vector<std::string>{});
            for (const auto& [fp, jm] : entry.at("members").items()) {
                GroupMember member;
                member.sealingPublicB64 = jm.at("sealing").get<std::string>();
                member.server = jm.at("server").get<std::string>();
                member.serverSealingB64 = jm.at("serverSealing").get<std::string>();
                member.sendTokens = jm.at("sendTokens").get<std::vector<std::string>>();
                member.admin = jm.value("admin", false);
                group.members.emplace(fp, std::move(member));
            }
            session.groups_.emplace(groupId, std::move(group));
        }
    }
    return session;
}

std::string Session::fingerprint() const
{
    return client_->identity().fingerprint();
}

const std::string& Session::displayName() const
{
    return name_;
}

std::string Session::sealingPublicB64() const
{
    return toBase64(sealingKey_.publicDer());
}

bool Session::hasContact(const std::string& peerFingerprint) const
{
    return contacts_.find(peerFingerprint) != contacts_.end();
}

std::vector<std::string> Session::contactFingerprints() const
{
    std::vector<std::string> fingerprints;
    fingerprints.reserve(contacts_.size());
    for (const auto& [fingerprint, contact] : contacts_) {
        (void)contact;
        fingerprints.push_back(fingerprint);
    }
    return fingerprints;
}

ContactInfo Session::lookupContactAt(const std::string& host, const int port,
    const std::string& basePath, const std::string& peerFingerprint) const
{
    // Default to our own facade; a non-empty host targets the peer's facade
    // (cross-server first contact).
    if (host.empty()) {
        return client_->lookupContact(peerFingerprint);
    }
    ServerEndpoint endpoint;
    endpoint.host = host;
    endpoint.port = port;
    endpoint.basePath = basePath;
    Client remote(Identity::fromPrivatePem(client_->identity().privatePem()),
        client_->clientId(), endpoint);
    return remote.lookupContact(peerFingerprint);
}

void Session::persistMeta() const
{
    const ServerEndpoint& endpoint = client_->endpoint();
    nlohmann::json facades = nlohmann::json::array();
    for (const Facade& facade : endpoint.facades) {
        facades.push_back(facadeToUrl(facade));
    }
    const nlohmann::json meta = {
        {"clientId", client_->clientId()},
        {"name", name_},
        {"fingerprint", client_->identity().fingerprint()},
        {"endpoint",
            {
                {"tls", endpoint.tls},
                {"host", endpoint.host},
                {"port", endpoint.port},
                {"basePath", endpoint.basePath},
                {"serverFingerprint", endpoint.serverFingerprint},
                {"facades", facades},
            }},
        {"serverCard", serverCardB64_},
        {"subscriptionCert", subscriptionCertB64_},
        {"encrypted", encrypted_},
    };
    writeFileText(stateDir_ / "meta.json", meta.dump(2));
}

nlohmann::json Session::contactsToJson() const
{
    nlohmann::json stored = nlohmann::json::object();
    for (const auto& [fingerprint, contact] : contacts_) {
        stored[fingerprint] = {
            {"sealingPublicB64", contact.sealingPublicB64},
            {"server", contact.server},
            {"serverSealingB64", contact.serverSealingB64},
            {"sendTokens", contact.sendTokens},
            {"issuedToThem", contact.issuedToThem},
        };
    }
    return stored;
}

void Session::persistContacts() const
{
    const nlohmann::json stored = contactsToJson();
    if (encrypted_) {
        // Delivery tokens are write capabilities into a peer's mailbox: seal
        // the file at rest under the profile passphrase (CMS PWRI).
        const std::string text = stored.dump();
        const Bytes sealed = cms::sealWithPassword(Bytes(text.begin(), text.end()), passphrase_);
        writeFileText(stateDir_ / "contacts.json", std::string(sealed.begin(), sealed.end()));
        return;
    }
    writeFileText(stateDir_ / "contacts.json", stored.dump(2));
}

void Session::subscribe(const std::int64_t days)
{
    const std::int64_t now = nowSeconds();
    // Publish our sealing key as a prekey so contacts can encrypt their very
    // first message to us before any token exchange.
    const SubscribeResult result
        = client_->subscribe(now, now + days * 24 * 3600, sealingKey_.publicDer());
    serverCardB64_ = toBase64(result.serverCardDer);
    subscriptionCertB64_ = toBase64(result.subscriptionCertDer);
    persistMeta();
    client_->registerThisClient();
}

void Session::registerAlias(const std::string& alias)
{
    client_->registerAlias(alias, nowSeconds(), std::nullopt);
}

std::vector<std::string> Session::issueTokenBatch()
{
    std::vector<std::string> tokens;
    std::vector<Bytes> hashes;
    tokens.reserve(kTokenBatchSize);
    hashes.reserve(kTokenBatchSize);
    for (int i = 0; i < kTokenBatchSize; ++i) {
        const Bytes token = generateDeliveryToken();
        tokens.push_back(toBase64(token));
        hashes.push_back(deliveryTokenHash(token));
    }
    client_->registerTokenHashes(hashes);
    return tokens;
}

void Session::deliver(const std::string& toServer, const Key& serverSealingKey,
    const std::string& kind, const std::string& mailbox, const std::optional<Bytes>& token,
    const Bytes& payload, const std::function<void()>& onAcceptedByOwnServer, bool* tokenRejected)
{
    // The envelope is sealed to the destination server's sealing key, so the
    // routing metadata is readable only there. Our own server relays it to a
    // foreign destination over federation. messageId stays fixed across
    // retries: the recipient server dedups, so resubmits are idempotent and
    // never consume a second token.
    const std::string messageId = toHex(randomBytes(16));
    const Bytes sealed = sealDeliveryEnvelope(kind, mailbox, messageId, token, serverSealingKey);

    // A foreign server may be momentarily unreachable while its I2P leaseset
    // publishes; that is transient, so retry. Other failures are terminal.
    constexpr int kMaxRounds = 12;
    std::string lastError = "delivery not attempted";
    bool acceptedByOwnServer = false;
    for (int round = 0; round < kMaxRounds; ++round) {
        try {
            const std::string attemptId = client_->submitSend(toServer, sealed, payload);
            // Our own server accepted the envelope into its buffer: the "grey"
            // delivery state. Fire once.
            if (!acceptedByOwnServer) {
                acceptedByOwnServer = true;
                if (onAcceptedByOwnServer) {
                    onAcceptedByOwnServer();
                }
            }
            for (int poll = 0; poll < 50; ++poll) {
                const SendStatus status = client_->pollSend(attemptId);
                if (status.status == "delivered") {
                    return;
                }
                if (status.status == "failed") {
                    if (status.errorCode == ErrorCode::kRecipientServerUnreachable) {
                        lastError = status.errorMessage.empty() ? "recipient unreachable"
                                                                : status.errorMessage;
                        break;  // transient: retry this round
                    }
                    // A spent token (e.g. a concurrent group sender took it): let
                    // the caller retry with another token instead of failing.
                    if (tokenRejected != nullptr
                        && status.errorCode == ErrorCode::kDeliveryRejected) {
                        *tokenRejected = true;
                        return;
                    }
                    throw std::runtime_error("delivery failed: "
                        + (status.errorMessage.empty() ? std::string("unknown")
                                                        : status.errorMessage));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } catch (const ApiError& error) {
            // Transport-level failure (timeout, attempt expired): resubmit.
            lastError = error.what();
        }
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    throw std::runtime_error("delivery did not complete after retries: " + lastError);
}

void Session::sendContactRequest(const std::string& peerFingerprint, const std::string& text,
    const std::string& peerHost, const int peerPort, const std::string& peerBasePath)
{
    // Resolve the peer's prekey, serving server and server card. The prekey
    // is signed by the peer (subscription certificate) and the server card by
    // the peer's server, so neither can be substituted by an intermediary.
    const ContactInfo info
        = lookupContactAt(peerHost, peerPort, peerBasePath, peerFingerprint);
    requestWithInfo(peerFingerprint, text, info);
}

void Session::addByInvite(const std::string& inviteUri, const std::string& text)
{
    // The invite carries the full chain; verify it offline. SubscriptionCertificate
    // and ServerCard verification bind every field to a signature, so a tampered
    // invite is rejected with no server involved at all.
    const Invite invite = decodeInvite(inviteUri);
    ContactInfo info;
    info.subscriptionCert = SubscriptionCertificate::verify(invite.subscriptionCertDer);
    info.serverCard = ServerCard::verify(invite.serverCardDer);
    requestWithInfo(info.subscriptionCert.user, text, info);
}

void Session::addByUsername(const std::string& alias, const std::string& text,
    const std::string& host, const int port, const std::string& basePath)
{
    // Resolve the alias to a fingerprint. This mapping is the one trust
    // compromise of the username path: a hostile resolver could return an
    // attacker's fingerprint. Everything after the mapping — the contact
    // lookup and its certificates — is verified end-to-end as usual.
    std::string fingerprint;
    if (host.empty()) {
        fingerprint = client_->resolve(alias).user;
    } else {
        ServerEndpoint endpoint;
        endpoint.host = host;
        endpoint.port = port;
        endpoint.basePath = basePath;
        Client remote(Identity::fromPrivatePem(client_->identity().privatePem()),
            client_->clientId(), endpoint);
        fingerprint = remote.resolve(alias).user;
    }
    sendContactRequest(fingerprint, text, host, port, basePath);
}

void Session::requestWithInfo(const std::string& peerFingerprint, const std::string& text,
    const ContactInfo& info)
{
    if (info.subscriptionCert.user != peerFingerprint) {
        throw std::runtime_error("contact lookup returned a different user");
    }
    if (info.subscriptionCert.server != info.serverCard.server) {
        throw std::runtime_error("subscription and server card disagree on the server");
    }
    const Key peerPrekey = info.subscriptionCert.sealingKey();
    const Key peerServerSealing = info.serverCard.sealingKey();

    // Mint a batch the peer will use to write back to us and hand it over,
    // with our sealing key and server card, inside the request's bootstrap.
    const std::vector<std::string> replyTokens = issueTokenBatch();

    const nlohmann::json payload = {
        {"v", kMessageFormatVersion},
        {"type", "contact.request"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowSeconds()},
        {"text", text},
        {"bootstrap",
            {
                {"sealing", sealingPublicB64()},
                {"server", client_->endpoint().serverFingerprint},
                {"serverCard", serverCardB64_},
                {"replyTokens", replyTokens},
            }},
    };
    // E2E-encrypted to the peer's prekey: the first message is confidential.
    // Delivered tokenless under the "contact" admission class.
    const std::string plain = payload.dump();
    const Bytes encrypted = cms::seal(Bytes(plain.begin(), plain.end()), peerPrekey);
    deliver(info.subscriptionCert.server, peerServerSealing, "contact", peerFingerprint,
        std::nullopt, encrypted);

    // We now know how to reach the peer; reciprocal tokens arrive with the
    // peer's reply.
    Contact& contact = contacts_[peerFingerprint];
    contact.server = info.subscriptionCert.server;
    contact.sealingPublicB64 = toBase64(peerPrekey.publicDer());
    contact.serverSealingB64 = toBase64(peerServerSealing.publicDer());
    contact.issuedToThem = true;
    persistContacts();
}

void Session::sendMessage(const std::string& peerFingerprint, const std::string& text,
    const std::string& messageId, const std::function<void()>& onAcceptedByOwnServer)
{
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "text"},
        {"id", messageId.empty() ? toHex(randomBytes(8)) : messageId},
        {"from", fingerprint()},
        {"sentAt", nowSeconds()},
        {"text", text},
    };
    sendContent(peerFingerprint, std::move(inner), onAcceptedByOwnServer);
}

void Session::sendFile(const std::string& peerFingerprint, const fs::path& path,
    const std::string& messageId, const std::function<void()>& onAcceptedByOwnServer)
{
    const Bytes data = readFileBytes(path);
    // Encrypt the bytes with a fresh random content key (the key is the CMS
    // password — standard primitives only). The store sees only ciphertext.
    const Bytes key = randomBytes(32);
    const std::string password(key.begin(), key.end());
    const Bytes ciphertext = cms::sealWithPassword(data, password);
    const std::string ref = client_->putContent(ciphertext);

    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "file"},
        {"id", messageId.empty() ? toHex(randomBytes(8)) : messageId},
        {"from", fingerprint()},
        {"sentAt", nowSeconds()},
        {"file",
            {
                {"ref", ref},
                {"key", toBase64(key)},
                {"size", data.size()},
                {"mime", guessMime(path)},
                {"name", path.filename().string()},
            }},
    };
    sendContent(peerFingerprint, std::move(inner), onAcceptedByOwnServer);
}

void Session::sendInteractive(const std::string& peerFingerprint, const std::string& text,
    const InlineKeyboard& keyboard, const std::string& messageId,
    const std::function<void()>& onAcceptedByOwnServer)
{
    // An interactive message is a "text" message that additionally carries an
    // inline keyboard. A recipient that does not understand keyboards still
    // renders the text; the registry stays forward-compatible.
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "text"},
        {"id", messageId.empty() ? toHex(randomBytes(8)) : messageId},
        {"from", fingerprint()},
        {"sentAt", nowSeconds()},
        {"text", text},
        {"keyboard", keyboardToJson(keyboard)},
    };
    sendContent(peerFingerprint, std::move(inner), onAcceptedByOwnServer);
}

void Session::sendCommand(const std::string& peerFingerprint, const std::string& command,
    const std::string& args, const std::string& messageId,
    const std::function<void()>& onAcceptedByOwnServer)
{
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "bot.command"},
        {"id", messageId.empty() ? toHex(randomBytes(8)) : messageId},
        {"from", fingerprint()},
        {"sentAt", nowSeconds()},
        {"command", command},
        {"args", args},
    };
    sendContent(peerFingerprint, std::move(inner), onAcceptedByOwnServer);
}

void Session::sendCallback(
    const std::string& peerFingerprint, const std::string& data, const std::string& refMessageId)
{
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "bot.callback"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowSeconds()},
        {"data", data},
        {"ref", refMessageId},
    };
    sendContent(peerFingerprint, std::move(inner));
}

void Session::sendEdit(const std::string& peerFingerprint, const std::string& refMessageId,
    const std::string& text, const InlineKeyboard& keyboard)
{
    // An edit fully replaces the target's text and keyboard; the keyboard is
    // always carried (an empty array clears it) so the shape is unambiguous.
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "edit"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowSeconds()},
        {"ref", refMessageId},
        {"text", text},
        {"keyboard", keyboardToJson(keyboard)},
    };
    sendContent(peerFingerprint, std::move(inner));
}

void Session::sendReceipt(const std::string& peerFingerprint, const std::string& refMessageId)
{
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "receipt"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowSeconds()},
        {"ref", refMessageId},
    };
    sendContent(peerFingerprint, std::move(inner));
}

void Session::saveAttachment(
    const std::string& ref, const std::string& keyB64, const fs::path& dest)
{
    const Bytes ciphertext = client_->getContent(ref);
    const Bytes key = fromBase64(keyB64);
    const std::string password(key.begin(), key.end());
    const Bytes plain = cms::unsealWithPassword(ciphertext, password);
    writeFileBytes(dest, plain);
}

void Session::sendContent(const std::string& peerFingerprint, nlohmann::json inner,
    const std::function<void()>& onAcceptedByOwnServer)
{
    const auto found = contacts_.find(peerFingerprint);
    if (found == contacts_.end()) {
        throw std::runtime_error("unknown contact: " + peerFingerprint);
    }
    Contact& contact = found->second;
    if (contact.sealingPublicB64.empty() || contact.serverSealingB64.empty()) {
        throw std::runtime_error("contact not established yet: " + peerFingerprint);
    }
    if (contact.sendTokens.empty()) {
        throw std::runtime_error("no delivery tokens left for contact: " + peerFingerprint);
    }

    // First reply to a peer that wrote to us first: hand them a bootstrap (our
    // routing + a token batch) so the reverse direction is usable too.
    if (!contact.issuedToThem) {
        inner["bootstrap"] = {
            {"sealing", sealingPublicB64()},
            {"server", client_->endpoint().serverFingerprint},
            {"serverCard", serverCardB64_},
            {"replyTokens", issueTokenBatch()},
        };
        contact.issuedToThem = true;
    }

    // After spending this token our stash for the peer would be this small;
    // ask them to refill us before it hits zero (Contacts.md).
    if (contact.sendTokens.size() - 1 <= kRefillThreshold) {
        inner["lowStash"] = true;
    }

    const std::string innerText = inner.dump();
    const Key peerSealing = Key::fromPublicDer(fromBase64(contact.sealingPublicB64));
    const Bytes payload = cms::seal(Bytes(innerText.begin(), innerText.end()), peerSealing);
    const Key peerServerSealing = Key::fromPublicDer(fromBase64(contact.serverSealingB64));

    const std::string token = contact.sendTokens.back();
    deliver(contact.server, peerServerSealing, "content", peerFingerprint, fromBase64(token),
        payload, onAcceptedByOwnServer);

    // Spend the token only after a successful delivery.
    contact.sendTokens.pop_back();
    persistContacts();
}

std::vector<IncomingMessage> Session::sync()
{
    std::vector<IncomingMessage> result;
    // Peers whose stash of our tokens is running low and who asked for a
    // refill; topped up after the fetch loop so we never write mid-iteration.
    std::set<std::string> refillPeers;
    // Groups we were just invited to: hand our token pool to their members after
    // the loop (same "never write mid-iteration" rule as refills).
    std::set<std::string> bootstrapGroups;
    // Groups where a member was removed: rotate our pool after the loop so the
    // removed member's stash of our tokens stops working.
    std::set<std::string> rotateGroups;
    bool groupsTouched = false;
    // Records the sender's group pool. Returns false when the group or member is
    // not known yet (e.g. the tokens arrived before the invite this same sync).
    const auto applyGroupTokens = [this](const std::string& groupId, const std::string& from,
                                      const nlohmann::json& tokens) -> bool {
        const auto group = groups_.find(groupId);
        if (group == groups_.end()) {
            return false;
        }
        const auto member = group->second.members.find(from);
        if (member == group->second.members.end()) {
            return false;
        }
        for (const nlohmann::json& token : tokens) {
            member->second.sendTokens.push_back(token.get<std::string>());
        }
        return true;
    };
    // Token grants that did not apply yet (unknown group/member), retried after
    // the loop so delivery order within a sync does not matter.
    struct DeferredTokens {
        std::string groupId;
        std::string from;
        nlohmann::json tokens;
    };
    std::vector<DeferredTokens> deferredTokens;
    for (const PendingEntry& entry : client_->listPending()) {
        const Bytes blob = client_->fetchBlob(entry.id);
        // Every item is sealed to our user sealing key the same way; the
        // server-visible delivery class never changes how we decrypt.
        const Bytes plain = cms::unseal(blob, sealingKey_);
        const nlohmann::json body = nlohmann::json::parse(plain.begin(), plain.end());

        IncomingMessage message;
        message.deliveryClass = entry.deliveryClass;
        message.fromFingerprint = body.at("from").get<std::string>();
        message.messageId = body.value("id", std::string());
        const std::string type = body.value("type", std::string("text"));

        // Bootstrap may ride with any content type; apply it before dispatch
        // so a new or migrated contact is established regardless of type.
        if (body.contains("bootstrap")) {
            applyBootstrap(contacts_[message.fromFingerprint], body.at("bootstrap"));
            message.establishedContact = true;
        }

        // The peer is low on our tokens and asked to be refilled.
        if (body.value("lowStash", false)) {
            refillPeers.insert(message.fromFingerprint);
        }

        // Content dispatch. An unknown type is still acked and surfaced (not
        // dropped) so a newer client could render it; see docs Messages.md.
        if (type == "text" || type == "contact.request") {
            message.contentType = type;
            message.text = body.value("text", std::string());
        } else if (type == "file" || type == "photo" || type == "audio" || type == "voice") {
            message.contentType = type;
            const nlohmann::json& file = body.at("file");
            message.attachmentRef = file.at("ref").get<std::string>();
            message.attachmentKeyB64 = file.at("key").get<std::string>();
            message.attachmentName = file.value("name", std::string());
            message.attachmentMime = file.value("mime", std::string());
            message.attachmentSize = file.value("size", std::uint64_t{0});
        } else if (type == "bot.command") {
            // A command invocation aimed at a bot: the command name and its
            // raw argument string. Surfaced as text too, for plain rendering.
            message.contentType = type;
            message.commandName = body.value("command", std::string());
            message.commandArgs = body.value("args", std::string());
            message.text = "/" + message.commandName
                + (message.commandArgs.empty() ? std::string() : " " + message.commandArgs);
        } else if (type == "bot.callback") {
            // A button press: the tapped button's payload and the keyboard
            // message it belongs to.
            message.contentType = type;
            message.callbackData = body.value("data", std::string());
            message.refId = body.value("ref", std::string());
        } else if (type == "edit") {
            // An in-place edit of a message the sender previously sent: the new
            // text (and keyboard, via the generic block below). refId is the
            // target message's id.
            message.contentType = type;
            message.refId = body.value("ref", std::string());
            message.text = body.value("text", std::string());
        } else if (type == "receipt") {
            // A delivery receipt for one of our sent messages (the "green"
            // state). Carries the acknowledged message id.
            message.contentType = type;
            message.refId = body.value("ref", std::string());
        } else if (type == "token-refill") {
            // The fresh tokens already arrived via the bootstrap block.
            message.contentType = type;
        } else if (type == "group.invite") {
            // Added to a group: verify and store the signed roster, then bootstrap
            // our token pool to its members after the loop.
            message.contentType = type;
            message.groupId = body.value("groupId", std::string());
            message.groupName = body.value("name", std::string());
            message.text = message.groupName;
            try {
                applyRoster(message.groupId, fromBase64(body.at("roster").get<std::string>()));
                bootstrapGroups.insert(message.groupId);
                groupsTouched = true;
            } catch (const std::exception&) {
                // Untrusted/malformed roster: surface the invite, do not join.
            }
        } else if (type == "group.tokens") {
            // A member's token pool for a group; record it so we can deliver to
            // them. Deferred when the invite has not been applied yet this sync.
            message.contentType = type;
            message.groupId = body.value("groupId", std::string());
            const nlohmann::json tokens = body.value("tokens", nlohmann::json::array());
            if (applyGroupTokens(message.groupId, message.fromFingerprint, tokens)) {
                groupsTouched = true;
            } else {
                deferredTokens.push_back({message.groupId, message.fromFingerprint, tokens});
            }
        } else if (type == "group.roster") {
            // A signed roster update (membership/admin/epoch). If it removed a
            // member, rotate our pool after the loop so their tokens die.
            message.contentType = type;
            message.groupId = body.value("groupId", std::string());
            try {
                bool shrank = false;
                applyRoster(
                    message.groupId, fromBase64(body.at("roster").get<std::string>()), &shrank);
                groupsTouched = true;
                if (shrank) {
                    rotateGroups.insert(message.groupId);
                }
            } catch (const std::exception&) {
            }
        } else if (type == "group.leave") {
            message.contentType = type;
            message.groupId = body.value("groupId", std::string());
            const auto group = groups_.find(message.groupId);
            if (group != groups_.end()) {
                group->second.members.erase(message.fromFingerprint);
                groupsTouched = true;
            }
        } else {
            message.contentType = "unsupported";
            message.rawType = type;
        }

        // A content message may belong to a group (filed under it, not the 1:1
        // thread). Orthogonal to the content type.
        if (body.contains("group")) {
            message.groupId = body.at("group").value("id", std::string());
        }

        // An inline keyboard may ride on any content message (typically text);
        // preserve it as its wire form so a UI can render the buttons.
        if (body.contains("keyboard")) {
            message.keyboardJson = body.at("keyboard").dump();
        }

        client_->ack(entry.id);
        result.push_back(std::move(message));
    }
    persistContacts();

    // Refill peers that ran low (a fresh token batch, sent as a token-refill).
    // Done after the loop so the outbound send never races the fetch loop.
    for (const std::string& peer : refillPeers) {
        sendTokenRefill(peer);
    }
    // Re-apply token grants whose group/invite was processed later in this sync.
    for (const DeferredTokens& deferred : deferredTokens) {
        if (applyGroupTokens(deferred.groupId, deferred.from, deferred.tokens)) {
            groupsTouched = true;
        }
    }
    // Hand our token pool to the members of any group we were just invited to.
    for (const std::string& groupId : bootstrapGroups) {
        broadcastGroupPool(groupId);
    }
    // Rotate our pool for groups where someone was removed (cut them off).
    for (const std::string& groupId : rotateGroups) {
        if (bootstrapGroups.find(groupId) == bootstrapGroups.end()) {
            rotateGroupPool(groupId);
        }
    }
    if (groupsTouched) {
        persistGroups();
    }
    return result;
}

void Session::sendTokenRefill(const std::string& peerFingerprint)
{
    const auto found = contacts_.find(peerFingerprint);
    if (found == contacts_.end()) {
        return;
    }
    const Contact& contact = found->second;
    // We need a usable route and at least one of the peer's tokens to deliver
    // the refill; otherwise the peer's own refill of us must arrive first.
    if (contact.sealingPublicB64.empty() || contact.serverSealingB64.empty()
        || contact.sendTokens.empty()) {
        return;
    }
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "token-refill"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowSeconds()},
        {"bootstrap", {{"replyTokens", issueTokenBatch()}}},
    };
    sendContent(peerFingerprint, std::move(inner));
}

// ============================ Groups ============================

std::string Session::ownServerSealingB64() const
{
    if (serverCardB64_.empty()) {
        throw std::runtime_error("no server card: subscribe first");
    }
    const ServerCard card = ServerCard::verify(fromBase64(serverCardB64_));
    return toBase64(card.sealingPublicKeyDer);
}

std::string Session::signedRosterB64(const std::string& groupId) const
{
    const Group& group = groups_.at(groupId);
    nlohmann::json members = nlohmann::json::array();
    nlohmann::json admins = nlohmann::json::array();

    // Our own entry first (with our routing), then every other member.
    members.push_back({
        {"fp", fingerprint()},
        {"sealing", sealingPublicB64()},
        {"server", client_->endpoint().serverFingerprint},
        {"serverSealing", ownServerSealingB64()},
        {"admin", group.iAmAdmin},
    });
    if (group.iAmAdmin) {
        admins.push_back(fingerprint());
    }
    for (const auto& [fp, member] : group.members) {
        members.push_back({
            {"fp", fp},
            {"sealing", member.sealingPublicB64},
            {"server", member.server},
            {"serverSealing", member.serverSealingB64},
            {"admin", member.admin},
        });
        if (member.admin) {
            admins.push_back(fp);
        }
    }
    const nlohmann::json body = {
        {"v", 1},
        {"groupId", groupId},
        {"name", group.name},
        {"epoch", group.epoch},
        {"members", members},
        {"admins", admins},
    };
    return toBase64(cms::signJsonHybrid(body, client_->identity()));
}

void Session::applyRoster(const std::string& groupId, const Bytes& rosterDer, bool* membershipShrank)
{
    // The roster is self-verifying: the embedded signature yields the signer's
    // identity fingerprint, which must be in the roster's admin set.
    const cms::VerifiedHybridJson verified = cms::verifyJsonHybrid(rosterDer);
    const nlohmann::json& body = verified.body;
    if (body.at("groupId").get<std::string>() != groupId) {
        throw std::runtime_error("group roster id mismatch");
    }
    bool signerIsAdmin = false;
    for (const nlohmann::json& admin : body.at("admins")) {
        if (admin.get<std::string>() == verified.identityFingerprint) {
            signerIsAdmin = true;
            break;
        }
    }
    if (!signerIsAdmin) {
        throw std::runtime_error("group roster not signed by an admin");
    }

    Group& group = groups_[groupId];
    const std::int64_t epoch = body.value("epoch", std::int64_t{0});
    if (epoch < group.epoch) {
        return;  // stale roster
    }

    // Preserve pool tokens we already hold for members that remain.
    std::map<std::string, std::vector<std::string>> keptTokens;
    for (const auto& [fp, member] : group.members) {
        keptTokens[fp] = member.sendTokens;
    }

    group.name = body.value("name", group.name);
    group.epoch = epoch;
    group.iAmAdmin = false;
    group.members.clear();
    const std::string me = fingerprint();
    for (const nlohmann::json& jm : body.at("members")) {
        const std::string fp = jm.at("fp").get<std::string>();
        const bool admin = jm.value("admin", false);
        if (fp == me) {
            group.iAmAdmin = admin;
            continue;  // we never store ourselves as a member
        }
        GroupMember member;
        member.sealingPublicB64 = jm.at("sealing").get<std::string>();
        member.server = jm.at("server").get<std::string>();
        member.serverSealingB64 = jm.at("serverSealing").get<std::string>();
        member.admin = admin;
        const auto kept = keptTokens.find(fp);
        if (kept != keptTokens.end()) {
            member.sendTokens = kept->second;
        }
        group.members.emplace(fp, std::move(member));
    }

    // A member that was present before but is gone now is a removal: signal the
    // caller to rotate its pool so the removed member's tokens stop working.
    if (membershipShrank != nullptr) {
        for (const auto& [fp, tokens] : keptTokens) {
            (void)tokens;
            if (group.members.find(fp) == group.members.end()) {
                *membershipShrank = true;
                break;
            }
        }
    }
}

void Session::sendToMemberContact(
    const std::string& memberFp, const GroupMember& member, const nlohmann::json& inner)
{
    const std::string text = inner.dump();
    const Key memberSealing = Key::fromPublicDer(fromBase64(member.sealingPublicB64));
    const Bytes payload = cms::seal(Bytes(text.begin(), text.end()), memberSealing);
    const Key memberServerSealing = Key::fromPublicDer(fromBase64(member.serverSealingB64));
    // Tokenless contact-class delivery — the standing path into any mailbox.
    deliver(member.server, memberServerSealing, "contact", memberFp, std::nullopt, payload);
}

void Session::sendToMemberContent(
    const std::string& groupId, const std::string& memberFp, const nlohmann::json& inner)
{
    Group& group = groups_.at(groupId);
    GroupMember& member = group.members.at(memberFp);
    const std::string text = inner.dump();
    const Key memberSealing = Key::fromPublicDer(fromBase64(member.sealingPublicB64));
    const Bytes payload = cms::seal(Bytes(text.begin(), text.end()), memberSealing);
    const Key memberServerSealing = Key::fromPublicDer(fromBase64(member.serverSealingB64));

    // Optimistic retry: the pool is shared, so a token may have been spent by a
    // concurrent sender. Drop a rejected token and try the next until one is
    // accepted or the pool is empty (then the member must refill us).
    while (!member.sendTokens.empty()) {
        const std::string token = member.sendTokens.back();
        member.sendTokens.pop_back();
        bool rejected = false;
        deliver(member.server, memberServerSealing, "content", memberFp, fromBase64(token), payload,
            {}, &rejected);
        if (!rejected) {
            return;
        }
    }
    throw std::runtime_error("no usable group token for member " + memberFp);
}

std::vector<std::string> Session::issueGroupPool(Group& group)
{
    std::vector<std::string> tokens;
    std::vector<Bytes> hashes;
    tokens.reserve(kTokenBatchSize);
    hashes.reserve(kTokenBatchSize);
    for (int i = 0; i < kTokenBatchSize; ++i) {
        const Bytes token = generateDeliveryToken();
        tokens.push_back(toBase64(token));
        hashes.push_back(deliveryTokenHash(token));
    }
    client_->registerTokenHashes(hashes);
    group.myPoolHashes.clear();
    group.myPoolHashes.reserve(hashes.size());
    for (const Bytes& hash : hashes) {
        group.myPoolHashes.push_back(toBase64(hash));
    }
    return tokens;
}

void Session::revokeGroupPool(Group& group)
{
    if (group.myPoolHashes.empty()) {
        return;
    }
    std::vector<Bytes> hashes;
    hashes.reserve(group.myPoolHashes.size());
    for (const std::string& hash : group.myPoolHashes) {
        hashes.push_back(fromBase64(hash));
    }
    client_->deleteTokenHashes(hashes);
    group.myPoolHashes.clear();
}

void Session::broadcastGroupPool(const std::string& groupId)
{
    Group& group = groups_.at(groupId);
    // One pool registered with our own server, the same raw tokens handed to
    // every member so each can deliver to us.
    const std::vector<std::string> pool = issueGroupPool(group);
    const nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "group.tokens"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowSeconds()},
        {"groupId", groupId},
        {"tokens", pool},
    };
    for (const auto& [fp, member] : group.members) {
        sendToMemberContact(fp, member, inner);
    }
}

std::string Session::createGroup(
    const std::string& name, const std::vector<std::string>& memberFingerprints)
{
    if (serverCardB64_.empty()) {
        throw std::runtime_error("subscribe first: a group needs our serving chain");
    }
    const std::string groupId = toHex(randomBytes(16));
    Group& group = groups_[groupId];
    group.name = name;
    group.epoch = 1;
    group.iAmAdmin = true;
    for (const std::string& fp : memberFingerprints) {
        const auto found = contacts_.find(fp);
        if (found == contacts_.end() || found->second.sealingPublicB64.empty()
            || found->second.serverSealingB64.empty()) {
            throw std::runtime_error("group member is not an established contact: " + fp);
        }
        GroupMember member;
        member.sealingPublicB64 = found->second.sealingPublicB64;
        member.server = found->second.server;
        member.serverSealingB64 = found->second.serverSealingB64;
        group.members.emplace(fp, std::move(member));
    }
    persistGroups();

    // Invite each member with the signed roster (over the established contact),
    // then hand everyone our token pool.
    const std::string roster = signedRosterB64(groupId);
    for (const auto& [fp, member] : group.members) {
        (void)member;
        nlohmann::json inner = {
            {"v", kMessageFormatVersion},
            {"type", "group.invite"},
            {"id", toHex(randomBytes(8))},
            {"from", fingerprint()},
            {"sentAt", nowSeconds()},
            {"groupId", groupId},
            {"name", name},
            {"roster", roster},
        };
        sendContent(fp, std::move(inner));
    }
    broadcastGroupPool(groupId);
    persistGroups();
    return groupId;
}

void Session::sendGroupMessage(const std::string& groupId, const std::string& text)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        throw std::runtime_error("unknown group: " + groupId);
    }
    Group& group = found->second;
    // One logical message id shared across the fan-out.
    const std::string id = toHex(randomBytes(8));
    std::vector<std::string> targets;
    for (const auto& [fp, member] : group.members) {
        if (!member.sendTokens.empty()) {
            targets.push_back(fp);
        }
    }
    for (const std::string& fp : targets) {
        nlohmann::json inner = {
            {"v", kMessageFormatVersion},
            {"type", "text"},
            {"id", id},
            {"from", fingerprint()},
            {"sentAt", nowSeconds()},
            {"text", text},
            {"group", {{"id", groupId}}},
        };
        // Best-effort fan-out: a member we cannot currently reach (pool drained)
        // is skipped, not allowed to abort delivery to the others.
        try {
            sendToMemberContent(groupId, fp, std::move(inner));
        } catch (const std::exception&) {
        }
    }
    persistGroups();
}

void Session::rotateGroupPool(const std::string& groupId)
{
    Group& group = groups_.at(groupId);
    revokeGroupPool(group);
    broadcastGroupPool(groupId);  // issues a fresh pool to the current members
}

void Session::broadcastRoster(const std::string& groupId)
{
    Group& group = groups_.at(groupId);
    const std::string roster = signedRosterB64(groupId);
    const nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "group.roster"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowSeconds()},
        {"groupId", groupId},
        {"roster", roster},
    };
    // Tokenless contact class so a critical roster update never fails on a
    // drained pool.
    for (const auto& [fp, member] : group.members) {
        sendToMemberContact(fp, member, inner);
    }
}

bool Session::isGroupAdmin(const std::string& groupId) const
{
    const auto found = groups_.find(groupId);
    return found != groups_.end() && found->second.iAmAdmin;
}

void Session::addGroupMembers(
    const std::string& groupId, const std::vector<std::string>& memberFingerprints)
{
    Group& group = groups_.at(groupId);
    if (!group.iAmAdmin) {
        throw std::runtime_error("only a group admin can add members");
    }
    std::vector<std::string> added;
    for (const std::string& fp : memberFingerprints) {
        if (fp == fingerprint() || group.members.count(fp) != 0) {
            continue;
        }
        const auto contact = contacts_.find(fp);
        if (contact == contacts_.end() || contact->second.sealingPublicB64.empty()
            || contact->second.serverSealingB64.empty()) {
            throw std::runtime_error("new group member is not an established contact: " + fp);
        }
        GroupMember member;
        member.sealingPublicB64 = contact->second.sealingPublicB64;
        member.server = contact->second.server;
        member.serverSealingB64 = contact->second.serverSealingB64;
        group.members.emplace(fp, std::move(member));
        added.push_back(fp);
    }
    if (added.empty()) {
        return;
    }
    group.epoch += 1;
    persistGroups();

    const std::string roster = signedRosterB64(groupId);
    for (const std::string& fp : added) {
        nlohmann::json invite = {
            {"v", kMessageFormatVersion},
            {"type", "group.invite"},
            {"id", toHex(randomBytes(8))},
            {"from", fingerprint()},
            {"sentAt", nowSeconds()},
            {"groupId", groupId},
            {"name", group.name},
            {"roster", roster},
        };
        sendContent(fp, std::move(invite));
    }
    broadcastRoster(groupId);     // existing members learn the new roster
    broadcastGroupPool(groupId);  // hand the new members our pool
    persistGroups();
}

void Session::removeGroupMember(const std::string& groupId, const std::string& memberFingerprint)
{
    Group& group = groups_.at(groupId);
    if (!group.iAmAdmin) {
        throw std::runtime_error("only a group admin can remove members");
    }
    if (group.members.erase(memberFingerprint) == 0) {
        return;
    }
    group.epoch += 1;
    persistGroups();
    broadcastRoster(groupId);   // remaining members get the roster without them
    rotateGroupPool(groupId);   // our old pool (which the removed member holds) stops working
    persistGroups();
}

void Session::setGroupAdmin(
    const std::string& groupId, const std::string& memberFingerprint, bool admin)
{
    Group& group = groups_.at(groupId);
    if (!group.iAmAdmin) {
        throw std::runtime_error("only a group admin can change admins");
    }
    const auto member = group.members.find(memberFingerprint);
    if (member == group.members.end()) {
        throw std::runtime_error("not a group member: " + memberFingerprint);
    }
    member->second.admin = admin;
    group.epoch += 1;
    persistGroups();
    broadcastRoster(groupId);
}

void Session::leaveGroup(const std::string& groupId)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        return;
    }
    Group& group = found->second;
    const nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "group.leave"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowSeconds()},
        {"groupId", groupId},
    };
    for (const auto& [fp, member] : group.members) {
        try {
            sendToMemberContact(fp, member, inner);
        } catch (const std::exception&) {
            // best effort
        }
    }
    revokeGroupPool(group);  // our pool stops working once we are gone
    groups_.erase(found);
    persistGroups();
}

std::vector<std::string> Session::groupIds() const
{
    std::vector<std::string> ids;
    ids.reserve(groups_.size());
    for (const auto& [id, group] : groups_) {
        (void)group;
        ids.push_back(id);
    }
    return ids;
}

std::string Session::groupName(const std::string& groupId) const
{
    const auto found = groups_.find(groupId);
    return found == groups_.end() ? std::string() : found->second.name;
}

std::vector<std::string> Session::groupMemberFingerprints(const std::string& groupId) const
{
    std::vector<std::string> fps;
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        return fps;
    }
    for (const auto& [fp, member] : found->second.members) {
        (void)member;
        fps.push_back(fp);
    }
    return fps;
}

nlohmann::json Session::groupsToJson() const
{
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [groupId, group] : groups_) {
        nlohmann::json members = nlohmann::json::object();
        for (const auto& [fp, member] : group.members) {
            members[fp] = {
                {"sealing", member.sealingPublicB64},
                {"server", member.server},
                {"serverSealing", member.serverSealingB64},
                {"sendTokens", member.sendTokens},
                {"admin", member.admin},
            };
        }
        out[groupId] = {
            {"name", group.name},
            {"epoch", group.epoch},
            {"iAmAdmin", group.iAmAdmin},
            {"myPoolHashes", group.myPoolHashes},
            {"members", members},
        };
    }
    return out;
}

void Session::persistGroups() const
{
    const nlohmann::json stored = groupsToJson();
    if (encrypted_) {
        const std::string text = stored.dump();
        const Bytes sealed = cms::sealWithPassword(Bytes(text.begin(), text.end()), passphrase_);
        writeFileText(stateDir_ / "groups.json", std::string(sealed.begin(), sealed.end()));
        return;
    }
    writeFileText(stateDir_ / "groups.json", stored.dump(2));
}

std::string Session::inviteUri() const
{
    if (subscriptionCertB64_.empty() || serverCardB64_.empty()) {
        throw std::runtime_error("subscribe first: no serving chain to publish");
    }
    Invite invite;
    invite.subscriptionCertDer = fromBase64(subscriptionCertB64_);
    invite.serverCardDer = fromBase64(serverCardB64_);
    return encodeInvite(invite);
}

void Session::exportState(const fs::path& outFile, const std::string& password) const
{
    const nlohmann::json meta = nlohmann::json::parse(readFileText(stateDir_ / "meta.json"));
    // Use the in-memory contacts: the on-disk file may be sealed, and the
    // bundle carries them in the clear (the bundle password is the protection).
    const nlohmann::json contacts = contactsToJson();

    // The keys are re-serialized unencrypted inside the bundle; the password
    // protects the bundle as a whole, decoupling the export from whatever
    // at-rest passphrase this state directory happens to use.
    const nlohmann::json bundle = {
        {"v", 1},
        {"identityPem", client_->identity().privatePem()},
        {"sealingPem", sealingKey_.privatePem()},
        {"meta", meta},
        {"contacts", contacts},
    };
    const std::string text = bundle.dump();
    const Bytes sealed = cms::sealWithPassword(Bytes(text.begin(), text.end()), password);
    writeFileText(outFile, std::string(sealed.begin(), sealed.end()));
}

void Session::importState(const fs::path& bundleFile, const fs::path& stateDir,
    const std::string& password, const std::string& atRestPassphrase)
{
    const std::string sealedText = readFileText(bundleFile);
    const Bytes plain
        = cms::unsealWithPassword(Bytes(sealedText.begin(), sealedText.end()), password);
    const nlohmann::json bundle = nlohmann::json::parse(plain.begin(), plain.end());

    fs::create_directories(stateDir);

    // Round-trip the keys through the crypto types so the imported PEMs adopt
    // the chosen at-rest scheme (encrypted iff a passphrase is given).
    const Identity identity = Identity::fromPrivatePem(bundle.at("identityPem").get<std::string>());
    const Key sealing = Key::fromPrivatePem(bundle.at("sealingPem").get<std::string>());
    writeFileText(stateDir / "identity.pem", identity.privatePem(atRestPassphrase));
    writeFileText(stateDir / "sealing.pem", sealing.privatePem(atRestPassphrase));

    nlohmann::json meta = bundle.at("meta");
    meta["encrypted"] = !atRestPassphrase.empty();
    writeFileText(stateDir / "meta.json", meta.dump(2));

    // Match the contacts file to the chosen at-rest scheme (sealed iff a
    // passphrase is given), mirroring the keys above.
    const nlohmann::json contacts = bundle.at("contacts");
    if (!atRestPassphrase.empty()) {
        const std::string text = contacts.dump();
        const Bytes sealed
            = cms::sealWithPassword(Bytes(text.begin(), text.end()), atRestPassphrase);
        writeFileText(stateDir / "contacts.json", std::string(sealed.begin(), sealed.end()));
    } else {
        writeFileText(stateDir / "contacts.json", contacts.dump(2));
    }
}

}  // namespace bazarish::client
