#include "kms_backend.h"
#include <exception>
namespace dm::native {
bool KmsDisplayBackend::acquire(std::string& reason){
    if(active_)return true;
    try {
        if(!guard_.verified(reason))return false;
        // A supervisor may have been revoked between steps; recheck before
        // DRM master acquisition and again before modesetting.
        if(!driver_.open_and_find_panel(reason))goto failure;
        if(!driver_.save_previous_state(reason))goto failure;
        if(!guard_.verified(reason))goto failure;
        if(!driver_.acquire_master(reason))goto failure;
        if(!driver_.allocate_xrgb8888(reason))goto failure;
        if(!guard_.verified(reason))goto failure;
        if(!driver_.activate(reason))goto failure;
        if(!guard_.verified(reason))goto failure;
        active_=true;reason.clear();return true;
    } catch(const std::exception& e) {reason=std::string("DRM acquisition exception: ")+e.what();}
      catch(...) {reason="DRM acquisition exception";}
failure:
    driver_.restore_and_close();
    return false;
}
bool KmsDisplayBackend::submit(const std::vector<std::uint8_t>& frame,std::string& reason){
    if(!active_){reason="DRM backend inactive";return false;}
    if(frame.size()!=320u*240u*4u){reason="Expected 320x240 XRGB8888 frame";return false;}
    try {
        if(!guard_.verified(reason)){release();return false;}
        if(!driver_.blit(frame,reason)){release();return false;}
        return true;
    } catch(...) {
        reason="DRM frame write failed";release();return false;
    }
}
void KmsDisplayBackend::release() noexcept {
    if(active_)active_=false;
    // Called even after a *partial* acquire, and must be idempotent.
    driver_.restore_and_close();
}
} // namespace dm::native
