// Bazarish project (c) 2026
#include "Session.hpp"

#include "BlobTransport.hpp"
#include "FederationFetch.hpp"
#include "I2pKeys.hpp"
#include "LargeBlob.hpp"

#include <bazarish/Auth.hpp>
#include <bazarish/Cms.hpp>
#include <bazarish/Descriptor.hpp>
#include <bazarish/I2pAddress.hpp>
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

// Normalizes an alias to the resolver's canonical form: case-insensitive, 1-32
// characters of a-z and 0-9 (api/AliasResolver.md). Throws on an invalid name so
// a malformed query never reaches the resolver.
std::string normalizeAlias(const std::string& alias)
{
    if (alias.empty() || alias.size() > 32) {
        throw std::runtime_error("alias must be 1-32 characters");
    }
    std::string normalized;
    normalized.reserve(alias.size());
    for (const char c : alias) {
        char lower = c;
        if (c >= 'A' && c <= 'Z') {
            lower = static_cast<char>(c - 'A' + 'a');
        }
        const bool valid = (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9');
        if (!valid) {
            throw std::runtime_error("alias may contain only a-z and 0-9");
        }
        normalized.push_back(lower);
    }
    return normalized;
}

// Applies a bootstrap block (the peer's sealing prekey, serving destination +
// serving sealing key, and a fresh token batch) carried by a contact request, a
// first reply or a token refill. Orthogonal to the message's content type.
void applyBootstrap(Contact& contact, const nlohmann::json& bootstrap)
{
    if (bootstrap.contains("sealing")) {
        contact.sealingPublicB64 = bootstrap.at("sealing").get<std::string>();
    }
    if (bootstrap.contains("dest")) {
        contact.dest = bootstrap.at("dest").get<std::string>();
        validateB32I2pHost(contact.dest);
    }
    if (bootstrap.contains("servingKey")) {
        contact.servingSealingB64 = bootstrap.at("servingKey").get<std::string>();
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

    // A fresh profile has no server yet: an empty facade list marks "unconnected".
    const ServerEndpoint endpoint;

    const nlohmann::json meta = {
        {"clientId", clientId},
        {"name", name},
        {"fingerprint", fingerprint},
        {"endpoint",
            {
                {"serverFingerprint", endpoint.serverFingerprint},
                {"facades", nlohmann::json::array()},
            }},
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
    return !client_->endpoint().facades.empty();
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
    std::vector<std::string> urls;
    for (const Facade& facade : client_->endpoint().facades) {
        urls.push_back(facadeToUrl(facade));
    }
    return urls;
}

Session Session::open(const fs::path& stateDir, const std::string& passphrase)
{
    const nlohmann::json meta = nlohmann::json::parse(readFileText(stateDir / "meta.json"));
    ServerEndpoint endpoint;
    const nlohmann::json& endpointJson = meta.at("endpoint");
    endpoint.serverFingerprint = endpointJson.at("serverFingerprint").get<std::string>();
    for (const nlohmann::json& url : endpointJson.at("facades")) {
        endpoint.facades.push_back(parseFacadeUrl(url.get<std::string>()));
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
            contact.dest = entry.at("dest").get<std::string>();
            contact.servingSealingB64 = entry.at("servingSealingB64").get<std::string>();
            contact.sendTokens = entry.at("sendTokens").get<std::vector<std::string>>();
            contact.issuedToThem = entry.at("issuedToThem").get<bool>();
            contacts.emplace(fingerprint, std::move(contact));
        }
    }

    auto client = std::make_unique<Client>(std::move(identity), clientId, endpoint);
    Session session(stateDir, std::move(client), std::move(sealing), std::move(contacts));
    session.subscriptionCertB64_ = meta.value("subscriptionCert", std::string{});
    // Our own routing (dest + serving sealing key) lives in our self-signed
    // subscription certificate; recover it for invites and contact bootstraps.
    if (!session.subscriptionCertB64_.empty()) {
        const SubscriptionCertificate cert
            = SubscriptionCertificate::verify(fromBase64(session.subscriptionCertB64_));
        session.myDest_ = cert.dest;
        if (!cert.servingSealingKeyDer.empty()) {
            session.myServingKeyB64_ = toBase64(cert.servingSealingKeyDer);
        }
    }
    session.encrypted_ = encrypted;
    session.passphrase_ = passphrase;
    session.name_ = meta.value("name", std::string{});
    session.loadSentBlobs();

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
                member.dest = jm.at("dest").get<std::string>();
                member.servingSealingB64 = jm.at("servingKey").get<std::string>();
                member.sendTokens = jm.at("sendTokens").get<std::vector<std::string>>();
                member.admin = jm.value("admin", false);
                group.members.emplace(fp, std::move(member));
            }
            session.groups_.emplace(groupId, std::move(group));
        }
    }

    // Load the user-owned I2P destination, if this profile has one (per-user
    // path). Both blobs are sealed at rest when the profile is encrypted.
    const auto loadI2pBlob = [&](const char* filename) -> Bytes {
        const fs::path path = stateDir / filename;
        if (!fs::exists(path)) {
            return {};
        }
        const std::string raw = readFileText(path);
        const Bytes blob(raw.begin(), raw.end());
        return encrypted ? cms::unsealWithPassword(blob, passphrase) : blob;
    };
    session.i2pMaster_ = loadI2pBlob("i2p-master.dat");
    if (!session.i2pMaster_.empty()) {
        session.i2pAddress_ = i2pBase32(session.i2pMaster_);
    }
    session.i2pTransient_ = loadI2pBlob("i2p-transient.dat");

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
                {"serverFingerprint", endpoint.serverFingerprint},
                {"facades", facades},
            }},
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
            {"dest", contact.dest},
            {"servingSealingB64", contact.servingSealingB64},
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
    subscriptionCertB64_ = toBase64(result.subscriptionCertDer);
    myDest_ = result.dest;
    myServingKeyB64_
        = result.servingSealingKeyDer.empty() ? std::string() : toBase64(result.servingSealingKeyDer);
    persistMeta();
    client_->registerThisClient();

    // For a user-owned destination, refresh the transient delegation handed to
    // the serving server so it can operate the destination for this period.
    // The transient expiry tracks the subscription window (kept short — the
    // server only ever holds a time-boxed capability, never the master).
    if (hasI2pDestination()) {
        const std::int64_t expiresUnix = now + days * 24 * 3600;
        renewI2pTransient(expiresUnix);
        // Best effort: hand the serving server the transient so it can operate
        // the personal destination for this window. Requires a paid i2pDest
        // entitlement; a failure (no entitlement, server down) must not fail the
        // subscription itself.
        try {
            client_->sendI2pTransient(i2pTransientBase64(), expiresUnix);
        } catch (const std::exception&) {
        }
    }
}

