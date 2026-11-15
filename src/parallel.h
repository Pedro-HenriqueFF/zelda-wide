// A tiny parallel-for for the per-pixel passes (rows or columns that do not depend on each other). Work is handed out in chunks to
// whichever thread is free, so the result never depends on the number of threads. The worker threads are created once and sleep between
// jobs (creating threads on every call costs about 100 us each on Windows, which a frame with ten such passes cannot afford). Without a
// usable std::thread (old MinGW "win32" thread model) everything runs on the calling thread.
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdlib>
#if defined(__GLIBCXX__) && !defined(_GLIBCXX_HAS_GTHREADS)
#define ZW_NO_THREADS 1
#endif
#ifndef ZW_NO_THREADS
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#endif

namespace par {

inline int& maxThreads() {                                  // (tests set this to compare 1 thread against several)
    static int n = [] {
#ifndef ZW_NO_THREADS
        if (const char* e = std::getenv("ZW_THREADS")) { const int v = std::atoi(e); if (v >= 1) return std::min(v, 8); }   // (ZW_THREADS=1 turns the threads off)
        const unsigned h = std::thread::hardware_concurrency();
        return (int)std::clamp<unsigned>(h ? h : 1, 1, 8);
#else
        return 1;
#endif
    }();
    return n;
}

#ifndef ZW_NO_THREADS
class Pool {
public:
    static constexpr int kWorkers = 7;                      // (threads are only used up to maxThreads(); idle workers cost nothing)
    static Pool& get() { static Pool p; return p; }

    // Runs f(ctx) on `helpers` workers and on the calling thread, and returns when all are done.
    void dispatch(void (*f)(void*), void* ctx, int helpers) {
        helpers = std::min(helpers, kWorkers);
        if (helpers <= 0 || onWorker()) { f(ctx); return; }   // (a pass started from inside a pass stays on its own thread)
        {
            std::lock_guard<std::mutex> l(m);
            fn = f; arg = ctx; wanted = helpers; finished = 0; ++generation;
        }
        wake.notify_all();
        f(ctx);
        std::unique_lock<std::mutex> l(m);
        done.wait(l, [&] { return finished == wanted; });
    }

private:
    Pool() { for (int i = 0; i < kWorkers; i++) workers.emplace_back([this, i] { loop(i); }); }
    ~Pool() {
        { std::lock_guard<std::mutex> l(m); stop = true; }
        wake.notify_all();
        for (auto& t : workers) t.join();
    }
    static bool& onWorker() { static thread_local bool w = false; return w; }

    void loop(int index) {
        onWorker() = true;
        unsigned long long seen = 0;
        for (;;) {
            void (*f)(void*); void* a;
            {
                std::unique_lock<std::mutex> l(m);
                wake.wait(l, [&] { return stop || generation != seen; });
                if (stop) return;
                seen = generation;
                if (index >= wanted) continue;                // (this job needs fewer helpers)
                f = fn; a = arg;
            }
            f(a);
            std::lock_guard<std::mutex> l(m);
            if (++finished == wanted) done.notify_one();
        }
    }

    std::vector<std::thread> workers;
    std::mutex m;
    std::condition_variable wake, done;
    void (*fn)(void*) = nullptr; void* arg = nullptr;
    int wanted = 0, finished = 0; unsigned long long generation = 0; bool stop = false;
};

template <class W> void trampoline(void* p) { (*static_cast<W*>(p))(); }
#endif

// fn(a, b) is called for chunks [a, b) of [begin, end), each at most `chunk` long; work smaller than two chunks stays on this thread.
template <class F> void forRange(int begin, int end, int chunk, F&& fn) {
    const int n = end - begin;
    if (n <= 0) return;
    chunk = std::max(1, chunk);
    const int threads = std::min(maxThreads(), (n + chunk - 1) / chunk);
#ifndef ZW_NO_THREADS
    if (threads > 1) {
        std::atomic<int> next{begin};
        auto work = [&] { for (;;) { const int a = next.fetch_add(chunk); if (a >= end) return; fn(a, std::min(end, a + chunk)); } };
        Pool::get().dispatch(&trampoline<decltype(work)>, &work, threads - 1);
        return;
    }
#endif
    fn(begin, end);
}

}  // namespace par
