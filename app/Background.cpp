// Bazarish project (c) 2026
#include "Background.hpp"

#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

namespace bazarish::app {

namespace {

class Background {
public:
    static Background& instance()
    {
        static Background background;
        return background;
    }

    ~Background() { reap(true); }

    void run(std::function<void()> work)
    {
        reap(false);
        const std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
        std::thread thread([work = std::move(work), done]() {
            work();
            done->store(true);
        });
        const std::lock_guard<std::mutex> lock(mutex_);
        running_.push_back(Running{std::move(thread), done});
    }

private:
    struct Running {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    void reap(const bool all)
    {
        std::list<Running> finished;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = running_.begin(); it != running_.end();) {
                if (all || it->done->load()) {
                    finished.splice(finished.end(), running_, it++);
                } else {
                    ++it;
                }
            }
        }
        for (Running& gone : finished) {
            gone.thread.join();
        }
    }

    std::mutex mutex_;
    std::list<Running> running_;
};

}  // namespace

void inBackground(std::function<void()> work)
{
    Background::instance().run(std::move(work));
}

}  // namespace bazarish::app
