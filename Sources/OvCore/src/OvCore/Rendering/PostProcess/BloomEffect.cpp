/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <cmath>

#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Rendering/PostProcess/BloomEffect.h>
#include <OvCore/ResourceManagement/ShaderManager.h>
#include <OvRendering/Utils/Profiling.h>

#include <baregl/data/TextureDesc.h>

// Implementation reference: https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom

// Additional private constants for the bloom effect
namespace OvCore::Rendering::PostProcess::BloomConstants
{
	constexpr float kFilterRadius = 0.005f;
}

namespace
{
	const auto kBloomTextureDesc = baregl::data::TextureDesc{
		.width = 1, // Unknown size at this point
		.height = 1,
		.minFilter = baregl::types::ETextureFilteringMode::LINEAR,
		.magFilter = baregl::types::ETextureFilteringMode::LINEAR,
		.horizontalWrap = baregl::types::ETextureWrapMode::CLAMP_TO_EDGE,
		.verticalWrap = baregl::types::ETextureWrapMode::CLAMP_TO_EDGE,
		.internalFormat = baregl::types::EInternalFormat::RGBA32F,
		.useMipMaps = false,
		.mutableDesc = baregl::data::MutableTextureDesc{
			.format = baregl::types::EFormat::RGBA,
			.type = baregl::types::EPixelDataType::FLOAT
		}
	};

	struct BloomPass
	{
		uint16_t width;
		uint16_t height;
		OvRendering::Data::ResourceHandle target;
	};
}

OvCore::Rendering::PostProcess::BloomEffect::BloomEffect()
{
	auto& shaderManager = OVSERVICE(OvCore::ResourceManagement::ShaderManager);

	m_downsamplingMaterial.SetShader(shaderManager[":Shaders\\PostProcess\\BloomDownsampling.ovfx"]);
	m_upsamplingMaterial.SetShader(shaderManager[":Shaders\\PostProcess\\BloomUpsampling.ovfx"]);
	m_bloomMaterial.SetShader(shaderManager[":Shaders\\PostProcess\\Bloom.ovfx"]);
	m_blitMaterial.SetShader(shaderManager[":Shaders\\PostProcess\\Blit.ovfx"]);

	// Since we want to use blending during the upsampling pass, we need to set up the material
	// manually. The EBlitFlag::USE_MATERIAL_STATE_MASK will be used to enforce these settings.
	m_upsamplingMaterial.SetDepthWriting(false);
	m_upsamplingMaterial.SetColorWriting(true);
	m_upsamplingMaterial.SetBlendable(true);
	m_upsamplingMaterial.SetFrontfaceCulling(false);
	m_upsamplingMaterial.SetBackfaceCulling(false);
	m_upsamplingMaterial.SetDepthTest(false);
}

