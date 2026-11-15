#include "proxy/stream_bridge.h"

#include <chrono>
#include <utility>

namespace mxr
{

    void StreamBridge::setHead(int status, std::string contentType)
    {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (headReady_)
            {
                return; // 只认第一次
            }
            headReady_ = true;
            status_ = status;
            ct_ = std::move(contentType);
        }
        cvHead_.notify_all();
    }

    void StreamBridge::fail(std::string why)
    {
        {
            std::lock_guard<std::mutex> lk(mu_);
            err_ = std::move(why);
            failed_ = true;
            headReady_ = true; // 让 waitHead 别干等到超时
            closeLocked(false);
        }
        // 三个都唤醒：可能有人在等头、有人在等数据、有人在等空间
        cvHead_.notify_all();
        cvData_.notify_all();
        cvSpace_.notify_all();
    }

    void StreamBridge::finish()
    {
        {
            std::lock_guard<std::mutex> lk(mu_);
            closeLocked(false);
        }
        cvHead_.notify_all();
        cvData_.notify_all();
        cvSpace_.notify_all();
    }

    void StreamBridge::abort()
    {
        {
            std::lock_guard<std::mutex> lk(mu_);
            closeLocked(true);
        }
        cvHead_.notify_all();
        cvData_.notify_all();
        cvSpace_.notify_all();
    }

    bool StreamBridge::waitHead(int timeoutMs)
    {
        std::unique_lock<std::mutex> lk(mu_);
        // 谓词必须把 closed_ 也算进去：fail() 会置 closed_，
        // 如果只等 headReady_，某些路径上会白等到超时。
        return cvHead_.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                                [this]
                                { return headReady_ || closed_; });
    }

    int StreamBridge::status() const
    {
        std::lock_guard<std::mutex> lk(mu_);
        return status_;
    }

    std::string StreamBridge::contentType() const
    {
        std::lock_guard<std::mutex> lk(mu_);
        return ct_;
    }

    std::string StreamBridge::error() const
    {
        std::lock_guard<std::mutex> lk(mu_);
        return err_;
    }

    bool StreamBridge::failed() const
    {
        std::lock_guard<std::mutex> lk(mu_);
        return failed_;
    }

    bool StreamBridge::aborted() const
    {
        std::lock_guard<std::mutex> lk(mu_);
        return aborted_;
    }

    std::size_t StreamBridge::queuedBytes() const
    {
        std::lock_guard<std::mutex> lk(mu_);
        return queued_;
    }

    bool StreamBridge::push(const char *data, std::size_t len)
    {
        {
            std::unique_lock<std::mutex> lk(mu_);
            cvSpace_.wait(lk, [&]
                          { return closed_ || queued_ < kMaxQueuedBytes; });
            if (closed_)
                return false;
            q_.emplace_back(data, len);
            queued_ += len;
        }
        cvData_.notify_one();
        return true;
    }

    bool StreamBridge::next(std::string &out)
    {
        {
            std::unique_lock<std::mutex> lk(mu_);
            cvData_.wait(lk, [this]
                         { return !q_.empty() || closed_; });
            if (q_.empty())
                return false;
            out = std::move(q_.front());
            queued_ -= out.size();
            q_.pop_front();
        }
        cvSpace_.notify_one();
        return true;
    }

    void StreamBridge::closeLocked(bool aborted)
    {
        closed_ = true;
        if (aborted)
            aborted_ = true;
    }

} // namespace mxr
