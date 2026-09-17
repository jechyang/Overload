#include <OvCore/UI/Frame.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/SceneSystem/Scene.h>
#include <OvCore/ResourceManagement/TextureManager.h>
#include <OvRendering/Resources/Texture.h>
#include <OvRendering/Entities/Camera.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

namespace OvCore::UI
{
    namespace
    {
        using RenderMode = ECS::Components::CCanvas::RenderMode;

        OvMaths::FMatrix4 CameraProjection(const OvRendering::Entities::Camera& camera, OvMaths::FVector2 size)
        {
            const auto aspect = size.x / size.y;
            return camera.GetProjectionMode() == OvRendering::Settings::EProjectionMode::ORTHOGRAPHIC ?
                OvMaths::FMatrix4::CreateOrthographic(camera.GetSize(), aspect, camera.GetNear(), camera.GetFar()) :
                OvMaths::FMatrix4::CreatePerspective(camera.GetFov(), aspect, camera.GetNear(), camera.GetFar());
        }

        OvMaths::FMatrix4 CameraView(const OvRendering::Entities::Camera& camera)
        {
            const auto position = camera.GetPosition();
            const auto forward = camera.transform->GetWorldForward();
            const auto up = camera.transform->GetWorldUp();
            return OvMaths::FMatrix4::CreateView(position.x, position.y, position.z,
                position.x + forward.x, position.y + forward.y, position.z + forward.z,
                up.x, up.y, up.z);
        }

        std::optional<std::array<OvMaths::FVector2, 4>> ProjectRect(const Rect& rect,
            const OvMaths::FMatrix4& localToWorld, const OvRendering::Entities::Camera* camera,
            OvMaths::FVector2 framebufferSize)
        {
            if (!camera) return std::nullopt;
            const auto viewProjection = CameraProjection(*camera, framebufferSize) * CameraView(*camera);
            const auto end = rect.min + rect.size;
            const std::array<OvMaths::FVector2, 4> local{rect.min, {rect.min.x, end.y}, end, {end.x, rect.min.y}};
            std::array<OvMaths::FVector2, 4> result;
            for (size_t i = 0; i < local.size(); ++i)
            {
                const auto world = localToWorld * OvMaths::FVector4{local[i].x, local[i].y, 0, 1};
                const auto clip = viewProjection * world;
                if (!std::isfinite(clip.w) || clip.w <= 0.000001f) return std::nullopt;
                result[i] = {(clip.x / clip.w * 0.5f + 0.5f) * framebufferSize.x,
                    (clip.y / clip.w * 0.5f + 0.5f) * framebufferSize.y};
                if (!IsFinite(result[i])) return std::nullopt;
            }
            return result;
        }

        bool ContainsQuad(const std::array<OvMaths::FVector2, 4>& corners, OvMaths::FVector2 point)
        {
            float sign = 0;
            for (size_t i = 0; i < corners.size(); ++i)
            {
                const auto a = corners[i], b = corners[(i + 1) % corners.size()];
                const float cross = (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
                if (std::abs(cross) <= 0.0001f) continue;
                if (sign == 0) sign = cross;
                else if ((cross > 0) != (sign > 0)) return false;
            }
            return sign != 0;
        }

        bool Near(OvMaths::FVector3 a, OvMaths::FVector3 b)
        {
            constexpr float epsilon = 0.00001f;
            return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon &&
                std::abs(a.z - b.z) <= epsilon;
        }

        void SynchronizeCameraCanvasTransform(ECS::Components::CCanvas& canvas, const CanvasGeometry& geometry)
        {
            if (canvas.GetRenderMode() != RenderMode::ScreenSpaceCamera) return;
            const auto* cameraComponent = ResolveCanvasCamera(canvas);
            if (!cameraComponent) return;

            const auto center2D = geometry.rect.min + geometry.rect.size * 0.5f;
            const auto center4D = geometry.localToWorld * OvMaths::FVector4{center2D.x, center2D.y, 0, 1};
            const OvMaths::FVector3 center{center4D.x, center4D.y, center4D.z};
            const auto rotation = cameraComponent->GetCamera().GetRotation();
            auto& transform = canvas.owner.transform;
            if (!Near(transform.GetWorldPosition(), center)) transform.SetWorldPosition(center);
            if (std::abs(std::abs(OvMaths::FQuaternion::DotProduct(transform.GetWorldRotation(), rotation)) - 1.0f) > 0.00001f)
                transform.SetWorldRotation(rotation);
        }
    }

