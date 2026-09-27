#include "port/hnm5.h"

#include <algorithm>
#include <cstring>

namespace od::port {
namespace {
constexpr int8_t base[48][2] = {
    {0,0},{0,0},{0,0},{0,0},{0,0},{-8,-8},{-8,-8},{-8,-8},
    {-2,-8},{-14,-8},{-8,-2},{-8,-14},{-2,-2},{-14,-2},{-2,-14},{-14,-14},
    {-2,-8},{-14,-8},{-8,-2},{-8,-14},{-2,-2},{-14,-2},{-2,-14},{-14,-14},
    {-2,-8},{-14,-8},{-8,-2},{-8,-14},{-2,-2},{-14,-2},{-2,-14},{-14,-14},
    {0,0},{-16,-8},{-8,-16},{-24,-16},{8,-16},{-4,-32},{-12,-32},{4,-32},
    {0,0},{-16,-8},{-8,-16},{-24,-16},{8,-16},{-4,-32},{-12,-32},{4,-32}
};
constexpr uint8_t run_width[3][32] = {
    {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,18,20,22,24,26,28,30,32,34,36,38,40,44,48,52,56},
    {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,22,24,26,28,30,32,34,36,38,40,42,44},
    {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,28,30,32,34,36,38}
};
enum class MotionKind { signed16, negative16, normal, x, y };
struct Mode {
    size_t height=0;
    bool copy_previous=false;
    int x_bias=0,y_bias=0;
    MotionKind motion=MotionKind::signed16;
};
bool fail(std::string& error, const char* message) { error=message; return false; }
}

bool Hnm5Decoder::update_palette(const uint8_t* payload, size_t size,
                                 std::string& error) {
    error.clear();
    if (!payload && size) return fail(error,"HNM5 PL payload is absent");
    size_t pos=0,minimum=0;
    while (pos+2<=size) {
        const size_t start=payload[pos++];
        const size_t count_byte=payload[pos++];
        if (start==255 && count_byte==255) return true;
        const size_t count=count_byte ? count_byte : 256;
        if (start<minimum || start+count>256 || count>(size-pos)/3)
            return fail(error,"HNM5 PL palette range is invalid");
        for (size_t i=0; i<count; ++i) {
            const uint8_t r=payload[pos++],g=payload[pos++],b=payload[pos++];
            // WINDREAM's palette expansion masks VGA-style six-bit channels.
            palette_[start+i]=static_cast<uint16_t>(((r&0x3e)<<10)|((g&0x3f)<<5)|((b>>1)&0x1f));
        }
        minimum=start+count;
    }
    return fail(error,"HNM5 PL palette terminator is missing");
}

bool Hnm5Decoder::decode(const uint8_t* payload, size_t size,
                         std::vector<uint16_t>& rgb565, std::string& error) {
    error.clear();
    const size_t width=width_,height=height_,pixels=width*height;
    if (!payload || size<2 || payload[0]!=0xe0 || payload[1]<2)
        return fail(error,"HNM5 IV strip stream has no opener");
    if (indexed_[0].empty()) {
        indexed_[0].assign(pixels,0);
        indexed_[1].assign(pixels,0);
    }
    const unsigned dest=1-previous_;
    auto& frame=indexed_[dest];
    const auto& previous=indexed_[previous_];
    size_t pos=0,x=0,y=0;
    Mode mode;
    auto byte = [&](uint8_t& result) -> bool {
        if (pos>=size) return false;
        result=payload[pos++]; return true;
    };
    auto word = [&](uint16_t& result) -> bool {
        if (size-pos<2) return false;
        result=static_cast<uint16_t>(payload[pos] | (payload[pos+1]<<8));
        pos+=2; return true;
    };
    while (y<height && pos<size) {
        uint8_t op=0;
        if (!byte(op)) return fail(error,"HNM5 IV operation ended");
        if (op==0xe0) {
            uint8_t sub=0;
            if (!byte(sub)) return fail(error,"HNM5 IV special operation ended");
            if (sub==1) break;
            if (sub==0) {
                uint8_t count=0,color=0;
                if (!byte(count)||!byte(color)||!mode.height||x+size_t(count)+1>width)
                    return fail(error,"HNM5 IV color run is invalid");
                for (size_t row=0; row<mode.height; ++row)
                    std::fill_n(frame.begin()+(y+row)*width+x,size_t(count)+1,color);
                x+=size_t(count)+1;
                continue;
            }
            if (sub>=48 || (x!=0 && x!=width))
                return fail(error,"HNM5 IV strip initializer is invalid");
            y+=mode.height;
            mode.height = (sub==2||sub==5||(sub>=8&&sub<=15)||(sub>=32&&sub<=39)) ? 2 :
                          (sub==3||sub==6||(sub>=16&&sub<=23)||(sub>=40&&sub<=47)) ? 3 : 4;
            if (y+mode.height>height) return fail(error,"HNM5 IV strip exceeds frame");
            mode.copy_previous=sub<32;
            mode.x_bias=base[sub][0]; mode.y_bias=base[sub][1];
            if (sub>=2&&sub<=4) mode.motion=MotionKind::signed16;
            else if (sub==32||sub==40) mode.motion=MotionKind::negative16;
            else if (sub==33||sub==41) mode.motion=MotionKind::x;
            else if ((sub>=37&&sub<=39)||(sub>=45&&sub<=47)) mode.motion=MotionKind::y;
            else mode.motion=MotionKind::normal;
            x=0;
            continue;
        }
        if (!mode.height) return fail(error,"HNM5 IV draws before strip initialization");
        if (op==0x20) {
            uint8_t count=0;
            if (!byte(count)||x+size_t(count)+1>width)
                return fail(error,"HNM5 IV previous-frame run is invalid");
            for (size_t row=0; row<mode.height; ++row)
                std::memcpy(frame.data()+(y+row)*width+x,
                            previous.data()+(y+row)*width+x,size_t(count)+1);
            x+=size_t(count)+1;
            continue;
        }
        if (op==0x60) {
            if (x>=width || size-pos<mode.height)
                return fail(error,"HNM5 IV raw column is truncated");
            for (size_t row=0; row<mode.height; ++row)
                frame[(y+row)*width+x]=payload[pos++];
            ++x; continue;
        }
        if (op==0xa0) {
            uint8_t count=0;
            if (!byte(count)||x+size_t(count)+2>width ||
                (size-pos)/(mode.height)<size_t(count)+2)
                return fail(error,"HNM5 IV raw run is truncated");
            for (size_t col=0; col<size_t(count)+2; ++col,++x)
                for (size_t row=0; row<mode.height; ++row)
                    frame[(y+row)*width+x]=payload[pos++];
            continue;
        }
        const size_t span=run_width[mode.height-2][op&31];
        if (x+span>width) return fail(error,"HNM5 IV motion run exceeds row");
        const unsigned transform=op>>5;
        ptrdiff_t motion=0;
        uint16_t vector=0;
        uint8_t packed=0;
        if (mode.motion==MotionKind::signed16 || mode.motion==MotionKind::negative16) {
            if (!word(vector)) return fail(error,"HNM5 IV long motion is truncated");
            motion=static_cast<ptrdiff_t>(vector)-(mode.motion==MotionKind::signed16 ? 0x8000 : 0x10000);
        } else {
            if (!byte(packed)) return fail(error,"HNM5 IV short motion is truncated");
            int dx=0,dy=0;
            if (mode.motion==MotionKind::normal) { dx=packed&15; dy=packed>>4; }
            else if (mode.motion==MotionKind::x) { dx=packed&31; dy=packed>>5; }
            else { dx=packed&7; dy=packed>>3; }
            motion=dx+mode.x_bias+(dy+mode.y_bias)*static_cast<ptrdiff_t>(width);
        }
        ptrdiff_t source=static_cast<ptrdiff_t>(y*width+x)+motion;
        ptrdiff_t pixel_step=1,row_step=width;
        if (transform&4) std::swap(pixel_step,row_step);
        if (transform&2) row_step=-row_step;
        if (transform&1) pixel_step=-pixel_step;
        const auto& src=mode.copy_previous ? previous : frame;
        for (size_t row=0; row<mode.height; ++row)
            for (size_t col=0; col<span; ++col) {
                const ptrdiff_t at=source+static_cast<ptrdiff_t>(row)*row_step+
                                   static_cast<ptrdiff_t>(col)*pixel_step;
                if (at<0 || at>=static_cast<ptrdiff_t>(pixels))
                    return fail(error,"HNM5 IV motion source exceeds frame");
                frame[(y+row)*width+x+col]=src[static_cast<size_t>(at)];
            }
        x+=span;
    }
    previous_=dest;
    expand(rgb565);
    return true;
}

void Hnm5Decoder::expand(std::vector<uint16_t>& rgb565) const {
    const size_t pixels=static_cast<size_t>(width_)*height_;
    const auto& frame=indexed_[previous_];
    rgb565.resize(pixels);
    for (size_t i=0; i<pixels; ++i) rgb565[i]=palette_[frame[i]];
}

} // namespace od::port
