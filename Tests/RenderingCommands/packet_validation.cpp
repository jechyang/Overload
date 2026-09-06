#define DRAW_INPUT_VALIDATION_NO_MAIN
#include "draw_input_validation.cpp"
#include <OvCore/Rendering/ReflectionRenderFeature.h>
#include <OvCore/Rendering/ShadowRenderFeature.h>
#include <OvRendering/Features/LightingRenderFeature.h>

struct PacketPass : Core::ARenderPass
{
    PacketPass(Core::CompositeRenderer& renderer,std::function<void(Data::PipelineState)> body):ARenderPass(renderer),body(std::move(body)){}
    void Draw(Data::PipelineState pso) override {body(pso);}
    std::function<void(Data::PipelineState)> body;
};
struct UnreviewedFeature : Features::ARenderFeature
{
    UnreviewedFeature(Core::CompositeRenderer& renderer,Features::EFeatureExecutionPolicy policy):ARenderFeature(renderer,policy){}
};

struct UnreviewedEngineFeature : EngineFeature
{
    UnreviewedEngineFeature(Core::CompositeRenderer& renderer,Features::EFeatureExecutionPolicy policy):EngineFeature(renderer,policy){}
};
struct PacketCustomMesh : Resources::Mesh
{
    using Resources::Mesh::Mesh;
    void Bind() const override {throw std::logic_error("custom mesh must be rejected before binding");}
};

void APIENTRY PacketElements(GLenum mode,GLsizei count,GLenum type,const void* indices)
{
    Record(0,mode,count,1,type);
    // Compare data visible to each real draw, without comparing GPU object IDs.
    GLint buffer=0,program=0;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,0,&buffer);
    std::array<std::byte,64> model{};glGetNamedBufferSubData(buffer,0,model.size(),model.data());
    drawTrace->write(reinterpret_cast<const char*>(model.data()),model.size());
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    std::array<float,4> tint{};glGetUniformfv(program,glGetUniformLocation(program,"tint"),tint.data());
    for(auto value:tint)Write(*drawTrace,value);
    realElements(mode,count,type,indices);
}
struct PacketDrawHooks
{
    PacketDrawHooks(std::ostream& trace){drawTrace=&trace;drawCounts={};realElements=glad_glDrawElements;glad_glDrawElements=PacketElements;}
    ~PacketDrawHooks(){glad_glDrawElements=realElements;drawTrace=nullptr;}
};

