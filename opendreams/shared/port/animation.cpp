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

template<typename Key>
size_t right_key(const std::vector<Key>& keys, float frame) {
    const auto found=std::lower_bound(keys.begin()+1,keys.end(),frame,
        [](const Key& key,float time) { return key.frame<time; });
    return static_cast<size_t>(found-keys.begin());
}

unsigned weight256(float amount) {
    return static_cast<unsigned>(std::lround(std::clamp(amount,0.0f,1.0f)*256.0f));
}

Quat4 rotation_at(const AnimationTrack& track, float frame, bool spline) {
    const auto& keys=track.rotations;
    if (keys.size()==1 || frame<=keys.front().frame) return keys.front().quaternion;
    if (frame>=keys.back().frame) return keys.back().quaternion;
    const size_t right=right_key(keys,frame);
    const auto& a=keys[right-1];
    const auto& b=keys[right];
    if (b.frame<=a.frame) return b.quaternion;
    float t=(frame-a.frame)/static_cast<float>(b.frame-a.frame);
    if (!spline || !a.has_out_control || !b.has_in_control)
        return MATH_QuatSlerp(a.quaternion,b.quaternion,weight256(t),true);
    t=ANIM_ApplyEase(t,a.ease_out,b.ease_in);
    const Quat4 base=MATH_QuatSlerp(a.quaternion,b.quaternion,weight256(t),false);
    const Quat4 control=MATH_QuatSlerp(a.out_control,b.in_control,
                                       weight256(t),false);
    return MATH_QuatSlerp(base,control,weight256(2.0f*t*(1.0f-t)),false);
}

Vec3 position_at(const AnimationTrack& track, float frame, bool spline) {
    const auto& keys=track.translations;
    if (keys.size()==1 || frame<=keys.front().frame) return keys.front().position;
    if (frame>=keys.back().frame) return keys.back().position;
    const size_t right=right_key(keys,frame);
    const auto& a=keys[right-1];
    const auto& b=keys[right];
    if (b.frame<=a.frame) return b.position;
    float t=(frame-a.frame)/static_cast<float>(b.frame-a.frame);
    Vec3 result{};
    if (!spline) {
        const unsigned weight=weight256(t);
        for (size_t i=0; i<3; ++i)
            result[i]=static_cast<int32_t>(
                (static_cast<int64_t>(a.position[i])*(256u-weight)+
                 static_cast<int64_t>(b.position[i])*weight)>>8);
        return result;
    }
    t=ANIM_ApplyEase(t,a.ease_out,b.ease_in);
    const double u=t;
    const double h00=2*u*u*u-3*u*u+1;
    const double h01=-2*u*u*u+3*u*u;
    const double h10=u*u*u-2*u*u+u;
    const double h11=u*u*u-u*u;
    for (size_t i=0; i<3; ++i) {
        const double value=h00*a.position[i]+h01*b.position[i]+
            h10*a.out_tangent[i]+h11*b.in_tangent[i];
        result[i]=static_cast<int32_t>(std::clamp(std::llround(value),
            static_cast<long long>(INT32_MIN),static_cast<long long>(INT32_MAX)));
    }
    return result;
}

bool eval_track(const AnimationTrack& track, float frame, bool spline,
                Mat3& rotation, Vec3& position, bool& has_rotation,
                bool& has_position, std::string& error) {
    error.clear();
    if (!std::isfinite(frame)) return fail(error,"animation frame is not finite");
    has_rotation=!track.rotations.empty();
    has_position=!track.translations.empty();
    if (has_rotation)
        MATH_QuatToMatrix(rotation_at(track,frame,spline),rotation);
    if (has_position) position=position_at(track,frame,spline);
    return true;
}

