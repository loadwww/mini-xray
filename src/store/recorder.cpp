#include "store/recorder.h"

#include <exception>
#include <utility>

#include "store/redact.h"

namespace mxr {
namespace {

std::string capBody(std::string s)
{
    constexpr std::size_t kMax = 256 * 1024;
    if (s.size() <= kMax) {
        return s;
    }
    s.resize(kMax);
    s += "...<truncated>";
    return s;
}

} // namespace

Recorder::Recorder(const std::string& path) : writer_(path)
{
    // writer_ 已经在初始化列表里打开了文件（打不开就抛，进程起不来 —— 这是对的：
    // 记录器坏了，整个 serve 就没意义了）。
    // 线程放到构造函数体里起，而不是初始化列表：要确保 writer_ 已经就位。
    th_ = std::thread([this] { run(); });
}

void Recorder::flush()
{
    {
        std::unique_lock<std::mutex>lk(mu_);
        stopping_=true;
    }
    cv_.notify_all();
    if(th_.joinable())th_.join();

}

Recorder::~Recorder()
{
    flush();
}

void Recorder::push(Event ev)
{
    {
        std::unique_lock<std::mutex> lk(mu_);
        if(stopping_){
            return;
        }
        if(q_.size()>=kMaxQueued){
            ++dropped_;
            return;
        }
        ev.seq = ++seq_;
        q_.push(std::move(ev));
        lk.unlock();
    }
    cv_.notify_one();
}

void Recorder::run()
{
    Event ev;
    while(1){
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk,
                [&]{return stopping_||!q_.empty();}
        );
        if(q_.empty())return;
        ev=std::move(q_.front());  
        q_.pop();
        lk.unlock();

        ev.headers=redactHeaders(ev.headers);
        ev.body=capBody(redactBody(ev.body));
        try{
            writer_.append(toJson(ev));
            ++written_;

        }catch(...){
            ++dropped_;
        }
    }
}

} // namespace mxr
