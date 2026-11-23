// DISKMODE native UI desktop simulator. Produces an actual 320x240 PPM
// framebuffer, never touches the MPC, real mounts, or SampQOL.
#include "native_model.h"
#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
namespace {
using namespace dm::native;
using Rows = std::array<std::uint8_t,7>;
const std::map<char,Rows> font = {
{'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},
{'C',{15,16,16,16,16,16,15}},{'D',{30,17,17,17,17,17,30}},
{'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},
{'G',{15,16,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},
{'I',{31,4,4,4,4,4,31}},{'J',{7,2,2,2,18,18,12}},
{'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
{'M',{17,27,21,21,17,17,17}},{'N',{17,25,21,19,17,17,17}},
{'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},
{'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},
{'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},
{'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},
{'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
{'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
{'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},
{'2',{14,17,1,2,4,8,31}},{'3',{30,1,1,14,1,1,30}},
{'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},
{'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},
{'8',{14,17,17,14,17,17,14}},{'9',{14,17,17,15,1,1,14}},
{'/',{1,2,2,4,8,8,16}},{'.',{0,0,0,0,0,12,12}},
{'-',{0,0,0,31,0,0,0}},{':',{0,12,12,0,12,12,0}},
{'_',{0,0,0,0,0,0,31}},{' ',{0,0,0,0,0,0,0}},
{'?',{14,17,1,2,4,0,4}},{'+',{0,4,4,31,4,4,0}}
};
struct Demo final : Service {
    std::vector<Volume> volumes() override {return {{"sd","DEMO SD",true,true},{"internal","DEMO INTERNAL",true,false}};}
    bool list(const std::string& v,const std::string& dir,std::size_t offset,std::size_t limit,
              std::vector<Item>& out,bool& more,std::string& error) override {
        out.clear();std::vector<Item> all;
        if(v=="sd" && dir.empty()) {
            all.push_back({"DRUMS",true});all.push_back({"SAMPLES",true});
            for(int i=0;i<220;++i)all.push_back({"SAMPLE"+std::to_string(i)+".WAV",false});
        } else if(v=="sd" && dir=="DRUMS") all={{"KICK.WAV",false},{"SNARE.WAV",false}};
        else if(v=="sd" && dir=="SAMPLES") all={{"PAD.WAV",false}};
        else if(v=="internal" && dir.empty())all={{"FACTORY",true}};
        else if(v=="internal" && dir=="FACTORY")all={{"READ ONLY",false}};
        else {error="Directory unavailable";return false;}
        for(std::size_t i=offset;i<all.size() && out.size()<limit;++i)out.push_back(all[i]);
        more=offset+out.size()<all.size();return true;
    }
};
struct Frame {
    static constexpr int w=320,h=240;
    std::vector<std::uint8_t> rgb=std::vector<std::uint8_t>(w*h*3,15);
    void rect(int x,int y,int width,int height,int r,int g,int b) {
        for(int yy=std::max(0,y);yy<std::min(h,y+height);++yy)
        for(int xx=std::max(0,x);xx<std::min(w,x+width);++xx) {
            auto i=(yy*w+xx)*3;rgb[i]=r;rgb[i+1]=g;rgb[i+2]=b;
        }
    }
    void str(int x,int y,const std::string& s,int r,int g,int b) {
        for(char c:s) {
            auto it=font.find(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            const Rows& rows=(it==font.end()?font.at('?'):it->second);
            for(int yy=0;yy<7;++yy)
            for(int xx=0;xx<5;++xx)if(rows[yy] & (1<<(4-xx)))rect(x+xx*2,y+yy*2,2,2,r,g,b);
            x+=12;if(x>w-10)break;
        }
    }
    bool save(const std::string& path) const {
        std::ofstream f(path,std::ios::binary);
        if(!f)return false;
        f<<"P6\n320 240\n255\n";
        f.write(reinterpret_cast<const char*>(rgb.data()),static_cast<std::streamsize>(rgb.size()));
        return bool(f);
    }
};
void draw(const Controller& c,Frame& f) {
    f.rect(0,0,320,240,15,19,24);
    f.rect(0,0,320,33,22,105,73);
    f.str(12,10,"DISKMODE / DESKTOP",255,255,255);
    std::string location=c.screen()==Screen::volumes?"VOLUMES":c.volume_id()+"/"+c.directory();
    f.str(10,42,location,130,220,175);
    if(c.screen()==Screen::error)f.str(10,80,"ERROR: "+c.error(),255,112,112);
    else {
        std::size_t count=c.screen()==Screen::volumes?c.volumes().size():c.entries().size();
        for(std::size_t row=0;row<7 && c.top()+row<count;++row) {
            auto idx=c.top()+row;auto y=64+static_cast<int>(row)*23;
            if(idx==c.cursor())f.rect(5,y-3,310,21,42,74,65);
            const bool is_vol=c.screen()==Screen::volumes;
            const std::string name=is_vol?c.volumes()[idx].label:c.entries()[idx].name;
            const std::string prefix=is_vol?(c.volumes()[idx].available?"V ":"X "):
                (c.selected(idx)?"* ":"  ");
            f.str(12,y,prefix+name,245,246,239);
        }
    }
    f.rect(0,224,320,16,31,40,49);
    f.str(9,225,"J/K NAV  E OPEN  B BACK  S PICK",214,221,222);
}
}
int main(int argc,char** argv) {
    using namespace dm::native;
    std::string output="diskmode-frame.ppm";
    bool smoke=argc>1 && std::string(argv[1])=="--smoke";
    if(argc>2)output=argv[2];
    Demo s; Controller c(s);
    auto render=[&] {
        Frame f;draw(c,f);
        if(!f.save(output)) {std::cerr<<"Cannot write frame: "<<output<<"\n";return false;}
        return true;
    };
    if(!render())return 2;
    if(smoke) {
        c.press(Key::enter);
        for(int i=0;i<130;++i)c.press(Key::down);
        return render()?0:2;
    }
    std::cout<<"DISKMODE desktop simulator (320x240 PPM), no hardware or device storage\n";
    std::cout<<"Frame: "<<output<<"\nCommands: j down, k up, e enter, b back, s select, r refresh, q quit\n";
    for(char key;std::cin>>key && key!='q';) {
        switch(key) {
            case 'j':c.press(Key::down);break;
            case 'k':c.press(Key::up);break;
            case 'e':c.press(Key::enter);break;
            case 'b':c.press(Key::back);break;
            case 's':c.press(Key::select);break;
            case 'r':c.press(Key::refresh);break;
            default:break;
        }
        if(!render())return 2;
        std::cout<<"Screen updated: "<<output<<"; cursor "<<c.cursor()<<" selected "<<c.selected_count()<<"\n";
    }
}
