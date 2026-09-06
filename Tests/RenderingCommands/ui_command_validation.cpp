#include <algorithm>
#include <array>
#include <cstdio>
#include <crtdbg.h>
#include <limits>
#include <stdexcept>
#include <vector>
#include <baregl/detail/glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <OvUI/Rendering/DrawData.h>
#include <OvUI/Core/UIManager.h>
#include <OvUI/Panels/PanelWindow.h>
#include <OvUI/Widgets/Visual/Image.h>
#include <OvUI/Widgets/Buttons/ButtonImage.h>
#include <OvUI/Widgets/Texts/Text.h>
#include <OvRendering/Context/Driver.h>
#include <OvWindowing/Context/Device.h>
#include <OvWindowing/Window.h>

using OvUI::Rendering::DrawData;
using OvUI::Rendering::DrawDataRecorder;
void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
template<class F> void Reject(F&& fn, const char* text)
{ bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}Require(rejected,text); }
std::shared_ptr<baregl::Texture> Icon()
{
    auto texture=std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_2D);
    texture->Allocate({.width=2,.height=2,.minFilter=baregl::types::ETextureFilteringMode::LINEAR,
        .magFilter=baregl::types::ETextureFilteringMode::LINEAR,.internalFormat=baregl::types::EInternalFormat::RGBA8,.useMipMaps=false});
    const std::array<uint8_t,16> pixels{255,32,64,255,20,255,60,255,40,50,255,255,255,255,255,128};
    texture->Upload(pixels.data(),baregl::types::EFormat::RGBA,baregl::types::EPixelDataType::UNSIGNED_BYTE);return texture;
}
void Clear()
{glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_SCISSOR_TEST);glClearColor(0.08f,0.12f,0.18f,1);glClear(GL_COLOR_BUFFER_BIT);}
std::vector<uint8_t> Pixels()
{std::vector<uint8_t> result(320*240*4);glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,result.data());return result;}
void BeginContext()
{
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={320,240};io.DeltaTime=1.f/60;
    Require(ImGui_ImplOpenGL3_Init("#version 450 core"),"backend init");ImGui_ImplOpenGL3_NewFrame();
}
void EndContext(DrawDataRecorder* recorder=nullptr)
{if(recorder)recorder->ResetFontIdentifiers();ImGui_ImplOpenGL3_Shutdown();ImGui::DestroyContext();}
ImDrawData& Frame(uint32_t icon,int frame,bool large=false)
{
    auto& io=ImGui::GetIO();
    if(frame==4){io.Fonts->Clear();io.Fonts->AddFontDefault();}
    ImGui::NewFrame();auto* list=ImGui::GetForegroundDrawList();
    const std::string text="Owned frame "+std::to_string(frame)+" / glyph "+std::string(1,char('A'+frame));
    list->AddText(ImVec2(14,12),IM_COL32_WHITE,text.c_str());
    list->AddImage(ImTextureID(icon),{20,50},{110,130},{0,1},{1,0});
    list->PushClipRect({140,65},{260,150},true);
    list->AddRectFilled({120,50},{280,165},IM_COL32(80,200,120,190));list->PopClipRect();
    list->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
    list->AddCircleFilled({220,195},20,IM_COL32(240,100,50,255));
    if(large)for(int i=0;i<18000;++i){const float x=float(i%280);list->AddRectFilled({x,220},{x+1,222},IM_COL32_WHITE);}
    ImGui::Render();return *ImGui::GetDrawData();
}

