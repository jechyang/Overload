/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <algorithm>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <tracy/Tracy.hpp>

#include <OvCore/ECS/Components/CModelRenderer.h>
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvCore/ECS/Components/CSkinnedMeshRenderer.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Rendering/EngineBufferRenderFeature.h>
#include <OvCore/Rendering/EngineDrawableDescriptor.h>
#include <OvCore/Rendering/PostProcessRenderPass.h>
#include <OvCore/Rendering/ReflectionRenderFeature.h>
#include <OvCore/Rendering/ReflectionRenderPass.h>
#include <OvCore/Rendering/SceneRenderer.h>
#include <OvCore/Rendering/ShadowRenderFeature.h>
#include <OvCore/Rendering/ShadowRenderPass.h>
#include <OvCore/Rendering/SkinningDrawableDescriptor.h>
#include <OvCore/Rendering/SkinningRenderFeature.h>
#include <OvCore/Rendering/SkinningUtils.h>
#include <OvCore/ResourceManagement/ShaderManager.h>
#include <OvRendering/Data/Frustum.h>
#include <OvRendering/Features/LightingRenderFeature.h>
#include <OvRendering/Resources/Loaders/ShaderLoader.h>
#include <OvRendering/Utils/Profiling.h>
#include <OvTools/Jobs/JobSystem.h>

namespace
{
	using namespace OvCore::Rendering;
	const std::string kSkinningFeatureName{ SkinningUtils::kFeatureName };
	// Baselines from the stage-0 release benchmark; filtering has more merge overhead.
	constexpr size_t kMinParallelModelRenderers = 128;
	constexpr size_t kMinParallelDrawables = 256;

	class SceneRenderPass : public OvRendering::Core::ARenderPass
	{
	public:
		SceneRenderPass(OvRendering::Core::CompositeRenderer& p_renderer, bool stencilWrite = false) :
			OvRendering::Core::ARenderPass(p_renderer),
			m_stencilWrite(stencilWrite)
		{
		}

	protected:
		void PrepareStencilBuffer(OvRendering::Data::PipelineState& p_pso)
		{
			p_pso.stencilTest = true;
			p_pso.stencilWriteMask = 0xFF;
			p_pso.stencilFuncRef = 1;
			p_pso.stencilFuncMask = 0xFF;
			p_pso.stencilOpFail = baregl::types::EOperation::REPLACE;
			p_pso.depthOpFail = baregl::types::EOperation::REPLACE;
			p_pso.bothOpFail = baregl::types::EOperation::REPLACE;
			p_pso.colorWriting.mask = 0x00;
		}

	private:
		bool m_stencilWrite;
	};

	class OpaqueRenderPass : public SceneRenderPass
	{
	public:
		OpaqueRenderPass(OvRendering::Core::CompositeRenderer& p_renderer, bool p_stencilWrite = false) :
			SceneRenderPass(p_renderer, p_stencilWrite)
		{
		}

	protected:
		virtual void Draw(OvRendering::Data::PipelineState p_pso) override
		{
			ZoneScoped;
			TracyGpuZone("OpaqueRenderPass");

			PrepareStencilBuffer(p_pso);

			const auto& drawables = m_renderer.GetDescriptor<SceneRenderer::SceneFilteredDrawablesDescriptor>();

			for (const auto& drawable : drawables.opaques | std::views::values)
			{
				m_renderer.DrawEntity(p_pso, drawable);
			}
		}
	};

	class TransparentRenderPass : public SceneRenderPass
	{
	public:
		TransparentRenderPass(OvRendering::Core::CompositeRenderer& p_renderer, bool p_stencilWrite = false) :
			SceneRenderPass(p_renderer, p_stencilWrite) {
		}

	protected:
		virtual void Draw(OvRendering::Data::PipelineState p_pso) override
		{
			ZoneScoped;
			TracyGpuZone("TransparentRenderPass");

			PrepareStencilBuffer(p_pso);

			const auto& drawables = m_renderer.GetDescriptor<SceneRenderer::SceneFilteredDrawablesDescriptor>();

			for (const auto& drawable : drawables.transparents | std::views::values)
			{
				m_renderer.DrawEntity(p_pso, drawable);
			}
		}
	};

