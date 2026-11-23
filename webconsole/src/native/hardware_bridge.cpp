#include "hardware_bridge.h"
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
namespace dm::native {
InputResult SurfaceMapper::route(const SurfaceEvent& e, Controller& controller) const {
    if (!enabled_) return InputResult::disabled;
    // Channel 1 only. Ignore pads (channel 10), release events,
    // unrelated CCs, SysEx and stock transport controls.
    if ((e.status & 0x0f) != 0) return InputResult::ignored;
    const std::uint8_t kind=e.status & 0xf0;
    if (kind==0xb0 && e.number==100) {
        // Relative encoder: 1 clockwise, 127 counterclockwise, zero neutral.
        // Bound large values to one action to avoid input storms.
        if(e.value==1){controller.press(Key::down);return InputResult::dispatched;}
        if(e.value==127){controller.press(Key::up);return InputResult::dispatched;}
        return InputResult::ignored;
    }
    if(kind!=0x90 || e.value==0) return InputResult::ignored;
    switch(e.number) {
        case 111: controller.press(Key::enter);break; // Wheel push
        case 55: controller.press(Key::back);break; // Minus (provisional mapping)
        case 54: controller.press(Key::select);break; // Plus (provisional mapping)
        case 12: controller.press(Key::refresh);break; // Top left (provisional)
        default:return InputResult::ignored;
    }
    return InputResult::dispatched;
}
DisplayInfo probe_display_candidates(const std::string& directory) {
    DisplayInfo out;
    DIR* d=::opendir(directory.c_str());
    if(!d){out.reason="DRM directory unavailable; safe headless fallback";return out;}
    while(dirent* e=::readdir(d)){
        const std::string name(e->d_name);
        if(name.rfind("card",0)!=0 || name.size()<=4 ||
           !std::all_of(name.begin()+4,name.end(),[](unsigned char c){return c>='0'&&c<='9';})) continue;
        const std::string path=directory+"/"+name;
        struct stat st{};
        if(::lstat(path.c_str(),&st)!=0)continue;
        if(S_ISCHR(st.st_mode))out.nodes.push_back(path);
    }
    ::closedir(d);
    std::sort(out.nodes.begin(),out.nodes.end());
    // A node is not proof of an available 320x240 connector or ownership.
    out.reason=out.nodes.empty()?"No DRM character devices; safe headless fallback":
        "Candidate DRM nodes found; panel mode and ownership not verified";
    return out;
}
} // namespace dm::native
