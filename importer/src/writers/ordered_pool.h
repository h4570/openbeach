#pragma once

// A worker pool with an ordered commit (docs/architecture/data-formats/converter/index.md, "Parallel conversion").
// Independent items are produced on worker threads, but every result is handed to `consume` on the calling thread in item
// order, so what the caller writes, which error it reports first and the progress it counts are the same as with one job.
// std::thread only. The browser build (Emscripten, no pthreads) always runs serially.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#if !defined(__EMSCRIPTEN__)
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#endif

namespace obl::convert {

// The jobs a caller may use: `requested` (0 or less: one per hardware thread), at most one per item, one in the browser.
inline int EffectiveJobs(int requested, size_t items)
{
#if defined(__EMSCRIPTEN__)
    (void)requested;
    (void)items;
    return 1;
#else
    int jobs = requested > 0 ? requested : static_cast<int>(std::thread::hardware_concurrency());
    if (jobs < 1) jobs = 1;
    return static_cast<int>(std::min<size_t>(static_cast<size_t>(jobs), std::max<size_t>(items, 1)));
#endif
}

// Runs produce(i) for i in [0, count) and consume(i, result) in index order; false as soon as a consume returns false (items
// after it are not consumed; workers finish the item they hold, and a result nobody consumes is dropped). `cost(i)` is the
// memory in bytes item i holds from its start until it is consumed; items start in order and only while the running total
// stays within `budget` (an item that alone exceeds it runs when nothing else holds memory), which bounds the memory in
// flight however large the items are. With one job nothing is threaded: produce and consume alternate item by item.
// An exception thrown by produce(i) is rethrown from RunOrdered when its turn to be consumed comes.
template<class Result>
bool RunOrdered(size_t count, int jobs, uint64_t budget, const std::function<uint64_t(size_t)>& cost,
        const std::function<Result(size_t)>& produce, const std::function<bool(size_t, Result&)>& consume)
{
    jobs = EffectiveJobs(jobs, count);
    if (jobs <= 1) {
        for (size_t i = 0; i < count; ++i) {
            Result result = produce(i);
            if (!consume(i, result)) return false;
        }
        return true;
    }

#if defined(__EMSCRIPTEN__)
    return true; // unreachable: EffectiveJobs is 1 here, and the browser has no pthreads
#else
    struct Slot {
        std::optional<Result> result;
        std::exception_ptr failure;
        uint64_t cost = 0;
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::map<size_t, Slot> slots; // started and not yet consumed
    size_t next = 0; // the next item to start
    uint64_t held = 0; // cost of the slots
    bool stop = false;

    const auto worker = [&] {
        for (;;) {
            size_t item = 0;
            {
                std::unique_lock lock(mutex);
                uint64_t itemCost = 0;
                changed.wait(lock, [&] {
                    if (stop || next >= count) return true;
                    itemCost = cost(next);
                    return held == 0 || held + itemCost <= budget;
                });
                if (stop || next >= count) return;
                item = next++;
                held += itemCost;
                slots[item].cost = itemCost;
            }
            std::optional<Result> result;
            std::exception_ptr failure;
            try {
                result = produce(item);
            } catch (...) {
                failure = std::current_exception();
            }
            {
                const std::lock_guard lock(mutex);
                Slot& slot = slots[item];
                slot.result = std::move(result);
                slot.failure = failure;
            }
            changed.notify_all();
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(jobs));
    for (int i = 0; i < jobs; ++i) threads.emplace_back(worker);
    const auto finish = [&] {
        {
            const std::lock_guard lock(mutex);
            stop = true;
        }
        changed.notify_all();
        for (auto& thread : threads) thread.join();
    };

    bool ok = true;
    std::exception_ptr failure;
    for (size_t i = 0; i < count && ok && !failure; ++i) {
        std::optional<Result> result;
        {
            std::unique_lock lock(mutex);
            changed.wait(lock, [&] {
                const auto it = slots.find(i);
                return it != slots.end() && (it->second.result || it->second.failure);
            });
            Slot& slot = slots[i];
            result = std::move(slot.result);
            failure = slot.failure;
        }
        if (!failure) ok = consume(i, *result);
        result.reset(); // its memory is released before the next item may start
        {
            const std::lock_guard lock(mutex);
            held -= slots[i].cost;
            slots.erase(i);
        }
        changed.notify_all();
    }
    finish();
    if (failure) std::rethrow_exception(failure);
    return ok;
#endif
}

} // namespace obl::convert
