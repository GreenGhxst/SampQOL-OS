#include "test_main.h"
#include "fakes.h"
#include "native/native_model.h"
#include "native/native_renderer.h"
#include "native/mock_inputs.h"
#include "native/native_jobs.h"
#include <chrono>
#include <memory>
#include <thread>
using namespace dm;
using namespace dm::native;
namespace {
struct Sample : Service {
 std::vector<Volume> volumes() override {return {{"sd","SD CARD",true,true}};}
 bool list(const std::string&,const std::string& path,std::size_t,std::size_t,
           std::vector<Item>& out,bool& more,std::string& err) override {
   out=path.empty()?std::vector<Item>{{"DRUMS",true},{"KICK.WAV",false}}:
                    std::vector<Item>{{"SNARE.WAV",false}};
   more=false;err.clear();return true;
 }
};
struct JobsFixture {
 TempDir t;
 FakeProbe* probe=nullptr;
 std::unique_ptr<VolumeRegistry> registry;
 Journal journal;
 FileOps ops;
 JobQueue queue;
 std::unique_ptr<NativeJobs> jobs;
 JobsFixture():journal(t.sub("j.jsonl")),ops(journal,0),queue(1,8){
   t.mkdir("sd");t.mkdir("sd/dst");t.mkdir("sd/other");t.mkdir("sd/autorun");
   t.write("sd/KICK.wav","kick audio");t.write("sd/autorun/locked","no");
   auto fp=std::make_unique<FakeProbe>();probe=fp.get();
   fp->table={{"/dev/mmcblk1p1",t.sub("sd"),"exfat","rw"}};
   auto rules=test_rules();rules[0].mount_prefix=t.sub("s");
   registry=std::make_unique<VolumeRegistry>(rules,std::move(fp));
   jobs=std::make_unique<NativeJobs>(*registry,ops,queue);
 }
 std::string finish(const std::string& id) {
   for(int i=0;i<500;++i){
     auto j=queue.get(id);
     if(!j.is_null()) {
       std::string state=j["state"];
       if(state=="done"||state=="failed"||state=="partial"||state=="cancelled")return state;
     }
     std::this_thread::sleep_for(std::chrono::milliseconds(5));
   }
   return "timeout";
 }
};
}
TEST(native_p0_renderer_produces_xrgb8888_and_highlights_cursor){
 Sample s;Controller c(s);NativeRenderer renderer;
 auto before=renderer.render(c);
 CHECK_EQ(before.size(),320u*240u*4u);
 CHECK_EQ(before[3],255u);
 c.press(Key::enter);
 auto files=renderer.render(c);
 CHECK(files!=before);
 c.press(Key::enter);
 auto nested=renderer.render(c);
 CHECK(nested!=files);
 c.press(Key::back);
 c.press(Key::down);
 auto moved=renderer.render(c);
 CHECK(files!=moved);
}
TEST(native_p0_mock_buttons_are_deterministic_and_isolated){
 Sample s;Controller c(s);MockInputs mock;
 CHECK(!mock.dispatch(MockButton::unknown,c));
 CHECK(c.screen()==Screen::volumes);
 CHECK(mock.dispatch(MockButton::wheel_press,c));
 CHECK(c.screen()==Screen::files);
 CHECK(mock.dispatch(MockButton::wheel_right,c));CHECK_EQ(c.cursor(),1u);
 CHECK(mock.dispatch(MockButton::select,c));CHECK(c.selected(1));
 CHECK(mock.dispatch(MockButton::refresh,c));CHECK(c.selected(1));
 CHECK(mock.dispatch(MockButton::back,c));CHECK(c.screen()==Screen::volumes);
}
TEST(native_p0_jobs_require_confirmation_and_safe_paths){
 JobsFixture f;std::string id;
 NativeRequest r{NativeOperation::copy,"sdcard","KICK.wav","sdcard","dst",false};
 CHECK_ERR(f.jobs->submit(r,id),Err::forbidden);CHECK(id.empty());
 r.confirmed=true;
 r.source_path="";CHECK_ERR(f.jobs->submit(r,id),Err::invalid_argument);
 r.source_path="../x";CHECK_ERR(f.jobs->submit(r,id),Err::invalid_argument);
 r.source_path="autorun/locked";CHECK_ERR(f.jobs->submit(r,id),Err::forbidden);
 r.source_path="KICK.wav";r.destination_dir="";
 CHECK_ERR(f.jobs->submit(r,id),Err::invalid_argument);
 r.destination_dir="dst";
 f.probe->exported={"mmcblk1"};
 CHECK_ERR(f.jobs->submit(r,id),Err::unavailable);
 f.probe->exported.clear();
}
TEST(native_p0_shared_copy_move_trash_and_collision){
 JobsFixture f;std::string id;
 NativeRequest r{NativeOperation::copy,"sdcard","KICK.wav","sdcard","dst",true};
 CHECK_OK(f.jobs->submit(r,id));CHECK_EQ(f.finish(id),"done");
 CHECK_EQ(f.t.read("sd/dst/KICK.wav"),"kick audio");
 CHECK_OK(f.jobs->submit(r,id));CHECK_EQ(f.finish(id),"failed");
 CHECK_EQ(f.t.read("sd/dst/KICK.wav"),"kick audio");
 r.op=NativeOperation::move;
 r.source_path="dst/KICK.wav";r.destination_dir="other";
 CHECK_OK(f.jobs->submit(r,id));CHECK_EQ(f.finish(id),"done");
 CHECK(!f.t.exists("sd/dst/KICK.wav"));
 CHECK_EQ(f.t.read("sd/other/KICK.wav"),"kick audio");
 r.op=NativeOperation::trash;r.source_path="other/KICK.wav";r.destination_volume.clear();r.destination_dir.clear();
 CHECK_OK(f.jobs->submit(r,id));CHECK_EQ(f.finish(id),"done");
 CHECK(!f.t.exists("sd/other/KICK.wav"));CHECK(f.t.exists("sd/KICK.wav"));
}
TEST(native_p0_jobs_refuse_recursive_destination){
 JobsFixture f;std::string id;
 NativeRequest r{NativeOperation::copy,"sdcard","dst","sdcard","dst/child",true};
 CHECK_ERR(f.jobs->submit(r,id),Err::invalid_argument);
}

