/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <cmath>
#include "OvEditor/Core/GizmoMath.h"

#include "OvEditor/Core/GizmoBehaviour.h"
#include "OvEditor/Core/EditorActions.h"
#include "OvEditor/Settings/EditorSettings.h"

namespace {
    struct MouseRay { OvMaths::FVector3 origin, direction; };
    MouseRay UnprojectRay(const OvMaths::FVector2& mouse, const OvMaths::FMatrix4& view,
        const OvMaths::FMatrix4& projection, const OvMaths::FVector2& size)
    {
        const auto inverse = OvMaths::FMatrix4::Inverse(view) * OvMaths::FMatrix4::Inverse(projection);
        const float x = 2 * mouse.x / size.x - 1, y = 1 - 2 * mouse.y / size.y;
        const auto nearPoint = inverse * OvMaths::FVector4{x,y,-1,1};
        const auto farPoint = inverse * OvMaths::FVector4{x,y,1,1};
        const OvMaths::FVector3 origin = OvMaths::FVector3{nearPoint.x,nearPoint.y,nearPoint.z} / nearPoint.w;
        const auto end = OvMaths::FVector3{farPoint.x,farPoint.y,farPoint.z} / farPoint.w;
        return {origin, OvMaths::FVector3::Normalize(end-origin)};
    }
}

bool OvEditor::Core::GizmoBehaviour::IsSnappedBehaviourEnabled() const
{
    if (m_snapSettings) return m_snapSettings->enabled;
	using namespace OvWindowing::Inputs;

	const auto& inputManager = EDITOR_CONTEXT(inputManager);
	return inputManager->GetKeyState(EKey::KEY_LEFT_CONTROL) == EKeyState::KEY_DOWN || inputManager->GetKeyState(EKey::KEY_RIGHT_CONTROL) == EKeyState::KEY_DOWN;
}

void OvEditor::Core::GizmoBehaviour::StartPicking(OvCore::ECS::Actor& p_target, const OvMaths::FVector3& p_cameraPosition, EGizmoOperation p_operation, EDirection p_direction, std::optional<Frame> frame)
{
	m_target = &p_target;
	m_firstMouse = true;
	m_firstPick = true;
	m_screenDrag = false;
	m_rotationAngle = 0.0f;
	m_originalTransform = p_target.transform.GetFTransform();
    m_frame = frame.value_or(Frame{m_originalTransform.GetWorldPosition(),m_originalTransform.GetWorldRotation()});
	m_currentOperation = p_operation;
	m_direction = p_direction;
}

void OvEditor::Core::GizmoBehaviour::StopPicking()
{
	m_target = nullptr;
}

void OvEditor::Core::GizmoBehaviour::CancelPicking()
{
    if (!m_target) return;
    m_target->transform.SetLocalPosition(m_originalTransform.GetLocalPosition());
    m_target->transform.SetLocalRotation(m_originalTransform.GetLocalRotation());
    m_target->transform.SetLocalScale(m_originalTransform.GetLocalScale());
    StopPicking();
}

OvMaths::FVector3 OvEditor::Core::GizmoBehaviour::GetFakeDirection() const
{
	auto result = OvMaths::FVector3();

	switch (m_direction)
	{
	case OvEditor::Core::GizmoBehaviour::EDirection::X:
		result = OvMaths::FVector3::Right;
		break;
	case OvEditor::Core::GizmoBehaviour::EDirection::Y:
		result = OvMaths::FVector3::Up;
		break;
	case OvEditor::Core::GizmoBehaviour::EDirection::Z:
		result = OvMaths::FVector3::Forward;
		break;
	}

	return result;
}

OvMaths::FVector3 OvEditor::Core::GizmoBehaviour::GetRealDirection(bool) const
{
    return m_frame.rotation * GetFakeDirection();
}

