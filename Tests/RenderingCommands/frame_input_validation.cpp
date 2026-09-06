#define DRAW_PACKET_VALIDATION_NO_MAIN
#include "packet_validation.cpp"

using LightFeature = Features::LightingRenderFeature;

std::vector<std::byte> ReadBuffer(GLuint id, GLintptr offset, size_t size)
{
    std::vector<std::byte> bytes(size);
    if(size)glGetNamedBufferSubData(id,offset,static_cast<GLsizeiptr>(size),bytes.data());
    return bytes;
}
GLuint BoundBuffer(GLenum target,GLuint index)
{
    GLint id=0;glGetIntegeri_v(target,index,&id);return static_cast<GLuint>(id);
}
template<class T> std::vector<std::byte> Bytes(const T& value)
{
    const auto* p=reinterpret_cast<const std::byte*>(&value);return {p,p+sizeof(value)};
}

void FrameInputValidation(const std::filesystem::path& dir)
{
    // This portion runs before GL initialization. Camera matrices must be cached
    // by the caller, and no source object is retained by preparation.
    std::vector<EngineFeature::FrameUploadCommand> frames;
    std::vector<std::vector<std::byte>> expected;
    for(unsigned i=0;i<32;++i)
    {
        OvMaths::FTransform transform;
        Entities::Camera camera(transform);
        camera.SetPosition({float(i)*0.1f,0.25f,3});camera.SetRotation(OvMaths::FQuaternion({float(i),float(i)*2,0}));
        camera.SetProjectionMode(i%2 ? Settings::EProjectionMode::ORTHOGRAPHIC : Settings::EProjectionMode::PERSPECTIVE);
        camera.SetSize(2+float(i)*0.05f);camera.SetFov(45+float(i));camera.CacheMatrices(80+i,48+i);
        // Original packed layout, built independently of the command fields.
        struct {FMatrix4 view,projection;FVector3 position;float elapsed;} reference{
            FMatrix4::Transpose(camera.GetViewMatrix()),FMatrix4::Transpose(camera.GetProjectionMatrix()),camera.GetPosition(),float(i)*0.125f};
        frames.push_back(EngineFeature::PrepareFrameUpload(camera,reference.elapsed));expected.push_back(Bytes(reference));
        camera.SetPosition({99,98,97});camera.CacheMatrices(4,4);
    }
    LightFeature::LightingUploadCommand lights;
    std::vector<std::byte> expectedLights;
    {
        Entities::Camera camera;camera.SetFrustumLightCulling(false);camera.CacheMatrices(64,32);
        Entities::Light nearLight,farLight,infinite;
        nearLight.type=Settings::ELightType::AMBIENT_SPHERE;nearLight.constant=0.1f;nearLight.transform->SetLocalPosition({0,0,0});
        farLight.type=Settings::ELightType::AMBIENT_SPHERE;farLight.constant=0.1f;farLight.transform->SetLocalPosition({100,100,100});
        infinite.type=Settings::ELightType::DIRECTIONAL;infinite.intensity=2;
        LightFeature::LightingDescriptor descriptor{{nearLight,farLight,infinite},{}};
        Require(LightFeature::PrepareLightingUpload(descriptor,camera).matrices.size()==3,"disabled light culling changed");
        Data::Frustum overrideFrustum;overrideFrustum.CalculateFrustum(FMatrix4::Identity);descriptor.frustumOverride=overrideFrustum;
        lights=LightFeature::PrepareLightingUpload(descriptor,camera);
        Require(lights.matrices.size()==2,"override frustum or infinite-range light changed");
        const std::array oracle{nearLight.GenerateMatrix(),infinite.GenerateMatrix()};expectedLights=Bytes(oracle);
        camera.SetFrustumLightCulling(true);camera.CacheFrustum(FMatrix4::Identity,FMatrix4::Identity);descriptor.frustumOverride={};
        Require(LightFeature::PrepareLightingUpload(descriptor,camera).matrices.size()==2,"camera light frustum ignored");
        nearLight.intensity=99;infinite.transform->SetLocalPosition({99,99,99});
        bool failed=false;allocationFailureSize=3*sizeof(FMatrix4);allocationFailureCountdown=0;
        try{(void)LightFeature::PrepareLightingUpload(descriptor,camera);}catch(const std::bad_alloc&){failed=true;}
        allocationFailureCountdown=-1;Require(failed,"light preparation allocation failure not exercised");
    }
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=64;ws.height=32;ws.samples=1;
    OvWindowing::Window window(device,ws);window.MakeCurrentContext();Context::Driver driver(Settings::DriverSettings{true});
    Core::CompositeRenderer host(driver);EngineFeature engine(host,Features::EFeatureExecutionPolicy::ALWAYS);
    LightFeature lighting(host,Features::EFeatureExecutionPolicy::ALWAYS,3);
    std::ofstream trace(dir/"frame-upload-trace.bin",std::ios::binary);
    for(size_t i=0;i<frames.size();++i)
    {
        engine.ExecuteFrameUpload(frames[(i+1)%frames.size()]);
        const auto id=BoundBuffer(GL_UNIFORM_BUFFER_BINDING,0);
        std::array<std::byte,272> poison{};std::fill(poison.begin(),poison.end(),std::byte{0xA5});glNamedBufferSubData(id,0,poison.size(),poison.data());
        {InputHooks hooks;inputTrace=&trace;engine.ExecuteFrameUpload(frames[i]);}
        Require(ReadBuffer(id,64,144)==expected[i],"owned camera/time changed after source destruction");
        Require(ReadBuffer(id,0,64)==std::vector<std::byte>(64,std::byte{0xA5}) && ReadBuffer(id,208,64)==std::vector<std::byte>(64,std::byte{0xA5}),"frame upload overwrote model/user");
        const auto next=(i+1)%frames.size();engine.ExecuteCameraUpload(frames[next].camera);
        Require(ReadBuffer(id,64,140)==Bytes(frames[next].camera),"camera upload layout changed");
        Require(ReadBuffer(id,204,4)==Bytes(frames[i].elapsedTime),"camera switch changed elapsed time");
    }
    {InputHooks hooks;inputTrace=&trace;lighting.ExecuteLightingUpload(lights);}
    auto sceneBinding=lighting.RecordLightBinding();
    Require(ReadBuffer(sceneBinding.buffer->GetID(),0,expectedLights.size())==expectedLights,"captured lights changed after destruction");
    lighting.ExecuteLightingUpload({});GLint size=-1;glGetNamedBufferParameteriv(sceneBinding.buffer->GetID(),GL_BUFFER_SIZE,&size);
    Require(size==0,"empty lighting retained stale buffer contents");
    lighting.ExecuteLightingUpload(lights);
    auto overrideBuffer=std::make_shared<baregl::Buffer>();overrideBuffer->Allocate(expectedLights.size(),EAccessSpecifier::STREAM_DRAW);overrideBuffer->Upload(expectedLights.data());
    const auto overrideID=overrideBuffer->GetID();std::weak_ptr<baregl::Buffer> weak=overrideBuffer;
    lighting.BindOverride(overrideBuffer);auto retained=lighting.RecordLightBinding();overrideBuffer.reset();lighting.Bind();
    Require(BoundBuffer(GL_SHADER_STORAGE_BUFFER_BINDING,3)==sceneBinding.buffer->GetID(),"Bind failed to restore scene light selection");
    Require(!weak.expired(),"captured light binding did not retain override");LightFeature::ExecuteLightBinding(retained);
    Require(BoundBuffer(GL_SHADER_STORAGE_BUFFER_BINDING,3)==overrideID,"retained light override not rebound");
    retained.buffer.reset();Require(weak.expired() && !glIsBuffer(overrideID),"light override retained after final release");
    bool rejected=false;try{lighting.BindOverride({});}catch(const std::invalid_argument&){rejected=true;}
    Require(rejected && lighting.RecordLightBinding().buffer==sceneBinding.buffer,"invalid override changed selected lighting");
    std::optional<LightFeature::LightBindingCommand> orphan;
    {LightFeature temporary(host,Features::EFeatureExecutionPolicy::ALWAYS,4);temporary.ExecuteLightingUpload(lights);orphan=temporary.RecordLightBinding();}
    const auto orphanID=orphan->buffer->GetID();LightFeature::ExecuteLightBinding(*orphan);
    Require(ReadBuffer(orphanID,0,expectedLights.size())==expectedLights,"light binding did not survive its feature");
    orphan.reset();Require(!glIsBuffer(orphanID),"scene light buffer did not retire");
    Require(glGetError()==GL_NO_ERROR && trace.good(),"frame/light replay GL error");
    std::puts("PASS: 32 destroyed-camera payloads, exact UBO layout/ranges/time preservation, light frustum/empty/failure, destroyed-light replay and override retirement");
}

