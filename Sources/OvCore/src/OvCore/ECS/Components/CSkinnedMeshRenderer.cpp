/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tracy/Tracy.hpp>
#include <OvTools/Jobs/JobSystem.h>

#include <optional>
#include <string>
#include <tinyxml2.h>

#include <OvCore/ECS/Actor.h>
#include <OvCore/SceneSystem/Scene.h>
#include <OvCore/ECS/Components/CModelRenderer.h>
#include <OvCore/ECS/Components/CSkinnedMeshRenderer.h>
#include <OvCore/Helpers/ReflectionResourceCodecs.h>
#include <OvCore/Helpers/GUIDrawer.h>
#include <OvCore/Helpers/Serializer.h>
#include <OvDebug/Logger.h>
#include <OvMaths/FMatrix3.h>
#include <OvMaths/FTransform.h>
#include <OvUI/Plugins/DataDispatcher.h>
#include <OvUI/Widgets/Selection/ComboBox.h>
#include <OvUI/Widgets/Texts/Text.h>

namespace
{
	float WrapTime(float p_value, float p_duration)
	{
		if (p_duration <= 0.0f)
		{
			return 0.0f;
		}

		const auto wrapped = std::fmod(p_value, p_duration);
		return wrapped < 0.0f ? wrapped + p_duration : wrapped;
	}

	// Read-only spans retain bounds checks without MSVC vector iterator
	// registration on a global debug lock while workers sample shared keys.
	template<typename T, typename TLerp>
	T SampleKeys(
		std::span<const OvRendering::Animation::Keyframe<T>> p_keys,
		float p_time,
		float p_duration,
		const T& p_defaultValue,
		bool p_looping,
		TLerp p_lerp
	)
	{
		if (p_keys.empty())
		{
			return p_defaultValue;
		}

		if (p_keys.size() == 1)
		{
			return p_keys.front().value;
		}

		if (!p_looping)
		{
			if (p_time <= p_keys.front().time) return p_keys.front().value;
			if (p_time >= p_keys.back().time)  return p_keys.back().value;
		}

		const auto nextIt = std::upper_bound(
			p_keys.begin(),
			p_keys.end(),
			p_time,
			[](float p_lhs, const auto& p_rhs) { return p_lhs < p_rhs.time; }
		);

		const auto interpolate = [&](const auto& p_prev, const auto& p_next, float p_segmentDuration)
		{
			if (p_segmentDuration <= std::numeric_limits<float>::epsilon())
			{
				return p_prev.value;
			}

			const float alpha = std::clamp((p_time - p_prev.time) / p_segmentDuration, 0.0f, 1.0f);
			return p_lerp(p_prev.value, p_next.value, alpha);
		};

		if (nextIt == p_keys.end())
		{
			if (!p_looping)
			{
				return p_keys.back().value;
			}

			const auto& prev = p_keys.back();
			const auto& next = p_keys.front();
			return interpolate(prev, next, (p_duration - prev.time) + next.time);
		}

		if (nextIt == p_keys.begin())
		{
			return nextIt->value;
		}

		const auto& prev = *std::prev(nextIt);
		const auto& next = *nextIt;
		return interpolate(prev, next, next.time - prev.time);
	}

	void DecomposeTransform(
		const OvMaths::FMatrix4& p_matrix,
		OvMaths::FVector3& p_position,
		OvMaths::FQuaternion& p_rotation,
		OvMaths::FVector3& p_scale
	)
	{
		p_position.x = p_matrix.data[3];
		p_position.y = p_matrix.data[7];
		p_position.z = p_matrix.data[11];

		OvMaths::FVector3 columns[3] =
		{
			{ p_matrix.data[0], p_matrix.data[4], p_matrix.data[8] },
			{ p_matrix.data[1], p_matrix.data[5], p_matrix.data[9] },
			{ p_matrix.data[2], p_matrix.data[6], p_matrix.data[10] }
		};

		p_scale.x = OvMaths::FVector3::Length(columns[0]);
		p_scale.y = OvMaths::FVector3::Length(columns[1]);
		p_scale.z = OvMaths::FVector3::Length(columns[2]);

		if (p_scale.x > 0.0f)
		{
			columns[0] /= p_scale.x;
		}

		if (p_scale.y > 0.0f)
		{
			columns[1] /= p_scale.y;
		}

		if (p_scale.z > 0.0f)
		{
			columns[2] /= p_scale.z;
		}

		const OvMaths::FMatrix3 rotationMatrix(
			columns[0].x, columns[1].x, columns[2].x,
			columns[0].y, columns[1].y, columns[2].y,
			columns[0].z, columns[1].z, columns[2].z
		);

		p_rotation = OvMaths::FQuaternion(rotationMatrix);
	}

	bool AreMatricesClose(const OvMaths::FMatrix4& p_left, const OvMaths::FMatrix4& p_right)
	{
		constexpr float kSkeletonTransformTolerance = 0.0001f;

		for (uint8_t i = 0; i < 16; ++i)
		{
			if (std::abs(p_left.data[i] - p_right.data[i]) > kSkeletonTransformTolerance)
			{
				return false;
			}
		}

		return true;
	}

	bool CollectRequiredSkinningNodes(
		const OvRendering::Animation::Skeleton& p_skeleton,
		std::vector<bool>& p_requiredNodes
	)
	{
		p_requiredNodes.assign(p_skeleton.nodes.size(), false);

		for (const auto& bone : p_skeleton.bones)
		{
			if (bone.nodeIndex >= p_skeleton.nodes.size())
			{
				return false;
			}

			int32_t nodeIndex = static_cast<int32_t>(bone.nodeIndex);
			while (nodeIndex >= 0)
			{
				const auto currentNodeIndex = static_cast<size_t>(nodeIndex);
				if (currentNodeIndex >= p_skeleton.nodes.size())
				{
					return false;
				}

				if (p_requiredNodes[currentNodeIndex])
				{
					break;
				}

				p_requiredNodes[currentNodeIndex] = true;
				nodeIndex = p_skeleton.nodes[currentNodeIndex].parentIndex;
			}
		}

		return true;
	}