OvMaths::FVector2 OvEditor::Core::GizmoBehaviour::GetScreenDirection(const OvMaths::FMatrix4& p_viewMatrix, const OvMaths::FMatrix4& p_projectionMatrix, const OvMaths::FVector2& p_viewSize) const
{
	auto start = m_frame.position;
	auto end = start + GetRealDirection(true) * GizmoMath::WorldSize(start, p_viewMatrix, p_projectionMatrix, p_viewSize.y, 90.0f);

	auto start2D = OvMaths::FVector2();
	{
		auto clipSpacePos = p_projectionMatrix * (p_viewMatrix * OvMaths::FVector4{ start.x, start.y, start.z, 1.0f });
		if (std::abs(clipSpacePos.w) < 0.0001f) return {0, -1};
		auto ndcSpacePos = OvMaths::FVector3{ clipSpacePos.x, clipSpacePos.y, clipSpacePos.z } / clipSpacePos.w;
		auto windowSpacePos = ((OvMaths::FVector2{ ndcSpacePos.x, ndcSpacePos.y } + 1.0f) / 2.0f);
		windowSpacePos.x *= p_viewSize.x;
		windowSpacePos.y *= p_viewSize.y;
		start2D = windowSpacePos;
	}

	auto end2D = OvMaths::FVector2();
	{
		auto clipSpacePos = p_projectionMatrix * (p_viewMatrix * OvMaths::FVector4{ end.x, end.y, end.z, 1.0f });
		if (std::abs(clipSpacePos.w) < 0.0001f) return {0, -1};
		auto ndcSpacePos = OvMaths::FVector3{ clipSpacePos.x, clipSpacePos.y, clipSpacePos.z } / clipSpacePos.w;
		auto windowSpacePos = ((OvMaths::FVector2{ ndcSpacePos.x, ndcSpacePos.y } + 1.0f) / 2.0f);
		windowSpacePos.x *= p_viewSize.x;
		windowSpacePos.y *= p_viewSize.y;
		end2D = windowSpacePos;
	}

	auto result = end2D - start2D;

	result.y *= -1; // Screen coordinates are reversed, so we inverse the Y

	const float lengthSquared = OvMaths::FVector2::Dot(result, result);
	return lengthSquared > 0.000001f ? result / std::sqrt(lengthSquared) : OvMaths::FVector2{0, -1};
}

void OvEditor::Core::GizmoBehaviour::ApplyTranslation(const OvMaths::FMatrix4& p_viewMatrix, const OvMaths::FMatrix4& p_projectionMatrix, const OvMaths::FVector3& p_cameraPosition, const OvMaths::FVector2& p_viewSize)
{
	const auto mouseRay = UnprojectRay(m_currentMouse, p_viewMatrix, p_projectionMatrix, p_viewSize);
	const auto ray = mouseRay.direction;

    if (m_firstPick) {
        const auto axis = GetRealDirection(true);
        m_screenDrag = std::abs(OvMaths::FVector3::Dot(axis,ray)) > 0.98f;
        m_dragDirection = m_screenDrag ? OvMaths::FVector2{0,-1} : GetScreenDirection(p_viewMatrix,p_projectionMatrix,p_viewSize);
        const auto tangent = OvMaths::FVector3::Cross(axis,ray);
        if (!m_screenDrag) m_dragPlaneNormal = OvMaths::FVector3::Normalize(OvMaths::FVector3::Cross(axis,tangent));
    }
    if (m_screenDrag) {
        m_firstPick = false;
        float displacement = OvMaths::FVector2::Dot(m_currentMouse-m_originMouse,m_dragDirection) *
            GizmoMath::WorldSize(m_frame.position,p_viewMatrix,p_projectionMatrix,p_viewSize.y,1);
        if (IsSnappedBehaviourEnabled()) displacement=GizmoMath::Snap(displacement,
            m_snapSettings ? m_snapSettings->translation : Settings::EditorSettings::TranslationSnapUnit.Get());
        m_target->transform.SetWorldPosition(m_originalTransform.GetWorldPosition()+GetRealDirection(true)*displacement);
        return;
    }
    const auto planeNormal = m_dragPlaneNormal;

	OvMaths::FVector3 direction = GetRealDirection(true);

	OvMaths::FVector3 planePoint = m_frame.position;

	const float denom = OvMaths::FVector3::Dot(ray, planeNormal);

	if (std::abs(denom) <= 0.001f)
		return;

	const float t = OvMaths::FVector3::Dot(planePoint - mouseRay.origin, planeNormal) / denom;

	if (t <= 0.001f)
		return;

	OvMaths::FVector3 point = mouseRay.origin + ray * t;

	if (m_firstPick)
	{
		m_initialOffset = m_frame.position - point;
		m_firstPick = false;
	}

	const auto translationVector = point - planePoint + m_initialOffset;
	float displacement = OvMaths::FVector3::Dot(translationVector, direction);

	if (IsSnappedBehaviourEnabled())
	{
		displacement = GizmoMath::Snap(displacement, (m_snapSettings ? m_snapSettings->translation : static_cast<float>(OvEditor::Settings::EditorSettings::TranslationSnapUnit)));
	}

	OvMaths::FVector3 projectedPoint = planePoint + direction * displacement;

	m_target->transform.SetWorldPosition(m_originalTransform.GetWorldPosition() + projectedPoint - m_frame.position);
}

