// SPDX-License-Identifier: GPL-3.0-only
// Concrete resource consumer of the authored scene recognition profile.
#pragma once
#include "cedar/startup_resources.hpp"
#include "cedar/scene_resource.hpp"
#include "cedar/gzip_decode.hpp"

namespace cedar {
enum class StartupSceneEncoding : std::uint8_t { unspecified, plain_scene, gzip_scene };
struct StartupSceneBuffers {
    void* stored{};std::size_t stored_capacity{};
    void* decoded{};std::size_t decoded_capacity{};
    GzipWorkspace* gzip{};
    ScenePlacementRecord* records{};std::size_t record_capacity{};
};
enum class StartupSceneError : std::uint8_t {
    none,invalid_state,invalid_buffer,invalid_encoding,resource_failure,
    insufficient_capacity,binding_changed,read_failure,signature_mismatch,
    gzip_failure,parse_failure,no_placements,release_failure
};
struct StartupSceneStatus {
    StartupSceneError error{StartupSceneError::none};
    StartupResourcesStatus resources{},release{};
    ArchiveSetStatus archive{};
    GzipStatus gzip{};
    SceneResourceResult scene{};
    std::size_t required_stored_bytes{},stored_bytes_read{},decoded_bytes{};
    explicit operator bool() const noexcept {return error==StartupSceneError::none;}
};
// Immutable input bytes/records and live StartupResources must outlive this
// scene. Explicit close before destruction; no automatic borrowed-owner calls.
class StartupScene final {
public:
    StartupScene() noexcept=default;
    StartupScene(const StartupScene&)=delete;
    StartupScene& operator=(const StartupScene&)=delete;
    StartupSceneStatus open(StartupResources&,std::size_t requirement_index,
        StartupSceneEncoding,const StartupSceneBuffers&) noexcept;
    StartupSceneStatus close() noexcept;
    bool ready() const noexcept;
    // Returned view/records/strings are invalid after close or buffer mutation.
    const SceneResourceView* view() const noexcept {return ready()?&view_:nullptr;}
    std::uint64_t generation() const noexcept {return lease_.generation;}
    bool release_pending() const noexcept {return owner_!=nullptr && !ready_;}
private:
    StartupSceneStatus abandon(StartupSceneStatus) noexcept;
    StartupResources* owner_{};
    StartupResourcesLease lease_{};
    SceneResourceView view_{};
    bool ready_{};
};
} // namespace cedar
