#include "port/animation.h"

#include "port/scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace od::port {
namespace {

bool fail(std::string& error, const char* message) {
    error=message;
    return false;
}

bool range(const std::vector<uint8_t>& bytes, size_t at, size_t count) {
    return at<=bytes.size() && count<=bytes.size()-at;
}

uint32_t u32(const std::vector<uint8_t>& bytes, size_t at) {
    return static_cast<uint32_t>(bytes[at]) |
        (static_cast<uint32_t>(bytes[at+1])<<8) |
        (static_cast<uint32_t>(bytes[at+2])<<16) |
        (static_cast<uint32_t>(bytes[at+3])<<24);
}

int32_t i32(const std::vector<uint8_t>& bytes, size_t at) {
    const uint32_t bits=u32(bytes,at);
    int32_t result=0;
    std::memcpy(&result,&bits,sizeof(result));
    return result;
}

float f32(const std::vector<uint8_t>& bytes, size_t at) {
    const uint32_t bits=u32(bytes,at);
    float result=0;
    std::memcpy(&result,&bits,sizeof(result));
    return result;
}

int32_t sar14(int64_t value) {
    const uint32_t bits=static_cast<uint32_t>(value);
    int32_t signed_value=0;
    std::memcpy(&signed_value,&bits,sizeof(bits));
    return signed_value>>14;
}

bool nonzero(const Quat4& quaternion) {
    return std::any_of(quaternion.begin(),quaternion.end(),
                       [](int32_t value) { return value!=0; });
}

bool parse_tracks(const std::vector<uint8_t>& bytes, AnimationClip& clip,
                  unsigned rotation_stride, unsigned translation_stride,
                  std::string& error) {
    error.clear();
    clip.tracks.clear();
    clip.duration=0;
    if (!range(bytes,0,0x18)) return fail(error,"animation header is truncated");
    const uint32_t count=u32(bytes,0x14);
    if (!count || count>1024 || !range(bytes,0x18,static_cast<size_t>(count)*4u))
        return fail(error,"animation track directory is invalid");
    clip.tracks.reserve(count);
    for (uint32_t slot=0; slot<count; ++slot) {
        const size_t at=u32(bytes,0x18+slot*4u);
        if (!range(bytes,at,0x28))
            return fail(error,"animation track header is outside its payload");
        AnimationTrack track;
        track.duration=u32(bytes,at+0x14);
        track.rotation_stride=rotation_stride;
        const uint32_t rotation_count=u32(bytes,at+0x18);
        const uint32_t translation_count=u32(bytes,at+0x1c);
        const uint32_t rotation_ptr=u32(bytes,at+0x20);
        const uint32_t translation_ptr=u32(bytes,at+0x24);
        if (track.duration>1000000 || rotation_count>100000 ||
            translation_count>100000 ||
            rotation_ptr>UINT32_MAX-20u || translation_ptr>UINT32_MAX-20u ||
            rotation_ptr+20u!=at+0x28u ||
            static_cast<uint64_t>(rotation_ptr)+
                static_cast<uint64_t>(rotation_count)*rotation_stride!=translation_ptr)
            return fail(error,"animation key pointers or counts are invalid");
        const size_t rotation_at=static_cast<size_t>(rotation_ptr)+20u;
        const size_t translation_at=static_cast<size_t>(translation_ptr)+20u;
        if (!range(bytes,rotation_at,static_cast<size_t>(rotation_count)*rotation_stride) ||
            !range(bytes,translation_at,static_cast<size_t>(translation_count)*translation_stride))
            return fail(error,"animation key array exceeds its payload");
        track.rotations.reserve(rotation_count);
        for (uint32_t key_index=0; key_index<rotation_count; ++key_index) {
            const size_t key_at=rotation_at+static_cast<size_t>(key_index)*rotation_stride;
            RotationKey key;
            key.frame=i32(bytes,key_at);
            for (size_t i=0; i<4; ++i)
                key.quaternion[i]=i32(bytes,key_at+4u+i*4u);
            if (rotation_stride==60) {
                key.ease_out=f32(bytes,key_at+20);
                key.ease_in=f32(bytes,key_at+24);
                if (!std::isfinite(key.ease_out) || !std::isfinite(key.ease_in))
                    return fail(error,"animation rotation ease is not finite");
                for (size_t i=0; i<4; ++i) {
                    key.out_control[i]=i32(bytes,key_at+28u+i*4u);
                    key.in_control[i]=i32(bytes,key_at+44u+i*4u);
                }
                key.has_out_control=nonzero(key.out_control);
                key.has_in_control=nonzero(key.in_control);
            }
            if (key.frame<0 ||
                (!track.rotations.empty() && track.rotations.back().frame>key.frame))
                return fail(error,"animation rotation timestamps are not monotonic");
            track.rotations.push_back(key);
        }
        track.translations.reserve(translation_count);
        for (uint32_t key_index=0; key_index<translation_count; ++key_index) {
            const size_t key_at=translation_at+
                static_cast<size_t>(key_index)*translation_stride;
            TranslationKey key;
            key.frame=i32(bytes,key_at);
            for (size_t i=0; i<3; ++i)
                key.position[i]=i32(bytes,key_at+4u+i*4u);
            if (translation_stride==48) {
                key.ease_out=f32(bytes,key_at+16);
                key.ease_in=f32(bytes,key_at+20);
                if (!std::isfinite(key.ease_out) || !std::isfinite(key.ease_in))
                    return fail(error,"animation translation ease is not finite");
                for (size_t i=0; i<3; ++i) {
                    key.in_tangent[i]=i32(bytes,key_at+24u+i*4u);
                    key.out_tangent[i]=i32(bytes,key_at+36u+i*4u);
                }
            }
            if (!track.translations.empty() && track.translations.back().frame>key.frame)
                return fail(error,"animation translation timestamps are not monotonic");
            track.translations.push_back(key);
        }
        clip.duration=std::max(clip.duration,track.duration);
        clip.tracks.push_back(std::move(track));
    }
    return true;
}

// Retail integer arithmetic wraps at 32 bits. Products are formed in
// unsigned 64-bit (well defined on overflow) and reduced modulo 2^32.
uint64_t bits64(int64_t value) { return static_cast<uint64_t>(value); }

int32_t wrap32(uint64_t value) {
    const uint32_t bits=static_cast<uint32_t>(value);
    int32_t wrapped=0;
    std::memcpy(&wrapped,&bits,sizeof(bits));
    return wrapped;
}

// Retail's integer division by 256 (SAR/SHL/SBB/SAR): truncates toward zero.
int32_t div256(uint64_t value) {
    return wrap32(value)/256;
}

// __CHP then FISTP: truncate toward zero; NaN, infinities and values outside
// int32 store the x87 integer indefinite 0x80000000.
int32_t x87_trunc(double value) {
    if (!(value>-2147483649.0 && value<2147483648.0)) return INT32_MIN;
    return static_cast<int32_t>(value);
}

// Binary search shared by both evaluators: lo=0, hi=count-1, halve while
// lo+1<hi, moving lo when key.frame < frame. Returns hi.
template<typename Key>
size_t retail_key(const std::vector<Key>& keys, float frame) {
    int32_t lo=0,hi=static_cast<int32_t>(keys.size())-1;
    while (lo+1<hi) {
        const int32_t middle=(lo+hi)/2;
        if (static_cast<double>(keys[static_cast<size_t>(middle)].frame)<frame) lo=middle;
        else hi=middle;
    }
    return static_cast<size_t>(hi);
}

// FILD/FSUBR operands: the float-minus-int difference is exact in double for
// any frame the host accepts; the span is retail's wrapped 32-bit SUB.
double key_offset(float frame, int32_t left) {
    return static_cast<double>(frame)-left;
}
double key_span(int32_t right, int32_t left) {
    return wrap32(bits64(right)-bits64(left));
}

bool rotation_at(const AnimationTrack& track, float frame, bool spline,
                 Quat4& result) {
    const auto& keys=track.rotations;
    // Tracks with fewer than two keys leave the node rotation untouched.
    if (keys.size()<=1) return false;
    const size_t right=retail_key(keys,frame);
    const auto& b=keys[right];
    if (!(static_cast<double>(b.frame)>frame)) {
        result=b.quaternion;
        return true;
    }
    // right >= 1 here. A frame before the first key interpolates keys 0/1
    // with a negative weight, as retail does.
    const auto& a=keys[right-1];
    const double offset=key_offset(frame,a.frame);
    const double span=key_span(b.frame,a.frame);
    if (!spline) {
        // Linear: trunc((frame-a)*256/span). A non-integral quotient lies at
        // least 1/span from an integer, so double matches x87 here.
        result=MATH_QuatSlerp(a.quaternion,b.quaternion,
                              x87_trunc(offset*256.0/span));
        return true;
    }
    // Spline: t is stored as float, eased (float return), then ease*256 is
    // truncated. SQUAD always runs, including zero control quaternions.
    const float t=ANIM_ApplyEase(static_cast<float>(offset/span),a.ease_out,b.ease_in);
    const int32_t weight=x87_trunc(static_cast<double>(t)*256.0);
    const Quat4 base=MATH_QuatSlerp(a.quaternion,b.quaternion,weight);
    const Quat4 control=MATH_QuatSlerp(a.out_control,b.in_control,weight);
    const uint64_t twice_rest=(bits64(256)-bits64(weight))*2u;
    result=MATH_QuatSlerp(base,control,div256(bits64(weight)*twice_rest));
    return true;
}

// MATH_MulMat4Vec4f (0x459fdc) with the 0x4aa710 Hermite basis, then
// MATH_DotVec4f (0x459fbc). Every float store rounds to float; x87
// intermediates are modelled in double.
int32_t hermite_axis(float u, float u2, float u3, int32_t left, int32_t right,
                     int32_t left_out, int32_t right_in) {
    static constexpr float basis[16]={
        2.0f,-2.0f,1.0f,1.0f, -3.0f,3.0f,-2.0f,-1.0f,
        0.0f,0.0f,1.0f,0.0f, 1.0f,0.0f,0.0f,0.0f};
    const float control[4]={static_cast<float>(left),static_cast<float>(right),
                            static_cast<float>(left_out),static_cast<float>(right_in)};
    float coefficient[4]{};
    for (size_t row=0; row<4; ++row) {
        float sum=0.0f;
        for (size_t column=0; column<4; ++column)
            sum=static_cast<float>(static_cast<double>(basis[row*4+column])*
                                   control[column]+sum);
        coefficient[row]=sum;
    }
    const float power[4]={u3,u2,u,1.0f};
    double dot=static_cast<double>(power[1])*coefficient[1]+
               static_cast<double>(power[0])*coefficient[0];
    dot+=static_cast<double>(power[2])*coefficient[2];
    dot+=static_cast<double>(power[3])*coefficient[3];
    return x87_trunc(dot);
}

bool position_at(const AnimationTrack& track, float frame, bool spline,
                 Vec3& result) {
    const auto& keys=track.translations;
    // Tracks with fewer than two keys leave the node position untouched.
    if (keys.size()<=1) return false;
    const size_t right=retail_key(keys,frame);
    const auto& b=keys[right];
    if (!(static_cast<double>(b.frame)>frame)) {
        result=b.position;
        return true;
    }
    const auto& a=keys[right-1];
    const double offset=key_offset(frame,a.frame);
    const double span=key_span(b.frame,a.frame);
    if (!spline) {
        // (a*(256-w) + w*b) >> 8 with 32-bit IMUL/ADD/SAR.
        const int32_t weight=x87_trunc(offset*256.0/span);
        const uint64_t rest=bits64(256)-bits64(weight);
        for (size_t i=0; i<3; ++i)
            result[i]=wrap32(bits64(a.position[i])*rest+
                             bits64(weight)*bits64(b.position[i]))>>8;
        return true;
    }
    const float u=ANIM_ApplyEase(static_cast<float>(offset/span),a.ease_out,b.ease_in);
    const double square=static_cast<double>(u)*u; // exact: 48 significant bits
    const float u2=static_cast<float>(square);
    const float u3=static_cast<float>(square*u);
    for (size_t i=0; i<3; ++i)
        result[i]=hermite_axis(u,u2,u3,a.position[i],b.position[i],
                               a.out_tangent[i],b.in_tangent[i]);
    return true;
}

bool eval_track(const AnimationTrack& track, float frame, unsigned flags,
                bool spline, Mat3& rotation, Vec3& position, bool& has_rotation,
                bool& has_position, std::string& error) {
    error.clear();
    has_rotation=false;
    has_position=false;
    if (!std::isfinite(frame)) return fail(error,"animation frame is not finite");
    Quat4 quaternion{};
    if (!(flags&1u) && rotation_at(track,frame,spline,quaternion)) {
        MATH_QuatToMatrix(quaternion,rotation);
        has_rotation=true;
    }
    if (!(flags&2u)) has_position=position_at(track,frame,spline,position);
    return true;
}

bool apply_model(const AnimationClip& clip, float frame,
                 const ModelGraph& bind, ModelGraph& pose,
                 bool follow_root_motion, bool spline, std::string& error) {
    error.clear();
    // Retail ANIM_ApplyModelLinear/Spline takes the loop count from the model
    // directory (+0x14), then walks that many clip tracks. F03/ITO carry
    // additional trailing tracks; those do not address another model node.
    if (clip.tracks.size()<bind.nodes.size() ||
        pose.nodes.size()!=bind.nodes.size())
        return fail(error,"animation has fewer tracks than model node slots");
    if (!std::isfinite(frame) || frame<0 || frame>clip.duration)
        return fail(error,"animation frame is outside the clip duration");
    for (size_t slot=0; slot<bind.nodes.size(); ++slot) {
        pose.nodes[slot].local_rot=bind.nodes[slot].local_rot;
        pose.nodes[slot].local_xyz=bind.nodes[slot].local_xyz;
        Mat3 rotation{};
        Vec3 position{};
        bool has_rotation=false,has_position=false;
        if (!eval_track(clip.tracks[slot],frame,0,spline,rotation,position,
                        has_rotation,has_position,error)) return false;
        if (has_rotation && !MDL_SetNodeRotation(pose,slot,rotation,error)) return false;
        if (has_position) {
            if (slot==0) {
                const auto& first=clip.tracks[slot].translations.front().position;
                for (size_t axis=0; axis<3; ++axis) {
                    const int64_t displacement=static_cast<int64_t>(position[axis])-first[axis];
                    const int64_t value=static_cast<int64_t>(bind.nodes[slot].local_xyz[axis])+
                        ((!follow_root_motion && axis!=1) ? 0 : displacement);
                    position[axis]=static_cast<int32_t>(std::clamp(value,
                        static_cast<int64_t>(INT32_MIN),static_cast<int64_t>(INT32_MAX)));
                }
            }
            if (!MDL_SetNodePosition(pose,slot,position,error)) return false;
        }
    }
    return true;
}

} // namespace

