@vs direct_quad_vs
out vec2 uv;
void main() {
    uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
}
@end

@fs direct_draw_fs
layout(binding=0) uniform draw_params {
    vec4 draw_rect;
    vec4 canvas;
    vec4 dimensions; // physical xy, logical zw
    ivec4 operation; // kind, parameter, 555, GL render-texture flip
    ivec4 line_points; // inclusive logical endpoints for packed line stores
};
@image_sample_type previous_tex unfilterable_float
layout(binding=0) uniform texture2D previous_tex;
@image_sample_type source_tex unfilterable_float
layout(binding=1) uniform texture2D source_tex;
@image_sample_type packed_tex uint
layout(binding=2) uniform utexture2D packed_tex;
@image_sample_type lookup_tex uint
layout(binding=3) uniform utexture2D lookup_tex;
@sampler_type point_smp nonfiltering
layout(binding=0) uniform sampler point_smp;
in vec2 uv;
out vec4 frag_color;
uint pack_colour(vec4 c) {
    uvec3 b = uvec3(floor(clamp(c.rgb, 0.0, 1.0)*255.0+0.5));
    return operation.z != 0 ? (uint(floor(c.a*255.0+0.5))==1u?32768u:0u)|((b.r>>3)<<10)|((b.g>>3)<<5)|(b.b>>3)
                            : ((b.r>>3)<<11)|((b.g>>2)<<5)|(b.b>>3);
}
vec4 expand_colour(uint c) {
    uvec3 rgb;
    if(operation.z!=0) {
        rgb=uvec3((c>>10)&31u,(c>>5)&31u,c&31u);
        rgb=(rgb<<3)|(rgb>>2);
    } else {
        rgb=uvec3((c>>11)&31u,(c>>5)&63u,c&31u);
        rgb=(rgb<<uvec3(3,2,3))|(rgb>>uvec3(2,4,2));
    }
    // Retain the observable padding bit of raw RGB555 stores without
    // quantizing scene RGB. Final output always writes opaque alpha.
    float alpha=operation.z!=0&&(c&32768u)!=0u?1.0/255.0:1.0;
    return vec4(vec3(rgb)/255.0,alpha);
}
uint blend(uint d,uint s,uint a) {
    if(a==0u)return d; if(a>=63u)return s;
    uint weight=a>>1, rs=operation.z!=0?10u:11u, gs=operation.z!=0?5u:6u;
    uint r=(((d>>rs)&31u)*(31u-weight)+((s>>rs)&31u)*weight)>>5;
    uint g=(((d>>gs)&31u)*(31u-weight)+((s>>gs)&31u)*weight)>>5;
    uint b=((d&31u)*(31u-weight)+(s&31u)*weight)>>5;
    return (r<<rs)|(g<<gs)|b;
}
uint table_value(int index,uint a) {
    int i=index+3072;
    uint value=texelFetch(usampler2D(lookup_tex,point_smp),ivec2(i%256,i/256),0).r;
    // SPR_BlitSprite writes its coverage byte before SPR_BlendChannel reads
    // this same address through the out-of-range source-table row.
    return index==3664?(value&0xffffff00u)|a:value;
}
uint lookup_channel(uint d,uint s,uint a) {
    int w=int(a>>1);
    return (table_value((31-w)*32+int(d),a)+table_value(w*32+int(s),a))>>5;
}
uint lookup_blend(uint d,uint s,uint a) {
    uint rs=operation.z!=0?10u:11u,gs=operation.z!=0?5u:6u;
    return ((lookup_channel((d>>rs)&31u,(s>>rs)&31u,a)<<rs)+
            (lookup_channel((d>>gs)&31u,(s>>gs)&31u,a)<<gs)+
            lookup_channel(d&31u,s&31u,a))&65535u;
}
uint dim(uint s) {
    int p=operation.y;
    if(operation.z==0) {
        if(p==0)return s&0xf7deu;
        if(p==1)return ((s&0x1eu)>>1)|(((s&0x7c0u)>>6)<<5)|(((s&0xf000u)>>12)<<11);
        if(p==2)return ((s&0x1cu)>>1)|(((s&0x780u)>>6)<<5)|(((s&0xe000u)>>12)<<11);
        return ((s&0x1cu)>>2)|(((s&0x780u)>>7)<<5)|(((s&0xe000u)>>13)<<11);
    }
    if(p==0)return s&0x7bdeu;
    if(p==1)return ((s&0x1eu)>>1)|(((s&0x3c0u)>>6)<<5)|(((s&0x7800u)>>11)<<10);
    if(p==2)return ((s&0x1cu)>>1)|(((s&0x380u)>>6)<<5)|(((s&0x7000u)>>11)<<10);
    return ((s&0x1cu)>>2)|(((s&0x380u)>>7)<<5)|(((s&0x7000u)>>12)<<10);
}
void main() {
    vec2 t=vec2(uv.x,operation.w!=0?1.0-uv.y:uv.y);
    vec4 old=texture(sampler2D(previous_tex,point_smp),t);
    int kind=operation.x;
    if(kind==4) { frag_color=texture(sampler2D(source_tex,point_smp),t); return; }
    if(kind==5) { frag_color=expand_colour(dim(pack_colour(texture(sampler2D(source_tex,point_smp),t)))); return; }
    vec2 pos=(uv*dimensions.xy-canvas.xy)*dimensions.zw/canvas.zw;
    ivec2 local=ivec2(floor(pos-draw_rect.xy));
    bool inside=all(greaterThanEqual(pos,draw_rect.xy))&&all(lessThan(pos,draw_rect.xy+draw_rect.zw));
    if(!inside) { if(kind==11)discard;frag_color=kind==6?vec4(0,0,0,1):old; return; }
    if(kind==11) {
        ivec2 a=line_points.xy,b=line_points.zw,p=ivec2(floor(pos));
        ivec2 delta=abs(b-a);
        bool major_x=delta.x>=delta.y;
        if((major_x&&a.x>b.x)||(!major_x&&a.y>b.y)) { ivec2 swap=a;a=b;b=swap; }
        uint major=uint(major_x?delta.x:delta.y);
        uint minor=uint(major_x?delta.y:delta.x);
        uint step=uint(major_x?p.x-a.x:p.y-a.y);
        int offset=major==0u?0:int((step*minor+major/2u)/major);
        int expected=major_x?a.y+(b.y>=a.y?offset:-offset):a.x+(b.x>=a.x?offset:-offset);
        bool hit=(major_x?p.y:p.x)==expected&&all(greaterThanEqual(p,ivec2(0)))&&
                 all(lessThan(p,ivec2(dimensions.zw)));
        if(!hit)discard;
        frag_color=expand_colour(uint(operation.y));
        return;
    }
    uint d=pack_colour(old), s=d;
    if(kind==0) { frag_color=old; return; }
    if(kind==3) s=uint(operation.y);
    else if(kind==7) {
        uint rm=operation.z==0?0xf800u:0x7c00u, gm=operation.z==0?0x7e0u:0x3e0u;
        s=local.y==int(draw_rect.w)-1?0u:((d&31u)>>1)|(((d&rm)>>1)&rm)|(((d&gm)>>1)&gm);
    } else if(kind==8) s=(d&0xf7deu)>>1;
    else {
        uint packed=texelFetch(usampler2D(packed_tex,point_smp),local,0).r;
        s=packed&65535u;
        uint a=packed>>16;
        if(kind!=6) {
            if(a==0u) { frag_color=old; return; }
            if(kind!=9) {
                if(kind==2)s=((s&0xf7dfu)+(d&0xf7dfu))>>1;
                if(operation.y!=0)s=(s&0xf7deu)>>1;
                if(operation.z!=0)s=((s&0xffc0u)>>1)|(s&31u);
                if(kind!=2)s=kind==10&&a>=128u?lookup_blend(d,s,a):blend(d,s,a);
            }
        }
    }
    frag_color=expand_colour(s);
}
@end

