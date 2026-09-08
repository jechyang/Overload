#define COMMAND_LIST_VALIDATION_MAIN CommandListValidationMain
#include "command_list_validation.cpp"
#include <OvRendering/Data/RenderFrame.h>
#include <chrono>
#include <thread>
#include <mutex>

thread_local int recordingSubmissionFailure = -1;
void* operator new(std::size_t size, std::align_val_t alignment)
{
    if (recordingSubmissionFailure >= 0 && recordingSubmissionFailure-- == 0) throw std::bad_alloc();
    if (auto* p = _aligned_malloc(size, static_cast<size_t>(alignment))) return p;
    throw std::bad_alloc();
}
void operator delete(void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }

void RecordingGraphValidation(Context::Driver& driver)
{
    using List = Data::RenderCommandList;
    OvTools::Jobs::JobSystem jobs(4);
    const auto owner = std::this_thread::get_id();
    for (size_t count : {size_t(0), size_t(1), size_t(127), size_t(128), size_t(1024)})
    {
        std::vector<size_t> observed;
        std::atomic<size_t> workers = 0, finished = 0;
        List list;
        for (size_t i = 0; i < count; ++i)
        {
            list.AppendDeferred([&, i]() -> List::Operation {
                if (std::this_thread::get_id() != owner) ++workers;
                if (std::this_thread::get_id() != owner) Require(glfwGetCurrentContext() == nullptr, "worker owns GL context");
                ++finished;
                return [&, i](auto&) { Require(finished == count, "publication preceded recording fan-in"); observed.push_back(i); };
            });
            if (i % 31 == 0) list.Append([&, i](auto&) { observed.push_back(count + i); });
        }
        List moved(std::move(list));
        moved.Seal(&jobs);
        Require(observed.empty() && finished == count, "recording executed commands or lost outputs");
        Require((count >= 128) == (workers != 0), "recording threshold did not select expected path");
        moved.Execute(driver);
        size_t at = 0;
        for (size_t i = 0; i < count; ++i)
        { Require(observed[at++] == i, "parallel command order changed"); if (i % 31 == 0) Require(observed[at++] == count + i, "pass boundary moved"); }
    }
    for (size_t failing : {size_t(0), size_t(127), size_t(511)})
    {
        List::History history; List list; list.UseHistory(history);
        std::atomic<size_t> running = 0; size_t executed = 0;
        for (size_t i = 0; i < 512; ++i) list.AppendDeferred([&, i]() -> List::Operation {
            ++running; struct Exit { std::atomic<size_t>& value; ~Exit(){--value;} } exit{running};
            if (i == failing) throw std::runtime_error("expected encoding failure");
            return [&](auto&) { ++executed; };
        });
        bool rejected = false; try { list.Seal(&jobs); } catch (const std::runtime_error&) { rejected = true; }
        Require(rejected && list.GetState() == List::State::FAILED && running == 0 && executed == 0, "failed recording was published or escaped drain");
        Require(history.PrepareImmediate(), "encoding failure did not invalidate temporal history");
    }
    {
        auto capture = std::make_shared<int>(1); std::weak_ptr<int> weak = capture;
        List list; list.AppendDeferred([capture]() -> List::Operation { return [capture](auto&) {}; });
        capture.reset(); list.Cancel(); Require(weak.expired(), "cancelled factory kept source alive");
    }
    for (int failAfter : {0, 2, 3})
    {
        List list; std::atomic<size_t> running=0; size_t executed=0;
        for (size_t i=0;i<512;++i) list.AppendDeferred([&]() -> List::Operation {
            ++running; auto operation=[&](auto&) { ++executed; }; --running; return operation;
        });
        bool mainRan=false; auto mainJob=jobs.Submit([&]{mainRan=true;},{},OvTools::Jobs::EAffinity::MainThread);
        recordingSubmissionFailure=failAfter; bool failed=false;
        try { list.Seal(&jobs); } catch(const std::bad_alloc&) { failed=true; }
        recordingSubmissionFailure=-1;
        Require(failed&&list.GetState()==List::State::FAILED&&running==0&&executed==0&&!mainRan,"partial submit/join failure escaped drain or pumped Main work");
        jobs.Wait(mainJob); Require(mainRan,"queued Main job was lost");
    }
    for (bool fail : {false,true})
    {
        Data::RenderFrame frame;
        List::History sharedHistory;
        std::vector<List::Completion> children;
        std::vector<size_t> order; std::atomic<size_t> finished=0;
        for(size_t view=0;view<3;++view)
        {
            List list;list.UseHistory(sharedHistory);children.push_back(list.GetCompletion());
            for(size_t draw=0;draw<128;++draw) list.AppendDeferred([&,view,draw]() -> List::Operation {
                if(fail&&view==1&&draw==64) throw std::runtime_error("expected view encoding failure");
                ++finished;
                return [&,index=view*128+draw](auto&) { Require(finished==384,"view consumed before frame join");order.push_back(index); };
            });
            frame.AppendRecording(std::move(list));
        }
        bool rejected=false;try{frame.Seal(&jobs);}catch(const std::runtime_error&){rejected=true;}
        Require(rejected==fail&&order.empty(),"view graph published a partial frame");
        if(fail)
        {
            Require(frame.GetCompletion().GetState()==Data::RenderFrame::State::FAILED&&frame.GetCompletion().IsRetired(),"failed view graph did not retire frame");
            Require(sharedHistory.PrepareImmediate(),"failed view graph left temporal history valid");
        }
        else
        {
            for(const auto& child:children)Require(!child.Succeeded(),"CPU encoding masqueraded as execution");
            Require(frame.Submit(driver),"encoded view graph could not submit");frame.Wait();
            for(const auto& child:children)Require(child.Succeeded(),"view completion did not publish with frame");
            Require(order.size()==384,"view graph lost draw commands");
            for(size_t i=0;i<order.size();++i)Require(order[i]==i,"view merge reordered transparent/equal-key commands");
            Require(!sharedHistory.PrepareImmediate(),"successful ordered views invalidated shared history");
        }
    }
    std::puts("PASS: recording thresholds, worker affinity, fan-in, deterministic pass order, move, failure drain and cancellation");
}

