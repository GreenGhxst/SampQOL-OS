#include "native/native_model.h"
#include "test_main.h"
#include <map>
using namespace dm::native;
namespace {
struct FakeService final : Service {
  std::vector<Volume> volumes() override { return {{"sdcard","SD",true,true},{"internal","Internal",false,false}}; }
  std::map<std::string,std::vector<Item>> folders {
    {"sdcard:",{{"Drums",true},{"Bass.wav",false},{"Keys",true}}},
    {"sdcard:Drums",{{"Kick.wav",false},{"Snare.wav",false}}},
    {"sdcard:Keys",{{"Pad.wav",false}}}
  };
  bool list(const std::string& v,const std::string& p,std::vector<Item>& out,std::string& err) override {
    auto i=folders.find(v+":"+p); if(i==folders.end()){err="missing";return false;} out=i->second;return true;
  }
};
}
TEST(native_starts_at_volumes_and_blocks_unavailable) {
  FakeService s; Controller c(s);
  CHECK(c.screen()==Screen::volumes); CHECK_EQ(c.volumes().size(),2u);
  c.press(Key::down); c.press(Key::enter);
  CHECK(c.screen()==Screen::volumes); CHECK(c.volume_id().empty());
}
TEST(native_navigation_and_back) {
  FakeService s; Controller c(s);
  c.press(Key::enter);
  CHECK(c.screen()==Screen::files); CHECK_EQ(c.entries().size(),3u);
  c.press(Key::enter);
  CHECK_EQ(c.directory(),"Drums"); CHECK_EQ(c.entries().size(),2u);
  c.press(Key::back); CHECK(c.directory().empty());
  c.press(Key::back); CHECK(c.screen()==Screen::volumes);
}
TEST(native_multiselect_scroll_refresh) {
  FakeService s; Controller c(s); c.press(Key::enter); c.set_page_size(1);
  c.press(Key::down); c.press(Key::select);
  CHECK(c.selected(1)); CHECK_EQ(c.top(),1u);
  c.press(Key::select); CHECK(!c.selected(1));
  c.press(Key::refresh); CHECK_EQ(c.cursor(),0u); CHECK(!c.selected(1));
}
TEST(native_missing_folder_reports_error_and_recovers) {
  FakeService s; Controller c(s); c.press(Key::enter);
  s.folders.erase("sdcard:Drums");
  c.press(Key::enter); CHECK(c.screen()==Screen::error);
  CHECK_EQ(c.error(),"missing");
  c.press(Key::back); CHECK(c.screen()==Screen::volumes);
}
TEST(native_dimensions) {
  CHECK_EQ(Controller::width,320); CHECK_EQ(Controller::height,240);
}