@fs direct_output_fs
layout(binding=0) uniform output_params { vec4 output_mode; };
@image_sample_type output_tex unfilterable_float
layout(binding=0) uniform texture2D output_tex;
@sampler_type output_smp nonfiltering
layout(binding=0) uniform sampler output_smp;
in vec2 uv;
out vec4 frag_color;
void main() {
    vec2 t=vec2(uv.x,output_mode.y!=0.0?1.0-uv.y:uv.y);
    vec3 c=texture(sampler2D(output_tex,output_smp),t).rgb;
    frag_color=vec4(pow(max(c,vec3(0)),vec3(output_mode.x)),1);
}
@end

@vs direct_scene_vs
layout(binding=0) uniform scene_vs_params { mat4 view_projection; vec4 depth_mode; };
in vec3 position;
in vec2 texcoord;
in float brightness;
out vec2 uv;
out float view_w;
out float vertex_brightness;
void main() {
    gl_Position=view_projection*vec4(position,1);
    view_w=gl_Position.w*depth_mode.y;
    if(depth_mode.x!=0.0)gl_Position.z=2.0*gl_Position.z-gl_Position.w;
    uv=texcoord;
    vertex_brightness=brightness;
}
@end
@fs direct_scene_fs
layout(binding=1) uniform scene_fs_params {
    vec4 colour; vec4 material_mode; vec4 fog_colour; vec4 fog_table[16];
};
layout(binding=0) uniform texture2D material_tex;
layout(binding=0) uniform sampler material_smp;
in vec2 uv;
in float view_w;
in float vertex_brightness;
out vec4 frag_color;
float fog_entry(int i) {
    vec4 group=fog_table[i/4];int lane=i%4;
    return lane==0?group.x:lane==1?group.y:lane==2?group.z:group.w;
}
float fog_factor(float w) {
    // SST1 selects the table using reciprocal-W exponent/mantissa, not
    // linear interpolation in camera distance. Keep RGB itself floating point.
    int depth=0;
    if(w>=65536.0)depth=65535;
    else if(w>1.0) {
        uint bits=floatBitsToUint(w);
        int exponent=int((bits>>23)&255u)-127;
        float power=uintBitsToFloat(bits&0x7f800000u);
        depth=clamp(exponent*4096+8192-int(floor(8192.0*power/w)),0,65535);
    }
    int i=depth>>10;
    int base=int(fog_entry(i));
    int delta=((int(fog_entry(min(i+1,63)))-base)*4)&255;
    int value=base+((delta*((depth>>2)&255))>>10);
    return float(value+1)/256.0;
}
void main() {
    vec4 c=material_mode.x!=0.0?texture(sampler2D(material_tex,material_smp),uv):colour;
    if(material_mode.y!=0.0&&c.a<0.5)discard;
    c.rgb*=vertex_brightness;
    if(fog_colour.a!=0.0)c.rgb=mix(c.rgb,fog_colour.rgb,fog_factor(view_w));
    frag_color=vec4(c.rgb,material_mode.z);
}
@end
@program direct_draw direct_quad_vs direct_draw_fs
@program direct_output direct_quad_vs direct_output_fs
@program direct_scene direct_scene_vs direct_scene_fs