TEST(native_p0_trash_index_failure_is_partial_and_journaled){
 JobsFixture f;std::string id;
 f.t.mkdir("sd/.diskmode");
 f.t.mkdir("sd/.diskmode/trash");
 // A directory at the index path makes the read fail deterministically.
 f.t.mkdir("sd/.diskmode/trash/index.json");
 NativeRequest r{NativeOperation::trash,"sdcard","KICK.wav","","",true};
 CHECK_OK(f.jobs->submit(r,id));
 CHECK_EQ(f.finish(id),"partial");
 CHECK(!f.t.exists("sd/KICK.wav"));
 auto job=f.queue.get(id);
 CHECK_EQ(job["state"],"partial");
 CHECK_EQ(job["summary"]["partial"],1);
 CHECK(job["items"][0]["error"].get<std::string>().find("trash slot")!=std::string::npos);
 auto records=f.journal.recent(5);
 CHECK(!records.empty());
 CHECK_EQ(records[0].state,"partial");
}
TEST(native_p0_destroy_owner_with_pending_job_does_not_access_dead_service){
 JobsFixture f;
 std::atomic<bool> release{false};
 std::string blocker,id;
 CHECK_OK(f.queue.submit("block","test",[&](Job&){
   while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds(2));
 },blocker));
 for(int i=0;i<500 && f.queue.get(blocker)["state"]!="running";++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
 CHECK_EQ(f.queue.get(blocker)["state"],"running");
 NativeRequest r{NativeOperation::copy,"sdcard","KICK.wav","sdcard","dst",true};
 CHECK_OK(f.jobs->submit(r,id));
 f.jobs.reset();
 release=true;
 CHECK_EQ(f.finish(id),"cancelled");
 CHECK(!f.t.exists("sd/dst/KICK.wav"));
}
