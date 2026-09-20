#include <OvCore/Scripting/CSharp/FontBridge.h>
#include <OvCore/ECS/Actor.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/ResourceManagement/FontManager.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <map>

namespace OvCore::Scripting::CSharp
{
    namespace
    {
        using namespace OvReflection::Script;
        constexpr uint32_t Magic = 0x3146544f;
        struct Reader
        {
            std::span<const uint8_t> bytes;
            uint32_t U32(size_t at) const
            {
                Require(at <= bytes.size() && bytes.size() - at >= 4);
                uint32_t value = 0; for (size_t i = 0; i < 4; ++i) value |= uint32_t(bytes[at + i]) << (i * 8); return value;
            }
            uint64_t U64(size_t at) const { return U32(at) | (uint64_t(U32(at + 4)) << 32); }
        };
        struct Writer
        {
            std::vector<uint8_t> bytes;
            void U32(uint32_t value) { for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(value >> (i * 8))); }
            void U64(uint64_t value) { U32(uint32_t(value)); U32(uint32_t(value >> 32)); }
            void Float(float value) { U32(std::bit_cast<uint32_t>(value)); }
            void Bounds(UI::Fonts::Bounds value) { Float(value.left); Float(value.bottom); Float(value.right); Float(value.top); }
        };
        struct Receipt
        {
            uint64_t owner;
            std::weak_ptr<const void> lifetime;
            std::filesystem::path path;
            UI::Fonts::Batch batch;
        };
        struct Session
        {
            explicit Session(State& context) : state(&context) {}
            State* state;
            ResourceManagement::FontManager fonts;
            std::map<std::filesystem::path, std::filesystem::file_time_type> stamps;
            std::map<uint64_t, Receipt> receipts;
            UI::Fonts::Font& GetFont(const std::filesystem::path& path)
            {
                Require(fonts.GetResource(path, false) || fonts.GetResources().size() < 32);
                auto time = std::filesystem::last_write_time(fonts.ResolvePath(path));
                auto* font = fonts.GetResource(path);
                if (auto previous = stamps.find(path); previous != stamps.end() && previous->second != time) font->Reload(fonts.ResolvePath(path));
                stamps[path] = time;
                return *font;
            }
        };
        // Native-only owner-thread state; the host closes it explicitly with
        // op102/owner0 before marking the managed session closed.
        std::map<uint64_t, std::unique_ptr<Session>> sessions;
        std::filesystem::path FontPath(std::span<const uint8_t> data)
        {
            Require(!data.empty() && data.size() <= 4096);
            auto path = Decode({data.data(), data.size()});
            Require(path.find('\0') == std::string::npos);
            std::replace(path.begin(), path.end(), '\\', '/');
            const std::string relative = path.front() == ':' ? path.substr(1) : path;
            Require(!relative.empty() && relative.front() != '/' && relative.find(':') == std::string::npos);
            const auto parsed = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(relative.data()), relative.size()));
            Require(!parsed.is_absolute());
            for (const auto& part : parsed) Require(part != "..");
            return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
        }
    }

    void QueryFont(OvReflection::Script::State& state, uint64_t managedSession,
        uint64_t owner, uint32_t operation, std::span<uint8_t> packet, uint64_t& output)
    {
        using namespace OvReflection::Script;
        Require(state.main.IsCurrent(), OV_SCRIPT_WRONG_THREAD);
        Require(managedSession != 0 && operation >= 100 && operation <= 103 && packet.size() >= 64 && packet.size() <= 1024 * 1024);
        Reader input{packet};
        Require(input.U32(0) == Magic && input.U32(4) == 1 && input.U32(12) == packet.size());
        const uint32_t requestBytes = input.U32(8), count = input.U32(16), style = input.U32(20), pathBytes = input.U32(32);
        const uint64_t receiptId = input.U64(24);
        Require(requestBytes >= 64 && requestBytes <= packet.size() && count <= 4096 && input.U32(52) == 0 && input.U64(56) == 0);
        auto found = sessions.find(managedSession);
        if (found != sessions.end()) Require(found->second->state == &state, OV_SCRIPT_INVALID_HANDLE);
        if (operation == 102 && owner == 0)
        {
            Require(receiptId == 0 && requestBytes == 64 && count == 0 && pathBytes == 0);
            if (found != sessions.end()) sessions.erase(found);
            output = 0; return;
        }
        if (operation == 100)
        {
            Require(receiptId == 0 && pathBytes > 0 && pathBytes <= 4096 && requestBytes == 64ull + pathBytes + uint64_t(count) * 4);
            Require(packet.size() >= 48ull + uint64_t(count) * 80 && style <= 3);
            auto& actor = state.Get<ECS::Components::CTransform>(owner)->owner;
            Require(actor.CanRunScripts() && actor.GetScene(), OV_SCRIPT_INVALID_HANDLE);
            auto path = FontPath(packet.subspan(64, pathBytes));
            std::vector<uint32_t> scalars; scalars.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                auto scalar = input.U32(64 + pathBytes + size_t(i) * 4);
                Require(UI::Fonts::Font::IsUnicodeScalar(scalar)); scalars.push_back(scalar);
            }
            if (found == sessions.end())
            {
                Require(sessions.size() < 64);
                found = sessions.emplace(managedSession, std::make_unique<Session>(state)).first;
            }
            auto& session = *found->second;
            std::erase_if(session.receipts, [](const auto& item) { return item.second.lifetime.expired(); });
            Require(session.receipts.size() < 128);
            auto& font = session.GetFont(path);
            auto batch = font.EnsureGlyphs(scalars, static_cast<UI::Fonts::Style>(style), {input.U32(36), input.U32(40)});
            uint64_t token = NextIdentity();
            Writer response;
            response.bytes.reserve(48 + batch.glyphs.size() * 80);
            response.U32(Magic); response.U32(1); response.U32(static_cast<uint32_t>(48 + batch.glyphs.size() * 80)); response.U32(static_cast<uint32_t>(batch.glyphs.size()));
            response.U64(token); response.U64(batch.generation);
            response.Float(batch.metrics.ascender); response.Float(batch.metrics.descender); response.Float(batch.metrics.lineHeight);
            response.U32(static_cast<uint32_t>(batch.pages.size()));
            for (const auto& mapping : batch.glyphs)
            {
                const auto& g = mapping.glyph;
                response.U32(mapping.scalar); response.U32(g.glyphId); response.U32(mapping.missing ? 1 : 0); response.U32(g.page);
                response.U64(g.generation); response.Float(g.advance); response.Float(g.bearingX); response.Float(g.bearingY);
                response.Bounds(g.plane); response.Bounds(g.uv); response.Float(g.distanceScale); response.Float(g.distanceBias); response.U32(0);
            }
            session.receipts.emplace(token, Receipt{owner, actor.transform.InspectorTicket(), path, std::move(batch)});
            std::copy(response.bytes.begin(), response.bytes.end(), packet.begin()); output = token;
            return;
        }
        Require(found != sessions.end(), OV_SCRIPT_INVALID_HANDLE);
        auto& session = *found->second;
        auto receipt = session.receipts.find(receiptId);
        Require(receipt != session.receipts.end(), OV_SCRIPT_INVALID_HANDLE);
        auto& lease = receipt->second;
        Require(pathBytes == 0);
        if (operation == 102)
        {
            Require(requestBytes == 64 && count == 0 && lease.owner == owner, OV_SCRIPT_INVALID_HANDLE);
            session.receipts.erase(receipt); output = 0; return;
        }
        Require(!lease.lifetime.expired(), OV_SCRIPT_INVALID_HANDLE);
        if (operation == 101)
        {
            Require(lease.owner == owner, OV_SCRIPT_INVALID_HANDLE);
            auto& actor = state.Get<ECS::Components::CTransform>(owner)->owner;
            Require(actor.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
            Require(requestBytes == 64ull + uint64_t(count) * 8 && packet.size() >= 16ull + uint64_t(count) * 4);
            std::vector<UI::Fonts::KerningPair> pairs; pairs.reserve(count);
            for (uint32_t i = 0; i < count; ++i) pairs.push_back({input.U32(64 + size_t(i) * 8), input.U32(68 + size_t(i) * 8)});
            auto* font = session.fonts.GetResource(lease.path, false);
            Require(font && font->Generation() == lease.batch.generation, OV_SCRIPT_INVALID_HANDLE);
            auto values = font->GetKerning(pairs);
            Writer response; response.U32(Magic); response.U32(1); response.U32(16 + count * 4); response.U32(count);
            for (auto value : values) response.Float(value);
            std::copy(response.bytes.begin(), response.bytes.end(), packet.begin()); output = response.bytes.size();
        }
        else if (operation == 103)
        {
            Require(requestBytes == 64 && count == 0);
            auto& renderer = *state.Get<ECS::Components::CCanvasRenderer>(owner);
            auto& actor = state.Get<ECS::Components::CTransform>(lease.owner)->owner;
            Require(&renderer.owner == &actor && actor.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
            const auto page = input.U32(44), slot = input.U32(48);
            Require(page < lease.batch.pages.size() && slot < ECS::Components::CCanvasRenderer::MaximumMaterialSlots);
            renderer.SetFontPage(slot, lease.batch.pages[page]); output = 0;
        }
    }
}
