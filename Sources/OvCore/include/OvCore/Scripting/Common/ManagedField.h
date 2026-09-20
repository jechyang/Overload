#pragma once
#include <OvCore/Scripting/CSharp/HostABI.h>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace OvCore::Scripting
{
    // A separate typed surface leaves Lua's number semantics unchanged. No integer passes through double.
    struct ManagedFieldValue
    {
        static constexpr size_t MaxTextBytes = 64 * 1024;
        uint32_t tag = 0, auxiliary = 0;
        OvCsFieldScalar scalar{};
        std::string text;
        OvCsFieldValueView View() const { return {48, tag, 0, auxiliary, scalar, {reinterpret_cast<const uint8_t*>(text.data()), text.size()}}; }
        template<class T> static std::string Number(T value) {
            char buffer[128]; auto r = std::to_chars(buffer, buffer + sizeof(buffer), value);
            return r.ec == std::errc{} ? std::string(buffer, r.ptr) : std::string{};
        }
        std::string Display() const {
            switch (tag) {
            case 1: return scalar.boolean ? "true" : "false";
            case 2: return Number(scalar.int8_value); case 3: return Number(scalar.uint8_value);
            case 4: return Number(scalar.int16_value); case 5: return Number(scalar.uint16_value);
            case 6: return Number(scalar.int32_value); case 7: return Number(scalar.uint32_value);
            case 8: case 13: return Number(scalar.int64_value); case 9: case 14: case 17: return Number(scalar.uint64_value);
            case 10: return Number(scalar.float32_value); case 11: return Number(scalar.float64_value);
            case 15: return Number(scalar.vector3.x)+","+Number(scalar.vector3.y)+","+Number(scalar.vector3.z);
            case 16: return Number(scalar.quaternion.x)+","+Number(scalar.quaternion.y)+","+Number(scalar.quaternion.z)+","+Number(scalar.quaternion.w);
            case OVCS_FIELD_VECTOR2: return Number(scalar.float_values[0])+","+Number(scalar.float_values[1]);
            case OVCS_FIELD_VECTOR4: case OVCS_FIELD_RECT: case OVCS_FIELD_COLOR:
                return Number(scalar.float_values[0])+","+Number(scalar.float_values[1])+","+Number(scalar.float_values[2])+","+Number(scalar.float_values[3]);
            case OVCS_FIELD_RECT_OFFSET:
                return Number(scalar.int32_values[0])+","+Number(scalar.int32_values[1])+","+Number(scalar.int32_values[2])+","+Number(scalar.int32_values[3]);
            default: return text;
            }
        }
        template<class T> static bool ParseNumber(std::string_view text, T& value) {
            auto r = std::from_chars(text.data(),text.data()+text.size(),value);
            if constexpr (std::is_floating_point_v<T>) if (!std::isfinite(value)) return false;
            return r.ec == std::errc{} && r.ptr == text.data()+text.size();
        }
        static std::optional<ManagedFieldValue> Parse(uint32_t tag, uint32_t aux, const std::string& input) {
            if (input.size() > MaxTextBytes) return {};
            ManagedFieldValue v; v.tag=tag; v.auxiliary=aux; bool ok=false;
            switch(tag) {
            case 1: ok=input=="true" || input=="false"; v.scalar.boolean=input=="true"; break;
            case 2: ok=ParseNumber(input,v.scalar.int8_value); break; case 3: ok=ParseNumber(input,v.scalar.uint8_value); break;
            case 4: ok=ParseNumber(input,v.scalar.int16_value); break; case 5: ok=ParseNumber(input,v.scalar.uint16_value); break;
            case 6: ok=ParseNumber(input,v.scalar.int32_value); break; case 7: ok=ParseNumber(input,v.scalar.uint32_value); break;
            case 8: case 13: ok=ParseNumber(input,v.scalar.int64_value); break;
            case 9: case 14: case 17: ok=ParseNumber(input,v.scalar.uint64_value); break;
            case 10: ok=ParseNumber(input,v.scalar.float32_value); break; case 11: ok=ParseNumber(input,v.scalar.float64_value); break;
            case 15: case 16: {
                float parts[4]{}; size_t begin=0; ok=true; const size_t count=tag==15?3:4;
                for(size_t i=0;i<count;++i) {
                    const auto end=input.find(',',begin);
                    if ((i+1==count) != (end==std::string::npos)) { ok=false; break; }
                    if(!ParseNumber(std::string_view(input).substr(begin,end==std::string::npos?end:end-begin),parts[i])) { ok=false; break; }
                    begin=end+1;
                }
                if(tag==15) v.scalar.vector3={parts[0],parts[1],parts[2]}; else v.scalar.quaternion={parts[0],parts[1],parts[2],parts[3]};
                break;
            }
            case 12: case 18: case OVCS_FIELD_COMPONENT_REF: v.text=input; ok=true; break;
            case OVCS_FIELD_VECTOR2: case OVCS_FIELD_VECTOR4: case OVCS_FIELD_RECT: case OVCS_FIELD_RECT_OFFSET: case OVCS_FIELD_COLOR: {
                size_t begin=0; ok=true; const size_t count=tag==OVCS_FIELD_VECTOR2?2:4;
                for (size_t i=0;i<count;++i) {
                    const auto end=input.find(',',begin);
                    if ((i+1==count)!=(end==std::string::npos)) { ok=false; break; }
                    const auto part=std::string_view(input).substr(begin,end==std::string::npos?end:end-begin);
                    const bool parsed=tag==OVCS_FIELD_RECT_OFFSET?ParseNumber(part,v.scalar.int32_values[i]):ParseNumber(part,v.scalar.float_values[i]);
                    if (!parsed) { ok=false; break; } begin=end+1;
                }
                break;
            }
            }
            return ok?std::optional{v}:std::nullopt;
        }
    };
    struct ManagedField
    {
        uint64_t token = 0;
        uint32_t tag = 0, auxiliary = 0;
        std::string id, name;
        std::vector<std::string> aliases;
        std::string displayName;
    };
}