    std::optional<Hit> Frame::HitTest(OvMaths::FVector2 point) const
    {
        if (!Rect{{0, 0}, size}.Contains(point)) return std::nullopt;
        struct Candidate { bool overlay; int order; uint64_t sequence; Hit hit; };
        std::optional<Candidate> best;
        const auto consider = [&](bool raycast, bool inside, RenderMode mode, int order, uint64_t sequence,
            uint64_t actor, bool interactable) {
            if (!raycast || !inside) return;
            Candidate candidate{mode == RenderMode::ScreenSpaceOverlay, order, sequence, {actor, interactable}};
            if (!best || std::tie(candidate.overlay, candidate.order, candidate.sequence) >
                std::tie(best->overlay, best->order, best->sequence)) best = candidate;
        };
        for (const auto& image : images)
            consider(image.raycastTarget, image.hasScreenCorners ? ContainsQuad(image.screenCorners, point) : image.visibleRect.Contains(point),
                image.renderMode, image.sortingOrder, image.sequence, image.actorGuid, image.interactable);
        for (const auto& mesh : meshes)
            consider(mesh.raycastTarget, mesh.hasScreenCorners && ContainsQuad(mesh.screenCorners, point),
                mesh.renderMode, mesh.sortingOrder, mesh.sequence, mesh.actorGuid, mesh.interactable);
        return best ? std::optional<Hit>{best->hit} : std::nullopt;
    }
    std::optional<Hit> Frame::HitTestTopLeft(OvMaths::FVector2 point) const
    {
        if (!Rect{{0, 0}, size}.Contains(point)) return std::nullopt;
        // Convert the top-inclusive/bottom-exclusive interval into our
        // bottom-inclusive/top-exclusive interval, including the top viewport edge.
        return HitTest({point.x, std::nextafter(size.y - point.y, -std::numeric_limits<float>::infinity())});
    }

    const ECS::Components::CCamera* ResolveCanvasCamera(const ECS::Components::CCanvas& canvas)
    {
        if (const auto* assigned = canvas.GetWorldCamera()) return assigned;
        auto* scene = canvas.owner.GetScene();
        return scene ? scene->FindMainCamera() : nullptr;
    }

    std::optional<CanvasGeometry> ResolveCanvasGeometry(const ECS::Components::CCanvas& canvas,
        OvMaths::FVector2 size)
    {
        using namespace ECS::Components;
        if (!IsFinite(size) || size.x <= 0 || size.y <= 0) throw std::invalid_argument("Invalid UI viewport");
        const auto* rect = canvas.owner.GetComponent<CRectTransform>();
        if (!rect) return std::nullopt;
        const auto scale = canvas.GetScaleFactor();
        if (canvas.GetRenderMode() == RenderMode::WorldSpace)
            return CanvasGeometry{ResolveRect(rect->GetLayout(), {{0, 0}, {0, 0}}), canvas.owner.transform.GetWorldMatrix()};

        const Rect logical{{0, 0}, {size.x / scale, size.y / scale}};
        if (canvas.GetRenderMode() == RenderMode::ScreenSpaceOverlay)
            return CanvasGeometry{logical, canvas.owner.transform.GetWorldMatrix()};

        const auto* cameraComponent = ResolveCanvasCamera(canvas);
        if (!cameraComponent) return std::nullopt;
        const auto& camera = cameraComponent->GetCamera();
        const float aspect = size.x / size.y;
        const float height = camera.GetProjectionMode() == OvRendering::Settings::EProjectionMode::ORTHOGRAPHIC ?
            camera.GetSize() * 2.0f : 2.0f * canvas.GetPlaneDistance() * std::tan(camera.GetFov() * 0.5f * 3.14159265359f / 180.0f);
        const float width = height * aspect;
        const auto cameraMatrix = OvMaths::FMatrix4::Translation(camera.GetPosition()) * OvMaths::FMatrix4::Rotation(camera.GetRotation());
        const auto matrix = cameraMatrix * OvMaths::FMatrix4::Translation({-width * 0.5f, -height * 0.5f, canvas.GetPlaneDistance()}) *
            OvMaths::FMatrix4::Scaling({width / logical.size.x, height / logical.size.y, 1});
        return CanvasGeometry{logical, matrix};
    }

