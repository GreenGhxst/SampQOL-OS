#include "startup_preflight.h"
#include "core/path_auth.h"
#include <exception>
namespace dm::native {
std::string StartupReport::summary() const {
    if(ready)return "All simulated gates satisfied; physical hardware validation still required";
    for(const auto& c:checks)if(!c.passed)return c.name+": "+c.detail;
    return "Startup unavailable";
}
StartupReport StartupPreflight::inspect() {
    StartupReport report;
    auto add=[&](const std::string& name,bool pass,std::string detail){
        report.checks.push_back({name,pass,pass?"Verified by supplied authority":std::move(detail)});
    };
    try {
        volumes_.refresh();
        const auto v=volumes_.get(volume_id_);
        if(!v) add("storage",false,"Configured volume missing");
        else {
            const auto st=PathAuthorizer::check_read(*v);
            add("storage",st.ok(),st.message.empty()?
                "Volume unavailable or USB owned":st.message);
        }
    } catch(...) {add("storage",false,"Storage discovery threw an exception");}
    const bool lease_ok=recovery_.authenticate(lease_) && !recovery_.needs_restoration();
    add("recovery lease",lease_ok,"No active authenticated durable recovery lease");
    auto verify=[&](const std::string& name,auto&& fn){
        std::string reason;
        try {
            const bool ok=fn(reason);
            add(name,ok,reason.empty()?"Independent verification unavailable":reason);
        } catch(const std::exception& e) {
            add(name,false,std::string("Verification exception: ")+e.what());
        } catch(...) {add(name,false,"Verification exception");}
    };
    verify("stock ownership",[&](std::string& why){return owner_.granted(why);});
    verify("recovery supervisor",[&](std::string& why){return guard_.verified(why);});
    verify("stock restoration",[&](std::string& why){return hardware_.stock_restoration_verified(why);});
    verify("panel topology",[&](std::string& why){return hardware_.panel_topology_verified(why);});
    verify("surface mapping",[&](std::string& why){return hardware_.input_mapping_verified(why);});
    report.ready=!report.checks.empty();
    for(const auto& c:report.checks)report.ready=report.ready&&c.passed;
    return report;
}
bool StartupPreflight::start_guarded(DisplaySession& display,std::string& reason) {
    auto before=inspect();
    if(!before.ready){display.stop();reason=before.summary();return false;}
    try {
        if(!display.start(reason)){display.stop();return false;}
    } catch(...) {display.stop();reason="Display acquisition threw an exception";return false;}
    return verify_active(display,reason);
}
bool StartupPreflight::verify_active(DisplaySession& display,std::string& reason) {
    if(!display.active()){reason="Display session not active";return false;}
    auto report=inspect();
    if(!report.ready){
        display.stop();
        reason=report.summary();
        return false;
    }
    reason.clear();
    return true;
}
} // namespace dm::native
