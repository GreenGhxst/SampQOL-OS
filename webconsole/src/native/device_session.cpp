#include "device_session.h"
#include <cerrno>
#include <string>
#ifdef DM_WITH_ALSA
#include <alsa/asoundlib.h>
#endif
namespace dm::native {
bool DisplaySession::start(std::string& reason) {
    if(active_)return true;
    if(!authority_.granted(reason))return false;
    // Backend is responsible for capture+restoration before DRM master.
    if(!backend_.acquire(reason)) {backend_.release();return false;}
    active_=true;reason.clear();return true;
}
bool DisplaySession::present(const std::vector<std::uint8_t>& frame,std::string& reason) {
    if(!active_){reason="Display session inactive";return false;}
    if(!authority_.granted(reason)){stop();return false;}
    if(frame.size()!=320u*240u*4u){reason="Expected 320x240 XRGB8888";return false;}
    if(!backend_.submit(frame,reason)){stop();return false;}
    return true;
}
void DisplaySession::stop() noexcept {
    if(active_){active_=false;backend_.release();}
}
// Running status (channel voice only); realtime bytes do not break a message.
// SysEx, system-common, 1/2-data-byte messages and stray data are discarded.
bool MidiByteDecoder::feed(std::uint8_t byte,SurfaceEvent& out) {
    if(byte>=0xf8)return false;
    if(byte==0xf0){sysex_=true;status_=0;count_=0;return false;}
    if(sysex_){if(byte==0xf7)sysex_=false;return false;}
    if(byte&0x80){
        count_=0;status_=(byte>=0x80 && byte<0xf0) ? byte : 0;
        return false;
    }
    if(!status_)return false;
    const auto kind=status_ & 0xf0;
    if(kind==0xc0 || kind==0xd0){count_=0;return false;}
    if(count_==0){data0_=byte;count_=1;return false;}
    out={status_,data0_,byte};count_=0;return true;
}
bool AlsaSurfaceInput::open(OwnershipAuthority& authority,std::string& reason) {
    close();
    if(!authority.granted(reason))return false;
#ifndef DM_WITH_ALSA
    reason="ALSA support disabled in this build";return false;
#else
    snd_ctl_card_info_t* ci=nullptr;
    snd_rawmidi_info_t* ri=nullptr;
    snd_ctl_card_info_alloca(&ci);
    snd_rawmidi_info_alloca(&ri);
    int card=-1,found=0;
    for(int cards=0;cards<64 && snd_card_next(&card)>=0 && card>=0 && !found;++cards){
        snd_ctl_t* ctl=nullptr;
        std::string ctl_addr="hw:"+std::to_string(card);
        if(snd_ctl_open(&ctl,ctl_addr.c_str(),SND_CTL_NONBLOCK)<0)continue;
        if(snd_ctl_card_info(ctl,ci)==0){
            const std::string id=snd_ctl_card_info_get_id(ci);
            const std::string name=snd_ctl_card_info_get_name(ci);
            if(id=="Surface" || name=="Control Surface"){
                int dev=-1;
                for(int devices=0;devices<64 && snd_ctl_rawmidi_next_device(ctl,&dev)>=0 && dev>=0 && !found;++devices){
                    snd_rawmidi_info_set_device(ri,static_cast<unsigned>(dev));
                    snd_rawmidi_info_set_stream(ri,SND_RAWMIDI_STREAM_INPUT);
                    snd_rawmidi_info_set_subdevice(ri,0);
                    if(snd_ctl_rawmidi_info(ctl,ri)<0)continue;
                    std::string addr=ctl_addr+","+std::to_string(dev)+",0";
                    snd_rawmidi_t* input=nullptr;
                    if(snd_rawmidi_open(&input,nullptr,addr.c_str(),SND_RAWMIDI_NONBLOCK)==0){
                        handle_=input;found=1;
                    }
                }
            }
        }
        snd_ctl_close(ctl);
    }
    if(!found){reason="No exclusively granted ALSA Control Surface input found";return false;}
    // Detect handoff revocation between enumeration and opening.
    if(!authority.granted(reason)){close();return false;}
    authority_=&authority;decoder_.reset();reason.clear();return true;
#endif
}
std::size_t AlsaSurfaceInput::poll(SurfaceMapper& mapper,Controller& controller,std::string& reason){
    if(!handle_){reason="ALSA surface not open";return 0;}
    if(!authority_ || !authority_->granted(reason)){close();return 0;}
#ifndef DM_WITH_ALSA
    reason="ALSA support disabled";close();return 0;
#else
    std::size_t delivered=0;
    auto* input=static_cast<snd_rawmidi_t*>(handle_);
    for(unsigned batches=0;batches<4;++batches){
        std::uint8_t buf[128];
        const auto n=snd_rawmidi_read(input,buf,sizeof(buf));
        if(n==-EAGAIN || n==0)break;
        if(n<0){reason="ALSA input read failed or disconnected";close();return delivered;}
        for(ssize_t i=0;i<n;++i){
            SurfaceEvent event;
            if(decoder_.feed(buf[i],event) && mapper.route(event,controller)==InputResult::dispatched)
                ++delivered;
        }
    }
    return delivered;
#endif
}
void AlsaSurfaceInput::close() noexcept {
#ifdef DM_WITH_ALSA
    if(handle_)snd_rawmidi_close(static_cast<snd_rawmidi_t*>(handle_));
#endif
    handle_=nullptr;authority_=nullptr;decoder_.reset();
}
} // namespace dm::native
