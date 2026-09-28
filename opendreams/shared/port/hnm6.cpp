#include "port/hnm6.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace od::port {

void HNM6_StoreBlockRGB16(const std::array<std::array<int16_t, 64>, 3>& planes,
                          std::vector<uint16_t>& dst, int x0, int y0, int width) {
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        const int i = y * 8 + x;
        const int luminance = planes[0][i] + 128;
        const int u = planes[1][i], v = planes[2][i];
        const int red_offset = (16 * v + 8) / 10;
        const int green_offset = u / 3;
        // Retail StoreBlockRGB16 (0x47f814) adds U_raw >> 3 to blue.
        // The portable IDCT has already shifted each plane down by 4.
        const int red5 = std::clamp((luminance + red_offset) >> 3, 0, 31);
        const int green6 = std::clamp((luminance - (red_offset >> 1) -
                                       green_offset) >> 2, 0, 62);
        const int blue5 = std::clamp((luminance + 2 * u) >> 3, 0, 31);
        dst[(y0 + y) * width + x0 + x] =
            static_cast<uint16_t>((red5 << 11) | (green6 << 5) | blue5);
    }
}

namespace {

uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

constexpr uint8_t zigzag[64] = {
     0, 1, 8,16, 9, 2, 3,10,17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34,27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
};
constexpr uint8_t luma_base[64] = {
    16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,
    14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,
    18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,
    49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99
};
constexpr uint8_t chroma_base[64] = {
    17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,
    24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99
};

class Bits {
public:
    Bits(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    bool read(unsigned count, uint32_t& result) {
        result = 0;
        if (count > 32 || bit_ > size_ * 8 || count > size_ * 8 - bit_) return false;
        for (unsigned i = 0; i < count; ++i, ++bit_) {
            const size_t word_start = (bit_ / 32) * 4;
            const unsigned in_word = static_cast<unsigned>(bit_ % 32);
            uint32_t word = 0;
            for (size_t j = 0; j < 4 && word_start + j < size_; ++j)
                word |= static_cast<uint32_t>(data_[word_start + j]) << (j * 8);
            result = (result << 1) | ((word >> (31 - in_word)) & 1);
        }
        return true;
    }
private:
    const uint8_t* data_;
    size_t size_, bit_ = 0;
};

class Nibbles {
public:
    Nibbles(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    bool read(uint8_t& result) {
        if (index_ / 2 >= size_) return false;
        const uint8_t b = data_[index_ / 2];
        result = (index_++ & 1) ? b >> 4 : b & 15;
        return true;
    }
    bool signed4(int16_t& result) {
        uint8_t b = 0;
        if (!read(b)) return false;
        result = static_cast<int16_t>(b < 8 ? b : static_cast<int>(b) - 16);
        return true;
    }
    bool signed8(int16_t& result) {
        uint8_t a = 0, b = 0;
        if (!read(a) || !read(b)) return false;
        result = static_cast<int8_t>((a << 4) | b);
        return true;
    }
    bool pair2(int16_t& a, int16_t& b) {
        uint8_t n = 0;
        if (!read(n)) return false;
        constexpr int16_t values[4] = {1, 2, -2, -1};
        a = values[n >> 2]; b = values[n & 3];
        return true;
    }
private:
    const uint8_t* data_;
    size_t size_, index_ = 0;
};

class Shorts {
public:
    Shorts(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    bool read(uint16_t& result) {
        if (!pending_) {
            if (offset_ + 2 > size_) return false;
            const uint16_t word = le16(data_ + offset_);
            offset_ += 2;
            cached_ = word >> 12;
            result = word & 0x0fff;
            pending_ = true;
        } else {
            if (offset_ >= size_) return false;
            result = static_cast<uint16_t>((data_[offset_++] << 4) | cached_);
            pending_ = false;
        }
        return true;
    }
private:
    const uint8_t* data_;
    size_t size_, offset_ = 0;
    uint16_t cached_ = 0;
    bool pending_ = false;
};

enum class Op { dct, horizontal, vertical, quarters, motion, short_motion, skip };

struct Block { int x, y, w, h; };

bool operation(Bits& bits, const Block& b, bool key, Op& op) {
    uint32_t p = 0;
    if (key) {
        if (b.w == 2 && b.h == 2) { op = Op::motion; return true; }
        if (b.w == 4 && b.h == 2) {
            if (!bits.read(1, p)) return false;
            op = p ? Op::vertical : Op::motion; return true;
        }
        if (b.w == 2 && b.h == 4) {
            if (!bits.read(1, p)) return false;
            op = p ? Op::horizontal : Op::motion; return true;
        }
        if (b.w == 8 && b.h == 4) {
            if (!bits.read(1, p)) return false;
            op = p ? Op::motion : Op::vertical; return true;
        }
        if (b.w == 4 && b.h == 8) {
            if (!bits.read(1, p)) return false;
            op = p ? Op::motion : Op::horizontal; return true;
        }
        if (!bits.read(2, p)) return false;
        if (b.w == 4) {
            op = p == 0 ? Op::motion : p == 1 ? Op::horizontal :
                 p == 2 ? Op::vertical : Op::quarters;
            return true;
        }
        if (p == 0) op = Op::horizontal;
        else if (p == 1) op = Op::vertical;
        else if (p == 2) op = Op::quarters;
        else { if (!bits.read(1, p)) return false; op = p ? Op::motion : Op::dct; }
        return true;
    }
    if (b.w == 2 && b.h == 2) {
        if (!bits.read(1, p)) return false;
        if (!p) { op = Op::short_motion; return true; }
        if (!bits.read(1, p)) return false;
        op = p ? Op::skip : Op::motion; return true;
    }
    if ((b.w == 2 && b.h == 4) || (b.w == 4 && b.h == 2)) {
        if (!bits.read(1, p)) return false;
        if (!p) { op = Op::short_motion; return true; }
        if (!bits.read(1, p)) return false;
        if (!p) { op = Op::motion; return true; }
        if (!bits.read(1, p)) return false;
        op = !p ? Op::skip : b.w == 2 ? Op::horizontal : Op::vertical;
        return true;
    }
    if (!bits.read(2, p)) return false;
    if (b.w == 4 && b.h == 8) {
        op = p == 0 ? Op::short_motion : p == 1 ? Op::motion :
             p == 2 ? Op::horizontal : Op::skip; return true;
    }
    if (b.w == 8 && b.h == 4) {
        op = p == 0 ? Op::short_motion : p == 1 ? Op::motion :
             p == 2 ? Op::vertical : Op::skip; return true;
    }
    if (b.w == 4) {
        if (p == 0) op = Op::short_motion;
        else if (p == 1) op = Op::motion;
        else {
            const uint32_t prefix = p;
            if (!bits.read(1, p)) return false;
            op = prefix == 2 ? (p ? Op::horizontal : Op::skip) :
                 (p ? Op::quarters : Op::vertical);
        }
        return true;
    }
    if (p == 0) { op = Op::short_motion; return true; }
    const uint32_t prefix = p;
    if (!bits.read(1, p)) return false;
    if (prefix == 1) op = p ? Op::dct : Op::motion;
    else if (prefix == 2) op = p ? Op::vertical : Op::horizontal;
    else {
        op = p ? Op::quarters : Op::skip;
        if (p && (!bits.read(1,p) || p)) return false;
    }
    return true;
}

bool coefficients(Nibbles& n, std::array<int16_t, 64>& block) {
    block.fill(0);
    if (!n.signed8(block[0])) return false;
    size_t i = 1;
    auto place = [&](int16_t value) -> bool {
        if (i >= 64) return false;
        block[zigzag[i++]] = value;
        return true;
    };
    while (i < 64) {
        uint8_t code = 0;
        if (!n.read(code)) return false;
        if (code == 0) break;
        if (code == 1) { i += 5; if (i > 64) return false; continue; }
        if (code >= 2 && code <= 7) {
            const size_t skip = static_cast<size_t>((code - 2) / 2 + 1);
            i += skip;
            if (!place(code & 1 ? -1 : 1)) return false;
            continue;
        }
        if (code == 8 || code == 9) {
            uint8_t count = 0;
            if (!n.read(count)) return false;
            i += (count >> 2) + 1;
            if (i > 64) return false;
            const int total = (count & 3) + (code == 8 ? 2 : 1);
            for (int k = 0; k < total; ++k) {
                int16_t value = 0;
                if (code == 8) {
                    int16_t unused = 0;
                    if (!n.pair2(value, unused)) return false;
                    if (!place(value)) return false;
                    if (++k < total && !place(unused)) return false;
                } else {
                    if (!n.signed4(value) || !place(value)) return false;
                }
            }
            continue;
        }
        if (code == 14) { ++i; continue; }
        const int total = code == 10 || code == 12 ? 2 : code == 13 ? 3 : 1;
        for (int k = 0; k < total; ++k) {
            int16_t value = 0;
            if (code == 10) {
                int16_t other = 0;
                if (!n.pair2(value, other) || !place(value) || !place(other)) return false;
                break;
            }
            if (!(code == 15 ? n.signed8(value) : n.signed4(value)) || !place(value)) return false;
        }
    }
    return true;
}

// The same two-pass integer JPEG transform used by Cryo's coefficient path.
void idct(std::array<int16_t, 64>& a) {
    constexpr int w1=2841,w2=2676,w3=2408,w5=1609,w6=1108,w7=565,w8=181;
    auto pass = [&](int offset, int stride, int input_shift, int output_shift, bool column) {
        const int32_t v0 = static_cast<int32_t>(a[offset]) * (1 << input_shift) +
            (1 << (output_shift - 1));
        const int32_t v1 = static_cast<int32_t>(a[offset + 4*stride]) * (1 << input_shift);
        const int32_t v2 = a[offset + 6*stride], v3 = a[offset + 2*stride];
        const int32_t v4 = a[offset + stride], v5 = a[offset + 7*stride];
        const int32_t v6 = a[offset + 5*stride], v7 = a[offset + 3*stride];
        const int32_t t0 = w7*(v4+v5), t1 = w3*(v6+v7);
        const int32_t a4 = (t0+(w1-w7)*v4) >> (column ? 3 : 0);
        const int32_t a5 = (t0-(w1+w7)*v5) >> (column ? 3 : 0);
        const int32_t a6 = (t1-(w3-w5)*v6) >> (column ? 3 : 0);
        const int32_t a7 = (t1-(w3+w5)*v7) >> (column ? 3 : 0);
        const int32_t sum=v0+v1, diff=v0-v1, t2=w6*(v2+v3);
        const int32_t a2=(t2-(w2+w6)*v2) >> (column ? 3 : 0);
        const int32_t a3=(t2+(w2-w6)*v3) >> (column ? 3 : 0);
        const int32_t b1=a4+a6, b4=a4-a6, t3=a5-a7, b6=a5+a7;
        const int32_t b7=sum+a3,b5=sum-a3,b3=diff+a2,b0=diff-a2;
        const int32_t b2=(w8*(b4+t3)+128)>>8, b8=(w8*(b4-t3)+128)>>8;
        a[offset] = static_cast<int16_t>((b7+b1)>>output_shift);
        a[offset+7*stride] = static_cast<int16_t>((b7-b1)>>output_shift);
        a[offset+stride] = static_cast<int16_t>((b3+b2)>>output_shift);
        a[offset+6*stride] = static_cast<int16_t>((b3-b2)>>output_shift);
        a[offset+2*stride] = static_cast<int16_t>((b0+b8)>>output_shift);
        a[offset+5*stride] = static_cast<int16_t>((b0-b8)>>output_shift);
        a[offset+3*stride] = static_cast<int16_t>((b5+b6)>>output_shift);
        a[offset+4*stride] = static_cast<int16_t>((b5-b6)>>output_shift);
    };
    for (int y=0; y<8; ++y) pass(y*8,1,11,8,false);
    for (int x=0; x<8; ++x) pass(x,8,8,14,true);
}

std::pair<int,int> uncoil(uint16_t value) {
    int dx=1,dy=0,lim=4,low=0,edge=2;
    while (value >= lim) { low=lim; edge+=2; lim=edge*edge; ++dx; ++dy; }
    --edge;
    const int dir=(value-low)/edge, pos=(value-low)%edge;
    if (dir==0) dy-=pos;
    else if (dir==1) { dx-=pos+1; dy-=edge-1; }
    else if (dir==2) { dx-=edge; dy+=pos+2-edge; }
    else { dx+=pos+1-edge; ++dy; }
    return {dx,dy};
}

bool copy_block(std::vector<uint16_t>& dst, const std::vector<uint16_t>& prev,
                const Block& b, int dx, int dy, bool current, unsigned mode,
                int width, int height) {
    if (mode == 4 || mode == 7) mode ^= 3;
    const auto& src = current ? dst : prev;
    const bool mirror=(mode&1)!=0, flip=(mode&2)!=0, transpose=(mode&4)!=0;
    ptrdiff_t source = static_cast<ptrdiff_t>(b.y*width+b.x+dx+dy*width);
    ptrdiff_t xstep = transpose ? width : 1;
    ptrdiff_t rowstep = transpose ? 1 : width;
    if (flip) rowstep = -rowstep;
    if (mirror) xstep = -xstep;
    if (flip) source -= rowstep*(b.h-1);
    if (mirror) source -= xstep*(b.w-1);
    for (int y=0; y<b.h; ++y) for (int x=0; x<b.w; ++x) {
        const ptrdiff_t index=source+x*xstep+y*rowstep;
        if (index<0 || index>=static_cast<ptrdiff_t>(width*height)) return false;
        dst[(b.y+y)*width+b.x+x] = src[static_cast<size_t>(index)];
    }
    return true;
}

struct DecodeContext {
    Bits bits;
    Nibbles dct;
    Shorts short_vectors;
    const uint8_t* vectors;
    size_t vectors_size, vectors_pos=0;
    const std::array<int16_t,64>& yq;
    const std::array<int16_t,64>& cq;
    std::vector<uint16_t>& dst;
    const std::vector<uint16_t>& prev;
    bool key;
    int width,height;
    std::string failure;

