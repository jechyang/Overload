/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <format>
#include <stdexcept>

#include <baregl/Texture.h>

#include <OvCore/Global/ServiceLocator.h>
#include <OvDebug/Assertion.h>
#include <OvDebug/Logger.h>
#include <OvEditor/Panels/TextureDebugger.h>
#include <OvRendering/Utils/ResourceTracking.h>
#include <OvTools/Utils/EnumMapper.h>

template <>
struct OvTools::Utils::MappingFor<OvEditor::Panels::EScaleMode, std::string>
{
	static constexpr char one_to_one[] = "1:1";
	static constexpr char percentage_5[] = "5%";
	static constexpr char percentage_10[] = "10%";
	static constexpr char percentage_25[] = "25%";
	static constexpr char percentage_50[] = "50%";
	static constexpr char percentage_100[] = "100%";
	static constexpr char percentage_200[] = "200%";
	static constexpr char percentage_400[] = "400%";
	static constexpr char percentage_800[] = "800%";

	using enum OvEditor::Panels::EScaleMode;
	using type = std::tuple<
		EnumValuePair<ONE_TO_ONE, one_to_one>,
		EnumValuePair<PERCENTAGE_5, percentage_5>,
		EnumValuePair<PERCENTAGE_10, percentage_10>,
		EnumValuePair<PERCENTAGE_25, percentage_25>,
		EnumValuePair<PERCENTAGE_50, percentage_50>,
		EnumValuePair<PERCENTAGE_100, percentage_100>,
		EnumValuePair<PERCENTAGE_200, percentage_200>,
		EnumValuePair<PERCENTAGE_400, percentage_400>,
		EnumValuePair<PERCENTAGE_800, percentage_800>
	>;
};

template <>
struct OvTools::Utils::MappingFor<OvEditor::Panels::EScaleMode, float>
{
	using enum OvEditor::Panels::EScaleMode;
	using type = std::tuple<
		EnumValuePair<PERCENTAGE_5, 0.05f>,
		EnumValuePair<PERCENTAGE_10, 0.1f>,
		EnumValuePair<PERCENTAGE_25, 0.25f>,
		EnumValuePair<PERCENTAGE_50, 0.5f>,
		EnumValuePair<PERCENTAGE_100, 1.0f>,
		EnumValuePair<PERCENTAGE_200, 2.0f>,
		EnumValuePair<PERCENTAGE_400, 4.0f>,
		EnumValuePair<PERCENTAGE_800, 8.0f>
	>;
};

