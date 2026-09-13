#include <OvCore/Helpers/ReflectionProperties.h>
#include <OvCore/Helpers/Serializer.h>
#include <OvCore/ECS/Components/CTransform.h>
#include <OvCore/Reflection.generated.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace OvCore::Helpers::ReflectionProperties
{
    std::string_view Meta(std::span<const Metadata> metadata, std::string_view key)
    { for (const auto& item : metadata) if (item.key == key) return item.value; return {}; }
    bool Report::Ok() const { return std::all_of(fields.begin(), fields.end(), [](const auto& r) { return r.Ok(); }); }
    void Report::Require() const
    {
        for (const auto& r : fields) if (!r.Ok())
            throw std::runtime_error("Reflection property '" + r.property + "': status=" + std::to_string(int(r.status)) + ", access=" + std::to_string(int(r.access)));
    }
    std::vector<const PropertyDesc*> Properties(const TypeDesc& type, std::string_view capability)
    {
        std::vector<const PropertyDesc*> result;
        auto add = [&](const TypeDesc& t) { for (const auto& p : t.Info().properties)
            if (Meta(p.Info().metadata, capability) == "true" && std::find(result.begin(), result.end(), &p) == result.end()) result.push_back(&p); };
        for (const auto& base : type.Info().bases) if (auto* t = base.Reflected()) add(*t);
        add(type);
        auto order = [](const PropertyDesc* p) { auto text = Meta(p->Info().metadata, "Order"); return text.empty() ? 0.0 : std::stod(std::string(text)); };
        std::stable_sort(result.begin(), result.end(), [&](auto* a, auto* b) { return order(a) < order(b); });
        return result;
    }
    static std::string_view Trim(std::string_view text)
    {
        auto first = text.find_first_not_of(" \t\r\n");
        if (first == text.npos) return {};
        return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    }
    template<class T> static bool Number(std::string_view text, T& value)
    {
        text = Trim(text);
        if (text.empty()) return false;
        if constexpr (std::is_same_v<T, bool>)
        {
            if (text == "true" || text == "1") { value = true; return true; }
            if (text == "false" || text == "0") { value = false; return true; }
            return false;
        }
        else
        {
            auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
            if constexpr (std::is_floating_point_v<T>) return std::isfinite(value);
            return true;
        }
    }
    static bool EnumDecimal(const PropertyDesc& property, std::string_view text, std::string& output)
    {
        // Find enum's real underlying facts in the application registry in the UI;
        // range is also checked by generated WriteValue before the setter is called.
        long long signedValue{}; unsigned long long unsignedValue{};
        text = Trim(text);
        if (text.starts_with('-')) { if (!Number(text, signedValue)) return false; output = std::to_string(signedValue); }
        else { if (!Number(text, unsignedValue)) return false; output = std::to_string(unsignedValue); }
        return true;
    }
    bool Parse(const PropertyDesc& property, std::string_view text, std::any& value)
    {
        bool valid = false;
        Dispatch(property, [&]<class T>()
        {
            T parsed{};
            if constexpr (std::is_same_v<T, std::string>)
            {
                if (property.Info().type.category == "enum") valid = EnumDecimal(property, text, parsed);
                else { parsed = text; valid = true; }
            }
            else if constexpr (std::is_arithmetic_v<T>) valid = Number(text, parsed);
            else
            {
                std::vector<float> parts;
                while (true)
                {
                    auto comma = text.find(','); float part{};
                    if (!Number(text.substr(0, comma), part)) return;
                    parts.push_back(part);
                    if (comma == text.npos) break;
                    text.remove_prefix(comma + 1);
                }
                if constexpr (std::is_same_v<T, OvMaths::FVector3>)
                { if (parts.size() == 3) { parsed = {parts[0], parts[1], parts[2]}; valid = true; } }
                else { if (parts.size() == 4) { parsed = {parts[0], parts[1], parts[2], parts[3]}; valid = true; } }
            }
            if (valid) value = std::move(parsed);
        });
        return valid;
    }
    template<class T> static std::string NumberText(T value)
    {
        if constexpr (std::is_same_v<T, bool>) return value ? "true" : "false";
        else
        {
            char buffer[128];
            auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
            if (result.ec != std::errc{}) throw std::runtime_error("Unable to format reflection number");
            return {buffer, result.ptr};
        }
    }
    std::string Format(const std::any& value)
    {
#define OV_FORMAT(T) if (auto* p = std::any_cast<T>(&value)) return NumberText(*p);
        OV_FORMAT(bool) OV_FORMAT(signed char) OV_FORMAT(unsigned char) OV_FORMAT(short) OV_FORMAT(unsigned short)
        OV_FORMAT(int) OV_FORMAT(unsigned int) OV_FORMAT(long) OV_FORMAT(unsigned long)
        OV_FORMAT(long long) OV_FORMAT(unsigned long long) OV_FORMAT(float) OV_FORMAT(double)
#undef OV_FORMAT
        if (auto* p = std::any_cast<std::string>(&value)) return *p;
        if (auto* p = std::any_cast<OvMaths::FVector3>(&value)) return NumberText(p->x) + "," + NumberText(p->y) + "," + NumberText(p->z);
        if (auto* p = std::any_cast<OvMaths::FQuaternion>(&value)) return NumberText(p->x) + "," + NumberText(p->y) + "," + NumberText(p->z) + "," + NumberText(p->w);
        return {};
    }
    static bool ReadElement(const PropertyDesc& property, const tinyxml2::XMLElement& element, std::any& value)
    {
        if (auto* v = std::any_cast<OvMaths::FVector3>(&value))
        {
            for (auto [name, field] : {std::pair{"x", &v->x}, {"y", &v->y}, {"z", &v->z}})
                if (auto* child = element.FirstChildElement(name))
                    if (!child->GetText() || !Number(child->GetText(), *field)) return false;
            return true;
        }
        if (auto* v = std::any_cast<OvMaths::FQuaternion>(&value))
        {
            for (auto [name, field] : {std::pair{"x", &v->x}, {"y", &v->y}, {"z", &v->z}, {"w", &v->w}})
                if (auto* child = element.FirstChildElement(name))
                    if (!child->GetText() || !Number(child->GetText(), *field)) return false;
            return true;
        }
        if (element.FirstChildElement()) return false;
        const char* text = element.GetText();
        return Parse(property, text ? text : "", value);
    }
    Report Stage(const MainThread& main, ObjectRef object, const TypeDesc& type, const tinyxml2::XMLNode& node, std::vector<Pending>& pending)
    {
        pending.clear(); Report report;
        for (auto* property : Properties(type, "Serialize"))
        {
            const auto& info = property->Info(); Result result{std::string(info.id)};
            const auto* element = node.FirstChildElement(std::string(info.name).c_str());
            // Canonical key wins; aliases are consulted only when the key is absent.
            if (!element) for (auto alias : info.aliases) if ((element = node.FirstChildElement(std::string(alias).c_str()))) break;
            if (auto error = main.Check(info.thread); error != Error::None) result = {result.property, Status::AccessDenied, error};
            else if (info.readOnly || object.IsConst()) result.status = Status::ReadOnly;
            else
            {
                auto policy = Meta(info.metadata, "Missing");
                if (!element && policy != "Default") result.status = policy == "Error" ? Status::Invalid : Status::Missing;
                else
                {
                    std::any value;
                    if (auto error = property->ReadValue(main, object, value); error != Error::None)
                        result = {result.property, Status::AccessDenied, error};
                    else if (policy == "Default")
                    {
                        if (!Parse(*property, Meta(info.metadata, "Default"), value)) result.status = Status::Invalid;
                    }
                    if (result.Ok())
                    {
                        if ((element && !ReadElement(*property, *element, value)) || property->ValidateValue(value) != Error::None) result.status = Status::Invalid;
                        else { pending.push_back({property, std::move(value)}); result.status = element ? Status::Applied : Status::Defaulted; }
                    }
                }
            }
            report.fields.push_back(std::move(result));
        }
        if (!report.Ok()) pending.clear();
        return report;
    }
    Report Serialize(const MainThread& main, ObjectRef object, const TypeDesc& type, tinyxml2::XMLDocument& doc, tinyxml2::XMLNode& node, std::span<const Hooks> hooks)
    {
        if (node.GetDocument() != &doc) return {{{std::string(type.Info().id), Status::Invalid}}};
        auto hook = Meta(type.Info().metadata, "SerializeHook");
        if (!hook.empty())
        {
            for (const auto& entry : hooks) if (entry.id == hook && entry.serialize) return entry.serialize(main, object, type, doc, node);
            return {{{std::string(type.Info().id), Status::Unsupported}}};
        }
        Report report; auto* staging = doc.NewElement("reflection-staging");
        for (auto* property : Properties(type, "Serialize"))
        {
            std::any value; Result result{std::string(property->Info().id)};
            if (auto error = property->ReadValue(main, object, value); error != Error::None) result = {result.property, Status::AccessDenied, error};
            else
            {
                std::any checked;
                if (!Parse(*property, Format(value), checked))
                { report.fields.push_back({result.property, Status::Invalid}); continue; }
                std::string name(property->Info().name);
                if (auto* v = std::any_cast<OvMaths::FVector3>(&value)) Serializer::SerializeVec3(doc, staging, name, *v);
                else if (auto* q = std::any_cast<OvMaths::FQuaternion>(&value)) Serializer::SerializeQuat(doc, staging, name, *q);
                else
                {
                    auto text = Format(value); std::any checked;
                    if (!Parse(*property, text, checked) || std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 && c != 9 && c != 10 && c != 13; })) result.status = Status::Invalid;
                    else { auto* element = doc.NewElement(name.c_str()); element->SetText(text.c_str()); staging->InsertEndChild(element); }
                }
            }
            report.fields.push_back(result);
        }
        if (report.Ok()) while (auto* child = staging->FirstChild()) node.InsertEndChild(child);
        doc.DeleteNode(staging);
        return report;
    }
    Report Deserialize(const MainThread& main, ObjectRef object, const TypeDesc& type, const tinyxml2::XMLNode& node, std::span<const Hooks> hooks)
    {
        auto hook = Meta(type.Info().metadata, "DeserializeHook");
        if (!hook.empty())
        {
            for (const auto& entry : hooks) if (entry.id == hook && entry.deserialize) return entry.deserialize(main, object, type, node);
            return {{{std::string(type.Info().id), Status::Unsupported}}};
        }
        std::vector<Pending> pending; auto report = Stage(main, object, type, node, pending);
        if (!report.Ok()) return report;
        for (const auto& entry : pending)
            if (auto error = entry.property->WriteValue(main, object, entry.value); error != Error::None)
            { report.fields.push_back({std::string(entry.property->Info().id), Status::AccessDenied, error}); break; }
        return report;
    }
    static Report TransformTRS(const MainThread& main, ObjectRef object, const TypeDesc& type, const tinyxml2::XMLNode& node)
    {
        if (!main.IsCurrent()) return {{{std::string(type.Info().id), Status::AccessDenied, Error::WrongThread}}};
        auto* transform = object.MutableAs<ECS::Components::CTransform>();
        if (!transform) return {{{std::string(type.Info().id), Status::AccessDenied, object.IsConst() ? Error::ReadOnly : Error::WrongObjectType}}};
        std::vector<Pending> pending; auto report = Stage(main, object, type, node, pending);
        if (!report.Ok()) return report;
        auto position = transform->GetLocalPosition(); auto rotation = transform->GetLocalRotation(); auto scale = transform->GetLocalScale();
        for (const auto& entry : pending)
        {
            auto name = entry.property->Info().name;
            if (name == "position") position = std::any_cast<OvMaths::FVector3>(entry.value);
            else if (name == "rotation") rotation = std::any_cast<OvMaths::FQuaternion>(entry.value);
            else if (name == "scale") scale = std::any_cast<OvMaths::FVector3>(entry.value);
        }
        transform->GetFTransform().GenerateMatricesLocal(position, rotation, scale);
        return report;
    }
    std::span<const Hooks> ComponentHooks()
    { static const Hooks hooks[]{{"TransformTRS", nullptr, TransformTRS}}; return hooks; }
}
