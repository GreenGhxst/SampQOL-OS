#include "test_main.h"
#include "native/device_session.h"
using namespace dm::native;
namespace {
struct Authority : OwnershipAuthority {
    bool allowed=false;int calls=0;
    bool granted(std::string& why) override {++calls;if(!allowed){why="Ownership refused";return false;}return true;}
};
struct Backend : ScreenBackend {
    bool accept=true,send=true;int acquired=0,released=0,submitted=0;
    bool acquire(std::string& why) override {++acquired;if(!accept){why="No DRM master";return false;}return true;}
    bool submit(const std::vector<std::uint8_t>&,std::string& why) override {
        ++submitted;if(!send){why="Display I/O error";return false;}return true;
    }
    void release() noexcept override {++released;}
};
}
TEST(display_session_refuses_without_owner) {
 Authority a;Backend b;DisplaySession s(a,b);std::string why;
 CHECK(!s.start(why));CHECK(!s.active());CHECK_EQ(b.acquired,0);
 CHECK(!s.present(std::vector<std::uint8_t>(320*240*4),why));
 CHECK_EQ(b.released,0);
}
TEST(display_session_acquire_failure_restores_partial_backend) {
 Authority a;a.allowed=true;Backend b;b.accept=false;
 {DisplaySession s(a,b);std::string why;
  CHECK(!s.start(why));CHECK(!s.active());CHECK_EQ(b.released,1);
  s.stop();CHECK_EQ(b.released,1);
 }
}
TEST(display_session_revoke_and_double_stop) {
 Authority a;a.allowed=true;Backend b;DisplaySession s(a,b);std::string why;
 CHECK(s.start(why));CHECK(s.active());CHECK(s.start(why));CHECK_EQ(b.acquired,1);
 CHECK(!s.present({},why));CHECK(s.active());
 CHECK(s.present(std::vector<std::uint8_t>(320*240*4),why));CHECK_EQ(b.submitted,1);
 a.allowed=false;
 CHECK(!s.present(std::vector<std::uint8_t>(320*240*4),why));
 CHECK(!s.active());CHECK_EQ(b.released,1);
 s.stop();CHECK_EQ(b.released,1);
}
TEST(display_session_submit_error_restores) {
 Authority a;a.allowed=true;Backend b;b.send=false;std::string why;
 {DisplaySession s(a,b);CHECK(s.start(why));
  CHECK(!s.present(std::vector<std::uint8_t>(320*240*4),why));
  CHECK(!s.active());CHECK_EQ(b.released,1);
 }
 CHECK_EQ(b.released,1);
}
TEST(alsa_session_refuses_without_verified_handoff) {
 Authority a;AlsaSurfaceInput input;std::string why;
 CHECK(!input.open(a,why));CHECK(!input.active());
 CHECK_EQ(a.calls,1);CHECK(!why.empty());
 input.close();input.close();
}
TEST(midi_decoder_split_messages_and_running_status) {
 MidiByteDecoder d;SurfaceEvent ev;
 CHECK(!d.feed(0xb0,ev));CHECK(!d.feed(100,ev));
 CHECK(d.feed(1,ev));CHECK_EQ(ev.status,0xb0);CHECK_EQ(ev.number,100);CHECK_EQ(ev.value,1);
 CHECK(!d.feed(100,ev));CHECK(d.feed(127,ev));CHECK_EQ(ev.value,127);
}
TEST(midi_decoder_ignores_sysex_realtime_and_orphans) {
 MidiByteDecoder d;SurfaceEvent ev;
 CHECK(!d.feed(100,ev));
 CHECK(!d.feed(0xf0,ev));CHECK(!d.feed(0x90,ev));CHECK(!d.feed(111,ev));
 CHECK(!d.feed(127,ev));CHECK(!d.feed(0xf7,ev));
 CHECK(!d.feed(0xb0,ev));CHECK(!d.feed(100,ev));CHECK(!d.feed(0xf8,ev));
 CHECK(d.feed(1,ev));CHECK_EQ(ev.number,100);
 d.reset();CHECK(!d.feed(127,ev));
 CHECK(!d.feed(0xc0,ev));CHECK(!d.feed(10,ev));CHECK(!d.feed(1,ev));
}