bool ANIM_RelocLinearTracks(const std::vector<uint8_t>& bytes,
                            AnimationClip& clip, std::string& error) {
    return parse_tracks(bytes,clip,20,16,error);
}

bool ANIM_RelocSplineTracks(const std::vector<uint8_t>& bytes,
                            AnimationClip& clip, std::string& error) {
    return parse_tracks(bytes,clip,60,48,error);
}

bool ANIM_DecodeClip(const std::vector<uint8_t>& bytes, std::string_view name,
                     AnimationClip& clip, std::string& error) {
    clip={};
    error.clear();
    if (!range(bytes,0,0x18)) return fail(error,"animation resource header is truncated");
    clip.name=std::string(name);
    clip.resource_type=u32(bytes,8);
    if (clip.resource_type==4) return ANIM_RelocLinearTracks(bytes,clip,error);
    if (clip.resource_type==6) return ANIM_RelocSplineTracks(bytes,clip,error);
    return fail(error,"animation resource has no supported track layout");
}

void MATH_QuatToMatrix(const Quat4& q, Mat3& m) {
    const int64_t x=q[0], y=q[1], z=q[2], w=q[3];
    m={
        32768-sar14(y*y+z*z), sar14(x*y-z*w), sar14(x*z+y*w),
        sar14(x*y+z*w), 32768-sar14(x*x+z*z), sar14(y*z-x*w),
        sar14(x*z-y*w), sar14(y*z+x*w), 32768-sar14(x*x+y*y)
    };
}

