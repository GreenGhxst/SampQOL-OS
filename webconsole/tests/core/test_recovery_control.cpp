#include "test_main.h"
#include "native/recovery_control.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>
using namespace dm::native::recovery;
TEST(control_recovery_lease_requires_persistent_preparation) {
    TempDir t;
    RecoveryControl c;std::string error;LeaseToken token;
    CHECK(c.open(t.path,error));
    CHECK(c.prepare(token,error));
    CHECK_EQ(token.id.size(),64u);CHECK_EQ(token.secret.size(),64u);
    CHECK(!c.authenticate(token));
    CHECK(c.activate(token,error));CHECK(c.authenticate(token));
    LeaseToken invalid=token;invalid.secret[0]=invalid.secret[0]=='a'?'b':'a';
    CHECK(!c.authenticate(invalid));
    CHECK(!c.activate(invalid,error));
    CHECK(c.revoke(error));CHECK(!c.authenticate(token));
    CHECK(c.needs_restoration());
    CHECK(c.record_restored(true,error));
    CHECK(c.phase()==LeasePhase::recovered);
    CHECK(!c.authenticate(token));
}
TEST(control_recovery_restart_revokes_secret_and_requires_reconciliation) {
    TempDir t;std::string error;LeaseToken token;
    {
        RecoveryControl c;CHECK(c.open(t.path,error));
        CHECK(c.prepare(token,error));CHECK(c.activate(token,error));
        CHECK(c.authenticate(token));
    }
    RecoveryControl restarted;
    CHECK(restarted.open(t.path,error));
    CHECK(restarted.phase()==LeasePhase::recovery_pending);
    CHECK(!restarted.authenticate(token));
    LeaseToken next;
    CHECK(!restarted.prepare(next,error));
    CHECK(restarted.record_restored(true,error));
    CHECK(restarted.prepare(next,error));CHECK(next.secret!=token.secret);
    CHECK(restarted.activate(next,error));CHECK(!restarted.authenticate(token));
    CHECK(restarted.authenticate(next));
}
TEST(control_recovery_failed_restoration_is_sticky_on_restart) {
    TempDir t;std::string error;LeaseToken token;
    {
        RecoveryControl c;CHECK(c.open(t.path,error));
        CHECK(c.prepare(token,error));CHECK(c.activate(token,error));
        CHECK(!c.record_restored(true,error));
        CHECK(!c.authenticate(token)); // invalid completion fails closed
        CHECK(c.revoke(error));
        CHECK(!c.record_restored(false,error));
        CHECK(c.phase()==LeasePhase::failed);
        CHECK(!c.authenticate(token));
    }
    RecoveryControl resumed;CHECK(resumed.open(t.path,error));
    CHECK(resumed.phase()==LeasePhase::recovery_pending);
    LeaseToken attempt;CHECK(!resumed.prepare(attempt,error));
    CHECK(resumed.record_restored(true,error));
    CHECK(resumed.prepare(attempt,error));
}
TEST(control_recovery_corrupt_or_torn_journal_denies_start) {
    TempDir t;std::string error;LeaseToken token;
    {
        RecoveryControl c;CHECK(c.open(t.path,error));
        CHECK(c.prepare(token,error));c.close();
    }
    t.write("recovery.journal","DMR2|1|active|bad|0000000000000000\n");
    RecoveryControl invalid;CHECK(!invalid.open(t.path,error));
    CHECK(!invalid.prepare(token,error));
    t.write("recovery.journal","DMR2|1|active|");
    CHECK(!invalid.open(t.path,error));
    CHECK(!invalid.prepare(token,error));
}
TEST(control_recovery_single_instance_flok_across_processes) {
    TempDir t;std::string error;
    RecoveryControl first;CHECK(first.open(t.path,error));
    int pipefd[2];CHECK(::pipe(pipefd)==0);
    pid_t child=::fork();CHECK(child>=0);
    if(child==0){
        ::close(pipefd[0]);
        RecoveryControl other;
        std::string why;
        char outcome=other.open(t.path,why)?'X':'D';
        ::write(pipefd[1],&outcome,1);
        ::close(pipefd[1]);
        _exit(0);
    }
    ::close(pipefd[1]);char outcome='?';
    CHECK(::read(pipefd[0],&outcome,1)==1);
    ::close(pipefd[0]);
    int status=0;CHECK(::waitpid(child,&status,0)==child);
    CHECK(outcome=='D');CHECK(WIFEXITED(status) && WEXITSTATUS(status)==0);
    first.close();
    RecoveryControl next;CHECK(next.open(t.path,error));
}
TEST(control_recovery_refuses_unsafe_directory_permissions) {
    TempDir t;std::string error;
    CHECK(::chmod(t.path.c_str(),0755)==0);
    RecoveryControl c;CHECK(!c.open(t.path,error));
    CHECK(::chmod(t.path.c_str(),0700)==0);
    CHECK(c.open(t.path,error));
}
TEST(control_recovery_revocation_and_replay_prevention) {
    TempDir t;std::string error;LeaseToken first,second;
    RecoveryControl c;CHECK(c.open(t.path,error));
    CHECK(c.prepare(first,error));CHECK(c.activate(first,error));
    CHECK(c.revoke(error));CHECK(!c.activate(first,error));
    CHECK(!c.prepare(second,error));
    CHECK(c.record_restored(true,error));
    CHECK(c.prepare(second,error));CHECK(c.activate(second,error));
    CHECK(!c.authenticate(first));CHECK(c.authenticate(second));
}