    void UpdateRectContexts(SceneSystem::Scene& scene, OvMaths::FVector2 size)
    {
        using namespace ECS::Components;
        if (!IsFinite(size) || size.x <= 0 || size.y <= 0) throw std::invalid_argument("Invalid UI viewport");
        struct Node { ECS::Actor* actor; OvMaths::FVector2 parent; bool inCanvas; };
        std::vector<Node> stack;
        for (auto* actor : scene.GetActors()) if (!actor->GetParent()) stack.push_back({actor, size, false});
        std::unordered_set<ECS::Actor*> seen;
        while (!stack.empty())
        {
            auto node = stack.back(); stack.pop_back();
            if (!seen.insert(node.actor).second) throw std::logic_error("UI hierarchy is not a tree");
            auto* canvas = node.actor->GetComponent<CCanvas>();
            const bool root = canvas && !node.inCanvas;
            if (root)
            {
                if (canvas->GetRenderMode() == RenderMode::WorldSpace) node.parent = {0, 0};
                else
                {
                    const auto scale = canvas->GetScaleFactor(); node.parent = {size.x / scale, size.y / scale};
                    if (canvas->GetRenderMode() == RenderMode::ScreenSpaceCamera)
                        if (const auto geometry = ResolveCanvasGeometry(*canvas, size))
                            SynchronizeCameraCanvasTransform(*canvas, *geometry);
                }
            }
            if (auto* rect = node.actor->GetComponent<CRectTransform>())
            {
                rect->SetLayoutContext(node.parent, root && canvas->GetRenderMode() != RenderMode::WorldSpace);
                node.parent = rect->GetRect().size;
            }
            for (auto* child : node.actor->GetChildren()) stack.push_back({child, node.parent, node.inCanvas || canvas != nullptr});
        }
    }