Quat4 MATH_QuatSlerp(const Quat4& left, const Quat4& right, int32_t weight) {
    // WINDREAM 0x45bf68: raw Q15 inputs (no normalization or sign flip),
    // 32-bit wrapped products, SAR 15 and the fixed acos/sin tables.
    const auto& tables=math_trig_tables();
    // Weights in 0..256 keep every sine index in 0..2048. Retail indexes the
    // table unchecked for other weights; the host masks to 12 bits instead
    // of reading neighbouring retail memory.
    const auto sine=[&tables](int32_t index) {
        return static_cast<int64_t>(tables.sin[static_cast<size_t>(index&4095)]);
    };
    uint64_t dot_sum=0;
    for (size_t i=0; i<4; ++i) dot_sum+=bits64(left[i])*bits64(right[i]);
    const int32_t dot=wrap32(dot_sum)>>15;
    Quat4 out{};
    if (static_cast<int64_t>(dot)+0x8000<=20) {
        // Nearly opposite: blend toward the perpendicular (-y,x,-w,z). Retail
        // only rewrites the first three components; w keeps left z.
        out={wrap32(0u-bits64(left[1])),left[0],wrap32(0u-bits64(left[3])),left[2]};
        int32_t left_angle=div256((bits64(128)-bits64(weight))*1024u);
        if (left_angle<0) left_angle=-left_angle;
        const uint64_t left_weight=bits64(wrap32(bits64(sine(left_angle))*32768u));
        const uint64_t right_weight=bits64(wrap32(
            bits64(sine(div256(bits64(weight)*1024u)))*32768u));
        for (size_t i=0; i<3; ++i)
            out[i]=wrap32(bits64(left[i])*left_weight+bits64(out[i])*right_weight)>>15;
        return out;
    }
    int32_t left_weight=0,right_weight=0;
    if (0x8000-static_cast<int64_t>(dot)<=20) {
        left_weight=div256((bits64(256)-bits64(weight))*32768u);
        right_weight=div256(bits64(weight)*32768u);
    } else {
        // trunc(2048 + dot/16) under __CHP; the sum is positive here and the
        // index is 1..4094, whose acos entries (1..2027) have nonzero sines.
        const int32_t angle=tables.acos[static_cast<size_t>((dot+0x8000)>>4)];
        const int32_t sine_angle=static_cast<int32_t>(sine(angle));
        left_weight=wrap32(bits64(sine(div256((bits64(256)-bits64(weight))*
                                              bits64(angle))))*32768u)/sine_angle;
        right_weight=wrap32(bits64(sine(div256(bits64(weight)*bits64(angle))))*
                            32768u)/sine_angle;
    }
    for (size_t i=0; i<4; ++i)
        out[i]=wrap32(bits64(right[i])*bits64(right_weight)+
                      bits64(left[i])*bits64(left_weight))>>15;
    return out;
}