void OvEditor::Core::GizmoBehaviour::ApplyRotation(const OvMaths::FMatrix4& view, const OvMaths::FMatrix4& projection, const OvMaths::FVector2& size)
{
    const auto pivot = m_frame.position;
    const auto axis = GetRealDirection(true);
    auto rotate = [&](float angle) {
        const auto delta = m_frame.rotation * OvMaths::FQuaternion(GetFakeDirection()*angle) * OvMaths::FQuaternion::Inverse(m_frame.rotation);
        m_target->transform.SetWorldRotation(delta * m_originalTransform.GetWorldRotation());
        m_target->transform.SetWorldPosition(pivot + delta * (m_originalTransform.GetWorldPosition() - pivot));
    };
    const auto ray = UnprojectRay(m_currentMouse, view, projection, size);
    const float denominator = OvMaths::FVector3::Dot(ray.direction, axis);
    if (m_firstPick) {
        // Lock the interaction mode for the whole drag to avoid jumps at the threshold.
        m_screenDrag = std::abs(denominator) < 0.15f;
        if (m_screenDrag) {
            const auto projected = GetScreenDirection(view,projection,size);
            m_dragDirection = {-projected.y,projected.x};
            m_firstPick = false;
        }
    }
    if (m_screenDrag) {
        float angle = OvMaths::FVector2::Dot(m_currentMouse-m_originMouse,m_dragDirection)*0.5f;
        if (IsSnappedBehaviourEnabled()) angle=GizmoMath::Snap(angle,
            m_snapSettings ? m_snapSettings->rotation : Settings::EditorSettings::RotationSnapUnit.Get());
        rotate(angle);
        return;
    }
    if (std::abs(denominator) < 0.001f) return;
    const float distance = OvMaths::FVector3::Dot(pivot-ray.origin, axis) / denominator;
    if (distance <= 0) return;
    const auto offset = ray.origin + ray.direction * distance - pivot;
    if (OvMaths::FVector3::Dot(offset,offset) < 0.000001f) return;
    const auto direction = OvMaths::FVector3::Normalize(offset);
    if (m_firstPick) { m_initialOffset = direction; m_firstPick = false; return; }
    // Incremental angles cross +/-180 degrees continuously and allow multiple turns.
    m_rotationAngle += std::atan2(OvMaths::FVector3::Dot(axis, OvMaths::FVector3::Cross(m_initialOffset,direction)),
        OvMaths::FVector3::Dot(m_initialOffset,direction)) * 57.2957795f;
    m_initialOffset = direction;
    const float angle = IsSnappedBehaviourEnabled() ? GizmoMath::Snap(m_rotationAngle,
        m_snapSettings ? m_snapSettings->rotation : Settings::EditorSettings::RotationSnapUnit.Get()) : m_rotationAngle;
    rotate(angle);
}

