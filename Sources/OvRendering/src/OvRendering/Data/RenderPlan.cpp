#include <OvRendering/Data/RenderPlan.h>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
using namespace OvRendering::Data;
void RenderPlan::Validate(std::span<const std::shared_ptr<const RenderSceneSnapshot>> views) const
{
    const auto require = [](bool value, const char* message) { if (!value) throw std::invalid_argument(message); };
    std::vector<uint8_t> available;
    std::unordered_set<uint64_t> identities;
    std::unordered_set<uint32_t> storages;
    for (const auto& resource : resources)
    {
        require(resource.id && identities.insert(resource.id).second, "Duplicate or missing resource identity");
        require(resource.storage.GetID() && storages.insert(resource.storage.GetID()).second, "Duplicate or missing storage generation");
        require(resource.width && resource.height && (resource.subresources == 1 || resource.subresources == 63), "Invalid resource extent/subresources");
        require(!(resource.available & ~resource.subresources), "Invalid imported/history subresources");
        require(resource.origin != ResourceOrigin::CurrentFrame || !resource.available, "Current-frame content has no producer");
        available.push_back(resource.available);
    }
    std::unordered_set<uint64_t> viewIds;
    for (const auto& view : views)
        require(view && viewIds.insert(view->GetView().id).second, "Missing or duplicate view identity");
    std::vector<bool> used(views.size());
    for (const auto& pass : passes)
    {
        require(pass.view < views.size(), "Pass references missing view");
        const auto& view = *views[pass.view];
        require(pass.viewId == view.GetView().id && pass.stages == view.GetPipeline().stages, "Wrong View/Stage reference");
        require(!used[pass.view], "Duplicate view submission"); used[pass.view] = true;
        std::vector<baregl::Framebuffer::Snapshot::TextureAttachmentInfo> attachments;
        if (pass.target)
        {
            require(pass.target->GetSize() == std::pair{view.GetView().width, view.GetView().height}, "Pass output dimensions differ from view");
            attachments = pass.target->GetTextureAttachments();
        }
        const auto checkUse = [&](const ResourceUse& use) -> const PlanResource& {
            require(use.resource < resources.size(), "Unknown pass resource");
            const auto& resource = resources[use.resource];
            require(use.subresources && !(use.subresources & ~resource.subresources), "Invalid resource face");
            require(resource.sceneGeneration == view.GetView().sceneGeneration, "Resource belongs to another scene generation");
            return resource;
        };
        for (const auto& use : pass.reads)
        {
            const auto& resource = checkUse(use);
            require((available[use.resource] & use.subresources) == use.subresources, "Missing producer or invalid history content");
            for (const auto& attachment : attachments)
                require(attachment.storage.GetID() != resource.storage.GetID(), "Illegal framebuffer texture feedback");
        }
        for (const auto& use : pass.writes)
        {
            const auto& resource = checkUse(use);
            require(resource.width == view.GetView().width && resource.height == view.GetView().height, "Written resource dimensions differ from view");
            uint8_t attached = 0;
            for (const auto& attachment : attachments)
                if (attachment.writable && attachment.storage.GetID() == resource.storage.GetID())
                {
                    require(!attachment.layer || *attachment.layer < 6, "Invalid cubemap attachment layer");
                    attached |= attachment.layer ? uint8_t(1u << *attachment.layer) : resource.subresources;
                }
            require((attached & use.subresources) == use.subresources, "Wrong output attachment or cubemap face");
            available[use.resource] |= use.subresources;
        }
        if (pass.generateMipmaps)
        {
            const auto id = pass.generateMipmaps->RetainStorage().GetID();
            const auto found = std::find_if(resources.begin(), resources.end(), [&](const auto& r) { return r.storage.GetID() == id; });
            require(found != resources.end() && found->subresources == 63 && available[found - resources.begin()] == 63, "Mipmaps require all six valid faces");
        }
    }
    require(std::all_of(used.begin(), used.end(), [](bool value) { return value; }), "Prepared view has no pass");
}
