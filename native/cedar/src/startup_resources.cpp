#include "cedar/startup_resources.hpp"
#include <limits>

namespace cedar { namespace {
using E=StartupResourcesError;
struct Span {std::uintptr_t begin{},end{};};
bool span(const void* p,std::size_t n,std::size_t size,std::size_t alignment,Span& s) noexcept {
    const auto max=std::numeric_limits<std::uintptr_t>::max();
    s.begin=reinterpret_cast<std::uintptr_t>(p);
    if((n && (!p || s.begin%alignment)) || n>max/size || n*size>max-s.begin) return false;
    s.end=s.begin+n*size; return true;
}
bool overlap(Span a,Span b) noexcept {return a.begin<a.end && b.begin<b.end && a.begin<b.end && b.begin<a.end;}
StartupResourcesStatus failure(E e,std::size_t a=ArchiveSetStatus::no_archive,
                              std::size_t r=static_cast<std::size_t>(-1)) noexcept {
    StartupResourcesStatus out; out.error=e;out.archive_index=a;out.requirement_index=r;return out;
}
} // namespace
void StartupResources::discard() noexcept {
    archives_.unbind();
    for(std::size_t i=0;i<max_archives;++i) {mounts_[i].close();readers_[i]=nullptr;}
    for(auto& b:bindings_) b={};
    requirement_count_=0;ready_=false;
    assets_=nullptr;archive_count_=0;
    for(auto& s:index_storage_)s={};
}
StartupResourcesStatus StartupResources::prepare(const PosixReadOnlyAssets& assets,
    const StartupArchivePlan* plans,std::size_t count,
    const StartupResourceRequirement* required,std::size_t required_count) noexcept {
    if(ready_) return failure(E::invalid_state);
    if(!assets.mounted() || count==0 || count>max_archives || required_count==0 || required_count>max_requirements)
        return failure(E::invalid_plan);
    if(generation_==std::numeric_limits<std::uint64_t>::max()) return failure(E::overflow);
    Span immutable[4+2*max_archives]{},storage[max_archives]{};
    if(!span(this,1,sizeof(*this),alignof(StartupResources),immutable[0]) ||
       !span(&assets,1,sizeof(assets),alignof(PosixReadOnlyAssets),immutable[1]) ||
       !span(plans,count,sizeof(*plans),alignof(StartupArchivePlan),immutable[2]) ||
       !span(required,required_count,sizeof(*required),alignof(StartupResourceRequirement),immutable[3])) return failure(E::invalid_buffer);
    // Descriptor/path storage is read throughout preparation; none may be
    // overwritten by index reads or overlap the mutating resource owner.
    for(unsigned i=1;i<4;++i) if(overlap(immutable[0],immutable[i])) return failure(E::invalid_buffer);
    constexpr auto capacity_limit=TadIndex::header_bytes+static_cast<std::size_t>(TadIndex::max_entries)*TadIndex::entry_bytes;
    for(std::size_t i=0;i<count;++i) {
        const auto& p=plans[i];
        if(!p.index_capacity || p.index_capacity>capacity_limit ||
           !span(p.index_storage,p.index_capacity,1,1,storage[i]) ||
           !span(p.tad_path.data(),p.tad_path.size(),1,1,immutable[4+2*i]) ||
           !span(p.tac_path.data(),p.tac_path.size(),1,1,immutable[5+2*i])) return failure(E::invalid_buffer,i);
        if(p.tad_path.empty() || p.tac_path.empty() ||
           p.tad_path.size()>PosixReadOnlyAssets::max_path_bytes || p.tac_path.size()>PosixReadOnlyAssets::max_path_bytes)
            return failure(E::invalid_plan,i);
        if(overlap(immutable[0],immutable[4+2*i]) || overlap(immutable[0],immutable[5+2*i])) return failure(E::invalid_buffer,i);
    }
    for(std::size_t i=0;i<count;++i) {
        for(std::size_t j=0;j<4+2*count;++j) if(overlap(storage[i],immutable[j])) return failure(E::invalid_buffer,i);
        for(std::size_t j=0;j<i;++j) if(overlap(storage[i],storage[j])) return failure(E::invalid_buffer,i);
    }
    for(std::size_t i=0;i<required_count;++i) {
        const auto& r=required[i];
        if(r.length==0 || r.length>PosixReadOnlyAssets::max_read_bytes ||
           (r.kind!=StartupStoredKind::raw_stored_bytes && r.kind!=StartupStoredKind::exact_signature) ||
           (r.kind==StartupStoredKind::raw_stored_bytes && r.signature_size!=0) ||
           (r.kind==StartupStoredKind::exact_signature && (r.signature_size==0 || r.signature_size>16 || r.signature_size>r.length)))
            return failure(E::invalid_plan,ArchiveSetStatus::no_archive,i);
        for(std::size_t j=0;j<i;++j) if(required[j].label==r.label) return failure(E::invalid_plan,ArchiveSetStatus::no_archive,i);
    }
    for(std::size_t i=0;i<count;++i) {
        const auto result=mounts_[i].open(assets,plans[i].tad_path,plans[i].tac_path,plans[i].index_storage,plans[i].index_capacity);
        if(!result) {auto out=failure(E::mount_failure,i);out.mount=result;discard();return out;}
        readers_[i]=mounts_[i].reader();
    }
    auto archive=archives_.bind(readers_,count);
    if(!archive) {auto out=failure(E::archive_failure,archive.archive_index);out.archive=archive;discard();return out;}
    std::uint8_t chunk[4096]{};
    for(std::size_t i=0;i<required_count;++i) {
        const auto& r=required[i];TadEntry entry{};
        archive=archives_.stat_hash(r.hash1,r.hash2,entry);
        if(!archive) {auto out=failure(E::archive_failure,archive.archive_index,i);out.archive=archive;discard();return out;}
        const auto selected=archive.archive_index;
        if((r.require_entry_size && entry.size!=r.entry_size) || r.offset>entry.size || r.length>entry.size-r.offset) {
            auto out=failure(E::requirement_mismatch,selected,i);discard();return out;
        }
        std::uint64_t done=0;
        while(done<r.length) {
            const auto remaining=r.length-done;
            const auto bytes=static_cast<std::size_t>(remaining<sizeof(chunk)?remaining:sizeof(chunk));
            archive=archives_.read_hash(r.hash1,r.hash2,r.offset+done,chunk,bytes);
            if(!archive) {auto out=failure(E::archive_failure,archive.archive_index,i);out.archive=archive;
                out.requirement_bytes_read=done+archive.bytes_transferred;discard();return out;}
            if(archive.archive_index!=selected) {auto out=failure(E::requirement_mismatch,archive.archive_index,i);
                out.requirement_bytes_read=done+bytes;discard();return out;}
            if(done==0 && r.kind==StartupStoredKind::exact_signature)
                for(unsigned j=0;j<r.signature_size;++j) if(chunk[j]!=r.signature[j]) {
                    auto out=failure(E::signature_mismatch,selected,i);out.requirement_bytes_read=bytes;discard();return out;
                }
            done+=bytes;
        }
        bindings_[i]={r,entry,selected};
    }
    assets_=&assets;archive_count_=count;
    for(std::size_t i=0;i<count;++i)index_storage_[i]={plans[i].index_storage,plans[i].index_capacity};
    requirement_count_=required_count;++generation_;ready_=true;return {};
}
bool StartupResources::storage_disjoint(const void* data,std::size_t bytes) const noexcept {
    if(!ready_ || !assets_)return false;
    Span candidate{},retained{};
    if(!span(data,bytes,1,1,candidate) || !span(this,1,sizeof(*this),alignof(StartupResources),retained) || overlap(candidate,retained))return false;
    if(!span(assets_,1,sizeof(*assets_),alignof(PosixReadOnlyAssets),retained) || overlap(candidate,retained))return false;
    for(std::size_t i=0;i<archive_count_;++i)
        if(!span(index_storage_[i].data,index_storage_[i].bytes,1,1,retained) || overlap(candidate,retained))return false;
    return true;
}
StartupResourcesAcquire StartupResources::acquire() noexcept {
    if(!ready_) return {failure(E::invalid_state),{}};
    if(next_lease_==std::numeric_limits<std::uint64_t>::max()) return {failure(E::overflow),{}};
    for(auto& id:leases_) if(id==0) {id=++next_lease_;return {{},{this,&archives_,generation_,id}};}
    return {failure(E::limit),{}};
}
bool StartupResources::matches(StartupResourcesLease lease) const noexcept {
    if(!ready_ || lease.owner!=this || lease.archives!=&archives_ || lease.generation!=generation_ || lease.id==0) return false;
    for(auto id:leases_) if(id==lease.id) return true;
    return false;
}
StartupResourcesStatus StartupResources::release(StartupResourcesLease lease) noexcept {
    if(!matches(lease)) return failure(E::invalid_lease);
    for(auto& id:leases_) if(id==lease.id) {id=0;return {};}
    return failure(E::invalid_lease);
}
StartupResourcesBindingResult StartupResources::binding(StartupResourcesLease lease,std::size_t index) const noexcept {
    if(!matches(lease)) return {failure(E::invalid_lease),{}};
    if(index>=requirement_count_) return {failure(E::invalid_plan),{}};
    return {{},bindings_[index]};
}
StartupResourcesStatus StartupResources::close() noexcept {
    for(auto id:leases_) if(id) return failure(E::borrowed);
    discard();return {};
}
} // namespace cedar
