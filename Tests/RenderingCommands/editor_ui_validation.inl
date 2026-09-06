#include <OvDebug/Logger.h>
#include <OvEditor/Panels/TextureDebugger.h>
#include <OvRendering/Utils/ResourceTracking.h>
#include <OvEditor/Panels/MaterialEditor.h>
#include <OvEditor/Panels/AssetProperties.h>
#include <OvCore/Helpers/GUIHelpers.h>
#include <OvUI/Widgets/InputFields/AssetField.h>
#include <OvUI/Widgets/Layout/GroupCollapsable.h>
#include <OvUI/Widgets/Layout/TreeNode.h>
#include <imgui_internal.h>

void EditorUIValidation(OvEditor::Core::Editor& editor,OvEditor::Core::Context& context,const std::filesystem::path& dir)
{
    auto& ui=*context.uiManager;ui.EnableEditorLayoutSave(false);ImGui::GetIO().ConfigInputTextCursorBlink=false;
    auto& scene=*context.sceneManager.GetCurrentScene();Require(!scene.GetActors().empty(),"UI fixture has no actors");
    editor.m_editorActions.SelectActor(*scene.GetActors().back());
    auto* material=context.materialManager.GetResource(":Materials/Default.ovmat");Require(material,"UI material missing");
    editor.m_panelsManager.GetPanelAs<OvEditor::Panels::MaterialEditor>("Material Editor").SetTarget(*material);
    editor.m_panelsManager.GetPanelAs<OvEditor::Panels::AssetProperties>("Asset Properties").SetTarget(":Models/Mannequin.fbx");
    auto clear=[] {glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_SCISSOR_TEST);glClearColor(.08f,.12f,.18f,1);glClear(GL_COLOR_BUFFER_BIT);};
    auto pixels=[&] {const auto [w,h]=context.window->GetFramebufferSize();std::vector<uint8_t> result(size_t(w)*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,result.data());return result;};
    std::ofstream evidence(dir/"editor-ui-frames.bin",std::ios::binary);size_t pairs=0;
    auto compare=[&](const std::string& name) {
        for(int i=0;i<4;++i){clear();ui.Render();}
        const auto expected=pixels();auto frame=ui.RecordRenderCommands();clear();frame.Execute(*context.driver);const auto actual=pixels();
        if(actual!=expected)throw std::runtime_error("Editor UI pixels differ: "+name);
        size_t varied=0;for(size_t i=4;i<actual.size();i+=4)if(std::memcmp(actual.data(),actual.data()+i,3))++varied;
        Require(varied>100,"empty UI image");
        evidence.write(reinterpret_cast<const char*>(actual.data()),static_cast<std::streamsize>(actual.size()));++pairs;
        std::printf("PASS: owned Editor UI panel %s\n",name.c_str());
    };
    compare("complete canvas");
    // Render each actual production panel on its own canvas so overlap cannot hide a raw texture binding.
    for(auto& [name,panel]:editor.m_panelsManager.m_panels)
    {
        if(auto* window=dynamic_cast<OvUI::Panels::PanelWindow*>(panel.get()))
        {window->Open();window->dockable=false;window->autoSize=false;window->SetPosition({0,0});window->SetSize({600,440});}
        OvUI::Modules::Canvas canvas;canvas.AddPanel(*panel);ui.SetCanvas(canvas);compare(name);
    }
    {
        OvCore::Helpers::GUIHelpers::PickerItemList items;
        items.Add({"actor","Actor icon","Retained picker icon",OvCore::Helpers::GUIHelpers::GetActorIconID(),[]{}});
        editor.m_itemPicker->Open(std::move(items),"Owned picker");editor.m_itemPicker->SetPosition({0,0});editor.m_itemPicker->SetSize({400,350});
        OvUI::Modules::Canvas canvas;canvas.AddPanel(*editor.m_itemPicker);ui.SetCanvas(canvas);compare("Item Picker");editor.m_itemPicker->Close();
    }
    {
        auto& debugger=editor.m_panelsManager.GetPanelAs<OvEditor::Panels::TextureDebugger>("Texture Debugger");
        OvUI::Modules::Canvas canvas;canvas.AddPanel(debugger);ui.SetCanvas(canvas);
        std::optional<OvRendering::Data::RenderCommandList> pending;std::vector<uint8_t> expected;GLuint id=0;
        {
            baregl::Texture texture(baregl::types::ETextureType::TEXTURE_2D,"owned debugger source");
            texture.Allocate({.width=2,.height=2,.minFilter=baregl::types::ETextureFilteringMode::LINEAR,.magFilter=baregl::types::ETextureFilteringMode::LINEAR,.internalFormat=baregl::types::EInternalFormat::RGBA8,.useMipMaps=false});
            std::array<uint8_t,16> texels{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
            texture.Upload(texels.data(),baregl::types::EFormat::RGBA,baregl::types::EPixelDataType::UNSIGNED_BYTE);id=texture.GetID();
            debugger.Update(0);
            debugger.m_textureSelector.currentChoice=id;debugger.m_textureSelector.ValueChangedEvent.Invoke(id);
            for(int i=0;i<4;++i){clear();ui.Render();}expected=pixels();pending.emplace(ui.RecordRenderCommands());
        }
        Require(debugger.m_selectedTexture.has_value(),"resource writer unexpectedly mutated UI selection");
        debugger.Update(0);
        Require(glIsTexture(id) && !debugger.m_selectedTexture,"debugger did not separate wrapper and storage lifetime");
        clear();pending->Execute(*context.driver);Require(pixels()==expected,"destroyed debugger texture changed pending UI frame");
        pending.reset();Require(!glIsTexture(id),"debugger storage leaked after final frame release");++pairs;
    }
    ui.SetCanvas(editor.m_canvas);ui.SetCommandRecordingEnabled(false);
    Require(glGetError()==GL_NO_ERROR,"GL error after complete Editor UI validation");
    std::printf("PASS: %zu exact complete/per-panel/picker/debugger UI pairs; stack texture storage survives source destruction and releases\n",pairs);
}