void FramePacketValidation(const std::filesystem::path& dir)
{
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;
    OvWindowing::Context::Device device(ds);OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=64;ws.height=32;ws.samples=1;
    OvWindowing::Window window(device,ws);window.MakeCurrentContext();Context::Driver driver(Settings::DriverSettings{true});
    Core::CompositeRenderer renderer(driver);
    auto& engine=renderer.AddFeature<EngineFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
    auto& lighting=renderer.AddFeature<LightFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
    const auto destroy=[](Resources::Shader* s){Resources::Loaders::ShaderLoader::Destroy(s);};
    std::unique_ptr<Resources::Shader,decltype(destroy)> shader(Resources::Loaders::ShaderLoader::CreateFromSource(
        "#version 450 core\nlayout(location=0)in vec3 pos;layout(std140,binding=0)uniform Engine{mat4 model;mat4 view;mat4 projection;vec3 camera;float elapsed;mat4 user;};void main(){gl_Position=projection*view*model*vec4(pos,1);}",
        "#version 450 core\nlayout(std140,binding=0)uniform Engine{mat4 model;mat4 view;mat4 projection;vec3 camera;float elapsed;mat4 user;};layout(std430,binding=0)readonly buffer Lights{mat4 lights[];};out vec4 color;void main(){color=vec4(camera.z*.1,lights[0][3][3]*.15,.25,1);}"),destroy);
    Require(shader && shader->GetVariant({},{}).GetID()!=0,"frame packet shader invalid");Mat material(shader.get());
    const std::array<Geometry::Vertex,3> vertices{{{{-.8f,-.8f,0}},{{.8f,-.8f,0}},{{0,.8f,0}}}};const std::array<uint32_t,3> indices{0,1,2};Resources::Mesh mesh(vertices,indices);
    Entities::Drawable drawable;drawable.mesh=mesh;drawable.material=material;drawable.stateMask=Mask(2);
    drawable.AddDescriptor<OvCore::Rendering::EngineDrawableDescriptor>({FMatrix4::Identity,FMatrix4::Identity});
    Entities::Light light;light.type=Settings::ELightType::DIRECTIONAL;light.intensity=1;
    auto overrideBuffer=std::make_shared<baregl::Buffer>();const auto fake=FMatrix4::Identity*4.f;overrideBuffer->Allocate(sizeof(fake),EAccessSpecifier::STATIC_READ);overrideBuffer->Upload(&fake);
    baregl::Buffer noise;noise.Allocate(sizeof(fake),EAccessSpecifier::STREAM_DRAW);noise.Upload(&fake);
    baregl::Framebuffer target;OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,64,32,true,true,false);
    Entities::Camera camera;camera.SetPosition({0,0,3});camera.SetClearColor({0,0,0});
    Data::FrameDescriptor frame{.renderWidth=64,.renderHeight=32,.camera=camera,.outputBuffer=target};
    unsigned iteration=0;bool recorded=false;std::vector<std::vector<unsigned char>> reference;std::ofstream images(dir/"frame-packet-frames.bin",std::ios::binary);
    renderer.AddPass<PacketPass>("frame-cameras",0,std::function<void(Data::PipelineState)>([&](auto pso){
        const auto engineID=BoundBuffer(GL_UNIFORM_BUFFER_BINDING,0);const auto elapsed=ReadBuffer(engineID,204,4);
        for(unsigned view=0;view<3;++view)
        {
            EngineFeature::CameraUploadCommand expectedCamera;
            {Entities::Camera temporary;temporary.SetPosition({0,0,2.5f+iteration*.05f+view*.15f});temporary.SetRotation(OvMaths::FQuaternion({0,180,0}));temporary.SetFov(view==2?90.f:60.f);
             if(view==1){temporary.SetProjectionMode(Settings::EProjectionMode::ORTHOGRAPHIC);temporary.SetSize(2);}
             temporary.CacheMatrices(64,32);expectedCamera=EngineFeature::PrepareCameraUpload(temporary);engine.SetCamera(temporary);}
            if(view==1)lighting.BindOverride(overrideBuffer);else lighting.Bind();
            const auto expectedLight=lighting.RecordLightBinding();
            if(recorded){engine.ExecuteFrameUpload({{},-123.f});noise.Bind(EBufferType::SHADER_STORAGE,0);}
            renderer.Clear(true,true,true);renderer.DrawEntity(pso,drawable);
            Require(ReadBuffer(engineID,64,140)==Bytes(expectedCamera) && ReadBuffer(engineID,204,4)==elapsed,"draw packet failed to restore selected camera/time");
            Require(BoundBuffer(GL_SHADER_STORAGE_BUFFER_BINDING,0)==expectedLight.buffer->GetID(),"draw packet lost scene/override light selection");
            std::vector<unsigned char> image(64*32*4);glReadPixels(0,0,64,32,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
            Require(image[(16*64+32)*4]>0,"frame packet image blank");
            if(!recorded)reference.push_back(image);else Require(image==reference[view],"camera/light packet image differs");
            images.write(reinterpret_cast<const char*>(image.data()),image.size());
        }
    }));
    for(iteration=0;iteration<8;++iteration)
    {
        reference.clear();
        for(bool mode:{false,true}){recorded=mode;renderer.SetDrawRecordingEnabled(mode);renderer.AddDescriptor<LightFeature::LightingDescriptor>({{light},{}});renderer.BeginFrame(frame);renderer.DrawFrame();renderer.EndFrame();TracyGpuCollect;}
    }
    Require(glGetError()==GL_NO_ERROR && images.good(),"frame packet GL/output failure");
    std::puts("PASS: 24 camera/light immediate-packet image pairs, perspective/orthographic/cubemap camera switches, GPU contamination recovery and explicit light override/restore");
}

#ifndef FRAME_INPUT_VALIDATION_NO_MAIN
int main(int argc,char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);Require(argc==2,"pass output directory");
    FrameInputValidation(argv[1]);FramePacketValidation(argv[1]);PacketValidation(argv[1]);
    const auto immediate=std::filesystem::path(argv[1])/"immediate",recorded=std::filesystem::path(argv[1])/"recorded";
    std::filesystem::create_directories(immediate);std::filesystem::create_directories(recorded);
    ImageValidation(immediate,false,false);ImageValidation(recorded,false,true);
    const auto read=[](const auto& p){std::ifstream f(p,std::ios::binary);return std::vector<char>(std::istreambuf_iterator<char>(f),{});};
    Require(read(immediate/"frames.bin")==read(recorded/"frames.bin"),"SceneRenderer frame payload images differ");
    std::puts("PASS: 24 real Mannequin immediate/packet frames exactly match");return 0;
}
catch(const std::exception& e){allocationFailureCountdown=-1;std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}

#endif