	bool BuildAnimationNodeMap(
		const OvRendering::Animation::Skeleton& p_targetSkeleton,
		const OvRendering::Animation::Skeleton& p_sourceSkeleton,
		std::vector<int32_t>& p_nodeMap
	)
	{
		p_nodeMap.assign(p_sourceSkeleton.nodes.size(), -1);

		if (p_targetSkeleton.nodes.empty() || p_sourceSkeleton.nodes.empty() || p_targetSkeleton.bones.empty())
		{
			return false;
		}

		std::vector<bool> requiredNodes;
		if (!CollectRequiredSkinningNodes(p_targetSkeleton, requiredNodes))
		{
			return false;
		}

		for (size_t targetNodeIndex = 0; targetNodeIndex < requiredNodes.size(); ++targetNodeIndex)
		{
			if (!requiredNodes[targetNodeIndex])
			{
				continue;
			}

			const auto& targetNode = p_targetSkeleton.nodes[targetNodeIndex];
			const auto sourceNodeIndex = p_sourceSkeleton.FindNodeIndex(targetNode.name);
			if (!sourceNodeIndex.has_value() || *sourceNodeIndex >= p_sourceSkeleton.nodes.size())
			{
				return false;
			}

			const auto& sourceNode = p_sourceSkeleton.nodes[*sourceNodeIndex];
			const int32_t targetParentIndex = targetNode.parentIndex;
			const int32_t sourceParentIndex = sourceNode.parentIndex;
			const bool hasRequiredTargetParent =
				targetParentIndex >= 0 &&
				static_cast<size_t>(targetParentIndex) < requiredNodes.size() &&
				requiredNodes[static_cast<size_t>(targetParentIndex)];

			if (targetParentIndex >= 0 && static_cast<size_t>(targetParentIndex) >= requiredNodes.size())
			{
				return false;
			}

			if (hasRequiredTargetParent)
			{
				if (
					sourceParentIndex < 0 ||
					static_cast<size_t>(sourceParentIndex) >= p_sourceSkeleton.nodes.size() ||
					p_sourceSkeleton.nodes[static_cast<size_t>(sourceParentIndex)].name != p_targetSkeleton.nodes[static_cast<size_t>(targetParentIndex)].name
				)
				{
					return false;
				}
			}
			else if (sourceParentIndex >= 0)
			{
				return false;
			}

			if (!AreMatricesClose(targetNode.localBindTransform, sourceNode.localBindTransform))
			{
				return false;
			}

			p_nodeMap[*sourceNodeIndex] = static_cast<int32_t>(targetNodeIndex);
		}

		if (!p_sourceSkeleton.bones.empty())
		{
			if (p_sourceSkeleton.bones.size() != p_targetSkeleton.bones.size())
			{
				return false;
			}

			for (const auto& targetBone : p_targetSkeleton.bones)
			{
				const auto sourceBoneIndex = p_sourceSkeleton.FindBoneIndex(targetBone.name);
				if (!sourceBoneIndex.has_value() || *sourceBoneIndex >= p_sourceSkeleton.bones.size())
				{
					return false;
				}

				const auto& sourceBone = p_sourceSkeleton.bones[*sourceBoneIndex];
				if (
					sourceBone.nodeIndex >= p_nodeMap.size() ||
					p_nodeMap[sourceBone.nodeIndex] != static_cast<int32_t>(targetBone.nodeIndex)
				)
				{
					return false;
				}
			}
		}

		return true;
	}
}

OvCore::ECS::Components::CSkinnedMeshRenderer::CSkinnedMeshRenderer(ECS::Actor& p_owner) :
	AComponent(p_owner)
{
	m_animationSourceChangedEvent += [this]()
	{
		RebuildRuntimeData();
	};

	NotifyModelChanged();
}

std::string OvCore::ECS::Components::CSkinnedMeshRenderer::GetName()
{
	return "Skinned Mesh Renderer";
}

