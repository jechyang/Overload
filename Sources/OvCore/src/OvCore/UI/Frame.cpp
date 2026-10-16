#include <OvCore/UI/Frame.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/SceneSystem/Scene.h>
#include <OvCore/ResourceManagement/TextureManager.h>
#include <OvCore/ResourceManagement/MaterialManager.h>
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

        void RadialCutCorner(std::array<float, 8>& data, float cosVal, float sinVal, bool invert, int corner)
        {
            const int i0 = corner;
            const int i1 = (corner + 1) % 4;
            const int i2 = (corner + 2) % 4;
            const int i3 = (corner + 3) % 4;

            auto setX = [](std::array<float, 8>& d, int c, float v) { d[c * 2] = v; };
            auto setY = [](std::array<float, 8>& d, int c, float v) { d[c * 2 + 1] = v; };
            auto getX = [](const std::array<float, 8>& d, int c) { return d[c * 2]; };
            auto getY = [](const std::array<float, 8>& d, int c) { return d[c * 2 + 1]; };
            auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

            if ((corner & 1) == 1)
            {
                if (sinVal > cosVal)
                {
                    cosVal /= sinVal; sinVal = 1.0f;
                    if (invert)
                    {
                        setX(data, i1, lerp(getX(data, i0), getX(data, i2), cosVal));
                        setX(data, i2, getX(data, i1));
                    }
                }
                else if (cosVal > sinVal)
                {
                    sinVal /= cosVal; cosVal = 1.0f;
                    if (!invert)
                    {
                        setY(data, i2, lerp(getY(data, i0), getY(data, i2), sinVal));
                        setY(data, i3, getY(data, i2));
                    }
                }
                else
                {
                    cosVal = 1.0f; sinVal = 1.0f;
                }
                if (!invert) setX(data, i3, lerp(getX(data, i0), getX(data, i2), cosVal));
                else setY(data, i1, lerp(getY(data, i0), getY(data, i2), sinVal));
            }
            else
            {
                if (cosVal > sinVal)
                {
                    sinVal /= cosVal; cosVal = 1.0f;
                    if (!invert)
                    {
                        setY(data, i1, lerp(getY(data, i0), getY(data, i2), sinVal));
                        setY(data, i2, getY(data, i1));
                    }
                }
                else if (sinVal > cosVal)
                {
                    cosVal /= sinVal; sinVal = 1.0f;
                    if (invert)
                    {
                        setX(data, i2, lerp(getX(data, i0), getX(data, i2), cosVal));
                        setX(data, i3, getX(data, i2));
                    }
                }
                else
                {
                    cosVal = 1.0f; sinVal = 1.0f;
                }
                if (invert) setY(data, i3, lerp(getY(data, i0), getY(data, i2), sinVal));
                else setX(data, i1, lerp(getX(data, i0), getX(data, i2), cosVal));
            }
        }

        bool RadialCut(std::array<float, 8>& xy, std::array<float, 8>& uv, float fill, bool invert, int corner)
        {
            if (fill < 0.001f) return false;
            if ((corner & 1) == 1) invert = !invert;
            if (!invert && fill > 0.999f) return true;
            float angle = std::clamp(fill, 0.0f, 1.0f);
            if (invert) angle = 1.0f - angle;
            angle *= 90.0f * (3.14159265358979323846f / 180.0f);
            const float cosVal = std::cos(angle);
            const float sinVal = std::sin(angle);
            RadialCutCorner(xy, cosVal, sinVal, invert, corner);
            RadialCutCorner(uv, cosVal, sinVal, invert, corner);
            return true;
        }

        void FillQuadrant(std::array<float, 8>& xy, std::array<float, 8>& uv,
            const OvMaths::FVector4& v, const OvMaths::FVector4& outer,
            float fx0, float fy0, float fx1, float fy1)
        {
            const float x0 = v.x + (v.z - v.x) * fx0, x1 = v.x + (v.z - v.x) * fx1;
            const float y0 = v.y + (v.w - v.y) * fy0, y1 = v.y + (v.w - v.y) * fy1;
            const float u0 = outer.x + (outer.z - outer.x) * fx0, u1 = outer.x + (outer.z - outer.x) * fx1;
            const float w0 = outer.y + (outer.w - outer.y) * fy0, w1 = outer.y + (outer.w - outer.y) * fy1;
            xy[0] = x0; xy[1] = y0; xy[2] = x0; xy[3] = y1; xy[4] = x1; xy[5] = y1; xy[6] = x1; xy[7] = y0;
            uv[0] = u0; uv[1] = w0; uv[2] = u0; uv[3] = w1; uv[4] = u1; uv[5] = w1; uv[6] = u1; uv[7] = w0;
        }

        void AddFilledQuad(Mesh& output, const std::array<float, 8>& xy, const OvMaths::FVector4& color, const std::array<float, 8>& uv)
        {
            const uint32_t start = static_cast<uint32_t>(output.vertices.size());
            output.vertices.reserve(output.vertices.size() + 4);
            output.indices.reserve(output.indices.size() + 6);

            ECS::Components::UIVertex vert{};
            vert.color = color;

            vert.position = {xy[0], xy[1], 0.0f};
            vert.uv = {uv[0], uv[1]};
            output.vertices.push_back(vert);

            vert.position = {xy[2], xy[3], 0.0f};
            vert.uv = {uv[2], uv[3]};
            output.vertices.push_back(vert);

            vert.position = {xy[4], xy[5], 0.0f};
            vert.uv = {uv[4], uv[5]};
            output.vertices.push_back(vert);

            vert.position = {xy[6], xy[7], 0.0f};
            vert.uv = {uv[6], uv[7]};
            output.vertices.push_back(vert);

            output.indices.push_back(start);
            output.indices.push_back(start + 1);
            output.indices.push_back(start + 2);

            output.indices.push_back(start + 2);
            output.indices.push_back(start + 3);
            output.indices.push_back(start);
        }
    }

    std::optional<OvMaths::FVector2> Frame::ProjectGraphic(uint64_t actorGuid, OvMaths::FVector2 screen) const
    {
        if (!IsFinite(screen) || size.x <= 0 || size.y <= 0) return std::nullopt;
        for (const auto& mesh : meshes) {
            if (mesh.actorGuid != actorGuid || mesh.pop) continue;
            OvMaths::FVector2 point = screen;
            if (mesh.renderMode != RenderMode::ScreenSpaceOverlay) {
                const float x = screen.x / size.x * 2 - 1, y = screen.y / size.y * 2 - 1;
                auto a = mesh.screenToCanvas * OvMaths::FVector4{x,y,-1,1};
                auto b = mesh.screenToCanvas * OvMaths::FVector4{x,y,1,1};
                if (std::abs(a.w) < 1e-8f || std::abs(b.w) < 1e-8f) return std::nullopt;
                a = a * (1/a.w); b = b * (1/b.w);
                if (std::abs(b.z-a.z) < 1e-8f) return std::nullopt;
                const float t = -a.z/(b.z-a.z);
                point = {a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t};
            }
            return point / mesh.coordinateScale - mesh.localOrigin;
        }
        return std::nullopt;
    }

    std::optional<GraphicHit> Frame::RaycastGraphic(uint64_t actorGuid, OvMaths::FVector2 screen) const
    {
        if (!Rect{{0,0},size}.Contains(screen)) return std::nullopt;
        uint8_t stencil=0;
        std::optional<GraphicHit> hit;
        for (const auto& mesh : meshes) {
            OvMaths::FVector2 point=screen;
            if (mesh.renderMode != RenderMode::ScreenSpaceOverlay) {
                const float x=screen.x/size.x*2-1,y=screen.y/size.y*2-1;
                auto a=mesh.screenToCanvas*OvMaths::FVector4{x,y,-1,1}, b=mesh.screenToCanvas*OvMaths::FVector4{x,y,1,1};
                if(std::abs(a.w)<1e-8f||std::abs(b.w)<1e-8f)continue;
                a=a*(1/a.w);b=b*(1/b.w);const float dz=b.z-a.z;
                if(std::abs(dz)<1e-8f)continue;
                const float t=-a.z/dz; if(t<0||t>1)continue;
                point={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};
            }
            if(!mesh.clip.Contains(point))continue;
            bool inside=false;
            for(size_t i=0;i+2<mesh.indices.size();i+=3) {
                const auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
                auto cross=[&](const auto& u,const auto& v){return (v.x-u.x)*(point.y-u.y)-(v.y-u.y)*(point.x-u.x);};
                const float ab=cross(a,b),bc=cross(b,c),ca=cross(c,a);
                const float area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
                if(std::abs(area)>1e-8f && ((ab>=0&&bc>=0&&ca>=0)||(ab<=0&&bc<=0&&ca<=0))) {inside=true;break;}
            }
            const auto& state=mesh.stencil;
            const bool passes=!state.enabled || (stencil&state.readMask)==(state.reference&state.readMask);
            if(inside && passes && state.enabled && state.replace)stencil=uint8_t((stencil&~state.writeMask)|(state.reference&state.writeMask));
            if(mesh.actorGuid!=actorGuid||mesh.pop||!mesh.raycastTarget||!passes||!mesh.hasScreenCorners||!ContainsQuad(mesh.screenCorners,screen))continue;
            GraphicHit value;
            value.localPoint=point/mesh.coordinateScale-mesh.localOrigin;
            const auto world=mesh.canvasToWorld*OvMaths::FVector4{point.x,point.y,0,1};
            const auto normal=mesh.canvasToWorld*OvMaths::FVector4{0,0,-1,0};
            value.worldPoint={world.x,world.y,world.z};value.worldNormal={normal.x,normal.y,normal.z};
            value.distance=mesh.renderMode==RenderMode::ScreenSpaceOverlay?0:OvMaths::FVector3::Distance(value.worldPoint,mesh.cameraPosition);
            value.depth=static_cast<int>(mesh.sequence);value.sortingOrder=mesh.sortingOrder;
            value.reversed=mesh.renderMode!=RenderMode::ScreenSpaceOverlay && OvMaths::FVector3::Dot(value.worldNormal,value.worldPoint-mesh.cameraPosition)>0;
            hit=value;
        }
        return hit;
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
            consider(mesh.raycastTarget && !mesh.pop, RaycastGraphic(mesh.actorGuid,point).has_value(),
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
        const auto matrix = cameraMatrix * OvMaths::FMatrix4::Translation({width * 0.5f, -height * 0.5f, canvas.GetPlaneDistance()}) *
            OvMaths::FMatrix4::Scaling({-width / logical.size.x, height / logical.size.y, 1});
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
        for (auto* actor : scene.GetActors()) if(auto* renderer=actor->GetComponent<CCanvasRenderer>()) {
            renderer->SetHitFrame(nullptr);renderer->SetAbsoluteDepth(-1);
        }
        uint64_t sequence = 0;
        struct State
        {
            ECS::Actor* actor;
            bool hasCanvas = false;
            Rect parent, clip;
            Rect canvasClip;
            RenderMode mode = RenderMode::ScreenSpaceOverlay;
            OvMaths::FMatrix4 canvasToWorld = OvMaths::FMatrix4::Identity;
            bool drawable = true;
            float scale = 1, alpha = 1;
            int order = 0;
            bool blocks = true, interactable = true;
            std::shared_ptr<Mesh> popDraw;
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
            if (state.popDraw) {
                state.popDraw->sequence = sequence++;
                frame.meshes.push_back(std::move(*state.popDraw)); continue;
            }
            std::shared_ptr<Mesh> popDraw;
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
                        state.canvasClip = state.clip;
                        state.canvasToWorld = geometry->localToWorld;
                    }
                }
                if (canvas->OverridesSorting()) state.clip=state.canvasClip;
                if (rootCanvas || canvas->OverridesSorting()) state.order = canvas->GetSortingOrder();
            }
            if (const auto* group = actor.GetComponent<CCanvasGroup>(); group && group->IsEnabled())
            {
                if (group->IgnoresParentGroups()) { state.alpha=1;state.blocks=state.interactable=true; }
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
                        if (image->GetImageType() == ECS::Components::CUIImage::ImageType::Simple)
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
                        else if (image->GetImageType() == ECS::Components::CUIImage::ImageType::Sliced ||
                                 image->GetImageType() == ECS::Components::CUIImage::ImageType::Tiled)
                        {
                            const auto border = image->GetBorder();
                            float texW = 100.0f, texH = 100.0f;
                            if (auto* tex = image->GetTexture())
                            {
                                const auto& desc = tex->GetTexture().GetDesc();
                                if (desc.width > 0) texW = float(desc.width);
                                if (desc.height > 0) texH = float(desc.height);
                            }

                            const float borderScale = (state.mode == RenderMode::ScreenSpaceOverlay) ? state.scale : 1.0f;
                            float borderL = border.x * borderScale;
                            float borderB = border.y * borderScale;
                            float borderR = border.z * borderScale;
                            float borderT = border.w * borderScale;

                            const float rectW = local.size.x;
                            const float rectH = local.size.y;
                            if (rectW > 0.0f && rectH > 0.0f)
                            {
                                if (borderL + borderR > rectW && (borderL + borderR) > 0.0f)
                                {
                                    const float factor = rectW / (borderL + borderR);
                                    borderL *= factor;
                                    borderR *= factor;
                                }
                                if (borderB + borderT > rectH && (borderB + borderT) > 0.0f)
                                {
                                    const float factor = rectH / (borderB + borderT);
                                    borderB *= factor;
                                    borderT *= factor;
                                }

                                const std::array<float, 4> x = {
                                    local.min.x,
                                    local.min.x + borderL,
                                    local.min.x + rectW - borderR,
                                    local.min.x + rectW
                                };

                                const std::array<float, 4> y = {
                                    local.min.y,
                                    local.min.y + borderB,
                                    local.min.y + rectH - borderT,
                                    local.min.y + rectH
                                };

                                const std::array<float, 4> u = {
                                    0.0f,
                                    std::clamp(border.x / texW, 0.0f, 1.0f),
                                    std::clamp(1.0f - (border.z / texW), 0.0f, 1.0f),
                                    1.0f
                                };

                                const std::array<float, 4> v = {
                                    0.0f,
                                    std::clamp(border.y / texH, 0.0f, 1.0f),
                                    std::clamp(1.0f - (border.w / texH), 0.0f, 1.0f),
                                    1.0f
                                };

                                Mesh output;
                                output.actorGuid = actor.GetGUID();
                                output.sequence = sequence++;
                                output.sortingOrder = state.order;
                                output.renderMode = state.mode;
                                output.clip = state.clip;
                                output.canvasToWorld = state.canvasToWorld;
                                output.texture = image->GetTexture() ? image->GetTexture()->RetainTexture() : nullptr;
                                output.raycastTarget = state.blocks && image->IsRaycastTarget();
                                output.interactable = state.interactable;

                                if (state.mode == RenderMode::ScreenSpaceOverlay)
                                {
                                    const auto end = visible.min + visible.size;
                                    output.screenCorners = {visible.min, {visible.min.x, end.y}, end, {end.x, visible.min.y}};
                                    output.hasScreenCorners = true;
                                }
                                else if (const auto projected = ProjectRect(visible, state.canvasToWorld, renderCamera, size))
                                {
                                    output.screenCorners = *projected;
                                    output.hasScreenCorners = true;
                                }

                                if (state.mode != RenderMode::ScreenSpaceOverlay && renderCamera)
                                {
                                    output.screenToCanvas = OvMaths::FMatrix4::Inverse(CameraProjection(*renderCamera, size) * CameraView(*renderCamera) * state.canvasToWorld);
                                    output.cameraPosition = renderCamera->GetPosition();
                                }

                                output.vertices.reserve(16);
                                for (int j = 0; j < 4; ++j)
                                {
                                    for (int i = 0; i < 4; ++i)
                                    {
                                        ECS::Components::UIVertex vert{};
                                        vert.position = {x[i], y[j], 0.0f};
                                        vert.uv = {u[i], v[j]};
                                        vert.color = color;
                                        output.vertices.push_back(vert);
                                    }
                                }

                                const bool fillCenter = image->IsFillCenter();
                                output.indices.reserve(54);
                                for (int j = 0; j < 3; ++j)
                                {
                                    for (int i = 0; i < 3; ++i)
                                    {
                                        if (i == 1 && j == 1 && !fillCenter)
                                            continue;

                                        const uint32_t v0 = static_cast<uint32_t>(j * 4 + i);
                                        const uint32_t v1 = static_cast<uint32_t>(j * 4 + (i + 1));
                                        const uint32_t v2 = static_cast<uint32_t>((j + 1) * 4 + (i + 1));
                                        const uint32_t v3 = static_cast<uint32_t>((j + 1) * 4 + i);

                                        output.indices.push_back(v0);
                                        output.indices.push_back(v1);
                                        output.indices.push_back(v2);

                                        output.indices.push_back(v0);
                                        output.indices.push_back(v2);
                                        output.indices.push_back(v3);
                                    }
                                }

                                frame.meshes.push_back(std::move(output));
                            }
                        }
                        else if (image->GetImageType() == ECS::Components::CUIImage::ImageType::Filled)
                        {
                            const float fillAmount = image->GetFillAmount();
                            if (fillAmount >= 0.001f)
                            {
                                Mesh output;
                                output.actorGuid = actor.GetGUID();
                                output.sequence = sequence++;
                                output.sortingOrder = state.order;
                                output.renderMode = state.mode;
                                output.clip = state.clip;
                                output.canvasToWorld = state.canvasToWorld;
                                output.texture = image->GetTexture() ? image->GetTexture()->RetainTexture() : nullptr;
                                output.raycastTarget = state.blocks && image->IsRaycastTarget();
                                output.interactable = state.interactable;

                                if (state.mode == RenderMode::ScreenSpaceOverlay)
                                {
                                    const auto end = visible.min + visible.size;
                                    output.screenCorners = {visible.min, {visible.min.x, end.y}, end, {end.x, visible.min.y}};
                                    output.hasScreenCorners = true;
                                }
                                else if (const auto projected = ProjectRect(visible, state.canvasToWorld, renderCamera, size))
                                {
                                    output.screenCorners = *projected;
                                    output.hasScreenCorners = true;
                                }

                                if (state.mode != RenderMode::ScreenSpaceOverlay && renderCamera)
                                {
                                    output.screenToCanvas = OvMaths::FMatrix4::Inverse(CameraProjection(*renderCamera, size) * CameraView(*renderCamera) * state.canvasToWorld);
                                    output.cameraPosition = renderCamera->GetPosition();
                                }

                                OvMaths::FVector4 v{local.min.x, local.min.y, local.min.x + local.size.x, local.min.y + local.size.y};
                                const OvMaths::FVector4 outer{0.0f, 0.0f, 1.0f, 1.0f};
                                float tx0 = outer.x, ty0 = outer.y, tx1 = outer.z, ty1 = outer.w;
                                const auto fillMethod = image->GetFillMethod();
                                const int origin = image->GetFillOrigin();
                                const bool clockwise = image->IsFillClockwise();

                                if (fillMethod == ECS::Components::CUIImage::FillMethod::Horizontal ||
                                    fillMethod == ECS::Components::CUIImage::FillMethod::Vertical)
                                {
                                    if (fillMethod == ECS::Components::CUIImage::FillMethod::Horizontal)
                                    {
                                        const float fill = (tx1 - tx0) * fillAmount;
                                        if (origin == 1) // Right
                                        {
                                            v = {v.z - (v.z - v.x) * fillAmount, v.y, v.z, v.w};
                                            tx0 = tx1 - fill;
                                        }
                                        else // Left
                                        {
                                            v = {v.x, v.y, v.x + (v.z - v.x) * fillAmount, v.w};
                                            tx1 = tx0 + fill;
                                        }
                                    }
                                    else
                                    {
                                        const float fill = (ty1 - ty0) * fillAmount;
                                        if (origin == 1) // Top
                                        {
                                            v = {v.x, v.w - (v.w - v.y) * fillAmount, v.z, v.w};
                                            ty0 = ty1 - fill;
                                        }
                                        else // Bottom
                                        {
                                            v = {v.x, v.y, v.z, v.y + (v.w - v.y) * fillAmount};
                                            ty1 = ty0 + fill;
                                        }
                                    }
                                }

                                std::array<float, 8> xy{
                                    v.x, v.y,
                                    v.x, v.w,
                                    v.z, v.w,
                                    v.z, v.y
                                };
                                std::array<float, 8> uv{
                                    tx0, ty0,
                                    tx0, ty1,
                                    tx1, ty1,
                                    tx1, ty0
                                };

                                if (fillAmount < 1.0f &&
                                    fillMethod != ECS::Components::CUIImage::FillMethod::Horizontal &&
                                    fillMethod != ECS::Components::CUIImage::FillMethod::Vertical)
                                {
                                    if (fillMethod == ECS::Components::CUIImage::FillMethod::Radial90)
                                    {
                                        if (RadialCut(xy, uv, fillAmount, clockwise, origin))
                                            AddFilledQuad(output, xy, color, uv);
                                    }
                                    else if (fillMethod == ECS::Components::CUIImage::FillMethod::Radial180)
                                    {
                                        for (int side = 0; side < 2; ++side)
                                        {
                                            float fx0, fx1, fy0, fy1;
                                            const int even = origin > 1 ? 1 : 0;
                                            if (origin == 0 || origin == 2)
                                            {
                                                fy0 = 0.0f; fy1 = 1.0f;
                                                if (side == even) { fx0 = 0.0f; fx1 = 0.5f; } else { fx0 = 0.5f; fx1 = 1.0f; }
                                            }
                                            else
                                            {
                                                fx0 = 0.0f; fx1 = 1.0f;
                                                if (side == even) { fx0 = 0.0f; fx1 = 0.5f; } else { fx0 = 0.5f; fx1 = 1.0f; }
                                            }
                                            FillQuadrant(xy, uv, v, outer, fx0, fy0, fx1, fy1);
                                            const float val = fillAmount * 2.0f - static_cast<float>(side);
                                            if (RadialCut(xy, uv, std::clamp(val, 0.0f, 1.0f), clockwise, (side + origin + 3) % 4))
                                                AddFilledQuad(output, xy, color, uv);
                                        }
                                    }
                                    else // Radial360
                                    {
                                        for (int corner = 0; corner < 4; ++corner)
                                        {
                                            float fx0, fx1, fy0, fy1;
                                            if (corner < 2) { fx0 = 0.0f; fx1 = 0.5f; } else { fx0 = 0.5f; fx1 = 1.0f; }
                                            if (corner == 0 || corner == 3) { fy0 = 0.0f; fy1 = 0.5f; } else { fy0 = 0.5f; fy1 = 1.0f; }
                                            FillQuadrant(xy, uv, v, outer, fx0, fy0, fx1, fy1);
                                            const float val = clockwise
                                                ? fillAmount * 4.0f - static_cast<float>((corner + origin) % 4)
                                                : fillAmount * 4.0f - static_cast<float>(3 - ((corner + origin) % 4));
                                            if (RadialCut(xy, uv, std::clamp(val, 0.0f, 1.0f), clockwise, (corner + 2) % 4))
                                                AddFilledQuad(output, xy, color, uv);
                                        }
                                    }
                                }
                                else
                                {
                                    AddFilledQuad(output, xy, color, uv);
                                }

                                if (!output.vertices.empty())
                                {
                                    frame.meshes.push_back(std::move(output));
                                }
                            }
                        }
                    }
                }
                if (auto* renderer = actor.GetComponent<CCanvasRenderer>(); renderer && state.drawable && !renderer->IsCulled() &&
                    !renderer->GetMaterialSlots().empty() && !renderer->GetVertices().empty() && !renderer->GetIndices().empty())
                {
                    auto clip = renderer->UsesManagedRectClipping() ? state.canvasClip : state.clip;
                    const auto pivot = transform->GetPivot();
                    const auto origin = rect.min + OvMaths::FVector2{pivot.x * rect.size.x, pivot.y * rect.size.y};
                    const float coordinateScale = state.mode == RenderMode::ScreenSpaceOverlay ? state.scale : 1.0f;
                    if (renderer->HasRectClipping()) {
                        const auto& r = renderer->GetClipRect();
                        clip = Rect::Intersect(clip, {(origin+r.min)*coordinateScale,r.size*coordinateScale});
                    }
                    const auto visible = Rect::Intersect(local, clip);
                    if (!visible.Empty())
                    {
                        Mesh output;
                        renderer->SetAbsoluteDepth(static_cast<int>(sequence));
                        output.actorGuid = actor.GetGUID(); output.sequence = sequence++; output.sourceGeneration = renderer->GetMeshGeneration();
                        output.sortingOrder = state.order; output.renderMode = state.mode; output.clip = clip;
                        output.localOrigin=origin; output.coordinateScale=coordinateScale;
                        if (state.mode != RenderMode::ScreenSpaceOverlay && renderCamera) {
                            output.screenToCanvas=OvMaths::FMatrix4::Inverse(CameraProjection(*renderCamera,size)*CameraView(*renderCamera)*state.canvasToWorld);
                            output.cameraPosition=renderCamera->GetPosition();
                        }
                        output.canvasToWorld = state.canvasToWorld; output.indices = renderer->GetIndices();
                        output.materialPath = renderer->GetMaterialPath(); output.stencil = renderer->GetStencil();
                        output.raycastTarget = state.blocks && renderer->IsRaycastTarget(); output.interactable = state.interactable;
                        output.vertices.reserve(renderer->GetVertices().size());
                        const auto tint = renderer->GetColor();
                        for (auto vertex : renderer->GetVertices())
                        {
                            vertex.position.x = (origin.x + vertex.position.x) * coordinateScale;
                            vertex.position.y = (origin.y + vertex.position.y) * coordinateScale;
                            vertex.position.z *= coordinateScale;
                            vertex.color.x *= tint.x; vertex.color.y *= tint.y; vertex.color.z *= tint.z; vertex.color.w *= state.alpha * tint.w;
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
                        auto slots = [&](const auto& source, auto& target) {
                            for (const auto& slot : source) {
                                MaterialSlot batch;
                                batch.materialPath = slot.materialPath; batch.fontPage = slot.fontPage;
                                // Resolve authored UI materials during Main-thread extraction, before
                                // RenderResources takes immutable shader/property snapshots.
                                if (!batch.materialPath.empty())
                                    if (auto* manager = Global::ServiceLocator::TryGet<ResourceManagement::MaterialManager>())
                                        manager->GetResource(batch.materialPath);
                                batch.firstIndex = slot.explicitRange ? slot.firstIndex : 0;
                                batch.indexCount = slot.explicitRange ? slot.indexCount : static_cast<uint32_t>(output.indices.size());
                                if (batch.firstIndex > output.indices.size() || batch.indexCount > output.indices.size()-batch.firstIndex)
                                    throw std::invalid_argument("Stale UI index range");
                                if (!slot.texturePath.empty())
                                    if (auto* manager = Global::ServiceLocator::TryGet<ResourceManagement::TextureManager>())
                                        if (auto* texture = manager->GetResource(slot.texturePath,false)) batch.texture=texture->RetainTexture();
                                target.push_back(std::move(batch));
                            }
                        };
                        slots(renderer->GetMaterialSlots(), output.materialSlots);
                        slots(renderer->GetPopMaterialSlots(), output.popMaterialSlots);
                        if (renderer->GetPopStencil().enabled || !renderer->GetPopMaterialSlots().empty()) {
                            popDraw=std::make_shared<Mesh>(output);popDraw->stencil=renderer->GetPopStencil();
                            popDraw->pop=true;popDraw->raycastTarget=false;
                            if(!output.popMaterialSlots.empty())popDraw->materialSlots=output.popMaterialSlots;
                        }
                        frame.meshes.push_back(std::move(output));
                    }
                }
                state.parent = rect;
            }
            if (popDraw) { auto exit=state;exit.popDraw=std::move(popDraw);stack.push_back(std::move(exit)); }
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
        const auto hitFrame=std::make_shared<const Frame>(frame);
        for (const auto& mesh : frame.meshes) if (!mesh.pop)
            if (auto* actor=scene.FindActorByGUID(mesh.actorGuid))
                if (auto* renderer=actor->GetComponent<CCanvasRenderer>())renderer->SetHitFrame(hitFrame);
        return frame;
    }
}
