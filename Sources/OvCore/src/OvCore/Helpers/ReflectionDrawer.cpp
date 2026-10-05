#include <OvCore/Helpers/ReflectionDrawer.h>
#include <OvCore/Helpers/GUIDrawer.h>
#include <OvCore/ECS/Components/AComponent.h>
#include <OvTools/Utils/CallbackScope.h>
#include <OvUI/Widgets/Selection/CheckBox.h>
#include <OvUI/Widgets/Selection/ComboBox.h>
#include <OvUI/Widgets/Texts/Text.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace OvCore::Helpers::ReflectionProperties
{
    Report DrawComponent(OvUI::Internal::WidgetContainer& root, ECS::Components::AComponent& component)
    {
        const auto& components = ECS::GetComponentRegistry();
        const auto& registry = components.Reflection();
        if (!registry.Main().IsCurrent()) return {{{"component", Status::AccessDenied, Error::WrongThread}}};
        const auto* description = components.Find(component.GetTypeName());
        if (!description || !description->Borrow(component)) return {{{"component", Status::Unsupported}}};
        auto ticket = component.InspectorTicket();
        Report report;
        for (auto* property : Properties(*description->type, "Inspect"))
        {
            auto access = [target = &component, description, property, ticket, main = &registry.Main()](std::any* output, const std::any* input) -> Result
            {
                Result result{std::string(property->Info().id)};
                if (!main->IsCurrent()) return {result.property, Status::AccessDenied, Error::WrongThread};
                if (ticket.expired()) return {result.property, Status::Expired};
                auto object = description->Borrow(*target);
                if (!object) return {result.property, Status::AccessDenied, Error::WrongObjectType};
                auto error = output ? property->ReadValue(*main, *object, *output) : property->WriteValue(*main, *object, *input);
                return {result.property, error == Error::None ? Status::Applied : error == Error::ReadOnly ? Status::ReadOnly : Status::AccessDenied, error};
            };
            Binding binding{property, [access](std::any& value) { return access(&value, nullptr); }, [access](const std::any& value) { return access(nullptr, &value); }};
            report.fields.push_back(Draw(root, binding, registry, ComponentDrawers()));
        }
        return report;
    }

    struct Lease final : OvUI::Plugins::IPlugin
    {
        OvTools::Utils::CallbackScope scope;
        void Execute(OvUI::Plugins::EPluginExecutionContext) override {}
    };
    static std::string Label(const PropertyDesc& property)
    { auto label = Meta(property.Info().metadata, "DisplayName"); return std::string(label.empty() ? property.Info().name : label); }
    template<class T> static T Option(const PropertyDesc& property, std::string_view name, T fallback)
    {
        auto text = Meta(property.Info().metadata, name);
        if (text.empty()) return fallback;
        T value{}; auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) throw std::runtime_error("Invalid drawer numeric option");
        return value;
    }
    template<class T, class W, class Read, class Write> void Connect(W& widget, const Binding& binding, T initial, Read read, Write write)
    {
        auto ticket = widget.template AddPlugin<Lease>().scope.Ticket();
        auto last = std::make_shared<T>(std::move(initial));
        auto& dispatcher = widget.template AddPlugin<OvUI::Plugins::DataDispatcher<T>>();
        widget.disabled = binding.property->Info().readOnly;
        dispatcher.RegisterGatherer([ticket, last, binding, read, w = &widget]() mutable
        {
            if (ticket.expired()) return *last;
            std::any value; auto result = binding.read(value);
            if (result.Ok()) *last = read(value);
            else w->disabled = true;
            return *last;
        });
        dispatcher.RegisterProvider([ticket, binding, write](T value)
        {
            if (ticket.expired() || binding.property->Info().readOnly) return;
            auto converted = write(value);
            if (converted.has_value()) binding.write(converted);
        });
    }
    template<class T> void Vector(OvUI::Internal::WidgetContainer& root, const Binding& binding, const std::any& initial)
    {
        constexpr size_t count = std::is_same_v<T, OvMaths::FVector3> ? 3 : 4;
        using Array = std::array<float, count>;
        auto toArray = [](const std::any& value) -> Array
        {
            const auto& v = std::any_cast<const T&>(value);
            if constexpr (count == 3) return {v.x, v.y, v.z}; else return {v.x, v.y, v.z, v.w};
        };
        auto fromArray = [](Array v) -> std::any
        { if constexpr (count == 3) return T{v[0], v[1], v[2]}; else return T{v[0], v[1], v[2], v[3]}; };
        auto& p = *binding.property;
        auto& widget = root.CreateWidget<OvUI::Widgets::Drags::DragMultipleScalars<float, count>>(ImGuiDataType_Float,
            Option(p, "Min", GUIDrawer::_MIN_FLOAT), Option(p, "Max", GUIDrawer::_MAX_FLOAT), 0.f, Option(p, "Step", 1.f), "", "%.3f");
        widget.axisLabels = count == 3;
        if (p.Info().name == "scale") { widget.axisResetValue = 1.f; widget.showProportionalLock = true; }
        Connect<Array>(widget, binding, toArray(initial), toArray, fromArray);
    }
    static Result Euler(OvUI::Internal::WidgetContainer& root, const Binding& binding, const Registry&)
    {
        if (binding.property->Info().type.key != Key<OvMaths::FQuaternion>()) return {std::string(binding.property->Info().id), Status::Unsupported};
        Binding converted{binding.property,
            [binding](std::any& value) { auto r = binding.read(value); if (r.Ok()) value = OvMaths::FQuaternion::EulerAngles(std::any_cast<OvMaths::FQuaternion>(value)); return r; },
            [binding](const std::any& value) { return binding.write(OvMaths::FQuaternion(std::any_cast<OvMaths::FVector3>(value))); }};
        std::any initial; auto result = converted.read(initial); if (!result.Ok()) return result;
        GUIDrawer::CreateTitle(root, Label(*binding.property)); Vector<OvMaths::FVector3>(root, converted, initial);
        return result;
    }
    std::span<const DrawerHook> ComponentDrawers()
    { static const DrawerHook hooks[]{{"EulerAngles", Euler}}; return hooks; }
    Result Draw(OvUI::Internal::WidgetContainer& root, const Binding& binding, const Registry& registry, std::span<const DrawerHook> hooks)
    {
        const auto& p = *binding.property; const auto& info = p.Info();
        auto custom = Meta(info.metadata, "Drawer");
        if (!custom.empty())
        {
            for (const auto& hook : hooks) if (hook.id == custom && hook.draw) return hook.draw(root, binding, registry);
            return {std::string(info.id), Status::Unsupported};
        }
        std::any initial; auto result = binding.read(initial); if (!result.Ok()) return result;
        GUIDrawer::CreateTitle(root, Label(p));
        if (info.type.category == "enum")
        {
            const TypeDesc* type = nullptr;
            for (auto* candidate : registry.Types()) if (candidate->Info().key == info.type.key) type = candidate;
            if (!type) return {std::string(info.id), Status::Unsupported};
            auto& widget = root.CreateWidget<OvUI::Widgets::Selection::ComboBox>(-1);
            widget.choices[-1] = "(Unknown value)";
            int i = 0; for (const auto& entry : type->Info().enumerators) widget.choices[i++] = entry.name;
            auto gather = [type](const std::any& value)
            { int i = 0; for (const auto& entry : type->Info().enumerators) { if (entry.decimalValue == std::any_cast<const std::string&>(value)) return i; ++i; } return -1; };
            Connect<int>(widget, binding, gather(initial), gather, [type](int index) -> std::any
            { if (index < 0 || size_t(index) >= type->Info().enumerators.size()) return {}; return std::string(type->Info().enumerators[index].decimalValue); });
            return result;
        }
        if (!Dispatch(p, [&]<class T>()
        {
            if constexpr (std::is_same_v<T, bool>)
            {
                auto& widget = root.CreateWidget<OvUI::Widgets::Selection::CheckBox>();
                Connect<bool>(widget, binding, std::any_cast<bool>(initial), [](const std::any& v) { return std::any_cast<bool>(v); }, [](bool v) -> std::any { return v; });
            }
            else if constexpr (std::is_same_v<T, OvMaths::FVector3> || std::is_same_v<T, OvMaths::FQuaternion>) Vector<T>(root, binding, initial);
            else if constexpr (std::is_floating_point_v<T>)
            {
                auto& widget = root.CreateWidget<OvUI::Widgets::Drags::DragSingleScalar<T>>(std::is_same_v<T, float> ? ImGuiDataType_Float : ImGuiDataType_Double,
                    Option(p, "Min", std::numeric_limits<T>::lowest()), Option(p, "Max", std::numeric_limits<T>::max()), T{}, Option(p, "Step", 1.f), "", "%.6g");
                Connect<T>(widget, binding, std::any_cast<T>(initial), [](const std::any& v) { return std::any_cast<T>(v); }, [](T v) -> std::any { return v; });
            }
            else
            {
                // Text editing preserves every 64-bit integer bit. Parse rejects overflow;
                // declared UI bounds apply here only, never in property/XML setters.
                auto& widget = root.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
                Connect<std::string>(widget, binding, Format(initial), [](const std::any& v) { return Format(v); }, [property = &p](const std::string& text) -> std::any
                {
                    std::any value; if (!Parse(*property, text, value)) return {};
                    if constexpr (std::is_integral_v<T>)
                    {
                        auto current = std::any_cast<T>(value);
                        for (auto name : {"Min", "Max"})
                        {
                            auto bound = Meta(property->Info().metadata, name); std::any parsed;
                            if (!bound.empty() && Parse(*property, bound, parsed))
                                current = std::string_view(name) == "Min" ? std::max(current, std::any_cast<T>(parsed)) : std::min(current, std::any_cast<T>(parsed));
                        }
                        value = current;
                    }
                    return value;
                });
            }
        })) return {std::string(info.id), Status::Unsupported};
        return result;
    }
}
