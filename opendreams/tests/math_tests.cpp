#define _CRT_SECURE_NO_WARNINGS
#include "port/animation.h"
#include "port/math.h"
#include "port/scene.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

// FNV-1a over little-endian int32 entries. The expected values come from a
// 32-bit x87 replay of WINDREAM.EXE 0x45b040 (FSINCOS, FPATAN, __CHP) at both
// 53- and 64-bit precision control; both produced identical tables.
uint64_t fnv(const std::array<int32_t,4096>& table) {
    uint64_t hash=0xcbf29ce484222325ull;
    for (const int32_t value : table) {
        uint32_t bits=0;
        std::memcpy(&bits,&value,sizeof(bits));
        for (int shift=0; shift<32; shift+=8) {
            hash^=(bits>>shift)&0xffu;
            hash*=0x100000001b3ull;
        }
    }
    return hash;
}

int check_tables() {
    const auto& t=od::port::math_trig_tables();
    if (fnv(t.acos)!=0x02ae16b6ae9db1b3ull || fnv(t.cos)!=0x3a41fd0f820220cfull ||
        fnv(t.sin)!=0x2492823f0cd17efaull) {
        std::cerr << "trig tables differ from the x87 replay of 0x45b040\n";
        return 1;
    }
    // Retail truncates and steps by float pi / 2048, so quarter turns miss
    // 32768: only cos[0] reaches it.
    if (t.cos[0]!=32768 || t.sin[0]!=0 || t.cos[1]!=32767 || t.sin[1]!=50 ||
        t.sin[512]!=23170 || t.cos[512]!=23170 || t.sin[1024]!=32767 ||
        t.cos[1024]!=0 || t.cos[2048]!=-32767 || t.sin[2048]!=0 ||
        t.sin[3072]!=-32767 || t.cos[3072]!=0) {
        std::cerr << "cos/sin spot values differ from retail\n";
        return 2;
    }
    if (t.acos[0]!=2048 || t.acos[1]!=2027 || t.acos[2048]!=1024 ||
        t.acos[4095]!=20) {
        std::cerr << "acos spot values differ from retail\n";
        return 3;
    }
    return 0;
}

int check_euler() {
    struct Case { int x,y,z; od::port::Mat3 expected; };
    const Case cases[]={
        {0,0,0,{32768,0,0,0,32768,0,0,0,32768}},
        {0,1024,0,{0,0,32767,0,32768,0,-32767,0,0}},
        {0,3000,0,{-3611,0,-32568,0,32768,0,32568,0,-3611}},
        {100,200,300,{28658,-12518,9779,14381,29014,-5006,-6748,8670,30869}},
        {4000,1234,2345,{11321,-470,30745,-14261,-29107,4808,27241,-15042,-10262}},
    };
    for (const auto& c : cases) {
        od::port::Mat3 matrix{};
        std::string error;
        if (!od::port::MATH_EulerToMat3(c.x,c.y,c.z,matrix,error) ||
            matrix!=c.expected) {
            std::cerr << "MATH_EulerToMat3(" << c.x << ',' << c.y << ',' << c.z
                      << ") differs from 0x45b194 table arithmetic\n";
            return 4;
        }
    }
    return 0;
}

int check_slerp() {
    using od::port::Quat4;
    const Quat4 identity{0,0,0,32768};
    const Quat4 z90{0,0,23170,23170};
    struct Case { Quat4 left,right; int32_t weight; Quat4 expected; };
    const Case cases[]={
        {identity,z90,0,identity},
        {identity,z90,64,{0,0,6391,32135}},
        {identity,z90,128,{0,0,12538,30271}},
        {identity,z90,200,{0,0,18867,26789}},
        {identity,z90,256,z90},
        // dot within 20 of 32768: linear weights (256-w)<<15/256, w<<15/256.
        {identity,{0,0,100,32767},100,{0,0,39,32767}},
        // dot within 20 of -32768: perpendicular branch, w keeps left z.
        {identity,{0,0,0,-32768},64,{0,0,32768,0}},
        {{1000,2000,3000,32000},{-1000,-2000,-3000,-32000},200,
         {-735,-1470,-2204,-23509}},
    };
    for (const auto& c : cases) {
        if (od::port::MATH_QuatSlerp(c.left,c.right,c.weight)!=c.expected) {
            std::cerr << "MATH_QuatSlerp weight " << c.weight
                      << " differs from 0x45bf68 table arithmetic\n";
            return 5;
        }
    }
    return 0;
}

