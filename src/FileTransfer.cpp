// Bazarish project (c) 2026
#include "FileTransfer.hpp"

#include <bazarish/Cms.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Log.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace bazarish::client {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kChunkBytes = 64 * 1024;
// Consecutive attempts that move no bytes before a transfer is abandoned.
constexpr int kMaxStalledAttempts = 5;

void encodeBigEndian64(const std::uint64_t value, std::array<std::uint8_t, 8>& out)
{
    for (std::size_t i = 0; i < 8; ++i) {
        out[i] = static_cast<std::uint8_t>((value >> (56 - 8 * i)) & 0xFF);
    }
}

std::uint64_t decodeBigEndian64(const std::array<std::uint8_t, 8>& in)
{
    std::uint64_t value = 0;
    for (const std::uint8_t byte : in) {
        value = (value << 8) | byte;
    }
    return value;
}

// Appends to a partial ciphertext file, tracking its length so a resumed
// transfer knows where it stopped.
class PartialFile {
public:
    explicit PartialFile(fs::path path)
        : path_(std::move(path))
        , out_(path_, std::ios::binary | std::ios::trunc)
    {
        if (!out_) {
            throw std::runtime_error("failed to open " + path_.string());
        }
    }

    void append(const void* data, const std::size_t size)
    {
        if (size == 0) {
            return;
        }
        out_.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!out_) {
            throw std::runtime_error("failed to write " + path_.string());
        }
        size_ += size;
    }

    std::uint64_t size() const { return size_; }

    void finish()
    {
        out_.flush();
        if (!out_) {
            throw std::runtime_error("failed to flush " + path_.string());
        }
        out_.close();
    }

private:
    fs::path path_;
    std::ofstream out_;
    std::uint64_t size_ = 0;
};

}  // namespace

PreparedFile prepareFile(const fs::path& path, const fs::path& ciphertextPath)
{
    PreparedFile prepared;
    prepared.key = toHex(randomBytes(32));
    cms::sealWithPasswordToFile(path, ciphertextPath, prepared.key);
    prepared.ciphertextPath = ciphertextPath;
    prepared.sha256 = toHex(sha256File(ciphertextPath));
    prepared.size = fs::file_size(ciphertextPath);
    return prepared;
}

nlohmann::json fileOfferToJson(const FileOffer& offer)
{
    return {
        {"fileId", offer.fileId},
        {"host", offer.host},
        {"key", offer.key},
        {"sha256", offer.sha256},
        {"size", offer.size},
    };
}

FileOffer fileOfferFromJson(const nlohmann::json& json)
{
    FileOffer offer;
    offer.fileId = json.at("fileId").get<std::string>();
    offer.host = json.at("host").get<std::string>();
    offer.key = json.at("key").get<std::string>();
    offer.sha256 = json.at("sha256").get<std::string>();
    offer.size = json.at("size").get<std::uint64_t>();
    return offer;
}

namespace {

// Appends a fetch attempt's bytes to the partial file and records the total the
// sender declared, so the driver can tell progress from a stall.
class PartialSink : public TransferSink {
public:
    PartialSink(PartialFile& part, TransferProgressFn onProgress)
        : part_(part)
        , onProgress_(std::move(onProgress))
    {
    }

    void total(const std::uint64_t bytes) override { declaredTotal = bytes; }

    void append(const void* data, const std::size_t size) override
    {
        part_.append(data, size);
        if (onProgress_ && declaredTotal != 0) {
            onProgress_(part_.size(), declaredTotal);
        }
    }

    std::uint64_t declaredTotal = 0;

private:
    PartialFile& part_;
    TransferProgressFn onProgress_;
};

}  // namespace

