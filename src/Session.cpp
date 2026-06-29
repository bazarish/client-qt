// Bazarish project (c) 2026
#include "Session.hpp"

#include "BlobTransport.hpp"
#include "FederationFetch.hpp"
#include "I2pKeys.hpp"
#include "I2pRouter.hpp"
#include "LargeBlob.hpp"

#include <bazarish/Auth.hpp>
#include <bazarish/Certificates.hpp>
#include <bazarish/Cms.hpp>
#include <bazarish/Descriptor.hpp>
#include <bazarish/Errors.hpp>
#include <bazarish/Log.hpp>
#include <bazarish/I2pAddress.hpp>
#include <bazarish/Tokens.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
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
// (see Contacts.md refill) - so a conversation never runs dry.
constexpr int kTokenBatchSize = 64;
constexpr std::size_t kRefillThreshold = 16;

// Inner end-to-end payload format version (see docs Messages.md).
constexpr int kMessageFormatVersion = 1;

// How long an offline transient delegated to the serving server stays valid.
// Kept short so the operator only ever holds a time-boxed capability; the client
// re-issues a fresh one well before it lapses (see refreshI2pTransientIfDue).
constexpr std::int64_t kI2pTransientValiditySeconds = 7 * 24 * 3600;

std::int64_t nowSeconds()
{
    return static_cast<std::int64_t>(std::time(nullptr));
}

// Unix milliseconds from the client clock. Stamped into every outgoing message's
// sentAt so the recipient can reorder a burst that arrived out of order and show
// each message's own send time (see docs-main Messages.md "Ordering and timestamps").
std::int64_t nowMillis()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
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

// The protocol cap on an avatar's compressed size. The UI compresses a chosen
// image to a square within this limit before it ever reaches the core.
constexpr std::size_t kAvatarMaxBytes = 500 * 1024;

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
            const std::string t = token.get<std::string>();
            // Dedup: a pending item may be re-fetched before it is acked (acks are
            // deferred until the client durably stores the item), so applying the
            // same bootstrap twice must not double the token stash.
            if (std::find(contact.sendTokens.begin(), contact.sendTokens.end(), t)
                == contact.sendTokens.end()) {
                contact.sendTokens.push_back(t);
            }
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

Session::Session(fs::path profileDir, std::unique_ptr<Client> client, Key sealingKey,
    std::map<std::string, Contact> contacts)
    : profileDir_(std::move(profileDir))
    , client_(std::move(client))
    , sealingKey_(std::move(sealingKey))
    , contacts_(std::move(contacts))
{
    // The compiled-in resolver coordinate is empty until a developer-run resolver
    // is deployed and baked in. It can be overridden from the environment so a
    // freshly-built test or local resolver is exercised without a rebuild; all
    // three parts must be present or the alias path stays unconfigured.
    if (const char* const root = std::getenv("BAZARISH_RESOLVER_ROOT");
        root != nullptr && root[0] != '\0') {
        const char* const dest = std::getenv("BAZARISH_RESOLVER_DEST");
        const char* const key = std::getenv("BAZARISH_RESOLVER_KEY");
        if (dest != nullptr && dest[0] != '\0' && key != nullptr && key[0] != '\0') {
            resolverCoordinate_ = ResolverCoordinate{root, dest, fromBase64(key)};
        }
    }
}

bazarish::i2p::Router& Session::i2pRouter() const
{
    // The embedded router is process-global (one per process), so share it across
    // all profiles. Its state nests under the profiles root (the parent of this
    // profile's directory) so it is reused regardless of which profile starts it
    // first. Started lazily on first transport use; client role (notransit).
    return sharedI2pRouter(profileDir_.parent_path() / "i2p");
}

Session Session::create(
    const fs::path& profileDir, const std::string& passphrase, const std::string& name)
{
    fs::create_directories(profileDir);

    Identity identity = Identity::generate();
    writeFileText(profileDir / "identity.pem", identity.privatePem(passphrase));

    Key sealing = Key::generateSealing();
    writeFileText(profileDir / "sealing.pem", sealing.privatePem(passphrase));

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
    writeFileText(profileDir / "meta.json", meta.dump(2));

    auto client = std::make_unique<Client>(
        std::move(identity), clientId, endpoint, profileDir.parent_path() / "i2p");
    Session session(profileDir, std::move(client), std::move(sealing), {});
    session.encrypted_ = encrypted;
    session.passphrase_ = passphrase;
    session.name_ = name;
    return session;
}

Session Session::create(const fs::path& profileDir, const ServerEndpoint& endpoint,
    const std::string& passphrase)
{
    Session session = create(profileDir, passphrase);
    session.connectServer(endpoint);
    return session;
}

void Session::connectServer(const ServerEndpoint& endpoint)
{
    // Rebind the transport to the new server, reusing the identity and client
    // id. The in-memory identity PEM is unencrypted, so this is independent of
    // the at-rest passphrase.
    client_ = std::make_unique<Client>(
        Identity::fromPrivatePem(client_->identity().privatePem()), client_->clientId(), endpoint,
        profileDir_.parent_path() / "i2p");
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

Session Session::open(const fs::path& profileDir, const std::string& passphrase)
{
    const nlohmann::json meta = nlohmann::json::parse(readFileText(profileDir / "meta.json"));
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
        = Identity::fromPrivatePem(readFileText(profileDir / "identity.pem"), passphrase);
    Key sealing = Key::fromPrivatePem(readFileText(profileDir / "sealing.pem"), passphrase);
    const std::string clientId = meta.at("clientId").get<std::string>();

    std::map<std::string, Contact> contacts;
    const fs::path contactsPath = profileDir / "contacts.json";
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
            // Display name and avatar metadata are newer fields: tolerate their
            // absence in profiles written before they existed.
            contact.displayName = entry.value("displayName", std::string());
            contact.avatarMime = entry.value("avatarMime", std::string());
            contact.avatarSentToPeer = entry.value("avatarSentToPeer", false);
            contacts.emplace(fingerprint, std::move(contact));
        }
    }

    auto client = std::make_unique<Client>(
        std::move(identity), clientId, endpoint, profileDir.parent_path() / "i2p");
    Session session(profileDir, std::move(client), std::move(sealing), std::move(contacts));
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
    const fs::path groupsPath = profileDir / "groups.json";
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
            // The group photo mime is a newer field: tolerate its absence.
            group.avatarMime = entry.value("avatarMime", std::string());
            for (const auto& [fp, jm] : entry.at("members").items()) {
                GroupMember member;
                member.sealingPublicB64 = jm.at("sealing").get<std::string>();
                member.dest = jm.at("dest").get<std::string>();
                member.servingSealingB64 = jm.at("servingKey").get<std::string>();
                member.sendTokens = jm.at("sendTokens").get<std::vector<std::string>>();
                member.admin = jm.value("admin", false);
                member.displayName = jm.value("displayName", std::string());
                member.provisionalName = jm.value("provisionalName", std::string());
                group.members.emplace(fp, std::move(member));
            }
            session.groups_.emplace(groupId, std::move(group));
        }
    }
    session.loadLeftGroups();

    // Load the user-owned I2P destination, if this profile has one (per-user
    // path). Both blobs are sealed at rest when the profile is encrypted.
    const auto loadI2pBlob = [&](const char* filename) -> Bytes {
        const fs::path path = profileDir / filename;
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

    // Avatars (own + per-contact): the bytes live in sealed blob files, only the
    // mime flags ride in meta/contacts. Load the bytes for whatever has a mime.
    session.avatarMime_ = meta.value("avatarMime", std::string{});
    if (!session.avatarMime_.empty()) {
        session.avatar_ = loadI2pBlob("avatar.self");
        if (session.avatar_.empty()) {
            session.avatarMime_.clear();  // file gone (e.g. a backup restore): no avatar
        }
    }
    for (auto& [contactFp, contact] : session.contacts_) {
        if (!contact.avatarMime.empty()) {
            contact.avatar = loadI2pBlob(("avatar-" + contactFp).c_str());
            if (contact.avatar.empty()) {
                contact.avatarMime.clear();
            }
        }
    }
    // Group photos: same sealed-blob layout, keyed by group id.
    for (auto& [groupId, group] : session.groups_) {
        if (!group.avatarMime.empty()) {
            group.avatar = loadI2pBlob(("group-avatar-" + groupId).c_str());
            if (group.avatar.empty()) {
                group.avatarMime.clear();
            }
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

void Session::setDisplayName(const std::string& name)
{
    if (name == name_) {
        return;
    }
    name_ = name;
    // Persist to meta.json (stored in the clear, like the creation label). Future
    // inviteUri() descriptors carry the new name; existing contacts are not told.
    persistMeta();
}

const Bytes& Session::avatar() const
{
    return avatar_;
}

const std::string& Session::avatarMime() const
{
    return avatarMime_;
}

std::string Session::contactDisplayName(const std::string& peerFingerprint) const
{
    const auto found = contacts_.find(peerFingerprint);
    return found == contacts_.end() ? std::string() : found->second.displayName;
}

Bytes Session::contactAvatar(const std::string& peerFingerprint) const
{
    const auto found = contacts_.find(peerFingerprint);
    return found == contacts_.end() ? Bytes() : found->second.avatar;
}

bool Session::contactIsPending(const std::string& peerFingerprint) const
{
    const auto found = contacts_.find(peerFingerprint);
    return found != contacts_.end() && !found->second.issuedToThem;
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
        {"avatarMime", avatarMime_},
    };
    writeFileText(profileDir_ / "meta.json", meta.dump(2));
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
            {"displayName", contact.displayName},
            {"avatarMime", contact.avatarMime},
            {"avatarSentToPeer", contact.avatarSentToPeer},
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
        writeFileText(profileDir_ / "contacts.json", std::string(sealed.begin(), sealed.end()));
        return;
    }
    writeFileText(profileDir_ / "contacts.json", stored.dump(2));
}

PortalInfo Session::serverPortalInfo()
{
    return client_->fetchPortalInfo();
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
    // The transient expiry tracks the subscription window (kept short - the
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

void Session::persistSealedBlob(const std::string& filename, const Bytes& blob) const
{
    const Bytes onDisk = encrypted_ ? cms::sealWithPassword(blob, passphrase_) : blob;
    writeFileText(profileDir_ / filename, std::string(onDisk.begin(), onDisk.end()));
}

void Session::persistI2pBlob(const std::string& filename, const Bytes& blob) const
{
    // The master is the user's long-term routing identity and the transient is
    // a live delegation key: both are sealed at rest under the profile
    // passphrase, like the private-key PEMs.
    persistSealedBlob(filename, blob);
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

void Session::deleteI2pDestination()
{
    i2pMaster_.clear();
    i2pTransient_.clear();
    i2pAddress_.clear();
    // fs::remove returns false (no throw) when the file is already absent.
    fs::remove(profileDir_ / "i2p-master.dat");
    fs::remove(profileDir_ / "i2p-transient.dat");
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

std::string Session::loadI2pDestination(const Bytes& privateKeysDat)
{
    if (!i2pMaster_.empty()) {
        throw std::runtime_error("a user-owned I2P destination is already configured");
    }
    const I2pMasterKey master = loadI2pMaster(privateKeysDat);
    i2pMaster_ = master.privateKeys;
    i2pAddress_ = master.base32;
    persistI2pBlob("i2p-master.dat", i2pMaster_);
    return i2pAddress_;
}

bool Session::enableI2pDest(const std::int64_t now)
{
    if (i2pMaster_.empty()) {
        throw std::runtime_error("no user-owned I2P destination — generate or load one first");
    }
    if (!client_->setI2pDestEnabled(true)) {
        return false;  // server refused (e.g. insufficient balance to charge a term)
    }
    const std::int64_t expiresUnix = now + kI2pTransientValiditySeconds;
    renewI2pTransient(expiresUnix);
    if (!client_->sendI2pTransient(i2pTransientBase64(), expiresUnix)) {
        return false;
    }
    // Back the master up to the account's other devices so they keep the same
    // address. Best effort - failure must not fail enabling.
    try {
        syncI2pMasterToSelf();
    } catch (const std::exception&) {
    }
    return true;
}

void Session::disableI2pDest()
{
    client_->setI2pDestEnabled(false);
}

void Session::syncI2pMasterToSelf()
{
    if (i2pMaster_.empty() || myDest_.empty() || myServingKeyB64_.empty()) {
        return;  // nothing to sync, or our own routing is not known yet
    }
    const nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "device.i2p-master"},
        {"id", toHex(randomBytes(16))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"i2pMaster", toBase64(i2pMaster_)},
    };
    const std::string innerText = inner.dump();
    // Sealed to our own sealing key: only this account's devices, which share
    // the key, can read it.
    const Key ownSealing = Key::fromPublicDer(sealingKey_.publicDer());
    const Bytes payload = cms::seal(Bytes(innerText.begin(), innerText.end()), ownSealing);
    const Key ownServingKey = Key::fromPublicDer(fromBase64(myServingKeyB64_));
    // Tokenless contact-class delivery to our own destination: it lands in our
    // own mailbox, which every device of this account polls.
    deliver(myDest_, ownServingKey, "contact", fingerprint(), std::nullopt, payload);
}

void Session::storeOwnAvatar(const Bytes& data, const std::string& mime)
{
    avatar_ = data;
    avatarMime_ = mime;
    persistSealedBlob("avatar.self", avatar_);
    persistMeta();  // record the mime so open() knows to load the blob
}

void Session::storeContactAvatar(
    const std::string& peerFingerprint, const Bytes& data, const std::string& mime)
{
    if (data.size() > kAvatarMaxBytes) {
        return;  // over the protocol cap: drop it rather than store an oversized blob
    }
    const auto found = contacts_.find(peerFingerprint);
    if (found == contacts_.end()) {
        return;  // an avatar from someone who is not a contact: ignore
    }
    found->second.avatar = data;
    found->second.avatarMime = mime;
    persistSealedBlob("avatar-" + peerFingerprint, data);
    persistContacts();  // record the mime flag
}

void Session::syncAvatarToSelf()
{
    if (avatar_.empty() || myDest_.empty() || myServingKeyB64_.empty()) {
        return;  // nothing to sync, or our own routing is not known yet
    }
    const nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "device.avatar"},
        {"id", toHex(randomBytes(16))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"avatar", {{"mime", avatarMime_}, {"data", toBase64(avatar_)}}},
    };
    const std::string innerText = inner.dump();
    // Sealed to our own sealing key: only this account's devices can read it.
    const Key ownSealing = Key::fromPublicDer(sealingKey_.publicDer());
    const Bytes payload = cms::seal(Bytes(innerText.begin(), innerText.end()), ownSealing);
    const Key ownServingKey = Key::fromPublicDer(fromBase64(myServingKeyB64_));
    deliver(myDest_, ownServingKey, "contact", fingerprint(), std::nullopt, payload);
}

