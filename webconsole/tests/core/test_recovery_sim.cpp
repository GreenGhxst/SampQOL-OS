#include "test_main.h"
#include "native/recovery_sim.h"
using namespace dm::native::recovery;

TEST(recovery_lease_denied_until_stock_stopped) {
    MockStock stock; Supervisor s(stock); std::string why;
    CHECK(!s.child_ready(why));CHECK(!s.lease_valid());
    CHECK(s.begin(why));CHECK(!stock.running);CHECK_EQ(stock.stops,1u);
    CHECK(!s.lease_valid());
    CHECK(s.child_ready(why));CHECK(s.lease_valid());
    CHECK(!s.begin(why));
}
TEST(recovery_revocation_restores_stock_once) {
    MockStock stock;Supervisor s(stock);std::string why;
    CHECK(s.begin(why));CHECK(s.child_ready(why));
    s.revoke();CHECK(!s.lease_valid());
    CHECK(s.child_exited(why));CHECK(stock.running);
    CHECK_EQ(stock.restores,1u);
    CHECK(s.child_exited(why));CHECK_EQ(stock.restores,1u);
    CHECK(!s.lease_valid());CHECK(s.state()==State::recovered);
}
TEST(recovery_crash_without_child_cleanup_restores_stock) {
    MockStock stock;Supervisor s(stock);std::string why;
    CHECK(s.begin(why));CHECK(s.child_ready(why));
    // A real supervisor uses waitpid. Here the child has died without
    // calling its own display backend destructor.
    CHECK(s.child_exited(why));CHECK(stock.running);
    CHECK_EQ(stock.restores,1u);CHECK(!s.lease_valid());
}
TEST(recovery_restore_failure_blocks_reentry) {
    MockStock stock;stock.allow_restore=false;
    Supervisor s(stock);std::string why;
    CHECK(s.begin(why));CHECK(s.child_ready(why));
    CHECK(!s.child_exited(why));CHECK(!stock.running);
    CHECK(s.state()==State::failed);CHECK(!s.lease_valid());
    CHECK(!s.begin(why));
    stock.allow_restore=true;
    CHECK(s.child_exited(why));CHECK(stock.running);
    CHECK(s.state()==State::recovered);
}
TEST(recovery_stop_failure_refuses_child) {
    MockStock stock;stock.allow_stop=false;
    Supervisor s(stock);std::string why;
    CHECK(!s.begin(why));CHECK(stock.running);CHECK(!s.lease_valid());
    CHECK(!s.child_ready(why));CHECK_EQ(stock.restores,0u);
}
TEST(recovery_no_child_or_unknown_stock_refused) {
    MockStock stock;stock.running=false;
    Supervisor s(stock);std::string why;
    CHECK(!s.begin(why));CHECK(s.state()==State::failed);
    CHECK(!s.lease_valid());
    MockStock other;Supervisor x(other);
    CHECK(!x.child_exited(why));CHECK_EQ(other.restores,0u);
}
