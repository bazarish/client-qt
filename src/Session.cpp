// Bazarish project (c) 2026
#include "Session.hpp"

#include "FederationFetch.hpp"
#include "I2pKeys.hpp"
#include "I2pRouter.hpp"

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

// How long a call rings before it self-resolves: an unanswered outgoing call
// becomes "no answer", an unanswered incoming one "missed" - so a ringing call
// never blocks the UI waiting forever.
constexpr std::int64_t kRingTimeoutMs = 60000;

// Inner end-to-end payload format version (see docs Messages.md).
constexpr int kMessageFormatVersion = 1;

// How long an offline transient delegated to the serving server stays valid.
// Kept short so the operator only ever holds a time-boxed capability; the client
// re-issues a fresh one well before it lapses (see refreshI2pTransientIfDue).
constexpr std::int64_t kI2pTransientValiditySeconds = 7 * 24 * 3600;

// Host form of a standard-LeaseSet I2P address (the per-user destination).
constexpr const char* kI2pHostSuffix = ".b32.i2p";

// Applied to every profile opened afterwards (the CLI's BAZARISH_ALLOW_CLEARNET).
std::atomic<bool> g_allowClearnetDefault{false};

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
// How long a sender keeps a one-time destination up for one request. Long enough
// for a slow I2P transfer with reconnects, short enough that an abandoned request
// does not pin tunnels forever.
constexpr int kServeWindowSeconds = 30 * 60;

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
    const fs::path dataDir = profileDir_.parent_path() / "i2p";
    // On a first-ever start, take the netDb from our own server over the clearnet
    // facade rather than announcing an I2P bootstrap to a public reseed host.
    seedRouterOnce(dataDir, [this]() { return client_->fetchReseed(); });
    return sharedI2pRouter(dataDir);
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
    session.client_->setDestinationOwner(session.destinationOwner());
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
    session.client_->setI2pProven(meta.value("i2pProven", false));
    session.client_->setAllowClearnet(
        meta.value("allowClearnet", false) || g_allowClearnetDefault.load());
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
    session.client_->setDestinationOwner(session.destinationOwner());
    session.loadSentFiles();

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
    client_->setDestinationOwner(destinationOwner());
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
        // Sticky I2P: once this profile has reached its server over I2P it keeps
        // refusing clearnet across restarts, unless the user allowed it again.
        {"i2pProven", client_->i2pProven()},
        {"allowClearnet", client_->allowClearnet()},
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

void Session::setAllowClearnetDefault(const bool allow)
{
    g_allowClearnetDefault.store(allow);
}

void Session::setAllowClearnet(const bool allow)
{
    client_->setAllowClearnet(allow);
    persistMeta();
}

bool Session::allowClearnet() const
{
    return client_->allowClearnet();
}

void Session::subscribe(const std::int64_t days)
{
    const std::int64_t now = nowSeconds();
    const std::int64_t notAfter = now + days * 24 * 3600;
    // Publish our sealing key as a prekey so contacts can encrypt their very
    // first message to us before any token exchange.
    const SubscribeResult result
        = client_->subscribe(now, notAfter, sealingKey_.publicDer(), ownRoutingHost());
    // Reported here, not on entry: the call above is what brings the transport
    // up, so its own milestones (reseed, router, dial) come first.
    reportConnectProgress(70, "Subscribed; registering this device");
    storeSubscription(result);
    client_->registerThisClient();

    // Every account routes through a destination of its own, so mint the master
    // if this profile has none. The delegation can only be handed over once the
    // account exists, which is what the subscribe above created - hence the
    // second, routing-carrying certificate published right after it.
    ensureI2pDestination();
    reportConnectProgress(85, "Publishing your own destination");
    try {
        publishRouting();
    } catch (const ApiError& error) {
        // A moderated server withholds the destination until an operator
        // approves the account. That is the one refusal that is not a failure:
        // the subscription stands and the routing is published by a later
        // publishRouting() call, once approved.
        if (error.code != ErrorCode::eAccountPendingApproval) {
            throw;
        }
        bazarish::log::info("account awaiting operator approval: no routing published yet");
    }
}

