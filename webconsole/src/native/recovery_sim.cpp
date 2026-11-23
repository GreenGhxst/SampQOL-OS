#include "recovery_sim.h"
namespace dm::native::recovery {
bool MockStock::stop(std::string& error){
    ++stops;
    if(!allow_stop){error="simulated stock stop failed";return false;}
    running=false;return true;
}
bool MockStock::restore(std::string& error){
    ++restores;
    if(!allow_restore){error="simulated stock restart failed";return false;}
    running=true;return true;
}
bool Supervisor::begin(std::string& error){
    if(state_!=State::idle){error="simulation lease already used";return false;}
    if(!stock_.running){state_=State::failed;error="simulated stock state unknown";return false;}
    if(!stock_.stop(error)){state_=State::failed;return false;}
    state_=State::stock_stopped;return true;
}
bool Supervisor::child_ready(std::string& error){
    if(state_!=State::stock_stopped){error="child started without exclusive simulation lease";return false;}
    state_=State::child_running;return true;
}
void Supervisor::revoke(){
    if(state_==State::child_running || state_==State::stock_stopped)
        state_=State::revoking;
}
bool Supervisor::child_exited(std::string& error){
    if(state_==State::recovered) return true;
    // Never assume child cleanup was run. Restore even if it died abruptly.
    if(state_==State::idle){error="simulation was never started";return false;}
    if(state_==State::failed && stock_.running){return false;}
    state_=State::restoring;
    if(!stock_.restore(error)){state_=State::failed;return false;}
    state_=State::recovered;return true;
}
const char* state_name(State s){
    switch(s){
        case State::idle:return "idle";
        case State::stock_stopped:return "stock_stopped";
        case State::child_running:return "child_running";
        case State::revoking:return "revoking";
        case State::restoring:return "restoring";
        case State::recovered:return "recovered";
        case State::failed:return "failed";
    }
    return "unknown";
}
} // namespace dm::native::recovery