void Session::persistI2pBlob(const std::string& filename, const Bytes& blob) const
{
    // The master is the user's long-term routing identity and the transient is
    // a live delegation key: both are sealed at rest under the profile
    // passphrase, like the private-key PEMs.
    const Bytes onDisk = encrypted_ ? cms::sealWithPassword(blob, passphrase_) : blob;
    writeFileText(stateDir_ / filename, std::string(onDisk.begin(), onDisk.end()));
}

std::string Session::ensureI2pDestination()
{
    if (i2pMaster_.empty()) {
        const I2pMasterKey master = generateI2pMaster();
        i2pMaster_ = master.privateKeys;
        i2pAddress_ = master.base32;
        persistI2pBlob("i2p-master.dat", i2pMaster_);
    }
    return i2pAddress_;
}

bool Session::hasI2pDestination() const
{
    return !i2pMaster_.empty();
}

std::string Session::i2pAddress() const
{
    return i2pAddress_;
}

void Session::renewI2pTransient(const std::int64_t expiresUnix)
{
    if (i2pMaster_.empty()) {
        throw std::runtime_error("no user-owned I2P destination to delegate");
    }
    i2pTransient_ = issueI2pOfflineKeys(i2pMaster_, expiresUnix);
    persistI2pBlob("i2p-transient.dat", i2pTransient_);
}

Bytes Session::i2pTransient() const
{
    return i2pTransient_;
}

std::string Session::i2pTransientBase64() const
{
    return i2pPrivateKeysBase64(i2pTransient_);
}

