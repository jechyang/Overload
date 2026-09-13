#include "Reflection.generated.h"
#include <OvCore/Helpers/ReflectionDrawer.h>
#include <OvTools/Utils/CallbackScope.h>
#include <OvUI/Widgets/Selection/ComboBox.h>
#include <OvUI/Widgets/InputFields/InputText.h>
#include <cstdio>
#include <stdexcept>
#include <thread>
using namespace OvReflection;
using namespace OvCore::Helpers::ReflectionProperties;
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class T> OvUI::Plugins::DataDispatcher<T>& Dispatcher(OvUI::Internal::WidgetContainer& root)
{
    for (auto [widget, mode] : root.GetWidgets()) if (auto* d = widget->GetPlugin<OvUI::Plugins::DataDispatcher<T>>()) return *d;
    throw std::runtime_error("Missing drawer dispatcher");
}
static Report CustomWrite(const MainThread&, ObjectRef object, const TypeDesc&, tinyxml2::XMLDocument& doc, tinyxml2::XMLNode& node)
{ auto* element = doc.NewElement("custom"); element->SetText(object.As<R5::Custom>()->value * 2); node.InsertEndChild(element); return {}; }
static Report CustomRead(const MainThread&, ObjectRef object, const TypeDesc&, const tinyxml2::XMLNode& node)
{ auto* element = node.FirstChildElement("custom"); if (!element) return {{{"custom", Status::Invalid}}}; object.MutableAs<R5::Custom>()->value = element->FloatText() / 2; return {}; }
static Result CustomDraw(OvUI::Internal::WidgetContainer& root, const Binding& binding, const Registry&)
{
    auto& widget = root.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
    auto& dispatcher = widget.AddPlugin<OvUI::Plugins::DataDispatcher<std::string>>();
    dispatcher.RegisterGatherer([binding] { std::any v; binding.read(v); return Format(v); });
    dispatcher.RegisterProvider([binding](const std::string& s) { std::any v; if (Parse(*binding.property, s, v)) binding.write(v); });
    return {std::string(binding.property->Info().id)};
}
int main() try
{
    Registry registry; Require(RegisterBuiltinTypes(registry) == Error::None && RegisterFixtureTypes(registry) == Error::None && registry.Freeze() == Error::None, "fixture registry");
    R5::Values source, target; auto& type = TypeOf<R5::Values>(); auto& main = registry.Main();
    tinyxml2::XMLDocument doc; auto* node = doc.NewElement("data"); doc.InsertEndChild(node);
    Serialize(main, ObjectRef::From(source), type, doc, *node).Require();
    Require(std::string(node->FirstChildElement("u64")->GetText()) == "18446744073709551615" && std::string(node->FirstChildElement("i64")->GetText()) == "-9223372036854775808", "integer XML lost precision");
    for (auto* property : Properties(type, "Serialize"))
    {
        if (property->Info().readOnly) continue;
        std::any cleared;
        Dispatch(*property, [&]<class T>() { if (property->Info().type.category == "enum") cleared = std::string("0"); else cleared = T{}; });
        Require(property->WriteValue(main, ObjectRef::From(target), cleared) == Error::None, "initialize distinct round-trip target");
    }
    target.fixed = 99;
    Deserialize(main, ObjectRef::From(target), type, *node).Require();
    Require(target.i64 == source.i64 && target.u64 == source.u64 && target.text == source.text && target.precise == source.precise && target.wide == source.wide && target.fixed == 99, "XML round trip/read-only");
    for (auto* property : Properties(type, "Serialize"))
    {
        if (property->Info().readOnly) continue;
        std::any expected, actual;
        Require(property->ReadValue(main, ObjectRef::From(source), expected) == Error::None && property->ReadValue(main, ObjectRef::From(target), actual) == Error::None && Format(expected) == Format(actual), "primitive/enum XML property equivalence");
    }
    OvTools::Utils::CallbackScope lifetime;
    for (auto* property : Properties(type, "Inspect"))
    {
        OvUI::Internal::WidgetContainer root;
        auto binding = Bind(target, *property, main, lifetime.Ticket());
        Require(Draw(root, binding, registry).Ok(), "primitive drawer");
        Require(root.GetWidgets().size() == 2, "label and control");
        if (property->Info().name == "wide")
        {
            auto& d = Dispatcher<int>(root); Require(d.Gather() == 1, "enum display"); d.NotifyChange(); d.Provide(0);
            Require(target.wide == R5::Wide::Zero, "enum write"); d.NotifyChange(); d.Provide(1); Require(target.wide == R5::Wide::Max, "wide enum precision");
        }
        if (property->Info().name == "u64")
        {
            auto d = Dispatcher<std::string>(root); Require(d.Gather() == "18446744073709551615", "integer display");
            d.NotifyChange(); d.Provide("18446744073709551616"); Require(target.u64 == source.u64, "overflow reached setter");
            d.NotifyChange(); d.Provide("9007199254740993"); Require(target.u64 == 9007199254740993ULL, "UI rounded integer");
            root.RemoveAllWidgets(); d.NotifyChange(); d.Provide("1"); Require(target.u64 == 9007199254740993ULL, "old widget callback survived refresh");
        }
        if (property->Info().name == "fixed")
        { auto& d = Dispatcher<std::string>(root); d.NotifyChange(); d.Provide("5"); Require(target.fixed == 99 && root.GetWidgets()[1].first->disabled, "readonly drawer"); }
    }
    tinyxml2::XMLDocument missing; missing.Parse("<data/>");
    auto report = Deserialize(main, ObjectRef::From(target), type, *missing.RootElement());
    Require(report.Ok() && target.fallback == -7 && target.kept == 31, "missing/default/keep");
    bool defaulted = false, kept = false, readonly = false;
    for (auto& r : report.fields) { defaulted |= r.status == Status::Defaulted; kept |= r.status == Status::Missing; readonly |= r.status == Status::ReadOnly; }
    Require(defaulted && kept && readonly, "distinct result states");
    missing.Parse("<data><old-name>17</old-name></data>"); Deserialize(main, ObjectRef::From(target), type, *missing.RootElement()).Require(); Require(target.fallback == 17, "archive alias");
    missing.Parse("<data><fallback>9</fallback><narrow>256</narrow></data>");
    Require(!Deserialize(main, ObjectRef::From(target), type, *missing.RootElement()).Ok() && target.fallback == 17, "enum overflow partially applied earlier properties");
    missing.Parse("<data><real>nan</real></data>"); Require(!Deserialize(main, ObjectRef::From(target), type, *missing.RootElement()).Ok(), "NaN accepted");
    missing.Parse("<data><real>99</real></data>"); Deserialize(main, ObjectRef::From(target), type, *missing.RootElement()).Require(); Require(target.real == 99, "UI bound clipped archive");
    R5::Required required; missing.Parse("<data/>"); Require(!Deserialize(main, ObjectRef::From(required), TypeOf<R5::Required>(), *missing.RootElement()).Ok(), "missing required accepted");
    auto* property = type.FindProperty("kept"); auto binding = Bind(target, *property, main, lifetime.Ticket()); std::any value;
    std::thread worker([&] { Require(binding.read(value).access == Error::WrongThread && binding.write(1).access == Error::WrongThread, "worker UI access"); }); worker.join();
    lifetime.Invalidate(); Require(binding.read(value).status == Status::Expired && binding.write(1).status == Status::Expired, "expired target callback");
    source.text = std::string("a\0b", 3); tinyxml2::XMLDocument badDoc; auto* bad = badDoc.NewElement("data"); badDoc.InsertEndChild(bad);
    Require(!Serialize(main, ObjectRef::From(source), type, badDoc, *bad).Ok() && !bad->FirstChild(), "unsupported XML string silently lost data/partial save");
    R5::Custom custom; const Hooks hooks[]{{"CustomWrite", CustomWrite, nullptr}, {"CustomRead", nullptr, CustomRead}};
    Require(!Serialize(main, ObjectRef::From(custom), TypeOf<R5::Custom>(), badDoc, *bad).Ok(), "unknown hook silently fell back");
    Serialize(main, ObjectRef::From(custom), TypeOf<R5::Custom>(), badDoc, *bad, hooks).Require(); custom.value = 0;
    Deserialize(main, ObjectRef::From(custom), TypeOf<R5::Custom>(), *bad, hooks).Require(); Require(custom.value == 3, "custom archive hooks");
    const DrawerHook drawers[]{{"CustomDrawer", CustomDraw}}; OvUI::Internal::WidgetContainer customRoot;
    auto cb = Bind(custom, *TypeOf<R5::Custom>().FindProperty("value"), main, lifetime.Ticket());
    Require(!Draw(customRoot, cb, registry).Ok() && Draw(customRoot, cb, registry, drawers).Ok(), "custom drawer dispatch");
    auto& d = Dispatcher<std::string>(customRoot); d.NotifyChange(); d.Provide("6"); Require(custom.value == 6, "custom drawer write");
    std::puts("PASS: R5 primitive/enum XML and drawers, exact integers, defaults/read-only/errors, hooks and expired callbacks"); return 0;
}
catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