    bool block(const Block& b, unsigned depth=0) {
        auto failed = [&](const char* why) {
            if (failure.empty()) failure = std::string(why) + " at " +
                std::to_string(b.x) + "," + std::to_string(b.y) +
                " block " + std::to_string(b.w) + "x" + std::to_string(b.h);
            return false;
        };
        if (depth > 8 || b.x < 0 || b.y < 0 || b.x+b.w > width || b.y+b.h > height)
            return failed("invalid block bounds");
        Op op{};
        if (!operation(bits,b,key,op)) return failed("block operation ended");
        if (op==Op::horizontal) {
            const Block a{b.x,b.y,b.w,b.h/2};
            return block(a,depth+1) && block({b.x,b.y+a.h,a.w,a.h},depth+1);
        }
        if (op==Op::vertical) {
            const Block a{b.x,b.y,b.w/2,b.h};
            return block(a,depth+1) && block({b.x+a.w,b.y,a.w,a.h},depth+1);
        }
        if (op==Op::quarters) {
            const Block a{b.x,b.y,b.w/2,b.h/2};
            return block(a,depth+1) && block({b.x+a.w,b.y,a.w,a.h},depth+1) &&
                block({b.x,b.y+a.h,a.w,a.h},depth+1) &&
                block({b.x+a.w,b.y+a.h,a.w,a.h},depth+1);
        }
        if (op==Op::skip) {
            for (int y=0; y<b.h; ++y)
                std::memcpy(dst.data()+(b.y+y)*width+b.x,
                            prev.data()+(b.y+y)*width+b.x, b.w*sizeof(uint16_t));
            return true;
        }
        if (op==Op::dct) {
            if (b.w!=8 || b.h!=8) return failed("DCT at subblock");
            std::array<std::array<int16_t,64>,3> planes{};
            for (int p=0; p<3; ++p) {
                if (!coefficients(dct,planes[p])) return failed("DCT coefficients ended");
                const auto& quant=p==0 ? yq : cq;
                for (int i=0; i<64; ++i)
                    planes[p][i]=static_cast<int16_t>(planes[p][i]*quant[i]);
                idct(planes[p]);
            }
            HNM6_StoreBlockRGB16(planes, dst, b.x, b.y, width);
            return true;
        }
        int dx=0,dy=0;
        unsigned mode=0;
        bool current=false;
        if (op==Op::short_motion) {
            uint16_t code=0;
            if (!short_vectors.read(code)) return failed("short motion ended");
            const auto motion=uncoil(code);
            dx=motion.first; dy=motion.second;
        } else {
            if (vectors_pos+2>vectors_size) return failed("motion vector ended");
            const uint16_t word=le16(vectors+vectors_pos);
            vectors_pos+=2;
            const bool large=b.w>=4 && b.h>=4;
            current=(word&0x8000)!=0;
            if (key && large) {
                uint32_t bits_value=0;
                if (!bits.read(2,bits_value)) return failed("key motion mode ended");
                mode=bits_value*2+(word>>15);
                dx=128-((word>>7)&255);
                dy=(b.w==8 && b.h==8 ? 0 : 4)-(word&127);
                current=true;
            } else if (key || (!large && current)) {
                mode=(word>>12)&7;
                dx=63-(word&127); dy=6-((word>>7)&31);
                current=true;
            } else if (large) {
                uint32_t bits_value=0;
                if (!bits.read(3,bits_value)) return failed("inter motion mode ended");
                mode=bits_value;
                dx=128-((word>>7)&255);
                dy=(!current ? 64 : b.w==8 && b.h==8 ? 0 : 4)-(word&127);
            } else {
                mode=(word>>12)&7;
                dx=31-(word&63); dy=31-((word>>6)&63);
                current=false;
            }
            dx-=b.x&7; dy-=b.y&7;
            if (large) {
                if (b.x+dx<0) dx+=width;
                else if (b.x+dx>=width) dx-=width;
            }
        }
        if (!copy_block(dst,prev,b,dx,dy,current,mode,width,height)) {
            const std::string detail = "motion source exceeded frame (dx=" +
                std::to_string(dx) + ", dy=" + std::to_string(dy) +
                ", mode=" + std::to_string(mode) +
                ", current=" + std::to_string(current) + ")";
            return failed(detail.c_str());
        }
        return true;
    }
};

} // namespace

void Hnm6Decoder::set_quality(int q) {
    if (q==quality_) return;
    quality_=q;
    const int scale=q<50 ? 5000/q : 200-2*q;
    for (int i=0; i<64; ++i) {
        luma_quant_[i]=static_cast<int16_t>(std::clamp((luma_base[i]*scale+50)/100,8,255));
        chroma_quant_[i]=static_cast<int16_t>(std::clamp((chroma_base[i]*scale+50)/100,8,255));
    }
}

bool Hnm6Decoder::decode(const uint8_t* payload, size_t size,
                         const std::vector<uint16_t>& previous,
                         std::vector<uint16_t>& destination, std::string& error) {
    error.clear();
    const size_t pixels=static_cast<size_t>(width_)*height_;
    if (!payload || size<24 || previous.size()!=pixels) {
        error="HNM6 frame header or previous frame is missing"; return false;
    }
    const int32_t signed_quality=static_cast<int32_t>(le32(payload));
    if (!signed_quality || signed_quality < -100 || signed_quality > 100) {
        error="HNM6 frame quality is outside the retail range"; return false;
    }
    const size_t bo=le32(payload+4), mo=le32(payload+8), so=le32(payload+12);
    const size_t jo=le32(payload+16), je=le32(payload+20);
    if (bo>size || mo>size || so>size || jo>je || je>size) {
        error="HNM6 frame stream offsets exceed the IX payload"; return false;
    }
    set_quality(signed_quality<0 ? -signed_quality : signed_quality);
    destination.assign(pixels,0);
    DecodeContext context{Bits(payload+bo,size-bo), Nibbles(payload+jo,je-jo),
        Shorts(payload+so,size-so), payload+mo, size-mo, 0,
        luma_quant_,chroma_quant_,destination,previous,signed_quality<0,width_,height_};
    for (int y=0; y<height_; y+=8) for (int x=0; x<width_; x+=8) {
        if (!context.block({x,y,8,8})) {
            error="HNM6 IX " + context.failure;
            destination.clear(); return false;
        }
    }
    return true;
}

} // namespace od::port
