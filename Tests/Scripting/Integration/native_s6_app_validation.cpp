#include <OvCore/Resources/Material.h>
#include <OvEditor/Core/Editor.h>
#include <OvEditor/Panels/Inspector.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvRendering/Utils/ResourceTracking.h>
#include <OvCore/Scripting/CSharp/RuntimeHost.h>
#include <OvCore/Scripting/ScriptBridge.h>
#include "OvCore/Reflection.generated.h"
#include <fstream>
#include <thread>
#include <cstdio>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#undef APIENTRY
#include <Windows.h>
static void Require(bool test,const char* message){if(!test)throw std::runtime_error(message);std::printf("PASS: %s\n",message);}
#include "s4_fields_helpers.h"
namespace fs=std::filesystem;
static void Write(const fs::path& file,const std::string& text){std::ofstream f(file,std::ios::binary);f<<text;if(!f)throw std::runtime_error("write fixture");}
static std::string Source(bool incompatible=false,bool fail=false,bool leak=false){
    return std::string(R"CS(using System; using Overload; using Overload.Maths;
namespace ReloadTest {
sealed class Lease : IDisposable {
    EventHandler handler;
    public Lease(Behaviour value) { handler = (s,e) => GC.KeepAlive(value); AppDomain.CurrentDomain.ProcessExit += handler; }
    public void Dispose() { AppDomain.CurrentDomain.ProcessExit -= handler; }
}
[ScriptId("reload-motion")] public sealed class Motion : Behaviour {
    public )CS")+(incompatible?"string Count=\"bad\";":"int Count;")+R"CS(
    public int AwakeCalls, StartCalls, Restores, Code;
    public static int StaticCounter; public object Graph=new object();
    [FieldId("speed")] public float RenamedSpeed=2;
    public string Text="中文 field"; public ulong Wide=18446744073709551615;
    public ActorRef Target; public AssetRef Asset;
    void Subscribe(){ )CS"+(leak?"_ = new Lease(this);":"TrackSubscription(new Lease(this));")+R"CS( }
    public override void OnAwake(){AwakeCalls++; StaticCounter++; Subscribe();}
    public override void OnStart(){StartCalls++;}
    public override void OnUpdate(float dt){)CS"+(incompatible?"":"Count++;")+R"CS(
        Code=Helper.Version.Value; var p=Transform.Position; Transform.Position=new Vector3(p.X+Code*dt,p.Y,p.Z);
    }
    public override void OnReloadRelease(){var p=Transform.Position;Transform.Position=new Vector3(p.X,p.Y+1,p.Z);}
    public override void OnReloadRestore(){Restores++;Code=++StaticCounter;Subscribe();)CS"+(fail?"var p=Transform.Position;Transform.Position=new Vector3(p.X,p.Y,99);throw new Exception(\"restore failed after native effect\");":"")+R"CS(}
}
[ScriptId("reload-peer")] public sealed class Peer : Behaviour {public int Ticks;public override void OnUpdate(float dt){Ticks++;}}
})CS";
}
int wmain(int argc,wchar_t** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);std::setvbuf(stdout,nullptr,_IONBF,0);
    try {
        Require(argc==2||argc==3||(argc==4&&std::wstring(argv[2])==L"--cycles"),"S6 isolated editor project");const bool leak=argc==3;
        const int cycles=argc==4?std::stoi(argv[3]):20;Require(cycles>=1&&cycles<=20,"bounded development cycle count");
        OvRendering::Utils::ResourceTracking::TrackResources();
        OvEditor::Core::Context context(argv[1]);context.window->Hide();OvEditor::Core::Editor editor(context);
        auto& actions=OvCore::Global::ServiceLocator::Get<OvEditor::Core::EditorActions>();auto& project=*context.csharpProject;
        project.watch=false;project.hotReload=true;
        auto wait=[&]{const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);do{editor.PreUpdate();if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Build timeout: "+project.Diagnostics());std::this_thread::sleep_for(std::chrono::milliseconds(20));}while(project.Busy());};
        const auto source=context.projectAssetsPath/"Reload.cs";
        Require(project.CreateScript(source),"create production project");wait();
        fs::create_directories(context.projectFolder/"Helper");
        Write(context.projectFolder/"Helper/Helper.csproj",R"XML(<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework><PlatformTarget>x64</PlatformTarget><RestorePackagesWithLockFile>true</RestorePackagesWithLockFile></PropertyGroup></Project>)XML");
        Write(context.projectFolder/"Helper/packages.lock.json",R"JSON({"version":1,"dependencies":{"net10.0":{}}})JSON");
        const auto csproj=context.projectFolder/"GameScripts.csproj";
        std::ifstream input(csproj);std::string xml{std::istreambuf_iterator<char>(input),{}};input.close();
        xml.insert(xml.find("</ItemGroup>"),"<ProjectReference Include=\"Helper/Helper.csproj\" />");Write(csproj,xml);
        Write(context.projectFolder/"packages.lock.json",R"JSON({"version":1,"dependencies":{"net10.0":{"overload.scriptcore":{"type":"Project"},"helper":{"type":"Project"}}}})JSON");
        auto build=[&](int version,bool incompatible=false,bool fail=false){
            Write(context.projectFolder/"Helper/Version.cs","namespace Helper; public static class Version { public static int Value => "+std::to_string(version)+"; }");
            auto code=Source(incompatible,fail,leak); if(version%2==0) code.replace(code.find("RenamedSpeed"),12,"Speed");
            Write(source,code);project.RequestBuild();wait();
        };
        build(1);Require(project.Diagnostics().find("error ")==std::string::npos,"project with private dependency builds");
        context.sceneManager.LoadEmptyScene();auto& actor=context.sceneManager.GetCurrentScene()->CreateActor("reload");const auto guid=actor.GetGUID();
        const auto id=OvCore::Scripting::ScriptIdentity::CSharp("","","reload-motion"),peer=OvCore::Scripting::ScriptIdentity::CSharp("","","reload-peer");
        actor.AddBehaviour(id);actor.AddBehaviour(peer);actor.AddBehaviour("s3.lua");
        auto current=[&]() -> FieldBehaviour& {return *context.sceneManager.GetCurrentScene()->FindActorByGUID(guid)->GetBehaviour(id);};
        auto integer=[&](const char* name){return Read(current(),name).scalar.int32_value;};
        auto& inspector=actions.GetPanelsManager().GetPanelAs<OvEditor::Panels::Inspector>("Inspector");inspector.FocusActor(actor);
        Require(current().PrepareManagedFields(),"prepare original attachment");Set(current(),"speed","5");
        const auto stats=reinterpret_cast<OvCsCollectibleStatsFn>(context.csharpHost->Entry(L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost",L"CollectibleStats"));
        uint64_t alive=0,collected=0;
        for(int cycle=1;cycle<=(leak?1:cycles);++cycle){
            actions.StartPlaying();Require(integer("AwakeCalls")==1&&integer("StartCalls")==1,"Play initializes once");
            Set(current(),"speed","7");
            OvUI::Widgets::Layout::Group controls;current().OnInspector(controls);
            OvUI::Widgets::InputFields::InputText* speed=nullptr;
            for(auto [widget,mode]:controls.GetWidgets()){(void)mode;if(auto* input=dynamic_cast<OvUI::Widgets::InputFields::InputText*>(widget);input&&input->label=="##speed")speed=input;}
            Require(speed!=nullptr,"real Inspector field callback captured");auto staleUi=speed->ContentChangedEvent;
            editor.UpdateCurrentEditorMode(0.125f);const auto before=integer("Count");
            const auto session=context.scriptEngine->CSharpSession();const auto old=project.AppliedGeneration();
            auto& host=*context.csharpHost;const auto& api=host.Api();OvCsTypeInfo type{};
            host.Check(api.get_type_info(session,0,&type),"stale fixture type",session);
            const auto anchor=OvCore::Scripting::PublishComponent(host.NativeContext(),current().owner.transform);
            OvCsInstanceCreateInfo info{sizeof(info),0,type.type_token,host.NativeContext().Id(),anchor,guid};
            OvCsInstanceHandle staleInstance{};host.Check(api.create_instance(session,&info,&staleInstance),"stale fixture instance",session);
            host.Check(api.release_type_info(&type),"release stale fixture type");
            build(cycle+1);Require(project.AppliedGeneration()!=old&&!project.Pending(),"runtime candidate committed at Main safe point");
            Require(integer("Count")==before&&integer("AwakeCalls")==1&&integer("StartCalls")==1&&integer("Restores")==1,"supported fields migrate without repeating Awake or Start");
            Require(integer("Code")==1,"static state is fresh in each generation");
            Require(Read(current(),"Wide").scalar.uint64_value==UINT64_MAX&&Read(current(),"Text").text=="中文 field","exact wide integer and UTF8 field migration");
            Require(current().ManagedDefault("speed")&&current().ManagedDefault("speed")->scalar.float32_value==2,"new constructor defaults remain separate from migrated runtime values");
            staleUi.Invoke("999");Require(Read(current(),"speed").scalar.float32_value==7,"renamed stable field migrates and old UI callback is invalid");
            uint32_t count=0;Require(context.csharpHost->Api().get_type_count(session,&count)==OVCS_STATUS_SESSION_NOT_FOUND,"old session handle rejected after commit");
            OvCsLifecycleCall staleCall{sizeof(staleCall),OVCS_LIFECYCLE_AWAKE,staleInstance,0,0,0};
            Require(api.dispatch_lifecycle(session,&staleCall)==OVCS_STATUS_SESSION_NOT_FOUND,"old instance and session rejected");
            Require(api.dispatch_lifecycle(context.scriptEngine->CSharpSession(),&staleCall)==OVCS_STATUS_INSTANCE_NOT_FOUND,"old instance rejected in new session");
            const auto position=current().owner.transform.GetLocalPosition();editor.UpdateCurrentEditorMode(0.125f);
            Require(integer("Code")==cycle+1&&current().owner.transform.GetLocalPosition().x==position.x+(cycle+1)*0.125f,"private dependency runs from new collectible generation");
            context.csharpHost->Check(stats(1,&alive,&collected),"collectible diagnostic");
            std::printf("ALC cycle=%d alive=%llu collected=%llu\n",cycle,(unsigned long long)alive,(unsigned long long)collected);
            Require(leak?alive>0:alive==0,leak?"untracked external event root reported retained":"WeakReference confirms all retired contexts collected including event subscribers and type caches");
            actions.StopPlaying();Require(integer("Count")==0&&Read(current(),"speed").scalar.float32_value==5,"Stop restores edit values rather than runtime snapshot");
            editor.PreUpdate();editor.Update(0.016f);editor.PostUpdate();editor.DrainRenderFrames();
        }
        if(leak)return 0;
        actions.StartPlaying();editor.UpdateCurrentEditorMode(0.125f);const auto good=project.AppliedGeneration();const int previous=integer("Count");
        build(100,true);Require(project.AppliedGeneration()==good&&integer("Count")==previous,"incompatible field preflight preserves old live generation");
        Write(source,Source()+" syntax error");project.RequestBuild();wait();Require(project.AppliedGeneration()==good,"compile failure preserves old generation");
        build(101,false,true);Require(project.AppliedGeneration()!=good&&current().GetScript()->IsFaulted()&&current().owner.transform.GetLocalPosition().z==99,"post-commit restore exception faults new instance and preserves actual native side effect");
        auto& peerBehaviour=*current().owner.GetBehaviour(peer);const int ticks=Read(peerBehaviour,"Ticks").scalar.int32_value;
        editor.UpdateCurrentEditorMode(0.125f);Require(Read(peerBehaviour,"Ticks").scalar.int32_value==ticks+1,"restore exception leaves peer dispatch operational");
        build(102);Require(!current().GetScript()->IsFaulted(),"later valid reload recovers faulted instance");
        actions.StopPlaying();context.scriptEngine->ClearCSharpProject();
        context.csharpHost->Check(stats(1,&alive,&collected),"final collectible diagnostic");Require(alive==0&&collected>=static_cast<uint64_t>(cycles+3),"WeakReference proves normal and rejected generations reclaimed");
        std::printf("S6 RESULT cycles=%d collected=%llu alive=0\n",cycles,(unsigned long long)collected);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"S6 FAIL: %s\n",e.what());return 1;}
}