void Session::publishRouting()
{
    if (!hasI2pDestination()) {
        throw std::runtime_error("no user-owned I2P destination to publish");
    }
    if (subscriptionCertB64_.empty()) {
        throw std::runtime_error("not subscribed: nothing to publish routing into");
    }
    const SubscriptionCertificate held
        = SubscriptionCertificate::verify(fromBase64(subscriptionCertB64_));
    // The transient is a time-boxed capability that lets the server operate our
    // destination; it never outlives the subscription it belongs to.
    renewI2pTransient(held.notAfter);
    reportConnectProgress(88, "Delegating your destination to the server");
    client_->sendI2pTransient(i2pTransientBase64(), held.notAfter);
    reportConnectProgress(92, "Publishing your contact card");
    // Re-issue the card inside the term already held: same window, so the
    // service node treats it as a re-publish and grants nothing.
    storeSubscription(client_->renew(
        nowSeconds(), held.notAfter, sealingKey_.publicDer(), ownRoutingHost()));
    // Hand the master to this account's other devices so they keep the same
    // address and can re-issue transients. Best effort: our own routing is
    // published either way, and the sync needs it to be.
    reportConnectProgress(96, "Syncing your address to your other devices");
    try {
        syncI2pMasterToSelf();
    } catch (const std::exception& error) {
        bazarish::log::info("master not synced to this account's other devices: {}", error.what());
    }
}

void Session::storeSubscription(const SubscribeResult& result)
{
    subscriptionCertB64_ = toBase64(result.subscriptionCertDer);
    myDest_ = result.dest;
    myServingKeyB64_
        = result.servingSealingKeyDer.empty() ? std::string() : toBase64(result.servingSealingKeyDer);
    persistMeta();
}

