#include <OvRendering/Utils/ResourceTracking.h>
#include <baregl/debug/Debug.h>
#include <algorithm>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <stdexcept>
namespace
{
    using Entry = OvRendering::Utils::ResourceTracking::TextureRegistryEntryDesc;
    struct Record { Entry entry; std::thread::id creator; };
    std::mutex registryMutex;
    std::unordered_map<const baregl::Texture*, Record> textures;
    uint64_t nextGeneration = 0;
    void RequireRetentionThread(const Record& record)
    {
        if (!record.entry.storage.HasOwnerQueue() && record.creator != std::this_thread::get_id())
            throw std::logic_error("Legacy texture storage must stay on its creating GL thread");
    }
    class EventHandler final : public baregl::debug::IEventHandler
    {
    public:
		virtual void OnBufferCreated([[maybe_unused]] const baregl::Buffer&) override {}
		virtual void OnBufferDestroyed([[maybe_unused]] const baregl::Buffer&) override {}
		virtual void OnFramebufferCreated([[maybe_unused]] const baregl::Framebuffer&) override {}
		virtual void OnFramebufferDestroyed([[maybe_unused]] const baregl::Framebuffer&) override {}
		virtual void OnRenderbufferCreated([[maybe_unused]] const baregl::Renderbuffer&) override {}
		virtual void OnRenderbufferDestroyed([[maybe_unused]] const baregl::Renderbuffer&) override {}
		virtual void OnShaderProgramCreated([[maybe_unused]] const baregl::ShaderProgram&) override {}
		virtual void OnShaderProgramDestroyed([[maybe_unused]] const baregl::ShaderProgram&) override {}
		virtual void OnShaderStageCreated([[maybe_unused]] const baregl::ShaderStage&) override {}
		virtual void OnShaderStageDestroyed([[maybe_unused]] const baregl::ShaderStage&) override {}
		virtual void OnVertexArrayCreated([[maybe_unused]] const baregl::VertexArray&) override {}
		virtual void OnVertexArrayDestroyed([[maybe_unused]] const baregl::VertexArray&) override {}


        void OnTextureCreated(const baregl::Texture& texture) override
        {
            if (!texture.GetID()) return;
            Entry entry;
            entry.id = texture.GetID(); entry.name = texture.GetDebugName();
            entry.type = texture.GetType(); entry.storage = texture.RetainStorage();
            // The constructor notifies before Allocate: publish no borrowed or
            // uninitialized descriptor fields.
            std::lock_guard lock(registryMutex);
            entry.generation = ++nextGeneration;
            textures.emplace(&texture, Record{std::move(entry), std::this_thread::get_id()});
        }
        void OnTextureUpdated(const baregl::Texture& texture) override
        {
            std::lock_guard lock(registryMutex);
            if (auto found = textures.find(&texture); found != textures.end())
            {
                auto& entry = found->second.entry;
                entry.width = texture.GetDesc().width; entry.height = texture.GetDesc().height;
                entry.allocated = texture.IsValid();
            }
        }
        void OnTextureDestroyed(const baregl::Texture& texture) override
        {
            // Domain shutdown can already make GetID() zero. The address is an
            // identity key only; it is never dereferenced after this callback.
            decltype(textures)::node_type removed;
            { std::lock_guard lock(registryMutex); removed = textures.extract(&texture); }
            // Storage release and possible GL diagnostics happen outside the lock.
        }
    };
}
namespace OvRendering::Utils::ResourceTracking
{
    void TrackResources()
    {
        static std::once_flag installed;
        std::call_once(installed, [] { baregl::debug::SetEventHandler(std::make_unique<EventHandler>()); });
    }
    std::optional<TextureRegistryEntryDesc> GetTexture(uint32_t id, uint64_t generation)
    {
        std::lock_guard lock(registryMutex);
        const Record* selected = nullptr;
        for (const auto& [key, record] : textures)
        {
            const auto& entry = record.entry;
            if (entry.id == id && entry.storage.GetID() && (!generation || generation == entry.generation) &&
                (!selected || selected->entry.generation < entry.generation)) selected = &record;
        }
        if (!selected) return std::nullopt;
        RequireRetentionThread(*selected);
        return selected->entry;
    }
    std::vector<TextureRegistryEntryDesc> GetTextures()
    {
        std::vector<TextureRegistryEntryDesc> result;
        {
            std::lock_guard lock(registryMutex);
            result.reserve(textures.size());
            for (const auto& [key, record] : textures) if (record.entry.storage.GetID())
            { RequireRetentionThread(record); result.push_back(record.entry); }
        }
        std::ranges::sort(result, {}, &TextureRegistryEntryDesc::generation);
        return result;
    }
    std::vector<uint32_t> GetTextureIDs()
    {
        std::vector<std::pair<uint64_t,uint32_t>> ordered;
        {
            std::lock_guard lock(registryMutex);
            for (const auto& [key, record] : textures) if (record.entry.storage.GetID())
                ordered.emplace_back(record.entry.generation, record.entry.id);
        }
        std::ranges::sort(ordered);
        std::vector<uint32_t> result; result.reserve(ordered.size());
        for (auto [generation, id] : ordered) result.push_back(id);
        return result;
    }
}
