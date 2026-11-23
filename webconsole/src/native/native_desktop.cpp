// Host-only DISKMODE simulator. Uses the SAME renderer/runtime as the
// guarded application, converts its XRGB8888 framebuffer to a 320x240 PPM.
// Does not discover device nodes, open mounts, or take stock screen ownership.
#include "native_runtime.h"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
namespace {
using namespace dm::native;
struct Demo final:Service {
 std::vector<Volume> volumes() override {
   return {{"sd","DEMO SD",true,true},{"internal","DEMO INTERNAL",true,false}};
 }
 bool list(const std::string& v,const std::string& dir,std::size_t offset,
           std::size_t limit,std::vector<Item>& out,bool& more,std::string& error) override {
   out.clear();error.clear();std::vector<Item> all;
   if(v=="sd"&&dir.empty()){
     all={{"DRUMS",true},{"SAMPLES",true}};
     for(int i=0;i<220;++i)all.push_back({"SAMPLE"+std::to_string(i)+".WAV",false});
   } else if(v=="sd"&&dir=="DRUMS")all={{"KICK.WAV",false},{"SNARE.WAV",false}};
   else if(v=="sd"&&dir=="SAMPLES")all={{"PAD.WAV",false}};
   else if(v=="internal"&&dir.empty())all={{"FACTORY",true}};
   else if(v=="internal"&&dir=="FACTORY")all={{"READ ONLY",false}};
   else {error="Directory unavailable";return false;}
   for(std::size_t i=offset;i<all.size()&&out.size()<limit;++i)out.push_back(all[i]);
   more=offset+out.size()<all.size();return true;
 }
};
bool save_ppm(const std::string& path,const NativeRenderer::Frame& pixels) {
 if(pixels.size()!=320u*240u*4u)return false;
 std::ofstream f(path,std::ios::binary);if(!f)return false;
 f<<"P6\n320 240\n255\n";
 std::vector<char> rgb(320u*240u*3u);
 for(std::size_t i=0,j=0;i<pixels.size();i+=4,j+=3) {
    rgb[j]=static_cast<char>(pixels[i+2]);
    rgb[j+1]=static_cast<char>(pixels[i+1]);
    rgb[j+2]=static_cast<char>(pixels[i]);
 }
 f.write(rgb.data(),static_cast<std::streamsize>(rgb.size()));return bool(f);
}
}
int main(int argc,char** argv) {
 Demo service;NativeRuntime runtime(service);
 const bool smoke=argc>1&&std::string(argv[1])=="--smoke";
 const std::string output=argc>2?argv[2]:"diskmode-frame.ppm";
 auto write=[&](){return save_ppm(output,runtime.frame());};
 if(!write()){std::cerr<<"Cannot write frame\n";return 2;}
 if(smoke) {
   runtime.mock_press(MockButton::wheel_press);
   for(int i=0;i<130;++i)runtime.mock_press(MockButton::wheel_right);
   return write()?0:2;
 }
 std::cout<<"DISKMODE guarded native runtime desktop simulation, 320x240 PPM\n"
          <<"No physical MPC display or storage access\n"
          <<"Commands: j down, k up, e enter, b back, s select, r refresh, q quit\n";
 for(char key;std::cin>>key&&key!='q';){
   MockButton b=MockButton::unknown;
   switch(key){
    case 'j':b=MockButton::wheel_right;break;
    case 'k':b=MockButton::wheel_left;break;
    case 'e':b=MockButton::wheel_press;break;
    case 'b':b=MockButton::back;break;
    case 's':b=MockButton::select;break;
    case 'r':b=MockButton::refresh;break;
    default:break;
   }
   runtime.mock_press(b);
   if(!write())return 2;
   std::cout<<"Frame updated: "<<output<<"\n";
 }
 return 0;
}
