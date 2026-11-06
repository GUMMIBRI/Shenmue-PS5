#include "cedar/asset_stream.hpp"
#include <limits>

namespace cedar {
namespace {
AssetStreamStatus thread_error(int code) noexcept {
    return {AssetStreamError::pthread_failure, code};
}
} // namespace

AssetStream::~AssetStream() {
    // Explicit successful stop is a precondition, not a best-effort destructor
    // promise. Violating it leaves a worker referring to this object's storage.
    // Never detach, terminate or destroy live-worker synchronization primitives.
    if (join_pending_.load() || sync_broken_.load()) return;
    // Explicit release() is available when destroy errors must be observed.
    if (condition_initialized_) (void)pthread_cond_destroy(&condition_);
    if (mutex_initialized_) (void)pthread_mutex_destroy(&mutex_);
}

void AssetStream::fatal(int error, bool broken) noexcept {
    int expected = 0;
    (void)fatal_error_.compare_exchange_strong(expected, error);
    if (broken) sync_broken_.store(true);
    state_.store(AssetStreamState::faulted);
}
AssetStreamStatus AssetStream::lock() noexcept {
    if (sync_broken_.load())
        return {AssetStreamError::unrecoverable_sync, fatal_error_.load()};
    const int error = pthread_mutex_lock(&mutex_);
    if (error) { fatal(error, true); return thread_error(error); }
    return {};
}
AssetStreamStatus AssetStream::unlock() noexcept {
    const int error = pthread_mutex_unlock(&mutex_);
    if (error) { fatal(error, true); return thread_error(error); }
    return {};
}

AssetStreamStatus AssetStream::start(const ArchiveSet& archives) noexcept {
    if (!archives.bound()) return {AssetStreamError::invalid_argument};
    if (join_pending_.load()) return {AssetStreamError::already_started};
    if (sync_broken_.load())
        return {AssetStreamError::unrecoverable_sync, fatal_error_.load()};
    if (fatal_error_.load()) return {AssetStreamError::worker_failure, fatal_error_.load()};
    if (!mutex_initialized_) {
        const int error = pthread_mutex_init(&mutex_, nullptr);
        if (error) return thread_error(error);
        mutex_initialized_ = true;
    }
    if (!condition_initialized_) {
        const int error = pthread_cond_init(&condition_, nullptr);
        if (error) return thread_error(error); // Retain initialized mutex for retry/destruction.
        condition_initialized_ = true;
    }
    primitives_ready_.store(true);
    auto status = lock();
    if (!status) return status;
    if (pending_count_ || complete_count_) {
        const auto released = unlock();
        return released ? AssetStreamStatus{AssetStreamError::completions_pending} : released;
    }
    for (const auto& slot : slots_) {
        if (slot.state != SlotState::free) {
            const auto released = unlock();
            return released ? AssetStreamStatus{AssetStreamError::completions_pending} : released;
        }
    }
    archives_ = &archives;
    stopping_ = false;
    pending_head_ = complete_head_ = 0;
    const int error = pthread_create(&worker_, nullptr, worker_entry, this);
    if (error) {
        archives_ = nullptr;
        const auto released = unlock();
        return released ? thread_error(error) : released;
    }
    join_pending_.store(true);
    state_.store(AssetStreamState::running);
    // Creation already established the worker/borrow lifetime. A failed unlock
    // still leaves join_pending observable; it is NOT a failed-create rollback.
    return unlock();
}

AssetStreamStatus AssetStream::submit(const AssetReadRequest& request,
                                    std::uint64_t& token) noexcept {
    token = 0;
    if ((!request.buffer && request.length) ||
        request.length > std::numeric_limits<std::uint64_t>::max() - request.entry_offset)
        return {AssetStreamError::invalid_argument};
    if (!primitives_ready_.load()) return {AssetStreamError::not_started};
    auto status = lock();
    if (!status) return status;
    AssetStreamStatus result{};
    if (!join_pending_.load()) result.error = AssetStreamError::not_started;
    else if (stopping_) result.error = AssetStreamError::stopping;
    else if (fatal_error_.load()) result = {AssetStreamError::worker_failure, fatal_error_.load()};
    else if (!next_token_) result.error = AssetStreamError::token_exhausted;
    if (!result) {
        const auto released = unlock();
        return released ? result : released;
    }
    std::size_t index = 0;
    while (index < max_jobs && slots_[index].state != SlotState::free) ++index;
    if (index == max_jobs) {
        const auto released = unlock();
        return released ? AssetStreamStatus{AssetStreamError::queue_full} : released;
    }
    auto& slot = slots_[index];
    slot = {};
    slot.request = request;
    slot.token = next_token_++;
    slot.state = SlotState::queued;
    pending_[(pending_head_ + pending_count_) % max_jobs] = index;
    ++pending_count_;
    const int error = pthread_cond_signal(&condition_);
    if (error) {
        // Worker cannot consume this newly appended request while mutex is held.
        // Retract it, consuming the numeric token without ever publishing it.
        --pending_count_;
        slot = {};
        fatal(error, false);
        const auto released = unlock();
        return released ? thread_error(error) : released;
    }
    token = slot.token;
    return unlock(); // A nonzero token remains accepted on unlock failure.
}

AssetStreamStatus AssetStream::cancel(std::uint64_t token) noexcept {
    if (!token) return {AssetStreamError::invalid_argument};
    if (!primitives_ready_.load()) return {AssetStreamError::not_started};
    auto status = lock();
    if (!status) return status;
    AssetStreamStatus result{AssetStreamError::not_found};
    for (auto& slot : slots_) {
        if (slot.state == SlotState::free || slot.token != token) continue;
        if (slot.state == SlotState::completed) result.error = AssetStreamError::already_completed;
        else { slot.cancel_requested = true; result = {}; }
        break;
    }
    const auto released = unlock();
    return released ? result : released;
}

AssetStreamStatus AssetStream::poll_completion(AssetReadCompletion& completion,
                                             bool& available) noexcept {
    completion = {};
    available = false;
    if (!primitives_ready_.load()) return {AssetStreamError::not_started};
    auto status = lock();
    if (!status) return status;
    if (complete_count_) {
        const auto index = completed_[complete_head_];
        complete_head_ = (complete_head_ + 1) % max_jobs;
        --complete_count_;
        completion = slots_[index].completion;
        slots_[index] = {};
        available = true;
    }
    const auto released = unlock();
    if (!released) return released;
    if (fatal_error_.load()) return {AssetStreamError::worker_failure, fatal_error_.load()};
    return {};
}

AssetStreamStatus AssetStream::counts(AssetStreamCounts& result) noexcept {
    result = {};
    if (!primitives_ready_.load()) return {AssetStreamError::not_started};
    auto status = lock();
    if (!status) return status;
    result.queued = pending_count_;
    result.completed = complete_count_;
    for (const auto& slot : slots_)
        if (slot.state == SlotState::running) ++result.running;
    return unlock();
}

void AssetStream::complete_locked(std::size_t index,
                                  const AssetReadCompletion& completion) noexcept {
    auto& slot = slots_[index];
    slot.completion = completion;
    slot.state = SlotState::completed;
    // Capacity cannot overflow: each completion occupies an already-reserved
    // slot, and there are only max_jobs total queued/running/completed slots.
    completed_[(complete_head_ + complete_count_) % max_jobs] = index;
    ++complete_count_;
}

AssetReadCompletion AssetStream::perform(std::size_t index, const AssetReadRequest& request,
                                        std::uint64_t token) noexcept {
    AssetReadCompletion result{};
    result.token = token;
    bool first = true; // Zero-byte requests still perform one real range/key validation.
    while (first || result.bytes_transferred < request.length) {
        auto status = lock();
        if (!status) { result.pthread_error = status.pthread_error; return result; }
        const bool cancel_requested = stopping_ || slots_[index].cancel_requested;
        const int failure = fatal_error_.load();
        status = unlock();
        if (!status) { result.pthread_error = status.pthread_error; return result; }
        if (failure) { result.pthread_error = failure; return result; }
        if (cancel_requested) { result.result = AssetReadResult::cancelled; return result; }
        const auto remaining = request.length - result.bytes_transferred;
        const auto chunk = remaining < chunk_bytes ? remaining : chunk_bytes;
        auto* buffer = static_cast<std::uint8_t*>(request.buffer);
        void* destination = buffer ? buffer + result.bytes_transferred : nullptr;
        result.archive_status = archives_->read_hash(request.hash1, request.hash2,
            request.entry_offset + result.bytes_transferred, destination, chunk);
        result.archive_call_made = true;
        first = false;
        // ArchiveSet's bounded backend reports exactly the actually copied prefix.
        const auto transferred = result.archive_status.bytes_transferred;
        if (transferred > chunk) {
            // A violated backend contract cannot establish a larger reliable
            // prefix. Retain only bytes confirmed before this call.
            result.result = AssetReadResult::worker_failure;
            return result;
        }
        result.bytes_transferred += transferred;
        if (!result.archive_status || transferred != chunk) {
            result.result = AssetReadResult::read_failure;
            return result;
        }
    }
    // Do not relabel a completed final read as cancelled merely because a cancel
    // request arrived while that last synchronous read was already in progress.
    result.result = AssetReadResult::success;
    return result;
}

void* AssetStream::worker_entry(void* context) noexcept {
    static_cast<AssetStream*>(context)->worker_loop();
    return nullptr;
}
void AssetStream::worker_loop() noexcept {
    for (;;) {
        if (!lock()) return;
        while (!pending_count_ && !stopping_ && !fatal_error_.load()) {
            const int error = pthread_cond_wait(&condition_, &mutex_);
            if (error) {
                // Mutex/condition ownership is not assumed after a broken wait.
                // No speculative unlock/join recovery; owner must retain lifetime.
                fatal(error, true);
                return;
            }
        }
        if (!pending_count_) { (void)unlock(); return; }
        const auto index = pending_[pending_head_];
        pending_head_ = (pending_head_ + 1) % max_jobs;
        --pending_count_;
        slots_[index].state = SlotState::running;
        const auto request = slots_[index].request;
        const auto token = slots_[index].token;
        if (!unlock()) return;
        const auto completion = perform(index, request, token);
        if (!lock()) return; // Unrecoverable sync retains caller lifetimes.
        complete_locked(index, completion);
        if (!unlock()) return;
    }
}

AssetStreamStatus AssetStream::stop() noexcept {
    // start/stop must be externally serialized; no handle read races with create.
    if (!join_pending_.load()) {
        if (sync_broken_.load())
            return {AssetStreamError::unrecoverable_sync, fatal_error_.load()};
        return {};
    }
    if (pthread_equal(pthread_self(), worker_)) return {AssetStreamError::invalid_argument};
    auto status = lock();
    if (!status) return status;
    stopping_ = true;
    state_.store(AssetStreamState::stopping);
    const int wake_error = pthread_cond_broadcast(&condition_);
    if (wake_error) fatal(wake_error, false);
    status = unlock();
    if (!status) return status;
    if (wake_error) return thread_error(wake_error); // No speculative lost-wake join.
    const int joined = pthread_join(worker_, nullptr);
    if (joined) { fatal(joined, false); return thread_error(joined); }
    join_pending_.store(false); // Native join established worker completion.
    status = lock();
    if (!status) return status;
    archives_ = nullptr;
    state_.store(fatal_error_.load() ? AssetStreamState::faulted : AssetStreamState::stopped);
    return unlock();
}

AssetStreamStatus AssetStream::release() noexcept {
    if (join_pending_.load()) return {AssetStreamError::already_started};
    if (sync_broken_.load())
        return {AssetStreamError::unrecoverable_sync, fatal_error_.load()};
    if (primitives_ready_.load()) {
        auto status = lock();
        if (!status) return status;
        bool outstanding = pending_count_ || complete_count_;
        for (const auto& slot : slots_) outstanding |= slot.state != SlotState::free;
        status = unlock();
        if (!status) return status;
        if (outstanding) return {AssetStreamError::completions_pending};
    }
    // No concurrent API calls are permitted during release. A partially
    // successful release remains retryable without re-destroying primitives.
    primitives_ready_.store(false);
    if (condition_initialized_) {
        const int error = pthread_cond_destroy(&condition_);
        if (error) return thread_error(error);
        condition_initialized_ = false;
    }
    if (mutex_initialized_) {
        const int error = pthread_mutex_destroy(&mutex_);
        if (error) return thread_error(error);
        mutex_initialized_ = false;
    }
    return {};
}
} // namespace cedar