std::string Session::ownRoutingHost() const
{
    return i2pAddress_.empty() ? std::string() : i2pAddress_ + kI2pHostSuffix;
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

void Session::disableI2pDest()
{
    // Revoking is an empty delegation: the server tears the destination down and
    // holds nothing. The master stays in the profile, so publishing again later
    // restores the same address.
    client_->sendI2pTransient(std::string(), 0);
    i2pTransient_.clear();
    // fs::remove returns false (no throw) when the file is already absent.
    fs::remove(profileDir_ / "i2p-transient.dat");
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
    if (!status.approved()) {
        return false;  // no account here, or not approved yet: no destination to keep alive
    }
    // Poll-before-issue: the status read above is the check. If the server still
    // holds a transient comfortably in date, another of the user's devices has
    // already renewed it, so this device stands down.
    if (status.transientExpires != 0 && status.transientExpires - now > leadSeconds) {
        return false;
    }
    const std::int64_t expiresUnix = now + kI2pTransientValiditySeconds;
    renewI2pTransient(expiresUnix);
    client_->sendI2pTransient(i2pTransientBase64(), expiresUnix);
    return true;
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

std::string Session::issueOneToken()
{
    const Bytes token = generateDeliveryToken();
    client_->registerTokenHashes({deliveryTokenHash(token)});
    return toBase64(token);
}

bool Session::deliver(const std::string& toDest, const Key& servingSealingKey,
    const std::string& kind, const std::string& mailbox, const std::optional<Bytes>& token,
    const Bytes& payload, const std::function<void()>& onAcceptedByOwnServer,
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
    // Bounded by the clock, not by a poll count: over I2P one poll is seconds,
    // not the 100 ms a request count silently assumes, and the "bounded" window
    // becomes minutes of a caller waiting on a best-effort outcome.
    constexpr int kOutcomeWaitSeconds = 15;
    constexpr int kOutcomePollMillis = 100;
    const std::chrono::steady_clock::time_point deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(kOutcomeWaitSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            const SendStatus status = client_->pollSend(attemptId);
            if (status.status == "delivered") {
                return true;  // recipient server stored it: yellow
            }
            if (status.status == "failed") {
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
        std::this_thread::sleep_for(std::chrono::milliseconds(kOutcomePollMillis));
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
                    return federationFetchOverI2p(
                        *router, toDest, op, sealed, transferPrivacy_, destinationOwner());
                } catch (const std::exception& error) {
                    // Direct dial failed; fall back to the server proxy below.
                    bazarish::log::debug("direct fetch failed, relaying: {}", error.what());
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
    ctx.blobFetchPrivacy = transferPrivacy_;
    ctx.destinationOwner = destinationOwner();
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
                        return federationFetchOverI2p(*router, toDest, op, sealed,
                            context.blobFetchPrivacy, context.destinationOwner);
                    } catch (const std::exception& error) {
                        // Direct dial failed; fall back to the server relay below.
                        bazarish::log::debug("direct fetch failed, relaying: {}", error.what());
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
        // The card can only carry routing once the server operates this account's
        // destination, which happens when the delegation is published - not at
        // subscribe time. Name that, so the caller can offer the fix.
        throw std::runtime_error(
            "your destination is not published yet, so an invite would not be reachable");
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
        // friend in their contacts from the start - mirroring how we learn their
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
        // Our own display name, so the requester can name us in their contacts too -
        // the reverse direction of the requester's `dn` on the contact request. A
        // one-time seed (only when they hold no name for us yet), so names are
        // symmetric after a first exchange.
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
    std::string* outAttemptId, const std::string& replyTo)
{
    const std::string id = messageId.empty() ? toHex(randomBytes(8)) : messageId;
    // Only metadata travels. The digest is over the plaintext, so the recipient
    // can check that what it finally holds is what was announced, independently
    // of how many transfer attempts it took.
    const std::uint64_t size = fs::file_size(path);
    const std::string digest = toHex(sha256File(path));
    sentFiles_[id] = SentFile{path, digest, size};
    persistSentFiles();

    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "file"},
        {"id", id},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"file",
            {
                {"name", path.filename().string()},
                {"size", size},
                {"sha256", digest},
                {"mime", guessMime(path)},
            }},
    };
    if (!replyTo.empty()) {
        inner["replyTo"] = replyTo;
    }
    return sendContent(peerFingerprint, std::move(inner), onAcceptedByOwnServer, outAttemptId);
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

void Session::setTransferHandler(TransferEventFn handler)
{
    const std::lock_guard<std::mutex> lock(transfers_->mutex);
    transfers_->onEvent = std::move(handler);
}

void Session::emitTransfer(const std::string& messageId, const TransferState state,
    const std::uint64_t bytes, const std::uint64_t total, const std::string& error)
{
    TransferEventFn handler;
    {
        const std::lock_guard<std::mutex> lock(transfers_->mutex);
        handler = transfers_->onEvent;
    }
    if (handler) {
        handler(TransferEvent{messageId, state, bytes, total, error});
    }
}

void Session::requestFile(
    const std::string& peerFingerprint, const std::string& messageId, const fs::path& dest)
{
    {
        const std::lock_guard<std::mutex> lock(transfers_->mutex);
        transfers_->pending[messageId]
            = PendingTransfer{dest, std::make_shared<std::atomic<bool>>(false)};
    }
    emitTransfer(messageId, TransferState::eRequested, 0, 0);
    nlohmann::json inner = {
        {"v", kMessageFormatVersion},
        {"type", "file.request"},
        {"id", toHex(randomBytes(8))},
        {"from", fingerprint()},
        {"sentAt", nowMillis()},
        {"fileId", messageId},
    };
    sendContent(peerFingerprint, std::move(inner));
}

void Session::cancelTransfer(const std::string& messageId)
{
    const std::lock_guard<std::mutex> lock(transfers_->mutex);
    const auto found = transfers_->pending.find(messageId);
    if (found != transfers_->pending.end()) {
        found->second.cancel->store(true);
        transfers_->pending.erase(found);
    }
}

void Session::unsend(const std::string& messageId)
{
    // Nothing was ever copied off this machine, so unsending is just forgetting:
    // a later request is answered "no longer available".
    if (sentFiles_.erase(messageId) > 0) {
        persistSentFiles();
    }
}

void Session::setTransferPrivacy(const bazarish::i2p::Privacy privacy)
{
    transferPrivacy_ = privacy;
}

void Session::serveRequestedFile(const std::string& peerFingerprint, const std::string& fileId)
{
    const auto found = sentFiles_.find(fileId);
    if (found == sentFiles_.end() || !fs::exists(found->second.path)) {
        // Either we never announced it or the user moved the file: say so instead
        // of leaving the recipient waiting on a transfer that can never start.
        nlohmann::json inner = {
            {"v", kMessageFormatVersion},
            {"type", "file.unavailable"},
            {"id", toHex(randomBytes(8))},
            {"from", fingerprint()},
            {"sentAt", nowMillis()},
            {"fileId", fileId},
        };
        sendContent(peerFingerprint, std::move(inner));
        return;
    }
    const fs::path source = found->second.path;
    const fs::path ciphertextPath
        = profileDir_ / ("file-serve-" + toHex(randomBytes(8)) + ".tmp");

    std::thread([this, peerFingerprint, fileId, source, ciphertextPath]() {
        try {
            const PreparedFile prepared = prepareFile(source, ciphertextPath);
            bazarish::i2p::EndpointConfig config{bazarish::i2p::Keys::generate()};
            config.privacy = transferPrivacy_;
            config.tunnelQuantity = 2;
            config.label = "File upload";
            config.owner = destinationOwner();
            const std::shared_ptr<bazarish::i2p::Endpoint> endpoint
                = i2pRouter().createEndpoint(config);
            if (!endpoint->waitReady(std::chrono::seconds(180))) {
                throw std::runtime_error("could not publish a one-time destination");
            }

            FileOffer offer;
            offer.fileId = fileId;
            offer.host = endpoint->routingHost();
            offer.key = prepared.key;
            offer.sha256 = prepared.sha256;
            offer.size = prepared.size;
            nlohmann::json inner = {
                {"v", kMessageFormatVersion},
                {"type", "file.offer"},
                {"id", toHex(randomBytes(8))},
                {"from", fingerprint()},
                {"sentAt", nowMillis()},
                {"offer", fileOfferToJson(offer)},
            };
            sendContent(peerFingerprint, std::move(inner));

            serveFile(*endpoint, ciphertextPath, std::chrono::seconds(kServeWindowSeconds),
                [this, fileId](const std::uint64_t sent, const std::uint64_t total) {
                    emitTransfer(fileId, TransferState::eRunning, sent, total);
                });
            emitTransfer(fileId, TransferState::eDone, 0, 0);
        } catch (const std::exception& error) {
            emitTransfer(fileId, TransferState::eFailed, 0, 0, error.what());
        }
        std::error_code ec;
        fs::remove(ciphertextPath, ec);
    }).detach();
}

void Session::startAnnouncedFetch(const FileOffer& offer)
{
    fs::path dest;
    std::shared_ptr<std::atomic<bool>> cancel;
    {
        const std::lock_guard<std::mutex> lock(transfers_->mutex);
        const auto found = transfers_->pending.find(offer.fileId);
        if (found == transfers_->pending.end()) {
            return;  // an offer for something we never asked for
        }
        dest = found->second.dest;
        cancel = found->second.cancel;
    }

    std::thread([this, offer, dest, cancel]() {
        try {
            fetchFileOverI2p(i2pRouter(), offer, dest, transferPrivacy_,
                [this, &offer](const std::uint64_t got, const std::uint64_t total) {
                    emitTransfer(offer.fileId, TransferState::eRunning, got, total);
                },
                cancel.get(), destinationOwner());
            emitTransfer(offer.fileId, TransferState::eDone, offer.size, offer.size);
        } catch (const std::exception& error) {
            emitTransfer(offer.fileId, TransferState::eFailed, 0, 0, error.what());
        }
        const std::lock_guard<std::mutex> lock(transfers_->mutex);
        transfers_->pending.erase(offer.fileId);
    }).detach();
}

bool Session::sendContent(const std::string& peerFingerprint, nlohmann::json inner,
    const std::function<void()>& onAcceptedByOwnServer, std::string* outAttemptId,
    bool waitForOutcome, bool establishOnFirstReply, const std::string& overrideToken)
{
    const auto found = contacts_.find(peerFingerprint);
    if (found == contacts_.end()) {
        throw std::runtime_error("unknown contact: " + peerFingerprint);
    }
    Contact& contact = found->second;
    if (contact.sealingPublicB64.empty() || contact.servingSealingB64.empty()) {
        throw std::runtime_error("contact not established yet: " + peerFingerprint);
    }
    // A caller-supplied token is spent instead of one from our stash (a prepaid
    // token-refill reply), so an empty stash is not an error on that path.
    const bool useOverrideToken = !overrideToken.empty();
    if (!useOverrideToken && contact.sendTokens.empty()) {
        // Out of one-time delivery tokens for this peer: their stash refills when
        // they come back online (the low-stash signal we sent earlier prompts it),
        // so this is a recoverable "resend later", not a permanent failure. Surfaced
        // verbatim on the failed bubble, so keep it human and free of the raw
        // fingerprint.
        throw std::runtime_error("Out of delivery tokens for this contact - resend once "
                                 "they are back online and refill your sending capacity.");
    }
    // Captured before the bootstrap block below: when false here, this very send
    // is our first reply to the peer - the moment we accept/establish the dialog.
    const bool wasIssuedToThem = contact.issuedToThem;

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

    // After spending this token our stash for the peer would be this small; ask
    // them to refill us before it hits zero (Contacts.md), and prepay their reply
    // with one fresh token so the refill is deliverable even if they hold none of
    // ours. Skipped when we spend a caller-supplied token (the refill reply itself).
    if (!useOverrideToken && contact.sendTokens.size() - 1 <= kRefillThreshold) {
        inner["lowStash"] = true;
        inner["refillToken"] = issueOneToken();
    }

    const std::string innerText = inner.dump();
    const Key peerSealing = Key::fromPublicDer(fromBase64(contact.sealingPublicB64));
    const Bytes payload = cms::seal(Bytes(innerText.begin(), innerText.end()), peerSealing);
    const Key peerServingKey = Key::fromPublicDer(fromBase64(contact.servingSealingB64));

    const std::string token = useOverrideToken ? overrideToken : contact.sendTokens.back();
    const bool delivered = deliver(contact.dest, peerServingKey, "content", peerFingerprint,
        fromBase64(token), payload, onAcceptedByOwnServer, outAttemptId, waitForOutcome);

    // Spend the token: it is now committed to this message (consumed by the
    // recipient on delivery, or in flight while the server keeps delivering).
    // deliver() throws on a terminal failure, so a thrown send never spends one.
    // A caller-supplied token is not from our stash, so the stash is left untouched.
    if (!useOverrideToken) {
        contact.sendTokens.pop_back();
        persistContacts();
    }

    // If this send is the first reply that just established the reverse direction
    // (we accepted their request), share our avatar now - consent-gated, exactly
    // the "reply to the friend request" the spec ties avatar exchange to.
    if (!wasIssuedToThem) {
        maybeSendAvatarToContact(peerFingerprint);
    }
    return delivered;
}


void Session::loadSentFiles()
{
    const fs::path path = profileDir_ / "sent-files.json";
    if (!fs::exists(path)) {
        return;
    }
    const std::string raw = readFileText(path);
    const nlohmann::json stored = encrypted_
        ? nlohmann::json::parse(cms::unsealWithPassword(Bytes(raw.begin(), raw.end()), passphrase_))
        : nlohmann::json::parse(raw);
    for (const auto& [id, entry] : stored.items()) {
        sentFiles_[id] = SentFile{fs::path(entry.at("path").get<std::string>()),
            entry.value("sha256", std::string()), entry.value("size", std::uint64_t{0})};
    }
}

void Session::persistSentFiles() const
{
    nlohmann::json stored = nlohmann::json::object();
    for (const auto& [id, file] : sentFiles_) {
        stored[id] = {
            {"path", file.path.string()},
            {"sha256", file.sha256},
            {"size", file.size},
        };
    }
    const std::string text = stored.dump();
    persistSealedBlob("sent-files.json", Bytes(text.begin(), text.end()));
}


std::vector<IncomingMessage> Session::sync(bool autoAckSurfaced)
{
    std::vector<IncomingMessage> result;
    // Peers whose stash of our tokens is running low and who asked for a
    // refill; topped up after the fetch loop so we never write mid-iteration.
    std::set<std::string> refillPeers;
    // A fresh token each low-stash requester embedded to prepay our refill reply, by
    // peer fingerprint: we spend exactly it to deliver the batch (so the reply lands
    // even when we hold none of their tokens) and never fold it into our stash.
    std::map<std::string, std::string> prepaidRefillTokens;
    // Peers that carried a bootstrap this sync (a contact request, or - for one we
    // requested - their acceptance): we push our avatar to the established ones
    // after the loop, same "never write mid-iteration" rule.
    std::set<std::string> establishedPeers;
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
            // directions (requester names the accepter and vice versa). Only seeds an
            // empty name (never overwrites a name we already hold or the user later
            // set), so a peer can never rename themselves in our contacts after the
            // fact.
            if (type == "contact.request" || type == "contact.accept") {
                const std::string dn = body.value("dn", std::string());
                Contact& peer = contacts_[message.fromFingerprint];
                if (!dn.empty() && peer.displayName.empty()) {
                    peer.displayName = dn;
                }
            }

            // The peer is low on our tokens and asked to be refilled; their request
            // prepays our reply with a fresh token, held only for that refill.
            if (body.value("lowStash", false)) {
                refillPeers.insert(message.fromFingerprint);
            }
            if (body.contains("refillToken")) {
                prepaidRefillTokens[message.fromFingerprint]
                    = body.at("refillToken").get<std::string>();
            }

            // Content dispatch. An unknown type is still acked and surfaced (not
            // dropped) so a newer client could render it; see docs Messages.md.
            if (type == "text" || type == "contact.request") {
                message.contentType = type;
                message.text = body.value("text", std::string());
            } else if (type == "file" || type == "photo" || type == "audio" || type == "voice") {
                // An announcement, not a delivery: the bytes are still on the
                // sender's disk until we ask for them.
                message.contentType = type;
                const nlohmann::json& file = body.at("file");
                message.attachmentRef = file.value("sha256", std::string());
                message.attachmentName = file.value("name", std::string());
                message.attachmentMime = file.value("mime", std::string());
                message.attachmentSize = file.value("size", std::uint64_t{0});
            } else if (type == "file.request") {
                // Silent: a contact wants a file we announced.
                message.contentType = type;
                serveRequestedFile(message.fromFingerprint, body.value("fileId", std::string()));
            } else if (type == "file.offer") {
                // Silent: the sender is up and serving; start pulling.
                message.contentType = type;
                try {
                    startAnnouncedFetch(fileOfferFromJson(body.at("offer")));
                } catch (const std::exception& error) {
                    // Malformed offer: the transfer simply never starts.
                    bazarish::log::warn("file offer ignored: {}", error.what());
                }
            } else if (type == "file.unavailable") {
                message.contentType = type;
                const std::string fileId = body.value("fileId", std::string());
                {
                    const std::lock_guard<std::mutex> lock(transfers_->mutex);
                    transfers_->pending.erase(fileId);
                }
                emitTransfer(fileId, TransferState::eFailed, 0, 0,
                    "the sender no longer has this file");
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
                // by our own server (the send attempt), not by this receipt.
                message.contentType = type;
                message.refId = body.value("ref", std::string());
            } else if (type == "reaction") {
                // A reaction to a message: `ref` is the target message id, `text` the
                // emoji (empty removes the reactor's reaction). The reactor is the
                // message's verified `from`. The UI records it against the target
                // message and never renders it as a chat bubble.
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
                    } catch (const std::exception& error) {
                        // Malformed, wrong key type, or already configured. This device
                        // then keeps an address of its own, which is worth knowing.
                        bazarish::log::warn("master key from another device rejected: {}",
                            error.what());
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
                } catch (const std::exception& error) {
                    // Malformed avatar payload: ignore.
                    bazarish::log::warn("contact avatar ignored: {}", error.what());
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
                    } catch (const std::exception& error) {
                        // Malformed avatar payload: ignore.
                        bazarish::log::warn("own avatar from another device ignored: {}",
                            error.what());
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
            } else if (type == "chat.clear") {
                // The peer asked to clear our whole conversation with them; the GUI
                // wipes its transcript on receipt. No core state changes here.
                message.contentType = type;
            } else if (type == "contact.accept") {
                // The peer agreed to our contact request: their descriptor + reply
                // tokens already rode in the bootstrap block above, so we are now a
                // mutual contact. Surfaced as a system note by the UI.
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
            // A reply reference (the replied-to message's protocol id) may ride on
            // any content message; preserve it so the UI can render a quote/link.
            if (body.contains("replyTo")) {
                message.replyTo = body.value("replyTo", std::string());
            }

            // Ack now (CLI/bots), or defer to the caller (GUI). Deferring acks a
            // surfaced item only after the client has durably stored it, so a
            // crash/restart between fetch and store never loses it. Re-processing on
            // a pre-ack re-fetch is safe - applyBootstrap dedups tokens and the GUI
            // dedups by messageId.
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
            const auto pre = prepaidRefillTokens.find(peer);
            sendTokenRefill(
                peer, pre != prepaidRefillTokens.end() ? pre->second : std::string());
        } catch (const std::exception& error) {
            bazarish::log::warn("sync: token refill failed: {}", error.what());
        }
    }
    return result;
}

void Session::ackPending(const std::string& pendingId)
{
    if (!pendingId.empty()) {
        client_->ack(pendingId);
    }
}

void Session::sendTokenRefill(const std::string& peerFingerprint, const std::string& prepaidToken)
{
    const auto found = contacts_.find(peerFingerprint);
    if (found == contacts_.end()) {
        return;
    }
    const Contact& contact = found->second;
    // Never auto-refill a peer whose incoming request we have not accepted yet
    // (issuedToThem still false): that would attach our bootstrap and silently
    // establish the contact. We also need the peer's descriptor. With a prepaidToken
    // (embedded in their low-stash request) the reply spends exactly it, so it lands
    // even when we hold none of their tokens; without one we spend one of ours, so an
    // empty stash means the peer's own refill of us must arrive first.
    if (!contact.issuedToThem || contact.sealingPublicB64.empty()
        || contact.servingSealingB64.empty()
        || (prepaidToken.empty() && contact.sendTokens.empty())) {
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
    sendContent(peerFingerprint, std::move(inner), {}, nullptr, false, true, prepaidToken);
}

// ============================ Audio calls ============================

void Session::setAudioBackend(AudioSourceFactory sourceFactory, AudioSinkFactory sinkFactory)
{
    audioSourceFactory_ = std::move(sourceFactory);
    audioSinkFactory_ = std::move(sinkFactory);
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
        bazarish::i2p::Privacy::eMinimal, bazarish::i2p::kDefaultTunnelQuantity, true,
        "Call media", destinationOwner()});
}