uint32_t float_bits(float value) {
    uint32_t bits=0;
    std::memcpy(&bits,&value,sizeof(bits));
    return bits;
}

// Expected values below come from an independent Python model written from
// the disassembly of 0x459ec0, 0x459808, 0x45a03c, 0x459fdc and 0x459fbc.
int check_ease() {
    struct Case { float t,left_out,right_in; uint32_t expected; };
    const Case cases[]={
        {-0.1f,0.25f,0.25f,0x3cda740fu},  // unclamped: t < start uses t*t
        {1.2f,0.25f,0.25f,0x3f64b17du},   // unclamped tail past 1
        {0.3f,0.8f,0.6f,0x3e570a3fu},     // ease sum > 1 is renormalized
        {0.9f,0.8f,0.6f,0x3f7b851fu},
        {-0.5f,0.0f,0.0f,0xbf000000u},    // zero ease returns t unchanged
        {0.5f,0.25f,0.25f,0x3f000000u},
    };
    for (const auto& c : cases)
        if (float_bits(od::port::ANIM_ApplyEase(c.t,c.left_out,c.right_in))!=c.expected) {
            std::cerr << "ANIM_ApplyEase(" << c.t << ") differs from 0x459ec0\n";
            return 6;
        }
    return 0;
}

using od::port::AnimationTrack;
using od::port::Mat3;
using od::port::Vec3;

od::port::RotationKey rotation_key(int32_t frame, od::port::Quat4 q,
                                   float ease_out=0, float ease_in=0,
                                   od::port::Quat4 out_control={},
                                   od::port::Quat4 in_control={}) {
    od::port::RotationKey key;
    key.frame=frame; key.quaternion=q; key.ease_out=ease_out; key.ease_in=ease_in;
    key.out_control=out_control; key.in_control=in_control;
    return key;
}

od::port::TranslationKey translation_key(int32_t frame, Vec3 position,
                                         float ease_out=0, float ease_in=0,
                                         Vec3 in_tangent={}, Vec3 out_tangent={}) {
    od::port::TranslationKey key;
    key.frame=frame; key.position=position; key.ease_out=ease_out; key.ease_in=ease_in;
    key.in_tangent=in_tangent; key.out_tangent=out_tangent;
    return key;
}

bool eval(const AnimationTrack& track, float frame, unsigned flags, bool spline,
          Mat3& rotation, Vec3& position, bool& has_rotation, bool& has_position) {
    std::string error;
    return spline ?
        od::port::ANIM_EvalTrackSpline(track,frame,flags,rotation,position,
                                       has_rotation,has_position,error) :
        od::port::ANIM_EvalTrackLinear(track,frame,flags,rotation,position,
                                       has_rotation,has_position,error);
}

int check_linear_track() {
    const od::port::Quat4 identity{0,0,0,32768},z90{0,0,23170,23170},x90{23170,0,0,23170};
    AnimationTrack track;
    track.rotations={rotation_key(10,identity),rotation_key(20,z90),rotation_key(40,x90)};
    track.translations={translation_key(10,{100,-50,7}),translation_key(30,{300,50,-9})};
    struct RotationCase { float frame; Mat3 expected; };
    const RotationCase rotations[]={
        {5.0f,{23172,23169,0,-23170,23172,0,0,0,32768}},  // weight -128
        {15.0f,{23174,-23166,0,23165,23174,0,0,0,32768}},
        {20.0f,{2,-32767,0,32766,2,0,0,0,32768}},         // exact key
        {30.5f,{22789,-20849,10867,20848,10954,-22704,10867,22703,20933}},
        {50.0f,{32768,0,0,0,2,-32767,0,32766,2}},         // after last key
    };
    for (const auto& c : rotations) {
        Mat3 rotation{}; Vec3 position{}; bool has_rotation=false,has_position=false;
        if (!eval(track,c.frame,0,false,rotation,position,has_rotation,has_position) ||
            !has_rotation || rotation!=c.expected) {
            std::cerr << "linear rotation at " << c.frame << " differs from 0x459808\n";
            return 7;
        }
    }
    struct PositionCase { float frame; Vec3 expected; };
    const PositionCase positions[]={
        {0.0f,{0,-100,15}},      // before the first key: weight -128
        {17.25f,{171,-15,1}},    // weight truncates 92.8 to 92
        {30.0f,{300,50,-9}},
    };
    for (const auto& c : positions) {
        Mat3 rotation{}; Vec3 position{}; bool has_rotation=false,has_position=false;
        if (!eval(track,c.frame,0,false,rotation,position,has_rotation,has_position) ||
            !has_position || position!=c.expected) {
            std::cerr << "linear position at " << c.frame << " differs from 0x459808\n";
            return 8;
        }
    }
    // Equal key frames ahead of the sample: -inf stores 0x80000000 as the
    // weight and the wrapped products collapse to the left key.
    AnimationTrack duplicate;
    duplicate.translations={translation_key(10,{100,-50,7}),translation_key(10,{300,50,-9})};
    Mat3 rotation{}; Vec3 position{}; bool has_rotation=true,has_position=false;
    if (!eval(duplicate,5.0f,0,false,rotation,position,has_rotation,has_position) ||
        has_rotation || !has_position || position!=Vec3{100,-50,7}) {
        std::cerr << "linear zero-span weight differs from 0x459808\n";
        return 9;
    }
    // Skip flags and single-key tracks leave both channels untouched.
    AnimationTrack single;
    single.rotations={rotation_key(0,z90)};
    single.translations={translation_key(0,{1,2,3})};
    for (const bool spline : {false,true}) {
        if (!eval(single,0.0f,0,spline,rotation,position,has_rotation,has_position) ||
            has_rotation || has_position ||
            !eval(track,15.0f,1,spline,rotation,position,has_rotation,has_position) ||
            has_rotation || !has_position ||
            !eval(track,15.0f,2,spline,rotation,position,has_rotation,has_position) ||
            !has_rotation || has_position) {
            std::cerr << "skip flags or single-key tracks differ from retail\n";
            return 10;
        }
    }
    return 0;
}

