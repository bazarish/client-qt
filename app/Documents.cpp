// Bazarish project (c) 2026
#include "Documents.hpp"

#include <QDir>
#include <QStandardPaths>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

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

class DocumentOutput : public client::FileOutput {
public:
    explicit DocumentOutput(const int fd)
        : fd_(fd)
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

private:
    int fd_;
};

std::unique_ptr<client::FileOutput> openDocument(const QString& uri)
{
    const jint fd = QJniObject::callStaticMethod<jint>("sh/bazari/client/Documents",
        "openForWriting", "(Landroid/content/Context;Ljava/lang/String;)I",
        QNativeInterface::QAndroidApplication::context().object(),
        QJniObject::fromString(uri).object<jstring>());
    if (fd < 0) {
        throw std::runtime_error("the document does not open for writing");
    }
    return std::make_unique<DocumentOutput>(fd);
}

}  // namespace
#endif

std::unique_ptr<client::FileOutput> openOutput(const QString& dest)
{
#ifdef Q_OS_ANDROID
    if (isDocumentUri(dest)) {
        return openDocument(dest);
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