void Session::startCallMedia()
{
    call_.transport = std::make_unique<I2pCallTransport>(*call_.dgram, call_.peerMediaDest);
    std::unique_ptr<AudioSource> audioSource
        = audioSourceFactory_ ? audioSourceFactory_() : std::make_unique<SineAudioSource>();
    std::unique_ptr<AudioSink> audioSink
        = audioSinkFactory_ ? audioSinkFactory_() : std::make_unique<CapturingAudioSink>();
    call_.media = std::make_unique<CallMedia>(*call_.transport, std::move(audioSource),
        std::move(audioSink), call_.mediaKey,
        call_.initiator ? CallRole::eCaller : CallRole::eCallee);
    call_.media->setMuted(call_.muted);
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
    call_.muted = false;
    call_.startedAtMs = 0;
    call_.connectedAtMs = 0;
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

void Session::startCall(const std::string& peerFingerprint)
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
            {"media", "audio"},
            {"codec", "opus"},
            {"dest", dgram->routingHost()},
            {"key", toBase64(mediaKey)},
        });
    call_.state = CallState::eOutgoing;
    call_.callId = callId;
    call_.peerFingerprint = peerFingerprint;
    call_.mediaKey = mediaKey;
    call_.initiator = true;
    call_.muted = false;
    call_.startedAtMs = nowMillis();
    call_.dgram = std::move(dgram);
}

