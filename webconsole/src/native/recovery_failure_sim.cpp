// Supervisor crash simulation: harness survives, supervisor subprocess dies.
// All state changes are sandbox-only. No production stock service interactions.
#include "recovery_control.h"
#include <cerrno>
#include <csignal>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace {
using namespace dm::native::recovery;
bool transfer(int fd,char& c,bool send){
    ssize_t n;
    do{n=send ? ::write(fd,&c,1) : ::read(fd,&c,1);}while(n<0&&errno==EINTR);
    return n==1;
}
}
int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"usage: diskmode-supervisor-failure-sim [kill|exit|restore-fail|second-instance]\n";return 64;}
    const std::string mode=argv[1];
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
            char signal='F';transfer(ready[1],signal,true);_exit(5);
        }
        char signal='R';transfer(ready[1],signal,true);
        if(mode=="exit"){supervisor.revoke(why);supervisor.record_restored(true,why);_exit(0);}
        if(mode=="second-instance"){
            // Wait for harness attempt while lock is still held.
            ::sleep(1);_exit(0);
        }
        ::kill(::getpid(),SIGKILL);
        _exit(6);
    }
    ::close(ready[1]);char signal='F';
    const bool ready_ok=transfer(ready[0],signal,false)&&signal=='R';
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