void Session::syncContactNameToSelf(const std::string& peerFingerprint, const std::string& name)
{
    if (myDest_.empty() || myServingKeyB64_.empty()) {
        return;  // our own routing is not known yet
    }
    const nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "device.contact-name"},
        {"id", toHex(randomBytes(16))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"peer", peerFingerprint},
        {"name", name},
    };
    const std::string innerText = inner.dump();
    const Key ownSealing = Key::fromPublicDer(sealingKey_.publicDer());
    const Bytes payload = cms::seal(Bytes(innerText.begin(), innerText.end()), ownSealing);
    const Key ownServingKey = Key::fromPublicDer(fromBase64(myServingKeyB64_));
    deliver(myDest_, ownServingKey, "contact", fingerprint(), std::nullopt, payload);
}

void Session::syncChatPinToSelf(const std::string& peerFingerprint, bool pinned)
{
    if (myDest_.empty() || myServingKeyB64_.empty()) {
        return;  // our own routing is not known yet
    }
    // Mirror a pin/unpin to our own other devices (a purely local list ordering, so
    // it rides the same self-addressed device-sync channel as a contact rename).
    const nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "device.chat-pin"},
        {"id", toHex(randomBytes(16))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"peer", peerFingerprint},
        {"pinned", pinned},
    };
    const std::string innerText = inner.dump();
    const Key ownSealing = Key::fromPublicDer(sealingKey_.publicDer());
    const Bytes payload = cms::seal(Bytes(innerText.begin(), innerText.end()), ownSealing);
    const Key ownServingKey = Key::fromPublicDer(fromBase64(myServingKeyB64_));
    deliver(myDest_, ownServingKey, "contact", fingerprint(), std::nullopt, payload);
}

void Session::maybeSendAvatarToContact(const std::string& peerFingerprint)
{
    if (avatar_.empty()) {
        return;  // no avatar to share
    }
    const auto found = contacts_.find(peerFingerprint);
    if (found == contacts_.end()) {
        return;
    }
    Contact& contact = found->second;
    // Share only once the dialog is mutually established (issuedToThem: we are the
    // requester, or we accepted their request - never an automatic reply to an
    // un-accepted incoming request) and we can actually reach the peer.
    if (!contact.issuedToThem || contact.avatarSentToPeer || contact.sendTokens.empty()
        || contact.sealingPublicB64.empty() || contact.servingSealingB64.empty()) {
        return;
    }
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "avatar"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"avatar", {{"mime", avatarMime_}, {"data", toBase64(avatar_)}}},
    };
    try {
        sendContent(peerFingerprint, std::move(inner));
        contact.avatarSentToPeer = true;
        persistContacts();
    } catch (const std::exception& error) {
        // Best effort by design: a peer we cannot reach now gets the avatar on a
        // later establishment or avatar update. The spec surfaces no send error
        // for avatar distribution.
        bazarish::log::warn("avatar push to {} failed: {}", peerFingerprint, error.what());
    }
}

void Session::setAvatar(const Bytes& data, const std::string& mime)
{
    if (data.size() > kAvatarMaxBytes) {
        throw std::runtime_error("avatar exceeds the 500 KB protocol limit");
    }
    storeOwnAvatar(data, mime);
    // A changed avatar must reach every established contact: reset the per-contact
    // "already sent" flag, then push to all reachable contacts (best effort).
    for (auto& [contactFp, contact] : contacts_) {
        (void)contactFp;
        contact.avatarSentToPeer = false;
    }
    persistContacts();
    for (const auto& [contactFp, contact] : contacts_) {
        (void)contact;
        maybeSendAvatarToContact(contactFp);
    }
    // And to the account's other devices.
    try {
        syncAvatarToSelf();
    } catch (const std::exception& error) {
        bazarish::log::warn("avatar self-sync failed: {}", error.what());
    }
}

void Session::renameContact(const std::string& peerFingerprint, const std::string& name)
{
    const auto found = contacts_.find(peerFingerprint);
    if (found == contacts_.end()) {
        return;
    }
    found->second.displayName = name;
    persistContacts();
    // The rename is local; mirror it to the account's other devices only.
    try {
        syncContactNameToSelf(peerFingerprint, name);
    } catch (const std::exception& error) {
        bazarish::log::warn("contact-name self-sync failed: {}", error.what());
    }
}

void Session::removeContact(const std::string& peerFingerprint)
{
    if (contacts_.erase(peerFingerprint) == 0) {
        return;  // unknown contact
    }
    // Drop the sealed avatar blob too, so nothing of the contact lingers on disk.
    std::error_code ignore;
    fs::remove(profileDir_ / ("avatar-" + peerFingerprint), ignore);
    persistContacts();
}

I2pDestStatus Session::i2pDestStatus()
{
    return client_->i2pStatus();
}

StorageUsage Session::storageUsage()
{
    return client_->storageUsage();
}

