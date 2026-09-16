#include <OvCore/Resources/Material.h>
#include <OvEditor/Core/Editor.h>
#include <OvEditor/Panels/Inspector.h>
#include <OvEditor/Panels/CSharpScripts.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Helpers/GUIHelpers.h>
#include <OvRendering/Utils/ResourceTracking.h>
#include <OvUI/Widgets/Buttons/Button.h>
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
static void Write(const fs::path& file,const std::string& text){std::ofstream f(file,std::ios::binary);f<<text;Require(bool(f),"write fixture source");}
static std::string Source(int version){return "using Overload; using Overload.Maths; namespace Workflow { [ScriptId(\"s5-first\")] public sealed class First : Behaviour {} [ScriptId(\"s5-motion\")] public sealed class Motion : Behaviour { [FieldId(\"speed\")] public float Speed=2; public int Version="+std::to_string(version)+"; public float Observed; public override void OnAwake(){Observed=Speed;} public override void OnUpdate(float dt){var p=Owner.Transform.Position;Owner.Transform.Position=new Vector3(p.X+Speed*dt,p.Y,p.Z);} } }";}
static OvUI::Widgets::Buttons::Button* Button(OvUI::Internal::WidgetContainer& root,const std::string& label){
    for(auto [w,mode]:root.GetWidgets()){(void)mode;if(auto* b=dynamic_cast<OvUI::Widgets::Buttons::Button*>(w);b&&b->label==label)return b;if(auto* container=dynamic_cast<OvUI::Internal::WidgetContainer*>(w))if(auto* b=Button(*container,label))return b;}return nullptr;
}
int wmain(int argc,wchar_t** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);std::setvbuf(stdout,nullptr,_IONBF,0);
    try {
        Require(argc==2||argc==3,"S5 isolated editor project");OvRendering::Utils::ResourceTracking::TrackResources();
        OvEditor::Core::Context context(argv[1]);context.window->Hide();OvEditor::Core::Editor editor(context);
        auto& actions=OvCore::Global::ServiceLocator::Get<OvEditor::Core::EditorActions>();auto& project=*context.csharpProject;
        auto& panels=actions.GetPanelsManager();
        auto identity=OvCore::Scripting::ScriptIdentity::CSharp("","","s5-motion");
        auto wait=[&](auto ready){const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);while(!ready()){editor.PreUpdate();if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Build timeout: "+project.Status()+"\n"+project.Diagnostics());std::this_thread::sleep_for(std::chrono::milliseconds(20));}};
        if(argc==3){
            Require(context.scriptEngine->CSharpTypes().size()==2,"reopen loads the persisted successful generation");
            context.sceneManager.LoadScene((context.projectAssetsPath/"workflow.ovscene").string(),true);
            auto* actor=context.sceneManager.GetCurrentScene()->FindActorByName("workflow");Require(actor&&actor->GetBehaviour(identity),"reopen restores saved attachment");
            Require(Read(*actor->GetBehaviour(identity),"speed").scalar.float32_value==7,"reopen restores serialized Inspector override");
            actions.StartPlaying();editor.UpdateCurrentEditorMode(0.5f);Require(actor->transform.GetLocalPosition().x==3.5f,"reopened editor plays compiled script");actions.StopPlaying();editor.DrainRenderFrames();return 0;
        }
        Require(!context.csharpHost,"new project begins without a managed host");
        const auto source=context.projectAssetsPath/fs::path(L"中文 空格")/"Many Types.cs";
        Require(project.CreateScript(source),"production new-script workflow creates project and template in Chinese space path");
        Require(fs::exists(context.projectFolder/"GameScripts.csproj"),"GameScripts project created");
        // Build the actual generated template before adding the multi-type fixture.
        wait([&]{return !project.Busy();});Require(!project.AppliedGeneration().empty(),"generated template compiles and applies");
        Write(source,Source(1));
        auto* build=Button(panels.GetPanelAs<OvEditor::Panels::CSharpScripts>("C# Scripts"),"Build / Reload C# (Debug)");
        Require(build!=nullptr,"production C# panel exposes build command");build->ClickedEvent.Invoke();wait([&]{return !project.Busy();});
        Require(context.scriptEngine->CSharpTypes().size()==2,"one source file yields two selectable metadata types");
        Require(OvTools::Utils::PathParser::GetFileType(source.string())==OvTools::Utils::PathParser::EFileType::SCRIPT,"C# source recognized as script asset");
        context.sceneManager.LoadEmptyScene();auto& actor=context.sceneManager.GetCurrentScene()->CreateActor("workflow");const auto guid=actor.GetGUID();
        auto& inspector=panels.GetPanelAs<OvEditor::Panels::Inspector>("Inspector");inspector.FocusActor(actor);
        OvCore::Helpers::GUIHelpers::PickerItemList picker;
        OvCore::Helpers::GUIHelpers::SetPickerProvider([&](auto items,std::string){picker=std::move(items);});
        auto* add=Button(inspector,"Add Component...");Require(add!=nullptr,"real Inspector add-component button exists");add->ClickedEvent.Invoke();
        bool selected=false;for(const auto& item:picker.Items())if(item.key==identity.Key()){item.onSelected();selected=true;break;}
        Require(selected&&actor.GetBehaviour(identity)&&actor.GetBehaviours().size()==1,"picker attaches the explicitly selected second type only");
        actor.AddBehaviour("s3.lua");auto current=[&]() -> FieldBehaviour& {return *context.sceneManager.GetCurrentScene()->FindActorByGUID(guid)->GetBehaviour(identity);};
        InspectorEdit(current(),"7");
        tinyxml2::XMLDocument scene;auto* root=scene.NewElement("root");scene.InsertEndChild(root);context.sceneManager.GetCurrentScene()->OnSerialize(scene,root);
        tinyxml2::XMLPrinter printer;scene.Print(&printer);Write(context.projectAssetsPath/"workflow.ovscene",printer.CStr());
        actions.StartPlaying();Require(Read(current(),"Observed").scalar.float32_value==7,"Awake sees persisted field");editor.UpdateCurrentEditorMode(0.5f);
        Require(current().owner.transform.GetLocalPosition().x==3.5f,"compiled chosen type drives native Transform");
        Require(std::get<double>(current().owner.GetBehaviour("s3.lua")->GetScript()->GetProperty("ticks").value())==0.5,"Lua continues updating beside newly compiled CSharp");
        const auto old=project.AppliedGeneration();Write(source,Source(2));project.RequestBuild();wait([&]{return !project.Busy();});
        Require(project.Pending()&&project.AppliedGeneration()==old&&Read(current(),"Version").scalar.int32_value==1,"successful Play build remains pending with old instance running");
        actions.PauseGame();editor.PreUpdate();Require(project.Pending(),"Pause is not a stopped apply point");
        const auto dependency=project.PendingGeneration()/"GameScripts.deps.json";
        std::ifstream dependencyFile(dependency,std::ios::binary);const std::string intact{std::istreambuf_iterator<char>(dependencyFile),{}};dependencyFile.close();
        auto damaged=intact;damaged[0]=damaged[0]=='{'?' ': '{';Write(dependency,damaged);
        actions.StopPlaying();editor.PreUpdate();Require(project.AppliedGeneration()==old&&Read(current(),"Version").scalar.int32_value==1,"same-size damaged dependency rejected before apply; old version retained");
        Write(dependency,intact);project.RequestBuild();wait([&]{return !project.Busy();});
        Require(!project.Pending()&&project.AppliedGeneration()!=old&&Read(current(),"Version").scalar.int32_value==2&&Read(current(),"speed").scalar.float32_value==7,"Main after Stop applies candidate with edit override restored");
        const auto good=project.AppliedGeneration();Write(source,Source(2)+" syntax error");project.RequestBuild();wait([&]{return !project.Busy();});
        Require(project.AppliedGeneration()==good&&project.Diagnostics().find(".cs(")!=std::string::npos,"syntax failure exposes source location and retains old generation");
        actions.StartPlaying();Require(actions.GetCurrentEditorMode()==OvEditor::Core::EditorActions::EEditorMode::PLAY,"old version still plays after compile error");actions.StopPlaying();
        Write(source,Source(2)+" [Overload.ScriptId(\"s5-motion\")] public sealed class Duplicate : Overload.Behaviour {}");project.RequestBuild();wait([&]{return !project.Busy();});
        Require(project.AppliedGeneration()==good&&Read(current(),"Version").scalar.int32_value==2,"successful compile with invalid type catalogue retains the old project");
        actions.StartPlaying();Require(actions.GetCurrentEditorMode()==OvEditor::Core::EditorActions::EEditorMode::PLAY,"old catalogue remains playable after failed reload validation");actions.StopPlaying();
        Write(source,Source(3));project.RequestBuild("Debug");editor.PreUpdate();project.RequestBuild("Debug");project.RequestBuild("Release");wait([&]{return !project.Busy();});
        Require(project.Status().find("Release")!=std::string::npos&&Read(current(),"Version").scalar.int32_value==3,"concurrent requests coalesce to latest configuration");
        const auto release=project.AppliedGeneration();project.RequestBuild("Release");wait([&]{return !project.Busy();});Require(project.AppliedGeneration()==release,"unchanged build does not replace loaded generation");
        fs::create_directories(context.projectAssetsPath/"obj");Write(context.projectAssetsPath/"obj/noise.cs","invalid output ignored");
        const auto start=std::chrono::steady_clock::now();while(std::chrono::steady_clock::now()-start<std::chrono::seconds(2)){editor.PreUpdate();std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        Require(!project.Busy()&&project.AppliedGeneration()==release,"watch excludes output directories and never loops on generated output");
        Write(source,Source(4));wait([&]{return project.Busy();});wait([&]{return !project.Busy();});Require(Read(current(),"Version").scalar.int32_value==4,"debounced source watcher builds and applies");
        bool rejected=false;std::thread wrong([&]{try{project.Tick(true);}catch(const std::logic_error&){rejected=true;}});wrong.join();Require(rejected,"non-Main apply rejected");
        editor.PreUpdate();editor.Update(0.016f);editor.PostUpdate();editor.DrainRenderFrames();
        OvCore::Helpers::GUIHelpers::SetPickerProvider({});
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"S5 FAIL: %s\n",e.what());return 1;}
}
