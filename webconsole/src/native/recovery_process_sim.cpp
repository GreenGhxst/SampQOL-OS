// Independent *simulation-only* recovery supervisor process.
// Parent owns a fake stock service lease; forked child is the fake DISKMODE.
// No /dev/dri, ALSA, systemd, signals to third-party processes or autorun.
// Real device recovery needs an independently installed and verified service.
#include "recovery_sim.h"
#include "recovery_control.h"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <thread>
#include <cstdlib>
namespace {
using namespace dm::native::recovery;
bool write_byte(int fd,char c) {
    ssize_t n;
    do { n=::write(fd,&c,1); } while(n<0 && errno==EINTR);
    return n==1;
}
bool read_byte(int fd,char& c) {
    ssize_t n;
    do { n=::read(fd,&c,1); } while(n<0 && errno==EINTR);
    return n==1;
}
bool wait_child(pid_t child,int& status,unsigned millis) {
    for(unsigned n=0;n<millis/10;++n){
        pid_t p=::waitpid(child,&status,WNOHANG);
        if(p==child)return true;
        if(p<0 && errno==ECHILD)return false;
        if(p<0 && errno!=EINTR)return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
bool reap_child(pid_t child,int& status) {
    if(wait_child(child,status,1000))return true;
    // Escalation is always constrained to the child PID from our own fork.
    if(::kill(child,SIGKILL)!=0 && errno!=ESRCH)return false;
    for(;;){
        pid_t p=::waitpid(child,&status,0);
        if(p==child)return true;
        if(p<0 && errno==EINTR)continue;
        return false;
    }
}
int simulate(const std::string& mode){
    MockStock stock;
    Supervisor supervisor(stock);
    std::string why;
    if(mode=="stop-fail")stock.allow_stop=false;
    if(!supervisor.begin(why)){
        if(mode=="stop-fail" && stock.running && !supervisor.lease_valid() && stock.stops==1){
            std::cout<<"PASS stop-fail: fail closed, stock remains running\n";
            return 0;
        }
        std::cerr<<"unexpected stop refusal: "<<why<<"\n";return 2;
    }
    if(mode=="restore-fail")stock.allow_restore=false;
    int ready[2]={-1,-1}, control[2]={-1,-1};
    if(::pipe(ready)!=0 || ::pipe(control)!=0){
        if(ready[0]>=0){::close(ready[0]);::close(ready[1]);}
        if(control[0]>=0){::close(control[0]);::close(control[1]);}
        supervisor.child_exited(why);
        std::cerr<<"pipe failed\n";return 3;
    }
    pid_t child=::fork();
    if(child<0){
        ::close(ready[0]);::close(ready[1]);::close(control[0]);::close(control[1]);
        supervisor.child_exited(why);
        std::cerr<<"fork failed\n";return 4;
    }
    if(child==0){
        ::close(ready[0]);::close(control[1]);
        if(!write_byte(ready[1],'R'))_exit(6);
        ::close(ready[1]);
        if(mode=="child-crash") {
            struct rlimit limit{0,0};::setrlimit(RLIMIT_CORE,&limit);
            ::raise(SIGABRT);_exit(7);
        }
        // Real forked child remains alive until parent ends/revokes its lease.
        char cmd=0;
        while(::read(control[0],&cmd,1)<0 && errno==EINTR){}
        _exit(cmd=='Q'?0:8);
    }
    ::close(ready[1]);::close(control[0]);
    char got=0;
    const auto n=::read(ready[0],&got,1);
    ::close(ready[0]);
    if(n!=1 || got!='R' || !supervisor.child_ready(why)){
        ::close(control[1]);
        int status=0;reap_child(child,status);
        supervisor.child_exited(why);
        std::cerr<<"child failed readiness\n";return 5;
    }
    int status=0;
    bool reaped=false;
    if(mode=="normal" || mode=="restore-fail"){
        write_byte(control[1],'Q');::close(control[1]);
        reaped=reap_child(child,status);
    } else if(mode=="sigkill"){
        ::kill(child,SIGKILL);::close(control[1]);
        reaped=reap_child(child,status);
    } else if(mode=="revoke"){
        supervisor.revoke();
        ::kill(child,SIGTERM);::close(control[1]);
        reaped=reap_child(child,status);
    } else if(mode=="timeout"){
        // Simulated 30ms missing heartbeat; bounded independent supervisor kill.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        supervisor.revoke();::kill(child,SIGKILL);::close(control[1]);
        reaped=reap_child(child,status);
    } else if(mode=="child-crash"){
        ::close(control[1]);reaped=reap_child(child,status);
    } else {
        ::kill(child,SIGKILL);::close(control[1]);reap_child(child,status);
        supervisor.child_exited(why);
        std::cerr<<"unknown simulation mode\n";return 7;
    }
    if(!reaped){
        supervisor.child_exited(why);
        std::cerr<<"child reap failed; recovery attempted\n";return 8;
    }
    // A separate supervisor process restores simulated stock state only
    // after OS-confirmed child death, regardless of child's destructor.
    const bool restored=supervisor.child_exited(why);
    const bool failure_expected=mode=="restore-fail";
    if(failure_expected) {
        if(restored || supervisor.state()!=State::failed ||
           stock.running || stock.restores!=1 || supervisor.lease_valid()){
            std::cerr<<"restore failure was not detected\n";return 9;
        }
        std::cout<<"PASS restore-fail: reported failure, no new lease\n";return 0;
    }
    if(!restored || !stock.running || stock.restores!=1 ||
       supervisor.state()!=State::recovered || supervisor.lease_valid()) {
        std::cerr<<"stock restoration assertion failed: "<<why<<"\n";
        return 10;
    }
    if(mode=="normal" && !(WIFEXITED(status) && WEXITSTATUS(status)==0))return 11;
    if(mode=="sigkill" && !(WIFSIGNALED(status)&&WTERMSIG(status)==SIGKILL))return 12;
    if(mode=="child-crash" && !(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT))return 13;
    if(mode=="revoke" && !(WIFSIGNALED(status)&&WTERMSIG(status)==SIGTERM))return 14;
    std::cout<<"PASS "<<mode<<": child reaped, simulated stock restored, lease revoked\n";
    return 0;
}
}
int simulate_supervisor(const std::string& mode){
    using namespace dm::native::recovery;

    if(mode!="kill"&&mode!="exit"&&mode!="restore-fail"&&mode!="second-instance")return 64;
    char dir[]="/tmp/diskmode-recovery-test-XXXXXX";
    if(!::mkdtemp(dir)){std::cerr<<"mkdtemp failed\n";return 1;}
    const std::string directory=dir;
    auto cleanup=[&]{
        ::unlink((directory+"/recovery.journal").c_str());
        ::unlink((directory+"/recovery.lock").c_str());
        ::rmdir(directory.c_str());
    };
    int ready[2];
    if(::pipe(ready)!=0){cleanup();return 2;}
    const pid_t child=::fork();
    if(child<0){::close(ready[0]);::close(ready[1]);cleanup();return 3;}
    if(child==0){
        ::close(ready[0]);std::string why;LeaseToken lease;
        RecoveryControl supervisor;
        if(!supervisor.open(directory,why) ||
           !supervisor.prepare(lease,why) ||
           !supervisor.activate(lease,why)){
            char signal='F';write_byte(ready[1],signal);_exit(5);
        }
        char signal='R';write_byte(ready[1],signal);
        if(mode=="exit"){supervisor.revoke(why);supervisor.record_restored(true,why);_exit(0);}
        if(mode=="second-instance"){
            // Wait for harness attempt while lock is still held.
            ::sleep(1);_exit(0);
        }
        ::kill(::getpid(),SIGKILL);
        _exit(6);
    }
    ::close(ready[1]);char signal='F';
    const bool ready_ok=read_byte(ready[0],signal)&&signal=='R';
    ::close(ready[0]);
    bool concurrent_denied=true;
    if(mode=="second-instance" && ready_ok){
        RecoveryControl contender;std::string err;
        concurrent_denied=!contender.open(directory,err);
    }
    int status=0;
    while(::waitpid(child,&status,0)<0){if(errno==EINTR)continue;cleanup();return 4;}
    if(!ready_ok){cleanup();return 5;}
    if(mode=="kill" || mode=="restore-fail"){
        if(!WIFSIGNALED(status)||WTERMSIG(status)!=SIGKILL){cleanup();return 6;}
    }
    if(mode=="exit" && (!WIFEXITED(status) || WEXITSTATUS(status)!=0)){cleanup();return 7;}
    RecoveryControl restarted;std::string err;LeaseToken stale;
    if(!restarted.open(directory,err)){std::cerr<<err<<"\n";cleanup();return 8;}
    if(!concurrent_denied){restarted.close();cleanup();return 9;}
    if(mode=="exit"){
        if(restarted.phase()!=LeasePhase::recovered){restarted.close();cleanup();return 10;}
    }else{
        if(restarted.phase()!=LeasePhase::recovery_pending ||
           restarted.prepare(stale,err) || restarted.authenticate(stale)){
            restarted.close();cleanup();return 11;
        }
        // Simulated restoration outcome, never a real systemd operation.
        const bool success=mode!="restore-fail";
        if(restarted.record_restored(success,err)!=success){restarted.close();cleanup();return 12;}
        if(success && restarted.phase()!=LeasePhase::recovered){restarted.close();cleanup();return 13;}
        if(!success && restarted.phase()!=LeasePhase::failed){restarted.close();cleanup();return 14;}
    }
    restarted.close();cleanup();
    std::cout<<"PASS "<<mode<<": independent restart checked durable journal; no hardware changes\n";
    return 0;
}

int main(int argc,char** argv) {
    if(argc!=2) {
        std::cerr<<"usage: diskmode-recovery-sim "
                    "[normal|sigkill|child-crash|revoke|timeout|restore-fail|stop-fail|"
                    "supervisor:kill|supervisor:exit|supervisor:restore-fail|supervisor:second-instance]\n";
        return 64;
    }
    const std::string mode=argv[1];
    if(mode.rfind("supervisor:",0)==0)
        return simulate_supervisor(mode.substr(sizeof("supervisor:")-1));
    return simulate(mode);
}
