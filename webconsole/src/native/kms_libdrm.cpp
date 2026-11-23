#include "kms_backend.h"
#ifdef DM_WITH_DRM
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_mode.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <memory>
#include <utility>
namespace dm::native {
namespace {
class LibdrmDriver final : public KmsDriver {
public:
    explicit LibdrmDriver(std::string path):path_(std::move(path)){}
    ~LibdrmDriver() override {restore_and_close();}
    bool open_and_find_panel(std::string& why) override {
        // Never invoke without KmsDisplayBackend's external RecoveryGuard.
        if(fd_>=0){why="Already opened";return false;}
        fd_=::open(path_.c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW);
        if(fd_<0){why=std::string("Unable to open DRM device: ")+std::strerror(errno);return false;}
        drmModeRes* res=drmModeGetResources(fd_);
        if(!res){why="DRM resources unavailable";return false;}
        bool found=false;
        for(int i=0;i<res->count_connectors && !found;++i){
            drmModeConnector* c=drmModeGetConnector(fd_,res->connectors[i]);
            if(!c)continue;
            if(c->connection==DRM_MODE_CONNECTED){
                for(int m=0;m<c->count_modes && !found;++m){
                    const auto& mode=c->modes[m];
                    // Refuse portrait rotation until a verified panel mapping exists.
                    if(mode.hdisplay!=320 || mode.vdisplay!=240)continue;
                    for(int e=0;e<c->count_encoders && !found;++e){
                        drmModeEncoder* encoder=drmModeGetEncoder(fd_,c->encoders[e]);
                        if(!encoder)continue;
                        for(int k=0;k<res->count_crtcs;++k){
                            if(encoder->possible_crtcs & (1u<<k)){
                                crtc_=res->crtcs[k]; connector_=c->connector_id;
                                mode_=mode;found=true;break;
                            }
                        }
                        drmModeFreeEncoder(encoder);
                    }
                }
            }
            drmModeFreeConnector(c);
        }
        drmModeFreeResources(res);
        if(!found)why="No verified landscape 320x240 DRM connector/CRTC; refusing panel takeover";
        return found;
    }
    bool save_previous_state(std::string& why) override {
        if(fd_<0 || crtc_==0){why="DRM card/CRTC unavailable";return false;}
        previous_=drmModeGetCrtc(fd_,crtc_);
        if(!previous_){why="Cannot capture previous DRM CRTC state";return false;}
        return true;
    }
    bool acquire_master(std::string& why) override {
        if(!previous_){why="No prior CRTC restore state";return false;}
        if(drmSetMaster(fd_)!=0){why=std::string("DRM master unavailable: ")+std::strerror(errno);return false;}
        master_=true;return true;
    }
    bool allocate_xrgb8888(std::string& why) override {
        if(!master_){why="Not DRM master";return false;}
        drm_mode_create_dumb create{};
        create.width=320;create.height=240;create.bpp=32;
        if(drmIoctl(fd_,DRM_IOCTL_MODE_CREATE_DUMB,&create)!=0){
            why="DRM dumb buffer allocation failed";return false;
        }
        handle_=create.handle;pitch_=create.pitch;size_=create.size;
        if(pitch_<320*4 || size_<static_cast<std::uint64_t>(pitch_)*240){
            why="Unexpected DRM buffer layout";return false;
        }
        if(drmModeAddFB(fd_,320,240,24,32,pitch_,handle_,&fb_)!=0){
            why="Unable to create DRM framebuffer";return false;
        }
        drm_mode_map_dumb map{};map.handle=handle_;
        if(drmIoctl(fd_,DRM_IOCTL_MODE_MAP_DUMB,&map)!=0){
            why="Unable to map DRM buffer";return false;
        }
        pixels_=mmap(nullptr,size_,PROT_READ|PROT_WRITE,MAP_SHARED,fd_,map.offset);
        if(pixels_==MAP_FAILED){pixels_=nullptr;why="mmap DRM buffer failed";return false;}
        std::memset(pixels_,0,size_);
        return true;
    }
    bool activate(std::string& why) override {
        if(!pixels_ || !master_ || !fb_ || !previous_){
            why="Incomplete DRM presentation state";return false;
        }
        if(drmModeSetCrtc(fd_,crtc_,fb_,0,0,&connector_,1,&mode_)!=0){
            why="DRM CRTC activation rejected";return false;
        }
        changed_=true;return true;
    }
    bool blit(const std::vector<std::uint8_t>& frame,std::string& why) override {
        if(!pixels_ || !changed_ || frame.size()!=320u*240u*4u){
            why="DRM frame buffer unavailable";return false;
        }
        auto* dest=static_cast<std::uint8_t*>(pixels_);
        for(int y=0;y<240;++y)
            std::memcpy(dest+static_cast<std::size_t>(y)*pitch_,
                        frame.data()+static_cast<std::size_t>(y)*320*4,320*4);
        drmModeClip clip{0,0,320,240};
        if(drmModeDirtyFB(fd_,fb_,&clip,1)!=0){
            why="DRM framebuffer flush failed";return false;
        }
        return true;
    }
    void restore_and_close() noexcept override {
        // A process-kill does not execute this: externally monitored restart
        // and restoration are mandatory before any real DRM acquisition.
        if(fd_>=0 && master_ && changed_ && previous_){
            if(previous_->mode_valid)
                drmModeSetCrtc(fd_,previous_->crtc_id,previous_->buffer_id,
                               previous_->x,previous_->y,&connector_,1,&previous_->mode);
            else drmModeSetCrtc(fd_,crtc_,0,0,0,nullptr,0,nullptr);
        }
        changed_=false;
        if(pixels_){munmap(pixels_,size_);pixels_=nullptr;}
        if(fb_ && fd_>=0)drmModeRmFB(fd_,fb_);
        fb_=0;
        if(handle_ && fd_>=0){
            drm_mode_destroy_dumb destroy{};destroy.handle=handle_;
            drmIoctl(fd_,DRM_IOCTL_MODE_DESTROY_DUMB,&destroy);
        }
        handle_=0;pitch_=0;size_=0;
        if(master_ && fd_>=0)drmDropMaster(fd_);
        master_=false;
        if(previous_){drmModeFreeCrtc(previous_);previous_=nullptr;}
        if(fd_>=0){::close(fd_);fd_=-1;}
        crtc_=0;connector_=0;
    }
private:
    std::string path_;
    int fd_=-1;
    uint32_t crtc_=0,connector_=0,handle_=0,fb_=0,pitch_=0;
    std::uint64_t size_=0;
    drmModeModeInfo mode_{};
    drmModeCrtc* previous_=nullptr;
    void* pixels_=nullptr;
    bool master_=false,changed_=false;
};
}
std::unique_ptr<KmsDriver> make_libdrm_driver(std::string path){
    return std::make_unique<LibdrmDriver>(std::move(path));
}
}
#else
namespace dm::native {
std::unique_ptr<KmsDriver> make_libdrm_driver(std::string){return nullptr;}
}
#endif