void OwnershipValidation(OvWindowing::Window& window)
{
    for(int iteration=0;iteration<8;++iteration)
    {
        auto icon=Icon();const auto id=icon->GetID();std::weak_ptr<baregl::Texture> weak=icon;
        BeginContext();auto& source=Frame(id,iteration%4,true);
        if(iteration%2){source.DisplayPos={17,23};source.DisplaySize={160,120};source.FramebufferScale={2,2};}
        Clear();ImGui_ImplOpenGL3_RenderDrawData(&source);const auto expected=Pixels();
        ImGui_ImplOpenGL3_DestroyDeviceObjects();ImGui_ImplOpenGL3_NewFrame();
        DrawDataRecorder recorder;std::array owners{icon};
        glfwMakeContextCurrent(nullptr);auto captured=recorder.Capture(source,owners);window.MakeCurrentContext();
        Require(captured.GetVertexCount()>65536,"base vertex path was not exercised");
        // Destroy every source that a shallow copy could still refer to.
        for(auto* list:source.CmdLists)for(auto& vertex:list->VtxBuffer)vertex.pos={0,0};
        EndContext(&recorder);owners[0].reset();icon.reset();
        Require(!weak.expired() && glIsTexture(id),"captured icon lost ownership");
        Clear();captured.Execute();Require(Pixels()==expected,"owned geometry differs after source context destruction");
        Reject([&]{captured.Execute();},"duplicate UI consumption accepted");
        captured={};Require(weak.expired() && !glIsTexture(id),"captured icon was not released");
        Require(glGetError()==GL_NO_ERROR,"GL error after context-independent UI replay");
    }
    std::puts("PASS: 8 exact legacy/owned large-mesh images after source context/backend/icon destruction; single consumption and release");
}

void FontValidation(OvWindowing::Window& window)
{
    auto icon=Icon();std::array owners{icon};std::vector<std::vector<uint8_t>> expected;
    BeginContext();for(int i=0;i<8;++i){auto& source=Frame(icon->GetID(),i);Clear();ImGui_ImplOpenGL3_RenderDrawData(&source);expected.push_back(Pixels());}EndContext();
    BeginContext();DrawDataRecorder recorder;std::vector<DrawData> frames;int creates=0,updates=0;
    for(int i=0;i<8;++i){auto& source=Frame(icon->GetID(),i);
        for(auto* texture:*source.Textures){creates+=texture->Status==ImTextureStatus_WantCreate;updates+=texture->Status==ImTextureStatus_WantUpdates;}
        glfwMakeContextCurrent(nullptr);frames.push_back(recorder.Capture(source,owners));window.MakeCurrentContext();}
    Require(creates>=2 && updates>0,"font create/update/rebuild paths not exercised");
    EndContext(&recorder);
    GLuint unpack=0;glCreateBuffers(1,&unpack);glNamedBufferData(unpack,4096,nullptr,GL_STATIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,13);glPixelStorei(GL_UNPACK_SKIP_ROWS,3);glPixelStorei(GL_UNPACK_SKIP_PIXELS,2);
    // Consume in reverse order: later atlas writes/rebuilds must not mutate earlier frames.
    for(int i=7;i>=0;--i)
    {
        Clear();frames[i].Execute();Require(Pixels()==expected[i],"font generation image differs");
        GLint state=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&state);Require(GLuint(state)==unpack,"unpack buffer polluted");
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&state);Require(state==8,"unpack alignment polluted");
        glGetIntegerv(GL_UNPACK_ROW_LENGTH,&state);Require(state==13,"unpack row length polluted");
        glGetIntegerv(GL_UNPACK_SKIP_ROWS,&state);Require(state==3,"unpack skip rows polluted");
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&state);Require(state==2,"unpack skip pixels polluted");
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glDeleteBuffers(1,&unpack);
    std::puts("PASS: 8 exact reverse-order font update/rebuild images with destroyed source atlas; unpack state restored");
}