	class UIRenderPass : public SceneRenderPass
	{
	public:
		UIRenderPass(OvRendering::Core::CompositeRenderer& p_renderer, bool p_stencilWrite = false) :
			SceneRenderPass(p_renderer, p_stencilWrite) {
		}

	protected:
		virtual void Draw(OvRendering::Data::PipelineState p_pso) override
		{
			ZoneScoped;
			TracyGpuZone("UIRenderPass");

			PrepareStencilBuffer(p_pso);

			const auto& drawables = m_renderer.GetDescriptor<SceneRenderer::SceneFilteredDrawablesDescriptor>();

			for (const auto& drawable : drawables.ui | std::views::values)
			{
				m_renderer.DrawEntity(p_pso, drawable);
			}
		}
	};

	OvRendering::Features::LightingRenderFeature::LightSet FindActiveLights(const OvCore::SceneSystem::Scene& p_scene)
	{
		OvRendering::Features::LightingRenderFeature::LightSet lights;

		const auto& facs = p_scene.GetFastAccessComponents();

		for (auto light : facs.lights)
		{
			if (light->owner.IsActive())
			{
				lights.push_back(std::ref(light->GetData()));
			}
		}

		return lights;
	}

	std::vector<std::reference_wrapper<OvCore::ECS::Components::CReflectionProbe>> FindActiveReflectionProbes(const OvCore::SceneSystem::Scene& p_scene)
	{
		std::vector<std::reference_wrapper<OvCore::ECS::Components::CReflectionProbe>> probes;
		const auto& facs = p_scene.GetFastAccessComponents();
		for (auto probe : facs.reflectionProbes)
		{
			if (probe->owner.IsActive())
			{
				probes.push_back(*probe);
			}
		}
		return probes;
	}

	/**
	* Parses a single model renderer into drawables, appending them to the
	* output buffer. Shared by the serial and parallel paths of ParseScene so
	* both produce identical results
	*/
	void ParseModelRenderer(
		OvCore::ECS::Components::CModelRenderer& p_modelRenderer,
		std::vector<OvRendering::Entities::Drawable>& p_outDrawables)
	{
		using namespace OvCore::ECS::Components;

		auto& owner = p_modelRenderer.owner;
		if (!owner.IsActive()) return;
		const auto model = p_modelRenderer.GetModel();
		if (!model) return;
		const auto materialRenderer = p_modelRenderer.owner.GetComponent<CMaterialRenderer>();
		if (!materialRenderer) return;
		const auto* skinnedRenderer = owner.GetComponent<CSkinnedMeshRenderer>();
		const bool hasSkinning = SkinningUtils::IsSkinningActive(skinnedRenderer);

		const auto& transform = owner.transform.GetFTransform();
		const auto& materials = materialRenderer->GetMaterials();

		for (auto& mesh : model->GetMeshes())
		{
			OvTools::Utils::OptRef<OvRendering::Data::Material> material;

			if (mesh->GetMaterialIndex() < kMaxMaterialCount)
			{
				material = materials.at(mesh->GetMaterialIndex());
			}

			OvRendering::Entities::Drawable drawable{
				.mesh = *mesh,
				.material = material,
				.stateMask = material.has_value() ? material->GenerateStateMask() : OvRendering::Data::StateMask{},
			};

			auto bounds = [&]() -> std::optional<OvRendering::Geometry::BoundingSphere> {
				using enum CModelRenderer::EFrustumBehaviour;
				switch (p_modelRenderer.GetFrustumBehaviour())
				{
				case MESH_BOUNDS: return mesh->GetBoundingSphere();
				case DEPRECATED_MODEL_BOUNDS: return model->GetBoundingSphere();
				case CUSTOM_BOUNDS: return p_modelRenderer.GetCustomBoundingSphere();
				default: return std::nullopt;
				}
				return std::nullopt;
			}();

			drawable.AddDescriptor<SceneRenderer::SceneDrawableDescriptor>({
				.actor = p_modelRenderer.owner,
				.visibilityFlags = materialRenderer->GetVisibilityFlags(),
				.bounds = bounds
			});

			drawable.AddDescriptor<EngineDrawableDescriptor>({
				transform.GetWorldMatrix(),
				materialRenderer->GetUserMatrix()
			});

			if (hasSkinning && mesh->HasSkinningData())
			{
				SkinningUtils::ApplyDescriptor(drawable, *skinnedRenderer);
			}

			p_outDrawables.push_back(std::move(drawable));
		}
	}

