/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <stdexcept>
#include <cstring>
#include <tinyxml2.h>

#include <OvCore/ECS/Actor.h>
#include <OvCore/ECS/ComponentRegistry.h>
#include <OvTools/Utils/CallbackScope.h>
#include <OvUI/Widgets/Texts/TextColored.h>
#include <OvCore/ECS/Components/CReflectionProbe.h>

#include <OvDebug/Assertion.h>

#include <baregl/Renderbuffer.h>

#include <OvUI/Widgets/Buttons/Button.h>
#include <OvUI/Widgets/Visual/Separator.h>

namespace
{
	constexpr size_t kUBOSize =
		sizeof(OvMaths::FVector4) +	// Position (vec3)
		sizeof(OvMaths::FMatrix4) +	// Rotation (mat3)
		sizeof(OvMaths::FVector4) +	// Box Center (vec3)
		sizeof(OvMaths::FVector4) +	// Box Extents (vec3)
		sizeof(float) +				// Brightness (float)
		sizeof(int) +				// Box Projection (bool)
		sizeof(int) +			// Local (bool)
		sizeof(int);				// std140 block trailing padding

	constexpr uint32_t kBackBufferIndex = 0; // Cubemap that is being rendered
	constexpr uint32_t kCompleteBufferIndex = 1; // Cubemap that is used

	bool RequiresDoubleBuffering(OvCore::ECS::Components::CReflectionProbe::ECaptureSpeed p_speed)
	{
		return p_speed != OvCore::ECS::Components::CReflectionProbe::ECaptureSpeed::SIX_FACES;
	}
    struct InfluenceLease final : OvUI::Plugins::IPlugin
    {
        OvTools::Utils::CallbackScope scope;
        void Execute(OvUI::Plugins::EPluginExecutionContext) override {}
    };
    template<class T> void GateInfluence(OvUI::Widgets::AWidget& widget, OvCore::ECS::Components::CReflectionProbe& probe)
    {
        auto* dispatcher=widget.GetPlugin<OvUI::Plugins::DataDispatcher<T>>(); if (!dispatcher) return;
        auto copy=*dispatcher; auto ticket=widget.AddPlugin<InfluenceLease>().scope.Ticket(); auto component=probe.InspectorTicket();
        auto* main=&OvCore::ECS::GetComponentRegistry().Reflection().Main();
        widget.disabled=probe.GetInfluencePolicy()!=OvCore::ECS::Components::CReflectionProbe::EInfluencePolicy::LOCAL;
        dispatcher->ReplaceBinding([copy,ticket,component,main,field=&widget,target=&probe]() mutable {
            if (!main->IsCurrent() || ticket.expired() || component.expired()) return T{};
            field->disabled=target->GetInfluencePolicy()!=OvCore::ECS::Components::CReflectionProbe::EInfluencePolicy::LOCAL;
            return copy.Gather();
        },[copy,ticket,component,main,target=&probe](T value) mutable {
            if (!main->IsCurrent() || ticket.expired() || component.expired() || target->GetInfluencePolicy()!=OvCore::ECS::Components::CReflectionProbe::EInfluencePolicy::LOCAL) return;
            copy.NotifyChange(); copy.Provide(value);
        });
    }
    void GateInfluenceFields(OvUI::Internal::WidgetContainer& root,OvCore::ECS::Components::CReflectionProbe& probe)
    {
        std::string label;
        for (auto [widget,mode]:root.GetWidgets())
        {
            if (auto* text=dynamic_cast<OvUI::Widgets::Texts::TextColored*>(widget)) label=text->content;
            else if (label=="Influence Size") GateInfluence<std::array<float,3>>(*widget,probe);
            else if (label=="Box Projection") GateInfluence<bool>(*widget,probe);
            if (auto* children=dynamic_cast<OvUI::Internal::WidgetContainer*>(widget)) GateInfluenceFields(*children,probe);
        }
    }
}

OvCore::ECS::Components::CReflectionProbe::CReflectionProbe(ECS::Actor& p_owner) :
	AComponent(p_owner),
	m_framebuffers{ "ReflectionProbeFramebuffer" },
	m_cubemapIterator{ m_cubemaps }
{
	m_uniformBuffer = std::make_unique<baregl::Buffer>();
	m_uniformBuffer->Allocate(kUBOSize, baregl::types::EAccessSpecifier::STREAM_DRAW);

	_AllocateResources();
}

std::string OvCore::ECS::Components::CReflectionProbe::GetName()
{
	return "Reflection Probe";
}

std::string OvCore::ECS::Components::CReflectionProbe::GetTypeName()
{
	return std::string{ComponentTraits<CReflectionProbe>::Name};
}