bool Session::refreshI2pTransientIfDue(const std::int64_t now, const std::int64_t leadSeconds)
{
    if (!hasI2pDestination()) {
        return false;
    }
    const I2pDestStatus status = client_->i2pStatus();
    if (!status.enabled || !status.active) {
        return false;  // off or unpaid -> the personal destination is offline; do not issue
    }
    // Poll-before-issue: the status read above is the check. If the server still
    // holds a transient comfortably in date, another of the user's devices has
    // already renewed it, so this device stands down.
    if (status.transientExpires != 0 && status.transientExpires - now > leadSeconds) {
        return false;
    }
    const std::int64_t expiresUnix = now + kI2pTransientValiditySeconds;
    renewI2pTransient(expiresUnix);
    return client_->sendI2pTransient(i2pTransientBase64(), expiresUnix);
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

bool Session::deliver(const std::string& toDest, const Key& servingSealingKey,
    const std::string& kind, const std::string& mailbox, const std::optional<Bytes>& token,
    const Bytes& payload, const std::function<void()>& onAcceptedByOwnServer, bool* tokenRejected,
    std::string* outAttemptId, bool waitForOutcome)
{
    // The envelope is sealed to the recipient destination's serving sealing key,
    // so the routing metadata is readable only by the server operating that
    // destination. messageId stays fixed: the recipient server dedups, so a
    // resubmit is idempotent and never consumes a second token.
    const std::string messageId = toHex(randomBytes(16));
    const Bytes sealed = sealDeliveryEnvelope(kind, mailbox, messageId, token, servingSealingKey);

    // Hand the envelope to our own server. With store-and-forward it accepts the
    // envelope at once and federates in the background (retrying a recipient
    // whose I2P leaseset is still publishing), so this returns quickly. We poll
    // the attempt for the outcome, but only for a bounded window: a delivery
    // still in flight when the window passes is left "at our server" (grey) - the
    // server keeps trying and the recipient's read receipt confirms it (green) -
    // rather than blocking the caller for the whole federation.
    // messageId is passed in the clear too, so our server can match the recipient
    // server's signed delivered-ack (the amber "delivered to recipient's server").
    const std::string attemptId = client_->submitSend(toDest, sealed, payload, messageId);
    if (outAttemptId != nullptr) {
        *outAttemptId = attemptId;  // so the caller can reconcile a late outcome
    }
    if (onAcceptedByOwnServer) {
        onAcceptedByOwnServer();  // grey: our own server accepted the envelope
    }
    if (!waitForOutcome) {
        // The caller does not want to block on the outcome (e.g. a file send,
        // where the upload already took the time budget): leave it grey and let
        // the next sync reconcile the attempt to yellow/green/red.
        return false;
    }
    constexpr int kPollAttempts = 150;  // ~15 s at 100 ms
    for (int poll = 0; poll < kPollAttempts; ++poll) {
        try {
            const SendStatus status = client_->pollSend(attemptId);
            if (status.status == "delivered") {
                return true;  // recipient server stored it: yellow
            }
            if (status.status == "failed") {
                // A spent token (e.g. a concurrent group sender took it): let the
                // caller retry with another token instead of failing.
                if (tokenRejected != nullptr
                    && status.errorCode == ErrorCode::eDeliveryRejected) {
                    *tokenRejected = true;
                    return false;
                }
                throw std::runtime_error("delivery failed: "
                    + (status.errorMessage.empty() ? std::string("unknown")
                                                    : status.errorMessage));
            }
            if (status.status == "unconfirmed") {
                // Our server gave up trying to confirm delivery, but the envelope
                // may have been stored (only its ack was lost): leave it grey, not
                // failed. A read receipt later confirms it (green).
                return false;
            }
            // "pending": still being delivered server-side; keep waiting.
        } catch (const ApiError&) {
            // The attempt is momentarily unpollable (e.g. our own server restarted
            // and forgot it). The envelope was accepted; leave it grey rather than
            // failing the message.
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;  // accepted, still being delivered in the background
}

Session::AttemptOutcome Session::pollAttempt(const std::string& attemptId)
{
    try {
        const SendStatus status = client_->pollSend(attemptId);
        return AttemptOutcome{status.status, status.errorMessage, status.phase};
    } catch (const ApiError& error) {
        // A definite non-200 response means the server no longer knows this
        // attempt (it expired or the server restarted): report "unknown" so the
        // caller stops tracking it. A transport failure with no HTTP status is
        // transient - report "pending" so the next sync retries.
        if (error.httpStatus != 0) {
            return AttemptOutcome{"unknown", error.what(), {}};
        }
        return AttemptOutcome{"pending", error.what(), {}};
    }
}

void Session::sendContactRequest(const std::string& peerFingerprint, const std::string& text)
{
    // Resolve the peer's prekey, serving server and server card on our own
    // server (facade locality - we never reach a foreign facade). The prekey
    // is signed by the peer (subscription certificate) and the server card by
    // the peer's server, so neither can be substituted by an intermediary.
    const ContactInfo info = client_->lookupContact(peerFingerprint);
    requestWithInfo(peerFingerprint, text, info);
}

FetchTransport Session::fetchTransport() const
{
    return [this](const std::string& toDest, const std::string& op, const Bytes& sealed) {
        // Direct over a fresh transient I2P destination is preferred (our own
        // server is never involved, and a b33 dial authenticates the target), but
        // it needs the embedded router up with tunnels. Only attempt it when I2P
        // is enabled and the router is already running and ready: never force-start
        // a disabled router, and never block building tunnels that may never come
        // up (e.g. no reachable I2P network). Otherwise - or on a direct-dial
        // failure - relay the opaque sealed bytes through our own server's I2P
        // proxy, so adding a contact still works without a local I2P transport.
        if (i2pEnabled()) {
            bazarish::i2p::Router* const router = sharedI2pRouterIfRunning();
            if (router != nullptr && router->ready()) {
                try {
                    return federationFetchOverI2p(*router, toDest, op, sealed, blobFetchPrivacy_);
                } catch (const std::exception&) {
                    // Direct dial failed; fall back to the server proxy below.
                }
            }
        }
        return client_->relayFetch(toDest, op, sealed);
    };
}

std::string Session::addByInvite(const std::string& inviteUri, const std::string& text)
{
    // The invite is a descriptor (fingerprint + serving destination + serving
    // sealing key). Fetch the user-signed contact card for that fingerprint and
    // verify it against the fingerprint (api/FederatedResolve.md): a wrong server
    // can only withhold, never forge a card for someone else's fingerprint.
    //
    // ALWAYS over I2P federation, even for a peer on our own server. Do NOT
    // short-circuit to a facade contact lookup for "same-server" peers: when two
    // clients of one server have different i2p destinations, routing the lookup
    // through I2P is what keeps our own server from learning that we and the peer
    // are co-located on it. A facade lookup would disclose that. (Contacts.md /
    // the co-location rule - the by-fingerprint facade path is only for peers a
    // caller already knows are local.)
    const Descriptor descriptor = parseDescriptor(inviteUri);
    const ContactInfo info = client_->fetchCard(descriptor, fetchTransport());
    // Adopt the name advertised in the invite as this contact's local label.
    requestWithInfo(descriptor.fingerprint, text, info, descriptor.name);
    return descriptor.fingerprint;
}

Session::ContactFetchContext Session::contactFetchContext() const
{
    ContactFetchContext ctx;
    ctx.identityPem = client_->identity().privatePem();  // unencrypted in memory
    ctx.clientId = client_->clientId();
    ctx.endpoint = endpoint();
    ctx.i2pDataDir = profileDir_.parent_path() / "i2p";
    ctx.resolver = resolverCoordinate_;
    ctx.i2pEnabled = i2pEnabled();
    ctx.blobFetchPrivacy = blobFetchPrivacy_;
    return ctx;
}

Session::ContactCardResolved Session::resolveContactCard(
    const ContactFetchContext& context, const ContactCardRequest& request)
{
    ContactCardResolved out;
    out.introText = request.introText;
    // This runs on a background thread (the worker thread stays free for sync), so
    // time it to confirm the connection is never blocked by a slow/unreachable peer.
    const auto started = std::chrono::steady_clock::now();
    const auto elapsedMs = [started]() -> long long {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count();
    };
    bazarish::log::info(
        "contact-add: resolving card off-thread (byUsername={})...", request.byUsername);
    try {
        // A private throwaway client with its OWN connection (and its own request
        // mutex), so this slow federated fetch never contends with the session's
        // sync transport. Constructing a Client does no network work.
        Client fetchClient(Identity::fromPrivatePem(context.identityPem), context.clientId,
            context.endpoint, context.i2pDataDir);
        // Mirrors Session::fetchTransport, but bound to the throwaway client: direct
        // over a transient I2P destination when the router is up, else relay the
        // sealed bytes through our own server.
        const FetchTransport transport = [&fetchClient, &context](const std::string& toDest,
                                             const std::string& op,
                                             const Bytes& sealed) -> FetchOutcome {
            if (context.i2pEnabled) {
                bazarish::i2p::Router* const router = sharedI2pRouterIfRunning();
                if (router != nullptr && router->ready()) {
                    try {
                        return federationFetchOverI2p(
                            *router, toDest, op, sealed, context.blobFetchPrivacy);
                    } catch (const std::exception&) {
                        // Direct dial failed; fall back to the server relay below.
                    }
                }
            }
            return fetchClient.relayFetch(toDest, op, sealed);
        };

        if (request.byUsername) {
            const Descriptor descriptor = fetchClient.resolveAlias(
                normalizeAlias(request.uriOrAlias), context.resolver, nowSeconds(), transport);
            out.info = fetchClient.fetchCard(descriptor, transport);
            out.fingerprint = descriptor.fingerprint;
            out.displayName = request.uriOrAlias;  // the alias typed becomes the label
        } else {
            const Descriptor descriptor = parseDescriptor(request.uriOrAlias);
            out.info = fetchClient.fetchCard(descriptor, transport);
            out.fingerprint = descriptor.fingerprint;
            out.displayName = descriptor.name;
        }
        out.ok = true;
        bazarish::log::info("contact-add: card resolved off-thread in {} ms", elapsedMs());
    } catch (const std::exception& error) {
        out.error = error.what();
        out.ok = false;
        bazarish::log::warn(
            "contact-add: resolve failed off-thread after {} ms: {}", elapsedMs(), error.what());
    }
    return out;
}

std::string Session::commitContactAdd(const ContactCardResolved& resolved)
{
    // Fast: register reply tokens, send the request, record the contact. The slow
    // card fetch already happened off-thread in resolveContactCard.
    requestWithInfo(resolved.fingerprint, resolved.introText, resolved.info, resolved.displayName);
    return resolved.fingerprint;
}

void Session::setResolverCoordinate(ResolverCoordinate coordinate)
{
    resolverCoordinate_ = std::move(coordinate);
}

std::string Session::aliasBuyArtifacts(const std::string& alias) const
{
    // The artifacts the central resolver's portal needs to claim <alias> for this
    // identity: the normalized name, this user's serving destination + sealing
    // key (its descriptor, mirroring inviteUri), and a user-signed alias
    // certificate binding the name to the identity. The portal buy is driven by
    // POSTing this JSON to /portal/buy - the signing key never leaves the client,
    // the resolver only verifies the signature against the descriptor fingerprint.
    if (myDest_.empty() || myServingKeyB64_.empty()) {
        throw std::runtime_error("subscribe first: no serving destination to publish");
    }
    const std::string normalized = normalizeAlias(alias);
    const Bytes aliasCert
        = AliasCertificate::issue(client_->identity(), normalized, nowSeconds(), std::nullopt);
    const nlohmann::json artifacts = {
        {"alias", normalized},
        {"srv", myDest_},
        {"srvKey", myServingKeyB64_},
        {"aliasCert", toBase64(aliasCert)},
    };
    return artifacts.dump();
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
    // verification. Everything after the mapping - the card fetch and its
    // certificates - is verified end-to-end as usual.
    const std::string normalized = normalizeAlias(alias);
    const Descriptor descriptor
        = client_->resolveAlias(normalized, resolverCoordinate_, nowSeconds(), fetchTransport());
    // Card fetch always over I2P (never a facade contact lookup), to avoid
    // disclosing co-location to our own server (see addByInvite / the co-location
    // rule in Contacts.md).
    const ContactInfo info = client_->fetchCard(descriptor, fetchTransport());
    // The alias the user typed becomes this contact's local display name.
    requestWithInfo(descriptor.fingerprint, text, info, alias);
    return descriptor.fingerprint;
}

void Session::requestWithInfo(const std::string& peerFingerprint, const std::string& text,
    const ContactInfo& info, const std::string& displayName)
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
        {"sentAt", nowMillis()},
        {"text", text},
        // Our own self-chosen display name, so the recipient can show a named
        // friend in their roster from the start - mirroring how we learn their
        // name from their descriptor. A one-time seed label, not a live name push
        // (a later rename of ours is never sent; they control the name they keep).
        {"dn", name_},
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
    // The name (from an invite or the alias used) is a one-time local label set
    // at add time; it is never re-fetched or transmitted afterwards.
    if (!displayName.empty()) {
        contact.displayName = displayName;
    }
    persistContacts();
}

std::string Session::requestContactFromGroup(
    const std::string& groupId, const std::string& memberFingerprint, const std::string& text)
{
    if (memberFingerprint == fingerprint()) {
        throw std::runtime_error("cannot add yourself");
    }
    if (contacts_.find(memberFingerprint) != contacts_.end()) {
        throw std::runtime_error("already a contact");
    }
    const auto group = groups_.find(groupId);
    if (group == groups_.end()) {
        throw std::runtime_error("unknown group: " + groupId);
    }
    const auto member = group->second.members.find(memberFingerprint);
    if (member == group->second.members.end()) {
        throw std::runtime_error("not a group member: " + memberFingerprint);
    }
    // The group roster (admin-signed) gives the member's serving routing; we fetch
    // their contact card over it and requestWithInfo verifies the card binds to the
    // fingerprint end to end. So a hostile admin who forged the routing can only
    // make this fetch fail - never seal the request to a key they control.
    Descriptor descriptor;
    descriptor.fingerprint = memberFingerprint;
    descriptor.srv = member->second.dest;
    descriptor.srvKeyDer = fromBase64(member->second.servingSealingB64);
    const ContactInfo info = client_->fetchCard(descriptor, fetchTransport());
    // Seed the local label from the member's self-name if we learned one.
    requestWithInfo(memberFingerprint, text, info, member->second.displayName);
    return memberFingerprint;
}

void Session::acceptContactRequest(const std::string& peerFingerprint)
{
    // Agreeing is simply our first reply to the requester: sendContent attaches our
    // bootstrap (our routing + a reply-token batch) because issuedToThem is still
    // false, which is exactly the descriptor the requester needs to finish the add.
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "contact.accept"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        // Our own display name, so the requester can name us in their roster too -
        // the reverse direction of the requester's `dn` on the contact request. A
        // one-time seed (only when they hold no name for us yet), so names are
        // symmetric after a first exchange, including an add via a group roster.
        {"dn", name_},
    };
    sendContent(peerFingerprint, std::move(inner));
}

bool Session::sendMessage(const std::string& peerFingerprint, const std::string& text,
    const std::string& messageId, const std::function<void()>& onAcceptedByOwnServer,
    std::string* outAttemptId, const std::string& replyTo)
{
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "text"},
        {"id", messageId.empty() ? toHex(randomBytes(8)) : messageId},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"text", text},
    };
    if (!replyTo.empty()) {
        inner["replyTo"] = replyTo;
    }
    return sendContent(peerFingerprint, std::move(inner), onAcceptedByOwnServer, outAttemptId);
}

bool Session::sendFile(const std::string& peerFingerprint, const fs::path& path,
    const std::string& messageId, const std::function<void()>& onAcceptedByOwnServer,
    std::string* outAttemptId, const UploadProgressFn& onUploadProgress,
    const BlobRetention& retention, const std::string& replyTo)
{
    // Encrypt the file under a fresh key straight to a temp ciphertext file and
    // upload it streaming, so a large file is never held whole in memory. The
    // message carries only a small sealed pointer, so the recipient's mailbox
    // quota is never a factor for large files. (Blob storage hosts rotating
    // encrypted-LeaseSet destinations, served only over I2P.)
    const std::uint64_t plainSize = fs::file_size(path);
    const fs::path ciphertextPath
        = profileDir_ / ("blob-upload-" + toHex(randomBytes(8)) + ".tmp");

    BlobPointer pointer;
    BlobUploadResult uploaded;
    try {
        const PackedBlobFile packed = packLargeBlobToFile(path, ciphertextPath);
        uploaded = client_->uploadBlobFromFile(packed, retention, onUploadProgress);
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
        {"sentAt", nowMillis()},
        {"file",
            {
                {"ptr", toBase64(Bytes(pointerJson.begin(), pointerJson.end()))},
                {"size", plainSize},
                {"mime", guessMime(path)},
                {"name", path.filename().string()},
            }},
    };
    if (!replyTo.empty()) {
        inner["replyTo"] = replyTo;
    }
    // Remember the blob so the sender can unsend it later.
    recordSentBlob(inner.at("id").get<std::string>(), uploaded.blobUrl, uploaded.deleteToken);
    // Do not poll for the outcome: the upload already consumed the time budget, so
    // blocking the worker on a delivery poll on top of it is what made a file send
    // feel like a freeze. The grey state is fired on acceptance; a later sync
    // reconciles the attempt to yellow/green/red.
    return sendContent(
        peerFingerprint, std::move(inner), onAcceptedByOwnServer, outAttemptId, false);
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
        {"sentAt", nowMillis()},
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
        {"sentAt", nowMillis()},
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
        {"sentAt", nowMillis()},
        {"data", data},
        {"ref", refMessageId},
    };
    sendContent(peerFingerprint, std::move(inner));
}

bool Session::sendEdit(const std::string& peerFingerprint, const std::string& refMessageId,
    const std::string& text, const InlineKeyboard& keyboard,
    const std::function<void()>& onAcceptedByOwnServer, std::string* outAttemptId)
{
    // An edit fully replaces the target's text and keyboard; the keyboard is
    // always carried (an empty array clears it) so the shape is unambiguous.
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "edit"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"ref", refMessageId},
        {"text", text},
        {"keyboard", keyboardToJson(keyboard)},
    };
    return sendContent(peerFingerprint, std::move(inner), onAcceptedByOwnServer, outAttemptId);
}

void Session::sendDelete(const std::string& peerFingerprint, const std::string& refMessageId)
{
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "delete"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"ref", refMessageId},
    };
    sendContent(peerFingerprint, std::move(inner));
}

void Session::sendReceipt(const std::string& peerFingerprint, const std::string& refMessageId)
{
    // A read receipt confirms a read; it must NOT auto-accept an un-accepted contact
    // request. So it is sent with establishOnFirstReply=false: no bootstrap is
    // attached and issuedToThem is not flipped, so the sender's message can turn
    // green (read) while the request stays pending until explicit Agree (or a real
    // reply). Earlier this bailed entirely for un-accepted contacts, which left the
    // sender stuck on "yellow"; sending without the bootstrap fixes that.
    const auto found = contacts_.find(peerFingerprint);
    if (found == contacts_.end() || found->second.sendTokens.empty()) {
        return;  // unknown contact, or no token to deliver the receipt with
    }
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "receipt"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"ref", refMessageId},
    };
    sendContent(peerFingerprint, std::move(inner), {}, nullptr, false,
        /*establishOnFirstReply=*/false);
}

void Session::sendReaction(const std::string& peerFingerprint, const std::string& refMessageId,
    const std::string& emoji)
{
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "reaction"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"ref", refMessageId},
        {"text", emoji},
    };
    sendContent(peerFingerprint, std::move(inner));
}

void Session::sendGroupReaction(const std::string& groupId, const std::string& refMessageId,
    const std::string& emoji)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        throw std::runtime_error("unknown group: " + groupId);
    }
    Group& group = found->second;
    const std::string id = toHex(randomBytes(8));
    const std::int64_t sentAt = nowMillis();
    // Sign the reaction the same way as a group text: the gsig binds type/ref/emoji
    // to us, so a member cannot forge another member's reaction or move it to a
    // different message (the recipient verifies via authenticateGroupSender).
    const nlohmann::json gsigBody = {
        {"type", "reaction"},
        {"id", id},
        {"from", fingerprint()},
        {"groupId", groupId},
        {"sentAt", sentAt},
        {"ref", refMessageId},
        {"text", emoji},
        {"dn", name_},
    };
    const std::string gsig = toBase64(cms::signJsonHybrid(gsigBody, client_->identity()));
    for (const auto& [fp, member] : group.members) {
        if (member.sendTokens.empty()) {
            continue;
        }
        nlohmann::json inner = {
            {"v", kMessageFormatVersion},
            {"type", "reaction"},
            {"id", id},
            {"from", fingerprint()},
            {"sentAt", sentAt},
            {"ref", refMessageId},
            {"text", emoji},
            {"dn", name_},
            {"group", {{"id", groupId}}},
            {"gsig", gsig},
        };
        try {
            sendToMemberContent(groupId, fp, std::move(inner));
        } catch (const std::exception&) {
        }
    }
    persistGroups();  // tokens were spent in the fan-out
}

