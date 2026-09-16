#pragma warning(push)
#pragma warning(disable: 4100 4201)
#include <OvCore/Scripting/CSharp/RuntimeHost.h>
#include "OvCore/Reflection.generated.h"
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/SceneSystem/SceneManager.h>
#include <OvCore/ECS/Components/Behaviour.h>
#include <OvCore/ECS/Components/CPhysicalBox.h>
#include <OvPhysics/Core/PhysicsEngine.h>
#include <OvTools/Jobs/JobSystem.h>
#include <tinyxml2.h>
#pragma warning(pop)
#include <chrono>
#include <fstream>
#include <cstdio>
using namespace OvCore::Scripting;
using namespace OvCore::Scripting::CSharp;
struct Metrics {uint64_t allocated,native,lifecycle;uint32_t gen0,gen1,gen2,reserved;};
static_assert(sizeof(Metrics)==40);
using ProbeMetrics=OvCsStatus(OVCS_CALL*)(uint32_t,Metrics*);
static void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv){
    try {
        Require(argc==5,"headless usage: runtime managed project raw-jsonl");
        const std::filesystem::path project=argv[3];
        OvReflection::MainThread main;RuntimeHost host(main);host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(),argv[1],argv[2]));
        ScriptEngine scripts(project,project);OvCore::Global::ServiceLocator::Provide(scripts);
        OvPhysics::Core::PhysicsEngine physics{OvPhysics::Settings::PhysicsSettings{}};
        OvCore::SceneSystem::SceneManager scenes(project.string(),project.string());OvCore::Global::ServiceLocator::Provide(scenes);
        OvTools::Jobs::JobSystem jobs(2);
        Require(scripts.LoadCSharpProject(host,project,project/"GameScripts.dll","GameScripts"),"load S7 project and bind real R6 ABI");
        auto metrics=reinterpret_cast<ProbeMetrics>(host.Entry(L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost",L"GetRuntimeMetrics"));
        // Serialize the actual production scene for the packaged OvGame smoke.
        scenes.LoadEmptyScene();auto& actor=scenes.GetCurrentScene()->CreateActor("S7");actor.AddComponent<OvCore::ECS::Components::CPhysicalBox>().SetMass(0);
        auto& b=actor.AddBehaviour(ScriptIdentity::CSharp("","","s7-probe"));Require(b.PrepareManagedFields(),"prepare package fixture");
        ManagedFieldValue speed{};speed.tag=OVCS_FIELD_FLOAT32;speed.scalar.float32_value=7;Require(b.EditManagedField("speed",speed),"persist speed override");
        actor.AddBehaviour("s3.lua");
        tinyxml2::XMLDocument xml;auto* node=xml.NewElement("root");xml.InsertEndChild(node);scenes.GetCurrentScene()->OnSerialize(xml,node);
        tinyxml2::XMLPrinter printer;xml.Print(&printer);std::ofstream(project/"sample.ovscene",std::ios::binary)<<printer.CStr();scenes.UnloadCurrentScene();
        std::ofstream output(std::filesystem::path(argv[4]),std::ios::binary);Require(bool(output),"open raw performance evidence");
        for(const bool move:{false,true})for(const int count:{1,100,1000}){
            scenes.LoadEmptyScene();auto& scene=*scenes.GetCurrentScene();
            for(int i=0;i<count;++i)scene.CreateActor("bench").AddBehaviour(ScriptIdentity::CSharp("GameScripts",move?"S7.Move":"S7.Noop"));
            scene.Play();constexpr float dt=1.0f/64;int warmup=0;
            const auto warmStart=std::chrono::steady_clock::now();
            do{scene.Update(dt,jobs);++warmup;}while(warmup<500||std::chrono::steady_clock::now()-warmStart<std::chrono::seconds(1));
            output<<"{\"kind\":\"warmup\",\"workload\":\""<<(move?"transform":"noop")<<"\",\"count\":"<<count<<",\"frames\":"<<warmup<<",\"minimum_seconds\":1}\n";
            for(int frame=0;frame<500;++frame){
                Metrics before{},after{};host.Check(metrics(sizeof(before),&before),"metrics before");
                const auto start=std::chrono::steady_clock::now();scene.Update(dt,jobs);const auto end=std::chrono::steady_clock::now();
                host.Check(metrics(sizeof(after),&after),"metrics after");
                Require(after.lifecycle-before.lifecycle==static_cast<uint64_t>(count),"exact per-frame lifecycle ABI count");
                Require(after.native-before.native==static_cast<uint64_t>(move?4*count:0),"exact gameplay/R6 call count");
                output<<"{\"kind\":\"sample\",\"workload\":\""<<(move?"transform":"noop")<<"\",\"count\":"<<count<<",\"frame\":"<<frame
                    <<",\"nanoseconds\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()<<",\"allocated_bytes\":"<<after.allocated-before.allocated
                    <<",\"native_to_managed\":"<<after.lifecycle-before.lifecycle<<",\"managed_to_native\":"<<after.native-before.native
                    <<",\"gen0\":"<<after.gen0-before.gen0<<",\"gen1\":"<<after.gen1-before.gen1<<",\"gen2\":"<<after.gen2-before.gen2<<"}\n";
            }
            for(auto* item:scene.GetActors())Require(item->transform.GetLocalPosition().x==(move?(warmup+500)*dt:0),"all actual Transform results match dispatch count");
            scenes.UnloadCurrentScene();
        }
        scripts.ClearCSharpProject();uint64_t alive=0,collected=0;
        auto collect=reinterpret_cast<OvCsCollectibleStatsFn>(host.Entry(L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost",L"CollectibleStats"));
        host.Check(collect(1,&alive,&collected),"headless collectible proof");Require(alive==0,"headless project collection");
        host.Shutdown();std::puts("S7 HEADLESS PASS: real lifecycle/Transform/ABI/GC, no window or GPU created");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"S7 HEADLESS FAIL: %s\n",e.what());return 1;}
}