	/**
	* Filters, culls and categorizes a single drawable into the given output
	* descriptor. Shared by the serial and parallel paths of FilterDrawables so
	* both produce identical results
	*/
	void FilterDrawable(
		const OvRendering::Entities::Drawable& p_drawable,
		const SceneRenderer::SceneDrawablesFilteringInput& p_filteringInput,
		const OvTools::Utils::OptRef<const OvRendering::Data::Frustum>& p_frustum,
		SceneRenderer::SceneFilteredDrawablesDescriptor& p_output)
	{
		using namespace OvCore::ECS::Components;

		const auto& desc = p_drawable.GetDescriptor<SceneRenderer::SceneDrawableDescriptor>();
		OvTools::Utils::OptRef<const SkinningDrawableDescriptor> skinningDescriptor;
		const bool hasSkinningDescriptor = p_drawable.TryGetDescriptor<SkinningDrawableDescriptor>(skinningDescriptor);

		// Skip drawables that do not satisfy the required visibility flags
		if (!SatisfiesVisibility(desc.visibilityFlags, p_filteringInput.requiredVisibilityFlags))
		{
			return;
		}

		const auto targetMaterial =
			p_filteringInput.overrideMaterial.has_value() ?
			p_filteringInput.overrideMaterial.value() :
			(p_drawable.material.has_value() ? p_drawable.material.value() : p_filteringInput.fallbackMaterial);

		// Skip if material is invalid
		if (!targetMaterial || !targetMaterial->IsValid()) return;

		// Filter drawables based on the type (UI, opaque, transparent)
		// Except for the fallback material, which is always included.
		if (!p_filteringInput.fallbackMaterial || &p_filteringInput.fallbackMaterial.value() != &targetMaterial.value())
		{
			const bool isUI = targetMaterial->IsUserInterface();
			if (isUI && !p_filteringInput.includeUI) return;
			if (!isUI && !targetMaterial->IsBlendable() && !p_filteringInput.includeOpaque) return;
			if (!isUI && targetMaterial->IsBlendable() && !p_filteringInput.includeTransparent) return;
		}

		// Perform frustum culling if enabled
		if (p_frustum && desc.bounds.has_value())
		{
			ZoneScopedN("Frustum Culling");

			auto cullingBounds = desc.bounds.value();
			if (hasSkinningDescriptor)
			{
				cullingBounds.radius *= skinningDescriptor->boundsScale;
			}

			if (!p_frustum->BoundingSphereInFrustum(cullingBounds, desc.actor.transform.GetFTransform()))
			{
				return; // Skip this drawable as it's outside the frustum
			}
		}

		// Calculate distance to camera for sorting
		const float distanceToCamera = OvMaths::FVector3::Distance(
			desc.actor.transform.GetWorldPosition(),
			p_filteringInput.camera.GetPosition()
		);

		// At this point we want to copy the drawable to avoid modifying the original one.
		// The copy will use the updated material.
		// At this point, the filtered drawable should be guaranteed to have a valid material.
		auto drawableCopy = p_drawable;
		drawableCopy.material = targetMaterial;
		drawableCopy.stateMask = targetMaterial->GenerateStateMask();

		if (
			hasSkinningDescriptor &&
			targetMaterial->HasShader() &&
			targetMaterial->SupportsFeature(kSkinningFeatureName)
		)
		{
			drawableCopy.featureSetOverride = SkinningUtils::BuildFeatureSet(&targetMaterial->GetFeatures());
		}
		else
		{
			drawableCopy.featureSetOverride = std::nullopt;
		}

		// Categorize drawable based on their type.
		// This is also where sorting happens, using
		// the multimap key.
		if (drawableCopy.material->IsUserInterface())
		{
			p_output.ui.emplace(decltype(decltype(p_output.ui)::value_type::first){
				.order = drawableCopy.material->GetDrawOrder(),
				.materialKey = reinterpret_cast<uintptr_t>(&drawableCopy.material.value()),
				.distance = distanceToCamera
			}, drawableCopy);
		}
		else if (drawableCopy.material->IsBlendable())
		{
			p_output.transparents.emplace(decltype(decltype(p_output.transparents)::value_type::first){
				.order = drawableCopy.material->GetDrawOrder(),
				.materialKey = reinterpret_cast<uintptr_t>(&drawableCopy.material.value()),
				.distance = distanceToCamera
			}, drawableCopy);
		}
		else
		{
			p_output.opaques.emplace(decltype(decltype(p_output.opaques)::value_type::first){
				.order = drawableCopy.material->GetDrawOrder(),
				.materialKey = reinterpret_cast<uintptr_t>(&drawableCopy.material.value()),
				.distance = distanceToCamera
			}, drawableCopy);
		}
	}
}