void SnapshotInputValidation(const std::filesystem::path& file, Context::Driver& driver, OvTools::Jobs::JobSystem& jobs)
{
    using List=Data::RenderCommandList;
    for(int round=0;round<8;++round)
    {
        List commands;
        std::weak_ptr<baregl::Texture> weak;
        {
            const auto destroy=[](Resources::Shader* p){Resources::Loaders::ShaderLoader::Destroy(p);};
            std::unique_ptr<Resources::Shader,decltype(destroy)> shader(Resources::Loaders::ShaderLoader::Create(file.string()),destroy);
            Require(shader!=nullptr,"snapshot input shader missing"); Mat material(shader.get());
            auto texture=std::make_shared<baregl::Texture>(ETextureType::TEXTURE_2D); weak=texture;
            texture->Allocate({.width=1,.height=1,.minFilter=ETextureFilteringMode::NEAREST,.magFilter=ETextureFilteringMode::NEAREST,.internalFormat=EInternalFormat::RGBA8,.useMipMaps=false});
            material.SetProperty("surface",texture.get());
            material.SetProperty("tint",FVector4{.25f,.5f,.75f,1},true);
            bool rejected=false;
            try{material.PrepareSnapshot({}, {}, {}, {}, {}, {}, true, true);}catch(const std::invalid_argument&){rejected=true;}
            Require(rejected&&std::get<FVector4>(material.GetProperty("tint")->value).x==.25f,"invalid raw sampler consumed single-use input");
            const std::array<std::shared_ptr<baregl::Texture>,1> retained{texture};
            for(int i=0;i<128;++i)
            {
                const float value=float(i+round)/256;
                material.SetProperty("tint",FVector4{value,.5f,.75f,1},true);
                auto input=material.PrepareSnapshot({}, {}, {}, {}, retained, {}, true, true);
                commands.AppendDeferred([input,value]() -> List::Operation {
                    auto snapshot=Data::Material::EncodeSnapshot(input);
                    return [snapshot,value](auto&) {
                        snapshot->Execute(); GLint program=0;glGetIntegerv(GL_CURRENT_PROGRAM,&program);
                        std::array<float,4> tint{};glGetUniformfv(program,glGetUniformLocation(program,"tint"),tint.data());
                        Require(tint[0]==value,"destroyed/replaced source changed immutable shader input");
                    };
                });
            }
        }
        Require(!weak.expired(),"input failed to retain texture generation");
        commands.Seal(&jobs);commands.Execute(driver);
        Require(weak.expired(),"completed private command buffers retained unused resources");
    }
    std::puts("PASS: 1024 source-destroyed immutable material inputs, 8 GPU generations, unowned sampler rejection and final release");
}