void OvCore::ECS::Components::CReflectionProbe::SetRefreshMode(ERefreshMode p_mode)
{
	if (p_mode != m_refreshMode)
	{
		m_refreshMode = p_mode;
	}
}

OvCore::ECS::Components::CReflectionProbe::ERefreshMode OvCore::ECS::Components::CReflectionProbe::GetRefreshMode() const
{
	return m_refreshMode;
}

void OvCore::ECS::Components::CReflectionProbe::SetCaptureSpeed(ECaptureSpeed p_speed)
{
	const bool requiredDoubleBuffering = _IsDoubleBuffered();
	const bool willRequireDoubleBuffering = RequiresDoubleBuffering(p_speed);

	m_captureSpeed = p_speed;
	m_captureFaceIndex = 0; // Reset the capture face index, to make sure we start from the first face again.

	// Progressive uses double buffering, while immediate (6 face per frame) doesn't.
	// This makes sure that the proper resources are allocated for
	// the given refresh mode.
	if (requiredDoubleBuffering != willRequireDoubleBuffering)
	{
		_AllocateResources();
	}
}

OvCore::ECS::Components::CReflectionProbe::ECaptureSpeed OvCore::ECS::Components::CReflectionProbe::GetCaptureSpeed() const
{
	return m_captureSpeed;
}

void OvCore::ECS::Components::CReflectionProbe::SetCapturePosition(const OvMaths::FVector3& p_position)
{
	m_capturePosition = p_position;
}

const OvMaths::FVector3& OvCore::ECS::Components::CReflectionProbe::GetCapturePosition() const
{
	return m_capturePosition;
}

void OvCore::ECS::Components::CReflectionProbe::SetBrightness(float p_brightness)
{
	m_brightness = p_brightness;
}

float OvCore::ECS::Components::CReflectionProbe::GetBrightness() const
{
	return m_brightness;
}

void OvCore::ECS::Components::CReflectionProbe::SetCubemapResolution(uint32_t p_resolution)
{
    if (!p_resolution || p_resolution > 4096 || (p_resolution & (p_resolution - 1)))
        throw std::invalid_argument("Cubemap resolution must be a power of two between 1 and 4096");

	if (p_resolution != m_resolution)
	{
		m_resolution = p_resolution;
		_AllocateResources();
	}
}

uint32_t OvCore::ECS::Components::CReflectionProbe::GetCubemapResolution() const
{
	return m_resolution;
}

void OvCore::ECS::Components::CReflectionProbe::SetInfluencePolicy(EInfluencePolicy p_policy)
{
	m_influencePolicy = p_policy;
}

OvCore::ECS::Components::CReflectionProbe::EInfluencePolicy OvCore::ECS::Components::CReflectionProbe::GetInfluencePolicy() const
{
	return m_influencePolicy;
}

void OvCore::ECS::Components::CReflectionProbe::SetInfluenceSize(const OvMaths::FVector3& p_size)
{
	m_influenceSize = p_size;
}

const OvMaths::FVector3& OvCore::ECS::Components::CReflectionProbe::GetInfluenceSize() const
{
	return m_influenceSize;
}

void OvCore::ECS::Components::CReflectionProbe::SetBoxProjection(bool p_enabled)
{
	m_boxProjection = p_enabled;
}

bool OvCore::ECS::Components::CReflectionProbe::IsBoxProjectionEnabled() const
{
	return m_boxProjection;
}

void OvCore::ECS::Components::CReflectionProbe::RequestCapture(bool p_forceImmediate)
{
	m_captureFaceIndex = 0;

	if (m_captureRequest.has_value() && m_captureRequest->forceImmediate)
	{
		// If a request already exists and is set to force immediate,
		// we ignore the new request.
		return;
	}

	m_captureRequest = {
		.forceImmediate = p_forceImmediate
	};
}

std::shared_ptr<baregl::Texture> OvCore::ECS::Components::CReflectionProbe::GetCubemap() const
{
	const auto& target =
		_IsDoubleBuffered() ?
		m_cubemapIterator[kCompleteBufferIndex] :
		m_cubemaps[0];

	OVASSERT(target != nullptr, "Cubemap is not initialized");
	return target;
}

OvCore::ECS::Components::CReflectionProbe::Settings OvCore::ECS::Components::CReflectionProbe::GetSettings() const
{
    return {m_refreshMode, m_captureSpeed, m_resolution, m_capturePosition, m_brightness, m_influencePolicy, m_influenceSize, m_boxProjection};
}