void Session::startAudioCall(const std::string& peerFingerprint)
{
    startCall(peerFingerprint);
}

void Session::acceptCall(const std::string& callId)
{
    if (call_.state != CallState::eIncoming || call_.callId != callId) {
        throw std::runtime_error("no matching incoming call");
    }
    auto dgram = openCallMediaSession();
    sendCallSignal(call_.peerFingerprint, "call.accept",
        {{"callId", callId}, {"media", "audio"},
            {"dest", dgram->routingHost()}});
    call_.dgram = std::move(dgram);
    call_.state = CallState::eActive;
    call_.connectedAtMs = nowMillis();
    startCallMedia();
}

void Session::declineCall(const std::string& callId)
{
    if (call_.state != CallState::eIncoming || call_.callId != callId) {
        throw std::runtime_error("no matching incoming call");
    }
    const std::string peer = call_.peerFingerprint;
    logCompletedCall(CallOutcome::eDeclined);
    clearCall();
    try {
        sendCallSignal(peer, "call.decline", {{"callId", callId}, {"reason", "declined"}});
    } catch (const std::exception& error) {
        // The local call is already cleared; a failed signal only leaves the
        // caller to time out on its own.
        bazarish::log::warn("decline signal not delivered: {}", error.what());
    }
}

void Session::endCall()
{
    if (call_.state == CallState::eIdle) {
        return;
    }
    const std::string peer = call_.peerFingerprint;
    const std::string callId = call_.callId;
    // Active: a normal hang-up (answered). Still ringing: we gave up - outgoing is a
    // cancel, an incoming one we end is a decline.
    const CallOutcome outcome = call_.state == CallState::eActive
        ? CallOutcome::eAnswered
        : (call_.initiator ? CallOutcome::eCancelled : CallOutcome::eDeclined);
    logCompletedCall(outcome);
    clearCall();
    try {
        sendCallSignal(peer, "call.end", {{"callId", callId}});
    } catch (const std::exception& error) {
        // The call is over locally either way; the peer falls back to its timeout.
        bazarish::log::warn("end signal not delivered: {}", error.what());
    }
}

