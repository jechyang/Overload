#pragma warning(push)
#pragma warning(disable: 4100 4201)
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/Resources/Material.h>
#include <OvRendering/Utils/ResourceTracking.h>
#if defined(S3_EDITOR)
#include <OvEditor/Core/Editor.h>
#else
#include <OvGame/Core/Game.h>
#endif
#pragma warning(pop)
#include <cstdio>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#undef APIENTRY
#include <Windows.h>
static void Require(bool test,const char* text) {if(!test)throw std::runtime_error(text);std::printf("PASS: %s\n",text);}
#include "s4_fields_helpers.h"
using OvCore::Scripting::ScriptIdentity;
static const auto identity=ScriptIdentity::CSharp("","","s4-matrix");
int wmain(int argc,wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    try {
        Require(argc==2,"S4 application isolated project");std::setvbuf(stdout,nullptr,_IONBF,0);
        OvRendering::Utils::ResourceTracking::TrackResources();
#if defined(S3_EDITOR)
        OvEditor::Core::Context context(argv[1]);context.window->Hide();OvEditor::Core::Editor editor(context);
        context.sceneManager.LoadEmptyScene();
        auto& actor=context.sceneManager.GetCurrentScene()->CreateActor("s4-editor");const auto guid=actor.GetGUID();auto& b=actor.AddBehaviour(identity);actor.AddBehaviour("s3.lua");
        InspectorEdit(b,"7");Set(b,"U64","18446744073709551615");const auto edit=SaveFieldRecord(b);
        const auto editOrdinal=Read(b,"ConstructorOrdinal").scalar.int32_value;
        auto& actions=OvCore::Global::ServiceLocator::Get<OvEditor::Core::EditorActions>();
        auto current=[&]() -> FieldBehaviour& {return *context.sceneManager.GetCurrentScene()->FindActorByGUID(guid)->GetBehaviour(identity);};
        editor.UpdateCurrentEditorMode(0.5f);Require(actor.transform.GetLocalPosition().x==0,"Inspector creates Prepared instance without running edit-time gameplay");
        actions.StartPlaying();Require(Read(current(),"ObservedSpeed").scalar.float32_value==7,"real Editor Play restores Inspector fields before Awake");
        Require(Read(current(),"ConstructorOrdinal").scalar.int32_value==editOrdinal,"Editor Play reuses the actual Prepared object that supplied Inspector defaults");
        editor.UpdateCurrentEditorMode(0.5f);Require(current().owner.transform.GetLocalPosition().x==10.5f && Read(current(),"speed").scalar.float32_value==8,"real Editor update observes field override and mutates runtime field");
        InspectorEdit(current(),"12");editor.UpdateCurrentEditorMode(0.5f);Require(current().owner.transform.GetLocalPosition().x==16.5f,"Play Inspector edit affects CSharp next update");
        Require(SaveFieldRecord(current())==edit,"Editor Play field edits never leak into saved edit data");
        actions.StopPlaying();Require(current().PrepareManagedFields() && Read(current(),"speed").scalar.float32_value==7 && Read(current(),"U64").scalar.uint64_value==UINT64_MAX && current().owner.transform.GetLocalPosition().x==0,"real Editor Stop restores edit fields, precision and native snapshot");
        actions.StartPlaying();Require(Read(current(),"ObservedSpeed").scalar.float32_value==7 && Read(current(),"ConstructorCount").scalar.int32_value==1,"second Play uses one restored attached instance");
        actions.StopPlaying();editor.DrainRenderFrames();
#else
        OvGame::Core::Context context;context.window->Hide();OvGame::Core::Game game(context);
        auto& actor=context.sceneManager.GetCurrentScene()->CreateActor("s4-game");auto& b=actor.AddBehaviour(identity);actor.AddBehaviour("s3.lua");
        // Existing Game scene is playing; restored XML is applied before the queued first Awake.
        tinyxml2::XMLDocument doc;doc.Parse("<data><managed_fields><field id='speed' tag='10' auxiliary='0'>7</field></managed_fields></data>");b.OnDeserialize(doc,doc.RootElement());
        for(int n=0;n<2;++n){game.PreUpdate();game.Update(0.5f);game.PostUpdate();}game.DrainRenderFrames();
        Require(Read(b,"ObservedSpeed").scalar.float32_value==7 && actor.transform.GetLocalPosition().x==14.5f,"real Game restores field then runs production update phases");
        Require(SaveFieldRecord(b).find(">7<")!=std::string::npos,"real Game preserves original stored override");
        InspectorEdit(b,"12");Set(b,"U64","18446744073709551615");
        Require(Read(b,"U64").scalar.uint64_value==UINT64_MAX,"Game Host field writes retain UInt64 precision");
        Require(std::get<double>(actor.GetBehaviour("s3.lua")->GetScript()->GetProperty("ticks").value())==1,"Game Lua coexistence");
#endif
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"S4 app FAIL: %s\n",e.what());return 1;}
}
