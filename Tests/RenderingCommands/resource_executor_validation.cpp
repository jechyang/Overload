#define main UIValidationMain
#include "ui_command_validation.cpp"
#undef main
#include <baregl/ResourceExecutor.h>
#include <baregl/Buffer.h>
#include <baregl/ShaderProgram.h>
#include <baregl/ShaderStage.h>
#include <baregl/VertexArray.h>
#include <OvRendering/Threading/RenderThread.h>
#include <atomic>

using Executor = baregl::ResourceExecutor;
using RenderThread = OvRendering::Threading::RenderThread;
using RenderFrame = OvRendering::Data::RenderFrame;
using List = OvRendering::Data::RenderCommandList;
std::thread::id owner;
std::atomic_size_t checkedCalls{0};

template<class Tag, class F> struct CheckedGL;
template<class Tag, class R, class... Args> struct CheckedGL<Tag, R(APIENTRY*)(Args...)>
{
    inline static R(APIENTRY* original)(Args...);
    static R APIENTRY Call(Args... args)
    {
        Require(std::this_thread::get_id() == owner && glfwGetCurrentContext(), "resource GL escaped its owner");
        ++checkedCalls;
        return original(args...);
    }
};

#define RESOURCE_GL(X) \
    X(CreateBuffers) X(NamedBufferData) X(NamedBufferSubData) X(BindBuffer) X(BindBufferBase) X(DeleteBuffers) \
    X(CreateTextures) X(TextureStorage2D) X(TextureParameteri) X(TextureParameterfv) X(TextureSubImage2D) \
    X(TextureSubImage3D) X(TexImage2D) X(BindTexture) X(BindTextureUnit) X(BindImageTexture) X(GenerateTextureMipmap) X(DeleteTextures) \
    X(CreateRenderbuffers) X(NamedRenderbufferStorage) X(BindRenderbuffer) X(DeleteRenderbuffers) \
    X(CreateFramebuffers) X(NamedFramebufferTexture) X(NamedFramebufferTextureLayer) X(NamedFramebufferRenderbuffer) \
    X(CheckNamedFramebufferStatus) X(NamedFramebufferDrawBuffer) X(NamedFramebufferReadBuffer) X(BindFramebuffer) \
    X(BlitNamedFramebuffer) X(ReadPixels) X(DeleteFramebuffers) X(GetIntegerv) X(PixelStorei) \
    X(CreateVertexArrays) X(BindVertexArray) X(EnableVertexAttribArray) X(DisableVertexAttribArray) \
    X(VertexAttribPointer) X(VertexAttribIPointer) X(VertexAttribLPointer) X(DeleteVertexArrays) \
    X(CreateShader) X(ShaderSource) X(CompileShader) X(GetShaderiv) X(GetShaderInfoLog) X(DeleteShader) \
    X(CreateProgram) X(AttachShader) X(DetachShader) X(LinkProgram) X(GetProgramiv) X(GetProgramInfoLog) \
    X(GetActiveUniform) X(GetUniformLocation) X(GetUniformfv) X(GetUniformiv) X(GetUniformuiv) X(UseProgram) \
    X(ProgramUniform1f) X(ProgramUniform1i) X(ProgramUniform1ui) X(ProgramUniform2f) X(ProgramUniform3f) \
    X(ProgramUniform4f) X(ProgramUniformMatrix3fv) X(ProgramUniformMatrix4fv) X(DeleteProgram)
#define DECLARE(Name) struct Tag##Name {}; using Hook##Name = CheckedGL<Tag##Name, decltype(glad_gl##Name)>;
RESOURCE_GL(DECLARE)
#undef DECLARE
struct GLChecks
{
    GLChecks()
    {
        owner = std::this_thread::get_id();
#define INSTALL(Name) Hook##Name::original = glad_gl##Name; glad_gl##Name = Hook##Name::Call;
        RESOURCE_GL(INSTALL)
#undef INSTALL
    }
    ~GLChecks()
    {
#define RESTORE(Name) glad_gl##Name = Hook##Name::original;
        RESOURCE_GL(RESTORE)
#undef RESTORE
    }
};