int check_spline_track() {
    const od::port::Quat4 identity{0,0,0,32768},z90{0,0,23170,23170},x90{23170,0,0,23170};
    AnimationTrack track;
    // The right control quaternions are zero; retail still runs SQUAD.
    track.rotations={rotation_key(0,identity,0.25f,0.0f,{0,0,11585,30274}),
                     rotation_key(16,z90,0.0f,0.5f),rotation_key(32,x90)};
    track.translations={
        translation_key(0,{0,100,-20},0.3f,0.0f,{0,0,0},{40,-30,5}),
        translation_key(20,{200,150,60},0.0f,0.4f,{-10,25,3},{0,0,0})};
    struct Case { float frame; Mat3 rotation; Vec3 position; };
    const Case cases[]={
        {-4.0f,{31745,-8083,0,8082,31745,0,0,0,32768},{2,99,-19}},
        {5.0f,{30600,-11639,0,11638,30600,0,0,0,32768},{2,99,-19}},
        {12.5f,{12371,-27611,0,27610,12371,0,0,0,32768},{89,113,13}},
        {24.0f,{27323,-10894,5445,10893,21877,-10894,5445,10893,27323},{199,149,59}},
    };
    // Positions are sampled at -3, 3, 9.75 and 18 so both channels are
    // covered at distinct weights.
    const float position_frames[]={-3.0f,3.0f,9.75f,18.0f};
    for (size_t i=0; i<std::size(cases); ++i) {
        Mat3 rotation{}; Vec3 position{}; bool has_rotation=false,has_position=false;
        if (!eval(track,cases[i].frame,2,true,rotation,position,has_rotation,has_position) ||
            !has_rotation || rotation!=cases[i].rotation) {
            std::cerr << "spline rotation at " << cases[i].frame
                      << " differs from 0x45a03c\n";
            return 11;
        }
        if (!eval(track,position_frames[i],1,true,rotation,position,has_rotation,
                  has_position) || !has_position || position!=cases[i].position) {
            std::cerr << "spline position at " << position_frames[i]
                      << " differs from 0x45a03c\n";
            return 12;
        }
    }
    return 0;
}

struct Section { uint32_t va,size,raw,raw_size; };

bool read_va(const std::vector<uint8_t>& file, uint32_t va, size_t count,
             std::vector<uint8_t>& out) {
    const auto u16=[&](size_t at) { return static_cast<uint32_t>(file[at]|(file[at+1]<<8)); };
    const auto u32=[&](size_t at) { return u16(at)|(u16(at+2)<<16); };
    if (file.size()<0x40) return false;
    const size_t pe=u32(0x3c);
    if (pe+24>file.size()) return false;
    const uint32_t sections=u16(pe+6), optional=u16(pe+20);
    const uint32_t base=u32(pe+24+28);
    for (uint32_t i=0; i<sections; ++i) {
        const size_t at=pe+24+optional+i*40u;
        if (at+40>file.size()) return false;
        const Section s{base+u32(at+12),u32(at+8),u32(at+20),u32(at+16)};
        if (va<s.va || va-s.va>=s.raw_size) continue;
        const size_t offset=s.raw+(va-s.va);
        if (va-s.va+count>s.raw_size || offset+count>file.size()) return false;
        out.assign(file.begin()+static_cast<std::ptrdiff_t>(offset),
                   file.begin()+static_cast<std::ptrdiff_t>(offset+count));
        return true;
    }
    return false;
}

