#include "cedar/spsd_voice.hpp"
#include <SDL3/SDL_error.h>
#include <cmath>
#include <limits>
namespace cedar {
namespace {
using E=SpsdVoiceError;
bool separate(const void* p,std::size_t n,const void* q,std::size_t m) {
    const auto a=reinterpret_cast<std::uintptr_t>(p),b=reinterpret_cast<std::uintptr_t>(q);
    const auto max=std::numeric_limits<std::uintptr_t>::max();
    return (!n || p) && (!m || q) && n<=max-a && m<=max-b &&
        (!n || !m || a+n<=b || b+m<=a);
}
}
SpsdVoice::~SpsdVoice(){stop();}
void SpsdVoice::stop() noexcept {
    if(converter_) SDL_DestroyAudioStream(converter_);
    converter_=nullptr;decoder_.stop();source_=nullptr;source_bytes_=0;
    output_channels_=0;flushed_=false;faulted_=false;error_[0]=0;
}
bool SpsdVoice::valid_buffer(const void* p,std::size_t count,std::size_t element,std::size_t alignment) const noexcept {
    if(count>std::numeric_limits<std::size_t>::max()/element ||
       (count && reinterpret_cast<std::uintptr_t>(p)%alignment)) return false;
    const auto bytes=count*element;
    return separate(p,bytes,this,sizeof(*this)) && separate(p,bytes,source_,source_bytes_);
}
SpsdVoiceResult SpsdVoice::native_failure(bool consumption) noexcept {
    const char* s=SDL_GetError();std::size_t i=0;
    if(s) {
        for(;i+1<sizeof(error_) && s[i];++i) error_[i]=s[i];
    }
    error_[i]=0;
    faulted_=true;SpsdVoiceResult r;r.error=E::converter_failure;r.converter_consumption_uncertain=consumption;return r;
}
SpsdVoiceResult SpsdVoice::start(const SpsdView& view,std::uint32_t channels) noexcept {
    if(converter_ || faulted_) return {E::invalid_state};
    if(channels<1 || channels>2 || !view.parsed() ||
       !separate(&view,sizeof(view),this,sizeof(*this)) ||
       !separate(view.source_data(),view.source_size(),this,sizeof(*this))) return {E::invalid_argument};
    SpsdVoiceResult r;r.decoder=decoder_.start(view);
    if(!r.decoder) {r.error=E::decoder_failure;return r;}
    const SDL_AudioSpec input{SDL_AUDIO_S16,static_cast<int>(decoder_.channels()),static_cast<int>(decoder_.sample_rate())};
    const SDL_AudioSpec output{SDL_AUDIO_F32,static_cast<int>(channels),48000};
    converter_=SDL_CreateAudioStream(&input,&output);
    if(!converter_) {decoder_.stop();return native_failure();}
    source_=view.source_data();source_bytes_=view.source_size();output_channels_=channels;
    return r;
}
SpsdVoiceResult SpsdVoice::pump(std::size_t frames,std::int16_t* pcm,std::size_t capacity,
    std::int16_t* mono,std::size_t scratch) noexcept {
    if(!converter_ || faulted_ || flushed_) return {E::invalid_state};
    if(!frames || frames>SpsdAicaStream::max_frames_per_call || frames>decoder_.frames_remaining() ||
       capacity<frames*decoder_.channels() || !scratch) return {E::invalid_argument};
    if(!valid_buffer(pcm,capacity,sizeof(*pcm),alignof(std::int16_t)) ||
       !valid_buffer(mono,scratch,sizeof(*mono),alignof(std::int16_t)) ||
       !separate(pcm,capacity*sizeof(*pcm),mono,scratch*sizeof(*mono))) return {E::invalid_buffer};
    const std::size_t bytes=frames*decoder_.channels()*sizeof(*pcm);
    const int queued=SDL_GetAudioStreamQueued(converter_);
    if(queued<0) return native_failure();
    if(static_cast<std::size_t>(queued)>max_queued_bytes || bytes>max_queued_bytes-static_cast<std::size_t>(queued)) return {E::queue_full};
    SpsdVoiceResult r;r.decoder=decoder_.decode_frames(frames,pcm,capacity,mono,scratch);
    r.output_modified=r.decoder.output_modified;
    if(!r.decoder || !r.decoder.output_valid) {r.error=E::decoder_failure;faulted_=true;return r;}
    r.decoder_advanced=true;
    if(!SDL_PutAudioStreamData(converter_,pcm,static_cast<int>(bytes))) {
        auto failure=native_failure();failure.decoder=r.decoder;failure.decoder_advanced=true;
        failure.output_modified=r.output_modified;failure.queue_acceptance_uncertain=true;return failure;
    }
    r.frames=frames;return r;
}
SpsdVoiceResult SpsdVoice::read(float* output,std::size_t capacity) noexcept {
    if(!converter_ || faulted_) return {E::invalid_state};
    if(!capacity || capacity%output_channels_ || capacity>max_queued_bytes/sizeof(float)) return {E::invalid_argument};
    if(!valid_buffer(output,capacity,sizeof(float),alignof(float))) return {E::invalid_buffer};
    const int bytes=SDL_GetAudioStreamData(converter_,output,static_cast<int>(capacity*sizeof(float)));
    if(bytes<0) {auto r=native_failure(true);r.output_modified=true;return r;}
    SpsdVoiceResult r;r.converted_bytes=static_cast<std::size_t>(bytes);r.output_modified=bytes!=0;
    if(r.converted_bytes>capacity*sizeof(float) || r.converted_bytes%(sizeof(float)*output_channels_)) {
        r.error=E::converter_contract;r.converter_consumption_uncertain=true;faulted_=true;return r;
    }
    const auto samples=r.converted_bytes/sizeof(float);
    for(std::size_t i=0;i<samples;++i) if(!std::isfinite(output[i])) {
        r.error=E::nonfinite_output;faulted_=true;return r;
    }
    for(std::size_t i=0;i<samples;++i) {
        if(output[i]>1) {output[i]=1;++r.clipped_samples;}
        else if(output[i]<-1) {output[i]=-1;++r.clipped_samples;}
    }
    r.frames=samples/output_channels_;r.output_valid=true;return r;
}
SpsdVoiceResult SpsdVoice::finish() noexcept {
    if(!converter_ || faulted_ || decoder_.frames_remaining()!=0) return {E::invalid_state};
    if(!flushed_) {if(!SDL_FlushAudioStream(converter_)) return native_failure();flushed_=true;}
    return drain_status();
}
SpsdVoiceResult SpsdVoice::drain_status() noexcept {
    if(!converter_ || faulted_) return {E::invalid_state};
    const int available=SDL_GetAudioStreamAvailable(converter_);
    if(available<0) return native_failure();
    SpsdVoiceResult r;r.drained=flushed_ && decoder_.frames_remaining()==0 && available==0;return r;
}
}
