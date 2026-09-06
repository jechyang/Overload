#define SNAPSHOT_VALIDATION_NO_MAIN
#include "snapshot_validation.cpp"
#include <OvCore/Rendering/EngineBufferRenderFeature.h>
#include <OvCore/Rendering/EngineDrawableDescriptor.h>
#include <OvCore/Rendering/SkinningRenderFeature.h>
#include <OvCore/Rendering/SkinningDrawableDescriptor.h>

using EngineFeature = OvCore::Rendering::EngineBufferRenderFeature;
using PaletteFeature = OvCore::Rendering::SkinningRenderFeature;
struct EngineProbe : EngineFeature { using EngineFeature::EngineFeature; using EngineFeature::OnBeginFrame; using EngineFeature::OnBeforeDraw; using EngineFeature::OnEndFrame; };
struct PaletteProbe : PaletteFeature { using PaletteFeature::PaletteFeature; using PaletteFeature::OnBeginFrame; using PaletteFeature::OnBeforeDraw; using PaletteFeature::OnEndFrame; };
struct DrawInputRenderer : ProbeRenderer
{
    using ProbeRenderer::ProbeRenderer;
#ifdef DRAW_INPUTS_NEW_PATH
    using ABaseRenderer::GeometryDrawCommand;
    using ABaseRenderer::ExecuteGeometryDraw;
#endif
};

std::ostream* inputTrace = nullptr;
size_t inputCalls = 0;
std::unordered_map<GLuint,unsigned> bufferTags;
PFNGLNAMEDBUFFERDATAPROC inputData;
PFNGLNAMEDBUFFERSUBDATAPROC inputSubData;
PFNGLBINDBUFFERBASEPROC inputBind;
unsigned BufferTag(GLuint id)
{
    if (id == 0) return 0;
    auto [it, inserted] = bufferTags.try_emplace(id, static_cast<unsigned>(bufferTags.size()+1));
    (void)inserted; return it->second;
}
void APIENTRY InputData(GLuint id, GLsizeiptr size, const void* data, GLenum usage)
{
    ++inputCalls;
    if (inputTrace) { Write(*inputTrace,0u); Write(*inputTrace,BufferTag(id)); Write(*inputTrace,size); Write(*inputTrace,usage); }
    inputData(id,size,data,usage);
}
void APIENTRY InputSubData(GLuint id, GLintptr offset, GLsizeiptr size, const void* data)
{
    ++inputCalls;
    if (inputTrace) { Write(*inputTrace,1u); Write(*inputTrace,BufferTag(id)); Write(*inputTrace,offset); Write(*inputTrace,size); inputTrace->write(static_cast<const char*>(data),size); }
    inputSubData(id,offset,size,data);
}
void APIENTRY InputBind(GLenum target, GLuint index, GLuint buffer)
{
    ++inputCalls;
    if (inputTrace) { Write(*inputTrace,2u); Write(*inputTrace,target); Write(*inputTrace,index); Write(*inputTrace,BufferTag(buffer)); }
    inputBind(target,index,buffer);
}
struct InputHooks
{
    InputHooks()
    {
        bufferTags.clear(); inputTrace = nullptr;
        inputData=glad_glNamedBufferData; glad_glNamedBufferData=InputData;
        inputSubData=glad_glNamedBufferSubData; glad_glNamedBufferSubData=InputSubData;
        inputBind=glad_glBindBufferBase; glad_glBindBufferBase=InputBind;
    }
    bool active=true;
    void Restore()
    {
        if(!active) return; active=false;
        inputTrace=nullptr;
        glad_glNamedBufferData=inputData; glad_glNamedBufferSubData=inputSubData; glad_glBindBufferBase=inputBind;
    }
    ~InputHooks(){Restore();}
};