size_t recordingTextureDeletes=0;PFNGLDELETETEXTURESPROC originalTextureDelete=nullptr;
void APIENTRY CountTextureDeletes(GLsizei n,const GLuint* ids){recordingTextureDeletes+=n;originalTextureDelete(n,ids);}
void UnknownCallback(const ImDrawList*,const ImDrawCmd*) { throw std::runtime_error("borrowed callback executed"); }
void FailureValidation(OvWindowing::Window& window)
{
    auto icon=Icon();std::array owners{icon};BeginContext();DrawDataRecorder recorder;
    auto& data=Frame(icon->GetID(),0);auto* font=data.Textures->front();const auto initialID=font->TexID;const auto initialStatus=font->Status;
    Reject([&]{recorder.Capture(data);},"unowned external texture accepted");
    Require(font->TexID==initialID && font->Status==initialStatus,"failed capture acknowledged font");
    auto& command=data.CmdLists[0]->CmdBuffer[0];const auto saved=command;
    command.UserCallback=UnknownCallback;Reject([&]{recorder.Capture(data,owners);},"unknown callback accepted");command=saved;
    command.IdxOffset=UINT32_MAX;Reject([&]{recorder.Capture(data,owners);},"bad index range accepted");command=saved;
    command.VtxOffset=UINT32_MAX;Reject([&]{recorder.Capture(data,owners);},"bad vertex offset accepted");command=saved;
    command.ClipRect.x=std::numeric_limits<float>::quiet_NaN();Reject([&]{recorder.Capture(data,owners);},"nonfinite clip accepted");command=saved;
    const auto size=data.DisplaySize;data.DisplaySize.x=std::numeric_limits<float>::infinity();Reject([&]{recorder.Capture(data,owners);},"invalid framebuffer dimensions accepted");data.DisplaySize=size;
    {auto abandoned=recorder.Capture(data,owners);} // Acknowledgement keeps CPU pixels, not a promise of a GPU upload.
    auto recovered=recorder.Capture(data,owners);Clear();recovered.Execute();const auto expected=Pixels();recovered={};
    // Force an atlas update after the preceding frame released its GPU handles.
    auto& updated=Frame(icon->GetID(),1);originalTextureDelete=glad_glDeleteTextures;glad_glDeleteTextures=CountTextureDeletes;
    glfwMakeContextCurrent(nullptr);auto next=recorder.Capture(updated,owners);window.MakeCurrentContext();glad_glDeleteTextures=originalTextureDelete;
    Require(recordingTextureDeletes==0,"capture released an old GPU font generation");Clear();next.Execute();
    EndContext(&recorder);Require(!expected.empty() && glGetError()==GL_NO_ERROR,"capture recovery GL error");
    std::puts("PASS: unowned textures/callbacks/malformed ranges rejected transactionally; abandoned capture and later font update recovered");
}

void ManagerValidation(OvWindowing::Window& window,OvRendering::Context::Driver& driver)
{
    std::optional<OvRendering::Data::RenderCommandList> retained;std::vector<uint8_t> expected;std::weak_ptr<baregl::Texture> weak;
    {
        auto manager=std::make_unique<OvUI::Core::UIManager>(window);manager->EnableEditorLayoutSave(false);
        OvUI::Modules::Canvas canvas;OvUI::Panels::PanelWindow panel("Owned widgets");panel.SetPosition({0,0});panel.SetSize({300,220});panel.autoSize=false;
        auto icon=Icon();weak=icon;panel.CreateWidget<OvUI::Widgets::Visual::Image>(icon,OvMaths::FVector2{50,50});
        panel.CreateWidget<OvUI::Widgets::Buttons::ButtonImage>(icon,OvMaths::FVector2{40,40});
        panel.CreateWidget<OvUI::Widgets::Texts::Text>("Stable UI snapshot");canvas.AddPanel(panel);manager->SetCanvas(canvas);
        for(int i=0;i<3;++i){Clear();manager->Render();}expected=Pixels();
        manager->SetCommandRecordingEnabled(true);retained.emplace(manager->RecordRenderCommands());
        Clear();retained->Execute(driver);Require(Pixels()==expected,"real UIManager widget image differs");
        retained.emplace(manager->RecordRenderCommands());auto token=retained->GetCompletion();retained->Cancel();Require(!token.Succeeded(),"cancelled UI completed");
        retained.emplace(manager->RecordRenderCommands());
        manager.reset();icon.reset(); // panel/canvas also die before retained replay.
    }
    Require(!weak.expired(),"UI command lost widget texture");Clear();retained->Execute(driver);Require(Pixels()==expected,"UIManager destruction changed queued image");
    retained.reset();Require(weak.expired(),"UIManager command retained icon after release");
    // Empty canvas must not replay stale draw data, and mode switches must clear logical font IDs.
    {OvUI::Core::UIManager manager(window);manager.EnableEditorLayoutSave(false);OvUI::Modules::Canvas canvas;manager.SetCanvas(canvas);
     auto empty=manager.RecordRenderCommands();empty.Execute(driver);manager.Render();}
    Require(glGetError()==GL_NO_ERROR,"manager lifecycle GL error");
    std::puts("PASS: real UIManager Canvas Image/ButtonImage legacy/owned pixels, cancellation, source destruction, empty canvas and mode switches");
}
int main() try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=320;ws.height=240;ws.samples=1;
    OvWindowing::Window window(device,ws);window.MakeCurrentContext();OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
    OwnershipValidation(window);FontValidation(window);FailureValidation(window);ManagerValidation(window,driver);
    return 0;
}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
