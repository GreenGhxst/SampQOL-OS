// Independent *simulation-only* recovery supervisor process.
// Parent owns a fake stock service lease; forked child is the fake DISKMODE.
// No /dev/dri, ALSA, systemd, signals to third-party processes or autorun.
// Real device recovery needs an independently installed and verified service.
#include "recovery_sim.h"
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
namespace {
using namespace dm::native::recovery;
bool write_byte(int fd,char c) {
    return ::write(fd,&c,1)==1;
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
int main(int argc,char** argv){
    if(argc!=2){
        std::cerr<<"usage: diskmode-recovery-sim [normal|sigkill|child-crash|revoke|timeout|restore-fail|stop-fail]\n";
        return 64;
    }
    return simulate(argv[1]);
}
