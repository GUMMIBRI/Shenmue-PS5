// SPDX-License-Identifier: GPL-3.0-only
#include "cedar/startup_scene.hpp"
#include <limits>

namespace cedar { namespace {
struct Span {std::uintptr_t begin{},end{};};
bool span(const void* p,std::size_t n,std::size_t size,std::size_t alignment,Span& s) noexcept {
    const auto max=std::numeric_limits<std::uintptr_t>::max();
    s.begin=reinterpret_cast<std::uintptr_t>(p);
    if((n && (!p || s.begin%alignment)) || n>max/size || n*size>max-s.begin) return false;
    s.end=s.begin+n*size;return true;
}
bool overlap(Span a,Span b) noexcept {return a.begin<a.end && b.begin<b.end && a.begin<b.end && b.begin<a.end;}
bool same(const TadEntry& a,const TadEntry& b) noexcept {
    return a.hash1==b.hash1 && a.hash2==b.hash2 && a.unknown==b.unknown && a.offset==b.offset && a.size==b.size;
}
StartupSceneStatus fail(StartupSceneError e) noexcept {StartupSceneStatus s;s.error=e;return s;}
} // namespace
bool StartupScene::ready() const noexcept {return ready_ && owner_ && owner_->matches(lease_);}
StartupSceneStatus StartupScene::close() noexcept {
    view_={};ready_=false;
    if(!owner_) return {};
    const auto released=owner_->release(lease_);
    if(!released) {auto s=fail(StartupSceneError::release_failure);s.release=released;return s;}
    owner_=nullptr;lease_={};return {};
}
StartupSceneStatus StartupScene::abandon(StartupSceneStatus result) noexcept {
    const auto cleanup=close();
    if(!cleanup) result.release=cleanup.release; // Preserve original read/parse failure.
    return result;
}
StartupSceneStatus StartupScene::open(StartupResources& resources,std::size_t requirement,
    StartupSceneEncoding encoding,const StartupSceneBuffers& buffers) noexcept {
    if(owner_ || ready_) return fail(StartupSceneError::invalid_state);
    if(!resources.ready()) return fail(StartupSceneError::invalid_state);
    if(encoding!=StartupSceneEncoding::plain_scene && encoding!=StartupSceneEncoding::gzip_scene)
        return fail(StartupSceneError::invalid_encoding);
    if(!buffers.stored_capacity || buffers.stored_capacity>SceneResourceView::max_bytes ||
       !buffers.record_capacity || buffers.record_capacity>SceneResourceView::max_records ||
       (encoding==StartupSceneEncoding::plain_scene && (buffers.decoded || buffers.decoded_capacity || buffers.gzip)) ||
       (encoding==StartupSceneEncoding::gzip_scene && (!buffers.gzip || !buffers.decoded_capacity || buffers.decoded_capacity>gzip_output_limit)))
        return fail(StartupSceneError::invalid_buffer);
    Span spans[7]{};
    if(!span(this,1,sizeof(*this),alignof(StartupScene),spans[0]) ||
       !span(&resources,1,sizeof(resources),alignof(StartupResources),spans[1]) ||
       !span(&buffers,1,sizeof(buffers),alignof(StartupSceneBuffers),spans[2]) ||
       !span(buffers.stored,buffers.stored_capacity,1,1,spans[3]) ||
       !span(buffers.decoded,buffers.decoded_capacity,1,1,spans[4]) ||
       !span(buffers.gzip,buffers.gzip?1:0,sizeof(GzipWorkspace),alignof(GzipWorkspace),spans[5]) ||
       !span(buffers.records,buffers.record_capacity,sizeof(ScenePlacementRecord),alignof(ScenePlacementRecord),spans[6]))
        return fail(StartupSceneError::invalid_buffer);
    for(unsigned i=0;i<7;++i) for(unsigned j=i+1;j<7;++j)
        if(overlap(spans[i],spans[j])) return fail(StartupSceneError::invalid_buffer);
    // Also protect borrowed external indexes/root, unavailable through a lease's
    // ArchiveSet interface. Owner supplies the complete retained-span guard.
    for(unsigned i=0;i<7;++i) if(i!=1 &&
        !resources.storage_disjoint(reinterpret_cast<const void*>(spans[i].begin),spans[i].end-spans[i].begin))
        return fail(StartupSceneError::invalid_buffer);
    const auto acquired=resources.acquire();
    if(!acquired.status) {auto s=fail(StartupSceneError::resource_failure);s.resources=acquired.status;return s;}
    owner_=&resources;lease_=acquired.lease;
    const auto selected=resources.binding(lease_,requirement);
    if(!selected.status) {auto s=fail(StartupSceneError::resource_failure);s.resources=selected.status;return abandon(s);}
    const auto& binding=selected.binding;
    const auto& r=binding.requirement;
    StartupSceneStatus result;
    if(r.length>std::numeric_limits<std::size_t>::max()) return abandon(fail(StartupSceneError::insufficient_capacity));
    result.required_stored_bytes=static_cast<std::size_t>(r.length);
    if(r.length>buffers.stored_capacity) {result.error=StartupSceneError::insufficient_capacity;return abandon(result);}
    TadEntry current{};
    result.archive=lease_.archives->stat_hash(r.hash1,r.hash2,current);
    if(!result.archive) {result.error=StartupSceneError::read_failure;return abandon(result);}
    if(result.archive.archive_index!=binding.archive_index || !same(current,binding.entry) ||
       r.offset>current.size || r.length>current.size-r.offset) {
        result.error=StartupSceneError::binding_changed;return abandon(result);
    }
    result.archive=lease_.archives->read_hash(r.hash1,r.hash2,r.offset,buffers.stored,result.required_stored_bytes);
    result.stored_bytes_read=result.archive.bytes_transferred;
    if(!result.archive) {result.error=StartupSceneError::read_failure;return abandon(result);}
    if(result.archive.archive_index!=binding.archive_index) {result.error=StartupSceneError::binding_changed;return abandon(result);}
    const auto* source=static_cast<const std::uint8_t*>(buffers.stored);
    if(r.kind==StartupStoredKind::exact_signature)
        for(unsigned i=0;i<r.signature_size;++i) if(source[i]!=r.signature[i]) {
            result.error=StartupSceneError::signature_mismatch;return abandon(result);
        }
    auto length=result.required_stored_bytes;
    if(encoding==StartupSceneEncoding::gzip_scene) {
        result.gzip=decode_gzip(source,length,buffers.decoded,buffers.decoded_capacity,*buffers.gzip);
        result.decoded_bytes=result.gzip.output_written;
        if(!result.gzip) {result.error=StartupSceneError::gzip_failure;return abandon(result);}
        source=static_cast<const std::uint8_t*>(buffers.decoded);length=result.decoded_bytes;
    }
    result.scene=view_.parse(source,length,buffers.records,buffers.record_capacity);
    if(!result.scene) {result.error=StartupSceneError::parse_failure;return abandon(result);}
    if(view_.record_count()==0) {result.error=StartupSceneError::no_placements;return abandon(result);}
    ready_=true;return result;
}
} // namespace cedar