@fs direct_shadow_fs
layout(binding=0) uniform shadow_params { vec4 shadow_zero; vec4 shadow_one; vec4 shadow_layout; };
@image_sample_type shadow_tex unfilterable_float
layout(binding=0) uniform texture2D shadow_tex;
@sampler_type shadow_smp nonfiltering
layout(binding=0) uniform sampler shadow_smp;
in vec2 uv;
out vec4 frag_color;
void main() {
    ivec2 p=ivec2(floor(uv*vec2(128.0)))*ivec2(1,2);
    if(shadow_layout.x!=0.0)p.y=255-p.y;
    float index=texelFetch(sampler2D(shadow_tex,shadow_smp),p,0).r;
    frag_color=index>0.5/255.0?shadow_one:shadow_zero;
}
@end
@program direct_shadow direct_quad_vs direct_shadow_fs

@vs direct_mask_vs
layout(binding=0) uniform mask_params { vec4 mask_dimensions; };
in vec2 position;
in ivec4 edge0;
in ivec4 edge1;
in ivec4 edge2;
out vec2 pixel;
flat out ivec4 span0;
flat out ivec4 span1;
flat out ivec4 span2;
void main() {
    pixel=position;
    gl_Position=vec4(position.x*2.0/mask_dimensions.x-1.0,
                     1.0-position.y*2.0/mask_dimensions.y,0.0,1.0);
    span0=edge0;span1=edge1;span2=edge2;
}
@end
@fs direct_mask_fs
in vec2 pixel;
flat in ivec4 span0;
flat in ivec4 span1;
flat in ivec4 span2;
out vec4 frag_color;
void main() {
    int y=int(floor(pixel.y));
    int left=2147483647,right=-2147483647,count=0;
    for(int i=0;i<3;i++) {
        ivec4 e=i==0?span0:i==1?span1:span2;
        if(y>=e.x&&y<e.y) {
            int x=int(uint(e.z)+uint(e.w)*uint(y-e.x+1))>>12;
            left=min(left,x);right=max(right,x);count++;
        }
    }
    int x=int(floor(pixel.x));
    if(count!=2||x<left||x>=right)discard;
    frag_color=vec4(vec3(1.0/255.0),1.0);
}
@end
@program direct_mask direct_mask_vs direct_mask_fs