void Session::sendGroupReceipt(const std::string& groupId, const std::string& refMessageId,
    const std::string& authorFingerprint)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        return;
    }
    Group& group = found->second;
    const auto member = group.members.find(authorFingerprint);
    if (member == group.members.end() || member->second.sendTokens.empty()) {
        return;  // author unknown or no token to reach them right now
    }
    const std::string id = toHex(randomBytes(8));
    const std::int64_t sentAt = nowMillis();
    // Signed so the author records exactly the verified viewer (a member cannot
    // forge "X read it"). Delivered only to the author, never fanned out.
    const nlohmann::json gsigBody = {
        {"type", "receipt"},
        {"id", id},
        {"from", fingerprint()},
        {"groupId", groupId},
        {"sentAt", sentAt},
        {"ref", refMessageId},
    };
    const std::string gsig = toBase64(cms::signJsonHybrid(gsigBody, client_->identity()));
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "receipt"},
        {"id", id},
        {"from", fingerprint()},
        {"sentAt", sentAt},
        {"ref", refMessageId},
        {"group", {{"id", groupId}}},
        {"gsig", gsig},
    };
    try {
        sendToMemberContent(groupId, authorFingerprint, std::move(inner));
    } catch (const std::exception&) {
    }
    persistGroups();  // a token was spent
}

void Session::sendChatClear(const std::string& peerFingerprint)
{
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "chat.clear"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
    };
    sendContent(peerFingerprint, std::move(inner));
}

void Session::saveAttachment(const std::string& ref, const std::string& keyB64,
    const fs::path& dest, const UploadProgressFn& onProgress, const BlobStageFn& onStage,
    const std::atomic<bool>* cancel)
{
    (void)keyB64;  // the decryption key now travels inside the pointer
    // ref is the base64 sealed blob pointer; fetch the ciphertext over I2P,
    // verify its digest and decrypt it straight to dest (never whole in RAM).
    const Bytes pointerBytes = fromBase64(ref);
    const BlobPointer pointer
        = blobPointerFromJson(nlohmann::json::parse(pointerBytes.begin(), pointerBytes.end()));
    fetchLargeBlobToFile(pointer, dest, onProgress, onStage, cancel);
}

bool Session::sendContent(const std::string& peerFingerprint, nlohmann::json inner,
    const std::function<void()>& onAcceptedByOwnServer, std::string* outAttemptId,
    bool waitForOutcome, bool establishOnFirstReply)
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
    // Captured before the bootstrap block below: when false here, this very send
    // is our first reply to the peer - the moment we accept/establish the dialog.
    const bool wasIssuedToThem = contact.issuedToThem;

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
    // routing + a token batch) so the reverse direction is usable too. A read
    // receipt (establishOnFirstReply=false) skips this, so confirming a read never
    // auto-accepts an un-accepted contact request.
    if (establishOnFirstReply && !contact.issuedToThem) {
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
    const bool delivered = deliver(contact.dest, peerServingKey, "content", peerFingerprint,
        fromBase64(token), payload, onAcceptedByOwnServer, nullptr, outAttemptId, waitForOutcome);

    // Spend the token: it is now committed to this message (consumed by the
    // recipient on delivery, or in flight while the server keeps delivering).
    // deliver() throws on a terminal failure, so a thrown send never spends one.
    contact.sendTokens.pop_back();
    persistContacts();

    // If this send is the first reply that just established the reverse direction
    // (we accepted their request), share our avatar now - consent-gated, exactly
    // the "reply to the friend request" the spec ties avatar exchange to.
    if (!wasIssuedToThem) {
        maybeSendAvatarToContact(peerFingerprint);
    }
    return delivered;
}

void Session::setBlobFetchPrivacy(const bazarish::i2p::Privacy privacy)
{
    blobFetchPrivacy_ = privacy;
}

Bytes Session::fetchLargeBlob(const BlobPointer& pointer)
{
    try {
        // Direct over a fresh transient I2P destination (preferred - our server is
        // never involved).
        return fetchBlob(i2pRouter(), pointer, blobFetchPrivacy_);
    } catch (const std::exception&) {
        // No I2P transport of our own (or the direct fetch failed): fall back to our
        // own server proxying the fetch over I2P.
        return client_->fetchBlobViaProxy(pointer);
    }
}

void Session::fetchLargeBlobToFile(const BlobPointer& pointer, const fs::path& dest,
    const UploadProgressFn& onProgress, const BlobStageFn& onStage,
    const std::atomic<bool>* cancel)
{
    try {
        // Direct over a fresh transient I2P destination, streamed to disk (preferred
        // - our server is never involved and the file never sits whole in RAM).
        fetchBlobToFile(i2pRouter(), pointer, dest, blobFetchPrivacy_, onProgress, onStage, cancel);
    } catch (const BlobNotFoundError&) {
        // The blob has aged out of the store (404/410). The own-server proxy hits
        // the same store and would also 404, so don't bother - surface it at once.
        throw;
    } catch (const std::exception&) {
        // A cancelled fetch (teardown / session switch) must abort, not fall back to
        // a fresh proxy download.
        if (cancel && cancel->load()) {
            throw;
        }
        // No I2P transport of our own (or the direct fetch failed): the own-server proxy
        // relays the whole ciphertext through the facade (buffered fallback).
        const Bytes plain = client_->fetchBlobViaProxy(pointer);
        writeFileBytes(dest, plain);
    }
}

void Session::deleteLargeBlob(const std::string& blobUrl, const std::string& deleteToken)
{
    try {
        deleteBlob(i2pRouter(), blobUrl, deleteToken, blobFetchPrivacy_);  // direct
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
    const fs::path path = profileDir_ / "sent-blobs.json";
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
        writeFileText(profileDir_ / "sent-blobs.json", std::string(sealed.begin(), sealed.end()));
        return;
    }
    writeFileText(profileDir_ / "sent-blobs.json", stored.dump(2));
}

