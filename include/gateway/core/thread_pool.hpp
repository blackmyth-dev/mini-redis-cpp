#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace gateway::core {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count) {
        if (thread_count == 0) throw std::invalid_argument("thread_count must be greater than zero");
        workers_.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i) workers_.emplace_back([this] { worker_loop(); });
    }

    ~ThreadPool() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename Function>
    void submit(Function&& function) {
        {
            std::lock_guard lock(mutex_);
            if (stopping_) throw std::runtime_error("submit on stopped ThreadPool");
            tasks_.emplace(std::forward<Function>(function));
        }
        ready_.notify_one();
    }

private:
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                if (stopping_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            try { task(); } catch (...) { /* A client task must not kill a worker. */ }
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    bool stopping_{false};
};

}  // namespace gateway::core