void PacketValidation(const std::filesystem::path& dir)
{
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion=4; ds.contextMinorVersion=5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible=false; ws.focused=false; ws.width=64; ws.height=32; ws.samples=1;
    OvWindowing::Window window(device,ws); window.MakeCurrentContext();
    Context::Driver driver(Settings::DriverSettings{true});
    Core::CompositeRenderer renderer(driver);
    renderer.AddFeature<EngineFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
    renderer.AddFeature<PaletteFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
    renderer.AddFeature<OvCore::Rendering::ReflectionRenderFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
    renderer.AddFeature<OvCore::Rendering::ShadowRenderFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
    Scene scene;
    auto& actor=scene.CreateActor("Packet reflection probe");
    auto& probe=actor.AddComponent<OvCore::ECS::Components::CReflectionProbe>();
    probe.SetCubemapResolution(4); probe.SetCaptureSpeed(OvCore::ECS::Components::CReflectionProbe::ECaptureSpeed::SIX_FACES);
    Entities::Light light; light.type=Settings::ELightType::DIRECTIONAL; light.castShadows=true;
    light.shadowCamera=Entities::Camera{}; light.lightSpaceMatrix=FMatrix4::Identity;
    light.shadowBuffer=std::make_unique<baregl::Framebuffer>();
    auto depth=std::make_shared<baregl::Texture>(ETextureType::TEXTURE_2D);
    depth->Allocate({.width=4,.height=4,.minFilter=ETextureFilteringMode::NEAREST,.magFilter=ETextureFilteringMode::NEAREST,.internalFormat=EInternalFormat::DEPTH_COMPONENT,.useMipMaps=false,.mutableDesc=baregl::data::MutableTextureDesc{.format=EFormat::DEPTH_COMPONENT,.type=EPixelDataType::FLOAT}});
    light.shadowBuffer->Attach(depth,EFramebufferAttachment::DEPTH);
    Require(light.shadowBuffer->Validate(),"shadow framebuffer invalid");
    light.shadowBuffer->SetTargetDrawBuffer({}); light.shadowBuffer->SetTargetReadBuffer({});
    const auto file=dir / "packet.ovfx";
    std::ofstream shaderText(file);
    shaderText << R"(#engine_feature SKINNING
#shader vertex
#version 450 core
layout(location=0) in vec3 pos;
layout(location=5) in uvec4 ids; layout(location=6) in vec4 weights;
layout(std140,binding=0) uniform Engine{mat4 model;mat4 view;mat4 projection;vec3 camera;float elapsed;mat4 user;};
#ifdef SKINNING
layout(std430,binding=1) readonly buffer Palette{mat4 bones[];};
#endif
void main(){vec4 p=vec4(pos,1);
#ifdef SKINNING
p=bones[ids.x]*p*weights.x;
#endif
gl_Position=model*p;}
#shader fragment
#version 450 core
layout(std140,binding=1) uniform Probe{vec4 probePosition;mat4 rotation;vec4 boxCenter;vec4 halfExtents;float brightness;bool boxProjection;bool probeLocal;};
uniform samplerCube _EnvironmentMap; uniform sampler2D _ShadowMap;
uniform mat4 _LightSpaceMatrix=mat4(1); uniform vec4 tint=vec4(1);
out vec4 color;
void main(){color=vec4((texture(_EnvironmentMap,vec3(1,0,0)).rgb*brightness+vec3(texture(_ShadowMap,vec2(0.5)).r*0.2)+vec3(_LightSpaceMatrix[0][0]*0.1))*tint.rgb,1);}
)"; shaderText.close();
    const auto destroy=[](Resources::Shader* p){Resources::Loaders::ShaderLoader::Destroy(p);};
    std::unique_ptr<Resources::Shader,decltype(destroy)> shader(Resources::Loaders::ShaderLoader::Create(file.string()),destroy);
    Require(shader!=nullptr,"packet shader missing");
    Mat reflective(shader.get()),plain(shader.get());
    reflective.SetReceiveReflections(true); reflective.SetReceiveShadows(true);
    plain.SetReceiveReflections(false); plain.SetReceiveShadows(false);
    std::array<Geometry::SkinnedVertex,3> vertices{};
    vertices[0].position[0]=-0.8f;vertices[0].position[1]=-0.8f;vertices[1].position[0]=0.8f;vertices[1].position[1]=-0.8f;vertices[2].position[1]=0.8f;
    for(auto& v:vertices)v.boneWeights[0]=1;
    const std::array<uint32_t,3> indices{0,1,2}; Resources::Mesh mesh(vertices,indices);
    std::vector<FMatrix4> palette(1,FMatrix4::Identity);
    Entities::Drawable left,right;
    for(auto* drawable:{&left,&right}){drawable->mesh=mesh;drawable->stateMask=Mask(2);drawable->featureSetOverride=Data::FeatureSet{"SKINNING"};drawable->AddDescriptor<OvCore::Rendering::SkinningDrawableDescriptor>({palette.data(),1,1,1});}
    left.material=reflective;right.material=plain;
    left.AddDescriptor<OvCore::Rendering::EngineDrawableDescriptor>({FMatrix4::Translation({-0.5f,0,0})*FMatrix4::Scaling({0.5f,1,1}),FMatrix4::Identity});
    right.AddDescriptor<OvCore::Rendering::EngineDrawableDescriptor>({FMatrix4::Translation({0.5f,0,0})*FMatrix4::Scaling({0.5f,1,1}),FMatrix4::Identity});
    bool failureCase=false;
    renderer.AddPass<PacketPass>("packets",0,std::function<void(Data::PipelineState)>([&](auto pso){
        if(failureCase){renderer.DrawEntity(pso,right);return;}
        reflective.SetProperty("tint",FVector4{0.5f,0.7f,0.9f,1},true);
        renderer.DrawEntity(pso,left);renderer.DrawEntity(pso,left);
        plain.SetProperty("tint",FVector4{0.25f,0.5f,0.75f,1},true);
        renderer.DrawEntity(pso,right);renderer.DrawEntity(pso,right);renderer.DrawEntity(pso,right);
    }));
    baregl::Framebuffer target; OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,64,32,true,true,false);
    Entities::Camera camera;camera.SetClearColor({0,0,0});
    Data::FrameDescriptor frame{.renderWidth=64,.renderHeight=32,.camera=camera,.outputBuffer=target};
    auto begin=[&]{renderer.AddDescriptor<OvCore::Rendering::ReflectionRenderFeature::ReflectionDescriptor>({{probe}});renderer.AddDescriptor<Features::LightingRenderFeature::LightingDescriptor>({{light}});renderer.BeginFrame(frame);};
    auto pixels=[&]{std::vector<unsigned char> image(64*32*4);glReadPixels(0,0,64,32,GL_RGBA,GL_UNSIGNED_BYTE,image.data());Require(glGetError()==GL_NO_ERROR,"packet GL error");return image;};
    std::ofstream images(dir / "packet-frames.bin",std::ios::binary);
    std::ofstream traces(dir / "packet-draw-trace.bin",std::ios::binary);
    for(unsigned i=0;i<12;++i)
    {
        probe.SetBrightness(0.25f+float(i)*0.025f);
        auto& probeTarget=probe.m_framebuffers[0];
        const std::array<float,4> color{0.15f+float(i)*0.025f,0.2f,0.3f,1};
        for(unsigned face=0;face<6;++face){probeTarget.SetTargetDrawBuffer(face);glClearNamedFramebufferfv(probeTarget.GetID(),GL_COLOR,0,color.data());}
        probe.GetCubemap()->GenerateMipmaps(); probe.m_isAnyCubemapComplete=true;
        const float z=0.1f+float(i)*0.03f;glClearTexImage(depth->GetID(),0,GL_DEPTH_COMPONENT,GL_FLOAT,&z);
        light.lightSpaceMatrix=FMatrix4::Scaling({1+float(i)*0.02f,1,1});
        std::vector<unsigned char> reference;std::string referenceTrace;
        for(bool recorded:{false,true})
        {
            renderer.SetDrawRecordingEnabled(recorded); begin();
            std::ostringstream trace(std::ios::binary);{PacketDrawHooks hooks(trace);renderer.DrawFrame();}
            Require(drawCounts[0]==5,"packet draw count differs");
            const auto image=pixels();renderer.EndFrame();TracyGpuCollect;
            if(!recorded)referenceTrace=trace.str();else Require(trace.str()==referenceTrace,"draw order/model/tint/geometry state differs");
            traces<<trace.str();
            Require(std::get<FVector4>(plain.GetProperty("tint")->value).x==1,"single-use source was not consumed");
            const float expectedLeft=(color[0]*(0.25f+float(i)*0.025f)+z*0.2f+(1+float(i)*0.02f)*0.1f)*255;
            Require(std::abs(float(image[(16*64+16)*4])-expectedLeft)<=1,"reflection/shadow inputs were not sampled");
            Require(std::abs(int(image[(16*64+48)*4])-19)<=1,"single-use cached tint did not persist on GPU");
            if(!recorded)reference=image;else Require(image==reference,"recorded reflection/shadow/palette/cache image differs");
            images.write(reinterpret_cast<const char*>(image.data()),static_cast<std::streamsize>(image.size()));
        }
    }
    Require(images.good() && traces.good(),"packet output missing");
    // Unsupported feature rejection is before feature recording; EndFrame then
    // another frame must recover the pass cursor and recording/material caches.
    renderer.AddFeature<UnreviewedFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
    begin();bool rejected=false;
    try{renderer.DrawFrame();}catch(const std::invalid_argument&){rejected=true;}
    renderer.EndFrame();Require(rejected,"unknown feature accepted for recording");
    renderer.RemoveFeature<UnreviewedFeature>();
    renderer.AddFeature<UnreviewedEngineFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
    begin();rejected=false;
    try{renderer.DrawFrame();}catch(const std::invalid_argument&){rejected=true;}
    renderer.EndFrame();Require(rejected,"derived feature silently inherited recording opt-in");
    renderer.RemoveFeature<UnreviewedEngineFeature>();
    PacketCustomMesh customMesh(vertices,indices);right.mesh=customMesh;failureCase=true;
    begin();rejected=false;
    try{renderer.DrawFrame();}catch(const std::invalid_argument&){rejected=true;}
    renderer.EndFrame();Require(rejected,"custom mesh virtual behavior bypassed");right.mesh=mesh;
    plain.SetProperty("_ShadowMap",depth.get());
    begin(); rejected=false;
    {InputHooks hooks;const auto recordStart=inputCalls;try{renderer.DrawFrame();}catch(const std::invalid_argument&){rejected=true;}Require(inputCalls==recordStart,"failed recording performed feature GL uploads");}
    renderer.EndFrame(); Require(rejected,"unowned raw sampler accepted");
    plain.SetProperty("_ShadowMap",static_cast<baregl::Texture*>(nullptr));failureCase=false;
    begin();renderer.DrawFrame();pixels();renderer.EndFrame();
    // Attachment ownership survives replacing/destroying the framebuffer; the
    // retained object is not a promise to freeze future writes or resizes.
    auto retained=light.shadowBuffer->RetainTextureAttachment(EFramebufferAttachment::DEPTH);
    Require(retained==depth && !light.shadowBuffer->RetainTextureAttachment(EFramebufferAttachment::COLOR),"attachment lease mismatch");
    std::weak_ptr<baregl::Texture> weak=retained;const auto textureID=retained->GetID();
    light.shadowBuffer.reset();depth.reset();Require(!weak.expired() && glIsTexture(textureID),"attachment did not survive framebuffer destruction");
    retained.reset();Require(weak.expired() && !glIsTexture(textureID),"attachment lease leaked");
    std::puts("PASS: 12 immediate/packet image and ordered draw-state pairs, reflection UBO/cubemap + directional shadow + skinning + sticky single-use cache, failure recovery and attachment retirement");
}

int main(int argc,char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    Require(argc==2,"pass output directory");
    PacketValidation(argv[1]);
    const auto immediate=std::filesystem::path(argv[1])/"immediate",recorded=std::filesystem::path(argv[1])/"recorded";
    std::filesystem::create_directories(immediate);std::filesystem::create_directories(recorded);
    ImageValidation(immediate,false,false);ImageValidation(recorded,false,true);
    const auto read=[](const auto& p){std::ifstream f(p,std::ios::binary);return std::vector<char>(std::istreambuf_iterator<char>(f),{});};
    Require(read(immediate/"frames.bin")==read(recorded/"frames.bin"),"SceneRenderer packet frames differ");
    std::puts("PASS: 24 real Mannequin/Standard/SceneRenderer immediate/packet frames exactly match");return 0;
}
catch(const std::exception& e){allocationFailureCountdown=-1;std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
