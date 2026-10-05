#pragma warning(push)
// Prepared render data deliberately embeds aligned GPU constants.
#pragma warning(disable: 4324)
#define wmain S1ValidationMain
#include "native_host_validation.cpp"
#undef wmain
#pragma warning(push)
#pragma warning(disable: 4100 4201)
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/SceneSystem/PrefabOperations.h>
#pragma warning(pop)
#include <fstream>
#include "s4_fields_helpers.h"
#include "s4_collection_ui_validation.h"
#pragma warning(pop)
using OvCore::Scripting::ScriptIdentity;
static std::string PathUtf8(const std::filesystem::path& p) { const auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()}; }
static const auto identity=ScriptIdentity::CSharp("","","s4-matrix");
static void AbiFields(RuntimeHost& host,const std::filesystem::path& project,Engine& engine,OvCore::SceneSystem::Scene& scene) {
    const auto root=PathUtf8(project),file=PathUtf8(project/"S4.dll");const std::string logical="S4",module="OvCore";
    const auto& api=host.Api();OvCsSessionHandle session{};
    OvCsSessionCreateInfo create{sizeof(create),0,0,View(root),View(file),View(logical)};
    host.Check(api.create_session(&create,&session),"field ABI session");
    uint8_t signature[32]{};const std::string hex=OV_SCRIPT_OVCORE_SIGNATURE;
    for(size_t i=0;i<32;++i)signature[i]=static_cast<uint8_t>(std::stoul(hex.substr(i*2,2),nullptr,16));
    host.Check(api.bind_native_api(session,View(module),1,sizeof(OvScriptOvCoreTable),signature,reinterpret_cast<uintptr_t>(&OvScript_GetOvCoreApi)),"field ABI R6",session);
    OvCsTypeInfo type{};host.Check(api.get_type_info(session,0,&type),"field ABI type",session);
    const auto token=type.type_token; Require(type.field_count>=30,"metadata exposes complete supported matrix");
    uint64_t wide=0,narrowEnum=0,textToken=0,nullToken=0;
    for(uint32_t n=0;n<type.field_count;++n) {
        OvCsFieldInfo f{};host.Check(api.get_field_info(session,token,n,&f),"field ABI info",session);
        const std::string name(reinterpret_cast<const char*>(f.managed_name.data),static_cast<size_t>(f.managed_name.length));
        if(name=="Speed") Require(std::string(reinterpret_cast<const char*>(f.display_name.data),static_cast<size_t>(f.display_name.length))=="Movement Speed","InspectorName changes presentation without changing serialized identity");
        if(n==0) Require(name=="Bool","Inspector metadata follows field declaration order");
        if(name=="U64")wide=f.field_token;if(name=="SignedEnum")narrowEnum=f.field_token;
        if(name=="Text")textToken=f.field_token;if(name=="NullText")nullToken=f.field_token;
        Require(name!="Property" && name!="Readonly" && name!="Static" && name!="Unsupported" && name!="Excluded","field eligibility excludes properties/static/readonly/nonserialized/unsupported");
        auto copied=f;host.Check(api.release_field_info(&f),"release metadata");
        Require(!f.field_token && api.release_field_info(&copied)==OVCS_STATUS_BUFFER_OWNERSHIP,"field metadata ownership and zeroing");
    }
    host.Check(api.release_type_info(&type),"release type");
    auto& actor=scene.CreateActor("abi-fields");
    OvCsInstanceCreateInfo info{sizeof(info),0,token,host.NativeContext().Id(),OvCore::Scripting::PublishComponent(host.NativeContext(),actor.transform),actor.GetGUID()};
    OvCsInstanceHandle instance{};host.Check(api.create_instance(session,&info,&instance),"field ABI instance",session);
    auto value=FieldValue::Parse(9,0,"18446744073709551615")->View();
    host.Check(api.write_field(session,instance,wide,&value),"write UInt64.MaxValue",session);
    OvCsOwnedFieldValue result{};host.Check(api.read_field(session,instance,wide,&result),"read UInt64.MaxValue",session);
    Require(result.scalar.uint64_value==UINT64_MAX,"ABI retains every UInt64 bit");auto copied=result;
    host.Check(api.release_field_value(&result),"release value");Require(!result.struct_size && api.release_field_value(&copied)==OVCS_STATUS_BUFFER_OWNERSHIP,"scalar owned receipt rejects copied release");
    value.tag=8;Require(api.write_field(session,instance,wide,&value)==OVCS_STATUS_FIELD_TYPE_MISMATCH,"ABI rejects mismatched scalar tag");
    value.tag=13;value.auxiliary=2;value.scalar.int64_value=128;
    Require(api.write_field(session,instance,narrowEnum,&value)==OVCS_STATUS_INVALID_ARGUMENT,"ABI checks exact enum underlying range");
    value.tag=12;value.auxiliary=0;const uint8_t invalidUtf8[]={0xc0,0xaf};value.text={invalidUtf8,2};
    Require(api.write_field(session,instance,textToken,&value)==OVCS_STATUS_MANAGED_EXCEPTION,"strict UTF8 rejects malformed input through Host error boundary");
    Require(api.read_field(session,instance,nullToken,&result)==OVCS_STATUS_UNSUPPORTED_FIELD,"ABI reports null string unsupported without guessing a default");
    auto stale=instance;++stale.session_generation;Require(api.read_field(session,stale,wide,&result)==OVCS_STATUS_INSTANCE_NOT_FOUND,"field API rejects stale instance generation");
    Require(api.read_field(session,instance,UINT64_MAX,&result)==OVCS_STATUS_FIELD_NOT_FOUND,"field token checked against instance type");
    OvCsStatus status=0;std::thread worker([&]{status=api.read_field(session,instance,wide,&result);});worker.join();Require(status==OVCS_STATUS_WRONG_THREAD,"field reads reject worker");
    if(engine.render)Require(engine.render->Invoke([&](auto&){return api.read_field(session,instance,wide,&result);})==OVCS_STATUS_WRONG_THREAD,"field reads reject RenderThread");
    host.Check(api.destroy_instance(session,instance),"destroy Prepared without gameplay",session);
    host.Check(api.close_session(session),"close fields ABI",session);
}
int wmain(int argc,wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    try {
        Require(argc==5,"S4 arguments");std::setvbuf(stdout,nullptr,_IONBF,0);
        const std::filesystem::path project=argv[3];Require(GetModuleHandleW(L"coreclr.dll")==nullptr,"S4 native launcher begins without CLR");
        OvReflection::MainThread main;RuntimeHost host(main);host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(),argv[1],argv[2]));
        OvCore::Scripting::ScriptEngine scripts(project,project);OvCore::Global::ServiceLocator::Provide(scripts);
        Engine engine(std::wstring(argv[4])==L"render-thread");
        OvCore::SceneSystem::SceneManager scenes(project.string());OvCore::Global::ServiceLocator::Provide(scenes);
        auto& scene=*scenes.GetCurrentScene();
        Require(scripts.LoadCSharpProject(host,project,project/"S4.dll","S4"),"load newly compiled field assembly");
        AbiFields(host,project,engine,scene);
        auto& actor=scene.CreateActor("matrix");auto& b=actor.AddBehaviour(identity);
        Require(!b.GetScript()->IsValid(),"type attachment does not construct temporary defaults instance");
        Require(b.PrepareManagedFields() && Read(b,"ConstructorCount").scalar.int32_value==1,"single attached Prepared instance supplies defaults");
        Require(Read(b,"secret").scalar.int32_value==17 && Read(b,"inherited").scalar.int32_value==31,"annotated private and inherited fields exposed");
        const std::vector<std::pair<std::string,std::vector<std::string>>> bounds={
            {"Bool",{"false","true"}},{"I8",{"-128","127"}},{"U8",{"0","255"}},
            {"I16",{"-32768","32767"}},{"U16",{"0","65535"}},
            {"I32",{"-2147483648","2147483647"}},{"U32",{"0","4294967295"}},
            {"I64",{"-9223372036854775808","9223372036854775807"}},{"U64",{"0","18446744073709551615"}},
            {"SignedEnum",{"-128","127"}},{"UnsignedEnum",{"0","18446744073709551615"}},
            {"F32",{"1e-45","-0","1.1754944e-38","3.4028235e+38"}},{"F64",{"5e-324","-0","2.2250738585072014e-308","1.7976931348623157e+308"}},
            {"Vector",{"-1.25,2.5,3.75"}},{"Rotation",{"0,0,0,1"}}};
        for(const auto& [id,values]:bounds)for(const auto& text:values) { Set(b,id,text);Require(Read(b,id).Display()==text,"boundary value roundtrips native-to-managed exactly"); }
        Require(!FieldValue::Parse(9,0,"18446744073709551616") && !FieldValue::Parse(8,0,"-9223372036854775809") && !FieldValue::Parse(2,0,"128"),"integer overflow rejected before write");
        const std::string unicode=std::string(reinterpret_cast<const char*>(u8"\u4e2d\u6587<&>"))+std::string("\0end",4);
        Set(b,"Text",unicode);Require(Read(b,"Text").text==unicode,"strict UTF8 string including embedded NUL survives ABI");
        InspectorEdit(b,"7");Require(b.ManagedDefault("speed")->scalar.float32_value==2,"constructor default stays separate from Inspector override");
        Require(b.SetManagedFieldOverride("speed",false) && Read(b,"speed").scalar.float32_value==2 && !b.HasManagedFieldOverride("speed"),"reset restores default and clears explicit override");
        InspectorEdit(b,"7");Require(Read(b,"ConstructorOrdinal").scalar.int32_value==1,"repeated Inspector/default reads never construct extra instances");
        ValidateManagedListUI(b,engine,project.parent_path());
        Set(b,"RenameMe","123");Set(b,"ChangeMe","456");Set(b,"DeleteMe","9223372036854775807");
        auto& child=scene.CreateActor("internal");child.SetParent(actor);auto& external=scene.CreateActor("external");
        Set(b,"Target",std::to_string(child.GetGUID()));Set(b,"External",std::to_string(external.GetGUID()));
        FieldValue asset;asset.tag=18;asset.auxiliary=9;asset.text="./asset.ovprefab";
        std::ofstream(project/"asset.ovprefab")<<"<root/>";Require(b.EditManagedField("Asset",asset),"asset descriptor written");
        asset.text="../outside.ovprefab";Require(!b.EditManagedField("Asset",asset),"asset traversal rejected without replacing field");
        const auto editRecord=SaveFieldRecord(b);Require(editRecord.find("18446744073709551615")!=std::string::npos && editRecord.find("utf8-hex")!=std::string::npos,"save stores exact integers and XML-safe string bytes");
        Require(OvCore::SceneSystem::PrefabOperations::SaveToFile(actor,project/"matrix.ovprefab"),"save actual prefab hierarchy");
        auto* clone=scene.InstantiatePrefab("matrix.ovprefab");Require(clone && clone->GetChildren().size()==1,"instantiate prefab creates full hierarchy");
        auto& cb=*clone->GetBehaviour(identity);Require(cb.PrepareManagedFields(),"prepare remapped prefab");
        Require(Read(cb,"Target").scalar.actor_guid==clone->GetChildren()[0]->GetGUID() && Read(cb,"Target").scalar.actor_guid!=child.GetGUID() && Read(cb,"External").scalar.actor_guid==external.GetGUID(),"prefab internal references remap; external scene references retained");
        Require(Read(cb,"Text").text==unicode,"prefab string roundtrip retains UTF8 and NUL");
        scene.Play();Require(Read(b,"ObservedSpeed").scalar.float32_value==7 && actor.transform.GetLocalPosition().x==7,"stored fields restored before Awake changes native Transform");
        Require(Read(cb,"ObservedTarget").scalar.uint64_value==clone->GetChildren()[0]->GetGUID() && Read(cb,"ObservedExternal").scalar.uint64_value==external.GetGUID() && Read(cb,"ObservedAsset").scalar.boolean,"Awake resolves prefab and asset identities in actual scene");
        scene.Update(0.5f);Require(actor.transform.GetLocalPosition().x==10.5f && Read(b,"speed").scalar.float32_value==8,"Inspector override affects real CSharp Update");
        Require(SaveFieldRecord(b)==editRecord,"Play field mutations do not rewrite edit-time serialization");
        auto* runtimeClone=scene.InstantiatePrefab("matrix.ovprefab");Require(runtimeClone && Read(*runtimeClone->GetBehaviour(identity),"ObservedTarget").scalar.uint64_value==runtimeClone->GetChildren()[0]->GetGUID(),"runtime prefab remaps and restores entire batch before Awake");
        const auto clonedTarget=clone->GetChildren()[0]->GetGUID();scene.DestroyActor(*clone->GetChildren()[0]);scene.Update(0);
        Require(Read(cb,"Target").scalar.actor_guid==clonedTarget && Read(cb,"ObservedTarget").scalar.uint64_value==0,"deleted ActorRef stays persistent but resolves missing");
        // Reopen the saved scene in edit mode for schema evolution, without S6 runtime migration.
        tinyxml2::XMLDocument saved;auto* root=saved.NewElement("root");saved.InsertEndChild(root);scene.OnSerialize(saved,root);
        const auto guid=actor.GetGUID();Require(scenes.LoadSceneFromMemory(saved),"scene reload returns to edit state");
        auto* restored=scenes.GetCurrentScene()->FindActorByGUID(guid);auto* rb=restored->GetBehaviour(identity);
        Require(rb && rb->PrepareManagedFields() && Read(*rb,"Text").text==unicode,"real scene roundtrip restores managed field bytes");
        Require(scripts.LoadCSharpProject(host,project,project/"S4Renamed.dll","S4"),"load separately compiled schema evolution fixture");
        Require(rb->PrepareManagedFields() && Read(*rb,"Renamed").scalar.int32_value==123 && Read(*rb,"ChangeMe").text=="new type default","renamed fields restored by alias and incompatible type uses actual default");
        auto migrated=SaveFieldRecord(*rb);Require(migrated.find("id=\"Renamed\"")!=std::string::npos && migrated.find("id=\"DeleteMe\"")!=std::string::npos && migrated.find(">456<")!=std::string::npos,"rename writes current ID; deleted and type-changed records retained");
        Require(!scripts.LoadCSharpProject(host,project,project/"S4Duplicate.dll","S4"),"duplicate field ID fails discovery and retains prior project");
        scripts.ClearCSharpProject();std::filesystem::rename(project/"S4Renamed.dll",project/"S4Renamed.missing");
        Require(!scripts.LoadCSharpProject(host,project,project/"S4Renamed.dll","S4"),"actual absent assembly rejected");
        const auto missing=SaveFieldRecord(*rb);
        auto* missingPrefab=scenes.GetCurrentScene()->InstantiatePrefab("matrix.ovprefab");
        Require(missingPrefab && missingPrefab->GetChildren().size()==1,"missing-assembly prefab still instantiates native hierarchy");
        const auto missingPrefabTarget=missingPrefab->GetChildren()[0]->GetGUID();
        tinyxml2::XMLDocument opaque;opaque.Parse(missing.c_str());auto& reopened=scenes.GetCurrentScene()->CreateActor("missing-roundtrip").AddBehaviour(identity);reopened.DeserializeRecord(opaque,opaque.RootElement());
        Require(SaveFieldRecord(reopened)==missing,"missing assembly roundtrip preserves complete field record");
        std::filesystem::rename(project/"S4Renamed.missing",project/"S4Renamed.dll");
        Require(scripts.LoadCSharpProject(host,project,project/"S4.dll","S4") && reopened.PrepareManagedFields(),"restore original assembly recovers deleted/type-changed fields");
        Require(Read(reopened,"ChangeMe").scalar.int32_value==456 && Read(reopened,"DeleteMe").scalar.int64_value==INT64_MAX && Read(reopened,"U64").scalar.uint64_value==UINT64_MAX,"opaque values recover without precision loss");
        Require(Read(*missingPrefab->GetBehaviour(identity),"Target").scalar.actor_guid==missingPrefabTarget,"missing-assembly prefab remaps raw ActorRef before assembly recovery");
        // Invalid/unknown/duplicate records must survive; valid old-name metadata migrates independently.
        auto& unknown=scenes.GetCurrentScene()->CreateActor("unknown").AddBehaviour(identity);tinyxml2::XMLDocument raw;
        raw.Parse("<data extra='kept'><managed_fields><!--keep--><field id='OldSpeed' tag='10' auxiliary='0' future='yes'>6<extra/></field><field id='I8' tag='2' auxiliary='0'>999</field><field id='future' tag='99' auxiliary='0'><nested/></field><field id='U8' tag='3' auxiliary='0'>1</field><field id='U8' tag='3' auxiliary='0'>2</field></managed_fields></data>");
        unknown.OnDeserialize(raw,raw.RootElement());Require(unknown.PrepareManagedFields(),"prepare unknown-field fixture");
        Require(Read(unknown,"speed").scalar.float32_value==6 && Read(unknown,"I8").scalar.int8_value==0,"invalid range rejected while old-name field restores");
        auto preserved=SaveFieldRecord(unknown);Require(preserved.find("999")!=std::string::npos && preserved.find("nested")!=std::string::npos && preserved.find("extra")!=std::string::npos && preserved.find("keep")!=std::string::npos && preserved.find(">1<")!=std::string::npos && preserved.find(">2<")!=std::string::npos,"unknown XML extensions and ambiguous records survive save");
        OvUI::Widgets::Layout::Group inspector;unknown.OnInspector(inspector);
        OvTools::Eventing::Event<FieldValue> staleEdit;
        for(auto [widget,mode]:inspector.GetWidgets()) {
            (void)mode;auto* input=dynamic_cast<OvCore::Helpers::ManagedFieldInput*>(widget);
            if(input && input->fieldId=="speed")staleEdit=input->ValueChangedEvent;
        }
        Require(staleEdit.GetListenerCount()==1,"retained real Inspector edit callback");
        const auto uiSaved=SaveFieldRecord(unknown);
        scripts.ClearCSharpProject();staleEdit.Invoke(*FieldValue::Parse(10,0,"999"));
        Require(SaveFieldRecord(unknown)==uiSaved,"project removal invalidates field UI callbacks");
        auto& doomed=unknown.owner;scenes.GetCurrentScene()->DestroyActor(doomed);staleEdit.Invoke(*FieldValue::Parse(10,0,"999"));
        Require(true,"deleted Behaviour Inspector callback returns through expired lifetime");
        scenes.UnloadCurrentScene();scripts.ClearCSharpProject();host.Shutdown();return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"S4 FAIL: %s\n",e.what());return 1;}
}
