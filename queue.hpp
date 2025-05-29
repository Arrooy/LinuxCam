#ifndef QUEUE_H
#define QUEUE_H

#include <condition_variable>
#include <mutex>
#include <queue>

#include "common.h"
template <typename T>
class SafeQueue
{
  public:
    SafeQueue() : is_shutdown_(false) {}

    void push(const T& item)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_shutdown_)
        {
            return;
        }
        queue_.push(item);
        const int size = queue_.size();
        if (size > 3)
        {
            // Empty queue
            std::queue<T> empty;
            std::swap(queue_, empty);
        }
        cond_var_.notify_one();
    }

    void push(T&& item)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_shutdown_)
        {
            return;
        }
        queue_.push(std::move(item));

        cond_var_.notify_one();
    }

    // Wait and pop the front item (blocking)
    bool wait_and_pop(T& item)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_.wait(lock, [&] { return is_shutdown_ || !queue_.empty(); });
        if (is_shutdown_)
        {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    // Try to pop (non-blocking)
    bool try_pop(T& item)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty())
        {
            return false;
        }

        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            is_shutdown_ = true;
        }
        cond_var_.notify_all();
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool is_shutdown() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_shutdown_;
    }

  private:
    bool is_shutdown_;
    mutable std::mutex mutex_;
    std::condition_variable cond_var_;
    std::queue<T> queue_;
};

#endif // QUEUE_H
