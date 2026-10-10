#include "tefra/core/time.h"

#include <chrono>
#include <thread>

namespace tefra {

u64 time_now_ns() {
    using namespace std::chrono;
    return static_cast<u64>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

void sleep_ms(u32 ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

} // namespace tefra
