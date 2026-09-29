// Isolated integer 2D compositor experiment; no production UI replacement.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d11.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#define SOKOL_IMPL
#define SOKOL_D3D11
#include "sokol_gfx.h"

using Clock = std::chrono::steady_clock;
static void check(bool b, const char* s) { if (!b) { std::fprintf(stderr,"%s\n",s); std::exit(2); } }
static double ms(Clock::time_point a) { return std::chrono::duration<double,std::milli>(Clock::now()-a).count(); }
static uint32_t word(FILE* f) { uint32_t v; check(fread(&v,4,1,f)==1,"short fixture"); return v; }
static void logfn(const char*,uint32_t level,uint32_t,const char* msg,uint32_t,const char*,void*) {
    if (level<=1) { std::fprintf(stderr,"sokol: %s\n",msg?msg:"error"); std::exit(2); }
}
struct Image { sg_image image{}; sg_view texture{},attachment{}; };
static Image image(int w,int h,bool render,const std::vector<uint32_t>& data={}) {
    sg_image_desc d{}; d.width=w; d.height=h; d.pixel_format=SG_PIXELFORMAT_R32UI;
    d.usage.color_attachment=render;
    if (!render) d.usage.dynamic_update=true;
    Image i; i.image=sg_make_image(&d);
    sg_view_desc v{}; v.texture.image=i.image; i.texture=sg_make_view(&v);
    if (render) { v={}; v.color_attachment.image=i.image; i.attachment=sg_make_view(&v); }
    check(sg_query_image_state(i.image)==SG_RESOURCESTATE_VALID,"integer image creation failed");
    if (!render) { sg_image_data upload{}; upload.mip_levels[0]={data.data(),data.size()*4}; sg_update_image(i.image,&upload); }
    return i;
}
static ID3D11Texture2D* native(const Image& i) {
    return (ID3D11Texture2D*)sg_d3d11_query_image_info(i.image).tex2d;
}
struct Target { Image sides[2]; unsigned current=0; };
struct Command { uint32_t kind,target,source,x,y,w,h,param; std::vector<uint32_t> data;
                 std::vector<uint16_t> expected; Image upload; };
struct Params { uint32_t kind,param,format,unused; int32_t rect[4]; };
static const char* fragment = R"(
Texture2D<uint> previous:register(t0);
Texture2D<uint> source_image:register(t1);
cbuffer Params:register(b0) { uint kind,param,format,unused; int4 rect; };
uint blend(uint d,uint s,uint a) {
    if(a==0)return d; if(a>=63)return s;
    uint weight=a>>1, rs=format!=0?10:11, gs=format!=0?5:6;
    uint r=(((d>>rs)&31)*(31-weight)+((s>>rs)&31)*weight)>>5;
    uint g=(((d>>gs)&31)*(31-weight)+((s>>gs)&31)*weight)>>5;
    uint b=((d&31)*(31-weight)+(s&31)*weight)>>5;
    return (r<<rs)|(g<<gs)|b;
}
uint dim(uint s) {
    if(format==0) {
        if(param==0)return s&0xf7de;
        if(param==1)return ((s&0x1e)>>1)|(((s&0x7c0)>>6)<<5)|(((s&0xf000)>>12)<<11);
        if(param==2)return ((s&0x1c)>>1)|(((s&0x780)>>6)<<5)|(((s&0xe000)>>12)<<11);
        return ((s&0x1c)>>2)|(((s&0x780)>>7)<<5)|(((s&0xe000)>>13)<<11);
    }
    if(param==0)return s&0x7bde;
    if(param==1)return ((s&0x1e)>>1)|(((s&0x3c0)>>6)<<5)|(((s&0x7800)>>11)<<10);
    if(param==2)return ((s&0x1c)>>1)|(((s&0x380)>>6)<<5)|(((s&0x7000)>>11)<<10);
    return ((s&0x1c)>>2)|(((s&0x380)>>7)<<5)|(((s&0x7000)>>12)<<10);
}
uint main(float4 p:SV_Position):SV_Target0 {
    int2 q=int2(p.xy), local=q-rect.xy;
    uint d=previous.Load(int3(q,0));
    if(kind==5)return dim(source_image.Load(int3(q,0)));
    bool inside=all(local>=0)&&all(local<rect.zw);
    if(!inside)return kind==6?0:d;
    if(kind==7) {
        if(local.y==rect.w-1)return 0;
        uint rm=format==0?0xf800:0x7c00,gm=format==0?0x7e0:0x3e0;
        return ((d&31)>>1)|(((d&rm)>>1)&rm)|(((d&gm)>>1)&gm);
    }
    if(kind==8)return (d&0xf7de)>>1;
    if(kind==0)return d;
    if(kind==3)return param;
    uint packed=source_image.Load(int3(local,0));
    if(kind==6)return packed;
    uint s=packed&65535,a=packed>>16;
    if(a==0)return d;
    if(kind==9)return s;
    if(kind==2)s=((s&0xf7df)+(d&0xf7df))>>1;
    if(param!=0)s=(s&0xf7de)>>1;
    if(format!=0)s=((s&0xffc0)>>1)|(s&31);
    return kind==2?s:blend(d,s,a);
}
)";