void DrawInputValidation(const std::filesystem::path& dir, bool benchmark=false)
{
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion=4; ds.contextMinorVersion=5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible=false; ws.focused=false; ws.width=32; ws.height=32; ws.samples=1;
    OvWindowing::Window window(device,ws); window.MakeCurrentContext();
    Context::Driver driver(Settings::DriverSettings{true});
    DrawInputRenderer renderer(driver);
    Core::CompositeRenderer featureHost(driver);
    EngineProbe engine(featureHost,Features::EFeatureExecutionPolicy::ALWAYS);
    PaletteProbe skin(featureHost,Features::EFeatureExecutionPolicy::ALWAYS);
    const auto shaderFile=dir / "draw-input.ovfx";
    std::ofstream shaderText(shaderFile);
    shaderText << R"(#engine_feature SKINNING
#shader vertex
#version 450 core
layout(location=0) in vec3 pos;
layout(location=5) in uvec4 ids;
layout(location=6) in vec4 weights;
layout(std140,binding=0) uniform Engine { mat4 model; mat4 view; mat4 projection; vec3 camera; float elapsed; mat4 user; };
#ifdef SKINNING
layout(std430,binding=1) readonly buffer Palette { mat4 bones[]; };
#endif
void main(){
 vec4 point=vec4(pos,1);
#ifdef SKINNING
 point=bones[ids.x]*point*weights.x;
#endif
 gl_Position=model*point;
 gl_Position.xy+=vec2(user[0][1],user[1][0])*0.015;
}
#shader fragment
#version 450 core
uniform vec4 tint=vec4(0.8,0.3,0.1,1);
out vec4 color; void main(){color=tint;}
)";
    shaderText.close();
    using SL=Resources::Loaders::ShaderLoader;
    const auto deleteShader=[](Resources::Shader* p){SL::Destroy(p);};
    std::unique_ptr<Resources::Shader,decltype(deleteShader)> shader(SL::Create(shaderFile.string()),deleteShader);
    Require(shader != nullptr,"draw input shader missing");
    std::array<Geometry::SkinnedVertex,3> vertices{};
    vertices[0].position[0]=-0.6f; vertices[0].position[1]=-0.6f;
    vertices[1].position[0]=0.6f; vertices[1].position[1]=-0.6f;
    vertices[2].position[1]=0.6f;
    for(auto& v:vertices) v.boneWeights[0]=1;
    const std::array<uint32_t,3> indices{0,1,2};
    Resources::Mesh mesh(vertices,indices);
    Mat material(shader.get());
    baregl::Framebuffer target("Draw input validation");
    OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,32,32,true,true,false);
    Entities::Camera camera; camera.SetClearColor({0,0,0});
    Data::FrameDescriptor frame{.renderWidth=32,.renderHeight=32,.camera=camera,.outputBuffer=target};
    auto begin=[&]{renderer.BeginFrame(frame); engine.OnBeginFrame(frame); skin.OnBeginFrame(frame);};
    auto end=[&]{skin.OnEndFrame(); engine.OnEndFrame(); renderer.EndFrame(); TracyGpuCollect;};
    auto read=[&]{ std::vector<unsigned char> pixels(32*32*4); glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data()); Require(glGetError()==GL_NO_ERROR,"draw input GL error"); return pixels; };
    const FMatrix4 user{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    auto describe=[&](Entities::Drawable& drawable, const FMatrix4& model, std::vector<FMatrix4>& palette, uint64_t version)
    {
        drawable.SetDescriptor<OvCore::Rendering::EngineDrawableDescriptor>({model,user});
        drawable.SetDescriptor<OvCore::Rendering::SkinningDrawableDescriptor>({palette.data(),static_cast<uint32_t>(palette.size()),version,1});
    };
    std::ofstream trace(dir / "draw-input-trace.bin",std::ios::binary), frames(dir / "draw-input-frames.bin",std::ios::binary);
    InputHooks hooks;
    size_t visible=0;
    for(unsigned i=0;i<24;++i)
    {
        begin();
        auto pso=renderer.CreatePipelineState();
        Entities::Drawable drawable; drawable.mesh=mesh; drawable.material=material; drawable.stateMask=Mask(2);
        drawable.featureSetOverride=Data::FeatureSet{};
        std::vector<FMatrix4> palette(1+i%3,FMatrix4::Transpose(FMatrix4::Translation({float(i%5)*0.03f,0,0})));
        describe(drawable,FMatrix4::Translation({0,float(i%4)*0.04f,0}),palette,i+1);
        if(i%4 != 0) drawable.featureSetOverride=Data::FeatureSet{"SKINNING"};
        if(i%4 == 1) drawable.SetDescriptor<OvCore::Rendering::SkinningDrawableDescriptor>({});
        inputTrace=&trace;
        for(int repeat=0;repeat<3;++repeat)
        {
            if(repeat==2 && i%4>=2) describe(drawable,FMatrix4::Identity,palette,i+100);
            engine.OnBeforeDraw(pso,drawable); skin.OnBeforeDraw(pso,drawable); renderer.DrawEntity(pso,drawable);
        }
        inputTrace=nullptr;
        const auto pixels=read();
        visible+=pixels[(16*32+16)*4]>0;
        frames.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));
        end();
    }
    Require(visible==24 && trace.good() && frames.good(),"draw input image/trace output missing");
    std::puts("PASS: 24 draw-input frames, 72 immediate draws, exact buffer allocation/upload/binding trace, palette cache/identity/static transitions");