OvCore::Rendering::SceneRenderer::SceneRenderer(OvRendering::Context::Driver& p_driver, bool p_stencilWrite)
	: OvRendering::Core::CompositeRenderer(p_driver)
{
	using namespace OvRendering::Features;
	using namespace OvRendering::Settings;
	using enum OvRendering::Features::EFeatureExecutionPolicy;

	AddFeature<EngineBufferRenderFeature, ALWAYS>();
	AddFeature<LightingRenderFeature, ALWAYS>();
	AddFeature<SkinningRenderFeature, ALWAYS>();

	AddFeature<ReflectionRenderFeature, WHITELIST_ONLY>()
		.Include<OpaqueRenderPass>()
		.Include<TransparentRenderPass>();

	AddFeature<ShadowRenderFeature, WHITELIST_ONLY>()
		.Include<OpaqueRenderPass>()
		.Include<TransparentRenderPass>()
		.Include<UIRenderPass>();

	AddPass<ShadowRenderPass>("Shadows", ERenderPassOrder::Shadows);
	AddPass<ReflectionRenderPass>("ReflectionRenderPass", ERenderPassOrder::Reflections);
	AddPass<OpaqueRenderPass>("Opaques", ERenderPassOrder::Opaque, p_stencilWrite);
	AddPass<TransparentRenderPass>("Transparents", ERenderPassOrder::Transparent, p_stencilWrite);
	AddPass<PostProcessRenderPass>("Post-Process", ERenderPassOrder::PostProcessing);
	AddPass<UIRenderPass>("UI", ERenderPassOrder::UI);
}

void OvCore::Rendering::SceneRenderer::BeginFrame(const OvRendering::Data::FrameDescriptor& p_frameDescriptor)
{
	ZoneScoped;

	OVASSERT(HasDescriptor<SceneDescriptor>(), "Cannot find SceneDescriptor attached to this renderer");

	auto& sceneDescriptor = GetDescriptor<SceneDescriptor>();

	const bool frustumLightCulling = p_frameDescriptor.camera.value().HasFrustumLightCulling();

	AddDescriptor<OvRendering::Features::LightingRenderFeature::LightingDescriptor>({
		FindActiveLights(sceneDescriptor.scene),
		frustumLightCulling ? sceneDescriptor.frustumOverride : std::nullopt
	});

	AddDescriptor<OvCore::Rendering::ReflectionRenderFeature::ReflectionDescriptor>({
		FindActiveReflectionProbes(sceneDescriptor.scene)
	});

	OvRendering::Core::CompositeRenderer::BeginFrame(p_frameDescriptor);

	AddDescriptor<SceneDrawablesDescriptor>({
		ParseScene(SceneParsingInput{
			.scene = sceneDescriptor.scene
		})
	});

	// Default filtered drawables descriptor using the main camera (used by most render passes).
	// Some other render passes can decide to filter the drawables themselves, using the 
	// SceneDrawablesDescriptor instead of the SceneFilteredDrawablesDescriptor one.
	AddDescriptor<SceneFilteredDrawablesDescriptor>({
		FilterDrawables(
			GetDescriptor<SceneDrawablesDescriptor>(),
			SceneDrawablesFilteringInput{
				.camera = p_frameDescriptor.camera.value(),
				.frustumOverride = sceneDescriptor.frustumOverride,
				.overrideMaterial = sceneDescriptor.overrideMaterial,
				.fallbackMaterial = sceneDescriptor.fallbackMaterial,
				.requiredVisibilityFlags = EVisibilityFlags::GEOMETRY
			}
		)
	});
}

