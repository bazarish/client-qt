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
                {"host", endpoint.host},
                {"port", endpoint.port},
                {"basePath", endpoint.basePath},
                {"serverFingerprint", endpoint.serverFingerprint},
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

Session Session::open(const fs::path& stateDir, const std::string& passphrase)
{
    const nlohmann::json meta = nlohmann::json::parse(readFileText(stateDir / "meta.json"));
    ServerEndpoint endpoint;
    endpoint.host = meta.at("endpoint").at("host").get<std::string>();
    endpoint.port = meta.at("endpoint").at("port").get<int>();
    endpoint.basePath = meta.at("endpoint").at("basePath").get<std::string>();
    endpoint.serverFingerprint
        = meta.at("endpoint").at("serverFingerprint").get<std::string>();

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
    const nlohmann::json meta = {
        {"clientId", client_->clientId()},
        {"name", name_},
        {"fingerprint", client_->identity().fingerprint()},
        {"endpoint",
            {
                {"host", client_->endpoint().host},
                {"port", client_->endpoint().port},
                {"basePath", client_->endpoint().basePath},
                {"serverFingerprint", client_->endpoint().serverFingerprint},
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
    const Bytes& payload, const std::function<void()>& onAcceptedByOwnServer)
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
        } else {
            message.contentType = "unsupported";
            message.rawType = type;
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