std::string OvCore::ECS::Components::CSkinnedMeshRenderer::GetTypeName()
{
	return std::string{ ComponentTraits<CSkinnedMeshRenderer>::Name };
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::NotifyModelChanged()
{
	m_model = nullptr;
	SyncWithModel();
}

bool OvCore::ECS::Components::CSkinnedMeshRenderer::HasSkinningData() const
{
	return HasCompatibleModel() &&
		!m_boneMatrices.empty() &&
		((m_animationIndex.has_value() && HasCompatibleAnimationSource()) || m_manualPoseOverride);
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::Play()
{
	m_playing = true;
	m_poseEvaluationAccumulator = 0.0f;
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::Pause()
{
	m_playing = false;
	m_poseEvaluationAccumulator = 0.0f;
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::Stop()
{
	m_playing = false;
	m_currentTimeTicks = 0.0f;
	m_poseEvaluationAccumulator = 0.0f;
	EvaluatePose();
}

bool OvCore::ECS::Components::CSkinnedMeshRenderer::IsPlaying() const
{
	return m_playing;
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::SetLooping(bool p_value)
{
	m_looping = p_value;
}

bool OvCore::ECS::Components::CSkinnedMeshRenderer::IsLooping() const
{
	return m_looping;
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::SetPlaybackSpeed(float p_value)
{
	m_playbackSpeed = p_value;
}

float OvCore::ECS::Components::CSkinnedMeshRenderer::GetPlaybackSpeed() const
{
	return m_playbackSpeed;
}

float OvCore::ECS::Components::CSkinnedMeshRenderer::GetMeshBoundsScale() const
{
	return m_meshBoundsScale;
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::SetMeshBoundsScale(float p_scale)
{
	m_meshBoundsScale = std::max(1.0f, p_scale);
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::SetTime(float p_timeSeconds)
{
	if (!HasCompatibleAnimationSource() || !m_animationIndex.has_value())
	{
		return;
	}

	const auto& animation = GetAnimationModel()->GetAnimations().at(*m_animationIndex);
	const float ticksPerSecond = animation.GetEffectiveTicksPerSecond();

	m_currentTimeTicks = p_timeSeconds * ticksPerSecond;
	if (m_looping)
	{
		m_currentTimeTicks = WrapTime(m_currentTimeTicks, animation.duration);
	}
	else
	{
		m_currentTimeTicks = std::clamp(m_currentTimeTicks, 0.0f, animation.duration);
	}

	m_poseEvaluationAccumulator = 0.0f;
	EvaluatePose();
}

float OvCore::ECS::Components::CSkinnedMeshRenderer::GetTime() const
{
	if (!HasCompatibleAnimationSource() || !m_animationIndex.has_value())
	{
		return 0.0f;
	}

	const auto& animation = GetAnimationModel()->GetAnimations().at(*m_animationIndex);
	const float ticksPerSecond = animation.GetEffectiveTicksPerSecond();
	return m_currentTimeTicks / ticksPerSecond;
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::SetAnimationSourceModel(OvRendering::Resources::Model* p_model)
{
	m_animationSourcePath.clear();
	if (m_animationSourceModel == p_model)
	{
		return;
	}

	m_animationSourceModel = p_model;
	RebuildRuntimeData();
}

OvRendering::Resources::Model* OvCore::ECS::Components::CSkinnedMeshRenderer::GetAnimationSourceModel() const
{
	return m_animationSourceModel;
}

bool OvCore::ECS::Components::CSkinnedMeshRenderer::IsAnimationSourceCompatible() const
{
	return HasCompatibleAnimationSource();
}

uint32_t OvCore::ECS::Components::CSkinnedMeshRenderer::GetAnimationCount() const
{
	return static_cast<uint32_t>(m_animationNames.size());
}

std::optional<std::string> OvCore::ECS::Components::CSkinnedMeshRenderer::GetAnimationName(uint32_t p_index) const
{
	if (p_index < m_animationNames.size())
	{
		return m_animationNames[p_index];
	}

	return std::nullopt;
}

bool OvCore::ECS::Components::CSkinnedMeshRenderer::SetAnimation(std::optional<uint32_t> p_index)
{
	if (!p_index.has_value())
	{
		m_animationIndex = std::nullopt;
		m_currentTimeTicks = 0.0f;
		m_poseEvaluationAccumulator = 0.0f;
		EvaluatePose();
		return true;
	}

	if (!HasCompatibleAnimationSource() || *p_index >= GetAnimationModel()->GetAnimations().size())
	{
		return false;
	}

	m_animationIndex = p_index;
	m_currentTimeTicks = 0.0f;
	m_poseEvaluationAccumulator = 0.0f;
	EvaluatePose();
	return true;
}

bool OvCore::ECS::Components::CSkinnedMeshRenderer::SetAnimation(const std::string& p_name)
{
	if (!HasCompatibleAnimationSource())
	{
		return false;
	}

	const auto& animations = GetAnimationModel()->GetAnimations();

	const auto found = std::find_if(animations.begin(), animations.end(), [&p_name](const auto& p_animation)
	{
		return p_animation.name == p_name;
	});

	if (found == animations.end())
	{
		return false;
	}

	return SetAnimation(static_cast<uint32_t>(std::distance(animations.begin(), found)));
}

std::optional<uint32_t> OvCore::ECS::Components::CSkinnedMeshRenderer::GetActiveAnimationIndex() const
{
	return m_animationIndex;
}

std::optional<std::string> OvCore::ECS::Components::CSkinnedMeshRenderer::GetActiveAnimationName() const
{
	if (!m_animationIndex.has_value())
	{
		return std::nullopt;
	}

	return GetAnimationName(m_animationIndex.value());
}

uint32_t OvCore::ECS::Components::CSkinnedMeshRenderer::GetBoneCount() const
{
	if (!HasCompatibleModel())
	{
		return 0;
	}

	return static_cast<uint32_t>(m_model->GetSkeleton().value().bones.size());
}

std::optional<std::string> OvCore::ECS::Components::CSkinnedMeshRenderer::GetBoneName(uint32_t p_index) const
{
	if (!HasCompatibleModel())
	{
		return std::nullopt;
	}

	const auto& bones = m_model->GetSkeleton().value().bones;
	if (p_index >= bones.size())
	{
		return std::nullopt;
	}

	return bones[p_index].name;
}

std::optional<uint32_t> OvCore::ECS::Components::CSkinnedMeshRenderer::GetBoneIndex(const std::string& p_name) const
{
	if (!HasCompatibleModel())
	{
		return std::nullopt;
	}

	return m_model->GetSkeleton().value().FindBoneIndex(p_name);
}

std::optional<OvMaths::FVector3> OvCore::ECS::Components::CSkinnedMeshRenderer::GetBoneLocalPosition(uint32_t p_boneIndex) const
{
	const auto nodeIndex = GetNodeIndexFromBoneIndex(p_boneIndex);
	if (!nodeIndex.has_value())
	{
		return std::nullopt;
	}

	OvMaths::FVector3 position;
	OvMaths::FQuaternion rotation;
	OvMaths::FVector3 scale;
	DecomposeTransform(m_localPose[*nodeIndex], position, rotation, scale);
	return position;
}

std::optional<OvMaths::FQuaternion> OvCore::ECS::Components::CSkinnedMeshRenderer::GetBoneLocalRotation(uint32_t p_boneIndex) const
{
	const auto nodeIndex = GetNodeIndexFromBoneIndex(p_boneIndex);
	if (!nodeIndex.has_value())
	{
		return std::nullopt;
	}

	OvMaths::FVector3 position;
	OvMaths::FQuaternion rotation;
	OvMaths::FVector3 scale;
	DecomposeTransform(m_localPose[*nodeIndex], position, rotation, scale);
	return rotation;
}

std::optional<OvMaths::FVector3> OvCore::ECS::Components::CSkinnedMeshRenderer::GetBoneLocalScale(uint32_t p_boneIndex) const
{
	const auto nodeIndex = GetNodeIndexFromBoneIndex(p_boneIndex);
	if (!nodeIndex.has_value())
	{
		return std::nullopt;
	}

	OvMaths::FVector3 position;
	OvMaths::FQuaternion rotation;
	OvMaths::FVector3 scale;
	DecomposeTransform(m_localPose[*nodeIndex], position, rotation, scale);
	return scale;
}

std::optional<OvMaths::FVector3> OvCore::ECS::Components::CSkinnedMeshRenderer::GetBoneGlobalPosition(uint32_t p_boneIndex) const
{
	const auto nodeIndex = GetNodeIndexFromBoneIndex(p_boneIndex);
	if (!nodeIndex.has_value() || *nodeIndex >= m_globalPose.size())
	{
		return std::nullopt;
	}

	OvMaths::FVector3 position;
	OvMaths::FQuaternion rotation;
	OvMaths::FVector3 scale;
	DecomposeTransform(m_globalPose[*nodeIndex], position, rotation, scale);
	return position;
}

std::optional<OvMaths::FQuaternion> OvCore::ECS::Components::CSkinnedMeshRenderer::GetBoneGlobalRotation(uint32_t p_boneIndex) const
{
	const auto nodeIndex = GetNodeIndexFromBoneIndex(p_boneIndex);
	if (!nodeIndex.has_value() || *nodeIndex >= m_globalPose.size())
	{
		return std::nullopt;
	}

	OvMaths::FVector3 position;
	OvMaths::FQuaternion rotation;
	OvMaths::FVector3 scale;
	DecomposeTransform(m_globalPose[*nodeIndex], position, rotation, scale);
	return rotation;
}

std::optional<OvMaths::FVector3> OvCore::ECS::Components::CSkinnedMeshRenderer::GetBoneGlobalScale(uint32_t p_boneIndex) const
{
	const auto nodeIndex = GetNodeIndexFromBoneIndex(p_boneIndex);
	if (!nodeIndex.has_value() || *nodeIndex >= m_globalPose.size())
	{
		return std::nullopt;
	}

	OvMaths::FVector3 position;
	OvMaths::FQuaternion rotation;
	OvMaths::FVector3 scale;
	DecomposeTransform(m_globalPose[*nodeIndex], position, rotation, scale);
	return scale;
}

bool OvCore::ECS::Components::CSkinnedMeshRenderer::SetBoneLocalPosition(uint32_t p_boneIndex, const OvMaths::FVector3& p_position)
{
	const auto nodeIndex = GetNodeIndexFromBoneIndex(p_boneIndex);
	if (!nodeIndex.has_value())
	{
		return false;
	}

	OvMaths::FVector3 currentPosition;
	OvMaths::FQuaternion currentRotation;
	OvMaths::FVector3 currentScale;
	DecomposeTransform(m_localPose[*nodeIndex], currentPosition, currentRotation, currentScale);

	const OvMaths::FTransform transform(p_position, currentRotation, currentScale);
	m_poseScratch.localPose = m_localPose;
	m_poseScratch.localPose[*nodeIndex] = transform.GetLocalMatrix();
	RecomputeBoneMatricesFromLocalPose();
	return true;
}

bool OvCore::ECS::Components::CSkinnedMeshRenderer::SetBoneLocalRotation(uint32_t p_boneIndex, const OvMaths::FQuaternion& p_rotation)
{
	const auto nodeIndex = GetNodeIndexFromBoneIndex(p_boneIndex);
	if (!nodeIndex.has_value())
	{
		return false;
	}

	OvMaths::FVector3 currentPosition;
	OvMaths::FQuaternion currentRotation;
	OvMaths::FVector3 currentScale;
	DecomposeTransform(m_localPose[*nodeIndex], currentPosition, currentRotation, currentScale);

	const OvMaths::FTransform transform(currentPosition, p_rotation, currentScale);
	m_poseScratch.localPose = m_localPose;
	m_poseScratch.localPose[*nodeIndex] = transform.GetLocalMatrix();
	RecomputeBoneMatricesFromLocalPose();
	return true;
}

bool OvCore::ECS::Components::CSkinnedMeshRenderer::SetBoneLocalScale(uint32_t p_boneIndex, const OvMaths::FVector3& p_scale)
{
	const auto nodeIndex = GetNodeIndexFromBoneIndex(p_boneIndex);
	if (!nodeIndex.has_value())
	{
		return false;
	}

	OvMaths::FVector3 currentPosition;
	OvMaths::FQuaternion currentRotation;
	OvMaths::FVector3 currentScale;
	DecomposeTransform(m_localPose[*nodeIndex], currentPosition, currentRotation, currentScale);

	const OvMaths::FTransform transform(currentPosition, currentRotation, p_scale);
	m_poseScratch.localPose = m_localPose;
	m_poseScratch.localPose[*nodeIndex] = transform.GetLocalMatrix();
	RecomputeBoneMatricesFromLocalPose();
	return true;
}

const std::vector<OvMaths::FMatrix4>& OvCore::ECS::Components::CSkinnedMeshRenderer::GetBoneMatricesTransposed() const
{
	return m_boneMatricesTransposed;
}

uint64_t OvCore::ECS::Components::CSkinnedMeshRenderer::GetPoseVersion() const
{
	return m_poseVersion;
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::OnUpdate(float p_deltaTime)
{
	if (PrepareUpdate(p_deltaTime))
	{
		EvaluatePose();
	}
}

bool OvCore::ECS::Components::CSkinnedMeshRenderer::CanBatchUpdate() const
{
	const auto renderer = owner.GetComponent<CModelRenderer>();
	// Resource synchronization can log/rebuild; it remains on the serial path.
	return renderer && renderer->GetModel() == m_model && HasCompatibleModel();
}

bool OvCore::ECS::Components::CSkinnedMeshRenderer::PrepareUpdate(float p_deltaTime)
{
	if (!IsEnabled())
	{
		return false;
	}

	SyncWithModel();

	if (!HasCompatibleModel() || !m_playing)
	{
		return false;
	}

	const float previousTimeTicks = m_currentTimeTicks;
	const bool wasPlaying = m_playing;

	UpdatePlayback(p_deltaTime);

	const bool timeChanged = std::abs(m_currentTimeTicks - previousTimeTicks) > std::numeric_limits<float>::epsilon();
	const bool playbackStateChanged = wasPlaying != m_playing;

	if (timeChanged || playbackStateChanged)
	{
		const float clampedPoseEvaluationRate = std::max(0.0f, m_poseEvaluationRate);
		const bool hasRateLimit = clampedPoseEvaluationRate > std::numeric_limits<float>::epsilon();

		if (hasRateLimit)
		{
			m_poseEvaluationAccumulator += p_deltaTime;
			const float updatePeriod = 1.0f / clampedPoseEvaluationRate;
			if (m_poseEvaluationAccumulator < updatePeriod && !playbackStateChanged)
			{
				return false;
			}

			m_poseEvaluationAccumulator = std::fmod(m_poseEvaluationAccumulator, updatePeriod);
		}
		else
		{
			m_poseEvaluationAccumulator = 0.0f;
		}

		return true;
	}
	return false;
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::UpdateBatch(
	std::span<CSkinnedMeshRenderer* const> p_renderers,
	float p_deltaTime,
	OvTools::Jobs::JobSystem& p_jobs,
	OvCore::SceneSystem::SkinningUpdateStats* p_stats
)
{
	ZoneScopedN("Skinning Batch");
	if (p_renderers.size() < 8 || p_jobs.GetWorkerCount() < 2)
	{
		for (auto* renderer : p_renderers)
		{
			const auto version = renderer->GetPoseVersion();
			renderer->OnUpdate(p_deltaTime);
			if (p_stats && renderer->GetPoseVersion() != version)
			{
				++p_stats->evaluatedInstances;
				p_stats->evaluatedNodes += renderer->m_model->GetSkeleton()->nodes.size();
			}
		}
		return;
	}
	struct PendingPose
	{
		CSkinnedMeshRenderer* renderer;
		PoseEvaluationInput input;
		std::exception_ptr error;
		bool completed = false;
	};
	std::vector<PendingPose> pending;
	pending.reserve(p_renderers.size());
	size_t nodeCount = 0;
	{
		ZoneScopedN("Skinning Batch Prepare");
		for (auto* renderer : p_renderers)
		{
			if (renderer->PrepareUpdate(p_deltaTime))
			{
				pending.push_back({ renderer, renderer->PreparePoseEvaluation(), {}, false });
				nodeCount += pending.back().input.skeleton.nodes.size();
			}
		}
	}

	if (p_stats)
	{
		p_stats->evaluatedInstances += pending.size();
		p_stats->evaluatedNodes += nodeCount;
	}

	// Small batches cannot amortize submission/wakeup costs. See the skinning
	// validation report for the workload used to select these conservative gates.
	if (pending.size() < 8 || nodeCount < 512 || p_jobs.GetWorkerCount() < 2)
	{
		for (auto& pose : pending)
		{
			ComputePose(pose.input, pose.renderer->m_poseScratch);
			pose.renderer->PublishPose(pose.renderer->m_poseScratch, false);
		}
		return;
	}

	if (p_stats)
	{
		++p_stats->parallelBatches;
		p_stats->parallelInstances += pending.size();
	}

	const auto evaluateRange = [&pending](size_t begin, size_t end)
	{
		ZoneScopedN("Skinning Job");
		for (size_t i = begin; i < end; ++i)
		{
			auto& pose = pending[i];
			try
			{
				ComputePose(pose.input, pose.renderer->m_poseScratch);
				pose.completed = true;
			}
			catch (...)
			{
				// Report on the main thread after draining, never through worker UI
				// callbacks. Each instance owns its output and failure slot.
				pose.error = std::current_exception();
			}
		}
	};
	const size_t chunkCount = std::min(pending.size(), size_t(p_jobs.GetWorkerCount()) * 2 + 1);
	std::vector<OvTools::Jobs::JobHandle> handles;
	handles.reserve(chunkCount - 1);
	try
	{
		for (size_t chunk = 1; chunk < chunkCount; ++chunk)
		{
			const size_t begin = pending.size() * chunk / chunkCount;
			const size_t end = pending.size() * (chunk + 1) / chunkCount;
			handles.push_back(p_jobs.Submit([&evaluateRange, begin, end] { evaluateRange(begin, end); }));
			if (p_stats) ++p_stats->submittedJobs;
		}
	}
	catch (...)
	{
		// All captures and borrowed resources must remain alive on unwind too.
		p_jobs.WaitAllPassive(handles);
		throw;
	}
	// Help only with our own explicitly assigned range, not arbitrary queued work.
	evaluateRange(0, pending.size() / chunkCount);
	{
		ZoneScopedN("Skinning Wait");
		p_jobs.WaitAllPassive(handles);
	}

	std::exception_ptr firstError;
	for (auto& pose : pending)
	{
		if (pose.completed)
		{
			pose.renderer->PublishPose(pose.renderer->m_poseScratch, false);
		}
		else if (!firstError)
		{
			firstError = pose.error ? pose.error :
				std::make_exception_ptr(std::runtime_error("Skinning job ended without a pose"));
		}
	}
	if (firstError)
	{
		std::rethrow_exception(firstError);
	}
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::OnInspector(OvUI::Internal::WidgetContainer& p_root)
{
	SyncWithModel();

	using namespace OvCore::Helpers;

	GUIDrawer::DrawBoolean(p_root, "Playing", m_playing);
	GUIDrawer::DrawBoolean(p_root, "Looping", m_looping);
	GUIDrawer::DrawScalar<float>(p_root, "Playback Speed", m_playbackSpeed, 0.01f, -10.0f, 10.0f);
	GUIDrawer::DrawScalar<float>(p_root, "Mesh Bounds Scale", m_meshBoundsScale, 0.05f, 1.0f, 10.0f);
	GUIDrawer::DrawScalar<float>(p_root, "Pose Eval Rate", m_poseEvaluationRate, 1.0f, 0.0f, 240.0f);
	m_poseEvaluationRate = std::max(0.0f, m_poseEvaluationRate);
	GUIDrawer::DrawScalar<float>(
		p_root,
		"Time (Seconds)",
		[this]() { return GetTime(); },
		[this](float p_value) { SetTime(p_value); },
		0.01f,
		0.0f,
		std::max(GetAnimationDurationSeconds(), 3600.0f)
	);

	ReflectionProperties::DrawResourceReference<OvRendering::Resources::Model>(p_root, "Animation Source", InspectorTicket(),
        [this] { return GetSerializedAnimationSource(); }, [this](const auto& value) { SetAnimationSourceModel(value.resource); m_animationSourcePath = value.path; }, OvTools::Utils::PathParser::EFileType::MODEL);

	GUIDrawer::CreateTitle(p_root, "Active Animation");
	const int currentAnimIndex = GetActiveAnimationIndex().has_value() ? static_cast<int>(*GetActiveAnimationIndex()) : -1;
	auto& animationChoice = p_root.CreateWidget<OvUI::Widgets::Selection::ComboBox>(currentAnimIndex);

	auto& animDispatcher = animationChoice.AddPlugin<OvUI::Plugins::DataDispatcher<int>>();
	animDispatcher.RegisterGatherer([this, &animationChoice]
	{
		animationChoice.choices.clear();
		animationChoice.choices.emplace(-1, "<None>");

		for (size_t i = 0; i < m_animationNames.size(); ++i)
		{
			animationChoice.choices.emplace(static_cast<int>(i), m_animationNames[i]);
		}

		return GetActiveAnimationIndex().has_value() ? static_cast<int>(*GetActiveAnimationIndex()) : -1;
	});
	animDispatcher.RegisterProvider([this](int p_choice)
	{
		SetAnimation(p_choice >= 0 ? std::make_optional(static_cast<uint32_t>(p_choice)) : std::nullopt);
	});

	if (!HasCompatibleModel())
	{
		p_root.CreateWidget<OvUI::Widgets::Texts::Text>("No skinned model assigned");
	}
	else if (m_animationSourceModel && !HasCompatibleAnimationSource())
	{
		p_root.CreateWidget<OvUI::Widgets::Texts::Text>("Animation source skeleton is not compatible with model");
	}
	else if (m_animationNames.empty())
	{
		p_root.CreateWidget<OvUI::Widgets::Texts::Text>(m_animationSourceModel ? "Animation source has no animation clips" : "Model has no animation clips");
	}
}

bool OvCore::ECS::Components::CSkinnedMeshRenderer::HasCompatibleModel() const
{
	return m_model && m_model->IsSkinned() && m_model->GetSkeleton().has_value();
}

bool OvCore::ECS::Components::CSkinnedMeshRenderer::HasCompatibleAnimationSource() const
{
	if (!HasCompatibleModel())
	{
		return false;
	}

	const auto animationModel = GetAnimationModel();
	return animationModel && animationModel->GetSkeleton().has_value() && !m_animationNodeMap.empty();
}

const OvRendering::Resources::Model* OvCore::ECS::Components::CSkinnedMeshRenderer::GetAnimationModel() const
{
	return m_animationSourceModel ? m_animationSourceModel : m_model;
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::SyncWithModel()
{
	const auto modelRenderer = owner.GetComponent<CModelRenderer>();
	const auto model = modelRenderer ? modelRenderer->GetModel() : nullptr;

	if (m_model == model)
	{
		return;
	}

	m_model = model;
	RebuildRuntimeData();
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::RebuildRuntimeData()
{
	const float preservedTimeTicks = m_currentTimeTicks;
	const std::optional<uint32_t> preservedAnimationIndex = m_animationIndex;
	const std::string requestedAnimationName = m_deserializedAnimationName;

	m_animationNames.clear();
	m_animationNodeMap.clear();
	// A palette from the previous model is not valid for the replacement. Keep
	// the published arrays empty until its initial pose has been computed.
	m_localPose.clear();
	m_globalPose.clear();
	m_boneMatrices.clear();
	m_boneMatricesTransposed.clear();
	m_animationIndex = std::nullopt;
	m_currentTimeTicks = preservedTimeTicks;
	m_poseEvaluationAccumulator = 0.0f;
	m_manualPoseOverride = false;

	if (!HasCompatibleModel())
	{
		return;
	}

	const auto& skeleton = m_model->GetSkeleton().value();
	const auto animationModel = GetAnimationModel();

	if (
		animationModel &&
		animationModel->GetSkeleton().has_value() &&
		BuildAnimationNodeMap(skeleton, animationModel->GetSkeleton().value(), m_animationNodeMap)
	)
	{
		const auto& animations = animationModel->GetAnimations();

		m_animationNames.reserve(animations.size());
		for (const auto& animation : animations)
		{
			m_animationNames.push_back(animation.name);
		}
	}
	else if (m_animationSourceModel)
	{
		OVLOG_WARNING("SkinnedMeshRenderer: Animation source model '" + m_animationSourceModel->path + "' is not compatible with target model '" + m_model->path + "'.");
	}

	const auto& animations = GetAnimationModel()->GetAnimations();

	if (!m_animationNames.empty())
	{
		if (!requestedAnimationName.empty())
		{
			const auto found = std::find(m_animationNames.begin(), m_animationNames.end(), requestedAnimationName);
			m_animationIndex = found != m_animationNames.end() ?
				std::optional<uint32_t>{ static_cast<uint32_t>(std::distance(m_animationNames.begin(), found)) } :
				std::optional<uint32_t>{ 0 };
		}
		else if (preservedAnimationIndex.has_value() && *preservedAnimationIndex < m_animationNames.size())
		{
			m_animationIndex = *preservedAnimationIndex;
		}
	}

	if (HasCompatibleAnimationSource() && m_animationIndex.has_value() && *m_animationIndex < animations.size())
	{
		const auto& animation = animations.at(*m_animationIndex);
		if (m_looping)
		{
			m_currentTimeTicks = WrapTime(m_currentTimeTicks, animation.duration);
		}
		else
		{
			m_currentTimeTicks = std::clamp(m_currentTimeTicks, 0.0f, animation.duration);
		}
	}
	else
	{
		m_currentTimeTicks = 0.0f;
	}

	m_deserializedAnimationName.clear();
	EvaluatePose();
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::EvaluatePose()
{
	if (!HasCompatibleModel())
	{
		return;
	}

	const auto input = PreparePoseEvaluation();
	ComputePose(input, m_poseScratch);
	PublishPose(m_poseScratch, false);
}

OvCore::ECS::Components::CSkinnedMeshRenderer::PoseEvaluationInput OvCore::ECS::Components::CSkinnedMeshRenderer::PreparePoseEvaluation() const
{
	ZoneScopedN("Skinning Prepare");
	const auto animationModel = GetAnimationModel();
	const bool hasAnimation = HasCompatibleAnimationSource() && m_animationIndex.has_value() &&
		*m_animationIndex < animationModel->GetAnimations().size();

	return {
		m_model->GetSkeleton().value(),
		hasAnimation ? &animationModel->GetAnimations().at(*m_animationIndex) : nullptr,
		hasAnimation ? animationModel->GetSkeleton()->nodes.size() : 0,
		m_animationNodeMap,
		m_currentTimeTicks,
		m_looping
	};
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::ComputePose(const PoseEvaluationInput& p_input, PoseEvaluationOutput& p_output)
{
	ZoneScopedN("Skinning Evaluate");
	const auto& skeleton = p_input.skeleton;
	p_output.localPose.resize(skeleton.nodes.size());

	for (size_t nodeIndex = 0; nodeIndex < skeleton.nodes.size(); ++nodeIndex)
	{
		p_output.localPose[nodeIndex] = skeleton.nodes[nodeIndex].localBindTransform;
	}

	if (p_input.animation)
	{
		const auto& animation = *p_input.animation;
		const float duration = std::max(animation.duration, 0.0f);
		const float sampleTime =
			duration > 0.0f ?
			(p_input.looping ? WrapTime(p_input.timeTicks, duration) : std::clamp(p_input.timeTicks, 0.0f, duration)) :
			0.0f;

		for (const auto& track : std::span{animation.tracks})
		{
			if (track.nodeIndex >= p_input.animationNodeCount || track.nodeIndex >= p_input.animationNodeMap.size())
			{
				continue;
			}

			const int32_t targetNodeIndex = p_input.animationNodeMap[track.nodeIndex];
			if (targetNodeIndex < 0 || static_cast<size_t>(targetNodeIndex) >= p_output.localPose.size())
			{
				continue;
			}

			const auto& node = skeleton.nodes[static_cast<size_t>(targetNodeIndex)];

			const OvMaths::FVector3 sampledPosition = SampleKeys(
				std::span{track.positionKeys},
				sampleTime,
				duration,
				node.bindPosition,
				p_input.looping,
				[](const auto& p_a, const auto& p_b, float p_alpha) { return OvMaths::FVector3::Lerp(p_a, p_b, p_alpha); }
			);

			const OvMaths::FQuaternion sampledRotation = SampleKeys(
				std::span{track.rotationKeys},
				sampleTime,
				duration,
				node.bindRotation,
				p_input.looping,
				[](const auto& p_a, const auto& p_b, float p_alpha) { return OvMaths::FQuaternion::Slerp(p_a, p_b, p_alpha); }
			);

			const OvMaths::FVector3 sampledScale = SampleKeys(
				std::span{track.scaleKeys},
				sampleTime,
				duration,
				node.bindScale,
				p_input.looping,
				[](const auto& p_a, const auto& p_b, float p_alpha) { return OvMaths::FVector3::Lerp(p_a, p_b, p_alpha); }
			);

			// Same local TRS expression and operation order as FTransform, without
			// allocating a notifier or decomposing an unused world transform per track.
			p_output.localPose[static_cast<size_t>(targetNodeIndex)] =
				OvMaths::FMatrix4::Translation(sampledPosition) *
				OvMaths::FQuaternion::ToMatrix4(OvMaths::FQuaternion::Normalize(sampledRotation)) *
				OvMaths::FMatrix4::Scaling(sampledScale);
		}
	}

	ComputeBoneMatrices(skeleton, p_output);
}

std::optional<uint32_t> OvCore::ECS::Components::CSkinnedMeshRenderer::GetNodeIndexFromBoneIndex(uint32_t p_boneIndex) const
{
	if (!HasCompatibleModel())
	{
		return std::nullopt;
	}

	const auto& skeleton = m_model->GetSkeleton().value();
	if (p_boneIndex >= skeleton.bones.size())
	{
		return std::nullopt;
	}

	const uint32_t nodeIndex = skeleton.bones[p_boneIndex].nodeIndex;
	if (nodeIndex >= m_localPose.size())
	{
		return std::nullopt;
	}

	return nodeIndex;
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::RecomputeBoneMatricesFromLocalPose()
{
	// Manual edits have already copied and modified the local pose in scratch.
	ComputeBoneMatrices(m_model->GetSkeleton().value(), m_poseScratch);
	PublishPose(m_poseScratch, true);
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::ComputeBoneMatrices(
	const OvRendering::Animation::Skeleton& p_skeleton,
	PoseEvaluationOutput& p_output
)
{
	const auto& skeleton = p_skeleton;
	if (p_output.localPose.size() != skeleton.nodes.size())
	{
		throw std::invalid_argument("Skinning local pose does not match skeleton");
	}
	p_output.globalPose.resize(skeleton.nodes.size());
	p_output.boneMatrices.resize(skeleton.bones.size());
	p_output.boneMatricesTransposed.resize(skeleton.bones.size());

	for (size_t nodeIndex = 0; nodeIndex < skeleton.nodes.size(); ++nodeIndex)
	{
		const auto parentIndex = skeleton.nodes[nodeIndex].parentIndex;
		if (parentIndex >= 0 && static_cast<size_t>(parentIndex) >= nodeIndex)
		{
			throw std::invalid_argument("Skinning requires parent-before-child skeleton nodes");
		}
		p_output.globalPose[nodeIndex] =
			parentIndex >= 0 ?
			p_output.globalPose[static_cast<size_t>(parentIndex)] * p_output.localPose[nodeIndex] :
			p_output.localPose[nodeIndex];
	}

	for (size_t boneIndex = 0; boneIndex < skeleton.bones.size(); ++boneIndex)
	{
		const auto& bone = skeleton.bones[boneIndex];
		p_output.boneMatrices[boneIndex] =
			bone.nodeIndex < p_output.globalPose.size() ?
			p_output.globalPose[bone.nodeIndex] * bone.offsetMatrix :
			OvMaths::FMatrix4::Identity;

		p_output.boneMatricesTransposed[boneIndex] = OvMaths::FMatrix4::Transpose(p_output.boneMatrices[boneIndex]);
	}
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::PublishPose(PoseEvaluationOutput& p_output, bool p_manualOverride) noexcept
{
	ZoneScopedN("Skinning Publish");
	// Main thread only, before returning to any component/script/render consumer.
	// Swaps cannot throw; failed computation leaves the last published pose intact.
	m_localPose.swap(p_output.localPose);
	m_globalPose.swap(p_output.globalPose);
	m_boneMatrices.swap(p_output.boneMatrices);
	m_boneMatricesTransposed.swap(p_output.boneMatricesTransposed);
	m_manualPoseOverride = p_manualOverride;
	++m_poseVersion;
}

float OvCore::ECS::Components::CSkinnedMeshRenderer::GetAnimationDurationSeconds() const
{
	if (!HasCompatibleAnimationSource() || !m_animationIndex.has_value())
	{
		return 0.0f;
	}

	const auto& animation = GetAnimationModel()->GetAnimations().at(*m_animationIndex);
	return animation.GetDurationSeconds();
}

void OvCore::ECS::Components::CSkinnedMeshRenderer::UpdatePlayback(float p_deltaTime)
{
	if (!HasCompatibleAnimationSource() || !m_animationIndex.has_value())
	{
		return;
	}

	const auto& animation = GetAnimationModel()->GetAnimations().at(*m_animationIndex);
	if (animation.duration <= 0.0f)
	{
		return;
	}

	if (std::abs(m_playbackSpeed) <= std::numeric_limits<float>::epsilon())
	{
		return;
	}

	const float ticksPerSecond = animation.GetEffectiveTicksPerSecond();
	m_currentTimeTicks += p_deltaTime * ticksPerSecond * m_playbackSpeed;

	if (m_looping)
	{
		m_currentTimeTicks = WrapTime(m_currentTimeTicks, animation.duration);
	}
	else
	{
		const float clamped = std::clamp(m_currentTimeTicks, 0.0f, animation.duration);
		const bool reachedStart = clamped <= 0.0f && m_playbackSpeed < 0.0f;
		const bool reachedEnd = clamped >= animation.duration && m_playbackSpeed > 0.0f;
		m_currentTimeTicks = clamped;
		if (reachedStart || reachedEnd)
		{
			m_playing = false;
		}
	}
}

OvCore::Resources::ResourceReference<OvRendering::Resources::Model> OvCore::ECS::Components::CSkinnedMeshRenderer::GetSerializedAnimationSource() const
{ return Resources::ResourceReference<OvRendering::Resources::Model>::From(m_animationSourceModel, m_animationSourcePath); }
void OvCore::ECS::Components::CSkinnedMeshRenderer::SetSerializedAnimationSource(const Resources::ResourceReference<OvRendering::Resources::Model>& value)
{ m_animationSourceModel = value.resource; m_animationSourcePath = value.path; }
std::string OvCore::ECS::Components::CSkinnedMeshRenderer::GetSerializedAnimation() const { return GetActiveAnimationName().value_or(m_deserializedAnimationName); }
void OvCore::ECS::Components::CSkinnedMeshRenderer::SetSerializedAnimation(const std::string& name) { m_deserializedAnimationName = name; }
void OvCore::ECS::Components::CSkinnedMeshRenderer::RestoreAnimationState()
{
    SetMeshBoundsScale(m_meshBoundsScale);
    m_poseEvaluationRate = std::max(0.0f, m_poseEvaluationRate);
    m_poseEvaluationAccumulator = 0.0f;
    NotifyModelChanged();
}