void Session::setCallMuted(const bool muted)
{
    call_.muted = muted;
    if (call_.media) {
        call_.media->setMuted(muted);
    }
}

Session::CallInfo Session::currentCall() const
{
    CallInfo info;
    info.state = call_.state;
    info.callId = call_.callId;
    info.peerFingerprint = call_.peerFingerprint;
    info.muted = call_.muted;
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
            } catch (const std::exception& error) {
                bazarish::log::warn("busy signal not delivered: {}", error.what());
            }
            // We could not take this call: record it as a missed call from that peer.
            pendingCallLog_.push_back({from, true, CallOutcome::eMissed, 0});
            message.text = "busy";
            return;
        }
        Bytes key;
        try {
            key = fromBase64(body.value("key", std::string()));
        } catch (const std::exception& error) {
            // Surface the event but do not ring: a call we cannot key is a call we
            // cannot take, and a silent one looks like the peer never called.
            bazarish::log::warn("call invite dropped, key unreadable: {}", error.what());
            return;
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
        call_.muted = false;
            return;
    }

    if (type == "call.accept") {
        if (call_.state == CallState::eOutgoing && call_.callId == message.callId
            && from == call_.peerFingerprint) {
            call_.peerMediaDest = body.value("dest", std::string());
            call_.state = CallState::eActive;
            call_.connectedAtMs = nowMillis();
            startCallMedia();
        }
        return;
    }

    // call.decline / call.end: record the outcome and tear the call down if it is
    // the one we track.
    if (call_.state != CallState::eIdle && call_.callId == message.callId
        && from == call_.peerFingerprint) {
        CallOutcome outcome;
        if (type == "call.decline") {
            // The peer rejected our outgoing call (busy vs an explicit decline).
            outcome = body.value("reason", std::string()) == "busy" ? CallOutcome::eBusy
                                                                     : CallOutcome::eDeclined;
        } else if (call_.state == CallState::eActive) {
            outcome = CallOutcome::eAnswered;  // normal hang-up after connecting
        } else if (call_.state == CallState::eIncoming) {
            outcome = CallOutcome::eMissed;  // the caller cancelled before we answered
        } else {
            outcome = CallOutcome::eDeclined;  // outgoing torn down before it connected
        }
        logCompletedCall(outcome);
        clearCall();
    }
}