#ifdef DRAW_INPUTS_NEW_PATH
    struct Packet
    {
        DrawInputRenderer::GeometryDrawCommand geometry;
        Mat::Snapshot material;
        EngineFeature::DrawUploadCommand model;
        PaletteFeature::PaletteSnapshot palette;
    };
    {
        struct DerivedMesh : Resources::Mesh
        {
            using Mesh::Mesh;
            uint32_t GetIndexCount() const override { return 0; }
        } derived(vertices,indices);
        Entities::Drawable drawable; drawable.mesh=derived; drawable.material=material;
        const auto command=renderer.PrepareGeometryDraw({},drawable);
        Require(!command.retainedMesh && &command.mesh==&derived,"retention bypassed custom Mesh virtual behavior");
        Require(!EngineFeature::PrepareDrawUpload(drawable),"absent engine descriptor recorded a write");
    }
    std::ofstream retainedFrames(dir / "retained-draw-frames.bin",std::ios::binary);
    for(unsigned cycle=0;cycle<8;++cycle)
    {
        auto ownedMesh=std::make_unique<Resources::Mesh>(vertices,indices);
        auto ownedMaterial=std::make_unique<Mat>(shader.get());
        auto drawable=std::make_unique<Entities::Drawable>();
        drawable->mesh=*ownedMesh; drawable->material=*ownedMaterial; drawable->stateMask=Mask(2);
        drawable->featureSetOverride=Data::FeatureSet{"SKINNING"};
        std::vector<FMatrix4> palette(3,FMatrix4::Transpose(FMatrix4::Translation({float(cycle)*0.02f,0.1f,0})));
        describe(*drawable,FMatrix4::Translation({0,-0.1f,0}),palette,cycle+1);
        ownedMesh->Bind(); GLint vao=0,vbo=0,ebo=0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetVertexArrayIndexediv(vao,0,GL_VERTEX_BINDING_BUFFER,&vbo);
        glGetVertexArrayiv(vao,GL_ELEMENT_ARRAY_BUFFER_BINDING,&ebo);
        ownedMesh->Unbind();
        std::weak_ptr<const Resources::IMesh> weak=ownedMesh->RetainGeometry();
        begin(); auto pso=renderer.CreatePipelineState();
        engine.OnBeforeDraw(pso,*drawable); skin.OnBeforeDraw(pso,*drawable); renderer.DrawEntity(pso,*drawable);
        const auto expected=read(); end();
        allocationFailureSize=palette.size()*sizeof(FMatrix4); allocationFailureCountdown=0;
        bool rejected=false; const auto beforeFailure=inputCalls;
        try{(void)PaletteFeature::RecordPalette(*drawable);}catch(const std::bad_alloc&){rejected=true;}
        allocationFailureCountdown=-1;
        Require(rejected && inputCalls==beforeFailure,"palette allocation failure touched GL");
        const auto before=inputCalls;
        std::optional<Packet> packet(Packet{renderer.PrepareGeometryDraw(pso,*drawable),ownedMaterial->RecordSnapshot({},Data::FeatureSet{"SKINNING"}),*EngineFeature::PrepareDrawUpload(*drawable),PaletteFeature::RecordPalette(*drawable)});
        Require(inputCalls==before && packet->geometry.retainedMesh,"draw recording touched GL or failed to retain geometry");
        // All source wrappers and descriptor arrays are gone before replay.
        ownedMaterial->SetProperty("tint",FVector4{0,0,0,1});
        palette.assign(200,FMatrix4{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}); drawable.reset(); ownedMaterial.reset(); ownedMesh.reset();
        palette.clear(); palette.shrink_to_fit();
        Require(!weak.expired(),"mesh wrapper destruction retired in-flight geometry");
        for(int repeat=0;repeat<4;++repeat)
        {
            begin();
            Entities::Drawable poison; poison.material=material; poison.featureSetOverride=Data::FeatureSet{"SKINNING"};
            std::vector<FMatrix4> wrong(3,FMatrix4{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}); describe(poison,FMatrix4{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},wrong,900);
            engine.OnBeforeDraw(pso,poison); skin.OnBeforeDraw(pso,poison);
            engine.ExecuteDrawUpload(packet->model); skin.ExecutePaletteSnapshot(packet->palette);
            packet->material.Execute(); renderer.ExecuteGeometryDraw(packet->geometry);
            const auto actual=read(); Require(actual==expected,"retained draw differs after source destruction/state contamination");
            retainedFrames.write(reinterpret_cast<const char*>(actual.data()),static_cast<std::streamsize>(actual.size()));
            // Replay must invalidate the immediate palette cache, even when the
            // live component pointer/version still match the preceding upload.
            skin.OnBeforeDraw(pso,poison);
            GLint buffer=0; glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,1,&buffer);
            std::array<float,16> gpu{}; glGetNamedBufferSubData(buffer,0,sizeof(gpu),gpu.data());
            Require(std::all_of(gpu.begin(),gpu.end(),[](float v){return v==0;}),"snapshot replay left stale immediate palette cache");
            Entities::Drawable absent;
            const auto noPalette=PaletteFeature::RecordPalette(absent);
            const auto beforeNoop=inputCalls;
            skin.ExecutePaletteSnapshot(noPalette);
            Require(inputCalls==beforeNoop,"disabled palette snapshot touched GL");
            absent.featureSetOverride=Data::FeatureSet{"SKINNING"};
            const auto identity=PaletteFeature::RecordPalette(absent);
            skin.ExecutePaletteSnapshot(identity);
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,1,&buffer);
            glGetNamedBufferSubData(buffer,0,sizeof(gpu),gpu.data());
            Require(std::memcmp(gpu.data(),FMatrix4::Identity.data,sizeof(gpu))==0,"missing palette snapshot did not bind identity");
            end();
        }
        packet.reset();
        Require(weak.expired() && !glIsVertexArray(vao) && !glIsBuffer(vbo) && !glIsBuffer(ebo),"last geometry lease did not retire GPU objects");
    }
    Require(retainedFrames.good(),"retained draw output failed");
    std::puts("PASS: 8 destroyed mesh/material/palette cycles, 32 retained draw frames, GPU geometry retirement, CPU-only capture/failure, immediate palette cache recovery");
