#include "test_main.h"
#include "fakes.h"
#include "native/native_runtime.h"
#include "native/native_core_service.h"
#include <chrono>
#include <memory>
#include <thread>
using namespace dm;
using namespace dm::native;
namespace {
struct DeniedBackend:ScreenBackend {
 int acquires=0,submits=0,releases=0;
 bool acquire(std::string&) override {++acquires;return true;}
 bool submit(const std::vector<std::uint8_t>&,std::string&) override {++submits;return true;}
 void release() noexcept override {++releases;}
};
struct GrantedForTest:OwnershipAuthority {
 bool allowed=true;
 bool granted(std::string& reason) override {
   if(!allowed)reason="revoked by test";
   return allowed;
 }
};
struct Fixture {
 TempDir t;
 FakeProbe* probe;
 std::unique_ptr<VolumeRegistry> registry;
 Journal journal;
 FileOps files;
 JobQueue queue;
 std::unique_ptr<CoreService> service;
 std::unique_ptr<NativeJobs> jobs;
 std::unique_ptr<NativeRuntime> runtime;
 Fixture():journal(t.sub("journal.jsonl")),files(journal,0),queue(1,8){
   t.mkdir("sd");t.mkdir("sd/Drums");t.mkdir("sd/Export");t.mkdir("sd/Other");t.mkdir("sd/autorun");
   t.write("sd/Drums/Kick.wav","kick");t.write("sd/Drums/Snare.wav","snare");
   t.write("sd/autorun/locked","locked");
   auto fp=std::make_unique<FakeProbe>();probe=fp.get();
   fp->table={{"/dev/mmcblk1p1",t.sub("sd"),"exfat","rw"}};
   auto rules=test_rules();rules[0].mount_prefix=t.sub("s");
   registry=std::make_unique<VolumeRegistry>(rules,std::move(fp));
   service=std::make_unique<CoreService>(*registry,files);
   jobs=std::make_unique<NativeJobs>(*registry,files,queue);
   runtime=std::make_unique<NativeRuntime>(*service,jobs.get());
 }
 void open_drums(){
   auto& c=runtime->controller();
   CHECK(c.screen()==Screen::volumes);
   runtime->mock_press(MockButton::wheel_press);
   CHECK(c.screen()==Screen::files);
   for(unsigned i=0;i<c.entries().size();++i)
     if(c.entries()[i].name=="Drums"){
       for(unsigned n=0;n<i;++n)runtime->mock_press(MockButton::wheel_right);
       runtime->mock_press(MockButton::wheel_press);return;
     }
   CHECK(false);
 }
 void select_name(const std::string& name){
   auto& c=runtime->controller();
   for(unsigned i=0;i<c.entries().size();++i)if(c.entries()[i].name==name){
     while(c.cursor()<i)runtime->mock_press(MockButton::wheel_right);
     while(c.cursor()>i)runtime->mock_press(MockButton::wheel_left);
     runtime->mock_press(MockButton::select);
     return;
   }
   CHECK(false);
 }
 Status approve(NativeOperation op,const std::string& volume,
                const std::string& directory,std::string& id) {
   id.clear();
   auto st=runtime->prepare_selected(op,volume,directory);
   if(!st.ok())return st;
   runtime->mock_press(MockButton::wheel_press);
   id=runtime->last_job_id();
   return runtime->last_result();
 }
 std::string finish(const std::string& id){
   for(int i=0;i<500;++i){
     auto j=queue.get(id);
     if(!j.is_null()) {
       auto state=j["state"].get<std::string>();
       if(state=="done"||state=="failed"||state=="partial"||state=="cancelled")return state;
     }
     std::this_thread::sleep_for(std::chrono::milliseconds(5));
   }
   return "timeout";
 }
};
}
TEST(native_runtime_renders_and_mock_navigates_actual_fileops){
 Fixture f;auto before=f.runtime->frame();CHECK_EQ(before.size(),320u*240u*4u);
 f.open_drums();
 CHECK_EQ(f.runtime->controller().directory(),"Drums");
 CHECK_EQ(f.runtime->controller().entries().size(),2u);
 auto after=f.runtime->frame();CHECK(after!=before);
 f.select_name("Kick.wav");CHECK_EQ(f.runtime->controller().selected_count(),1u);
 CHECK(f.runtime->frame()!=after);
}
TEST(native_runtime_selected_copy_move_and_trash_pipeline){
 Fixture f;f.open_drums();f.select_name("Kick.wav");std::string id;
 CHECK_OK(f.runtime->prepare_selected(NativeOperation::copy,"sdcard","Export"));
 CHECK(f.runtime->awaiting_confirmation());
 const auto preview=f.runtime->frame();
 CHECK(!f.runtime->mock_press(MockButton::wheel_right));
 CHECK_EQ(f.runtime->frame(),preview);
 CHECK(f.runtime->mock_press(MockButton::back));
 CHECK(!f.runtime->awaiting_confirmation());
 CHECK(id.empty());
 CHECK(!f.t.exists("sd/Export/Kick.wav"));
 CHECK_OK(f.approve(NativeOperation::copy,"sdcard","Export",id));
 CHECK_EQ(f.finish(id),"done");
 CHECK_EQ(f.t.read("sd/Export/Kick.wav"),"kick");
 CHECK_OK(f.approve(NativeOperation::copy,"sdcard","Export",id));
 CHECK_EQ(f.finish(id),"failed"); // collision never overwrites
 f.runtime->mock_press(MockButton::back);
 f.runtime->refresh();
 f.runtime->mock_press(MockButton::back);
 CHECK(f.runtime->controller().screen()==Screen::volumes);
 f.runtime->mock_press(MockButton::wheel_press);
 f.runtime->refresh();
 auto& c=f.runtime->controller();
 for(unsigned i=0;i<c.entries().size();++i)if(c.entries()[i].name=="Export"){
   while(c.cursor()<i)f.runtime->mock_press(MockButton::wheel_right);
   f.runtime->mock_press(MockButton::wheel_press);break;
 }
 CHECK_EQ(c.directory(),"Export");
 f.select_name("Kick.wav");
 CHECK_OK(f.approve(NativeOperation::move,"sdcard","Other",id));
 CHECK_EQ(f.finish(id),"done");
 CHECK(!f.t.exists("sd/Export/Kick.wav"));
 CHECK_EQ(f.t.read("sd/Other/Kick.wav"),"kick");
 f.runtime->mock_press(MockButton::back);
 f.runtime->mock_press(MockButton::back);
 f.runtime->mock_press(MockButton::wheel_press);
 f.runtime->refresh();
 for(unsigned i=0;i<c.entries().size();++i)if(c.entries()[i].name=="Other"){
   while(c.cursor()<i)f.runtime->mock_press(MockButton::wheel_right);
   f.runtime->mock_press(MockButton::wheel_press);break;
 }
 CHECK_EQ(c.directory(),"Other");f.select_name("Kick.wav");
 CHECK_OK(f.approve(NativeOperation::trash,"","",id));
 CHECK_EQ(f.finish(id),"done");
 CHECK(!f.t.exists("sd/Other/Kick.wav"));
 f.runtime->refresh();
 CHECK_EQ(c.selected_count(),0u);
}
TEST(native_runtime_fail_closed_on_multi_select_and_usb_revoke){
 Fixture f;std::string id;f.open_drums();
 CHECK_ERR(f.approve(NativeOperation::copy,"sdcard","Export",id),Err::invalid_argument);
 f.select_name("Kick.wav");f.select_name("Snare.wav");
 CHECK_ERR(f.approve(NativeOperation::copy,"sdcard","Export",id),Err::invalid_argument);
 f.runtime->mock_press(MockButton::select); // unselect Snare
 f.probe->exported={"mmcblk1"};
 CHECK_ERR(f.approve(NativeOperation::copy,"sdcard","Export",id),Err::unavailable);
 CHECK(!f.t.exists("sd/Export/Kick.wav"));
}
TEST(native_runtime_refuses_physical_display_without_handoff){
 Fixture f;std::string reason;
 CHECK(!f.runtime->start_display(reason));CHECK(!reason.empty());
 CHECK(!f.runtime->present(reason));
 DeniedBackend backend;RefuseOwnership deny;DisplaySession denied(deny,backend);
 NativeRuntime guarded(*f.service,f.jobs.get(),&denied);
 CHECK(!guarded.start_display(reason));
 CHECK_EQ(backend.acquires,0);
 CHECK(!guarded.present(reason));
}
TEST(native_runtime_test_granted_session_stops_on_revocation){
 Fixture f;DeniedBackend backend;GrantedForTest authority;
 DisplaySession session(authority,backend);
 NativeRuntime guarded(*f.service,f.jobs.get(),&session);
 std::string reason;CHECK(guarded.start_display(reason));
 CHECK(guarded.present(reason));CHECK_EQ(backend.submits,1);
 authority.allowed=false;
 CHECK(!guarded.present(reason));CHECK(!session.active());
 CHECK_EQ(backend.releases,1);
}