std::string Session::signLogin(const std::string& challenge) const
{
    const auth::Headers headers = auth::signRequest(client_->identity(), nowSeconds(), kLoginMethod,
        kLoginPath, Bytes(challenge.begin(), challenge.end()));
    const nlohmann::json blob = {
        {"k", headers.at(auth::kHeaderKeys)},
        {"t", headers.at(auth::kHeaderTimestamp)},
        {"c", headers.at(auth::kHeaderSignatureClassical)},
        {"p", headers.at(auth::kHeaderSignaturePq)},
    };
    const std::string text = blob.dump();
    return toBase64(Bytes(text.begin(), text.end()));
}

std::string verifyLoginBlob(
    const std::string& blob, const std::int64_t now, const std::string& challenge)
{
    const Bytes raw = fromBase64(blob);
    const nlohmann::json parsed = nlohmann::json::parse(raw.begin(), raw.end());
    auth::Headers headers;
    headers[auth::kHeaderKeys] = parsed.at("k").get<std::string>();
    headers[auth::kHeaderTimestamp] = parsed.at("t").get<std::string>();
    headers[auth::kHeaderSignatureClassical] = parsed.at("c").get<std::string>();
    headers[auth::kHeaderSignaturePq] = parsed.at("p").get<std::string>();
    return auth::verifyRequest(
        headers, now, kLoginMethod, kLoginPath, Bytes(challenge.begin(), challenge.end()));
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

void Session::deliver(const std::string& toDest, const Key& servingSealingKey,
    const std::string& kind, const std::string& mailbox, const std::optional<Bytes>& token,
    const Bytes& payload, const std::function<void()>& onAcceptedByOwnServer, bool* tokenRejected)
{
    // The envelope is sealed to the recipient destination's serving sealing key,
    // so the routing metadata is readable only by the server operating that
    // destination. Our own server relays it to toDest over federation (or
    // delivers locally when toDest is our own destination). messageId stays
    // fixed across retries: the recipient server dedups, so resubmits are
    // idempotent and never consume a second token.
    const std::string messageId = toHex(randomBytes(16));
    const Bytes sealed = sealDeliveryEnvelope(kind, mailbox, messageId, token, servingSealingKey);

    // A foreign server may be momentarily unreachable while its I2P leaseset
    // publishes; that is transient, so retry. Other failures are terminal.
    constexpr int kMaxRounds = 12;
    std::string lastError = "delivery not attempted";
    bool acceptedByOwnServer = false;
    for (int round = 0; round < kMaxRounds; ++round) {
        try {
            const std::string attemptId = client_->submitSend(toDest, sealed, payload);
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

void Session::sendContactRequest(const std::string& peerFingerprint, const std::string& text)
{
    // Resolve the peer's prekey, serving server and server card on our own
    // server (facade locality — we never reach a foreign facade). The prekey
    // is signed by the peer (subscription certificate) and the server card by
    // the peer's server, so neither can be substituted by an intermediary.
    const ContactInfo info = client_->lookupContact(peerFingerprint);
    requestWithInfo(peerFingerprint, text, info);
}

FetchTransport Session::fetchTransport() const
{
    return [this](const std::string& toDest, const std::string& op, const Bytes& sealed) {
        try {
            // Direct over a fresh transient SAM session (preferred — our own
            // server is never involved, and a b33 dial authenticates the target).
            return federationFetchOverSam(
                "127.0.0.1", 7656, toDest, op, sealed, blobFetchPrivacy_);
        } catch (const std::exception&) {
            // No local SAM bridge (or the direct dial failed): relay the opaque
            // sealed bytes through our own server's I2P proxy.
            return client_->relayFetch(toDest, op, sealed);
        }
    };
}

std::string Session::addByInvite(const std::string& inviteUri, const std::string& text)
{
    // The invite is a descriptor (fingerprint + serving destination + serving
    // sealing key). Fetch the user-signed contact card for that fingerprint and
    // verify it against the fingerprint (api/FederatedResolve.md): a wrong server
    // can only withhold, never forge a card for someone else's fingerprint.
    const Descriptor descriptor = parseDescriptor(inviteUri);
    const ContactInfo info = client_->fetchCard(descriptor, fetchTransport());
    requestWithInfo(descriptor.fingerprint, text, info);
    return descriptor.fingerprint;
}

void Session::setResolverCoordinate(ResolverCoordinate coordinate)
{
    resolverCoordinate_ = std::move(coordinate);
}

std::string Session::addByUsername(const std::string& alias, const std::string& text)
{
    if (!resolverCoordinate_.configured()) {
        throw std::runtime_error("no alias resolver is configured in this build");
    }
    // Resolve the alias to a descriptor on the central resolver. The resolver's
    // record is self-verifying (signed, chained to the hardcoded root), so even a
    // malicious relay can only withhold, never forge the binding. The
    // alias->fingerprint mapping is the one residual trust of the name path: the
    // resolved fingerprint is returned so the UI can surface it for out-of-band
    // verification. Everything after the mapping — the card fetch and its
    // certificates — is verified end-to-end as usual.
    const std::string normalized = normalizeAlias(alias);
    const Descriptor descriptor
        = client_->resolveAlias(normalized, resolverCoordinate_, nowSeconds(), fetchTransport());
    const ContactInfo info = client_->fetchCard(descriptor, fetchTransport());
    requestWithInfo(descriptor.fingerprint, text, info);
    return descriptor.fingerprint;
}

void Session::requestWithInfo(const std::string& peerFingerprint, const std::string& text,
    const ContactInfo& info)
{
    if (info.subscriptionCert.user != peerFingerprint) {
        throw std::runtime_error("contact lookup returned a different user");
    }
    const Key peerPrekey = info.subscriptionCert.sealingKey();
    const Key peerServingKey = info.subscriptionCert.servingSealingKey();
    const std::string peerDest = info.subscriptionCert.dest;
    validateB32I2pHost(peerDest);

    // Mint a batch the peer will use to write back to us and hand it over, with
    // our prekey and our routing (dest + serving sealing key), in the bootstrap.
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
                {"dest", myDest_},
                {"servingKey", myServingKeyB64_},
                {"replyTokens", replyTokens},
            }},
    };
    // E2E-encrypted to the peer's prekey: the first message is confidential.
    // Delivered tokenless under the "contact" admission class.
    const std::string plain = payload.dump();
    const Bytes encrypted = cms::seal(Bytes(plain.begin(), plain.end()), peerPrekey);
    deliver(peerDest, peerServingKey, "contact", peerFingerprint, std::nullopt, encrypted);

    // We now know how to reach the peer; reciprocal tokens arrive with the
    // peer's reply.
    Contact& contact = contacts_[peerFingerprint];
    contact.dest = peerDest;
    contact.sealingPublicB64 = toBase64(peerPrekey.publicDer());
    contact.servingSealingB64 = toBase64(peerServingKey.publicDer());
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
    // Encrypt the file under a fresh key straight to a temp ciphertext file and
    // upload it streaming, so a large file is never held whole in memory. The
    // message carries only a small sealed pointer, so the recipient's mailbox
    // quota is never a factor for large files. (Blob storage hosts rotating
    // encrypted-LeaseSet destinations, served only over I2P.)
    const std::uint64_t plainSize = fs::file_size(path);
    const fs::path ciphertextPath
        = stateDir_ / ("blob-upload-" + toHex(randomBytes(8)) + ".tmp");

    BlobPointer pointer;
    BlobUploadResult uploaded;
    try {
        const PackedBlobFile packed = packLargeBlobToFile(path, ciphertextPath);
        uploaded = client_->uploadBlobFromFile(packed, BlobRetention{});
        pointer.fileKey = packed.fileKey;
        pointer.sha256 = packed.sha256;
        pointer.size = packed.size;
    } catch (...) {
        std::error_code ec;
        fs::remove(ciphertextPath, ec);
        throw;
    }
    std::error_code ec;
    fs::remove(ciphertextPath, ec);

    pointer.blobUrl = uploaded.blobUrl;
    pointer.blobId = uploaded.blobId;
    const std::string pointerJson = blobPointerToJson(pointer).dump();

    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "file"},
        {"id", messageId.empty() ? toHex(randomBytes(8)) : messageId},
        {"from", fingerprint()},
        {"sentAt", nowSeconds()},
        {"file",
            {
                {"ptr", toBase64(Bytes(pointerJson.begin(), pointerJson.end()))},
                {"size", plainSize},
                {"mime", guessMime(path)},
                {"name", path.filename().string()},
            }},
    };
    // Remember the blob so the sender can unsend it later.
    recordSentBlob(inner.at("id").get<std::string>(), uploaded.blobUrl, uploaded.deleteToken);
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
    (void)keyB64;  // the decryption key now travels inside the pointer
    // ref is the base64 sealed blob pointer; fetch the ciphertext over I2P,
    // verify its digest and decrypt it straight to dest (never whole in RAM).
    const Bytes pointerBytes = fromBase64(ref);
    const BlobPointer pointer
        = blobPointerFromJson(nlohmann::json::parse(pointerBytes.begin(), pointerBytes.end()));
    fetchLargeBlobToFile(pointer, dest);
}

