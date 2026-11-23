#include "test_main.h"
#include "native/hardware_bridge.h"
#include <map>
using namespace dm::native;
namespace {
struct SampleService final : Service {
  std::vector<Volume> volumes() override {return {{"sd","SD",true,true}};}
  bool list(const std::string&,const std::string& dir,std::size_t offset,std::size_t limit,
            std::vector<Item>& out,bool& more,std::string& error) override {
    error.clear();out.clear();more=false;
    if(!dir.empty())return true;
    const std::vector<Item> list={{"A",true},{"B",true},{"C",true}};
    for(std::size_t i=offset;i<list.size()&&out.size()<limit;++i)out.push_back(list[i]);
    more=offset+out.size()<list.size();return true;
  }
};
}
TEST(hardware_input_disabled_by_default) {
 SampleService s;Controller c(s);SurfaceMapper m;
 CHECK(m.route({0x90,111,127},c)==InputResult::disabled);
 CHECK(c.screen()==Screen::volumes);
}
TEST(hardware_surface_channel_one_wheel) {
 SampleService s;Controller c(s);SurfaceMapper m(true);
 CHECK(m.route({0x90,111,127},c)==InputResult::dispatched);
 CHECK(c.screen()==Screen::files);
 CHECK(m.route({0xb0,100,1},c)==InputResult::dispatched);
 CHECK_EQ(c.cursor(),1u);
 CHECK(m.route({0xb0,100,127},c)==InputResult::dispatched);
 CHECK_EQ(c.cursor(),0u);
 CHECK(m.route({0xb0,100,0},c)==InputResult::ignored);
 CHECK(m.route({0xb0,100,64},c)==InputResult::ignored);
}
TEST(hardware_ignores_pad_channel_and_release) {
 SampleService s;Controller c(s);SurfaceMapper m(true);
 CHECK(m.route({0x99,111,127},c)==InputResult::ignored);
 CHECK(m.route({0x80,111,0},c)==InputResult::ignored);
 CHECK(m.route({0x90,111,0},c)==InputResult::ignored);
 CHECK(m.route({0x90,82,127},c)==InputResult::ignored);
 CHECK(c.screen()==Screen::volumes);
}
TEST(hardware_button_map_is_explicit_and_non_destructive) {
 SampleService s;Controller c(s);SurfaceMapper m(true);
 m.route({0x90,111,127},c);
 m.route({0x90,54,127},c);CHECK(c.selected(0));
 m.route({0x90,12,127},c);CHECK(c.selected(0));
 m.route({0x90,55,127},c);CHECK(c.screen()==Screen::volumes);
}
TEST(hardware_probe_is_read_only_and_unverified) {
 // Fail-closed authorization is covered by DisplaySession and KMS tests.
 auto probe=probe_display_candidates("/not-a-real-diskmode-dri");
 CHECK(!probe.available);
 CHECK(probe.nodes.empty());
 CHECK(!probe.reason.empty());
}
