// Reuse the real GL/physics engine fixture and native host prerequisites.
#define wmain S1ValidationMain
#include "native_host_validation.cpp"
#undef wmain
#pragma warning(push)
#pragma warning(disable: 4100 4201)
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/SceneSystem/PrefabOperations.h>
#include <tinyxml2.h>
#pragma warning(pop)
#include <fstream>

using OvCore::Scripting::ScriptIdentity;
using OvCore::Scripting::ScriptStatus;
using OvCore::ECS::Components::Behaviour;
static std::string Xml(const tinyxml2::XMLNode* node)
{ tinyxml2::XMLPrinter printer; node->Accept(&printer); return printer.CStr(); }
static std::string Utf8Path(const std::filesystem::path& path)
{ const auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()}; }
static std::string Utf8Text(const char8_t* value) { return reinterpret_cast<const char*>(value); }
static double Number(Behaviour& b, const char* key)
{ auto script = b.GetScript(); if (!script) throw std::runtime_error("No Lua instance"); return std::get<double>(script->GetProperty(key).value()); }
static std::string SaveRecord(Behaviour& b)
{
    tinyxml2::XMLDocument doc; auto* root = doc.NewElement("behaviours"); doc.InsertEndChild(root);
    b.SerializeRecord(doc, root); return Xml(root->FirstChild());
}
static void Discover(RuntimeHost& host, const std::filesystem::path& root, const char* file,
    uint32_t expected, OvCsStatus expectedStatus = 0)
{
    const std::string path = Utf8Path(root / file), dir = Utf8Path(root), logical = "Game";
    OvCsSessionCreateInfo info{sizeof(info), 0, 0, View(dir), View(path), View(logical)};
    OvCsSessionHandle session{999,999};
    const auto& api = host.Api(); const auto status = api.create_session(&info, &session);
    if (status != expectedStatus) host.Check(status, file);
    Require(status == expectedStatus, file);
    if (status) { Require(session.id == 0 && session.generation == 0, "failed project load publishes no session"); return; }
    uint32_t count = 99; host.Check(api.get_type_count(session, &count), "type count", session);
    Require(count == expected, "real assembly discovery filters invalid Behaviour types without constructors");
    uint64_t token = 0;
    for (uint32_t i = 0; i < count; ++i) {
        OvCsTypeInfo type{}; host.Check(api.get_type_info(session, i, &type), "type info", session);
        Require(type.struct_size == sizeof(type) && type.type_token > token, "type metadata ABI and monotonic tokens");
        token = type.type_token;
        for (uint32_t a = 0; a < type.alias_count; ++a) {
            OvCsOwnedUtf8 alias{}; host.Check(api.get_type_alias(session, token, a, &alias), "alias", session);
            Require(alias.length != 0, "real alias value"); host.Check(api.release_owned_utf8(&alias), "alias release");
        }
        auto copy = type;
        host.Check(api.release_type_info(&type), "type release");
        Require(type.type_token == 0 && api.release_type_info(&copy) == OVCS_STATUS_BUFFER_OWNERSHIP, "type ownership rejects copied release");
    }
    host.Check(api.close_session(session), "close project", session);
    Require(api.get_type_count(session, &count) == OVCS_STATUS_SESSION_NOT_FOUND, "closed discovery session rejects access");
}

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        if (argc != 5) throw std::runtime_error("S2 usage: runtime managed project mode");
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        const std::filesystem::path project = argv[3];
        Require(GetModuleHandleW(L"coreclr.dll") == nullptr, "S2 native EXE starts without CLR");
        OvReflection::MainThread main; RuntimeHost host(main);
        host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(), argv[1], argv[2]));
        Discover(host, project, "original.dll", 5);
        Discover(host, project, "moved.dll", 5);
        Discover(host, project, "renamed.dll", 5);
        Discover(host, project, "duplicate-id.dll", 0, OVCS_STATUS_DUPLICATE_IDENTITY);
        Discover(host, project, "duplicate-alias.dll", 0, OVCS_STATUS_DUPLICATE_IDENTITY);
        Discover(host, project, "alias-primary.dll", 0, OVCS_STATUS_DUPLICATE_IDENTITY);
        Discover(host, project, "empty-id.dll", 0, OVCS_STATUS_INVALID_ARGUMENT);
        Discover(host, project, "absent.dll", 0, OVCS_STATUS_INVALID_ARGUMENT);
        Discover(host, project, "corrupt.dll", 0, OVCS_STATUS_MANAGED_EXCEPTION);
        Discover(host, project, "Dependent.dll", 1);
        std::filesystem::rename(project / "Dependency.dll", project / "Dependency.missing");
        Discover(host, project, "Dependent.dll", 0, OVCS_STATUS_MANAGED_EXCEPTION);
        std::filesystem::rename(project / "Dependency.missing", project / "Dependency.dll");
        Discover(host, project, "Dependent.dll", 1);

        OvCore::Scripting::ScriptEngine scripts(project, project);
        OvCore::Global::ServiceLocator::Provide(scripts);
        Engine engine(std::wstring(argv[4]) == L"render-thread");
        OvCore::Global::ServiceLocator::Provide(engine.scenes);
        auto& scene = *engine.scenes.GetCurrentScene();
        auto& actor = scene.CreateActor("S2");
        auto& reference = scene.CreateActor("Reference");
        int added = 0;
        actor.BehaviourAddedEvent += [&](Behaviour&) { ++added; };
        auto& lua = actor.AddBehaviour("legacy.lua");
        Require(&lua == &actor.AddBehaviour(".\\legacy.lua") && added == 1 && actor.GetBehavioursOrder().size() == 1,
            "duplicate normalized Lua attachment adds one event/order entry");
        tinyxml2::XMLDocument fields;
        fields.Parse("<data extra='preserved'><script_properties><speed type='number'>7</speed><label type='string'>LuaOwner</label><future type='uint64' note='opaque'>18446744073709551615</future><bad type='alien'><nested/></bad></script_properties><extension/></data>");
        auto* props = fields.RootElement()->FirstChildElement("script_properties");
        auto* targetField = fields.NewElement("target"); targetField->SetAttribute("type", "actor");
        targetField->SetText(std::to_string(reference.GetGUID()).c_str()); props->InsertEndChild(targetField);
        auto* assetField = fields.NewElement("prefab"); assetField->SetAttribute("type", "asset");
        assetField->SetAttribute("assetType", "Prefab"); assetField->SetText("nested.ovprefab"); props->InsertEndChild(assetField);
        auto* flag = fields.NewElement("flag"); flag->SetAttribute("type", "bool"); flag->SetText(true); props->InsertEndChild(flag);
        lua.OnDeserialize(fields, fields.RootElement());
        Require(Number(lua, "speed") == 7, "legacy Lua property restored into real interpreter");
        Require(OvCore::SceneSystem::PrefabOperations::SaveToFile(actor, project / "lua.ovprefab"), "save real Lua prefab");
        auto* prefab = OvCore::SceneSystem::PrefabOperations::InstantiateFromFile(project / "lua.ovprefab", [&]() -> OvCore::ECS::Actor& { return scene.CreateActor(); });
        Require(prefab && prefab->GetBehaviour("legacy.lua") && Number(*prefab->GetBehaviour("legacy.lua"), "speed") == 7,
            "real prefab clone restores Lua overrides");
        scene.Play(); scene.Update(0.5f);
        Require(Number(lua, "awakeSpeed") == 7 && Number(lua, "ticks") == 3.5 && actor.GetName() == "LuaOwner" &&
            std::get<bool>(lua.GetScript()->GetProperty("started").value()), "real Scene Play/Update sees fields and Owner before Awake");
        Require(Number(*prefab->GetBehaviour("legacy.lua"), "awakeSpeed") == 7, "prefab fields restored before Awake");
        Require(std::get<bool>(lua.GetScript()->GetProperty("awakeFlag").value()) &&
            std::get<std::string>(lua.GetScript()->GetProperty("targetName").value()) == "Reference" &&
            std::get<OvCore::Scripting::AssetRef>(lua.GetScript()->GetProperty("prefab").value()).path == "nested.ovprefab",
            "Lua bool/string/ActorRef/Prefab AssetRef remain live through scene callbacks");
        const auto luaSave = SaveRecord(lua);
        Require(luaSave.find("18446744073709551615") != std::string::npos && luaSave.find("extension") != std::string::npos, "Lua unknown fields survive save");
        scripts.Reload();
        Require(Number(lua, "speed") == 7, "Lua reload preserves stored override");

        const auto old = ScriptIdentity::CSharp("Old.Game", "Old.Player");
        auto& cs = actor.AddBehaviour(old);
        Require(cs.Status() == ScriptStatus::MissingBackend && !cs.GetScript(), "CSharp backend absent retains empty attachment");
        tinyxml2::XMLDocument raw;
        raw.Parse("<behaviour custom='kept'><type>Old.Player</type><script_identity backend='csharp' assembly='Old.Game' type='Old.Player' id='' extra='identity-data'/><data version='8'><script_properties><wide type='uint64'>18446744073709551615</wide><unknown type='future'><nested x='1'/><![CDATA[raw & value]]></unknown></script_properties><!--comment--></data><future_node/></behaviour>");
        cs.DeserializeRecord(raw, raw.RootElement());
        Require(SaveRecord(cs) == Xml(raw.RootElement()), "missing backend preserves complete record and raw XML tree");
        auto& defaultBound = scene.CreateActor("Initially default identity").AddBehaviour(ScriptIdentity::CSharp("Game", "Gameplay.Player"));
        Require(scripts.LoadCSharpProject(host, project, project / "original.dll", "Game"), "load real project into facade");
        const auto unicode = ScriptIdentity::CSharp(Utf8Text(u8"\u65e7\u7a0b\u5e8f\u96c6"), Utf8Text(u8"\u65e7\u547d\u540d\u7a7a\u95f4.\u73a9\u5bb6"));
        Require(scripts.ResolveIdentity(unicode).id == Utf8Text(u8"\u73a9\u5bb6-\u7a33\u5b9a"), "UTF-8 byte-length aliases resolve across Host ABI");
        Require(cs.Status() == ScriptStatus::TypeReady && cs.CurrentIdentity().id == "stable-player", "old tuple alias recovers existing missing attachment");
        const auto stable = ScriptIdentity::CSharp("", "", "stable-player");
        const auto before = actor.GetBehaviours().size(); const int beforeAdded = added;
        Require(&actor.AddBehaviour(stable) == &cs && &actor.AddBehaviour(ScriptIdentity::CSharp("Game", "Gameplay.Player")) == &cs &&
            actor.GetBehaviours().size() == before && added == beforeAdded, "explicit/default/alias keys refer to one Actor record");
        auto& first = actor.AddBehaviour(ScriptIdentity::CSharp("Game", "First.Same"));
        auto& second = actor.AddBehaviour(ScriptIdentity::CSharp("Game", "Second.Same"));
        Require(&first != &second && first.Status() == ScriptStatus::TypeReady && second.Status() == ScriptStatus::TypeReady, "same class name in different namespaces remains distinct");
        auto& sameText = actor.AddBehaviour("First.Same");
        Require(&sameText != &first && sameText.GetScript() && sameText.GetScript()->IsValid(), "valid Lua and CSharp types with identical text cannot collide");
        auto& absentType = actor.AddBehaviour(ScriptIdentity::CSharp("Game", "Deleted.Type"));
        absentType.OnDeserialize(fields, fields.RootElement());
        Require(absentType.Status() == ScriptStatus::MissingType && SaveRecord(absentType).find("18446744073709551615") != std::string::npos,
            "missing type in available assembly retains opaque fields");
        Require(scripts.LoadCSharpProject(host, project, project / "moved.dll", "Game") && cs.CurrentIdentity().id == "stable-player", "source and assembly filenames do not determine identity");
        Require(scripts.LoadCSharpProject(host, project, project / "renamed.dll", "Game") && cs.CurrentIdentity().type == "Gameplay.RenamedPlayer", "explicit stable ID survives class rename");
        Require(defaultBound.Status() == ScriptStatus::TypeReady && defaultBound.CurrentIdentity().type == "Gameplay.RenamedPlayer",
            "previously missing default tuple remembers resolved stable ID across rename");
        const auto migrated = SaveRecord(cs);
        Require(migrated.find("Gameplay.RenamedPlayer") != std::string::npos && migrated.find("18446744073709551615") != std::string::npos &&
            migrated.find("identity-data") != std::string::npos, "serialization writes current identity and preserves opaque fields/attributes");
        Require(!scripts.LoadCSharpProject(host, project, project / "duplicate-id.dll", "Game") && cs.Status() == ScriptStatus::TypeReady &&
            cs.CurrentIdentity().type == "Gameplay.RenamedPlayer", "invalid generation leaves prior catalogue usable");

        scripts.ClearCSharpProject();
        std::filesystem::rename(project / "original.dll", project / "temporarily-missing.dll");
        Require(!scripts.LoadCSharpProject(host, project, project / "original.dll", "Game"), "actual removed assembly fails load");
        const auto missingSave = SaveRecord(cs);
        Require(missingSave.find("18446744073709551615") != std::string::npos, "missing assembly still serializes all raw fields");
        tinyxml2::XMLDocument reopened;
        const auto reopenedText = "<actor><behaviours>" + missingSave + "</behaviours></actor>";
        reopened.Parse(reopenedText.c_str());
        auto& missingClone = scene.CreateActor("Reopened missing record"); missingClone.OnDeserialize(reopened, reopened.RootElement());
        Require(missingClone.GetBehaviour(stable) && SaveRecord(*missingClone.GetBehaviour(stable)) == missingSave,
            "save and reopen while assembly is absent preserves complete script record");
        std::filesystem::rename(project / "temporarily-missing.dll", project / "original.dll");
        Require(scripts.LoadCSharpProject(host, project, project / "original.dll", "Game") && cs.Status() == ScriptStatus::TypeReady,
            "restored assembly resolves attachment without replacing its record");
        Require(missingClone.GetBehaviour(stable)->Status() == ScriptStatus::TypeReady &&
            SaveRecord(*missingClone.GetBehaviour(stable)).find("18446744073709551615") != std::string::npos,
            "restored assembly also resolves reopened record with original field data");
        cs.RemoveScript();
        Require(!SaveRecord(cs).empty(), "null CSharp instance serialization does not crash");
        auto& unavailable = actor.AddBehaviour(ScriptIdentity{"future-backend", "A", "T", "id"});
        unavailable.OnDeserialize(fields, fields.RootElement()); unavailable.RemoveScript();
        Require(SaveRecord(unavailable).find("18446744073709551615") != std::string::npos, "unknown backend and empty instance retain data");

        // Two opaque records may have become aliases while the backend was absent.
        scripts.ClearCSharpProject();
        auto& ambiguous = scene.CreateActor("ambiguous");
        ambiguous.AddBehaviour(old); ambiguous.AddBehaviour(stable);
        Require(!scripts.LoadCSharpProject(host, project, project / "original.dll", "Game") && ambiguous.GetBehaviours().size() == 2,
            "alias activation rejects merging two existing Actor records");
        ambiguous.RemoveBehaviour(stable);
        Require(scripts.LoadCSharpProject(host, project, project / "original.dll", "Game"), "catalogue can recover after ambiguity is removed");

        auto& malformed = scene.CreateActor("Opaque records");
        tinyxml2::XMLDocument duplicateRecords;
        const auto recordText = SaveRecord(cs);
        const auto duplicateText = "<actor><behaviours>" + recordText + recordText +
            "<behaviour future='record'><data><untyped>keep</untyped></data></behaviour></behaviours></actor>";
        duplicateRecords.Parse(duplicateText.c_str()); malformed.OnDeserialize(duplicateRecords, duplicateRecords.RootElement());
        tinyxml2::XMLDocument opaqueSaved; auto* opaqueRoot = opaqueSaved.NewElement("actors"); opaqueSaved.InsertEndChild(opaqueRoot);
        malformed.OnSerialize(opaqueSaved, opaqueRoot);
        int recordCount = 0;
        for (auto* item = opaqueRoot->FirstChildElement("actor")->FirstChildElement("behaviours")->FirstChildElement("behaviour"); item; item = item->NextSiblingElement("behaviour")) ++recordCount;
        Require(malformed.GetBehaviours().size() == 1 && recordCount == 3 && Xml(opaqueRoot).find("untyped") != std::string::npos,
            "duplicate and malformed stored records stay opaque without duplicate instances");

        const auto savedGuid = actor.GetGUID();
        tinyxml2::XMLDocument sceneDoc; auto* sceneNode = sceneDoc.NewElement("root"); sceneDoc.InsertEndChild(sceneNode);
        scene.OnSerialize(sceneDoc, sceneNode);
        Require(engine.scenes.LoadSceneFromMemory(sceneDoc), "serialized mixed Lua/CSharp scene reloads");
        auto* restored = engine.scenes.GetCurrentScene()->FindActorByGUID(savedGuid);
        Require(restored && restored->GetBehaviour(stable) && Number(*restored->GetBehaviour("legacy.lua"), "speed") == 7,
            "mixed scene restores stable CSharp identity and live Lua property");
        engine.scenes.UnloadCurrentScene();
        host.Shutdown();
        Require(true, "S2 native behaviour acceptance completed");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "S2 FAIL: %s\n", error.what()); return 1; }
}
