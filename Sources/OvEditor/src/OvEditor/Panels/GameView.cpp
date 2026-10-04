/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/


#include <OvCore/ECS/Components/CCamera.h>
#include <OvUI/Styling/Style.h>
#include <algorithm>
#include <cmath>

#include "OvEditor/Panels/GameView.h"
#include "OvEditor/Core/EditorActions.h"
#include "OvEditor/Settings/EditorSettings.h"

namespace
{
    class GameViewportWidget final : public OvUI::Widgets::AWidget
    {
    public:
        explicit GameViewportWidget(std::function<void()> draw) : m_draw(std::move(draw)) {}
    private:
        void _Draw_Impl() override { m_draw(); }
        std::function<void()> m_draw;
    };
    const char* modes[] = {"Free aspect", "16:9", "9:16", "4:3", "1:1",
        "1280 x 720 (HD)", "1920 x 1080 (Full HD)", "1080 x 1920 (Portrait)", "Custom resolution"};
    using PreviewSettings = OvEditor::Settings::EditorSettings;
}

OvEditor::Panels::GameView::GameView
(
	const std::string & p_title,
	bool p_opened,
	const OvUI::Settings::PanelWindowSettings & p_windowSettings
) :
	AView(p_title, p_opened, p_windowSettings),
	m_sceneManager(EDITOR_CONTEXT(sceneManager))
{
    m_image->enabled = false;
    CreateWidget<GameViewportWidget>([this] { DrawViewport(); });
}

OvRendering::Entities::Camera* OvEditor::Panels::GameView::GetCamera()
{
	if (auto scene = m_sceneManager.GetCurrentScene())
	{
		if (auto camera = scene->FindMainCamera())
		{
			return &camera->GetCamera();
		}
	}

	return nullptr;
}

OvCore::SceneSystem::Scene* OvEditor::Panels::GameView::GetScene()
{
	return m_sceneManager.GetCurrentScene();
}


void OvEditor::Panels::GameView::Update(float deltaTime)
{
    AView::Update(deltaTime);
    RefreshInputViewport();
}

void OvEditor::Panels::GameView::RefreshInputViewport()
{
    auto& input = *EDITOR_CONTEXT(inputManager);
    const bool valid = m_imageSize.x > 0 && m_imageSize.y > 0;
    if (valid) input.SetUIViewport(GetInputViewport());
    input.SetUIFocused(valid && GetCamera() && IsOpened() && IsVisible() && IsFocused() && !m_controlsActive);
}

void OvEditor::Panels::GameView::SetPreviewMode(PreviewMode mode)
{
    PreviewSettings::GamePreviewMode = std::clamp(int(mode),0,int(PreviewMode::CUSTOM));
}

OvEditor::Panels::GameView::PreviewMode OvEditor::Panels::GameView::GetPreviewMode() const
{
    return PreviewMode(std::clamp(PreviewSettings::GamePreviewMode.Get(),0,int(PreviewMode::CUSTOM)));
}

void OvEditor::Panels::GameView::SetCustomResolution(int width, int height)
{
    PreviewSettings::GamePreviewWidth = std::clamp(width,64,4096);
    PreviewSettings::GamePreviewHeight = std::clamp(height,64,4096);
}

std::pair<uint16_t,uint16_t> OvEditor::Panels::GameView::GetSafeSize() const
{
    const auto mode = GetPreviewMode();
    if (mode == PreviewMode::HD) return {1280,720};
    if (mode == PreviewMode::FULL_HD) return {1920,1080};
    if (mode == PreviewMode::PHONE) return {1080,1920};
    if (mode == PreviewMode::CUSTOM) return {uint16_t(std::clamp(PreviewSettings::GamePreviewWidth.Get(),64,4096)),
        uint16_t(std::clamp(PreviewSettings::GamePreviewHeight.Get(),64,4096))};
    const auto fallback = AView::GetSafeSize();
    float width = m_availableSize.x > 0 ? m_availableSize.x : float(fallback.first);
    float height = m_availableSize.y > 0 ? m_availableSize.y : std::max(0.f,float(fallback.second)-ImGui::GetFrameHeightWithSpacing()-12*OVUI_SCALE);
    width = std::clamp(width,0.f,8192.f); height = std::clamp(height,0.f,8192.f);
    float ratio = 0;
    if (mode == PreviewMode::WIDE) ratio = 16.f/9;
    if (mode == PreviewMode::PORTRAIT) ratio = 9.f/16;
    if (mode == PreviewMode::CLASSIC) ratio = 4.f/3;
    if (mode == PreviewMode::SQUARE) ratio = 1;
    if (ratio > 0) { width = std::min(width,height*ratio); height = width/ratio; }
    return {uint16_t(width),uint16_t(height)};
}

OvWindowing::Inputs::UIInputViewport OvEditor::Panels::GameView::GetInputViewport() const
{
    const auto origin = ImGui::GetMainViewport()->Pos;
    const auto [width,height] = GetOutputSize();
    const double displayWidth = std::max(1.f,m_imageSize.x), displayHeight = std::max(1.f,m_imageSize.y);
    return {{m_imagePosition.x-origin.x,m_imagePosition.y-origin.y}, {displayWidth,displayHeight},
        {std::max(1,int(width))/displayWidth,std::max(1,int(height))/displayHeight}};
}

