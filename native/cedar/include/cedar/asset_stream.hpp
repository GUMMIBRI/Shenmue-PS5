// Original bounded pthread archive streaming; no game scheduling assumptions.
#pragma once
#include "cedar/archive_set.hpp"
#include <pthread.h>
#include <atomic>

namespace cedar {
enum class AssetStreamError : std::uint8_t {
    none, invalid_argument, not_started, already_started, stopping, queue_full,
    not_found, already_completed, token_exhausted, completions_pending,
    pthread_failure, unrecoverable_sync, worker_failure
};
struct AssetStreamStatus {
    AssetStreamError error{AssetStreamError::none};
    int pthread_error{};
    explicit operator bool() const noexcept { return error == AssetStreamError::none; }
};
enum class AssetStreamState : std::uint8_t { stopped, running, stopping, faulted };
struct AssetReadRequest {
    std::uint32_t hash1{}, hash2{};
    std::uint64_t entry_offset{};
    void* buffer{};
    std::size_t length{};
};
enum class AssetReadResult : std::uint8_t { success, cancelled, read_failure, worker_failure };
struct AssetReadCompletion {
    std::uint64_t token{};
    AssetReadResult result{AssetReadResult::worker_failure};
    std::size_t bytes_transferred{};
    ArchiveSetStatus archive_status{}; // Last completed archive call, if any.
    bool archive_call_made{};
    int pthread_error{};
};
struct AssetStreamCounts {
    std::size_t queued{}, running{}, completed{};
};

// EXPLICIT-CLOSE LIFETIME: once a worker is created, destruction is legal only
// after successful stop(), with no concurrent API call. start() can report an
// unlock failure AFTER creating a worker: inspect join_pending() even on error.
// A failed join retains
// all borrows and requires explicit retry; destructor does not detach/terminate
// or turn a failed stop into safe teardown. Keep object/buffers/backing assets
// alive in unrecoverable synchronization failure. No worker callbacks or SDL.
class AssetStream final {
public:
    static constexpr std::size_t max_jobs = 32;
    static constexpr std::size_t chunk_bytes = 1024u * 1024u;
    AssetStream() noexcept = default;
    ~AssetStream();
    AssetStream(const AssetStream&) = delete;
    AssetStream& operator=(const AssetStream&) = delete;
    // Serialize start/stop on the owning thread. Archive list/readers/mounts
    // must remain immutable and alive until successful stop returns.
    // A failed start is not proof that no worker exists. If join_pending() is
    // true, retain this object and ALL borrows, regardless of returned status.
    // The actual synchronization/join failure contract still applies; a failed
    // return alone never establishes safe destruction or a creation rollback.
    AssetStreamStatus start(const ArchiveSet& archives) noexcept;
    // Nonzero output token means the request was accepted, EVEN if a later
    // mutex-unlock failure is reported. Retain its buffer in that case too.
    AssetStreamStatus submit(const AssetReadRequest& request, std::uint64_t& token) noexcept;
    // Success requests cooperative cancellation; completion may still succeed
    // if its final read already completed. Never labels an existing completion
    // cancelled. Already-completed tokens remain until owner polls the record.
    AssetStreamStatus cancel(std::uint64_t token) noexcept;
    AssetStreamStatus poll_completion(AssetReadCompletion& completion, bool& available) noexcept;
    AssetStreamStatus counts(AssetStreamCounts& counts) noexcept;
    // Cancels pending/in-progress work between reads; joins exactly one worker.
    // Completed records remain pollable after stop and must be drained to restart.
    AssetStreamStatus stop() noexcept;
    // Optional observable destruction of pthread primitives after stop; drain
    // completions first. Serialize with every other call, including lifecycle.
    AssetStreamStatus release() noexcept;
    AssetStreamState state() const noexcept { return state_.load(); }
    bool join_pending() const noexcept { return join_pending_.load(); }
    int fatal_pthread_error() const noexcept { return fatal_error_.load(); }
private:
    enum class SlotState : std::uint8_t { free, queued, running, completed };
    struct Slot {
        SlotState state{SlotState::free};
        AssetReadRequest request{};
        AssetReadCompletion completion{};
        std::uint64_t token{};
        bool cancel_requested{};
    };
    static void* worker_entry(void* context) noexcept;
    void worker_loop() noexcept;
    AssetReadCompletion perform(std::size_t index, const AssetReadRequest& request,
                                std::uint64_t token) noexcept;
    AssetStreamStatus lock() noexcept;
    AssetStreamStatus unlock() noexcept;
    void fatal(int error, bool broken) noexcept;
    void complete_locked(std::size_t index, const AssetReadCompletion& completion) noexcept;
    pthread_mutex_t mutex_{};
    pthread_cond_t condition_{};
    pthread_t worker_{};
    std::atomic<bool> primitives_ready_{false}, join_pending_{false}, sync_broken_{false};
    std::atomic<int> fatal_error_{0};
    std::atomic<AssetStreamState> state_{AssetStreamState::stopped};
    const ArchiveSet* archives_{};
    Slot slots_[max_jobs]{};
    std::size_t pending_[max_jobs]{}, completed_[max_jobs]{};
    std::size_t pending_head_{}, pending_count_{}, complete_head_{}, complete_count_{};
    std::uint64_t next_token_{1};
    bool stopping_{}, mutex_initialized_{}, condition_initialized_{};
};
} // namespace cedar
