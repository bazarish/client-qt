// Bazarish project (c) 2026
#include "Session.hpp"

#include <bazarish/Cms.hpp>
#include <bazarish/Tokens.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace bazarish::client {

namespace {

namespace fs = std::filesystem;

// Tokens minted per batch handed to a contact. A small fixed batch keeps the
// demo simple; refill-on-low-stash is a future client concern.
constexpr int kTokenBatchSize = 8;

std::int64_t nowSeconds()
{
    return static_cast<std::int64_t>(std::time(nullptr));
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

}  // namespace

Session::Session(fs::path stateDir, std::unique_ptr<Client> client, Key sealingKey,
    std::map<std::string, Contact> contacts)
    : stateDir_(std::move(stateDir))
    , client_(std::move(client))
    , sealingKey_(std::move(sealingKey))
    , contacts_(std::move(contacts))
{
}

Session Session::create(const fs::path& stateDir, const ServerEndpoint& endpoint)
{
    fs::create_directories(stateDir);

    Identity identity = Identity::generate();
    writeFileText(stateDir / "identity.pem", identity.privatePem());

    Key sealing = Key::generateSealing();
    writeFileText(stateDir / "sealing.pem", sealing.privatePem());

    const std::string clientId = toHex(randomBytes(8));

    const nlohmann::json meta = {
        {"clientId", clientId},
        {"endpoint",
            {
                {"host", endpoint.host},
                {"port", endpoint.port},
                {"basePath", endpoint.basePath},
                {"serverFingerprint", endpoint.serverFingerprint},
            }},
        {"serverCard", ""},
    };
    writeFileText(stateDir / "meta.json", meta.dump(2));

    auto client = std::make_unique<Client>(std::move(identity), clientId, endpoint);
    return Session(stateDir, std::move(client), std::move(sealing), {});
}

Session Session::open(const fs::path& stateDir)
{
    const nlohmann::json meta = nlohmann::json::parse(readFileText(stateDir / "meta.json"));
    ServerEndpoint endpoint;
    endpoint.host = meta.at("endpoint").at("host").get<std::string>();
    endpoint.port = meta.at("endpoint").at("port").get<int>();
    endpoint.basePath = meta.at("endpoint").at("basePath").get<std::string>();
    endpoint.serverFingerprint
        = meta.at("endpoint").at("serverFingerprint").get<std::string>();

    Identity identity = Identity::fromPrivatePem(readFileText(stateDir / "identity.pem"));
    Key sealing = Key::fromPrivatePem(readFileText(stateDir / "sealing.pem"));
    const std::string clientId = meta.at("clientId").get<std::string>();

    std::map<std::string, Contact> contacts;
    const fs::path contactsPath = stateDir / "contacts.json";
    if (fs::exists(contactsPath)) {
        const nlohmann::json stored = nlohmann::json::parse(readFileText(contactsPath));
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
    return session;
}

std::string Session::fingerprint() const
{
    return client_->identity().fingerprint();
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
        {"endpoint",
            {
                {"host", client_->endpoint().host},
                {"port", client_->endpoint().port},
                {"basePath", client_->endpoint().basePath},
                {"serverFingerprint", client_->endpoint().serverFingerprint},
            }},
        {"serverCard", serverCardB64_},
    };
    writeFileText(stateDir_ / "meta.json", meta.dump(2));
}

void Session::persistContacts() const
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
    persistMeta();
    client_->registerThisClient();
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
    const Bytes& payload)
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
    for (int round = 0; round < kMaxRounds; ++round) {
        try {
            const std::string attemptId = client_->submitSend(toServer, sealed, payload);
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
    if (info.subscriptionCert.user != peerFingerprint) {
        throw std::runtime_error("contact lookup returned a different user");
    }
    if (info.subscriptionCert.server != info.serverCard.server) {
        throw std::runtime_error("subscription and server card disagree on the server");
    }
    const Key peerPrekey = info.subscriptionCert.sealingKey();
    const Key peerServerSealing = info.serverCard.sealingKey();

    // Mint a batch the peer will use to write back to us and hand it over,
    // with our sealing key and server card, inside the request payload.
    const std::vector<std::string> replyTokens = issueTokenBatch();

    const nlohmann::json payload = {
        {"type", "contact-request"},
        {"fromFp", fingerprint()},
        {"fromSealing", sealingPublicB64()},
        {"fromServer", client_->endpoint().serverFingerprint},
        {"fromServerCard", serverCardB64_},
        {"replyTokens", replyTokens},
        {"text", text},
    };
    // E2E-encrypted to the peer's prekey: the first message is confidential.
    const std::string plain = payload.dump();
    const Bytes encrypted = cms::seal(Bytes(plain.begin(), plain.end()), peerPrekey);
    deliver(info.subscriptionCert.server, peerServerSealing, "contact-request",
        peerFingerprint, std::nullopt, encrypted);

    // We now know how to reach the peer; reciprocal tokens arrive with the
    // peer's reply.
    Contact& contact = contacts_[peerFingerprint];
    contact.server = info.subscriptionCert.server;
    contact.sealingPublicB64 = toBase64(peerPrekey.publicDer());
    contact.serverSealingB64 = toBase64(peerServerSealing.publicDer());
    contact.issuedToThem = true;
    persistContacts();
}

void Session::sendMessage(const std::string& peerFingerprint, const std::string& text)
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

    nlohmann::json inner = {
        {"type", "message"},
        {"fromFp", fingerprint()},
        {"fromSealing", sealingPublicB64()},
        {"fromServer", client_->endpoint().serverFingerprint},
        {"fromServerCard", serverCardB64_},
        {"text", text},
    };
    // First reply to a peer that wrote to us first: hand them a batch so the
    // reverse direction is usable too.
    if (!contact.issuedToThem) {
        inner["replyTokens"] = issueTokenBatch();
        contact.issuedToThem = true;
    }

    const std::string innerText = inner.dump();
    const Key peerSealing = Key::fromPublicDer(fromBase64(contact.sealingPublicB64));
    const Bytes payload = cms::seal(Bytes(innerText.begin(), innerText.end()), peerSealing);
    const Key peerServerSealing = Key::fromPublicDer(fromBase64(contact.serverSealingB64));

    const std::string token = contact.sendTokens.back();
    deliver(contact.server, peerServerSealing, "message", peerFingerprint, fromBase64(token),
        payload);

    // Spend the token only after a successful delivery.
    contact.sendTokens.pop_back();
    persistContacts();
}