void OvCore::Rendering::SceneRenderer::DrawModelWithSingleMaterial(OvRendering::Data::PipelineState p_pso, OvRendering::Resources::Model& p_model, OvRendering::Data::Material& p_material, const OvMaths::FMatrix4& p_modelMatrix)
{
	auto stateMask = p_material.GenerateStateMask();
	auto userMatrix = OvMaths::FMatrix4::Identity;

	auto engineDrawableDescriptor = EngineDrawableDescriptor{
		p_modelMatrix,
		userMatrix
	};

	for (auto mesh : p_model.GetMeshes())
	{
		OvRendering::Entities::Drawable element;
		element.mesh = *mesh;
		element.material = p_material;
		element.stateMask = stateMask;
		element.AddDescriptor(engineDrawableDescriptor);

		DrawEntity(p_pso, element);
	}
}

SceneRenderer::SceneDrawablesDescriptor OvCore::Rendering::SceneRenderer::ParseScene(const SceneParsingInput& p_input)
{
	ZoneScoped;

	using namespace OvCore::ECS::Components;

	// Containers for the parsed drawables.
	SceneRenderer::SceneDrawablesDescriptor result;

	const auto& scene = p_input.scene;
	const auto& modelRenderers = scene.GetFastAccessComponents().modelRenderers;

	if (modelRenderers.empty())
	{
		return result;
	}

	// The job system is fetched per call: it is provided by the application
	// context, and renderers must not assume construction-time availability.
	auto& jobSystem = OVSERVICE(OvTools::Jobs::JobSystem);
	const uint32_t workerCount = jobSystem.GetWorkerCount();
	const size_t modelRendererCount = modelRenderers.size();

	// Fast path for small scenes: job scheduling overhead would dominate.
	if (modelRendererCount < std::max<size_t>(kMinParallelModelRenderers, static_cast<size_t>(workerCount) * 2))
	{
		for (const auto modelRenderer : modelRenderers)
		{
			ParseModelRenderer(*modelRenderer, result.drawables);
		}
		return result;
	}

	// Split the model renderer list into contiguous chunks, one job per chunk,
	// each filling its own buffer. Merging in chunk order afterwards makes the
	// output identical to the serial iteration.
	{
		ZoneScopedN("ParseScene (parallel)");

		const size_t chunkCount = std::clamp<size_t>(
			static_cast<size_t>(workerCount) * 4, 2, modelRendererCount
		);
		const size_t chunkSize = (modelRendererCount + chunkCount - 1) / chunkCount;

		std::vector<std::vector<OvRendering::Entities::Drawable>> chunkDrawables(chunkCount);
		std::vector<OvTools::Jobs::JobHandle> handles;
		handles.reserve(chunkCount);

		try
		{
			for (size_t chunk = 0; chunk < chunkCount; ++chunk)
			{
				const size_t begin = chunk * chunkSize;
				const size_t end = std::min(begin + chunkSize, modelRendererCount);
				if (begin >= end)
				{
					break;
				}

				auto& chunkOutput = chunkDrawables[chunk];
				chunkOutput.reserve(chunkSize * 2);

				handles.push_back(jobSystem.Submit([&modelRenderers, &chunkOutput, begin, end]
				{
					for (size_t index = begin; index < end; ++index)
					{
						ParseModelRenderer(*modelRenderers[index], chunkOutput);
					}
				}));
			}
		}
		catch (...)
		{
			// Submitted jobs must finish before their output buffers unwind.
			jobSystem.WaitAll(handles);
			throw;
		}

		// All jobs reference locals of this scope, so they must be joined here.
		jobSystem.WaitAll(std::span<const OvTools::Jobs::JobHandle>{ handles });

		size_t totalDrawables = 0;
		for (const auto& chunkOutput : chunkDrawables)
		{
			totalDrawables += chunkOutput.size();
		}
		result.drawables.reserve(totalDrawables);
		for (auto& chunkOutput : chunkDrawables)
		{
			result.drawables.insert(
				result.drawables.end(),
				std::make_move_iterator(chunkOutput.begin()),
				std::make_move_iterator(chunkOutput.end())
			);
		}
	}

	return result;
}