int main(int argc,char** argv) {
    check(argc==2,"usage: render_2d_gpu_smoke fixture.bin");
    FILE* f=fopen(argv[1],"rb"); check(f && word(f)==0x31443244,"bad fixture");
    uint32_t w=word(f),h=word(f),fmt=word(f),nt=word(f),nc=word(f);
    check(w<=1920 && h<=1080 && nt<=8 && nc<=256,"fixture limits");
    std::vector<std::vector<uint32_t>> initial(nt,std::vector<uint32_t>(w*h));
    for(auto& v:initial)check(fread(v.data(),4,v.size(),f)==v.size(),"initial short");
    std::vector<Command> commands(nc);
    for(auto& c:commands) {
        c.kind=word(f); c.target=word(f); c.source=word(f); c.x=word(f); c.y=word(f);
        c.w=word(f); c.h=word(f); c.param=word(f); uint32_t n=word(f);
        check(c.target<nt && c.source<nt && n<=w*h*2,"command limits");
        c.data.resize(n);check(fread(c.data.data(),4,n,f)==n,"source short");
        c.expected.resize(w*h);check(fread(c.expected.data(),2,w*h,f)==w*h,"expected short");
    }
    fclose(f);
    ID3D11Device* device=nullptr; ID3D11DeviceContext* context=nullptr;
    D3D_FEATURE_LEVEL level,requested=D3D_FEATURE_LEVEL_11_0;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&requested,1,
        D3D11_SDK_VERSION,&device,&level,&context)),"hardware D3D11 unavailable");
    sg_desc d{}; d.environment.d3d11.device=device; d.environment.d3d11.device_context=context;
    d.environment.defaults.color_format=SG_PIXELFORMAT_R32UI;
    d.environment.defaults.depth_format=SG_PIXELFORMAT_NONE; d.environment.defaults.sample_count=1;
    d.logger.func=logfn; sg_setup(&d);
    sg_shader_desc sd{};
    sd.vertex_func.source="float4 main(uint id:SV_VertexID):SV_Position {float2 p=float2((id<<1)&2,id&2);return float4(p.x*2-1,1-p.y*2,0,1);}";
    sd.fragment_func.source=fragment;
    sd.uniform_blocks[0].stage=SG_SHADERSTAGE_FRAGMENT; sd.uniform_blocks[0].size=sizeof(Params);
    for(int i=0;i<2;++i) { sd.views[i].texture.stage=SG_SHADERSTAGE_FRAGMENT;
        sd.views[i].texture.image_type=SG_IMAGETYPE_2D; sd.views[i].texture.sample_type=SG_IMAGESAMPLETYPE_UINT;
        sd.views[i].texture.hlsl_register_t_n=(uint8_t)i;
        sd.texture_sampler_pairs[i].stage=SG_SHADERSTAGE_FRAGMENT;
        sd.texture_sampler_pairs[i].view_slot=(uint8_t)i; sd.texture_sampler_pairs[i].sampler_slot=0; }
    sd.samplers[0].stage=SG_SHADERSTAGE_FRAGMENT;sd.samplers[0].sampler_type=SG_SAMPLERTYPE_NONFILTERING;
    sg_shader sh=sg_make_shader(&sd); check(sg_query_shader_state(sh)==SG_RESOURCESTATE_VALID,"shader invalid");
    sg_pipeline_desc pd{};pd.shader=sh;pd.colors[0].pixel_format=SG_PIXELFORMAT_R32UI;
    pd.depth.pixel_format=SG_PIXELFORMAT_NONE; sg_pipeline pipeline=sg_make_pipeline(&pd);
    sg_sampler_desc smpd{};smpd.min_filter=SG_FILTER_NEAREST;smpd.mag_filter=SG_FILTER_NEAREST;
    sg_sampler sampler=sg_make_sampler(&smpd);
    std::vector<Target> targets(nt);
    for(auto& t:targets)for(auto& i:t.sides)i=image(w,h,true);
    auto start=Clock::now(); size_t upload_bytes=0;
    for(auto& c:commands)if(!c.data.empty()) {
        c.upload=image(c.w,c.h,false,c.data);upload_bytes+=c.data.size()*4;
    }
    double create_upload_ms=ms(start);
    Image dummy=image(1,1,false,std::vector<uint32_t>{0});
    auto reset=[&]() { for(unsigned i=0;i<nt;++i) { targets[i].current=0;
        context->UpdateSubresource(native(targets[i].sides[0]),0,nullptr,initial[i].data(),w*4,0); }
        sg_reset_state_cache(); };
    auto submit=[&](const Command& c) {
        auto& t=targets[c.target]; auto& old=t.sides[t.current]; auto& next=t.sides[1-t.current];
        if(c.kind==4) {
            auto& s=targets[c.source]; context->CopyResource(native(next),native(s.sides[s.current]));
            sg_reset_state_cache();
        } else {
            sg_pass pass{};pass.attachments.colors[0]=next.attachment;
            pass.action.colors[0].load_action=SG_LOADACTION_DONTCARE;sg_begin_pass(&pass);
            sg_apply_pipeline(pipeline);sg_bindings b{};b.views[0]=old.texture;b.samplers[0]=sampler;
            b.views[1]=c.kind==5?targets[c.source].sides[targets[c.source].current].texture:
                c.data.empty()?dummy.texture:c.upload.texture;
            sg_apply_bindings(&b);
            Params p{c.kind,c.param,fmt,0,{(int)c.x,(int)c.y,(int)c.w,(int)c.h}};
            sg_range u{&p,sizeof p};sg_apply_uniforms(0,&u);sg_draw(0,3,1);sg_end_pass();
        }
        t.current=1-t.current;
    };
    D3D11_TEXTURE2D_DESC td{};native(targets[0].sides[0])->GetDesc(&td);
    td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;td.MiscFlags=0;
    ID3D11Texture2D* staging=nullptr;check(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&staging)),"staging");
    reset();size_t mismatches=0;UINT readback_pitch=0;
    for(unsigned k=0;k<nc;++k) {
        const auto& c=commands[k];submit(c);sg_commit();
        auto& t=targets[c.target];context->CopyResource(staging,native(t.sides[t.current]));
        D3D11_MAPPED_SUBRESOURCE m{};check(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&m)),"map");
        readback_pitch=m.RowPitch;
        size_t bad=0;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            uint32_t got=((uint32_t*)((char*)m.pData+y*m.RowPitch))[x];
            if(got!=c.expected[y*w+x]) { if(bad<2)std::fprintf(stderr,"op %u pixel %u,%u got %04x expected %04x\n",k,x,y,got,c.expected[y*w+x]); ++bad; }
        }
        if(bad)std::fprintf(stderr,"op %u mismatches %zu\n",k,bad);
        mismatches+=bad;context->Unmap(staging,0);sg_reset_state_cache();
    }
    D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT,0};ID3D11Query* query=nullptr;
    check(SUCCEEDED(device->CreateQuery(&qd,&query)),"query");
    std::vector<double> cpu_times,wait_times,upload_times;
    for(int trial=0;trial<25;++trial) {
        reset();start=Clock::now();
        for(const auto& c:commands)if(!c.data.empty()) { sg_image_data data{};
            data.mip_levels[0]={c.data.data(),c.data.size()*4};sg_update_image(c.upload.image,&data); }
        double upload_ms=ms(start);
        start=Clock::now(); for(const auto& c:commands)submit(c);sg_commit();
        double submit_ms=ms(start);context->End(query);context->Flush();start=Clock::now();
        while(context->GetData(query,nullptr,0,0)==S_FALSE)Sleep(0);
        if(trial>=5) { cpu_times.push_back(submit_ms); wait_times.push_back(ms(start));upload_times.push_back(upload_ms); }
    }
    std::sort(cpu_times.begin(),cpu_times.end());std::sort(wait_times.begin(),wait_times.end());
    std::sort(upload_times.begin(),upload_times.end());
    std::printf("{\"commands\":%u,\"width\":%u,\"height\":%u,\"readback_row_pitch\":%u,\"compared_pixels\":%llu,\"mismatches\":%zu,\"source_upload_bytes\":%zu,"
        "\"source_creation_upload_ms\":%.4f,\"cpu_submit_ms_p50\":%.4f,\"cpu_submit_ms_p95\":%.4f,"
        "\"benchmark_fence_wait_ms_p50\":%.4f,\"upload_ms_p50\":%.4f,\"routine_readbacks\":0,\"validation_readbacks\":%u}\n",
        nc,w,h,readback_pitch,(unsigned long long)nc*w*h,mismatches,upload_bytes,create_upload_ms,cpu_times[10],cpu_times[18],wait_times[10],upload_times[10],nc);
    query->Release();staging->Release();sg_shutdown();context->Release();device->Release();
    return mismatches?1:0;
}
