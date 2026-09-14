#define wmain S1ValidationMain
#include "native_host_validation.cpp"
#undef wmain
#pragma warning(push)
#pragma warning(disable: 4100 4201)
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/Scripting/DispatchScope.h>
#include <OvTools/Jobs/JobSystem.h>
#include <tinyxml2.h>
#pragma warning(pop)
#include <fstream>
#include <sstream>

using OvCore::Scripting::ScriptIdentity;
using OvCore::ECS::Actor;
using Behaviour = OvCore::ECS::Components::Behaviour;
static std::filesystem::path tracePath;
static std::filesystem::path EnvironmentPath(const wchar_t* name)
{
    wchar_t value[32768]{};
    const auto length = GetEnvironmentVariableW(name, value, 32768);
    if (!length || length >= 32768) throw std::runtime_error("Missing test environment path");
    return value;
}
static std::vector<std::string> Events(uint64_t guid, const std::string& type = "Trace")
{
    std::ifstream file(tracePath); std::string line; std::vector<std::string> result;
    const auto prefix = type + ":" + std::to_string(guid) + ":";
    while (std::getline(file, line)) if (line.starts_with(prefix)) result.push_back(line.substr(prefix.size()));
    return result;
}
static size_t Count(uint64_t guid, const std::string& type, const std::string& event)
{ const auto events = Events(guid, type); return std::count(events.begin(), events.end(), event); }
static Behaviour& Add(Actor& actor, const char* name)
{ return actor.AddBehaviour(ScriptIdentity::CSharp("S3", name)); }
static void Frame(OvCore::SceneSystem::SceneManager& scenes, OvTools::Jobs::JobSystem& jobs, float dt)
{
    auto* scene = scenes.GetCurrentScene();
    scene->FixedUpdate(dt); scene->Update(dt, jobs); scene->LateUpdate(dt); scenes.Update();
}
static void DirectAbi(RuntimeHost& host, const std::filesystem::path& project, Engine& engine, bool acceptance)
{
    const auto utf8 = [](const std::filesystem::path& p) { const auto s=p.u8string(); return std::string(reinterpret_cast<const char*>(s.data()),s.size()); };
    const auto dir=utf8(project), assembly=utf8(project / "S3.dll"); const std::string logical="S3", module="OvCore";
    const auto& api=host.Api(); OvCsSessionHandle session{};
    OvCsSessionCreateInfo info{sizeof(info),0,0,View(dir),View(assembly),View(logical)};
    host.Check(api.create_session(&info,&session),"ABI project");
    uint8_t hash[32]{}; const std::string hex=OV_SCRIPT_OVCORE_SIGNATURE;
    for(size_t i=0;i<32;++i)hash[i]=static_cast<uint8_t>(std::stoul(hex.substr(i*2,2),nullptr,16));
    host.Check(api.bind_native_api(session,View(module),1,sizeof(OvScriptOvCoreTable),hash,reinterpret_cast<uintptr_t>(&OvScript_GetOvCoreApi)),"ABI R6",session);
    uint32_t count=0; host.Check(api.get_type_count(session,&count),"ABI types",session); uint64_t token=0;
    for(uint32_t i=0;i<count;++i){OvCsTypeInfo type{};host.Check(api.get_type_info(session,i,&type),"ABI type",session);
        if(std::string(reinterpret_cast<const char*>(type.canonical_type_name.data),type.canonical_type_name.length)=="S3.Trace")token=type.type_token;
        host.Check(api.release_type_info(&type),"ABI release");}
    auto& actor=engine.scenes.GetCurrentScene()->CreateActor("ABI"); actor.AddComponent<Box>().SetMass(0);
    const auto owner=OvCore::Scripting::PublishComponent(host.NativeContext(),actor.transform);
    OvCsInstanceCreateInfo create{sizeof(create),0,token,host.NativeContext().Id(),owner,actor.GetGUID()}; OvCsInstanceHandle instance{};
    ++create.native_context_id;
    Require(api.create_instance(session,&create,&instance)==OVCS_STATUS_INVALID_ARGUMENT && !instance.id,"create rejects mismatched native context without publishing instance");
    --create.native_context_id;
    host.Check(api.create_instance(session,&create,&instance),"ABI create",session);
    OvCsLifecycleCall call{sizeof(call),OVCS_LIFECYCLE_START,instance,0.1f,0,0};
    Require(api.dispatch_lifecycle(session,&call)==OVCS_STATUS_INVALID_LIFECYCLE,"Start before Awake rejected");
    ++call.instance.session_generation;
    Require(api.dispatch_lifecycle(session,&call)==OVCS_STATUS_INSTANCE_NOT_FOUND,"stale instance generation rejected"); --call.instance.session_generation;
    OvCsStatus worker=0; std::thread wrong([&]{worker=api.dispatch_lifecycle(session,&call);});wrong.join();
    Require(worker==OVCS_STATUS_WRONG_THREAD,"lifecycle ABI rejects worker");
    if(engine.render) Require(engine.render->Invoke([&](auto&){return api.dispatch_lifecycle(session,&call);})==OVCS_STATUS_WRONG_THREAD,"lifecycle ABI rejects RenderThread");
    for(auto event:{1u,1u,3u,3u,2u,2u}){call.event=event;host.Check(api.dispatch_lifecycle(session,&call),"ABI lifecycle",session);}
    Require(Events(actor.GetGUID())==std::vector<std::string>{"awake","enable","start"},"Host state machine suppresses duplicate initialization");
    using Hold=OvCsStatus(OVCS_CALL*)(OvCsSessionHandle,OvCsInstanceHandle);
    using Probe=OvCsStatus(OVCS_CALL*)(uint32_t);
    Probe probe=nullptr;
    if(acceptance){
        auto hold=reinterpret_cast<Hold>(host.Entry(L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost",L"HoldFacade"));
        probe=reinterpret_cast<Probe>(host.Entry(L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost",L"ProbeFacade"));
        host.Check(hold(session,instance),"hold real facade",session);
        host.Check(probe(0),"live Actor facade");host.Check(probe(1),"live Box facade");
        actor.RemoveComponent<Box>();
        Require(probe(1)==OVCS_STATUS_MANAGED_EXCEPTION,"saved managed component wrapper rejects deleted Box");
        actor.AddComponent<Box>().SetMass(0);
        Require(probe(1)==OVCS_STATUS_MANAGED_EXCEPTION,"new Box cannot revive the old component epoch");
        host.Check(hold(session,instance),"hold replacement Box",session);
    }
    call.event=15;Require(api.dispatch_lifecycle(session,&call)==OVCS_STATUS_INVALID_LIFECYCLE,"reserved S6 reload callback rejected");
    host.Check(api.close_session(session),"close live session",session);
    Require(Count(actor.GetGUID(),"Trace","destroy")==1,"session close destroys live instances once");
    Require(api.dispatch_lifecycle(session,&call)==OVCS_STATUS_SESSION_NOT_FOUND,"closed session rejects old instance");
    if(probe)Require(probe(0)==OVCS_STATUS_SESSION_NOT_FOUND && probe(1)==OVCS_STATUS_SESSION_NOT_FOUND,
        "saved managed Actor and Box facades reject closed session while native objects still live");
    engine.scenes.GetCurrentScene()->DestroyActor(actor);
}

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        if (argc != 5) throw std::runtime_error("S3 usage: runtime managed project mode");
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        tracePath = EnvironmentPath(L"OV_S3_TRACE"); std::ofstream(tracePath).close();
        Require(GetModuleHandleW(L"coreclr.dll") == nullptr, "S3 starts in a native process without CLR");
        OvReflection::MainThread main; RuntimeHost host(main);
        host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(), argv[1], argv[2]));
        const std::filesystem::path project = argv[3];
        OvCore::Scripting::ScriptEngine scripts(project, project);
        OvCore::Global::ServiceLocator::Provide(scripts);
        Engine engine(std::wstring(argv[4]) == L"render-thread");
        OvCore::Global::ServiceLocator::Provide(engine.scenes);
        OvTools::Jobs::JobSystem jobs(2);
        Require(scripts.LoadCSharpProject(host, project, project / "S3.dll", "S3"), "load freshly built S3 project");
        if (std::wstring(argv[4]) == L"closed-facades") {
            DirectAbi(host, project, engine, true); scripts.ClearCSharpProject(); host.Shutdown(); return 0;
        }
        DirectAbi(host, project, engine, false);
        auto& scene = *engine.scenes.GetCurrentScene();
        auto& mixed = scene.CreateActor("mixed"); const auto mixedId = mixed.GetGUID();
        auto& box = mixed.AddComponent<Box>(); box.SetMass(0); box.SetSize({2,3,4});
        Add(mixed, "Gameplay.MoveAndResize"); Add(mixed, "S3.Trace");
        auto& lua = mixed.AddBehaviour("s3.lua");
        auto& faulty = scene.CreateActor("fault"); const auto faultId = faulty.GetGUID(); Add(faulty, "S3.Fault");
        auto& bad = scene.CreateActor("constructor"); Add(bad, "S3.BadConstructor");
        auto& parent = scene.CreateActor("parent"); parent.SetActive(false);
        auto& child = scene.CreateActor("child"); child.SetParent(parent); const auto childId = child.GetGUID(); Add(child, "S3.Trace");
        auto& worker = scene.CreateActor("worker"); const auto workerId = worker.GetGUID(); Add(worker, "S3.Worker");
        Require(Events(mixedId).empty(), "edit-time attachment executes no managed lifecycle");
        scene.Play();
        Require(Events(mixedId) == std::vector<std::string>{"awake","enable","start"}, "exact Awake Enable Start order, initialized fields and Owner");
        Require(Events(childId).empty(), "inactive hierarchy has no Awake");
        Require(Count(workerId, "Worker", "worker-rejected") == 1, "managed native facade rejects worker access");
        Frame(engine.scenes, jobs, 0.5f);
        Require(mixed.transform.GetLocalPosition().x == 1 && box.GetSize().x == 2.5f,
            "real OnUpdate moves native Actor and resizes native Box through generated R6");
        Require(std::get<double>(lua.GetScript()->GetProperty("ticks").value()) == 0.5,
            "Lua and CSharp update the same scene in one frame");
        Require(Events(mixedId) == std::vector<std::string>{"awake","enable","start","fixed","update","late"}, "fixed/update/late phase order");
        const auto paused = Events(mixedId); engine.scenes.Update();
        Require(Events(mixedId) == paused, "scene manager maintenance alone does not run gameplay");
        Frame(engine.scenes, jobs, 0.25f);
        Require(Count(faultId, "Fault", "fault") == 1 && mixed.transform.GetLocalPosition().x == 1.5f,
            "faulted instance stops subsequent callbacks while healthy instance continues");
        Require(faulty.GetBehaviour(ScriptIdentity::CSharp("S3","S3.Fault"))->Status() == OvCore::Scripting::ScriptStatus::Faulted &&
            bad.GetBehaviour(ScriptIdentity::CSharp("S3","S3.BadConstructor"))->Status() == OvCore::Scripting::ScriptStatus::Faulted,
            "constructor and callback failures expose instance Faulted status");
        parent.SetActive(true);
        Require(Events(childId) == std::vector<std::string>{"awake","enable","start"}, "parent enabling starts child exactly once");
        parent.SetActive(false); parent.SetActive(true);
        Require(Count(childId,"Trace","awake") == 1 && Count(childId,"Trace","start") == 1 && Count(childId,"Trace","disable") == 1,
            "hierarchical disable/re-enable preserves once-only Awake and Start");
        parent.transform.SetLocalPosition({10,0,0}); child.transform.SetLocalPosition({3,0,0});
        Require(child.transform.GetWorldPosition().x == 13, "parent/child transform matrix remains correct");
        mixed.SetActive(false); const auto pos = mixed.transform.GetLocalPosition().x; Frame(engine.scenes, jobs, 0.25f);
        Require(mixed.transform.GetLocalPosition().x == pos, "disabled Actor receives no updates");
        mixed.SetActive(true);

        auto& self = scene.CreateActor("self-remove"); const auto selfId = self.GetGUID(); Add(self,"S3.SelfRemove");
        auto& mutate = scene.CreateActor("mutator"); const auto mutateId = mutate.GetGUID(); Add(mutate,"S3.Mutate"); Add(mutate,"S3.Trace");
        auto& victim = scene.CreateActor("victim"); const auto victimId = victim.GetGUID(); Add(victim,"S3.Trace");
        auto& dead = scene.CreateActor("self-destroy"); const auto deadId = dead.GetGUID(); Add(dead,"S3.SelfDestroy");
        auto& disabled = scene.CreateActor("disable-self"); const auto disabledId = disabled.GetGUID(); Add(disabled,"S3.DisableSelf");
        auto& awakeRemoval = scene.CreateActor("awake-removal"); const auto awakeRemovalId = awakeRemoval.GetGUID(); Add(awakeRemoval,"S3.RemoveInAwake");
        Frame(engine.scenes, jobs, 0.1f);
        Require(!self.GetBehaviour(ScriptIdentity::CSharp("S3","S3.SelfRemove")) && Count(selfId,"SelfRemove","destroy") == 1,
            "callback self-removal releases managed instance exactly once");
        Require(Count(mutateId,"Trace","update") == 0 && Count(mutateId,"Trace","destroy") == 1,
            "removing a later Behaviour stops its current-phase dispatch");
        Require(!scene.FindActorByGUID(victimId) && Count(victimId,"Trace","update") == 0 && Count(victimId,"Trace","destroy") == 1,
            "callback deleting later Actor skips it and destroys at safe point");
        Require(!scene.FindActorByGUID(deadId) && Count(deadId,"SelfDestroy","destroy") == 1,
            "self-destroy remains safe through native Actor traversal");
        Require(Count(disabledId,"DisableSelf","disable") == 1 && Count(disabledId,"DisableSelf","late") == 0,
            "callback disable applies before late update without reentering Host ABI");
        Require(Events(awakeRemovalId,"RemoveInAwake") == std::vector<std::string>{"awake","destroy"},
            "removal from Awake skips Enable and Start and destroys exactly once");
        Require(scene.FindActorByName("created") && Count(mutateId,"Added","awake") == 1, "callback creation and behaviour addition enter lifecycle");
        Frame(engine.scenes, jobs, 0.1f);
        Require(Count(mutateId,"Added","update") == 1 && Count(scene.FindActorByName("created")->GetGUID(),"Added","update") == 1,
            "new behaviours start before their first subsequent frame update");

        // Actual Bullet contact callbacks use the same production Actor/Host path.
        engine.scenes.LoadEmptyScene();
        auto& contacts = *engine.scenes.GetCurrentScene();
        auto& a = contacts.CreateActor("contact-a"); const auto aId = a.GetGUID();
        auto& aBox = a.AddComponent<Box>(); aBox.SetMass(0); Add(a,"S3.Trace");
        auto& b = contacts.CreateActor("contact-b"); auto& bBox = b.AddComponent<Box>(); bBox.SetMass(1);
        contacts.Play();
        for (int i = 0; i < 6; ++i) engine.physics.Update(1.0f / 30);
        Require(Count(aId,"Trace","collision-enter") >= 1, "real Bullet collision reaches CSharp with validated other identity");
        Require(Count(aId,"Trace","collision-stay") >= 1, "real collision Stay dispatched");
        b.transform.SetLocalPosition({30,0,0});
        for (int i = 0; i < 3; ++i) engine.physics.Update(1.0f / 30);
        Require(Count(aId,"Trace","collision-exit") >= 1, "real separation dispatches collision Exit");
        aBox.SetTrigger(true); b.transform.SetLocalPosition({0,0,0});
        for (int i = 0; i < 3; ++i) engine.physics.Update(1.0f / 30);
        Require(Count(aId,"Trace","trigger-enter") >= 1 && Count(aId,"Trace","trigger-stay") >= 1, "real trigger Enter and Stay dispatched");
        b.transform.SetLocalPosition({30,0,0});
        for (int i = 0; i < 3; ++i) engine.physics.Update(1.0f / 30);
        Require(Count(aId,"Trace","trigger-exit") >= 1, "real trigger Exit dispatched");
        const auto targetPosition = b.transform.GetWorldPosition();
        Require(engine.physics.Raycast(targetPosition - OvMaths::FVector3{3,0,0},{1,0,0},6).has_value(), "real physics raycast hits native box at its simulated position");
        auto& contactDelete = contacts.CreateActor("contact-delete"); const auto contactDeleteId = contactDelete.GetGUID();
        contactDelete.transform.SetLocalPosition({100,0,0}); contactDelete.AddComponent<Box>().SetMass(0); Add(contactDelete,"S3.RemoveInCollision");
        auto& contactHit = contacts.CreateActor("contact-hit");
        contactHit.transform.SetLocalPosition({100,0,0}); contactHit.AddComponent<Box>().SetMass(1);
        OvCore::Scripting::DispatchScope::Flush();
        for (int i = 0; i < 3; ++i) engine.physics.Update(1.0f / 30);
        engine.scenes.Update();
        Require(!contacts.FindActorByGUID(contactDeleteId) && Count(contactDeleteId,"RemoveInCollision","destroy") == 1,
            "destruction requested from actual Bullet callback waits until simulation safe point");

        auto& cached = contacts.CreateActor("cache"); const auto cachedId = cached.GetGUID(); cached.AddComponent<Box>().SetMass(0); Add(cached,"S3.Cache");
        OvCore::Scripting::DispatchScope::Flush();
        const auto oldHandle = OvCore::Scripting::PublishComponent(host.NativeContext(), cached.transform);
        engine.scenes.LoadEmptyScene();
        auto& next = *engine.scenes.GetCurrentScene(); auto& stale = next.CreateActor("stale"); Add(stale,"S3.Stale"); next.Play();
        Require(stale.transform.GetLocalPosition().x == 91 && Count(cachedId,"Cache","destroy") == 1,
            "scene destruction invalidates saved managed Actor/Box wrappers after valid OnDestroy");
        Require(OvReflection::Script::Guard(host.NativeContext().Id(), [&](auto& state) { state.Resolve(oldHandle); }) == OV_SCRIPT_INVALID_HANDLE,
            "old scene native R6 handle is revoked by weak lifetime");

        const auto destination = EnvironmentPath(L"OV_S3_SCENE");
        tinyxml2::XMLDocument document; document.Parse("<root><scene><actors/></scene></root>"); document.SaveFile(destination.string().c_str());
        auto& switcher = next.CreateActor("switch"); const auto switchId = switcher.GetGUID(); Add(switcher,"S3.Switch");
        auto& skipped = next.CreateActor("skipped"); const auto skippedId = skipped.GetGUID(); Add(skipped,"S3.Trace");
        Frame(engine.scenes, jobs, 0.1f);
        Require(Count(switchId,"Switch","switch") == 1 && Count(switchId,"Switch","destroy") == 1 &&
            Count(skippedId,"Trace","update") == 0 && Count(skippedId,"Trace","late") == 0 && engine.scenes.GetCurrentScene()->GetActors().empty(),
            "callback scene switch stops old phase then loads new scene at SceneManager safe point");
        Require(Count(faultId,"Fault","destroy") == 1, "throwing cleanup attempted once and references released");
        scripts.ClearCSharpProject(); host.Shutdown();
        Require(true, "S3 runtime completed with all native value/event assertions");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
}