std::vector<IncomingMessage> Session::sync(bool autoAckSurfaced)
{
    std::vector<IncomingMessage> result;
    // Peers whose stash of our tokens is running low and who asked for a
    // refill; topped up after the fetch loop so we never write mid-iteration.
    std::set<std::string> refillPeers;
    // Peers that carried a bootstrap this sync (a contact request, or - for one we
    // requested - their acceptance): we push our avatar to the established ones
    // after the loop, same "never write mid-iteration" rule.
    std::set<std::string> establishedPeers;
    // Groups we were just invited to: hand our token pool to their members after
    // the loop (same "never write mid-iteration" rule as refills).
    std::set<std::string> bootstrapGroups;
    // Groups where a member was removed: rotate our pool after the loop so the
    // removed member's stash of our tokens stops working.
    std::set<std::string> rotateGroups;
    // Groups where a roster added a member: hand that member OUR pool after the
    // loop. On an add only the admin's pool and the newcomer's own pool flow, so
    // without this an existing member and the newcomer can never deliver to each
    // other (the asymmetric "A sees B but B never sees A" silence).
    std::set<std::string> poolRefreshGroups;
    bool groupsTouched = false;
    // Records the sender's group pool. Returns false when the group or member is
    // not known yet (e.g. the tokens arrived before the invite/roster).
    const auto applyGroupTokens = [this](const std::string& groupId, const std::string& from,
                                      const std::vector<std::string>& tokens) -> bool {
        const auto group = groups_.find(groupId);
        if (group == groups_.end()) {
            return false;
        }
        const auto member = group->second.members.find(from);
        if (member == group->second.members.end()) {
            return false;
        }
        for (const std::string& t : tokens) {
            // Dedup, like applyBootstrap: a pre-ack re-fetch must not double tokens.
            if (std::find(member->second.sendTokens.begin(), member->second.sendTokens.end(), t)
                == member->second.sendTokens.end()) {
                member->second.sendTokens.push_back(t);
            }
        }
        return true;
    };
    // Park a token grant that could not be applied yet onto the cross-sync backlog
    // (deduping a re-delivered grant and bounding the backlog), so a grant that
    // arrives in an earlier sync than its roster is retried later, not lost.
    const auto rememberPendingGroupTokens = [this](const std::string& groupId,
                                                const std::string& from,
                                                const std::vector<std::string>& tokens) {
        for (const PendingGroupTokens& p : pendingGroupTokens_) {
            if (p.groupId == groupId && p.from == from && p.tokens == tokens) {
                return;
            }
        }
        constexpr std::size_t kMaxPendingGroupTokenGrants = 64;
        if (pendingGroupTokens_.size() >= kMaxPendingGroupTokenGrants) {
            pendingGroupTokens_.erase(pendingGroupTokens_.begin());
        }
        pendingGroupTokens_.push_back({groupId, from, tokens});
    };
    for (const PendingEntry& entry : client_->listPending()) {
        try {
            const Bytes blob = client_->fetchBlob(entry.id);
            // Every item is sealed to our user sealing key the same way; the
            // server-visible delivery class never changes how we decrypt.
            const Bytes plain = cms::unseal(blob, sealingKey_);
            nlohmann::json body = nlohmann::json::parse(plain.begin(), plain.end());

            IncomingMessage message;
            message.deliveryClass = entry.deliveryClass;
            message.fromFingerprint = body.at("from").get<std::string>();
            message.messageId = body.value("id", std::string());
            message.sentAt = body.value("sentAt", static_cast<std::int64_t>(0));
            std::string type = body.value("type", std::string("text"));

            // Bootstrap may ride with any content type; apply it before dispatch
            // so a new or migrated contact is established regardless of type.
            if (body.contains("bootstrap")) {
                applyBootstrap(contacts_[message.fromFingerprint], body.at("bootstrap"));
                message.establishedContact = true;
                establishedPeers.insert(message.fromFingerprint);
            }

            // A contact request OR its acceptance carries the sender's self-chosen
            // display name (`dn`); adopt it as this contact's initial local label so
            // we show a named friend instead of a bare fingerprint - in BOTH
            // directions (requester names the accepter and vice versa), including an
            // add via a group roster. Only seeds an empty name (never overwrites a
            // name we already hold or the user later set), so a peer can never rename
            // themselves in our roster after the fact.
            if (type == "contact.request" || type == "contact.accept") {
                const std::string dn = body.value("dn", std::string());
                Contact& peer = contacts_[message.fromFingerprint];
                if (!dn.empty() && peer.displayName.empty()) {
                    peer.displayName = dn;
                }
            }

            // The peer is low on our tokens and asked to be refilled.
            if (body.value("lowStash", false)) {
                refillPeers.insert(message.fromFingerprint);
            }

            // A blob pointer: the real content was externalized to blob storage.
            // Fetch it over I2P (a fresh transient destination), verify and decrypt
            // it, then dispatch on the recovered content's real type. Best effort -
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
            } else if (type == "delete") {
                // A delete-for-everyone of a message the sender previously sent:
                // refId is the target message's id; the client drops it.
                message.contentType = type;
                message.refId = body.value("ref", std::string());
            } else if (type == "receipt") {
                // A receipt: the recipient's client received one of our sent
                // messages (the green state). Carries the acknowledged message id.
                // The amber "delivered to the recipient's server" state is reported
                // by our own server (the send attempt), not by this receipt. A group
                // receipt additionally rides a `group` field (authenticated below)
                // and records a viewer; the UI distinguishes the two by groupId.
                message.contentType = type;
                message.refId = body.value("ref", std::string());
            } else if (type == "reaction") {
                // A reaction to a message (1:1 or group): `ref` is the target message
                // id, `text` the emoji (empty removes the reactor's reaction). The
                // reactor is the message's verified `from` (bound by the group gsig
                // below for a group). The UI records it against the target message and
                // never renders it as a chat bubble.
                message.contentType = type;
                message.refId = body.value("ref", std::string());
                message.text = body.value("text", std::string());
            } else if (type == "call.invite" || type == "call.accept" || type == "call.decline"
                || type == "call.end") {
                // Audio-call signalling: update call state and start/stop media. The
                // media itself never touches the server (it rides I2P datagrams).
                handleCallSignal(type, message.fromFingerprint, body, message);
            } else if (type == "token-refill") {
                // The fresh tokens already arrived via the bootstrap block.
                message.contentType = type;
            } else if (type == "device.i2p-master") {
                // A self-sync from another of our devices: adopt the I2P master if we
                // do not already hold one, so this device keeps the same address.
                // Idempotent (a device that already has it ignores it) and handled
                // silently - not a user-visible message.
                message.contentType = type;
                if (message.fromFingerprint == fingerprint() && i2pMaster_.empty()) {
                    try {
                        loadI2pDestination(fromBase64(body.at("i2pMaster").get<std::string>()));
                    } catch (const std::exception&) {
                        // Malformed, wrong key type, or already configured: ignore.
                    }
                }
            } else if (type == "avatar") {
                // A contact pushed their avatar (silent service message): store it
                // and surface the bytes so the UI's avatar store updates. Never a
                // chat bubble.
                message.contentType = type;
                try {
                    const nlohmann::json& av = body.at("avatar");
                    const Bytes data = fromBase64(av.value("data", std::string()));
                    storeContactAvatar(
                        message.fromFingerprint, data, av.value("mime", std::string()));
                    message.avatarData = std::string(data.begin(), data.end());
                } catch (const std::exception&) {
                    // Malformed avatar payload: ignore.
                }
            } else if (type == "device.avatar") {
                // Our own avatar from another of our devices: adopt it. Silent.
                message.contentType = type;
                if (message.fromFingerprint == fingerprint()) {
                    try {
                        const nlohmann::json& av = body.at("avatar");
                        const Bytes data = fromBase64(av.value("data", std::string()));
                        storeOwnAvatar(data, av.value("mime", std::string()));
                        message.avatarData = std::string(data.begin(), data.end());
                    } catch (const std::exception&) {
                        // Malformed avatar payload: ignore.
                    }
                }
            } else if (type == "device.contact-name") {
                // A contact rename mirrored from another of our devices: apply it
                // locally (purely a local label). Silent.
                message.contentType = type;
                if (message.fromFingerprint == fingerprint()) {
                    const auto named = contacts_.find(body.value("peer", std::string()));
                    if (named != contacts_.end()) {
                        named->second.displayName = body.value("name", std::string());
                    }
                }
            } else if (type == "device.chat-pin") {
                // A pin/unpin mirrored from another of our devices. The pin list lives
                // in the client's local store, so surface it (the peer in refId, the
                // pinned flag in text) for the GUI to apply. Honoured only from us.
                message.contentType = type;
                if (message.fromFingerprint == fingerprint()) {
                    message.refId = body.value("peer", std::string());
                    message.text
                        = body.value("pinned", false) ? std::string("1") : std::string("0");
                }
            } else if (type == "group.invite") {
                // Added to a group: verify and store the signed roster, then bootstrap
                // our token pool to its members after the loop.
                message.contentType = type;
                message.groupId = body.value("groupId", std::string());
                message.groupName = body.value("name", std::string());
                message.text = message.groupName;
                try {
                    // bootstrap=true: a first invite may establish the admin set
                    // (trust on first use). A re-invite to a group we already hold
                    // is still anchored to our known admins inside applyRoster.
                    applyRoster(message.groupId,
                        fromBase64(body.at("roster").get<std::string>()), nullptr, true);
                    bootstrapGroups.insert(message.groupId);
                    groupsTouched = true;
                    // A genuine re-invitation lifts the tombstone so the chat may
                    // come back (a fresh, empty conversation under the same id).
                    if (leftGroups_.erase(message.groupId) > 0) {
                        persistLeftGroups();
                    }
                } catch (const std::exception&) {
                    // Untrusted/malformed roster: surface the invite, do not join.
                }
            } else if (type == "group.tokens") {
                // A member's token pool for a group; record it so we can deliver to
                // them. Held on the cross-sync backlog when the group/member is not
                // known yet (its invite or roster has not arrived), retried later.
                message.contentType = type;
                message.groupId = body.value("groupId", std::string());
                // A token pool for a group we left must not re-create it.
                if (leftGroups_.count(message.groupId) == 0) {
                    std::vector<std::string> tokens;
                    for (const auto& token : body.value("tokens", nlohmann::json::array())) {
                        tokens.push_back(token.get<std::string>());
                    }
                    if (applyGroupTokens(message.groupId, message.fromFingerprint, tokens)) {
                        groupsTouched = true;
                    } else {
                        rememberPendingGroupTokens(
                            message.groupId, message.fromFingerprint, tokens);
                    }
                }
            } else if (type == "group.roster") {
                // A signed roster update (membership/admin/epoch). If it removed a
                // member, rotate our pool after the loop so their tokens die.
                message.contentType = type;
                message.groupId = body.value("groupId", std::string());
                // A roster for a group we left must not re-create it (applyRoster
                // would otherwise insert a fresh entry).
                if (leftGroups_.count(message.groupId) == 0) {
                    try {
                        bool shrank = false;
                        std::vector<std::string> added;
                        applyRoster(message.groupId,
                            fromBase64(body.at("roster").get<std::string>()), &shrank, false,
                            &added);
                        groupsTouched = true;
                        if (shrank) {
                            rotateGroups.insert(message.groupId);
                        }
                        if (!added.empty()) {
                            poolRefreshGroups.insert(message.groupId);
                        }
                        message.groupAddedMembers = std::move(added);
                    } catch (const std::exception&) {
                    }
                }
            } else if (type == "group.leave") {
                message.contentType = type;
                message.groupId = body.value("groupId", std::string());
                const auto group = groups_.find(message.groupId);
                if (group != groups_.end()) {
                    // A member removes ONLY themselves, authenticated by the leave's
                    // own signature - `from` alone is unauthenticated and must never
                    // drive a membership change. An unsigned/forged leave is ignored
                    // (the next roster reconciles membership).
                    const std::optional<std::string> leaver
                        = authenticateGroupLeave(body, message.groupId);
                    if (leaver && group->second.members.erase(*leaver) > 0) {
                        groupsTouched = true;
                    }
                }
            } else if (type == "chat.clear") {
                // The peer asked to clear our whole conversation with them; the GUI
                // wipes its transcript on receipt. No core state changes here.
                message.contentType = type;
            } else if (type == "group.avatar" || type == "group.rename") {
                // A group photo or rename by an admin; applied/surfaced after the
                // group-sender authentication and admin check below (so an unsigned
                // or non-admin one cannot take effect). The rename carries the new
                // name in `text` (so the recipient's notice is not blank).
                message.contentType = type;
                if (type == "group.rename") {
                    message.text = body.value("text", std::string());
                }
            } else if (type == "contact.accept") {
                // The peer agreed to our contact request: their descriptor + reply
                // tokens already rode in the bootstrap block above, so we are now a
                // mutual contact. Surfaced as a system note by the UI.
                message.contentType = type;
            } else {
                message.contentType = "unsupported";
                message.rawType = type;
            }

            // A content message may belong to a group (filed under it, not the 1:1
            // thread). Orthogonal to the content type.
            if (body.contains("group")) {
                message.groupId = body.at("group").value("id", std::string());
                // A content message for a group we left must never resurrect the
                // chat: consume it and move on (its sender's tokens for us are
                // already revoked; this catches anything sent before that landed).
                if (leftGroups_.count(message.groupId) > 0) {
                    client_->ack(entry.id);
                    continue;
                }
                // The sender is authenticated by a per-message hybrid signature
                // (`gsig`): the roster attests who is a member, but only this binds
                // the message's `from` and content to a signing identity. Drop
                // anything unsigned, malformed, field-mismatched, or (for a group we
                // already know) from a non-member - otherwise a member could forge
                // another member's `from`. The verified signer is the authoritative
                // sender. See docs Groups.md.
                const auto group = groups_.find(message.groupId);
                const std::optional<std::string> authedFrom = authenticateGroupSender(body, type,
                    message.messageId, message.groupId,
                    group != groups_.end() ? &group->second.members : nullptr);
                if (!authedFrom.has_value()) {
                    client_->ack(entry.id);  // consume the spoofed/unsigned item; never surface it
                    continue;
                }
                message.fromFingerprint = *authedFrom;
                // Group photo and rename are admin-only: drop a forged one from a
                // non-admin (so a bogus photo never applies and a bogus rename
                // notice never surfaces). The roster remains the name's authority.
                if (type == "group.avatar" || type == "group.rename") {
                    const auto g = groups_.find(message.groupId);
                    const bool senderIsAdmin = g != groups_.end()
                        && g->second.members.count(*authedFrom) > 0
                        && g->second.members.at(*authedFrom).admin;
                    if (!senderIsAdmin) {
                        client_->ack(entry.id);
                        continue;
                    }
                }
                // Learn the sender's self-chosen display name (signed via gsig), so
                // a non-contact member shows their own name instead of a raw
                // fingerprint. Cached on the member; persisted with the groups.
                const std::string dn = body.value("dn", std::string());
                const auto known = groups_.find(message.groupId);
                if (known != groups_.end()) {
                    const auto mem = known->second.members.find(*authedFrom);
                    if (mem != known->second.members.end() && !dn.empty()
                        && mem->second.displayName != dn) {
                        mem->second.displayName = dn;
                        groupsTouched = true;
                    }
                }
                // A rename by an admin: adopt the new name now. The notice is
                // admin-signed (just verified), exactly like the roster, so it is an
                // equally trustworthy name update - and adopting it here makes the
                // displayed group name change even if the separate, epoch-protected
                // group.roster broadcast is delayed or lost in this sync. The roster
                // remains the tiebreak authority (a stale lower-epoch roster cannot
                // revert it).
                if (type == "group.rename") {
                    const std::string newName = body.value("text", std::string());
                    Group& renamed = groups_[message.groupId];
                    if (!newName.empty() && renamed.name != newName) {
                        renamed.name = newName;
                        groupsTouched = true;
                    }
                }
                // A group photo set by a member: now that the setter is
                // authenticated, store the image and surface its bytes so the UI
                // updates the group's avatar (the bubble itself is rendered too).
                if (type == "group.avatar") {
                    try {
                        const nlohmann::json& av = body.at("avatar");
                        const Bytes data = fromBase64(av.value("data", std::string()));
                        storeGroupAvatar(
                            message.groupId, data, av.value("mime", std::string()));
                        message.avatarData = std::string(data.begin(), data.end());
                        groupsTouched = true;
                    } catch (const std::exception&) {
                        // Malformed avatar payload: still surface the bubble.
                    }
                }
            }

            // An inline keyboard may ride on any content message (typically text);
            // preserve it as its wire form so a UI can render the buttons.
            if (body.contains("keyboard")) {
                message.keyboardJson = body.at("keyboard").dump();
            }
            // A reply reference (the replied-to message's protocol id) may ride on
            // any content message; preserve it so the UI can render a quote/link.
            if (body.contains("replyTo")) {
                message.replyTo = body.value("replyTo", std::string());
            }

            // Ack now (CLI/bots), or defer to the caller (GUI). Deferring acks a
            // surfaced item only after the client has durably stored it, so a
            // crash/restart between fetch and store never loses it. Re-processing on
            // a pre-ack re-fetch is safe - applyBootstrap/applyGroupTokens dedup
            // tokens and the GUI dedups by messageId.
            if (autoAckSurfaced) {
                client_->ack(entry.id);
            } else {
                message.pendingId = entry.id;
            }
            result.push_back(std::move(message));
        } catch (const std::exception& error) {
            // Isolate a poison item: a single unreadable pending entry must
            // never throw out of the whole sync. That would leave it unacked,
            // blocking every later item and pinning the account at
            // "connecting" forever. Consume it so the mailbox unblocks and
            // carry on; the bytes are already delivered to us, we just cannot
            // read them. If the ack itself fails the server is unreachable, so
            // the exception propagates and the caller retries the whole tick.
            bazarish::log::warn(
                "sync: dropping unreadable pending item: {}", error.what());
            client_->ack(entry.id);
        }
    }
    persistContacts();

    // A peer accepted our request (or we just learned their routing): push our
    // avatar to any now-established contact we have engaged with. maybeSend... is
    // gated on issuedToThem, so an un-accepted incoming request never triggers an
    // automatic avatar reply. Best effort, after the loop like the refills below.
    for (const std::string& peer : establishedPeers) {
        maybeSendAvatarToContact(peer);
    }

    // Refill peers that ran low (a fresh token batch, sent as a token-refill).
    // Done after the loop so the outbound send never races the fetch loop.
    for (const std::string& peer : refillPeers) {
        // Best-effort: a peer we cannot route to right now must not fail the
        // whole sync (which would read as "server unreachable"); retry next tick.
        try {
            sendTokenRefill(peer);
        } catch (const std::exception& error) {
            bazarish::log::warn("sync: token refill failed: {}", error.what());
        }
    }
    // Retry parked token grants (their group/roster had not arrived when they
    // came in). Held ACROSS syncs, so a grant that arrived in an earlier sync than
    // its roster still lands. Applied grants drop off; the rest wait for a later
    // roster.
    if (!pendingGroupTokens_.empty()) {
        std::vector<PendingGroupTokens> stillPending;
        for (PendingGroupTokens& parked : pendingGroupTokens_) {
            if (applyGroupTokens(parked.groupId, parked.from, parked.tokens)) {
                groupsTouched = true;
            } else {
                stillPending.push_back(std::move(parked));
            }
        }
        pendingGroupTokens_ = std::move(stillPending);
    }
    // Hand our token pool to the members of any group we were just invited to.
    for (const std::string& groupId : bootstrapGroups) {
        // Best-effort, like the token refills above.
        try {
            broadcastGroupPool(groupId);
        } catch (const std::exception& error) {
            bazarish::log::warn("sync: group pool broadcast failed: {}", error.what());
        }
    }
    // Hand our pool to groups where a roster just added a member (existing-member
    // side), so the newcomer can deliver to us - the bootstrap broadcast above
    // already covers a group we were ourselves just invited to.
    for (const std::string& groupId : poolRefreshGroups) {
        if (bootstrapGroups.find(groupId) != bootstrapGroups.end()) {
            continue;
        }
        try {
            broadcastGroupPool(groupId);
        } catch (const std::exception& error) {
            bazarish::log::warn("sync: group pool refresh failed: {}", error.what());
        }
    }
    // Rotate our pool for groups where someone was removed (cut them off).
    for (const std::string& groupId : rotateGroups) {
        if (bootstrapGroups.find(groupId) == bootstrapGroups.end()) {
            try {
                rotateGroupPool(groupId);
            } catch (const std::exception& error) {
                bazarish::log::warn("sync: group pool rotate failed: {}", error.what());
            }
        }
    }
    if (groupsTouched) {
        persistGroups();
    }
    return result;
}

void Session::ackPending(const std::string& pendingId)
{
    if (!pendingId.empty()) {
        client_->ack(pendingId);
    }
}

