#include "native/native_model.h"
#include "native/native_core_service.h"
#include "test_main.h"
#include "fakes.h"
#include <map>
#include <cstdio>
#include <memory>
using namespace dm::native;
namespace {
struct FakeService final : Service {
  std::vector<Volume> volumes() override { return {{"sdcard","SD",true,true},{"internal","Internal",false,false}}; }
  std::map<std::string,std::vector<Item>> folders{
    {"sdcard:",{{"Drums",true},{"Bass.wav",false},{"Keys",true}}},
    {"sdcard:Drums",{{"Kick.wav",false},{"Snare.wav",false}}},
    {"sdcard:Keys",{{"Pad.wav",false}}}
  };
  bool list(const std::string& v,const std::string& p,std::size_t offset,
            std::size_t limit,std::vector<Item>& out,bool& more,std::string& err) override {
    auto it=folders.find(v+":"+p);
    if(it==folders.end()){err="missing";return false;}
    out.clear();
    for(std::size_t i=offset;i<it->second.size() && out.size()<limit;++i) out.push_back(it->second[i]);
    more=offset+out.size()<it->second.size(); return true;
  }
};
// Direct integration of CoreService with real authorization and temporary mounts.
struct CoreFixture {
    TempDir t;
    FakeProbe* probe;
    std::unique_ptr<dm::VolumeRegistry> registry;
    dm::Journal journal;
    dm::FileOps files;
    std::unique_ptr<CoreService> service;
    CoreFixture():journal(t.sub("journal.jsonl")),files(journal,0) {
        t.mkdir("sd"); t.mkdir("internal/UserContent/Samples");
        t.mkdir("sd/Drums"); t.write("sd/Drums/Kick.wav","abc");
        t.mkdir("sd/autorun"); t.write("sd/mpcqol.conf","keep");
        t.write("sd/Visible.wav","audio");
        auto fp=std::make_unique<FakeProbe>();probe=fp.get();
        fp->table={{"/dev/mmcblk1p1",t.sub("sd"),"exfat","rw"},
                   {"/dev/mmcblk0p9",t.sub("internal"),"ext4","rw"}};
        auto rules=test_rules();
        rules[0].mount_prefix=t.sub("s");
        rules[1].mount_point=t.sub("internal");
        registry=std::make_unique<dm::VolumeRegistry>(rules,std::move(fp));
        service=std::make_unique<CoreService>(*registry,files);
    }
    bool list(const std::string& volume,const std::string& path,std::vector<Item>& out,std::string& err) {
        bool more=false;return service->list(volume,path,0,128,out,more,err);
    }
};
}
TEST(native_starts_at_volumes_and_blocks_unavailable) {
  FakeService s;Controller c(s);
  CHECK(c.screen()==Screen::volumes);CHECK_EQ(c.volumes().size(),2u);
  c.press(Key::down);c.press(Key::enter);
  CHECK(c.screen()==Screen::volumes);CHECK(c.volume_id().empty());
}
TEST(native_navigation_and_back) {
  FakeService s;Controller c(s);c.press(Key::enter);
  CHECK(c.screen()==Screen::files);CHECK_EQ(c.entries().size(),3u);
  c.press(Key::enter);CHECK_EQ(c.directory(),"Drums");
  c.press(Key::back);CHECK(c.directory().empty());
  c.press(Key::back);CHECK(c.screen()==Screen::volumes);
}
TEST(native_multiselect_scroll_refresh) {
  FakeService s;Controller c(s);c.press(Key::enter);c.set_page_size(1);
  c.press(Key::down);c.press(Key::select);
  CHECK(c.selected(1));CHECK_EQ(c.top(),1u);
  c.press(Key::refresh);
  CHECK_EQ(c.cursor(),1u);CHECK(c.selected(1));
  s.folders["sdcard:"].erase(s.folders["sdcard:"].begin()+1);
  c.press(Key::refresh);CHECK_EQ(c.selected_count(),0u);
}
TEST(native_missing_folder_reports_error_and_recovers) {
  FakeService s;Controller c(s);c.press(Key::enter);
  s.folders.erase("sdcard:Drums");c.press(Key::enter);
  CHECK(c.screen()==Screen::error);CHECK_EQ(c.error(),"missing");
  c.press(Key::back);CHECK(c.screen()==Screen::volumes);
}
TEST(native_dimensions){CHECK_EQ(Controller::width,320);CHECK_EQ(Controller::height,240);}
TEST(native_pagination_over_500_entries) {
  FakeService s;s.folders["sdcard:"].clear();
  for(int i=0;i<610;++i) s.folders["sdcard:"].push_back({"File"+std::to_string(i),false});
  Controller c(s);c.press(Key::enter);CHECK_EQ(c.entries().size(),128u);
  for(int i=0;i<609;++i)c.press(Key::down);
  CHECK_EQ(c.cursor(),609u);CHECK_EQ(c.entries().size(),610u);
  CHECK(!c.has_more());c.press(Key::select);CHECK(c.selected(609));
  c.press(Key::refresh);CHECK_EQ(c.cursor(),609u);CHECK(c.selected(609));
}
TEST(native_selection_survives_reorder) {
  FakeService s;Controller c(s);c.press(Key::enter);
  c.press(Key::down);c.press(Key::select);
  auto &v=s.folders["sdcard:"];
  std::swap(v[0],v[1]);c.press(Key::refresh);
  CHECK_EQ(c.cursor(),0u);CHECK(c.selected(0));CHECK(!c.selected(1));
}
TEST(native_input_rejects_unsafe_names) {
  const std::vector<std::string> bad={"..","x/y","x\\y","",std::string("X\nY"),std::string(256,'a')};
  for(const auto& name:bad) {
    FakeService s;s.folders["sdcard:"]={{name,true}};
    Controller c(s);c.press(Key::enter);c.press(Key::enter);
    CHECK(c.screen()==Screen::error);
  }
}
TEST(native_core_service_checks_mount_and_protected_paths) {
  CoreFixture f;std::vector<Item> out;std::string err;
  CHECK(f.list("sdcard","",out,err));
  CHECK_EQ(out.size(),2u);
  for(const auto& e:out)CHECK(e.name!="autorun" && e.name!="mpcqol.conf");
  CHECK(!f.list("sdcard","autorun",out,err));
  CHECK(!f.list("sdcard","../sd",out,err));
  CHECK(!f.list("unknown","",out,err));
  CHECK(f.list("internal","",out,err));
  CHECK(!f.service->volumes()[0].id.empty());
}
TEST(native_core_service_blocks_usb_export_and_removal) {
  CoreFixture f;std::vector<Item> out;std::string err;
  CHECK(f.list("sdcard","Drums",out,err));
  f.probe->exported={"mmcblk1"};
  CHECK(!f.list("sdcard","",out,err));CHECK(out.empty());
  auto vs=f.service->volumes();bool blocked=false;
  for(const auto& v:vs)if(v.id=="sdcard")blocked=!v.available;
  CHECK(blocked);
  f.probe->exported.clear();f.probe->table.erase(f.probe->table.begin());
  CHECK(!f.list("sdcard","",out,err));CHECK(out.empty());
}
TEST(native_core_service_pagination_hides_protected) {
  CoreFixture f;
  for(int i=0;i<270;++i) {
    char buf[32];std::snprintf(buf,sizeof(buf),"Sample%03d.wav",i);
    f.t.write(std::string("sd/")+buf,"x");
  }
  std::vector<Item> page;std::string err;bool more=false;
  std::vector<std::string> names;
  for(std::size_t offset=0;;offset+=page.size()) {
    CHECK(f.service->list("sdcard","",offset,64,page,more,err));
    for(auto& e:page)names.push_back(e.name);
    if(!more)break;
    CHECK(!page.empty());
  }
  CHECK_EQ(names.size(),272u);
  for(auto& name:names)CHECK(name!="autorun"&&name!="mpcqol.conf");
  CHECK(!f.service->list("sdcard","",0,0,page,more,err));
  CHECK(!f.service->list("sdcard","",0,300,page,more,err));
}

TEST(native_core_service_refuses_truncated_directory_without_partial_results) {
  CoreFixture f;
  // FileOps has a hard cap of 20,000 visible entries per scan.
  for(int i=0;i<20001;++i) {
    char name[40]; std::snprintf(name,sizeof(name),"N%05d.wav",i);
    f.t.write(std::string("sd/")+name,"");
  }
  std::vector<Item> page={{"old",false}};
  std::string error;bool more=true;
  CHECK(!f.service->list("sdcard","",0,128,page,more,error));
  CHECK(page.empty());CHECK(!more);
  CHECK(error.find("safety limit")!=std::string::npos);
}