void OvEditor::Core::GizmoBehaviour::ApplyScale(const OvMaths::FMatrix4& view, const OvMaths::FMatrix4& projection,
    const OvMaths::FVector3&, const OvMaths::FVector2& size)
{
    const auto direction = GetScreenDirection(view, projection, size);
    float delta = OvMaths::FVector2::Dot(m_currentMouse-m_originMouse, direction) / 100.0f;
    if (IsSnappedBehaviourEnabled()) delta = GizmoMath::Snap(delta,
        m_snapSettings ? m_snapSettings->scale : Settings::EditorSettings::ScalingSnapUnit.Get());
    auto scale = m_originalTransform.GetLocalScale() + GetFakeDirection() * delta;
    scale.x = std::max(scale.x, 0.0001f);
    scale.y = std::max(scale.y, 0.0001f);
    scale.z = std::max(scale.z, 0.0001f);
    m_target->transform.SetLocalScale(scale);
    const auto original = m_originalTransform.GetLocalScale();
    auto offset = OvMaths::FQuaternion::Inverse(m_frame.rotation) * (m_originalTransform.GetWorldPosition() - m_frame.position);
    offset = {offset.x * (std::abs(original.x)>1e-6f ? scale.x/original.x : 1.f),
        offset.y * (std::abs(original.y)>1e-6f ? scale.y/original.y : 1.f),
        offset.z * (std::abs(original.z)>1e-6f ? scale.z/original.z : 1.f)};
    m_target->transform.SetWorldPosition(m_frame.position + m_frame.rotation * offset);
}

void OvEditor::Core::GizmoBehaviour::ApplyOperation(const OvMaths::FMatrix4& p_viewMatrix, const OvMaths::FMatrix4& p_projectionMatrix, const OvMaths::FVector3& p_cameraPosition, const OvMaths::FVector2& p_viewSize, std::optional<SnapSettings> p_snapping)
{
    if (!m_target || p_viewSize.x <= 0 || p_viewSize.y <= 0) return;
    m_snapSettings = p_snapping;
    struct Reset { std::optional<SnapSettings>& value; ~Reset() { value.reset(); } } reset{m_snapSettings};
	switch (m_currentOperation)
	{
	case EGizmoOperation::TRANSLATE:
		ApplyTranslation(p_viewMatrix, p_projectionMatrix, p_cameraPosition, p_viewSize);
		break;

	case EGizmoOperation::ROTATE:
		ApplyRotation(p_viewMatrix, p_projectionMatrix, p_viewSize);
		break;

	case EGizmoOperation::SCALE:
		ApplyScale(p_viewMatrix, p_projectionMatrix, p_cameraPosition, p_viewSize);
		break;
	}
}

void OvEditor::Core::GizmoBehaviour::SetCurrentMouse(const OvMaths::FVector2& p_mousePosition)
{
	if (m_firstMouse)
	{
		m_currentMouse = m_originMouse = p_mousePosition;
		m_firstMouse = false;
	}
	else
	{
		m_currentMouse = p_mousePosition;
	}
}

bool OvEditor::Core::GizmoBehaviour::IsPicking() const
{
	return m_target;
}

OvEditor::Core::GizmoBehaviour::EDirection OvEditor::Core::GizmoBehaviour::GetDirection() const
{
	return m_direction;
}

OvMaths::FVector3 OvEditor::Core::GizmoBehaviour::GetMouseRay(const OvMaths::FVector2& p_mousePos, const OvMaths::FMatrix4& p_viewMatrix, const OvMaths::FMatrix4& p_projectionMatrix, const OvMaths::FVector2& p_viewSize)
{
    if (p_viewSize.x <= 0 || p_viewSize.y <= 0) return {};
    return UnprojectRay(p_mousePos,p_viewMatrix,p_projectionMatrix,p_viewSize).direction;
}