void Session::logCompletedCall(const CallOutcome outcome)
{
    if (call_.peerFingerprint.empty()) {
        return;
    }
    const std::int64_t durationSec
        = (outcome == CallOutcome::eAnswered && call_.connectedAtMs > 0)
        ? (nowMillis() - call_.connectedAtMs) / 1000
        : 0;
    // incoming = we did not initiate; the peer is the other party either way.
    pendingCallLog_.push_back(
        CompletedCall{call_.peerFingerprint, !call_.initiator, outcome, durationSec});
}

std::vector<Session::CompletedCall> Session::takeCallLog()
{
    std::vector<CompletedCall> out = std::move(pendingCallLog_);
    pendingCallLog_.clear();
    return out;
}

void Session::tickCalls()
{
    if (call_.startedAtMs == 0 || nowMillis() - call_.startedAtMs <= kRingTimeoutMs) {
        return;  // no ringing call, or still within the ring window (active calls too)
    }
    if (call_.state == CallState::eOutgoing) {
        const std::string peer = call_.peerFingerprint;
        const std::string callId = call_.callId;
        logCompletedCall(CallOutcome::eNoAnswer);
        clearCall();
        try {
            sendCallSignal(peer, "call.end", {{"callId", callId}});  // stop the peer ringing
        } catch (const std::exception& error) {
            bazarish::log::warn("end signal not delivered: {}", error.what());
        }
    } else if (call_.state == CallState::eIncoming) {
        logCompletedCall(CallOutcome::eMissed);
        clearCall();  // the caller times out symmetrically; no signal needed
    }
}

bool Session::hasOwnRouting() const
{
    return !myDest_.empty() && !myServingKeyB64_.empty();
}

void Session::refreshOwnCard()
{
    if (subscriptionCertB64_.empty()) {
        throw std::runtime_error("not subscribed: nothing to refresh");
    }
    const SubscriptionCertificate held
        = SubscriptionCertificate::verify(fromBase64(subscriptionCertB64_));
    // Same term, so the service node treats this as a re-publish and consumes no
    // grant; the point is the routing the server now has and our card does not.
    storeSubscription(client_->renew(
        nowSeconds(), held.notAfter, sealingKey_.publicDer(), ownRoutingHost()));
}

std::string Session::destinationOwner() const
{
    // Enough of a fingerprint to tell two unnamed profiles apart at a glance.
    constexpr std::size_t kOwnerFingerprintChars = 8;
    return name_.empty() ? fingerprint().substr(0, kOwnerFingerprintChars) : name_;
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