void Session::sendTokenRefill(const std::string& peerFingerprint)
{
    const auto found = contacts_.find(peerFingerprint);
    if (found == contacts_.end()) {
        return;
    }
    const Contact& contact = found->second;
    // We need a usable route and at least one of the peer's tokens to deliver
    // the refill; otherwise the peer's own refill of us must arrive first. And,
    // like the automatic avatar push, never auto-refill a peer whose incoming
    // request we have not accepted yet (issuedToThem still false): that would
    // attach our bootstrap and silently establish the contact.
    if (!contact.issuedToThem || contact.sealingPublicB64.empty()
        || contact.servingSealingB64.empty() || contact.sendTokens.empty()) {
        return;
    }
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "token-refill"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"bootstrap", {{"replyTokens", issueTokenBatch()}}},
    };
    sendContent(peerFingerprint, std::move(inner));
}

// ============================ Audio calls ============================

void Session::setAudioBackend(AudioSourceFactory sourceFactory, AudioSinkFactory sinkFactory)
{
    audioSourceFactory_ = std::move(sourceFactory);
    audioSinkFactory_ = std::move(sinkFactory);
}

void Session::setVideoBackend(VideoSourceFactory sourceFactory, VideoSinkFactory sinkFactory)
{
    videoSourceFactory_ = std::move(sourceFactory);
    videoSinkFactory_ = std::move(sinkFactory);
}

std::shared_ptr<bazarish::i2p::Endpoint> Session::openCallMediaSession()
{
    // Call media rides a one-time encrypted-LS (b33) destination on the embedded
    // router, published so the peer can send RAW media datagrams to it; torn down
    // with the call. Minimal-length tunnels (1 hop each way, no variance): the
    // single biggest latency/jitter lever for realtime media. Safe here because
    // the media destination is one-time and unlinked from the identity
    // destination, so a short tunnel never weakens identity anonymity.
    return i2pRouter().createEndpoint(bazarish::i2p::EndpointConfig{
        bazarish::i2p::Keys::generate(), bazarish::i2p::LeaseSetKind::eEncrypted,
        bazarish::i2p::Privacy::eMinimal, bazarish::i2p::kDefaultTunnelQuantity, true});
}

void Session::startCallMedia()
{
    call_.transport = std::make_unique<I2pCallTransport>(*call_.dgram, call_.peerMediaDest);
    std::unique_ptr<AudioSource> audioSource
        = audioSourceFactory_ ? audioSourceFactory_() : std::make_unique<SineAudioSource>();
    std::unique_ptr<AudioSink> audioSink
        = audioSinkFactory_ ? audioSinkFactory_() : std::make_unique<CapturingAudioSink>();
    // Video backends are wired only on a video call; null source/sink leave the
    // engine audio-only.
    std::unique_ptr<VideoSource> videoSource;
    std::unique_ptr<VideoSink> videoSink;
    if (call_.video) {
        videoSource
            = videoSourceFactory_ ? videoSourceFactory_() : std::make_unique<PatternVideoSource>();
        videoSink = videoSinkFactory_ ? videoSinkFactory_() : std::make_unique<CapturingVideoSink>();
    }
    call_.media = std::make_unique<CallMedia>(*call_.transport, std::move(audioSource),
        std::move(audioSink), std::move(videoSource), std::move(videoSink), call_.mediaKey,
        call_.initiator ? CallRole::eCaller : CallRole::eCallee);
    call_.media->setMuted(call_.muted);
    call_.media->setCameraEnabled(!call_.cameraOff);
    call_.media->start();
}

void Session::clearCall()
{
    if (call_.media) {
        call_.media->stop();
    }
    call_.media.reset();
    call_.transport.reset();
    call_.dgram.reset();
    call_.state = CallState::eIdle;
    call_.callId.clear();
    call_.peerFingerprint.clear();
    call_.peerMediaDest.clear();
    call_.mediaKey.clear();
    call_.initiator = false;
    call_.video = false;
    call_.muted = false;
    call_.cameraOff = false;
}

void Session::sendCallSignal(
    const std::string& peerFingerprint, const std::string& type, nlohmann::json extra)
{
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", type},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
    };
    for (const auto& field : extra.items()) {
        inner[field.key()] = field.value();
    }
    sendContent(peerFingerprint, std::move(inner));
}

void Session::startCall(const std::string& peerFingerprint, const bool video)
{
    if (call_.state != CallState::eIdle) {
        throw std::runtime_error("a call is already in progress");
    }
    if (contacts_.find(peerFingerprint) == contacts_.end()) {
        throw std::runtime_error("unknown contact: " + peerFingerprint);
    }
    // Build the media destination first (strict I2P); only then announce the call.
    auto dgram = openCallMediaSession();
    const std::string callId = toHex(randomBytes(8));
    const Bytes mediaKey = randomBytes(kAeadKeyBytes);
    sendCallSignal(peerFingerprint, "call.invite",
        {
            {"callId", callId},
            {"media", video ? "video" : "audio"},
            {"codec", "opus"},
            {"video", video ? "vp8" : ""},
            {"dest", dgram->routingHost()},
            {"key", toBase64(mediaKey)},
        });
    call_.state = CallState::eOutgoing;
    call_.callId = callId;
    call_.peerFingerprint = peerFingerprint;
    call_.mediaKey = mediaKey;
    call_.initiator = true;
    call_.video = video;
    call_.muted = false;
    call_.cameraOff = false;
    call_.dgram = std::move(dgram);
}

void Session::startAudioCall(const std::string& peerFingerprint)
{
    startCall(peerFingerprint, false);
}

void Session::startVideoCall(const std::string& peerFingerprint)
{
    startCall(peerFingerprint, true);
}

void Session::acceptCall(const std::string& callId)
{
    if (call_.state != CallState::eIncoming || call_.callId != callId) {
        throw std::runtime_error("no matching incoming call");
    }
    auto dgram = openCallMediaSession();
    sendCallSignal(call_.peerFingerprint, "call.accept",
        {{"callId", callId}, {"media", call_.video ? "video" : "audio"},
            {"dest", dgram->routingHost()}});
    call_.dgram = std::move(dgram);
    call_.state = CallState::eActive;
    startCallMedia();
}

void Session::declineCall(const std::string& callId)
{
    if (call_.state != CallState::eIncoming || call_.callId != callId) {
        throw std::runtime_error("no matching incoming call");
    }
    const std::string peer = call_.peerFingerprint;
    clearCall();
    try {
        sendCallSignal(peer, "call.decline", {{"callId", callId}, {"reason", "declined"}});
    } catch (const std::exception&) {
        // The local call is already cleared; a failed signal only leaves the
        // caller to time out on its own.
    }
}

void Session::endCall()
{
    if (call_.state == CallState::eIdle) {
        return;
    }
    const std::string peer = call_.peerFingerprint;
    const std::string callId = call_.callId;
    clearCall();
    try {
        sendCallSignal(peer, "call.end", {{"callId", callId}});
    } catch (const std::exception&) {
    }
}

void Session::setCallMuted(const bool muted)
{
    call_.muted = muted;
    if (call_.media) {
        call_.media->setMuted(muted);
    }
}

void Session::setCameraEnabled(const bool enabled)
{
    call_.cameraOff = !enabled;
    if (call_.media) {
        call_.media->setCameraEnabled(enabled);
    }
}

Session::CallInfo Session::currentCall() const
{
    CallInfo info;
    info.state = call_.state;
    info.callId = call_.callId;
    info.peerFingerprint = call_.peerFingerprint;
    info.video = call_.video;
    info.muted = call_.muted;
    info.cameraOn = !call_.cameraOff;
    if (call_.media) {
        info.packetsSent = call_.media->packetsSent();
        info.packetsReceived = call_.media->packetsReceived();
    }
    return info;
}