float ANIM_ApplyEase(float t, float left_out, float right_in) {
    // WINDREAM 0x459ec0, unclamped. x87 intermediates are modelled in double;
    // every float load and store of the original is a float here.
    float start=right_in,end=left_out;
    const double total=static_cast<double>(start)+end;   // FADD, FSTP double
    const float total_float=static_cast<float>(total);   // FST float
    if (total==0.0) return t;
    if (1.0<total) {
        const double inverse=1.0/total_float;
        start=static_cast<float>(start*inverse);
        end=static_cast<float>(inverse*end);
    }
    const float scale=static_cast<float>(1.0/((2.0-start)-static_cast<double>(end)));
    if (t<start)
        return static_cast<float>(static_cast<double>(scale)/start*t*t);
    const double rest=1.0-static_cast<double>(t);
    if (1.0-static_cast<double>(end)<=t)
        return static_cast<float>(1.0-static_cast<double>(scale)/end*rest*rest);
    return static_cast<float>((2.0*t-start)*scale);
}

bool ANIM_EvalTrackLinear(const AnimationTrack& track, float frame,
                          unsigned flags, Mat3& rotation, Vec3& position,
                          bool& has_rotation, bool& has_position,
                          std::string& error) {
    return eval_track(track,frame,flags,false,rotation,position,has_rotation,
                      has_position,error);
}

bool ANIM_EvalTrackSpline(const AnimationTrack& track, float frame,
                          unsigned flags, Mat3& rotation, Vec3& position,
                          bool& has_rotation, bool& has_position,
                          std::string& error) {
    return eval_track(track,frame,flags,true,rotation,position,has_rotation,
                      has_position,error);
}

bool ANIM_ApplyModelLinear(const AnimationClip& clip, float frame,
                           const ModelGraph& bind, ModelGraph& pose,
                           bool follow_root_motion, std::string& error) {
    if (clip.resource_type!=4) return fail(error,"clip is not a linear animation");
    return apply_model(clip,frame,bind,pose,follow_root_motion,false,error);
}

bool ANIM_ApplyModelSpline(const AnimationClip& clip, float frame,
                           const ModelGraph& bind, ModelGraph& pose,
                           bool follow_root_motion, std::string& error) {
    if (clip.resource_type!=6) return fail(error,"clip is not a spline animation");
    return apply_model(clip,frame,bind,pose,follow_root_motion,true,error);
}

} // namespace od::port