void ResourceOperations(RenderThread& thread)
{
    for (int iteration = 0; iteration < 16; ++iteration)
    {
        // Construction, arguments, descriptors and metadata all originate on Main.
        Require(!glfwGetCurrentContext(), "producer unexpectedly owns GL");
        baregl::Buffer vertex, index;
        std::array<float, 6> vertices{0, 0, 1, 0, 0, 1};
        std::array<uint32_t, 3> indices{0, 1, 2};
        vertex.Allocate(sizeof(vertices)); vertex.Upload(vertices.data());
        index.Allocate(sizeof(indices)); index.Upload(indices.data());
        Require(vertex.GetSize() == sizeof(vertices), "buffer size was not published");
        thread.Invoke([&](auto&) {
            std::array<float, 6> actual{};
            glGetNamedBufferSubData(vertex.GetID(), 0, sizeof(actual), actual.data());
            Require(actual == vertices, "borrowed upload bytes changed before owner finished");
        });
        baregl::VertexArray vao;
        vao.SetLayout({baregl::data::FloatVertexAttribute{.count = 2}}, vertex, index);
        vao.Bind(); vao.Unbind(); vao.ResetLayout();

        auto texture = Icon();
        Require(texture->RetainStorage().HasOwnerQueue(), "producer texture missed owner's retirement domain");
        texture->Bind(2); texture->Unbind(); texture->SetBorderColor({1, 0, 0, 1});
        texture->Bind(0, baregl::types::EImageAccessSpecifier::READ_ONLY, baregl::types::EInternalFormat::RGBA8, 0, 0);
        baregl::Texture mipmapped(baregl::types::ETextureType::TEXTURE_2D);
        mipmapped.Allocate({.width=2, .height=2, .internalFormat=baregl::types::EInternalFormat::RGBA8});
        const std::array<uint8_t, 16> solid{255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
        mipmapped.Upload(solid.data(), baregl::types::EFormat::RGBA, baregl::types::EPixelDataType::UNSIGNED_BYTE);
        mipmapped.GenerateMipmaps();
        baregl::Texture cube(baregl::types::ETextureType::TEXTURE_CUBE);
        cube.Allocate({.width=2, .height=2, .internalFormat=baregl::types::EInternalFormat::RGBA8});
        std::array<uint8_t, 96> faces{};
        cube.Upload(faces.data(), baregl::types::EFormat::RGBA, baregl::types::EPixelDataType::UNSIGNED_BYTE);
        cube.GenerateMipmaps();
        baregl::Texture mutableTexture(baregl::types::ETextureType::TEXTURE_2D);
        mutableTexture.Allocate({.width=2, .height=2, .useMipMaps=false, .mutableDesc=baregl::data::MutableTextureDesc{}});
        mutableTexture.Resize(4, 4);
        std::array<uint8_t, 64> expanded{};
        mutableTexture.Upload(expanded.data(), baregl::types::EFormat::RGBA, baregl::types::EPixelDataType::UNSIGNED_BYTE);
        auto depth = std::make_shared<baregl::Renderbuffer>();
        depth->Allocate(2, 2, baregl::types::EInternalFormat::DEPTH_COMPONENT);
        depth->Bind(); depth->Unbind();
        baregl::Framebuffer target;
        target.Attach(texture, baregl::types::EFramebufferAttachment::COLOR);
        target.Attach(depth, baregl::types::EFramebufferAttachment::DEPTH);
        Require(target.Validate(), "producer attachments invalid");
        target.SetTargetDrawBuffer(0); target.SetTargetReadBuffer(0);
        auto old = target.RecordTarget();
        std::array<uint8_t, 16> actual{}, expected{255,32,64,255,20,255,60,255,40,50,255,255,255,255,255,128};
        old.ReadRGBA8(actual); Require(actual == expected, "owner snapshot readback changed pixels");
        actual.fill(0);
        target.ReadPixels(0, 0, 2, 2, baregl::types::EPixelDataFormat::RGBA,
            baregl::types::EPixelDataType::UNSIGNED_BYTE, actual.data());
        Require(actual == expected, "producer ReadPixels escaped or changed pixels");
        target.Resize(8, 4);
        Require(target.GetSize() == std::pair<uint16_t,uint16_t>{8,4} && old.GetSize() == std::pair<uint16_t,uint16_t>{2,2}, "resize rewrote captured dimensions");
        old.ReadRGBA8(actual); Require(actual == expected, "resize overwrote retained storage");
        Reject([&] { target.Resize(0, 4); }, "owner resize exception was swallowed");
        Reject([&] { old.ReadRGBA8(std::span(actual).first(1)); }, "owner readback exception was swallowed");
        old.Bind(); baregl::Framebuffer::BindDefault(); target.BlitToBackBuffer(8, 4);

        baregl::ShaderStage vs(baregl::types::EShaderType::VERTEX), frag(baregl::types::EShaderType::FRAGMENT);
        vs.Upload("#version 450 core\nthis is not valid GLSL");
        const auto failedCompile = vs.Compile();
        Require(!failedCompile.success && !failedCompile.message.empty(), "owner compile diagnostics lost their storage");
        vs.Upload("#version 450 core\nuniform float offset; void main(){gl_Position=vec4(offset,0,0,1);}");
        frag.Upload("#version 450 core\nlayout(location=0) out vec4 color; void main(){color=vec4(1);}");
        Require(vs.Compile().success && frag.Compile().success, "producer shader compile failed");
        baregl::ShaderProgram program, other;
        for (auto* p : {&program, &other})
        {
            p->Attach(vs); p->Attach(frag); Require(p->Link().success, "producer program link failed");
            p->DetachAll();
        }
        program.Bind();
        List commands;
        commands.Append([&other](auto&) { other.Bind(); }); commands.Seal();
        RenderFrame frame; frame.Append(std::move(commands)); frame.Seal();
        auto done = thread.Submit(std::move(frame)); thread.WaitIdle(); done.RethrowIfFailed();
        program.SetUniform<float>("offset", 0.25f);
        Require(program.GetUniform<float>("offset") == 0.25f && other.GetUniform<float>("offset") == 0.f,
            "intervening frame redirected uniform upload to its bound program");
        program.Unbind();
        thread.Invoke([](auto&) { Require(glGetError() == GL_NO_ERROR, "resource execution GL error"); });
    }
    thread.WaitIdle();
    std::puts("PASS: 16 producer-side seven-kind resource cycles, exact uploads/readbacks, retained resize, shader compile/link/uniforms across intervening frames, owner errors and retirement");
}

void ProducerUI(OvWindowing::Window& window, RenderThread& thread)
{
    std::optional<List> retained;
    std::vector<uint8_t> expected;
    std::weak_ptr<baregl::Texture> weak;
    {
        OvUI::Core::UIManager manager(window); manager.EnableEditorLayoutSave(false);
        OvUI::Modules::Canvas canvas;
        OvUI::Panels::PanelWindow panel("Owner-dispatched UI");
        panel.SetPosition({0,0}); panel.SetSize({300,220}); panel.autoSize = false;
        auto icon = Icon(); weak = icon;
        panel.CreateWidget<OvUI::Widgets::Visual::Image>(icon, OvMaths::FVector2{50,50});
        panel.CreateWidget<OvUI::Widgets::Buttons::ButtonImage>(icon, OvMaths::FVector2{40,40});
        panel.CreateWidget<OvUI::Widgets::Texts::Text>("Main CPU / Render GPU");
        canvas.AddPanel(panel); manager.SetCanvas(canvas);
        for (int i = 0; i < 3; ++i)
        {
            thread.Invoke([](auto&) { Clear(); }); manager.Render();
        }
        expected = thread.Invoke([](auto&) { return Pixels(); });
        for (int i = 0; i < 8; ++i)
        {
            retained.emplace(manager.RecordRenderCommands());
            thread.Invoke([&](auto& driver) { Clear(); retained->Execute(driver); Require(Pixels() == expected, "producer UI pixels differ"); });
        }
        retained.emplace(manager.RecordRenderCommands());
    }
    Require(!weak.expired(), "producer UI discarded retained widget storage");
    thread.Invoke([&](auto& driver) { Clear(); retained->Execute(driver); Require(Pixels() == expected, "destroyed producer UI changed replay"); });
    retained.reset(); thread.WaitIdle(); Require(weak.expired(), "producer UI leaked widget storage");
    std::puts("PASS: actual Main-thread UIManager/Canvas builds with contextless resources and backend, 8 exact images and replay after Main destroys the entire source UI");
}

int main() try
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion = 4; ds.contextMinorVersion = 5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible = false; ws.focused = false; ws.width = 320; ws.height = 240; ws.samples = 1;
    OvWindowing::Window window(device, ws); glfwMakeContextCurrent(nullptr);
    std::unique_ptr<GLChecks> checks;
    RenderThread thread([&] {
        window.MakeCurrentContext();
        auto driver = std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{true});
        checks = std::make_unique<GLChecks>(); return driver;
    }, [&] { checks.reset(); glfwMakeContextCurrent(nullptr); });
    size_t dispatches = 0;
    {
        Executor executor([&](std::function<void()> work) { ++dispatches; thread.Invoke([&](auto&) { work(); }); });
        const auto initial = dispatches;
        auto value = Executor::Invoke([] { return Executor::Invoke([] { return std::make_unique<int>(42); }); });
        Require(*value == 42 && dispatches == initial + 1, "nested owner work was dispatched again or lost owning return");
        struct RvalueOnly { int operator()() && { return 7; } };
        Require(Executor::Invoke(RvalueOnly{}) == 7, "callable forwarding changed across dispatch");
        Reject([] { Executor invalid({}); }, "empty dispatcher accepted");
        { Executor nested([&](std::function<void()> work) { thread.Invoke([&](auto&) { work(); }); });
          Require(Executor::Invoke([] { return std::this_thread::get_id(); }) == owner, "nested binding failed"); }
        Reject([] { Executor::Invoke([] { throw std::runtime_error("owner error"); }); }, "owner exception transport failed");
        ResourceOperations(thread); ProducerUI(window, thread);
        auto survivor = Icon(); auto storage = survivor->RetainStorage();
        thread.Shutdown(); thread.RethrowIfFailed();
        Require(!survivor->GetID() && !storage.GetID(), "producer constructor missed owner domain shutdown");
        Reject([] { baregl::Buffer afterShutdown; }, "resource creation after shutdown was accepted");
    }
    const auto main = std::this_thread::get_id();
    Require(Executor::Invoke([] { return std::this_thread::get_id(); }) == main, "resource scope did not restore legacy direct execution");
    std::printf("PASS: scoped/nested dispatch, move-only result, shutdown invalidation/rejection, restored direct path; %zu owner-checked GL calls, %zu producer submissions\n", checkedCalls.load(), dispatches);
    return 0;
}
catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
