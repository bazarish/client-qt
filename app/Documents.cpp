// Bazarish project (c) 2026
#include "Documents.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <cerrno>
#include <cstring>
#include <unistd.h>
#endif

#include <stdexcept>
#include <string>
#include <utility>

namespace bazarish::app {

#ifdef Q_OS_ANDROID
namespace {

const char* const kDocuments = "sh/bazari/client/Documents";

QJniObject jniString(const QString& text)
{
    return QJniObject::fromString(text);
}

int openDescriptor(const QString& uri, const QString& mode)
{
    const jint fd = QJniObject::callStaticMethod<jint>(kDocuments, "open",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)I",
        QNativeInterface::QAndroidApplication::context().object(),
        jniString(uri).object<jstring>(), jniString(mode).object<jstring>());
    if (fd < 0) {
        throw std::runtime_error("the document does not open");
    }
    return fd;
}

QString documentName(const QString& uri)
{
    return QJniObject::callStaticObjectMethod(kDocuments, "name",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
        QNativeInterface::QAndroidApplication::context().object(),
        jniString(uri).object<jstring>())
        .toString();
}

qint64 documentSize(const QString& uri)
{
    return QJniObject::callStaticMethod<jlong>(kDocuments, "size",
        "(Landroid/content/Context;Ljava/lang/String;)J",
        QNativeInterface::QAndroidApplication::context().object(),
        jniString(uri).object<jstring>());
}

bool deleteDocument(const QString& uri)
{
    return QJniObject::callStaticMethod<jboolean>(kDocuments, "delete",
        "(Landroid/content/Context;Ljava/lang/String;)Z",
        QNativeInterface::QAndroidApplication::context().object(),
        jniString(uri).object<jstring>());
}

class DocumentOutput : public client::FileOutput {
public:
    explicit DocumentOutput(QString uri)
        : uri_(std::move(uri))
        , fd_(openDescriptor(uri_, QStringLiteral("wt")))
    {
    }
    DocumentOutput(const DocumentOutput&) = delete;
    DocumentOutput& operator=(const DocumentOutput&) = delete;

    ~DocumentOutput() override
    {
        if (fd_ >= 0) {
            // Reached only when writing failed, and that failure is the one reported.
            static_cast<void>(::close(fd_));
        }
    }

    void write(const void* data, std::size_t size) override
    {
        const char* at = static_cast<const char*>(data);
        while (size > 0) {
            const ssize_t put = ::write(fd_, at, size);
            if (put < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(
                    std::string("the document did not take the file: ") + std::strerror(errno));
            }
            at += put;
            size -= static_cast<std::size_t>(put);
        }
    }

    void finish() override
    {
        if (::close(std::exchange(fd_, -1)) != 0) {
            throw std::runtime_error(
                std::string("the document did not keep the file: ") + std::strerror(errno));
        }
    }

    void discard() noexcept override
    {
        if (fd_ >= 0) {
            static_cast<void>(::close(std::exchange(fd_, -1)));
        }
        if (!deleteDocument(uri_)) {
            bazarish::log::warn("a broken download stays in the document it was saved to");
        }
    }

private:
    QString uri_;
    int fd_;
};

class DocumentInput : public client::FileInput {
public:
    explicit DocumentInput(const QString& uri)
        : fd_(openDescriptor(uri, QStringLiteral("r")))
    {
    }
    DocumentInput(const DocumentInput&) = delete;
    DocumentInput& operator=(const DocumentInput&) = delete;

    ~DocumentInput() override
    {
        // Read only: closing cannot lose anything.
        static_cast<void>(::close(fd_));
    }

    std::size_t read(void* const buffer, const std::size_t size) override
    {
        for (;;) {
            const ssize_t got = ::read(fd_, buffer, size);
            if (got >= 0) {
                return static_cast<std::size_t>(got);
            }
            if (errno != EINTR) {
                throw std::runtime_error(
                    std::string("the document did not give the file: ") + std::strerror(errno));
            }
        }
    }

private:
    int fd_;
};

class DeviceFiles : public client::FileSource {
public:
    bool exists(const std::string& location) const override
    {
        const QString where = QString::fromStdString(location);
        return isDocumentUri(where) ? !documentName(where).isEmpty() : local_->exists(location);
    }

    std::string name(const std::string& location) const override
    {
        const QString where = QString::fromStdString(location);
        return isDocumentUri(where) ? documentName(where).toStdString() : local_->name(location);
    }

    std::unique_ptr<client::FileInput> open(const std::string& location) const override
    {
        const QString where = QString::fromStdString(location);
        if (isDocumentUri(where)) {
            return std::make_unique<DocumentInput>(where);
        }
        return local_->open(location);
    }

private:
    std::shared_ptr<const client::FileSource> local_ = client::localFiles();
};

}  // namespace
#endif

FileFacts factsOf(const QString& location)
{
#ifdef Q_OS_ANDROID
    if (isDocumentUri(location)) {
        return FileFacts{documentName(location), documentSize(location)};
    }
#endif
    const QFileInfo info(location);
    return FileFacts{info.fileName(), info.exists() ? info.size() : -1};
}

std::shared_ptr<const client::FileSource> deviceFiles()
{
#ifdef Q_OS_ANDROID
    return std::make_shared<const DeviceFiles>();
#else
    return client::localFiles();
#endif
}

std::unique_ptr<client::FileOutput> openOutput(const QString& dest)
{
#ifdef Q_OS_ANDROID
    if (isDocumentUri(dest)) {
        return std::make_unique<DocumentOutput>(dest);
    }
#endif
    return client::fileTargetAt(dest.toStdString()).open();
}

client::FileTarget fileTargetFor(const QString& dest, [[maybe_unused]] const QString& partName)
{
#ifdef Q_OS_ANDROID
    if (isDocumentUri(dest)) {
        const QDir parts(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/parts"));
        if (!parts.mkpath(QStringLiteral("."))) {
            throw std::runtime_error("the directory for incoming files cannot be made");
        }
        return client::FileTarget{parts.filePath(partName).toStdString(),
            [dest]() { return openOutput(dest); }};
    }
#endif
    return client::fileTargetAt(dest.toStdString());
}

}  // namespace bazarish::app