void OvEditor::Panels::GameView::DrawViewport()
{
    const float scale = OVUI_SCALE;
    ImGui::PushID(this);
    ImGui::SetCursorPosX(8*scale);
    ImGui::SetNextItemWidth(std::max(80.f,std::min(235*scale,ImGui::GetContentRegionAvail().x-8*scale)));
    const auto mode = GetPreviewMode();
    bool popup = false;
    if (ImGui::BeginCombo("##Resolution",modes[int(mode)]))
    {
        popup = true;
        for (int i=0;i<int(std::size(modes));++i)
            if (ImGui::Selectable(modes[i],int(mode)==i)) SetPreviewMode(PreviewMode(i));
        ImGui::EndCombo();
    }
    const auto [width,height] = GetOutputSize();
    if (ImGui::GetContentRegionAvail().x > 260*scale)
    {
        ImGui::SameLine();
        if (width && height) ImGui::TextDisabled("%u x %u",unsigned(width),unsigned(height));
        else ImGui::TextDisabled("Game preview");
    }
    if (GetPreviewMode() == PreviewMode::CUSTOM)
    {
        ImGui::SetCursorPosX(8*scale);
        int w = PreviewSettings::GamePreviewWidth.Get(), h = PreviewSettings::GamePreviewHeight.Get();
        ImGui::SetNextItemWidth(std::max(40.f,std::min(80*scale,(ImGui::GetWindowSize().x-60*scale)/2)));
        const bool changedW = ImGui::DragInt("##Width",&w,1,64,4096,"%d",ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Render width (64 - 4096 pixels)");
        ImGui::SameLine(); ImGui::TextUnformatted("x"); ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(40.f,std::min(80*scale,(ImGui::GetWindowSize().x-60*scale)/2)));
        const bool changedH = ImGui::DragInt("##Height",&h,1,64,4096,"%d",ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Render height (64 - 4096 pixels)");
        if (changedW || changedH) SetCustomResolution(w,h);
    }
    m_controlsActive = popup || ImGui::IsAnyItemActive() || ImGui::GetIO().WantTextInput;
    const auto top = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({ImGui::GetWindowPos().x,top.y+4*scale});
    const auto area = ImGui::GetCursorScreenPos();
    const auto available = ImGui::GetContentRegionAvail();
    m_availableSize = {std::max(0.f,available.x),std::max(0.f,available.y)};
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(area,{area.x+available.x,area.y+available.y},IM_COL32(12,13,16,255));
    m_imagePosition = {}; m_imageSize = {};
    if (width && height && GetCamera() && available.x > 0 && available.y > 0)
    {
        const float fit = std::min(available.x/width,available.y/height);
        m_imageSize = {width*fit,height*fit};
        m_imagePosition = {area.x+(available.x-m_imageSize.x)*.5f,area.y+(available.y-m_imageSize.y)*.5f};
        OvUI::Internal::TextureID texture(GetOutputTexture()); texture.RetainForCapture();
        draw->AddImage(texture.id,{m_imagePosition.x,m_imagePosition.y},
            {m_imagePosition.x+m_imageSize.x,m_imagePosition.y+m_imageSize.y},{0,1},{1,0});
        draw->AddRect({m_imagePosition.x,m_imagePosition.y},{m_imagePosition.x+m_imageSize.x,m_imagePosition.y+m_imageSize.y},IM_COL32(62,65,73,255));
    }
    else if (!GetCamera())
    {
        const char* title = "No active camera";
        const char* hint = "Add a Camera component to an active entity.";
        const auto text = ImGui::CalcTextSize(title);
        const auto detail = ImGui::CalcTextSize(hint);
        const ImVec2 center{area.x+available.x*.5f,area.y+available.y*.5f};
        draw->AddText({center.x-text.x*.5f,center.y-text.y},IM_COL32(231,233,240,255),title);
        if (available.x > detail.x+20*scale)
            draw->AddText({center.x-detail.x*.5f,center.y+6*scale},IM_COL32(143,149,162,255),hint);
    }
    ImGui::Dummy({std::max(0.f,available.x),std::max(0.f,available.y)});
    ImGui::PopID();
}

OvEditor::Panels::CameraPreview::CameraPreview() : AView("Camera Preview",false,{}) {}

void OvEditor::Panels::CameraPreview::Configure(OvCore::SceneSystem::Scene::LifetimeToken scene, uint64_t guid,
    std::pair<uint16_t,uint16_t> size)
{
    m_scene = std::move(scene); m_guid = guid; m_size = size;
}

OvCore::SceneSystem::Scene* OvEditor::Panels::CameraPreview::GetScene()
{
    auto* scene = EDITOR_CONTEXT(sceneManager).GetCurrentScene();
    return scene && scene->GetLifetimeToken() == m_scene ? scene : nullptr;
}

OvRendering::Entities::Camera* OvEditor::Panels::CameraPreview::GetCamera()
{
    if (auto* scene = GetScene())
        if (auto* actor = scene->FindActorByGUID(m_guid); actor && actor->IsAlive() && actor->IsActive())
            if (auto* camera = actor->GetComponent<OvCore::ECS::Components::CCamera>()) return &camera->GetCamera();
    return nullptr;
}
