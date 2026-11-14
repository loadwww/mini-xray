#include "common/clock.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace mxr {

long long nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string stampNow()
{
    const std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);   // MSVC 的 localtime 是废弃的不安全版本，用的是带 _s 的这个
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tmv);
    return buf;
}

} // namespace mxr