    Frame ExtractFrame(SceneSystem::Scene& scene, OvMaths::FVector2 size, uint64_t generation,
        const OvRendering::Entities::Camera* renderCamera)
    {
        using namespace ECS::Components;
        if (!IsFinite(size) || size.x <= 0 || size.y <= 0) throw std::invalid_argument("Invalid UI viewport");
        UpdateRectContexts(scene, size);
        if (auto* scripts = Global::ServiceLocator::TryGet<Scripting::ScriptEngine>()) scripts->RebuildUI(scene, size);
        Frame frame{generation, size, {}};
        uint64_t sequence = 0;
        struct State
        {
            ECS::Actor* actor;
            bool hasCanvas = false;
            Rect parent, clip;
            RenderMode mode = RenderMode::ScreenSpaceOverlay;
            OvMaths::FMatrix4 canvasToWorld = OvMaths::FMatrix4::Identity;
            bool drawable = true;
            float scale = 1, alpha = 1;
            int order = 0;
            bool blocks = true, interactable = true;
        };
        std::vector<State> stack;
        const auto& actors = scene.GetActors();
        // Reverse push preserves scene-root order and Actor sibling order.
        for (auto it = actors.rbegin(); it != actors.rend(); ++it)
            if (!(*it)->GetParent()) stack.push_back({*it, false, {{0, 0}, size}, {{0, 0}, size}});
        std::unordered_set<ECS::Actor*> visited;
        while (!stack.empty())
        {
            auto state = stack.back(); stack.pop_back();
            auto& actor = *state.actor;
            if (!visited.insert(&actor).second) throw std::logic_error("UI actor hierarchy is not a tree");
            if (!actor.IsActive()) continue;
            const auto* canvas = actor.GetComponent<CCanvas>();
            const bool rootCanvas = canvas && !state.hasCanvas;
            if (canvas)
            {
                if (rootCanvas)
                {
                    state.hasCanvas = true;
                    state.scale = canvas->GetScaleFactor();
                    state.mode = canvas->GetRenderMode();
                    const auto geometry = ResolveCanvasGeometry(*canvas, size);
                    state.drawable = geometry.has_value();
                    if (geometry)
                    {
                        state.parent = geometry->rect;
                        state.clip = state.mode == RenderMode::ScreenSpaceOverlay ? Rect{{0, 0}, size} : geometry->rect;
                        state.canvasToWorld = geometry->localToWorld;
                    }
                }
                if (rootCanvas || canvas->OverridesSorting()) state.order = canvas->GetSortingOrder();
            }
            if (const auto* group = actor.GetComponent<CCanvasGroup>())
            {
                state.alpha *= group->GetAlpha();
                state.blocks = state.blocks && group->BlocksRaycasts();
                state.interactable = state.interactable && group->IsInteractable();
            }
            const auto* transform = actor.GetComponent<CRectTransform>();
            if (state.hasCanvas && transform)
            {
                const auto rect = rootCanvas ? state.parent : transform->Resolve(state.parent);
                const auto rendered = state.mode == RenderMode::WorldSpace ? rect : Rect{{rect.min.x * state.scale, rect.min.y * state.scale},
                    {rect.size.x * state.scale, rect.size.y * state.scale}};
                const auto local = state.mode == RenderMode::ScreenSpaceOverlay ? rendered : rect;
                if (!IsFinite(local.min) || !IsFinite(local.size)) throw std::overflow_error("UI rectangle overflow");
                if (const auto* mask = actor.GetComponent<CRectMask2D>(); mask && mask->IsEnabled())
                    state.clip = Rect::Intersect(state.clip, local);
                if (const auto* image = actor.GetComponent<CUIImage>(); image && state.drawable)
                {
                    auto color = image->GetColor(); color.w *= state.alpha;
                    const auto visible = Rect::Intersect(local, state.clip);
                    // Alpha zero still participates in raycasts, as an invisible blocker.
                    if (!visible.Empty())
                    {
                        Image output;
                        output.actorGuid = actor.GetGUID(); output.sequence = sequence++; output.sortingOrder = state.order; output.renderMode = state.mode;
                        output.rect = local; output.visibleRect = visible; output.canvasToWorld = state.canvasToWorld; output.color = color;
                        output.texture = image->GetTexture() ? image->GetTexture()->RetainTexture() : nullptr;
                        output.raycastTarget = state.blocks && image->IsRaycastTarget(); output.interactable = state.interactable;
                        if (state.mode == RenderMode::ScreenSpaceOverlay)
                        {
                            const auto end = visible.min + visible.size;
                            output.screenCorners = {visible.min, {visible.min.x, end.y}, end, {end.x, visible.min.y}};
                            output.hasScreenCorners = true;
                        }
                        else if (const auto projected = ProjectRect(visible, state.canvasToWorld, renderCamera, size))
                        { output.screenCorners = *projected; output.hasScreenCorners = true; }
                        frame.images.push_back(std::move(output));
                    }
                }
                if (const auto* renderer = actor.GetComponent<CCanvasRenderer>(); renderer && state.drawable &&
                    !renderer->GetVertices().empty() && !renderer->GetIndices().empty())
                {
                    const auto visible = Rect::Intersect(local, state.clip);
                    if (!visible.Empty())
                    {
                        Mesh output;
                        output.actorGuid = actor.GetGUID(); output.sequence = sequence++; output.sourceGeneration = renderer->GetMeshGeneration();
                        output.sortingOrder = state.order; output.renderMode = state.mode; output.clip = state.clip;
                        output.canvasToWorld = state.canvasToWorld; output.indices = renderer->GetIndices();
                        output.materialPath = renderer->GetMaterialPath();
                        output.raycastTarget = state.blocks && renderer->IsRaycastTarget(); output.interactable = state.interactable;
                        output.vertices.reserve(renderer->GetVertices().size());
                        const auto pivot = transform->GetPivot();
                        const auto origin = rect.min + OvMaths::FVector2{pivot.x * rect.size.x, pivot.y * rect.size.y};
                        const float coordinateScale = state.mode == RenderMode::ScreenSpaceOverlay ? state.scale : 1.0f;
                        for (auto vertex : renderer->GetVertices())
                        {
                            vertex.position.x = (origin.x + vertex.position.x) * coordinateScale;
                            vertex.position.y = (origin.y + vertex.position.y) * coordinateScale;
                            vertex.position.z *= coordinateScale;
                            vertex.color.w *= state.alpha;
                            output.vertices.push_back(vertex);
                        }
                        if (!renderer->GetTexturePath().empty())
                            if (auto* manager = Global::ServiceLocator::TryGet<ResourceManagement::TextureManager>())
                                if (auto* texture = manager->GetResource(renderer->GetTexturePath(), false))
                                    output.texture = texture->RetainTexture();
                        if (state.mode == RenderMode::ScreenSpaceOverlay)
                        {
                            const auto end = visible.min + visible.size;
                            output.screenCorners = {visible.min, {visible.min.x, end.y}, end, {end.x, visible.min.y}};
                            output.hasScreenCorners = true;
                        }
                        else if (const auto projected = ProjectRect(visible, state.canvasToWorld, renderCamera, size))
                        { output.screenCorners = *projected; output.hasScreenCorners = true; }
                        frame.meshes.push_back(std::move(output));
                    }
                }
                state.parent = rect;
            }
            const auto& children = actor.GetChildren();
            for (auto it = children.rbegin(); it != children.rend(); ++it)
            {
                auto next = state; next.actor = *it; stack.push_back(next);
            }
        }
        std::stable_sort(frame.images.begin(), frame.images.end(), [](const Image& a, const Image& b) {
            const bool aOverlay = a.renderMode == RenderMode::ScreenSpaceOverlay;
            const bool bOverlay = b.renderMode == RenderMode::ScreenSpaceOverlay;
            if (aOverlay != bOverlay) return !aOverlay;
            if (a.sortingOrder != b.sortingOrder) return a.sortingOrder < b.sortingOrder;
            return a.sequence < b.sequence;
        });
        std::stable_sort(frame.meshes.begin(), frame.meshes.end(), [](const Mesh& a, const Mesh& b) {
            const bool aOverlay = a.renderMode == RenderMode::ScreenSpaceOverlay;
            const bool bOverlay = b.renderMode == RenderMode::ScreenSpaceOverlay;
            if (aOverlay != bOverlay) return !aOverlay;
            if (a.sortingOrder != b.sortingOrder) return a.sortingOrder < b.sortingOrder;
            return a.sequence < b.sequence;
        });
        return frame;
    }
}