void ParallelDrawValidation(const std::filesystem::path& dir, bool connected)
{
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion=4; ds.contextMinorVersion=5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible=false; ws.focused=false; ws.width=64; ws.height=32; ws.samples=1;
    OvWindowing::Window window(device,ws); window.MakeCurrentContext();
    Context::Driver driver(Settings::DriverSettings{true});
    RecordingGraphValidation(driver);
    OvTools::Jobs::JobSystem jobs(4);
    const auto file = dir/"parallel-recording.ovfx";
    { std::ofstream shader(file); shader << R"(#shader vertex
#version 450 core
layout(location=0) in vec3 pos;
layout(std140,binding=0) uniform Engine{mat4 model;mat4 view;mat4 projection;vec3 camera;float elapsed;mat4 user;};
void main(){gl_Position=model*vec4(pos,1);}
#shader fragment
#version 450 core
uniform vec4 tint=vec4(1); uniform mat4 transform=mat4(1); uniform sampler2D surface;
uniform mat4 detail0=mat4(1);
uniform mat4 detail1=mat4(1);
uniform mat4 detail2=mat4(1);
uniform mat4 detail3=mat4(1);
uniform mat4 detail4=mat4(1);
uniform mat4 detail5=mat4(1);
uniform mat4 detail6=mat4(1);
uniform mat4 detail7=mat4(1);
uniform mat4 detail8=mat4(1);
uniform mat4 detail9=mat4(1);
uniform mat4 detail10=mat4(1);
uniform mat4 detail11=mat4(1);
uniform mat4 detail12=mat4(1);
uniform mat4 detail13=mat4(1);
uniform mat4 detail14=mat4(1);
uniform mat4 detail15=mat4(1);
uniform mat4 detail16=mat4(1);
uniform mat4 detail17=mat4(1);
uniform mat4 detail18=mat4(1);
uniform mat4 detail19=mat4(1);
uniform mat4 detail20=mat4(1);
uniform mat4 detail21=mat4(1);
uniform mat4 detail22=mat4(1);
uniform mat4 detail23=mat4(1);
out vec4 color;
void main(){color=vec4(tint.rgb*transform[0][0]*texture(surface,vec2(0.5)).rgb*((detail0[0][0]+detail1[0][0]+detail2[0][0]+detail3[0][0]+detail4[0][0]+detail5[0][0]+detail6[0][0]+detail7[0][0]+detail8[0][0]+detail9[0][0]+detail10[0][0]+detail11[0][0]+detail12[0][0]+detail13[0][0]+detail14[0][0]+detail15[0][0]+detail16[0][0]+detail17[0][0]+detail18[0][0]+detail19[0][0]+detail20[0][0]+detail21[0][0]+detail22[0][0]+detail23[0][0])/24.0),0.125);}
)"; }
    SnapshotInputValidation(file,driver,jobs);
    std::ofstream metrics(dir/(connected?"parallel-recording-metrics-connected.csv":"parallel-recording-metrics.csv")); metrics << "repeat,mode,prepare_us,encode_us,total_us\n";
    for (int repeat=0; repeat<12; ++repeat)
    {
        std::vector<unsigned char> reference; std::string referenceTrace;
        for (int mode=0; mode<3; ++mode)
        {
            std::optional<Data::RenderCommandList> commands; baregl::Framebuffer::Snapshot output;
            double prepare=0, encode=0;
            {
                Core::CompositeRenderer renderer(driver); renderer.SetDrawRecordingEnabled(true);
                if (mode) renderer.SetCommandRecordingJobs(&jobs);
                renderer.SetParallelCommandRecordingEnabled(mode==2);
                renderer.AddFeature<EngineFeature,Features::EFeatureExecutionPolicy::ALWAYS>();
                const auto destroy=[](Resources::Shader* p){Resources::Loaders::ShaderLoader::Destroy(p);};
                std::unique_ptr<Resources::Shader,decltype(destroy)> shader(Resources::Loaders::ShaderLoader::Create(file.string()),destroy);
                Require(shader!=nullptr,"parallel shader missing"); Mat material(shader.get());
                std::array<Geometry::Vertex,3> vertices{};
                vertices[0].position[0]=-.8f;vertices[0].position[1]=-.8f;vertices[1].position[0]=.8f;vertices[1].position[1]=-.8f;vertices[2].position[1]=.8f;
                const std::array<uint32_t,3> indices{0,1,2}; Resources::Mesh mesh(vertices,indices);
                Entities::Drawable drawable; drawable.mesh=mesh;drawable.material=material;drawable.stateMask=Mask(6);
                drawable.AddDescriptor<OvCore::Rendering::EngineDrawableDescriptor>({FMatrix4::Identity,FMatrix4::Identity});
                for (unsigned pass=0;pass<4;++pass) renderer.AddPass<CommandPass>("pass"+std::to_string(pass),pass,std::function<void(Data::PipelineState)>([&,pass](auto pso){
                    for (unsigned i=0;i<192;++i)
                    {
                        if (i%3==0) material.SetProperty("tint",FVector4{float((i+pass+repeat)%11)/11,.5f,.25f,1},true);
                        if (i%17==0) material.SetProperty("transform",FMatrix4::Scaling({.5f+float(i%5)*.1f,1,1}));
                        renderer.DrawEntity(pso,drawable);
                    }
                }));
                baregl::Framebuffer target; OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,64,32,true,true,false); output=target.RecordTarget();
                Entities::Camera camera;camera.SetClearColor({0,0,0});Data::FrameDescriptor frame{.renderWidth=64,.renderHeight=32,.camera=camera,.outputBuffer=target};
                using Clock=std::chrono::steady_clock;
                { CommandHooks hooks;
                    const auto record = [&] {
                    const auto begin=Clock::now();renderer.BeginCommandRecording();renderer.BeginFrame(frame);renderer.DrawFrame();renderer.EndFrame();
                    const auto prepared=Clock::now();commands.emplace(renderer.FinishCommandRecording());const auto done=Clock::now();
                    prepare=std::chrono::duration<double,std::micro>(prepared-begin).count();encode=std::chrono::duration<double,std::micro>(done-prepared).count();
                    };
                    if(mode==0){ZoneScopedN("Stage4 Legacy Recording");record();}
                    else if(mode==1){ZoneScopedN("Stage4 Serial Recording");record();}
                    else {ZoneScopedN("Stage4 Parallel Recording");record();}
                    Require(gpuCounts.draws==0&&gpuCounts.uploads==0&&gpuCounts.allocations==0&&gpuCounts.clears==0&&gpuCounts.targets==0,"parallel recording called GL");
                }
            } // Destroy every source renderer, material, shader asset, mesh and target before playback.
            std::ostringstream trace(std::ios::binary);{PacketDrawHooks hooks(trace);commands->Execute(driver);}
            auto image=ReadTarget(output); Require(drawCounts[0]==768,"parallel draw count differs");
            if(mode==0){reference=image;referenceTrace=trace.str();}
            else Require(image==reference&&trace.str()==referenceTrace,"parallel pass pixels/order/material state differs");
            metrics<<repeat<<','<<mode<<','<<prepare<<','<<encode<<','<<prepare+encode<<'\n';
        }
    }
    Require(glGetError()==GL_NO_ERROR&&metrics.good(),"parallel recording output or GL failure");
    std::puts("PASS: 12 frames x 3 modes x 4 passes, 27648 ordered shared-material draws; exact pixels and GL state after source destruction");
}

int main(int argc,char** argv) try
{
    Require(argc>=2,"pass output directory");
    if (const auto result=CommandListValidationMain(2,argv)) return result;
    const bool capture=argc>2&&std::string_view(argv[2])=="--capture";
    if(capture)
    {
        std::puts("TRACE READY");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        while(!TracyIsConnected&&std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Require(TracyIsConnected,"Tracy did not connect");
    }
    ParallelDrawValidation(argv[1],capture); return 0;
}
catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
