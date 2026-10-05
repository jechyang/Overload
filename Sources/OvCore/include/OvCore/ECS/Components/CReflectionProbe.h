/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvReflection/Annotations.h>

#include <OvRendering/Data/RenderCommandList.h>
#include <array>
#include <cstddef>

#include <baregl/Framebuffer.h>
#include <baregl/Texture.h>
#include <baregl/Buffer.h>

#include <OvCore/ECS/Components/AComponent.h>
#include <OvCore/Rendering/PingPongFramebuffer.h>

namespace OvCore::ECS { class Actor; }

namespace OvCore::Rendering
{
	class SceneFrameExtractor;
}

namespace OvCore::ECS::Components
{
	/**
	* Component that captures the surrounding environment into a cubemap texture.
	*/
	class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CReflectionProbe", DisplayName = "Reflection Probe", Order = 15) CReflectionProbe : public AComponent
	{
	public:
		enum class OV_TYPE() ERefreshMode : uint32_t
		{
			REALTIME,
			ONCE,
			ON_DEMAND
		};

		enum class OV_TYPE() ECaptureSpeed : uint32_t
		{
			ONE_FACE = 1,		// Capture one face per frame
			TWO_FACES = 2,		// Capture two faces per frame
			THREE_FACES = 3,	// Capture three faces per frame
			SIX_FACES = 6		// Capture all six faces per frame
		};

		enum class OV_TYPE() EInfluencePolicy : uint32_t
		{
			LOCAL,
			GLOBAL
		};

        struct OV_TYPE(Record) Settings
        {
            OV_FIELD(Serialize, Inspect, Name = "refresh_mode", DisplayName = "Refresh Mode", Validate = "EnumChoice", Order = 0)
            ERefreshMode refreshMode = ERefreshMode::REALTIME;
            OV_FIELD(Serialize, Inspect, Name = "capture_speed", DisplayName = "Capture Speed", Validate = "EnumChoice", Order = 1)
            ECaptureSpeed captureSpeed = ECaptureSpeed::ONE_FACE;
            OV_FIELD(Serialize, Inspect, Name = "resolution", DisplayName = "Cubemap Resolution", Validate = "PowerOfTwo", Drawer = "PowerOfTwo", Min = 16, Max = 4096, Order = 2)
            uint32_t resolution = 512;
            OV_FIELD(Serialize, Inspect, Name = "capture_position", DisplayName = "Capture Position", Step = 0.05, Order = 3)
            OvMaths::FVector3 capturePosition{0, 0, 0};
            OV_FIELD(Serialize, Inspect, Name = "brightness", DisplayName = "Brightness", Step = 0.01, Min = 0, Order = 4)
            float brightness = 1;
            OV_FIELD(Serialize, Inspect, Name = "influence_policy", DisplayName = "Influence Policy", Validate = "EnumChoice", Order = 5)
            EInfluencePolicy influencePolicy = EInfluencePolicy::GLOBAL;
            OV_FIELD(Serialize, Inspect, Name = "influence_size", DisplayName = "Influence Size", Step = 0.05, Min = 0, Order = 6)
            OvMaths::FVector3 influenceSize{10, 10, 10};
            OV_FIELD(Serialize, Inspect, Name = "box_projection", DisplayName = "Box Projection", Order = 7)
            bool boxProjection = false;
        };

		/**
		* Constructor
		* @param p_owner
		*/
		CReflectionProbe(ECS::Actor& p_owner);

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Returns the type name of the component
		*/
		virtual std::string GetTypeName() override;

		/**
		* Sets the refresh mode of the reflection probe
		* @param p_mode
		*/
		void SetRefreshMode(ERefreshMode p_mode);

		/**
		* Returns the refresh mode of the reflection probe
		*/
		ERefreshMode GetRefreshMode() const;

		/**
		* Determines how many faces the reflection probe should capture per frame
		* @param p_speed
		*/
		void SetCaptureSpeed(ECaptureSpeed p_speed);

		/**
		* Returns the capture speed (number of faces captured per frame)
		*/
		ECaptureSpeed GetCaptureSpeed() const;

		/**
		* Sets the position offset for the capture of the reflection probe
		* @param p_position
		*/
		void SetCapturePosition(const OvMaths::FVector3& p_position);