void receiveFile(const FetchAttemptFn& fetch, const FileOffer& offer, const fs::path& destPath,
    const TransferProgressFn& onProgress, const std::atomic<bool>* cancel)
{
    const fs::path partPath = destPath.string() + ".part";
    try {
        PartialFile part(partPath);
        int stalled = 0;
        while (part.size() < offer.size) {
            if (cancel != nullptr && cancel->load()) {
                throw std::runtime_error("transfer cancelled");
            }
            const std::uint64_t before = part.size();
            PartialSink sink(part, onProgress);
            fetch(before, sink);
            if (sink.declaredTotal != 0 && sink.declaredTotal != offer.size) {
                throw std::runtime_error("sender declares a different size than it offered");
            }
            if (part.size() > offer.size) {
                throw std::runtime_error("sender sent more than it offered");
            }
            if (part.size() == before) {
                if (++stalled >= kMaxStalledAttempts) {
                    throw std::runtime_error("transfer stalled");
                }
            } else {
                stalled = 0;
            }
        }
        part.finish();

        // Verify-then-decrypt: a tampered or truncated transfer never yields
        // cleartext.
        if (toHex(sha256File(partPath)) != offer.sha256) {
            throw std::runtime_error("file digest mismatch");
        }
        cms::unsealWithPasswordToFile(partPath, destPath, offer.key);
    } catch (...) {
        std::error_code ec;
        fs::remove(partPath, ec);
        throw;
    }
    std::error_code ec;
    fs::remove(partPath, ec);
}

bool serveFile(bazarish::i2p::Endpoint& endpoint, const fs::path& ciphertextPath,
    const std::chrono::seconds window, const TransferProgressFn& onProgress,
    const std::atomic<bool>* cancel)
{
    const std::uint64_t total = fs::file_size(ciphertextPath);
    const auto deadline = std::chrono::steady_clock::now() + window;

    while (std::chrono::steady_clock::now() < deadline) {
        if (cancel != nullptr && cancel->load()) {
            return false;
        }
        std::string peer;
        const std::unique_ptr<bazarish::i2p::Stream> stream
            = endpoint.accept(peer, std::chrono::seconds(5));
        if (!stream) {
            continue;
        }
        try {
            std::array<std::uint8_t, 8> header{};
            stream->readExact(header.data(), header.size());
            const std::uint64_t offset = decodeBigEndian64(header);
            if (offset > total) {
                stream->close();
                continue;
            }
            encodeBigEndian64(total, header);
            stream->writeAll(header.data(), header.size());

            std::ifstream in(ciphertextPath, std::ios::binary);
            if (!in) {
                throw std::runtime_error("failed to open " + ciphertextPath.string());
            }
            in.seekg(static_cast<std::streamoff>(offset));
            std::vector<char> buffer(kChunkBytes);
            std::uint64_t sent = offset;
            while (sent < total) {
                if (cancel != nullptr && cancel->load()) {
                    return false;
                }
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize got = in.gcount();
                if (got <= 0) {
                    break;
                }
                stream->writeAll(buffer.data(), static_cast<std::size_t>(got));
                sent += static_cast<std::uint64_t>(got);
                if (onProgress) {
                    onProgress(sent, total);
                }
            }
            stream->close();
            if (sent >= total) {
                return true;
            }
        } catch (const std::exception& error) {
            // A dropped peer is normal: it reconnects with the next offset.
            log::debug("file-serve: attempt failed: {}", error.what());
        }
    }
    return false;
}

void fetchFileOverI2p(bazarish::i2p::Router& router, const FileOffer& offer,
    const fs::path& destPath, const bazarish::i2p::Privacy privacy,
    const TransferProgressFn& onProgress, const std::atomic<bool>* cancel)
{
    // Each attempt dials from a fresh one-time destination, so a resumed transfer
    // is not linkable to the attempt it continues.
    const FetchAttemptFn fetch
        = [&router, &offer, privacy](const std::uint64_t offset, TransferSink& sink) {
              bazarish::i2p::EndpointConfig config{bazarish::i2p::Keys::generate()};
              config.privacy = privacy;
              config.tunnelQuantity = 2;
              config.published = false;
              config.label = "File download";
              const std::shared_ptr<bazarish::i2p::Endpoint> endpoint
                  = router.createEndpoint(config);
              const std::unique_ptr<bazarish::i2p::Stream> stream
                  = endpoint->connect(offer.host, std::chrono::seconds(60));
              if (!stream) {
                  throw std::runtime_error("cannot reach the sender");
              }

              std::array<std::uint8_t, 8> header{};
              encodeBigEndian64(offset, header);
              stream->writeAll(header.data(), header.size());
              stream->readExact(header.data(), header.size());
              sink.total(decodeBigEndian64(header));

              std::vector<std::uint8_t> buffer(kChunkBytes);
              while (true) {
                  const std::size_t got = stream->readSome(buffer.data(), buffer.size());
                  if (got == 0) {
                      break;
                  }
                  sink.append(buffer.data(), got);
              }
          };
    receiveFile(fetch, offer, destPath, onProgress, cancel);
}

}  // namespace bazarish::client