void Session::handleCallSignal(const std::string& type, const std::string& from,
    const nlohmann::json& body, IncomingMessage& message)
{
    message.contentType = type;
    message.callId = body.value("callId", std::string());

    if (type == "call.invite") {
        if (call_.state != CallState::eIdle) {
            // Already busy: decline so the caller is not left ringing.
            try {
                sendCallSignal(
                    from, "call.decline", {{"callId", message.callId}, {"reason", "busy"}});
            } catch (const std::exception&) {
            }
            message.text = "busy";
            return;
        }
        Bytes key;
        try {
            key = fromBase64(body.value("key", std::string()));
        } catch (const std::exception&) {
            return;  // malformed invite: surface the event but do not ring
        }
        if (key.size() != kAeadKeyBytes) {
            return;
        }
        call_.state = CallState::eIncoming;
        call_.callId = message.callId;
        call_.peerFingerprint = from;
        call_.peerMediaDest = body.value("dest", std::string());
        call_.mediaKey = std::move(key);
        call_.initiator = false;
        call_.video = body.value("media", std::string("audio")) == "video";
        call_.muted = false;
        call_.cameraOff = false;
        return;
    }

    if (type == "call.accept") {
        if (call_.state == CallState::eOutgoing && call_.callId == message.callId
            && from == call_.peerFingerprint) {
            call_.peerMediaDest = body.value("dest", std::string());
            call_.state = CallState::eActive;
            startCallMedia();
        }
        return;
    }

    // call.decline / call.end: tear the call down if it is the one we track.
    if (call_.state != CallState::eIdle && call_.callId == message.callId
        && from == call_.peerFingerprint) {
        clearCall();
    }
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

    // Our own entry first (with our routing), then every other member. Each entry
    // carries `dn` - a display name the signer (an admin) supplies for that member:
    // our own self-chosen name for ourselves, and our local contact name for any
    // member we know, so a recipient who has neither a contact for them nor a
    // message from them yet can still show a name. Empty when we have no name.
    members.push_back({
        {"fp", fingerprint()},
        {"sealing", sealingPublicB64()},
        {"dest", myDest_},
        {"servingKey", ownServingKeyB64()},
        {"admin", group.iAmAdmin},
        {"dn", name_},
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
            {"dn", contactDisplayName(fp)},
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

std::optional<std::string> Session::authenticateGroupLeave(
    const nlohmann::json& body, const std::string& groupId)
{
    try {
        const cms::VerifiedHybridJson v
            = cms::verifyJsonHybrid(fromBase64(body.at("gsig").get<std::string>()));
        // The signature authorizes removing exactly its own signer: the signed
        // `from` must equal the verified identity (so a member cannot sign a leave
        // claiming another member's `from`), and the group id must match.
        if (v.body.value("type", std::string()) == "group.leave"
            && v.body.value("groupId", std::string()) == groupId
            && !v.identityFingerprint.empty()
            && v.body.value("from", std::string()) == v.identityFingerprint) {
            return v.identityFingerprint;
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

bool Session::isRosterUpdateAuthorized(const std::string& signerFingerprint,
    const nlohmann::json& newRosterBody, const std::set<std::string>& currentAdmins,
    bool established, bool bootstrap)
{
    if (established) {
        // Anchored authority: the signer must be an admin we ALREADY recognize.
        // The incoming roster's own `admins` array is deliberately NOT consulted -
        // consulting it would let a member sign a roster naming themselves admin.
        return currentAdmins.count(signerFingerprint) > 0;
    }
    if (bootstrap) {
        // First roster (an invite), trusted on first use via the inviting contact:
        // accept a self-consistent roster signed by one of its declared admins.
        for (const nlohmann::json& admin : newRosterBody.at("admins")) {
            if (admin.get<std::string>() == signerFingerprint) {
                return true;
            }
        }
    }
    return false;
}

void Session::applyRoster(const std::string& groupId, const Bytes& rosterDer,
    bool* membershipShrank, bool bootstrap, std::vector<std::string>* addedMembers)
{
    // The roster is self-verifying: the embedded signature yields the signer's
    // identity fingerprint.
    const cms::VerifiedHybridJson verified = cms::verifyJsonHybrid(rosterDer);
    const nlohmann::json& body = verified.body;
    if (body.at("groupId").get<std::string>() != groupId) {
        throw std::runtime_error("group roster id mismatch");
    }

    // The admin set WE currently recognize for this group (anchored authority), and
    // whether we already hold the group at all.
    const auto existing = groups_.find(groupId);
    const bool established = existing != groups_.end() && !existing->second.members.empty();
    std::set<std::string> currentAdmins;
    if (existing != groups_.end()) {
        for (const auto& [fp, member] : existing->second.members) {
            if (member.admin) {
                currentAdmins.insert(fp);
            }
        }
        if (existing->second.iAmAdmin) {
            currentAdmins.insert(fingerprint());
        }
    }
    if (!isRosterUpdateAuthorized(
            verified.identityFingerprint, body, currentAdmins, established, bootstrap)) {
        throw std::runtime_error("group roster not signed by a current admin");
    }

    Group& group = groups_[groupId];
    const std::int64_t epoch = body.value("epoch", std::int64_t{0});
    if (epoch < group.epoch) {
        return;  // stale roster
    }

    // Preserve pool tokens and learned names we already hold for members that
    // remain - the rebuild below clears the roster, so without this a member's own
    // display name (and any earlier provisional label) would be wiped on every
    // roster update.
    std::map<std::string, std::vector<std::string>> keptTokens;
    std::map<std::string, std::string> keptDisplayNames;
    std::map<std::string, std::string> keptProvisionalNames;
    for (const auto& [fp, member] : group.members) {
        keptTokens[fp] = member.sendTokens;
        keptDisplayNames[fp] = member.displayName;
        keptProvisionalNames[fp] = member.provisionalName;
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
        const auto keptName = keptDisplayNames.find(fp);
        if (keptName != keptDisplayNames.end()) {
            member.displayName = keptName->second;
        }
        // The admin's provisional label for this member: a fresh, non-empty one in
        // the roster overrides; an absent one keeps whatever we already had.
        const std::string rosterDn = jm.value("dn", std::string());
        if (!rosterDn.empty()) {
            member.provisionalName = rosterDn;
        } else {
            const auto keptProv = keptProvisionalNames.find(fp);
            if (keptProv != keptProvisionalNames.end()) {
                member.provisionalName = keptProv->second;
            }
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

    // A member present now but absent before is a join: report it so the caller
    // can surface a "joined the group" notice to the existing members.
    if (addedMembers != nullptr) {
        for (const auto& [fp, member] : group.members) {
            (void)member;
            if (keptTokens.find(fp) == keptTokens.end()) {
                addedMembers->push_back(fp);
            }
        }
    }
}

void Session::sendToMemberContact(const std::string& memberFp, const GroupMember& member,
    const nlohmann::json& inner, std::string* outAttemptId)
{
    const std::string text = inner.dump();
    const Key memberSealing = Key::fromPublicDer(fromBase64(member.sealingPublicB64));
    const Bytes payload = cms::seal(Bytes(text.begin(), text.end()), memberSealing);
    const Key memberServingKey = Key::fromPublicDer(fromBase64(member.servingSealingB64));
    // Tokenless contact-class delivery - the standing path into any mailbox. Submit
    // without blocking on the outcome (store-and-forward federates in the background);
    // the caller reconciles the attempt to yellow/green/red later.
    deliver(member.dest, memberServingKey, "contact", memberFp, std::nullopt, payload, {}, nullptr,
        outAttemptId, /*waitForOutcome=*/false);
}

void Session::sendToMemberContent(const std::string& groupId, const std::string& memberFp,
    const nlohmann::json& inner, std::string* outAttemptId)
{
    Group& group = groups_.at(groupId);
    GroupMember& member = group.members.at(memberFp);
    if (member.sendTokens.empty()) {
        throw std::runtime_error("no usable group token for member " + memberFp);
    }
    const std::string text = inner.dump();
    const Key memberSealing = Key::fromPublicDer(fromBase64(member.sealingPublicB64));
    const Bytes payload = cms::seal(Bytes(text.begin(), text.end()), memberSealing);
    const Key memberServingKey = Key::fromPublicDer(fromBase64(member.servingSealingB64));

    // Spend one pool token and submit WITHOUT polling the outcome (waitForOutcome
    // false), exactly like a one-to-one send: the own server accepts at once
    // (store-and-forward) and federates in the background, so a group fan-out never
    // blocks the worker on per-member delivery polls. (A token a concurrent sender
    // already spent is reconciled by the member's next pool refill; the previous
    // synchronous retry is what made every group send wait on each recipient.)
    const std::string token = member.sendTokens.back();
    member.sendTokens.pop_back();
    deliver(member.dest, memberServingKey, "content", memberFp, fromBase64(token), payload, {},
        nullptr, outAttemptId, false);
}

bool Session::sendGroupContentToMember(const std::string& groupId, const std::string& memberFp,
    const GroupMember& member, nlohmann::json inner, std::string* outAttemptId)
{
    try {
        if (!member.sendTokens.empty()) {
            sendToMemberContent(groupId, memberFp, std::move(inner), outAttemptId);
        } else {
            // No token yet (a just-added member before their pool reached us): fall
            // back to the tokenless contact class so the message is never silently
            // dropped. Content-class resumes once their pool arrives.
            sendToMemberContact(memberFp, member, inner, outAttemptId);
        }
        return true;
    } catch (const std::exception&) {
        // Best-effort per member: a single unreachable member never aborts the rest.
        return false;
    }
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
        {"sentAt", nowMillis()},
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
            {"sentAt", nowMillis()},
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

std::optional<std::string> Session::authenticateGroupSender(const nlohmann::json& body,
    const std::string& type, const std::string& messageId, const std::string& groupId,
    const std::map<std::string, GroupMember>* members)
{
    try {
        const cms::VerifiedHybridJson signed_
            = cms::verifyJsonHybrid(fromBase64(body.at("gsig").get<std::string>()));
        const nlohmann::json& sb = signed_.body;
        // The signer signs their OWN from; the outer claim must match it; the
        // identifying + content fields must match what was signed; and (when we
        // know the group) the signer must be a current member.
        const bool memberKnown
            = members == nullptr || members->count(signed_.identityFingerprint) > 0;
        const bool ok = sb.value("from", std::string()) == signed_.identityFingerprint
            && sb.value("from", std::string()) == body.value("from", std::string())
            && sb.value("groupId", std::string()) == groupId
            && sb.value("id", std::string()) == messageId
            && sb.value("type", std::string()) == type
            && sb.value("text", std::string()) == body.value("text", std::string())
            // The target reference (`ref`) is signed too, so an edit/reaction/receipt
            // cannot be re-pointed at another message. Absent on plain messages
            // (empty == empty), so this stays backward compatible.
            && sb.value("ref", std::string()) == body.value("ref", std::string())
            // The sender's own display name (`dn`) is signed too, so a member
            // cannot put a different self-name on another member's message. Absent
            // on older senders (empty == empty), so this stays backward compatible.
            && sb.value("dn", std::string()) == body.value("dn", std::string())
            && memberKnown;
        if (ok) {
            return signed_.identityFingerprint;
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

Session::GroupFanout Session::sendGroupMessage(
    const std::string& groupId, const std::string& text, const std::string& replyTo,
    const std::string& messageId)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        throw std::runtime_error("unknown group: " + groupId);
    }
    Group& group = found->second;
    // One logical message id + timestamp shared across the fan-out. The caller's id
    // is reused when given, so its own stored copy and every recipient's copy carry
    // the SAME id - an edit/reply/delete referencing it then resolves on every side.
    const std::string id = messageId.empty() ? toHex(randomBytes(8)) : messageId;
    const std::int64_t sentAt = nowMillis();
    // Authenticate the sender per message: a hybrid signature over the message's
    // identifying and content fields. The signed roster only attests membership,
    // so without this a member could put another member's fingerprint in `from`.
    // verifyJsonHybrid yields the signer's identity fingerprint, which the
    // recipient checks equals `from` (and is a current member); see Groups.md.
    const nlohmann::json gsigBody = {
        {"type", "text"},
        {"id", id},
        {"from", fingerprint()},
        {"groupId", groupId},
        {"sentAt", sentAt},
        {"text", text},
        {"dn", name_},
    };
    const std::string gsig = toBase64(cms::signJsonHybrid(gsigBody, client_->identity()));
    GroupFanout coverage;
    coverage.total = static_cast<int>(group.members.size());
    for (const auto& [fp, member] : group.members) {
        nlohmann::json inner = {
            {"v", kMessageFormatVersion},
            {"type", "text"},
            {"id", id},
            {"from", fingerprint()},
            {"sentAt", sentAt},
            {"text", text},
            {"dn", name_},
            {"group", {{"id", groupId}}},
            {"gsig", gsig},
        };
        // The reply reference rides outside the signature: each member gets their
        // own sealed copy, so the CMS seal already protects it end to end.
        if (!replyTo.empty()) {
            inner["replyTo"] = replyTo;
        }
        // Fan out to every member: content class when we hold a token, else the
        // tokenless contact-class fallback so a just-added member (whose pool has not
        // reached us yet) is never silently skipped - the admin's first message used
        // to vanish until that member wrote first. See sendGroupContentToMember.
        std::string attemptId;
        const bool ok = sendGroupContentToMember(groupId, fp, member, std::move(inner), &attemptId);
        coverage.members.push_back(MemberOutcome{fp, ok, attemptId});
        if (ok) {
            ++coverage.reached;
        }
    }
    persistGroups();
    return coverage;
}

bool Session::sendGroupTextToMember(const std::string& groupId, const std::string& memberFp,
    const std::string& text, const std::string& replyTo, const std::string& messageId,
    std::string* outAttemptId)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        throw std::runtime_error("unknown group: " + groupId);
    }
    Group& group = found->second;
    const auto member = group.members.find(memberFp);
    if (member == group.members.end()) {
        return false;  // no longer a member
    }
    const std::int64_t sentAt = nowMillis();
    // Re-sign with the SAME id so the recipient's copy (and any edit/reply that
    // references it) matches the original; the recipient dedups a copy it already has.
    const nlohmann::json gsigBody = {
        {"type", "text"},
        {"id", messageId},
        {"from", fingerprint()},
        {"groupId", groupId},
        {"sentAt", sentAt},
        {"text", text},
        {"dn", name_},
    };
    const std::string gsig = toBase64(cms::signJsonHybrid(gsigBody, client_->identity()));
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "text"},
        {"id", messageId},
        {"from", fingerprint()},
        {"sentAt", sentAt},
        {"text", text},
        {"dn", name_},
        {"group", {{"id", groupId}}},
        {"gsig", gsig},
    };
    if (!replyTo.empty()) {
        inner["replyTo"] = replyTo;
    }
    const bool ok = sendGroupContentToMember(
        groupId, memberFp, member->second, std::move(inner), outAttemptId);
    persistGroups();
    return ok;
}

bool Session::resendGroupRenameToMember(const std::string& groupId, const std::string& memberFp,
    const std::string& messageId, std::string* outAttemptId)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        throw std::runtime_error("unknown group: " + groupId);
    }
    Group& group = found->second;
    const auto member = group.members.find(memberFp);
    if (member == group.members.end()) {
        return false;  // no longer a member
    }
    const std::string& name = group.name;
    const std::int64_t sentAt = nowMillis();
    const nlohmann::json gsigBody = {
        {"type", "group.rename"},
        {"id", messageId},
        {"from", fingerprint()},
        {"groupId", groupId},
        {"sentAt", sentAt},
        {"text", name},
        {"dn", name_},
    };
    const std::string gsig = toBase64(cms::signJsonHybrid(gsigBody, client_->identity()));
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "group.rename"},
        {"id", messageId},
        {"from", fingerprint()},
        {"sentAt", sentAt},
        {"text", name},
        {"dn", name_},
        {"group", {{"id", groupId}}},
        {"gsig", gsig},
    };
    const bool ok = sendGroupContentToMember(
        groupId, memberFp, member->second, std::move(inner), outAttemptId);
    persistGroups();
    return ok;
}

bool Session::resendGroupAvatarToMember(const std::string& groupId, const std::string& memberFp,
    const std::string& messageId, std::string* outAttemptId)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        throw std::runtime_error("unknown group: " + groupId);
    }
    Group& group = found->second;
    const auto member = group.members.find(memberFp);
    if (member == group.members.end()) {
        return false;  // no longer a member
    }
    const std::int64_t sentAt = nowMillis();
    const nlohmann::json gsigBody = {
        {"type", "group.avatar"},
        {"id", messageId},
        {"from", fingerprint()},
        {"groupId", groupId},
        {"sentAt", sentAt},
        {"text", std::string()},
        {"dn", name_},
    };
    const std::string gsig = toBase64(cms::signJsonHybrid(gsigBody, client_->identity()));
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "group.avatar"},
        {"id", messageId},
        {"from", fingerprint()},
        {"sentAt", sentAt},
        {"dn", name_},
        {"avatar", {{"mime", group.avatarMime}, {"data", toBase64(group.avatar)}}},
        {"group", {{"id", groupId}}},
        {"gsig", gsig},
    };
    const bool ok = sendGroupContentToMember(
        groupId, memberFp, member->second, std::move(inner), outAttemptId);
    persistGroups();
    return ok;
}

bool Session::resendGroupRosterToMember(const std::string& groupId, const std::string& memberFp,
    const std::string& messageId, std::string* outAttemptId)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        throw std::runtime_error("unknown group: " + groupId);
    }
    Group& group = found->second;
    const auto member = group.members.find(memberFp);
    if (member == group.members.end()) {
        return false;  // no longer a member
    }
    const nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "group.roster"},
        {"id", messageId.empty() ? toHex(randomBytes(8)) : messageId},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"groupId", groupId},
        {"roster", signedRosterB64(groupId)},
    };
    try {
        sendToMemberContact(memberFp, member->second, inner, outAttemptId);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void Session::sendGroupEdit(
    const std::string& groupId, const std::string& refMessageId, const std::string& text)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        throw std::runtime_error("unknown group: " + groupId);
    }
    Group& group = found->second;
    // Same fan-out shape as a group text: one shared id + signature, sealed per
    // member. The gsig binds type/ref/text to us, so a member can only edit their
    // own messages (the recipient also scopes the edit to our authored copy).
    const std::string id = toHex(randomBytes(8));
    const std::int64_t sentAt = nowMillis();
    const nlohmann::json gsigBody = {
        {"type", "edit"},
        {"id", id},
        {"from", fingerprint()},
        {"groupId", groupId},
        {"sentAt", sentAt},
        {"ref", refMessageId},
        {"text", text},
        {"dn", name_},
    };
    const std::string gsig = toBase64(cms::signJsonHybrid(gsigBody, client_->identity()));
    for (const auto& [fp, member] : group.members) {
        nlohmann::json inner = {
            {"v", kMessageFormatVersion},
            {"type", "edit"},
            {"id", id},
            {"from", fingerprint()},
            {"sentAt", sentAt},
            {"ref", refMessageId},
            {"text", text},
            {"dn", name_},
            {"group", {{"id", groupId}}},
            {"gsig", gsig},
        };
        sendGroupContentToMember(groupId, fp, member, std::move(inner));
    }
    persistGroups();
}

void Session::storeGroupAvatar(
    const std::string& groupId, const Bytes& data, const std::string& mime)
{
    if (data.size() > kAvatarMaxBytes) {
        return;  // over the protocol cap: drop it rather than store an oversized blob
    }
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        return;
    }
    found->second.avatar = data;
    found->second.avatarMime = mime;
    persistSealedBlob("group-avatar-" + groupId, data);
}

Bytes Session::groupAvatar(const std::string& groupId) const
{
    const auto found = groups_.find(groupId);
    return found == groups_.end() ? Bytes() : found->second.avatar;
}

