// Original explicit resource ownership; no game boot or resource naming policy.
#pragma once
#include "cedar/archive_mount.hpp"
#include "cedar/archive_set.hpp"

namespace cedar {
struct StartupArchivePlan {
    std::string_view tad_path, tac_path;
    void* index_storage{};
    std::size_t index_capacity{};
};
enum class StartupStoredKind : std::uint8_t { unspecified, raw_stored_bytes, exact_signature };
struct StartupResourceRequirement {
    std::uint64_t label{}; // Opaque caller profile label, unique within this plan.
    std::uint32_t hash1{},hash2{};
    std::uint64_t offset{},length{}; // Nonempty raw entry subrange, at most 64MiB.
    bool require_entry_size{};
    std::uint64_t entry_size{};
    StartupStoredKind kind{StartupStoredKind::unspecified};
    std::uint8_t signature[16]{},signature_size{}; // Exact bytes at subrange start.
};
struct StartupResourceBinding {
    StartupResourceRequirement requirement{};
    TadEntry entry{};
    std::size_t archive_index{ArchiveSetStatus::no_archive};
};
enum class StartupResourcesError : std::uint8_t {
    none,invalid_state,invalid_plan,invalid_buffer,mount_failure,archive_failure,
    requirement_mismatch,signature_mismatch,borrowed,invalid_lease,limit,overflow
};
struct StartupResourcesStatus {
    StartupResourcesError error{StartupResourcesError::none};
    std::size_t archive_index{ArchiveSetStatus::no_archive};
    std::size_t requirement_index{static_cast<std::size_t>(-1)};
    ArchiveMountStatus mount{};
    ArchiveSetStatus archive{};
    std::uint64_t requirement_bytes_read{}; // Actual prefix read for failing requirement.
    explicit operator bool() const noexcept {return error==StartupResourcesError::none;}
};
class StartupResources;
struct StartupResourcesLease {
    const StartupResources* owner{};
    const ArchiveSet* archives{};
    std::uint64_t generation{},id{};
};
struct StartupResourcesAcquire {StartupResourcesStatus status{}; StartupResourcesLease lease{};};
struct StartupResourcesBindingResult {StartupResourcesStatus status{}; StartupResourceBinding binding{};};
// Explicit close before destruction, after every consumer (including workers)
// stops and releases its lease. Copying a token does not create another lease.
class StartupResources final {
public:
    static constexpr std::size_t max_archives=32,max_requirements=64,max_leases=32;
    StartupResources() noexcept=default;
    StartupResources(const StartupResources&)=delete;
    StartupResources& operator=(const StartupResources&)=delete;
    StartupResourcesStatus prepare(const PosixReadOnlyAssets&,
        const StartupArchivePlan*,std::size_t archive_count,
        const StartupResourceRequirement*,std::size_t requirement_count) noexcept;
    StartupResourcesAcquire acquire() noexcept;
    StartupResourcesStatus release(StartupResourcesLease) noexcept;
    bool matches(StartupResourcesLease) const noexcept;
    // Checks writable consumer storage against this owner and retained external
    // asset/index objects. Synchronize with prepare/close; false when not ready.
    bool storage_disjoint(const void*,std::size_t) const noexcept;
    StartupResourcesBindingResult binding(StartupResourcesLease,std::size_t index) const noexcept;
    StartupResourcesStatus close() noexcept;
    bool ready() const noexcept {return ready_;}
    std::uint64_t generation() const noexcept {return generation_;}
    std::size_t requirement_count() const noexcept {return ready_?requirement_count_:0;}
private:
    void discard() noexcept;
    ArchiveMount mounts_[max_archives]{};
    const TacReader* readers_[max_archives]{};
    ArchiveSet archives_{};
    StartupResourceBinding bindings_[max_requirements]{};
    std::uint64_t leases_[max_leases]{};
    std::uint64_t generation_{},next_lease_{};
    std::size_t requirement_count_{};
    bool ready_{};
    const PosixReadOnlyAssets* assets_{};
    struct IndexStorage {const void* data{};std::size_t bytes{};};
    IndexStorage index_storage_[max_archives]{};
    std::size_t archive_count_{};
};
} // namespace cedar