namespace
{
    using Entry = OvRendering::Utils::ResourceTracking::TextureRegistryEntryDesc;
    bool IsValidTexture(const Entry& texture)
    { return texture.id && texture.storage.GetID() && texture.type == baregl::types::ETextureType::TEXTURE_2D; }
    OvMaths::FVector2 CalculateImageSize(OvEditor::Panels::EScaleMode mode, const OvMaths::FVector2& windowSize, const Entry& texture)
    {
        if (!texture.allocated || !texture.width || !texture.height || !texture.storage.GetID()) return OvMaths::FVector2::Zero;
        const float scale = mode == OvEditor::Panels::EScaleMode::ONE_TO_ONE ?
            std::min(std::max(0.f, windowSize.x - 45.f) / texture.width, std::max(0.f, windowSize.y - 120.f) / texture.height) :
            OvTools::Utils::ToValueImpl<OvEditor::Panels::EScaleMode, float>(mode);
        return {texture.width * scale, texture.height * scale};
    }
}
namespace OvEditor::Panels
{
    TextureDebugger::TextureDebugger(const std::string& title, bool opened, const OvUI::Settings::PanelWindowSettings& settings) :
        PanelWindow(title, opened, settings),
        m_image(CreateWidget<OvUI::Widgets::Visual::Image>(0, OvMaths::FVector2::Zero)),
        m_textureSelector(CreateWidget<OvUI::Widgets::Selection::ComboBox>()),
        m_scaleSelector(CreateWidget<OvUI::Widgets::Selection::ComboBox>())
    {
        allowHorizontalScrollbar = true;
        RefreshTextures(true);
        m_textureSelector.ValueChangedEvent += [this](int id) {
            RequireMainThread();
            if(id==-1&&m_capture)return;
            m_capture.reset();
            SetSelection(id ? OvRendering::Utils::ResourceTracking::GetTexture(id) : std::nullopt);
        };
        if (m_textureSelector.choices.size() > 1)
            m_textureSelector.ValueChangedEvent.Invoke(m_textureSelector.choices.rbegin()->first);
        auto scaleEntry = [](auto mode) {
            return std::pair{static_cast<int>(mode), OvTools::Utils::ToValueImpl<EScaleMode, std::string>(mode)};
        };
        using enum EScaleMode;
        m_scaleSelector.choices = {
            scaleEntry(ONE_TO_ONE), scaleEntry(PERCENTAGE_5), scaleEntry(PERCENTAGE_10),
            scaleEntry(PERCENTAGE_25), scaleEntry(PERCENTAGE_50), scaleEntry(PERCENTAGE_100),
            scaleEntry(PERCENTAGE_200), scaleEntry(PERCENTAGE_400), scaleEntry(PERCENTAGE_800)
        };
        m_scaleSelector.currentChoice = 0;
        m_scaleSelector.ValueChangedEvent += [this](int scale) {
            RequireMainThread(); m_scaleMode = static_cast<EScaleMode>(scale);
            m_image.size = m_selectedTexture ? CalculateImageSize(m_scaleMode, GetSize(), *m_selectedTexture) : OvMaths::FVector2::Zero;
        };
        CloseEvent += [this]{if(m_capture){m_capture.reset();SetSelection(std::nullopt);}};
    }
    void TextureDebugger::RequireMainThread() const
    {
        if (std::this_thread::get_id() != m_mainThread) throw std::logic_error("TextureDebugger must be updated on its UI thread");
    }
    void TextureDebugger::SetSelection(std::optional<Entry> texture)
    {
        if (texture && !IsValidTexture(*texture)) texture.reset();
        m_selectedTexture = std::move(texture);
        m_textureSelector.currentChoice = m_selectedTexture ? m_selectedTexture->id : 0;
        m_image.textureID = m_selectedTexture ? OvUI::Internal::TextureID(m_selectedTexture->storage) : OvUI::Internal::TextureID(0u);
        m_image.size = m_selectedTexture ? CalculateImageSize(m_scaleMode, GetSize(), *m_selectedTexture) : OvMaths::FVector2::Zero;
    }
    void TextureDebugger::RefreshTextures(bool choices)
    {
        RequireMainThread();
        if(m_capture) {
            if(!m_capture->texture->RetainStorage().GetID()){m_capture.reset();SetSelection(std::nullopt);return;}
            if(choices) {
                m_textureSelector.choices={{0,"None"}};
                for(const auto& texture:OvRendering::Utils::ResourceTracking::GetTextures())if(IsValidTexture(texture))m_textureSelector.choices[texture.id]=std::format("Texture {}: {}",texture.id,texture.name);
                m_textureSelector.choices[-1]=std::format("Frame Graph: {} v{} / mip {} / layer {}",m_capture->resourceName,m_capture->version,m_capture->mip,m_capture->layer);
            }
            m_image.size=CalculateImageSize(m_scaleMode,GetSize(),*m_selectedTexture);return;
        }
        if (choices)
        {
            const auto textures = OvRendering::Utils::ResourceTracking::GetTextures();
            m_textureSelector.choices = {{0, "None"}};
            std::optional<Entry> selected;
            for (const auto& texture : textures) if (IsValidTexture(texture))
            {
                m_textureSelector.choices[texture.id] = std::format("Texture {}: {}", texture.id, texture.name);
                if (m_selectedTexture && texture.id == m_selectedTexture->id && texture.generation == m_selectedTexture->generation)
                    selected = texture;
            }
            SetSelection(std::move(selected));
        }
        else if (m_selectedTexture)
            SetSelection(OvRendering::Utils::ResourceTracking::GetTexture(m_selectedTexture->id, m_selectedTexture->generation));
    }
    void TextureDebugger::Update(float)
    {
        RequireMainThread();
        // Even closed panels release a removed selection. Visible choices are
        // rebuilt from one stable registry snapshot, never from writer callbacks.
        RefreshTextures(IsOpened());
    }
    void TextureDebugger::ShowCapturedTexture(std::shared_ptr<const OvRendering::Data::FrameGraphPreviewImage> image)
    {
        RequireMainThread();if(!image||!image->texture)return;
        const auto entry=OvRendering::Utils::ResourceTracking::GetTexture(image->texture->GetID());
        if(!entry)return;m_capture=std::move(image);SetSelection(entry);m_textureSelector.currentChoice=-1;
        RefreshTextures(true);Open();
    }
}