bool OvCore::Rendering::PostProcess::BloomEffect::IsApplicable(const EffectSettings& p_settings) const
{
	auto& bloomSettings = static_cast<const BloomSettings&>(p_settings);

	return
		AEffect::IsApplicable(p_settings) &&
		bloomSettings.intensity > 0.0f;
}
void OvCore::Rendering::PostProcess::BloomEffect::Draw(
    OvRendering::Core::FullscreenCommandRecorder& recorder,
	OvRendering::Data::PipelineState p_pso,
	OvRendering::Data::ResourceHandle p_src,
	OvRendering::Data::ResourceHandle p_dst,
	const EffectSettings& p_settings
)
{
	ZoneScoped;
	OV_GPU_ZONE(recorder, "BloomEffect");

	using enum OvRendering::Settings::EBlitFlags;

	const auto [refX, refY] = recorder.GetSize(p_src);

	OVASSERT(refX > 0 && refY > 0, "Invalid source framebuffer size");

	const auto& bloomSettings = static_cast<const BloomSettings&>(p_settings);
    const auto preferredPassCount = static_cast<uint32_t>(std::clamp(bloomSettings.passes,
        static_cast<int>(BloomConstants::kMinPassCount), static_cast<int>(BloomConstants::kMaxPassCount)));

    // Thin/first-layout targets can reach one texel on an axis before the other.
    // Keep at least one valid sampling pass, including for a 1x1 input.
    const auto maxPassCountForCurrentResolution = std::max(BloomConstants::kMinPassCount,
        static_cast<uint32_t>(std::log2(std::max(refX, refY))));
    const auto passCount = std::min(preferredPassCount, maxPassCountForCurrentResolution);

	std::vector<BloomPass> bloomMips;
	bloomMips.reserve(passCount);

	for (uint32_t i = 0; i < passCount; ++i)
	{
		// (value >> i) is equivalent to (value / 2^i),
		// so we essentially get half the resolution each time.
		const uint16_t width = std::max<uint16_t>(1, refX >> (i + 1));
		const uint16_t height = std::max<uint16_t>(1, refY >> (i + 1));
		auto descriptor=kBloomTextureDesc;descriptor.width=width;descriptor.height=height;
		const auto target=recorder.CreateTransient("Bloom.Mip"+std::to_string(i),descriptor);
		bloomMips.emplace_back(width, height, target);
	}

	auto outputDesc=kBloomTextureDesc;outputDesc.width=refX;outputDesc.height=refY;
	const auto bloomOutput=recorder.CreateTransient("Bloom.Output",outputDesc);

	// First we want to copy the input image to another buffer to avoid modifying the original image.
	// This could also be made into a filtering pass, so we can exclude low luminance pixels.
    recorder.SetPassName("Bloom.InputBlit");recorder.Blit(p_pso, p_src, bloomOutput, m_blitMaterial);

	auto downsamplingPass = [&](
		OvRendering::Data::ResourceHandle p_src,
		OvRendering::Data::ResourceHandle p_dst,
		const OvMaths::FVector2& p_srcRes 
	) {
		m_downsamplingMaterial.SetProperty("_InputResolution", p_srcRes, true);
        recorder.SetPassName("Bloom.Downsample");
		recorder.Blit(p_pso, p_src, p_dst, m_downsamplingMaterial, DEFAULT & ~RESIZE_DST_TO_MATCH_SRC);
	};

	// First downsample pass, using karis average filter
	m_downsamplingMaterial.SetFeatures({ "KARIS_AVERAGE" });
	downsamplingPass(bloomOutput, bloomMips[0].target, {
		static_cast<float>(refX),
		static_cast<float>(refY)
	});

	// Subsequent downsample passes don't need the karis average filter
	m_downsamplingMaterial.SetFeatures({});

	// Downsample the remaining passes (passCount - 1)
	for (uint32_t i = 1; i < passCount; ++i)
	{
		auto& src = bloomMips[i - 1];
		auto& dst = bloomMips[i];

		downsamplingPass(src.target, dst.target, {
			static_cast<float>(src.width),
			static_cast<float>(src.height)
		});
	}

	// Custom PSO for the upsampling pass, allowing us to use additive blending (accumulation)
	auto upsamplingPSO = p_pso;
	upsamplingPSO.blendingSrcFactor = baregl::types::EBlendingFactor::ONE;
	upsamplingPSO.blendingDestFactor = baregl::types::EBlendingFactor::ONE;
	upsamplingPSO.blendingEquation = baregl::types::EBlendingEquation::FUNC_ADD;

	m_upsamplingMaterial.SetProperty("_FilterRadius", BloomConstants::kFilterRadius, true);

	auto upsamplingPass = [&](
		OvRendering::Data::ResourceHandle p_src,
		OvRendering::Data::ResourceHandle p_dst
	) {
        recorder.SetPassName("Bloom.UpsampleLoadBlend");
		recorder.Blit(upsamplingPSO, p_src, p_dst, m_upsamplingMaterial, (DEFAULT & ~RESIZE_DST_TO_MATCH_SRC) | USE_MATERIAL_STATE_MASK);
	};

	// Blur and upsample back to the original resolution
	for (int32_t i = passCount - 1; i > 0; --i)
	{
		upsamplingPass(bloomMips[i].target, bloomMips[i - 1].target);
	}

	upsamplingPass(bloomMips[0].target, bloomOutput);

	// Final pass, interpolate bloom result with the input image
	m_bloomMaterial.SetProperty("_BloomStrength", std::min(bloomSettings.intensity * 0.04f, 1.0f));
    const std::array bloomBindings{OvRendering::Core::FullscreenCommandRecorder::SamplerBinding{"_BloomTexture",bloomOutput}};
    recorder.SetPassName("Bloom.Composite");recorder.Blit(p_pso,p_src,p_dst,m_bloomMaterial,DEFAULT,bloomBindings);
}