void OvCore::ECS::Components::CReflectionProbe::SetSettings(const Settings& settings)
{
    // Restore both resource-dependent settings in one batch. Loading through the
    // generated Record codec validates every leaf before this setter is invoked.
    if (!settings.resolution || settings.resolution > 4096 || (settings.resolution & (settings.resolution - 1)))
        throw std::invalid_argument("Cubemap resolution must be a power of two between 1 and 4096");
    const bool allocate = settings.resolution != m_resolution || RequiresDoubleBuffering(settings.captureSpeed) != _IsDoubleBuffered();
    m_refreshMode = settings.refreshMode; m_captureSpeed = settings.captureSpeed; m_resolution = settings.resolution;
    m_capturePosition = settings.capturePosition; m_brightness = settings.brightness;
    m_influencePolicy = settings.influencePolicy; m_influenceSize = settings.influenceSize; m_boxProjection = settings.boxProjection;
    m_captureFaceIndex = 0;
    if (allocate) _AllocateResources();
}

void OvCore::ECS::Components::CReflectionProbe::OnInspector(OvUI::Internal::WidgetContainer& root)
{
    AComponent::OnInspector(root);
    GateInfluenceFields(root,*this);
    root.CreateWidget<OvUI::Widgets::Visual::Separator>();
    auto& capture = root.CreateWidget<OvUI::Widgets::Buttons::Button>("Capture Now");
    capture.ClickedEvent += [this, ticket = InspectorTicket(), main = &GetComponentRegistry().Reflection().Main()] {
        if (main->IsCurrent() && !ticket.expired()) RequestCapture();
    };
}

void OvCore::ECS::Components::CReflectionProbe::_NotifyCubemapComplete()
{
	_NotifyCubemapComplete(true);
}

void OvCore::ECS::Components::CReflectionProbe::_NotifyCubemapComplete(bool p_generateMipmaps)
{
	if (m_captureRequest)
	{
		m_captureRequest.reset(); // Discard the capture request after processing it.
	}

	// This is to keep track of whether any cubemap is complete.
	// Useful to ensure that we always have a valid cubemap to present.
	m_isAnyCubemapComplete = true;

	// Once the cubemap is complete, we can generate the mipmaps
	// that will be used to sample the average color of the cubemap (diffuse IBL).
	if (p_generateMipmaps) m_cubemapIterator[kBackBufferIndex]->GenerateMipmaps();

	// When using double buffering (i.e. progressive capture), we need to
	// update which buffer is the back buffer and which one is the complete buffer (presented to shaders).
	if (_IsDoubleBuffered())
	{
		++m_cubemapIterator;
		++m_framebuffers;
	}
}

void OvCore::ECS::Components::CReflectionProbe::_AllocateResources()
{
	const uint8_t cubemapCount = _IsDoubleBuffered() ? 2 : 1;

	// Reset the ping pong iterators
	m_framebuffers.Reset();
	m_cubemapIterator.Reset();

	// This forces a capture on the next frame,
	// as we always want to have at least one valid cubemap.
	m_isAnyCubemapComplete = false;

	m_captureFaceIndex = 0;

	// Allocate 1 or 2 cubemaps (depending on double buffering)
	for (uint8_t i = 0; i < cubemapCount; ++i)
	{
		auto& cubemap = m_cubemaps[i];
		cubemap = std::make_shared<baregl::Texture>(
			baregl::types::ETextureType::TEXTURE_CUBE,
			"ReflectionProbeCubemap"
		);

		cubemap->Allocate(
			baregl::data::TextureDesc{
				.width = m_resolution,
				.height = m_resolution,
				.minFilter = baregl::types::ETextureFilteringMode::LINEAR_MIPMAP_LINEAR,
				.magFilter = baregl::types::ETextureFilteringMode::LINEAR,
				.horizontalWrap = baregl::types::ETextureWrapMode::CLAMP_TO_EDGE,
				.verticalWrap = baregl::types::ETextureWrapMode::CLAMP_TO_EDGE,
				.internalFormat = baregl::types::EInternalFormat::RGBA32F,
				.useMipMaps = true
			}
		);

		for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex)
		{
			m_framebuffers[i].Attach<baregl::Texture>(
				cubemap,
				baregl::types::EFramebufferAttachment::COLOR,
				faceIndex, // Each color attachment is a face of the cubemap
				faceIndex // Each face of the cubemap is accessed by its layer index
			);
		}

		// Depth buffer
		const auto renderbuffer = std::make_shared<baregl::Renderbuffer>();
		const auto internalFormat = baregl::types::EInternalFormat::DEPTH_COMPONENT;
		renderbuffer->Allocate(m_resolution, m_resolution, internalFormat);
		m_framebuffers[i].Attach(renderbuffer, baregl::types::EFramebufferAttachment::DEPTH);

		// Validation
		m_framebuffers[i].Validate();
	}
}