void Session::sendContent(const std::string& peerFingerprint, nlohmann::json inner,
    const std::function<void()>& onAcceptedByOwnServer)
{
    const auto found = contacts_.find(peerFingerprint);
    if (found == contacts_.end()) {
        throw std::runtime_error("unknown contact: " + peerFingerprint);
    }
    Contact& contact = found->second;
    if (contact.sealingPublicB64.empty() || contact.servingSealingB64.empty()) {
        throw std::runtime_error("contact not established yet: " + peerFingerprint);
    }
    if (contact.sendTokens.empty()) {
        throw std::runtime_error("no delivery tokens left for contact: " + peerFingerprint);
    }

    // Externalize content larger than the threshold: encrypt it under a fresh
    // key, upload the ciphertext to blob storage, and replace the body with a
    // small sealed pointer. The messaging server only ever sees the pointer, so
    // a quota-constrained recipient can receive arbitrarily large content. The
    // routing fields (id/from) and the bootstrap/lowStash control blocks stay in
    // the small outer envelope.
    {
        const std::string contentText = inner.dump();
        if (contentText.size() > kLargeBlobThresholdBytes) {
            const PackedBlob packed
                = packLargeBlob(Bytes(contentText.begin(), contentText.end()));
            const BlobUploadResult uploaded = client_->uploadBlob(packed, BlobRetention{});
            // Remember the blob so the sender can unsend it later.
            recordSentBlob(
                inner.value("id", std::string()), uploaded.blobUrl, uploaded.deleteToken);
            BlobPointer pointer;
            pointer.blobUrl = uploaded.blobUrl;
            pointer.blobId = uploaded.blobId;
            pointer.fileKey = packed.fileKey;
            pointer.sha256 = packed.sha256;
            pointer.size = packed.size;
            inner = {
                {"v", kMessageFormatVersion},
                {"type", "blob.pointer"},
                {"id", inner.value("id", std::string())},
                {"from", inner.value("from", fingerprint())},
                {"pointer", blobPointerToJson(pointer)},
            };
        }
    }

    // First reply to a peer that wrote to us first: hand them a bootstrap (our
    // routing + a token batch) so the reverse direction is usable too.
    if (!contact.issuedToThem) {
        inner["bootstrap"] = {
            {"sealing", sealingPublicB64()},
            {"dest", myDest_},
            {"servingKey", myServingKeyB64_},
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
    const Key peerServingKey = Key::fromPublicDer(fromBase64(contact.servingSealingB64));

    const std::string token = contact.sendTokens.back();
    deliver(contact.dest, peerServingKey, "content", peerFingerprint, fromBase64(token), payload,
        onAcceptedByOwnServer);

    // Spend the token only after a successful delivery.
    contact.sendTokens.pop_back();
    persistContacts();
}

void Session::setBlobFetchPrivacy(const I2pPrivacy privacy)
{
    blobFetchPrivacy_ = privacy;
}

Bytes Session::fetchLargeBlob(const BlobPointer& pointer)
{
    try {
        // Direct over a fresh transient SAM session (preferred — our server is
        // never involved).
        return fetchBlob("127.0.0.1", 7656, pointer, blobFetchPrivacy_);
    } catch (const std::exception&) {
        // No local SAM bridge (or the direct fetch failed): fall back to our own
        // server proxying the fetch over I2P.
        return client_->fetchBlobViaProxy(pointer);
    }
}

void Session::fetchLargeBlobToFile(const BlobPointer& pointer, const fs::path& dest)
{
    try {
        // Direct over a fresh transient SAM session, streamed to disk (preferred
        // — our server is never involved and the file never sits whole in RAM).
        fetchBlobToFile("127.0.0.1", 7656, pointer, dest, blobFetchPrivacy_);
    } catch (const std::exception&) {
        // No local SAM bridge (or the direct fetch failed): the own-server proxy
        // relays the whole ciphertext through the facade (buffered fallback).
        const Bytes plain = client_->fetchBlobViaProxy(pointer);
        writeFileBytes(dest, plain);
    }
}

void Session::deleteLargeBlob(const std::string& blobUrl, const std::string& deleteToken)
{
    try {
        deleteBlob("127.0.0.1", 7656, blobUrl, deleteToken, blobFetchPrivacy_);  // direct
    } catch (const std::exception&) {
        client_->deleteBlobViaProxy(blobUrl, deleteToken);  // own-server proxy fallback
    }
}

void Session::unsend(const std::string& messageId)
{
    const auto found = sentBlobs_.find(messageId);
    if (found == sentBlobs_.end()) {
        throw std::runtime_error("no externalized blob recorded for message: " + messageId);
    }
    try {
        deleteLargeBlob(found->second.blobUrl, found->second.deleteToken);
    } catch (const std::exception&) {
        // Best effort: the blob also reclaims via its TTL.
    }
    sentBlobs_.erase(found);
    persistSentBlobs();
}

void Session::recordSentBlob(
    const std::string& messageId, const std::string& blobUrl, const std::string& deleteToken)
{
    sentBlobs_[messageId] = SentBlob{blobUrl, deleteToken};
    persistSentBlobs();
}

void Session::loadSentBlobs()
{
    const fs::path path = stateDir_ / "sent-blobs.json";
    if (!fs::exists(path)) {
        return;
    }
    const std::string raw = readFileText(path);
    const nlohmann::json stored = encrypted_
        ? nlohmann::json::parse(cms::unsealWithPassword(Bytes(raw.begin(), raw.end()), passphrase_))
        : nlohmann::json::parse(raw);
    for (const auto& [id, entry] : stored.items()) {
        sentBlobs_[id] = SentBlob{
            entry.at("url").get<std::string>(), entry.at("token").get<std::string>()};
    }
}

void Session::persistSentBlobs() const
{
    nlohmann::json stored = nlohmann::json::object();
    for (const auto& [id, blob] : sentBlobs_) {
        stored[id] = {{"url", blob.blobUrl}, {"token", blob.deleteToken}};
    }
    if (encrypted_) {
        // Delete-tokens are capabilities over our own blobs: seal at rest under
        // the profile passphrase (CMS PWRI), like contacts.
        const std::string text = stored.dump();
        const Bytes sealed = cms::sealWithPassword(Bytes(text.begin(), text.end()), passphrase_);
        writeFileText(stateDir_ / "sent-blobs.json", std::string(sealed.begin(), sealed.end()));
        return;
    }
    writeFileText(stateDir_ / "sent-blobs.json", stored.dump(2));
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
        nlohmann::json body = nlohmann::json::parse(plain.begin(), plain.end());

        IncomingMessage message;
        message.deliveryClass = entry.deliveryClass;
        message.fromFingerprint = body.at("from").get<std::string>();
        message.messageId = body.value("id", std::string());
        std::string type = body.value("type", std::string("text"));

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

        // A blob pointer: the real content was externalized to blob storage.
        // Fetch it over I2P (a fresh transient destination), verify and decrypt
        // it, then dispatch on the recovered content's real type. Best effort —
        // a failed fetch surfaces the pointer (the blob persists until its TTL,
        // so a later sync can retry).
        if (type == "blob.pointer") {
            try {
                const BlobPointer pointer = blobPointerFromJson(body.at("pointer"));
                const Bytes content = fetchLargeBlob(pointer);
                body = nlohmann::json::parse(content.begin(), content.end());
                type = body.value("type", std::string("text"));
            } catch (const std::exception&) {
                message.contentType = "blob.pointer";
                message.text = "[large message — fetch failed; retry later]";
            }
        }

        // Content dispatch. An unknown type is still acked and surfaced (not
        // dropped) so a newer client could render it; see docs Messages.md.
        if (type == "text" || type == "contact.request") {
            message.contentType = type;
            message.text = body.value("text", std::string());
        } else if (type == "file" || type == "photo" || type == "audio" || type == "voice") {
            message.contentType = type;
            const nlohmann::json& file = body.at("file");
            // The base64 sealed blob pointer; the decryption key rides inside it.
            message.attachmentRef = file.at("ptr").get<std::string>();
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
    if (contact.sealingPublicB64.empty() || contact.servingSealingB64.empty()
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

std::string Session::ownServingKeyB64() const
{
    if (myServingKeyB64_.empty()) {
        throw std::runtime_error("no serving key: subscribe first");
    }
    return myServingKeyB64_;
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
        {"dest", myDest_},
        {"servingKey", ownServingKeyB64()},
        {"admin", group.iAmAdmin},
    });
    if (group.iAmAdmin) {
        admins.push_back(fingerprint());
    }
    for (const auto& [fp, member] : group.members) {
        members.push_back({
            {"fp", fp},
            {"sealing", member.sealingPublicB64},
            {"dest", member.dest},
            {"servingKey", member.servingSealingB64},
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
        member.dest = jm.at("dest").get<std::string>();
        validateB32I2pHost(member.dest);
        member.servingSealingB64 = jm.at("servingKey").get<std::string>();
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
    const Key memberServingKey = Key::fromPublicDer(fromBase64(member.servingSealingB64));
    // Tokenless contact-class delivery — the standing path into any mailbox.
    deliver(member.dest, memberServingKey, "contact", memberFp, std::nullopt, payload);
}

void Session::sendToMemberContent(
    const std::string& groupId, const std::string& memberFp, const nlohmann::json& inner)
{
    Group& group = groups_.at(groupId);
    GroupMember& member = group.members.at(memberFp);
    const std::string text = inner.dump();
    const Key memberSealing = Key::fromPublicDer(fromBase64(member.sealingPublicB64));
    const Bytes payload = cms::seal(Bytes(text.begin(), text.end()), memberSealing);
    const Key memberServingKey = Key::fromPublicDer(fromBase64(member.servingSealingB64));

    // Optimistic retry: the pool is shared, so a token may have been spent by a
    // concurrent sender. Drop a rejected token and try the next until one is
    // accepted or the pool is empty (then the member must refill us).
    while (!member.sendTokens.empty()) {
        const std::string token = member.sendTokens.back();
        member.sendTokens.pop_back();
        bool rejected = false;
        deliver(member.dest, memberServingKey, "content", memberFp, fromBase64(token), payload,
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
    if (myServingKeyB64_.empty()) {
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
            || found->second.servingSealingB64.empty()) {
            throw std::runtime_error("group member is not an established contact: " + fp);
        }
        GroupMember member;
        member.sealingPublicB64 = found->second.sealingPublicB64;
        member.dest = found->second.dest;
        member.servingSealingB64 = found->second.servingSealingB64;
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
            || contact->second.servingSealingB64.empty()) {
            throw std::runtime_error("new group member is not an established contact: " + fp);
        }
        GroupMember member;
        member.sealingPublicB64 = contact->second.sealingPublicB64;
        member.dest = contact->second.dest;
        member.servingSealingB64 = contact->second.servingSealingB64;
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
                {"dest", member.dest},
                {"servingKey", member.servingSealingB64},
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
    if (myDest_.empty() || myServingKeyB64_.empty()) {
        throw std::runtime_error("subscribe first: no serving destination to publish");
    }
    // The invite is a small descriptor: fingerprint + serving destination +
    // serving sealing key. The contact fetches and verifies the full card.
    Descriptor descriptor;
    descriptor.fingerprint = fingerprint();
    descriptor.srv = myDest_;
    descriptor.srvKeyDer = fromBase64(myServingKeyB64_);
    return encodeDescriptor(descriptor);
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
