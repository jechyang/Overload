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

struct EditorUIProgramHook
{
    inline static PFNGLCREATEPROGRAMPROC original=nullptr;
    inline static unsigned calls=0;
    static GLuint APIENTRY Create(){++calls;return original();}
    EditorUIProgramHook(){original=glad_glCreateProgram;calls=0;glad_glCreateProgram=Create;}
    ~EditorUIProgramHook(){glad_glCreateProgram=original;}
};

void EditorUIValidation(OvEditor::Core::Editor& editor,OvEditor::Core::Context& context,const std::filesystem::path& dir)
{
    // Driver readback performance messages are external input to Console. Keep
    // both UI frames on identical input while forwarding every other diagnostic.
    struct StableDiagnostics {
        GLDEBUGPROC callback=nullptr;const void* user=nullptr;
        static void APIENTRY Forward(GLenum source,GLenum type,GLuint id,GLenum severity,
            GLsizei length,const GLchar* message,const void* parameter) {
            const auto& self=*static_cast<const StableDiagnostics*>(parameter);
            if(type!=GL_DEBUG_TYPE_PERFORMANCE&&self.callback)
                self.callback(source,type,id,severity,length,message,self.user);
        }
        StableDiagnostics() {
            void* value=nullptr;glGetPointerv(GL_DEBUG_CALLBACK_FUNCTION,&value);callback=reinterpret_cast<GLDEBUGPROC>(value);
            glGetPointerv(GL_DEBUG_CALLBACK_USER_PARAM,&value);user=value;
            glDebugMessageCallback(Forward,this);
        }
        ~StableDiagnostics(){glDebugMessageCallback(callback,user);}
    } stableDiagnostics;
    auto& ui=*context.uiManager;ui.EnableEditorLayoutSave(false);ImGui::GetIO().ConfigInputTextCursorBlink=false;
    auto& scene=*context.sceneManager.GetCurrentScene();Require(!scene.GetActors().empty(),"UI fixture has no actors");
    editor.m_editorActions.SelectActor(*scene.GetActors().back());
    auto* material=context.materialManager.GetResource(":Materials/Default.ovmat");Require(material,"UI material missing");
    editor.m_panelsManager.GetPanelAs<OvEditor::Panels::MaterialEditor>("Material Editor").SetTarget(*material);
    editor.m_panelsManager.GetPanelAs<OvEditor::Panels::AssetProperties>("Asset Properties").SetTarget(":Models/Mannequin.fbx");
    // The window's implicit 4x MSAA resolve occasionally changes one edge pixel
    // even when replaying the same native draw data. Isolate the renderer on an
    // explicit RGBA8 target; keep byte-exact comparisons and the product-window
    // checks in EditorFrameValidation / EditorThreadValidation.
    const auto [width,height]=context.window->GetFramebufferSize();
    baregl::Framebuffer target("Editor UI exact comparison");
    OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,{.width=width,.height=height,
        .internalFormat=baregl::types::EInternalFormat::RGBA8,.useMipMaps=false},false,false);
    const auto output=target.RecordTarget();
    auto clear=[&] {target.Bind();glDisable(GL_SCISSOR_TEST);glClearColor(.08f,.12f,.18f,1);glClear(GL_COLOR_BUFFER_BIT);};
    auto pixels=[&] {const auto [w,h]=context.window->GetFramebufferSize();std::vector<uint8_t> result(size_t(w)*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,result.data());return result;};
    std::ofstream evidence(dir/"editor-ui-frames.bin",std::ios::binary);size_t pairs=0;
    const auto geometry=[] {std::vector<ImDrawVert> vertices;for(const auto* list:ImGui::GetDrawData()->CmdLists)vertices.insert(vertices.end(),list->VtxBuffer.begin(),list->VtxBuffer.end());return vertices;};
    const auto layout=[] {
        std::ostringstream out(std::ios::binary);
        for(const auto* list:ImGui::GetDrawData()->CmdLists) {
            out.write(reinterpret_cast<const char*>(&list->VtxBuffer.Size),sizeof(int));
            out.write(reinterpret_cast<const char*>(&list->IdxBuffer.Size),sizeof(int));
            out.write(reinterpret_cast<const char*>(list->IdxBuffer.Data),list->IdxBuffer.Size*sizeof(ImDrawIdx));
            for(const auto& command:list->CmdBuffer) {
                out.write(reinterpret_cast<const char*>(&command.ClipRect),sizeof(ImVec4));
                out.write(reinterpret_cast<const char*>(&command.ElemCount),sizeof(unsigned));
                out.write(reinterpret_cast<const char*>(&command.IdxOffset),sizeof(unsigned));
                out.write(reinterpret_cast<const char*>(&command.VtxOffset),sizeof(unsigned));
                const uint64_t texture=command.TexRef._TexData?0:command.GetTexID();out.write(reinterpret_cast<const char*>(&texture),sizeof(texture));
            }
        }
        return out.str();
    };
    auto compare=[&](const std::string& name) {
        for(int i=0;i<4;++i){clear();ui.Render();}
        const auto nativeReplay=pixels();
        for(unsigned i=0;i<3;++i){clear();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());Require(pixels()==nativeReplay,"native UI replay itself is not pixel deterministic");}
        const auto expected=pixels();const auto expectedGeometry=geometry();const auto expectedLayout=layout();EditorUIProgramHook program;
        auto frame=ui.RecordRenderCommands(output);const auto actualGeometry=geometry();const auto actualLayout=layout();clear();frame.Execute(*context.driver);const auto actual=pixels();
        Require(EditorUIProgramHook::calls==0,"native/owned UI switch recreated its device program");
        if(actual!=expected) {
            const auto [w,h]=context.window->GetFramebufferSize();
            std::ofstream(dir/"ui-diff-size.txt")<<w<<' '<<h;
            std::ofstream(dir/"ui-diff-expected.rgba",std::ios::binary).write(reinterpret_cast<const char*>(expected.data()),expected.size());
            std::ofstream(dir/"ui-diff-actual.rgba",std::ios::binary).write(reinterpret_cast<const char*>(actual.data()),actual.size());
            std::ofstream(dir/"ui-diff-expected.vertices",std::ios::binary).write(reinterpret_cast<const char*>(expectedGeometry.data()),expectedGeometry.size()*sizeof(ImDrawVert));
            std::ofstream(dir/"ui-diff-actual.vertices",std::ios::binary).write(reinterpret_cast<const char*>(actualGeometry.data()),actualGeometry.size()*sizeof(ImDrawVert));
            std::ofstream(dir/"ui-diff-expected.layout",std::ios::binary)<<expectedLayout;
            std::ofstream(dir/"ui-diff-actual.layout",std::ios::binary)<<actualLayout;
            throw std::runtime_error("Editor UI pixels differ: "+name);
        }
        size_t varied=0;for(size_t i=4;i<actual.size();i+=4)if(std::memcmp(actual.data(),actual.data()+i,3))++varied;
        Require(varied>100,"empty UI image");
        evidence.write(reinterpret_cast<const char*>(actual.data()),static_cast<std::streamsize>(actual.size()));++pairs;
        std::printf("PASS: owned Editor UI panel %s\n",name.c_str());
    };
    for(unsigned cycle=0;cycle<8;++cycle)compare("complete canvas");
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
            for(int i=0;i<4;++i){clear();ui.Render();}expected=pixels();pending.emplace(ui.RecordRenderCommands(output));
        }
        Require(debugger.m_selectedTexture.has_value(),"resource writer unexpectedly mutated UI selection");
        debugger.Update(0);
        Require(glIsTexture(id) && !debugger.m_selectedTexture,"debugger did not separate wrapper and storage lifetime");
        clear();pending->Execute(*context.driver);Require(pixels()==expected,"destroyed debugger texture changed pending UI frame");
        pending.reset();Require(!glIsTexture(id),"debugger storage leaked after final frame release");++pairs;
    }
    ui.SetCanvas(editor.m_canvas);ui.SetCommandRecordingEnabled(false);baregl::Framebuffer::BindDefault();
    Require(glGetError()==GL_NO_ERROR,"GL error after complete Editor UI validation");
    std::printf("PASS: %zu exact complete/per-panel/picker/debugger UI pairs; stack texture storage survives source destruction and releases\n",pairs);
}