std::vector<uint8_t> hex(const char* text) {
    std::vector<uint8_t> bytes;
    for (size_t i=0; text[i] && text[i+1]; i+=2)
        bytes.push_back(static_cast<uint8_t>(std::stoul(std::string(text+i,2),nullptr,16)));
    return bytes;
}

// Confirms that the executable holds the generator code and constants
// modelled by math_trig_tables(); the tables themselves live in .bss.
int check_executable(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);
    if (!stream) { std::cerr << "cannot open " << path.string() << '\n'; return 20; }
    const std::vector<uint8_t> file{std::istreambuf_iterator<char>(stream),
                                    std::istreambuf_iterator<char>()};
    struct Expect { uint32_t va; const char* bytes; };
    const Expect expects[]={
        {0x4c6074,"000000000000e040"},             // 32768.0
        {0x4c6094,"02000060fb21593f"},             // angle step
        {0x4c608c,"2dc9c96d305fd43f"},             // acos scale (~1/pi)
        {0x4c6084,"000000000000a040"},             // 2048.0
        {0x4c607c,"0000003a"},                     // 1/2048f
        {0x4c60fc,"00000038"},                     // 1/32768f (slerp index)
        {0x4c6100,"00000045"},                     // 2048f (slerp index)
        {0x4ac8d8,"00c06821a2da0fc9ff3f"},         // acos pi/2 (double precision)
        {0x45b040,
         "515283ec1831d289542410dd0574604c00d9442410dd0594604c00d9c1d9c0d9fbd8cd83"
         "c204e8d3e3ffffdb5c24148b442414d8cc8982ec5f6600e8bee3ffffdb5c24148b4424"
         "14d8c18982ec9f6600ddda81fa0040000075c1ddd8ba00f8ffffb900e0ffffd95c2410"
         "ddd8dd058c604c00dd0584604c00d9057c604c0089542414db442414d8c9e885380200"
         "d8cad8cb83c104e865e3ffffdb5c24148b442414428981ec3f660081fa000800007cce"
         "ddd8ddd8ddd883c4185a59c3"},
        {0x47e952,
         "6650d9c0d8c8d9e8dee1d9e4dfe09e750fded9dfe09e7704d9eeeb02d9ebeb1ab001e8"
         "c1d8fdff3c00750fd9c9e8831efeffdb2dd8c84a00dee16658c3"},
        {0x45bfc8,
         "89542414db442414d80dfc604c00d90500614c00d9c9d8c9dec1e857d4ffffdb5c2414"
         "8b7c24148b5424048b3cbdf01f66000fafd78b04bdf09f6600"},
    };
    for (const auto& e : expects) {
        const auto wanted=hex(e.bytes);
        std::vector<uint8_t> actual;
        if (!read_va(file,e.va,wanted.size(),actual) || actual!=wanted) {
            std::cerr << path.filename().string() << ": bytes at 0x" << std::hex
                      << e.va << std::dec << " differ from the modelled generator\n";
            return 21;
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc>1 && std::string(argv[1])=="--retail-exe") {
        const char* disc=std::getenv("DREAMS_DISC1");
        if (!disc || !*disc) {
            std::cout << "DREAMS_DISC1 is not set; skipping executable check\n";
            return 77;
        }
        for (const char* name : {"WINDREAM.EXE","GDIDREAM.EXE"})
            if (const int result=check_executable(std::filesystem::u8path(disc)/name))
                return result;
        std::cout << "WINDREAM.EXE and GDIDREAM.EXE hold the modelled trig generator\n";
        return 0;
    }
    if (const int result=check_tables()) return result;
    if (const int result=check_euler()) return result;
    if (const int result=check_slerp()) return result;
    if (const int result=check_ease()) return result;
    if (const int result=check_linear_track()) return result;
    if (const int result=check_spline_track()) return result;
    std::cout << "retail trig tables, Euler matrices, slerp, ease and track "
                 "evaluation match\n";
    return 0;
}