#endif
    if(benchmark)
    {
        hooks.Restore(); begin();
        auto pso=renderer.CreatePipelineState();
        std::array<std::vector<FMatrix4>,2> palettes{std::vector<FMatrix4>(45,FMatrix4::Identity),std::vector<FMatrix4>(45,FMatrix4::Identity)};
        std::array<Entities::Drawable,2> draws;
        for(size_t i=0;i<draws.size();++i)
        {
            draws[i].material=material; draws[i].featureSetOverride=Data::FeatureSet{"SKINNING"};
            describe(draws[i],FMatrix4::Identity,palettes[i],1);
        }
        std::vector<double> times;
        for(int sample=-32;sample<256;++sample)
        {
            const auto start=std::chrono::steady_clock::now();
            for(int repeat=0;repeat<32;++repeat) skin.OnBeforeDraw(pso,draws[repeat%2]);
            const auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/32;
            if(sample>=0) times.push_back(us);
        }
        std::sort(times.begin(),times.end());
        std::printf("BENCH: 45-bone palette misses median=%.3f us p95=%.3f us (256 x 32, no hooks)\n",times[128],times[243]);
        end();
    }
    glUseProgram(0);
}

#ifndef DRAW_INPUT_VALIDATION_NO_MAIN
int main(int argc,char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    Require(argc==2 || argc==3,"pass an output directory");
    if(argc==3){DrawInputValidation(argv[1],true); return 0;}
    MaterialValidation(argv[1]); PreparationValidation(); GeometryValidation(argv[1]); ImageValidation(argv[1],false);
#ifdef SNAPSHOT_NEW_PATH
    SnapshotUniformValidation(argv[1]); SnapshotValidation(argv[1]);
#endif
    DrawInputValidation(argv[1]); return 0;
}
catch(const std::exception& error){allocationFailureCountdown=-1; std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1;}

#endif