std::string Session::groupAvatarMime(const std::string& groupId) const
{
    const auto found = groups_.find(groupId);
    return found == groups_.end() ? std::string() : found->second.avatarMime;
}

Session::GroupFanout Session::setGroupAvatar(
    const std::string& groupId, const Bytes& data, const std::string& mime,
    const std::string& messageId)
{
    if (data.size() > kAvatarMaxBytes) {
        throw std::runtime_error("group photo exceeds the 500 KB protocol limit");
    }
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        throw std::runtime_error("unknown group: " + groupId);
    }
    Group& group = found->second;
    if (!group.iAmAdmin) {
        throw std::runtime_error("only a group admin can set the group photo");
    }
    storeGroupAvatar(groupId, data, mime);
    persistGroups();  // record the mime so open() reloads the blob

    // Broadcast a signed group.avatar so the photo appears in every member's chat
    // as a message from us. Identical shape to a group text (gsig over identifying
    // fields + our self-name) so the recipient authenticates the setter; the image
    // rides in the sealed inner payload. Fan out to every member with the
    // content-or-contact fallback, collecting per-member outcomes for Delivery.
    const std::string id = messageId.empty() ? toHex(randomBytes(8)) : messageId;
    const std::int64_t sentAt = nowMillis();
    const nlohmann::json gsigBody = {
        {"type", "group.avatar"},
        {"id", id},
        {"from", fingerprint()},
        {"groupId", groupId},
        {"sentAt", sentAt},
        {"text", std::string()},
        {"dn", name_},
    };
    const std::string gsig = toBase64(cms::signJsonHybrid(gsigBody, client_->identity()));
    GroupFanout coverage;
    coverage.total = static_cast<int>(group.members.size());
    for (const auto& [fp, member] : group.members) {
        nlohmann::json inner = {
            {"v", kMessageFormatVersion},
            {"type", "group.avatar"},
            {"id", id},
            {"from", fingerprint()},
            {"sentAt", sentAt},
            {"dn", name_},
            {"avatar", {{"mime", mime}, {"data", toBase64(data)}}},
            {"group", {{"id", groupId}}},
            {"gsig", gsig},
        };
        std::string attemptId;
        const bool ok = sendGroupContentToMember(groupId, fp, member, std::move(inner), &attemptId);
        coverage.members.push_back(MemberOutcome{fp, ok, attemptId});
        if (ok) {
            ++coverage.reached;
        }
    }
    persistGroups();
    return coverage;
}

Session::GroupFanout Session::setGroupName(
    const std::string& groupId, const std::string& name, const std::string& messageId)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        throw std::runtime_error("unknown group: " + groupId);
    }
    Group& group = found->second;
    if (!group.iAmAdmin) {
        throw std::runtime_error("only a group admin can rename the group");
    }
    group.name = name;
    group.epoch += 1;
    persistGroups();
    // The roster is the authoritative, epoch-protected name (so a stale roster
    // cannot revert it and late joiners learn the new name). It rides alongside the
    // notice; the recipient adopts the name from the notice itself, so the tracked
    // and Resend-able fan-out below is the visible rename notice.
    broadcastRoster(groupId);

    // A signed service message so every member shows a highlighted rename notice
    // (same fan-out shape as a group text; the name rides in `text`). Every member
    // is covered with the content-or-contact fallback so the per-member Delivery
    // tracking matches a text.
    const std::string id = messageId.empty() ? toHex(randomBytes(8)) : messageId;
    const std::int64_t sentAt = nowMillis();
    const nlohmann::json gsigBody = {
        {"type", "group.rename"},
        {"id", id},
        {"from", fingerprint()},
        {"groupId", groupId},
        {"sentAt", sentAt},
        {"text", name},
        {"dn", name_},
    };
    const std::string gsig = toBase64(cms::signJsonHybrid(gsigBody, client_->identity()));
    GroupFanout coverage;
    coverage.total = static_cast<int>(group.members.size());
    for (const auto& [fp, member] : group.members) {
        nlohmann::json inner = {
            {"v", kMessageFormatVersion},
            {"type", "group.rename"},
            {"id", id},
            {"from", fingerprint()},
            {"sentAt", sentAt},
            {"text", name},
            {"dn", name_},
            {"group", {{"id", groupId}}},
            {"gsig", gsig},
        };
        std::string attemptId;
        const bool ok = sendGroupContentToMember(groupId, fp, member, std::move(inner), &attemptId);
        coverage.members.push_back(MemberOutcome{fp, ok, attemptId});
        if (ok) {
            ++coverage.reached;
        }
    }
    persistGroups();
    return coverage;
}

void Session::rotateGroupPool(const std::string& groupId)
{
    Group& group = groups_.at(groupId);
    revokeGroupPool(group);
    broadcastGroupPool(groupId);  // issues a fresh pool to the current members
}

Session::GroupFanout Session::broadcastRoster(
    const std::string& groupId, const std::string& messageId)
{
    Group& group = groups_.at(groupId);
    const std::string roster = signedRosterB64(groupId);
    const nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "group.roster"},
        {"id", messageId.empty() ? toHex(randomBytes(8)) : messageId},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"groupId", groupId},
        {"roster", roster},
    };
    // Tokenless contact class so a critical roster update never fails on a
    // drained pool. Each member's outcome is collected so a roster-carried control
    // event (add/remove/admin) gets the same per-member Delivery tracking as a text.
    GroupFanout coverage;
    coverage.total = static_cast<int>(group.members.size());
    for (const auto& [fp, member] : group.members) {
        std::string attemptId;
        bool ok = false;
        try {
            sendToMemberContact(fp, member, inner, &attemptId);
            ok = true;
        } catch (const std::exception&) {
            // Best-effort per member: a single unreachable member never aborts the rest.
        }
        coverage.members.push_back(MemberOutcome{fp, ok, attemptId});
        if (ok) {
            ++coverage.reached;
        }
    }
    return coverage;
}

bool Session::isGroupAdmin(const std::string& groupId) const
{
    const auto found = groups_.find(groupId);
    return found != groups_.end() && found->second.iAmAdmin;
}

bool Session::isGroupMemberAdmin(
    const std::string& groupId, const std::string& memberFingerprint) const
{
    const auto group = groups_.find(groupId);
    if (group == groups_.end()) {
        return false;
    }
    const auto member = group->second.members.find(memberFingerprint);
    return member != group->second.members.end() && member->second.admin;
}

Session::GroupFanout Session::addGroupMembers(const std::string& groupId,
    const std::vector<std::string>& memberFingerprints, const std::string& messageId)
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
        return {};
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
            {"sentAt", nowMillis()},
            {"groupId", groupId},
            {"name", group.name},
            {"roster", roster},
        };
        sendContent(fp, std::move(invite));
    }
    // The roster broadcast to the existing members is the tracked, Resend-able
    // fan-out (the "you added X" notice's delivery); the new members also get it.
    GroupFanout coverage = broadcastRoster(groupId, messageId);
    broadcastGroupPool(groupId);  // hand the new members our pool
    persistGroups();
    return coverage;
}

Session::GroupFanout Session::removeGroupMember(
    const std::string& groupId, const std::string& memberFingerprint, const std::string& messageId)
{
    Group& group = groups_.at(groupId);
    if (!group.iAmAdmin) {
        throw std::runtime_error("only a group admin can remove members");
    }
    if (group.members.erase(memberFingerprint) == 0) {
        return {};
    }
    group.epoch += 1;
    persistGroups();
    GroupFanout coverage = broadcastRoster(groupId, messageId);  // remaining members get the roster
    rotateGroupPool(groupId);   // our old pool (which the removed member holds) stops working
    persistGroups();
    return coverage;
}

Session::GroupFanout Session::setGroupAdmin(const std::string& groupId,
    const std::string& memberFingerprint, bool admin, const std::string& messageId)
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
    return broadcastRoster(groupId, messageId);
}

void Session::leaveGroup(const std::string& groupId)
{
    const auto found = groups_.find(groupId);
    if (found == groups_.end()) {
        return;
    }
    Group& group = found->second;
    // Sign the departure so a member cannot forge another member's leave (the
    // recipient verifies the signature authorizes removing exactly the signer).
    const nlohmann::json leaveBody
        = {{"type", "group.leave"}, {"groupId", groupId}, {"from", fingerprint()}};
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "group.leave"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"groupId", groupId},
        {"gsig", toBase64(cms::signJsonHybrid(leaveBody, client_->identity()))},
    };
    for (const auto& [fp, member] : group.members) {
        try {
            sendToMemberContact(fp, member, inner);
        } catch (const std::exception&) {
            // best effort
        }
    }
    revokeGroupPool(group);  // our pool stops working once we are gone
    // Drop the sealed group photo blob too, so nothing of the group lingers.
    std::error_code ignore;
    fs::remove(profileDir_ / ("group-avatar-" + groupId), ignore);
    groups_.erase(found);
    // Tombstone the id so a late item (one a member sent before our revocation
    // reached them) can never resurrect this chat on a later sync.
    leftGroups_.insert(groupId);
    persistGroups();
    persistLeftGroups();
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

std::string Session::groupMemberDisplayName(
    const std::string& groupId, const std::string& memberFingerprint) const
{
    const auto group = groups_.find(groupId);
    if (group == groups_.end()) {
        return std::string();
    }
    const auto member = group->second.members.find(memberFingerprint);
    return member == group->second.members.end() ? std::string() : member->second.displayName;
}

std::string Session::groupMemberProvisionalName(
    const std::string& groupId, const std::string& memberFingerprint) const
{
    const auto group = groups_.find(groupId);
    if (group == groups_.end()) {
        return std::string();
    }
    const auto member = group->second.members.find(memberFingerprint);
    return member == group->second.members.end() ? std::string() : member->second.provisionalName;
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
                {"displayName", member.displayName},
                {"provisionalName", member.provisionalName},
            };
        }
        out[groupId] = {
            {"name", group.name},
            {"epoch", group.epoch},
            {"iAmAdmin", group.iAmAdmin},
            {"myPoolHashes", group.myPoolHashes},
            {"avatarMime", group.avatarMime},
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
        writeFileText(profileDir_ / "groups.json", std::string(sealed.begin(), sealed.end()));
        return;
    }
    writeFileText(profileDir_ / "groups.json", stored.dump(2));
}

void Session::persistLeftGroups() const
{
    const nlohmann::json stored(leftGroups_);  // a JSON array of group ids
    if (encrypted_) {
        const std::string text = stored.dump();
        const Bytes sealed = cms::sealWithPassword(Bytes(text.begin(), text.end()), passphrase_);
        writeFileText(profileDir_ / "groups-left.json", std::string(sealed.begin(), sealed.end()));
        return;
    }
    writeFileText(profileDir_ / "groups-left.json", stored.dump(2));
}

void Session::loadLeftGroups()
{
    const fs::path path = profileDir_ / "groups-left.json";
    if (!fs::exists(path)) {
        return;
    }
    const std::string raw = readFileText(path);
    const nlohmann::json stored = encrypted_
        ? nlohmann::json::parse(cms::unsealWithPassword(Bytes(raw.begin(), raw.end()), passphrase_))
        : nlohmann::json::parse(raw);
    leftGroups_ = stored.get<std::set<std::string>>();
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
    // Advertise our profile name so the contact can adopt it as our display name.
    descriptor.name = name_;
    return encodeDescriptor(descriptor);
}

void Session::exportProfile(const fs::path& outFile, const std::string& password) const
{
    const nlohmann::json meta = nlohmann::json::parse(readFileText(profileDir_ / "meta.json"));
    // Use the in-memory contacts: the on-disk file may be sealed, and the
    // bundle carries them in the clear (the bundle password is the protection).
    const nlohmann::json contacts = contactsToJson();

    // The keys are re-serialized unencrypted inside the bundle; the password
    // protects the bundle as a whole, decoupling the export from whatever
    // at-rest passphrase this profile directory happens to use.
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

void Session::importProfile(const fs::path& bundleFile, const fs::path& profileDir,
    const std::string& password, const std::string& atRestPassphrase)
{
    const std::string sealedText = readFileText(bundleFile);
    const Bytes plain
        = cms::unsealWithPassword(Bytes(sealedText.begin(), sealedText.end()), password);
    const nlohmann::json bundle = nlohmann::json::parse(plain.begin(), plain.end());

    fs::create_directories(profileDir);

    // Round-trip the keys through the crypto types so the imported PEMs adopt
    // the chosen at-rest scheme (encrypted iff a passphrase is given).
    const Identity identity = Identity::fromPrivatePem(bundle.at("identityPem").get<std::string>());
    const Key sealing = Key::fromPrivatePem(bundle.at("sealingPem").get<std::string>());
    writeFileText(profileDir / "identity.pem", identity.privatePem(atRestPassphrase));
    writeFileText(profileDir / "sealing.pem", sealing.privatePem(atRestPassphrase));

    nlohmann::json meta = bundle.at("meta");
    meta["encrypted"] = !atRestPassphrase.empty();
    writeFileText(profileDir / "meta.json", meta.dump(2));

    // Match the contacts file to the chosen at-rest scheme (sealed iff a
    // passphrase is given), mirroring the keys above.
    const nlohmann::json contacts = bundle.at("contacts");
    if (!atRestPassphrase.empty()) {
        const std::string text = contacts.dump();
        const Bytes sealed
            = cms::sealWithPassword(Bytes(text.begin(), text.end()), atRestPassphrase);
        writeFileText(profileDir / "contacts.json", std::string(sealed.begin(), sealed.end()));
    } else {
        writeFileText(profileDir / "contacts.json", contacts.dump(2));
    }
}

}  // namespace bazarish::client
