/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvCore/SceneSystem/SceneManager.h>
#include <OvWindowing/Inputs/UIInputSnapshot.h>

#include "OvEditor/Panels/AView.h"

namespace OvEditor::Panels
{
	class GameView : public OvEditor::Panels::AView
	{
	public:
		/**
		* Constructor
		* @param p_title
		* @param p_opened
		* @param p_windowSettings
		*/
		GameView(
			const std::string& p_title,
			bool p_opened,
			const OvUI::Settings::PanelWindowSettings& p_windowSettings
		);

		/**
		* Returns the main camera used by the attached scene
		*/
		virtual OvRendering::Entities::Camera* GetCamera();

		/**
		* Returns the scene used by this view
		*/
		virtual OvCore::SceneSystem::Scene* GetScene();
        void Update(float deltaTime) override;
        void RefreshInputViewport();
        enum class PreviewMode { FREE, WIDE, PORTRAIT, CLASSIC, SQUARE, HD, FULL_HD, PHONE, CUSTOM };
        void SetPreviewMode(PreviewMode mode);
        PreviewMode GetPreviewMode() const;
        void SetCustomResolution(int width, int height);
        std::pair<uint16_t,uint16_t> GetSafeSize() const override;
        OvWindowing::Inputs::UIInputViewport GetInputViewport() const;
        OvMaths::FVector2 GetImagePosition() const { return m_imagePosition; }
        OvMaths::FVector2 GetImageSize() const { return m_imageSize; }

	private:
        void DrawViewport();
		OvCore::SceneSystem::SceneManager& m_sceneManager;
        OvMaths::FVector2 m_availableSize{}, m_imagePosition{}, m_imageSize{};
        bool m_controlsActive = false;
	};

    // An offscreen view owned by SceneView. Camera references are resolved on
    // Main using both scene identity and GUID, including after undo/scene loads.
    class CameraPreview final : public AView
    {
    public:
        CameraPreview();
        void Configure(OvCore::SceneSystem::Scene::LifetimeToken scene, uint64_t guid,
            std::pair<uint16_t,uint16_t> size);
        OvRendering::Entities::Camera* GetCamera() override;
        OvCore::SceneSystem::Scene* GetScene() override;
        std::pair<uint16_t,uint16_t> GetSafeSize() const override { return m_size; }
        uint64_t GetCameraGUID() const { return m_guid; }
    private:
        OvCore::SceneSystem::Scene::LifetimeToken m_scene;
        uint64_t m_guid = 0;
        std::pair<uint16_t,uint16_t> m_size{};
    };
}
