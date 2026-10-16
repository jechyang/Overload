#pragma once
#include <OvCore/ECS/Components/CModelRenderer.h>
#include <OvCore/ECS/Components/CSkinnedMeshRenderer.h>
#include <OvCore/ECS/Components/CPhysicalBox.h>
#include <OvCore/ECS/Components/CPhysicalSphere.h>
#include <OvCore/ECS/Components/CPhysicalCapsule.h>
#include <OvCore/ECS/Components/CUI.h>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace OvEditor::Core
{
    struct SelectionBounds
    {
        OvMaths::FVector3 min{}, max{};
        bool valid = false;
        OvMaths::FVector3 Center() const { return (min + max) * .5f; }
        float Radius() const { return OvMaths::FVector3::Length(max - min) * .5f; }
        void Include(const OvMaths::FVector3& center, float radius)
        {
            if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) || !std::isfinite(radius)) return;
            const auto lo = center - std::max(radius, 0.f), hi = center + std::max(radius, 0.f);
            if (!valid) { min = lo; max = hi; valid = true; return; }
            min = {std::min(min.x,lo.x),std::min(min.y,lo.y),std::min(min.z,lo.z)};
            max = {std::max(max.x,hi.x),std::max(max.y,hi.y),std::max(max.z,hi.z)};
        }
        static SelectionBounds From(const std::vector<OvCore::ECS::Actor*>& selection)
        {
            using namespace OvCore::ECS::Components;
            SelectionBounds bounds;
            std::vector<OvCore::ECS::Actor*> pending = selection;
            std::unordered_set<OvCore::ECS::Actor*> visited;
            while (!pending.empty())
            {
                auto* actor = pending.back(); pending.pop_back();
                if (!actor || !actor->IsAlive() || !visited.insert(actor).second) continue;
                const auto position = actor->transform.GetWorldPosition(), scale = actor->transform.GetWorldScale();
                const float factor = std::max({std::abs(scale.x),std::abs(scale.y),std::abs(scale.z)});
                // Empty entities still contribute their pivot, without expanding mesh bounds.
                bounds.Include(position, 0);
                if (auto* renderer = actor->GetComponent<CModelRenderer>())
                {
                    const auto* sphere = renderer->GetFrustumBehaviour() == CModelRenderer::EFrustumBehaviour::CUSTOM_BOUNDS
                        ? &renderer->GetCustomBoundingSphere() : renderer->GetModel() ? &renderer->GetModel()->GetBoundingSphere() : nullptr;
                    if (sphere)
                    {
                        const auto center = actor->transform.GetWorldMatrix() * OvMaths::FVector4{sphere->position.x,sphere->position.y,sphere->position.z,1};
                        const auto* skin = actor->GetComponent<CSkinnedMeshRenderer>();
                        bounds.Include({center.x,center.y,center.z},sphere->radius * factor * (skin ? std::max(1.f,skin->GetMeshBoundsScale()) : 1.f));
                    }
                }
                if (auto* box = actor->GetComponent<CPhysicalBox>()) bounds.Include(position,OvMaths::FVector3::Length(box->GetSize()) * factor);
                if (auto* sphere = actor->GetComponent<CPhysicalSphere>()) bounds.Include(position,sphere->GetRadius() * factor);
                if (auto* capsule = actor->GetComponent<CPhysicalCapsule>()) bounds.Include(position,(capsule->GetHeight() + capsule->GetRadius()) * factor);
                if (auto* rect = actor->GetComponent<CRectTransform>())
                {
                    const auto worldMatrix = actor->transform.GetWorldMatrix();
                    for (const auto& corner : rect->GetLocalCorners())
                    {
                        const auto p = worldMatrix * OvMaths::FVector4{corner.x, corner.y, 0.f, 1.f};
                        bounds.Include({p.x, p.y, p.z}, 0.f);
                    }
                }
                for (auto* child : actor->GetChildren()) pending.push_back(child);
            }
            return bounds;
        }
    };
}
