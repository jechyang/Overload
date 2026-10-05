/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvReflection/Annotations.h>
#include <OvCore/Resources/ResourceReference.h>

#include <OvAudio/Entities/AudioSource.h>
#include <OvTools/Eventing/Event.h>

#include "OvCore/ECS/Components/AComponent.h"

namespace OvCore::ECS { class Actor; }

namespace OvCore::ECS::Components
{
	/**
	* Represents an audio source. Its position in the world is important if the spatial sound settings is on
	*/
	class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CAudioSource", DisplayName = "Audio Source", Order = 12) CAudioSource : public AComponent
	{
	public:
		/**
		* Constructor
		* @param p_owner
		*/
		CAudioSource(ECS::Actor& p_owner);

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Returns the type name of the component
		*/
		virtual std::string GetTypeName() override;

		/**
		* Defines the sound to play on the audio source
		* @param p_sound
		*/
		void SetSound(OvAudio::Resources::Sound* p_sound);

		/**
		* Defines if the audio source should autoplay (Play sound on enable)
		*/
		void SetAutoplay(bool p_autoplay);

		/**
		* Defines the audio source volume
		* @param p_volume
		*/
		void SetVolume(float p_volume);

		/**
		* Defines the audio source pan (Left / Right)
		* @param p_pan
		*/
		void SetPan(float p_pan);

		/**
		* Defines if the audio source should loop
		* @param p_looped
		*/
		void SetLooped(bool p_looped);

		/**
		* Defines the audio source pitch (= frequency or playback speed)
		* @param p_pitch
		*/
		void SetPitch(float p_pitch);

		/**
		* Defines if the audio source should be spatialized or not
		* @param p_value
		*/
		void SetSpatial(bool p_value);

		/**
		* Defines the audio source attenuation threshold (Minimum distance before applying sound attenuation)
		* @param p_distance
		*/
		void SetAttenuationThreshold(float p_distance);

		/**
		* Returns the sound attached to the audio source
		*/
		OvAudio::Resources::Sound* GetSound() const;

		/**
		* Returns true if the audio source should call "Play" OnEnable
		*/
		OV_PROPERTY(Serialize, Name = "autoplay", Setter = SetAutoplay, Order = 0)
		bool IsAutoplayed() const;

		/**
		* Returns the audio source volume
		*/
		OV_PROPERTY(Serialize, Name = "volume", Setter = SetVolume, Order = 2, Missing = Default, Default = "0")
		float GetVolume() const;

		/**
		* Returns the audio source pan (Left / Right)
		*/
		OV_PROPERTY(Serialize, Name = "pan", Setter = SetPan, Order = 3, Missing = Default, Default = "0")
		float GetPan() const;

		/**
		* Returns if the audio source is looping
		*/
		OV_PROPERTY(Serialize, Name = "looped", Setter = SetLooped, Order = 4, Missing = Default, Default = "false")
		bool IsLooped() const;

		/**
		* Returns the audio source pitch (= frequency or playback speed)
		*/
		OV_PROPERTY(Serialize, Name = "pitch", Setter = SetPitch, Order = 5, Missing = Default, Default = "0")
		float GetPitch() const;

		/**
		* Returns true if the audio source is currently playing
		*/
		bool IsPlaying() const;

		/**
		* Returns true if the audio source is spatialized
		*/
		OV_PROPERTY(Serialize, Name = "spatial", Setter = SetSpatial, Order = 1, Missing = Default, Default = "false")
		bool IsSpatial() const;

		/**
		* Returns the audio source attenuation threshold (Minimum distance before applying sound attenuation)
		*/
		OV_PROPERTY(Serialize, Name = "attenuation_threshold", Setter = SetAttenuationThreshold, Order = 6, Missing = Default, Default = "0")
		float GetAttenuationThreshold() const;

		/**
		* Play the audio source attached sound
		*/
		void Play();

		/**
		* Resume the audio source
		*/
		void Resume();

		/**
		* Pause the audio source
		*/
		void Pause();

		/**
		* Stop the audio source
		*/
		void Stop();


		/**
		* Defines how the component should be drawn in the inspector
		* @param p_root
		*/
		virtual void OnInspector(OvUI::Internal::WidgetContainer& p_root) override;

	private:
		virtual void OnEnable() override;
		virtual void OnDisable() override;

	private:
        friend struct OvReflection::Access<CAudioSource>;
        OV_PROPERTY(Serialize, Name = "audio_clip", Setter = SetSerializedSound, Missing = Default, Default = "?", Order = 7)
        Resources::ResourceReference<OvAudio::Resources::Sound> GetSerializedSound() const;
        void SetSerializedSound(const Resources::ResourceReference<OvAudio::Resources::Sound>& value);
        std::string m_soundPath;
		OvAudio::Resources::Sound* m_sound = nullptr;
		OvAudio::Entities::AudioSource m_audioSource;
		bool m_autoPlay = false;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CAudioSource>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CAudioSource";
	};
}