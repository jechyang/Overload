/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <chrono>
#include <map>

#include <OvRendering/Core/CompositeRenderer.h>
#include <OvRendering/Data/Frustum.h>
#include <OvRendering/Entities/Drawable.h>
#include <OvRendering/HAL/UniformBuffer.h>
#include <OvRendering/HAL/ShaderStorageBuffer.h>
#include <OvRendering/Resources/Mesh.h>

#include <OvCore/ECS/Actor.h>
#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/Rendering/EVisibilityFlags.h>
#include <OvCore/Rendering/PostProcessRenderPass.h>
#include <OvCore/Resources/Material.h>
#include <OvCore/SceneSystem/Scene.h>

namespace OvCore::Rendering
{
	/**
	* Extension of the CompositeRenderer adding support for the scene system (parsing/drawing entities)
	*/
	class SceneRenderer : public OvRendering::Core::CompositeRenderer
	{
	public:
		enum class EOrderingMode
		{
			BACK_TO_FRONT,
			FRONT_TO_BACK,
		};

		template<EOrderingMode OrderingMode>
		struct DrawOrder
		{
			const int order;
			const float distance;

			bool operator<(const DrawOrder& p_other) const
			{
				if (order == p_other.order)
				{
					if constexpr (OrderingMode == EOrderingMode::BACK_TO_FRONT)
						return distance > p_other.distance;
					else
						return distance < p_other.distance;
				}
				return order < p_other.order;
			}
		};

		template<EOrderingMode OrderingMode>
		using DrawableMap = std::multimap<DrawOrder<OrderingMode>, OvRendering::Entities::Drawable>;

		struct SceneDescriptor
		{
			OvCore::SceneSystem::Scene& scene;
			OvTools::Utils::OptRef<const OvRendering::Data::Frustum> frustumOverride;
			OvTools::Utils::OptRef<OvRendering::Data::Material> overrideMaterial;
			OvTools::Utils::OptRef<OvRendering::Data::Material> fallbackMaterial;
		};

		struct SceneParsingInput
		{
			OvCore::SceneSystem::Scene& scene;
			OvMaths::FVector3 cameraRight = { 1.0f, 0.0f, 0.0f };
			OvMaths::FVector3 cameraUp    = { 0.0f, 1.0f, 0.0f };
		};

		struct SceneDrawablesDescriptor
		{
			std::vector<OvRendering::Entities::Drawable> drawables;
		};

		struct SceneDrawableDescriptor
		{
			OvCore::ECS::Actor& actor;
			EVisibilityFlags visibilityFlags = EVisibilityFlags::NONE;
			std::optional<OvRendering::Geometry::BoundingSphere> bounds;
		};

		struct SceneFilteredDrawablesDescriptor
		{
			DrawableMap<EOrderingMode::FRONT_TO_BACK> opaques;
			DrawableMap<EOrderingMode::BACK_TO_FRONT> transparents;
			DrawableMap<EOrderingMode::BACK_TO_FRONT> ui;
		};

		struct SceneDrawablesFilteringInput
		{
			const OvRendering::Entities::Camera& camera;
			OvTools::Utils::OptRef<const OvRendering::Data::Frustum> frustumOverride;
			OvTools::Utils::OptRef<OvRendering::Data::Material> overrideMaterial;
			OvTools::Utils::OptRef<OvRendering::Data::Material> fallbackMaterial;
			EVisibilityFlags requiredVisibilityFlags = EVisibilityFlags::NONE;
			bool includeUI = true;
			bool includeTransparent = true;
			bool includeOpaque = true;
		};

		/**
		* Constructor
		* @param p_driver
		* @param p_stencilWrite
		*/
		SceneRenderer(OvRendering::Context::Driver& p_driver, bool p_stencilWrite = false);

		/**
		* Begin Frame — parses scene and sets up descriptors.
		* @param p_frameDescriptor
		*/
		virtual void BeginFrame(const OvRendering::Data::FrameDescriptor& p_frameDescriptor) override;

		/**
		* Draw a model with a single material
		*/
		virtual void DrawModelWithSingleMaterial(
			OvRendering::Data::PipelineState p_pso,
			OvRendering::Resources::Model& p_model,
			OvRendering::Data::Material& p_material,
			const OvMaths::FMatrix4& p_modelMatrix
		);

		/**
		* Parse the scene to find drawables.
		*/
		SceneDrawablesDescriptor ParseScene(const SceneParsingInput& p_input);

		/**
		* Filter and sort drawables.
		*/
		SceneFilteredDrawablesDescriptor FilterDrawables(
			const SceneDrawablesDescriptor& p_drawables,
			const SceneDrawablesFilteringInput& p_filteringInput
		);

		// Rebind the light SSBO at binding point 0 (used by debug passes to restore after fake lights)
		void _BindLightBuffer();

		// Upload camera matrices to the engine UBO (used by debug passes)
		void _SetCameraUBO(const OvRendering::Entities::Camera& p_camera);

		/**
		* Get engine UBO for matrix upload (used by DebugModelRenderFeature)
		*/
		OvRendering::HAL::UniformBuffer& GetEngineBuffer() { return *m_engineBuffer; }

	protected:
		/**
		* Registers all FrameGraph passes for a scene frame.
		*/
		virtual void BuildFrameGraph(OvRendering::FrameGraph::FrameGraph& p_fg) override;

	private:
		bool m_stencilWrite = false;

		// Engine uniform buffer (model/view/proj/camera/time/user matrices)
		std::unique_ptr<OvRendering::HAL::UniformBuffer> m_engineBuffer;
		std::chrono::time_point<std::chrono::high_resolution_clock> m_startTime;

		// Light shader storage buffer
		std::unique_ptr<OvRendering::HAL::ShaderStorageBuffer> m_lightBuffer;

		// Cached post-process pass (owns ping-pong buffers and effects)
		std::unique_ptr<PostProcessRenderPass> m_postProcessPass;

		// Cached pass data for inter-pass communication
		std::vector<std::shared_ptr<OvRendering::HAL::Texture>> m_shadowMaps;
		std::vector<OvMaths::FMatrix4> m_lightSpaceMatrices;
		std::vector<std::reference_wrapper<OvCore::ECS::Components::CReflectionProbe>> m_reflectionProbes;
	};
}
