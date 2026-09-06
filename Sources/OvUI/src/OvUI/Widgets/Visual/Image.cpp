/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include "OvUI/Widgets/Visual/Image.h"
#include "OvUI/Internal/Converter.h"

OvUI::Widgets::Visual::Image::Image(uint32_t p_textureID, const OvMaths::FVector2& p_size) :
	textureID{ p_textureID }, size(p_size)
{
	
}

OvUI::Widgets::Visual::Image::Image(std::shared_ptr<baregl::Texture> p_texture, const OvMaths::FVector2& p_size) :
    textureID(std::move(p_texture)), size(p_size) {}

void OvUI::Widgets::Visual::Image::_Draw_Impl()
{
	textureID.RetainForCapture();
	ImGui::Image(textureID.id, Internal::Converter::ToImVec2(size), ImVec2(0.f, 1.f), ImVec2(1.f, 0.f));
}