SceneRenderer::SceneFilteredDrawablesDescriptor OvCore::Rendering::SceneRenderer::FilterDrawables(
	const SceneDrawablesDescriptor& p_drawables,
	const SceneDrawablesFilteringInput& p_filteringInput
)
{
	ZoneScoped;

	using namespace OvCore::ECS::Components;

	SceneFilteredDrawablesDescriptor output;

	const auto& camera = p_filteringInput.camera;

	// Determine if we should use frustum culling
	OvTools::Utils::OptRef<const OvRendering::Data::Frustum> frustum;
	if (camera.HasFrustumGeometryCulling())
	{
		frustum = p_filteringInput.frustumOverride ? p_filteringInput.frustumOverride : camera.GetFrustum();
	}

	const auto& drawables = p_drawables.drawables;
	const size_t drawableCount = drawables.size();

	// The job system is fetched per call: it is provided by the application
	// context, and renderers must not assume construction-time availability.
	auto& jobSystem = OVSERVICE(OvTools::Jobs::JobSystem);
	const uint32_t workerCount = jobSystem.GetWorkerCount();

	// Fast path for small batches: job scheduling overhead would dominate.
	if (drawableCount < std::max<size_t>(kMinParallelDrawables, static_cast<size_t>(workerCount) * 2))
	{
		for (const auto& drawable : drawables)
		{
			FilterDrawable(drawable, p_filteringInput, frustum, output);
		}
		return output;
	}

	// Contiguous chunks fill local descriptor sets, one job per chunk. Merging
	// in chunk order afterwards makes the output identical to the serial
	// iteration: non-hinted single-element multimap inserts land at the upper
	// bound of their equal-key range, so equal keys keep insertion order.
	{
		ZoneScopedN("FilterDrawables (parallel)");

		const size_t chunkCount = std::clamp<size_t>(
			static_cast<size_t>(workerCount) * 4, 2, drawableCount
		);
		const size_t chunkSize = (drawableCount + chunkCount - 1) / chunkCount;

		std::vector<SceneFilteredDrawablesDescriptor> chunkOutputs(chunkCount);
		std::vector<OvTools::Jobs::JobHandle> handles;
		handles.reserve(chunkCount);

		try
		{
			for (size_t chunk = 0; chunk < chunkCount; ++chunk)
			{
				const size_t begin = chunk * chunkSize;
				const size_t end = std::min(begin + chunkSize, drawableCount);
				if (begin >= end)
				{
					break;
				}

				auto& chunkOutput = chunkOutputs[chunk];

				handles.push_back(jobSystem.Submit([&drawables, &chunkOutput, &p_filteringInput, &frustum, begin, end]
				{
					for (size_t index = begin; index < end; ++index)
					{
						FilterDrawable(drawables[index], p_filteringInput, frustum, chunkOutput);
					}
				}));
			}
		}
		catch (...)
		{
			// Submitted jobs must finish before their output buffers unwind.
			jobSystem.WaitAll(handles);
			throw;
		}

		// All jobs reference locals of this scope, so they must be joined here.
		jobSystem.WaitAll(std::span<const OvTools::Jobs::JobHandle>{ handles });

		for (auto& chunkOutput : chunkOutputs)
		{
			for (auto& [key, drawable] : chunkOutput.opaques)
			{
				output.opaques.emplace(key, std::move(drawable));
			}
			for (auto& [key, drawable] : chunkOutput.transparents)
			{
				output.transparents.emplace(key, std::move(drawable));
			}
			for (auto& [key, drawable] : chunkOutput.ui)
			{
				output.ui.emplace(key, std::move(drawable));
			}
		}
	}

	return output;
}
