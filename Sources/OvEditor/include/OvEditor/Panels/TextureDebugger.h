/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <baregl/Texture.h>
#include <OvRendering/Utils/ResourceTracking.h>
#include <thread>
#include <OvUI/Widgets/Visual/Image.h>
#include <OvUI/Widgets/Selection/ComboBox.h>
#include <OvUI/Panels/PanelWindow.h>
#include <OvRendering/Data/FrameGraphPreview.h>

namespace OvEditor::Panels
{
	enum class EScaleMode : uint8_t
	{
		ONE_TO_ONE,
		PERCENTAGE_5,
		PERCENTAGE_10,
		PERCENTAGE_25,
		PERCENTAGE_50,
		PERCENTAGE_100,
		PERCENTAGE_200,
		PERCENTAGE_400,
		PERCENTAGE_800
	};

	class TextureDebugger : public OvUI::Panels::PanelWindow
	{
	public:
		/**
		* Creates a texture debugger.
		* @param p_title
		* @param p_opened
		* @param p_windowSettings
		*/
		TextureDebugger(
			const std::string& p_title,
			bool p_opened,
			const OvUI::Settings::PanelWindowSettings& p_windowSettings
		);

		/**
		* Destroys the texture debugger.
		*/
		virtual ~TextureDebugger() = default;

		/**
		* Updates the texture debugger.
		* @param p_deltaTime
		*/
		void Update(float p_deltaTime);
        void ShowCapturedTexture(std::shared_ptr<const OvRendering::Data::FrameGraphPreviewImage>);

	private:
		void RequireMainThread() const;
		void RefreshTextures(bool choices);
		void SetSelection(std::optional<OvRendering::Utils::ResourceTracking::TextureRegistryEntryDesc> texture);
		const std::thread::id m_mainThread = std::this_thread::get_id();
		EScaleMode m_scaleMode = EScaleMode::ONE_TO_ONE;
		OvUI::Widgets::Selection::ComboBox& m_textureSelector;
		OvUI::Widgets::Selection::ComboBox& m_scaleSelector;
		OvUI::Widgets::Visual::Image& m_image;
		std::optional<OvRendering::Utils::ResourceTracking::TextureRegistryEntryDesc> m_selectedTexture;
        std::shared_ptr<const OvRendering::Data::FrameGraphPreviewImage> m_capture;
	};
}