OvCore::ECS::Components::CReflectionProbe::UniformData OvCore::ECS::Components::CReflectionProbe::CaptureUniformData() const
{
	const auto& probePosition = owner.transform.GetWorldPosition() + m_capturePosition;
	const auto& boxPosition = owner.transform.GetWorldPosition();
	const auto& probeRotation = owner.transform.GetWorldRotation();
	const auto& probeRotationMatrix = OvMaths::FQuaternion::ToMatrix3(OvMaths::FQuaternion::Normalize(probeRotation));

	// UBO is padding sensitive, so we need to ensure proper alignment.
#pragma pack(push, 1)
	struct
	{
		OvMaths::FVector4 position;
		OvMaths::FMatrix4 rotation;
		OvMaths::FVector4 boxCenter;
		OvMaths::FVector4 boxHalfExtents;
		float brightness;
		bool boxProjection;
		std::byte padding1[3];
		bool local;
		std::byte padding2[3];
		std::byte blockPadding[4];
	} uboDataPage{
		.position = probePosition,
		.rotation = probeRotationMatrix,
		.boxCenter = boxPosition,
		.boxHalfExtents = m_influenceSize,
		.brightness = m_brightness,
		.boxProjection = m_boxProjection && m_influencePolicy == EInfluencePolicy::LOCAL,
		.local = m_influencePolicy == EInfluencePolicy::LOCAL,
	};
#pragma pack(pop)

	static_assert(sizeof(uboDataPage) == kUBOSize, "UBO data size mismatch");

	UniformData result;
	static_assert(sizeof(result) == sizeof(uboDataPage));
	std::memcpy(result.data(), &uboDataPage, result.size());
	return result;
}

void OvCore::ECS::Components::CReflectionProbe::_PrepareUBO()
{
	const auto data = CaptureUniformData();
	m_uniformBuffer->Upload(data.data());
}

std::vector<uint32_t> OvCore::ECS::Components::CReflectionProbe::_GetCaptureFaceIndices()
{
	auto targetFaceCount = [&]() -> uint32_t {
		const bool captureRequested = m_captureRequest.has_value();
		const bool immediateCaptureRequested =
			(captureRequested && m_captureRequest->forceImmediate) ||
			// Always make sure to capture all faces immediately when no cubemap is complete,
			// unless we are in "on demand" mode.
			(m_refreshMode != ERefreshMode::ON_DEMAND && !m_isAnyCubemapComplete);

		if (immediateCaptureRequested)
		{
			return 6; // Capture all faces immediately
		}

		if (captureRequested || m_refreshMode == ERefreshMode::REALTIME)
		{
			return static_cast<uint32_t>(m_captureSpeed);
		}

		return 0;
	}();

	if (targetFaceCount == 0)
	{
		return {}; // No faces to capture
	}

	const auto lastIndex = m_captureFaceIndex + (targetFaceCount - 1);

	OVASSERT(
		lastIndex < 6,
		"We shouldn't request more than one cubemap to be modified at a time."
		// Expected indices are:
		// [0, 1, 2, 3, 4, 5] for 6 faces per frame,
		// [0, 1, 2], [3, 4, 5] for 3 faces per frame,
		// [0, 1], [2, 3], [4, 5] for 2 faces per frame,
		// [0], [1], [2], [3], [4], [5] for 1 face per frame.
	);

	std::vector<uint32_t> faceIndices;
	faceIndices.reserve(targetFaceCount);

	for (uint32_t i = m_captureFaceIndex; i <= lastIndex; ++i)
	{
		faceIndices.push_back(i);
	}

	// Update the index for the next capture
	m_captureFaceIndex = (lastIndex + 1) % 6;

	return faceIndices;
}

baregl::Framebuffer& OvCore::ECS::Components::CReflectionProbe::_GetTargetFramebuffer() const
{
	auto& framebuffer =
		_IsDoubleBuffered() ?
		m_framebuffers[kBackBufferIndex] :
		m_framebuffers[0];

	OVASSERT(framebuffer.IsValid(), "Framebuffer is invalid");
	return framebuffer;
}

baregl::Buffer& OvCore::ECS::Components::CReflectionProbe::_GetUniformBuffer() const
{
	OVASSERT(m_uniformBuffer != nullptr, "Uniform buffer is not initialized");
	return *m_uniformBuffer;
}

bool OvCore::ECS::Components::CReflectionProbe::_IsDoubleBuffered() const
{
	return RequiresDoubleBuffering(m_captureSpeed);
}
