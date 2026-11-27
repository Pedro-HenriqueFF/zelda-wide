// Integration test: Widescreen::render on a synthetic ROM + faked game RAM.
// The harness is in test_wide_common.*; the sections are in test_wide_world/_caves/_hud/_sprites.cpp. They share one Ctx and
// run in this order: later sections rely on state left by earlier ones.
#include <chrono>
#include <string>
#include <cstdlib>
#include "test_wide_common.h"

int main() {
    auto t = std::make_unique<Ctx>();
    if (!t->setup()) return 1;
    // ZW_TIME=1 prints the run time of every section (to see what makes the suite slow)
    auto timed = [&](const char* name, void (*f)(Ctx&)) {
        const auto t0 = std::chrono::steady_clock::now(); f(*t);
        if (std::getenv("ZW_TIME")) std::fprintf(stderr, "%s %.2f s\n", name, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    };
#define RUN(f) if (!std::getenv("ZW_ONLY") || std::string(#f) == std::getenv("ZW_ONLY")) timed(#f, f)   // ZW_ONLY=section27 runs one section (profiling)
    RUN(section01); RUN(section02); RUN(section03); RUN(section04); RUN(section05); RUN(section06); RUN(section07); RUN(section08); RUN(section09); RUN(section10); RUN(section11); RUN(section12); RUN(section13); RUN(section14); RUN(section15); RUN(section16); RUN(section17); RUN(section18); RUN(section19); RUN(section20); RUN(section21); RUN(section22); RUN(section23); RUN(section24); RUN(section25); RUN(section26); RUN(section27); RUN(section28); RUN(section29); RUN(section30); RUN(section31); RUN(section32); RUN(section35);
#undef RUN
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
