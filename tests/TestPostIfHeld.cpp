// Bazarish project (c) 2026
#include "PostIfHeld.hpp"
#include "TestUtil.hpp"

#include <QCoreApplication>
#include <QObject>
#include <QThread>

#include <atomic>
#include <mutex>
#include <thread>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    std::mutex guard;
    QObject target;
    std::atomic<QObject*> held{nullptr};
    int calls = 0;

    CHECK(bazarish::app::postIfHeld<QObject>(guard, held, [&calls](QObject&) { ++calls; }));
    QCoreApplication::processEvents();
    CHECK(calls == 0);

    held.store(&target);
    bool posted = false;
    std::thread foreign([&]() {
        posted = bazarish::app::postIfHeld<QObject>(guard, held, [&calls, &target](QObject& on) {
            CHECK(&on == &target);
            CHECK(QThread::currentThread() == target.thread());
            ++calls;
        });
    });
    foreign.join();
    CHECK(posted);
    CHECK(calls == 0);
    QCoreApplication::processEvents();
    CHECK(calls == 1);
    return 0;
}
