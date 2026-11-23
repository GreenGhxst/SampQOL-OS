#include "native_renderer.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
namespace dm::native {
namespace {
using Frame=NativeRenderer::Frame;
void rect(Frame& f,int x,int y,int w,int h,std::uint32_t rgb) {
    const int l=std::max(0,x),t=std::max(0,y),r=std::min(320,x+w),b=std::min(240,y+h);
    for(int yy=t;yy<b;++yy)for(int xx=l;xx<r;++xx) {
        const auto i=(static_cast<std::size_t>(yy)*320+xx)*4;
        f[i]=static_cast<std::uint8_t>(rgb);
        f[i+1]=static_cast<std::uint8_t>(rgb>>8);
        f[i+2]=static_cast<std::uint8_t>(rgb>>16);
        f[i+3]=255;
    }
}
// Compact, deterministic 3x5 uppercase font. Unknown glyphs render '?'.
const std::array<std::uint8_t,5>& glyph(char c) {
    static const std::array<std::uint8_t,5> empty{{0,0,0,0,0}};
    static const std::array<std::uint8_t,5> unknown{{7,1,2,0,2}};
    static const std::array<std::array<std::uint8_t,5>,36> chars{{
        {{7,5,5,5,7}},{{2,6,2,2,7}},{{7,1,7,4,7}},{{7,1,7,1,7}},
        {{5,5,7,1,1}},{{7,4,7,1,7}},{{7,4,7,5,7}},{{7,1,1,1,1}},
        {{7,5,7,5,7}},{{7,5,7,1,7}},
        {{2,5,7,5,5}},{{6,5,6,5,6}},{{3,4,4,4,3}},{{6,5,5,5,6}},
        {{7,4,6,4,7}},{{7,4,6,4,4}},{{3,4,5,5,3}},{{5,5,7,5,5}},
        {{7,2,2,2,7}},{{1,1,1,5,2}},{{5,5,6,5,5}},{{4,4,4,4,7}},
        {{5,7,7,5,5}},{{5,7,7,7,5}},{{2,5,5,5,2}},{{6,5,6,4,4}},
        {{2,5,5,7,3}},{{6,5,6,5,5}},{{3,4,2,1,6}},{{7,2,2,2,2}},
        {{5,5,5,5,7}},{{5,5,5,5,2}},{{5,5,7,7,5}},{{5,5,2,5,5}},
        {{5,5,2,2,2}},{{7,1,2,4,7}}
    }};
    static const std::array<std::uint8_t,5> slash{{1,1,2,4,4}};
    static const std::array<std::uint8_t,5> dot{{0,0,0,0,2}};
    static const std::array<std::uint8_t,5> dash{{0,0,7,0,0}};
    static const std::array<std::uint8_t,5> colon{{0,2,0,2,0}};
    static const std::array<std::uint8_t,5> star{{0,5,2,5,0}};
    static const std::array<std::uint8_t,5> underscore{{0,0,0,0,7}};
    if(c>='0' && c<='9')return chars[c-'0'];
    if(c>='A' && c<='Z')return chars[10+c-'A'];
    if(c==' ')return empty;
    if(c=='/')return slash;if(c=='.')return dot;if(c=='-')return dash;
    if(c==':')return colon;if(c=='*')return star;if(c=='_')return underscore;
    return unknown;
}
void text(Frame& f,int x,int y,const std::string& value,std::uint32_t rgb,int max_chars=44) {
    int used=0;
    for(unsigned char raw:value) {
        if(used++>=max_chars || x>312)break;
        const auto& g=glyph(static_cast<char>(std::toupper(raw)));
        for(int gy=0;gy<5;++gy)for(int gx=0;gx<3;++gx)
            if(g[gy]&(1<<(2-gx)))rect(f,x+gx*2,y+gy*2,2,2,rgb);
        x+=7;
    }
}
}
NativeRenderer::Frame NativeRenderer::render(const Controller& c) const {
    Frame f(static_cast<std::size_t>(width)*height*4,0);
    rect(f,0,0,320,240,0x121a20);
    rect(f,0,0,320,31,0x126749);
    text(f,10,9,"DISKMODE",0xffffff);
    const std::string location=c.screen()==Screen::volumes?"VOLUMES":c.volume_id()+"/"+c.directory();
    text(f,9,39,location,0x9ed9b4);
    if(c.screen()==Screen::error) {
        rect(f,6,65,308,48,0x652d35);
        text(f,12,74,"ERROR",0xffffff);
        text(f,12,92,c.error(),0xffced0);
    } else {
        const bool is_volume=c.screen()==Screen::volumes;
        const std::size_t size=is_volume?c.volumes().size():c.entries().size();
        for(std::size_t row=0;row<7 && c.top()+row<size;++row) {
            const std::size_t i=c.top()+row;
            const int y=59+static_cast<int>(row)*23;
            if(i==c.cursor())rect(f,5,y-4,310,21,0x315c53);
            const bool available=is_volume?c.volumes()[i].available:true;
            const bool folder=!is_volume && c.entries()[i].directory;
            const std::string name=is_volume?c.volumes()[i].label:c.entries()[i].name;
            const std::string prefix=is_volume?(available?"V ":"X "):
                                     (c.selected(i)?"* ":(folder?"+ ":"  "));
            text(f,12,y,prefix+name,available?0xf0f6ef:0x87938d,42);
        }
    }
    rect(f,0,225,320,15,0x202c33);
    text(f,8,227,"UP DOWN OPEN BACK SELECT REFRESH",0xc8ddda);
    return f;
}
NativeRenderer::Frame NativeRenderer::render(const Controller& controller,
                                            const std::vector<std::string>& review_lines) const {
    Frame f=render(controller);
    if(review_lines.empty())return f;
    // The preview is fully contained within the physical framebuffer.
    rect(f,3,49,314,156,0x142d30);
    rect(f,3,49,314,18,0x7b5832);
    text(f,10,53,"CONFIRM FILE ACTION",0xffffff);
    int y=74;
    for(const auto& line:review_lines) {
        text(f,10,y,line,0xf3eedb,43);
        y+=28;
    }
    return f;
}
} // namespace dm::native