std::vector<IncomingMessage> Session::sync()
{
    std::vector<IncomingMessage> result;
    for (const PendingEntry& entry : client_->listPending()) {
        const Bytes blob = client_->fetchBlob(entry.id);

        IncomingMessage message;
        message.kind = entry.kind;
        if (entry.kind == "contact-request") {
            // E2E-encrypted to our prekey, like a message.
            const Bytes plain = cms::unseal(blob, sealingKey_);
            const nlohmann::json body = nlohmann::json::parse(plain.begin(), plain.end());
            const std::string peerFp = body.at("fromFp").get<std::string>();
            Contact& contact = contacts_[peerFp];
            contact.sealingPublicB64 = body.at("fromSealing").get<std::string>();
            contact.server = body.at("fromServer").get<std::string>();
            const ServerCard card = ServerCard::verify(
                fromBase64(body.at("fromServerCard").get<std::string>()));
            contact.serverSealingB64 = toBase64(card.sealingPublicKeyDer);
            for (const nlohmann::json& token : body.at("replyTokens")) {
                contact.sendTokens.push_back(token.get<std::string>());
            }
            message.fromFingerprint = peerFp;
            message.text = body.at("text").get<std::string>();
            message.establishedContact = true;
        } else {
            const Bytes plain = cms::unseal(blob, sealingKey_);
            const nlohmann::json body = nlohmann::json::parse(plain.begin(), plain.end());
            const std::string peerFp = body.at("fromFp").get<std::string>();
            Contact& contact = contacts_[peerFp];
            if (body.contains("fromSealing")) {
                contact.sealingPublicB64 = body.at("fromSealing").get<std::string>();
            }
            if (body.contains("fromServer")) {
                contact.server = body.at("fromServer").get<std::string>();
            }
            if (body.contains("fromServerCard")) {
                const ServerCard card = ServerCard::verify(
                    fromBase64(body.at("fromServerCard").get<std::string>()));
                contact.serverSealingB64 = toBase64(card.sealingPublicKeyDer);
            }
            if (body.contains("replyTokens")) {
                for (const nlohmann::json& token : body.at("replyTokens")) {
                    contact.sendTokens.push_back(token.get<std::string>());
                }
                message.establishedContact = true;
            }
            message.fromFingerprint = peerFp;
            message.text = body.at("text").get<std::string>();
        }

        client_->ack(entry.id);
        result.push_back(std::move(message));
    }
    persistContacts();
    return result;
}

}  // namespace bazarish::client
