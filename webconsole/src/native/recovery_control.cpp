#include "recovery_control.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>
namespace dm::native::recovery {
namespace {
constexpr const char* journal_name="recovery.journal";
constexpr const char* lock_name="recovery.lock";
std::string os_error(const char* what){return std::string(what)+": "+std::strerror(errno);}
bool write_all(int fd,const std::string& data) {
    size_t done=0;
    while(done<data.size()) {
        const ssize_t n=::write(fd,data.data()+done,data.size()-done);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return false;
        done+=static_cast<size_t>(n);
    }
    return true;
}
uint64_t checksum(const std::string& data){
    uint64_t h=14695981039346656037ULL;
    for(unsigned char c:data){h^=c;h*=1099511628211ULL;}
    return h;
}
std::string hex64(uint64_t n){
    char out[17];std::snprintf(out,sizeof(out),"%016llx",static_cast<unsigned long long>(n));return out;
}
bool parse_hex(const std::string& s,uint64_t& out){
    if(s.size()!=16)return false;
    out=0;
    for(char c:s){
        unsigned digit=0;
        if(c>='0' && c<='9')digit=static_cast<unsigned>(c-'0');
        else if(c>='a' && c<='f')digit=static_cast<unsigned>(c-'a'+10);
        else return false;
        out=(out<<4)|digit;
    }
    return true;
}
bool parse_u64(const std::string& s,uint64_t& out){
    if(s.empty() || s.size()>20)return false;
    out=0;
    for(char c:s){
        if(c<'0' || c>'9')return false;
        const unsigned n=static_cast<unsigned>(c-'0');
        if(out>(std::numeric_limits<uint64_t>::max()-n)/10)return false;
        out=out*10+n;
    }
    return true;
}
bool random_hex(std::string& out){
    const int fd=::open("/dev/urandom",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return false;
    std::array<unsigned char,32> bytes{};
    size_t pos=0;bool ok=true;
    while(pos<bytes.size()){
        const ssize_t n=::read(fd,bytes.data()+pos,bytes.size()-pos);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){ok=false;break;}
        pos+=static_cast<size_t>(n);
    }
    ::close(fd);
    if(!ok)return false;
    constexpr char hx[]="0123456789abcdef";
    out.clear();out.reserve(bytes.size()*2);
    for(unsigned char b:bytes){out.push_back(hx[b>>4]);out.push_back(hx[b&15]);}
    for(auto& b:bytes)b=0;
    return true;
}
bool secure_equal(const std::string& a,const std::string& b){
    unsigned diff=static_cast<unsigned>(a.size() ^ b.size());
    const size_t bound=a.size()>b.size()?a.size():b.size();
    for(size_t i=0;i<bound;++i){
        const unsigned x=i<a.size()?static_cast<unsigned char>(a[i]):0;
        const unsigned y=i<b.size()?static_cast<unsigned char>(b[i]):0;
        diff|=(x^y);
    }
    return diff==0;
}
bool decode_phase(const std::string& p,LeasePhase& out){
    for(auto phase:{LeasePhase::idle,LeasePhase::prepared,LeasePhase::active,
                    LeasePhase::revoked,LeasePhase::recovery_pending,
                    LeasePhase::recovered,LeasePhase::failed}){
        if(p==lease_phase_name(phase)){out=phase;return true;}
    }
    return false;
}
bool decode_record(const std::string& line,uint64_t& seq,LeasePhase& phase){
    const auto last=line.rfind('|');
    if(last==std::string::npos)return false;
    const std::string prefix=line.substr(0,last),sig=line.substr(last+1);
    uint64_t expected=0;
    if(!parse_hex(sig,expected) || checksum(prefix)!=expected)return false;
    const auto a=prefix.find('|'),b=prefix.find('|',a==std::string::npos?0:a+1);
    const auto c=prefix.find('|',b==std::string::npos?0:b+1);
    if(a==std::string::npos||b==std::string::npos||c==std::string::npos)return false;
    if(prefix.substr(0,a)!="DMR2" || !parse_u64(prefix.substr(a+1,b-a-1),seq)
       || !decode_phase(prefix.substr(b+1,c-b-1),phase))return false;
    const std::string id=prefix.substr(c+1);
    if(id.size()>64)return false;
    for(char x:id)if(!((x>='0'&&x<='9')||(x>='a'&&x<='f')))return false;
    return true;
}
}
const char* lease_phase_name(LeasePhase p){
    switch(p){
        case LeasePhase::idle:return "idle";
        case LeasePhase::prepared:return "prepared";
        case LeasePhase::active:return "active";
        case LeasePhase::revoked:return "revoked";
        case LeasePhase::recovery_pending:return "recovery_pending";
        case LeasePhase::recovered:return "recovered";
        case LeasePhase::failed:return "failed";
    }
    return "invalid";
}
RecoveryControl::~RecoveryControl(){close();}
void RecoveryControl::close() noexcept {
    current_={};
    if(journal_fd_>=0)::close(journal_fd_);
    if(lock_fd_>=0){::flock(lock_fd_,LOCK_UN);::close(lock_fd_);}
    if(dir_fd_>=0)::close(dir_fd_);
    journal_fd_=lock_fd_=dir_fd_=-1;
    sequence_=0;phase_=LeasePhase::idle;
}
bool RecoveryControl::open(const std::string& directory,std::string& error){
    close();error.clear();
    dir_fd_=::open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(dir_fd_<0){error=os_error("Open recovery directory");return false;}
    struct stat st{};
    if(::fstat(dir_fd_,&st)!=0 || !S_ISDIR(st.st_mode) ||
       st.st_uid!=::geteuid() || (st.st_mode & 0077)!=0){
        error="Recovery directory must be owned by this user and mode 0700";
        close();return false;
    }
    lock_fd_=::openat(dir_fd_,lock_name,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lock_fd_<0){error=os_error("Open recovery lock");close();return false;}
    if(::fstat(lock_fd_,&st)!=0 || !S_ISREG(st.st_mode) || st.st_nlink!=1 ||
       st.st_uid!=::geteuid() || (st.st_mode & 0077)!=0){
        error="Insecure recovery lock file";close();return false;
    }
    if(::flock(lock_fd_,LOCK_EX|LOCK_NB)!=0){
        error="Recovery supervisor already active";close();return false;
    }
    journal_fd_=::openat(dir_fd_,journal_name,O_RDWR|O_APPEND|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(journal_fd_<0){error=os_error("Open recovery journal");close();return false;}
    if(::fstat(journal_fd_,&st)!=0 || !S_ISREG(st.st_mode) ||
       st.st_nlink!=1 || st.st_uid!=::geteuid() || (st.st_mode & 0077)!=0){
        error="Insecure recovery journal file";close();return false;
    }
    if(st.st_size>1024*1024){
        error="Recovery journal exceeds review limit; refusing lease";close();return false;
    }
    // Persist directory entries before any lease transition is authorized.
    if(::fsync(dir_fd_)!=0){error=os_error("Sync recovery state directory");close();return false;}
    if(::lseek(journal_fd_,0,SEEK_SET)<0){error=os_error("Seek recovery journal");close();return false;}
    std::string content;
    char buffer[4096];
    while(true){
        const ssize_t n=::read(journal_fd_,buffer,sizeof(buffer));
        if(n<0 && errno==EINTR)continue;
        if(n<0){error=os_error("Read recovery journal");close();return false;}
        if(n==0)break;
        content.append(buffer,static_cast<size_t>(n));
    }
    if(!content.empty() && content.back()!='\n'){
        error="Torn recovery journal record; manual recovery required";close();return false;
    }
    uint64_t previous=0;
    size_t from=0;
    while(from<content.size()){
        const auto end=content.find('\n',from);
        const std::string line=content.substr(from,end-from);
        uint64_t next=0;LeasePhase p;
        if(!decode_record(line,next,p) || next!=previous+1){
            error="Invalid recovery journal checksum or sequence; fail closed";
            close();return false;
        }
        previous=next;phase_=p;
        from=end+1;
    }
    sequence_=previous;
    // On supervisor restart, secrets disappear. Any in-flight state must
    // be reconciled by the *external* restoration path before any new lease.
    if(phase_==LeasePhase::prepared || phase_==LeasePhase::active ||
       phase_==LeasePhase::revoked || phase_==LeasePhase::recovery_pending ||
       phase_==LeasePhase::failed){
        if(!append(LeasePhase::recovery_pending,"",error)){close();return false;}
    }
    return true;
}
bool RecoveryControl::append(LeasePhase next,const std::string& id,std::string& error){
    if(journal_fd_<0){error="Recovery journal unavailable";return false;}
    const std::string prefix=std::string("DMR2|")+std::to_string(sequence_+1)+"|"+
       lease_phase_name(next)+"|"+id;
    const std::string record=prefix+"|"+hex64(checksum(prefix))+"\n";
    if(!write_all(journal_fd_,record) || ::fsync(journal_fd_)!=0){
        error=os_error("Recovery journal persistence failed");
        current_={};phase_=LeasePhase::failed;return false;
    }
    sequence_++;phase_=next;return true;
}
bool RecoveryControl::prepare(LeaseToken& token,std::string& error){
    token={};
    if(journal_fd_<0){error="Supervisor lock unavailable";return false;}
    if(phase_!=LeasePhase::idle && phase_!=LeasePhase::recovered){
        error="Previous owner not reconciled; lease denied";return false;
    }
    LeaseToken fresh;
    if(!random_hex(fresh.id)||!random_hex(fresh.secret)){
        error="Cryptographic randomness unavailable";return false;
    }
    if(!append(LeasePhase::prepared,fresh.id,error))return false;
    current_=fresh;token=fresh;return true;
}
bool RecoveryControl::authenticate(const LeaseToken& token)const {
    return journal_fd_>=0 && phase_==LeasePhase::active &&
        current_.secret.size()==64 && current_.id.size()==64 &&
        secure_equal(token.id,current_.id) && secure_equal(token.secret,current_.secret);
}
bool RecoveryControl::activate(const LeaseToken& token,std::string& error){
    if(phase_!=LeasePhase::prepared || current_.secret.size()!=64 ||
       !secure_equal(token.id,current_.id)||!secure_equal(token.secret,current_.secret)){
        error="Invalid or stale recovery lease";return false;
    }
    return append(LeasePhase::active,current_.id,error);
}
bool RecoveryControl::revoke(std::string& error){
    current_={};
    if(phase_!=LeasePhase::active && phase_!=LeasePhase::prepared){
        error="No live lease to revoke";return false;
    }
    return append(LeasePhase::revoked,"",error);
}
bool RecoveryControl::record_restored(bool success,std::string& error){
    current_={};
    // A completion may only follow revocation/restart or a failed recovery.
    // Never permit an ACTIVE lease to be called "restored" directly.
    if(phase_!=LeasePhase::recovery_pending && phase_!=LeasePhase::revoked &&
       phase_!=LeasePhase::failed){
        error="No recovery outstanding";return false;
    }
    if(!append(success?LeasePhase::recovered:LeasePhase::failed,"",error))return false;
    return success;
}
} // namespace dm::native::recovery