		/**
		* Returns the position offset for the capture of the reflection probe
		*/
		const OvMaths::FVector3& GetCapturePosition() const;

		/**
		* Sets the brightness of the reflection probe
		* @param p_brightness
		*/
		void SetBrightness(float p_brightness);

		/**
		* Returns the brightness of the reflection probe
		*/
		float GetBrightness() const;

		/**
		* Sets the cubemap resolution
		* @note The resolution must be a power of 2!
		* @param p_resolution
		*/
		void SetCubemapResolution(uint32_t p_resolution);

		/**
		* Returns the cubemap resolution
		*/
		uint32_t GetCubemapResolution() const;

		/**
		* Determines the influence policy of the reflection probe
		* @param p_policy
		*/
		void SetInfluencePolicy(EInfluencePolicy p_policy);

		/**
		* Returns the influence policy of the reflection probe
		*/
		EInfluencePolicy GetInfluencePolicy() const;

		/**
		* Sets the size of the influence volume of the reflection probe
		* @param p_size
		*/
		void SetInfluenceSize(const OvMaths::FVector3& p_size);

		/**
		* Returns the size of the reflection probe volume
		*/
		const OvMaths::FVector3& GetInfluenceSize() const;

		/**
		* Sets if the reflection probe should use box projection
		*/
		void SetBoxProjection(bool p_enabled);

		/**
		* Returns if the reflection probe uses box projection
		*/
		bool IsBoxProjectionEnabled() const;

		/**
		* Requests the cubemap to be updated
		* @param p_forceImmediate
		*/
		void RequestCapture(bool p_forceImmediate = false);

		/**
		* Returns the last complete cubemap captured by the reflection probe
		*/
		std::shared_ptr<baregl::Texture> GetCubemap() const;

		// CPU snapshot of the padding-sensitive reflection UBO.
		using UniformData = std::array<std::byte, 128>;
		UniformData CaptureUniformData() const;

		/**
		* Defines how the component should be drawn in the inspector
		* @param p_root
		*/
		virtual void OnInspector(OvUI::Internal::WidgetContainer& p_root) override;

	private:
        friend struct OvReflection::Access<CReflectionProbe>;
        OV_PROPERTY(Serialize, Inspect, Flatten, Name = "settings", Setter = SetSettings, DisplayName = "Probe Settings")
        Settings GetSettings() const;
        void SetSettings(const Settings& settings);

		void _NotifyCubemapComplete();
		void _NotifyCubemapComplete(bool p_generateMipmaps);
		void _AllocateResources();
		void _PrepareUBO();
		std::vector<uint32_t> _GetCaptureFaceIndices();
		baregl::Framebuffer& _GetTargetFramebuffer() const;
		baregl::Buffer& _GetUniformBuffer() const;
		bool _IsDoubleBuffered() const;

		friend class OvCore::Rendering::SceneFrameExtractor;

	private:
		struct CaptureRequestDesc
		{
			bool forceImmediate = false;
		};

	private:
		// Double buffering so we can render to progressively one while reading from the other
		Rendering::PingPongFramebuffer m_framebuffers;
		std::array<std::shared_ptr<baregl::Texture>, 2> m_cubemaps;
		OvTools::Utils::CircularIterator<std::shared_ptr<baregl::Texture>, 2> m_cubemapIterator;
		std::unique_ptr<baregl::Buffer> m_uniformBuffer;
		uint32_t m_captureFaceIndex = 0; // Current frame index in the capture process
		std::optional<CaptureRequestDesc> m_captureRequest = std::nullopt;
		bool m_isAnyCubemapComplete = false;
		OvRendering::Data::RenderCommandList::History m_captureHistory;

		// Serialized properties
		ERefreshMode m_refreshMode = ERefreshMode::REALTIME;
		ECaptureSpeed m_captureSpeed = ECaptureSpeed::ONE_FACE; // Number of faces to capture per frame
		uint32_t m_resolution = 512;
		OvMaths::FVector3 m_capturePosition{ 0.0f, 0.0f, 0.0f };
		float m_brightness = 1.0f;
		EInfluencePolicy m_influencePolicy = EInfluencePolicy::GLOBAL;
		OvMaths::FVector3 m_influenceSize{ 10.0f, 10.0f, 10.0f };
		bool m_boxProjection = false;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CReflectionProbe>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CReflectionProbe";
	};
}