bool apply_model(const AnimationClip& clip, float frame,
                 const ModelGraph& bind, ModelGraph& pose,
                 bool follow_root_motion, bool spline, std::string& error) {
    error.clear();
    if (clip.tracks.size()!=bind.nodes.size() ||
        pose.nodes.size()!=bind.nodes.size())
        return fail(error,"animation track count differs from the model node directory");
    if (!std::isfinite(frame) || frame<0 || frame>clip.duration)
        return fail(error,"animation frame is outside the clip duration");
    for (size_t slot=0; slot<clip.tracks.size(); ++slot) {
        pose.nodes[slot].local_rot=bind.nodes[slot].local_rot;
        pose.nodes[slot].local_xyz=bind.nodes[slot].local_xyz;
        Mat3 rotation{};
        Vec3 position{};
        bool has_rotation=false,has_position=false;
        if (!eval_track(clip.tracks[slot],frame,spline,rotation,position,
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

Quat4 MATH_QuatSlerp(const Quat4& left, const Quat4& right,
                     unsigned weight, bool shortest_path) {
    if (!weight) return left;
    if (weight>=256) return right;
    std::array<double,4> a{},b{},out{};
    double norm_a=0,norm_b=0,dot=0;
    for (size_t i=0; i<4; ++i) {
        a[i]=left[i]; b[i]=right[i];
        norm_a+=a[i]*a[i]; norm_b+=b[i]*b[i];
    }
    if (norm_a==0 || norm_b==0) return norm_a==0 ? right : left;
    norm_a=std::sqrt(norm_a); norm_b=std::sqrt(norm_b);
    for (size_t i=0; i<4; ++i) {
        a[i]/=norm_a; b[i]/=norm_b; dot+=a[i]*b[i];
    }
    if (shortest_path && dot<0) {
        dot=-dot;
        for (double& value : b) value=-value;
    }
    dot=std::clamp(dot,-1.0,1.0);
    const double t=static_cast<double>(weight)/256.0;
    double left_weight=1.0-t,right_weight=t;
    if (!shortest_path && dot<-0.9995) {
        const std::array<double,4> orthogonal{{-a[1],a[0],-a[3],a[2]}};
        for (size_t i=0; i<4; ++i)
            out[i]=a[i]*std::cos(3.14159265358979323846*t)+
                   orthogonal[i]*std::sin(3.14159265358979323846*t);
    } else if (std::abs(dot)<0.9995) {
        const double angle=std::acos(dot);
        const double sine=std::sin(angle);
        if (std::abs(sine)>1e-8) {
            left_weight=std::sin((1.0-t)*angle)/sine;
            right_weight=std::sin(t*angle)/sine;
        }
    }
    if (shortest_path || dot>=-0.9995)
        for (size_t i=0; i<4; ++i)
            out[i]=left_weight*a[i]+right_weight*b[i];
    double length=0;
    for (double value : out) length+=value*value;
    if (length==0) return left;
    length=std::sqrt(length);
    Quat4 result{};
    for (size_t i=0; i<4; ++i)
        result[i]=static_cast<int32_t>(std::clamp(std::llround(out[i]/length*32768.0),
            -32768ll,32768ll));
    return result;
}

float ANIM_ApplyEase(float t, float left_out, float right_in) {
    t=std::clamp(t,0.0f,1.0f);
    float start=right_in,end=left_out;
    const float total=start+end;
    if (total==0) return t;
    if (total>1) { start/=total; end/=total; }
    const float scale=1.0f/(2.0f-start-end);
    if (start>0 && t<start) return scale*t*t/start;
    if (end>0 && t>=1.0f-end)
        return 1.0f-scale*(1.0f-t)*(1.0f-t)/end;
    return (2.0f*t-start)*scale;
}

bool ANIM_EvalTrackLinear(const AnimationTrack& track, float frame,
                          Mat3& rotation, Vec3& position, bool& has_rotation,
                          bool& has_position, std::string& error) {
    return eval_track(track,frame,false,rotation,position,has_rotation,
                      has_position,error);
}

bool ANIM_EvalTrackSpline(const AnimationTrack& track, float frame,
                          Mat3& rotation, Vec3& position, bool& has_rotation,
                          bool& has_position, std::string& error) {
    return eval_track(track,frame,true,rotation,position,has_rotation,
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
