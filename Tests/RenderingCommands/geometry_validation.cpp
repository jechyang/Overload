// Reuse the real animated-scene fixture, allocation hooks and hidden GL setup.
#define main SkinningValidationMain
#include "../Skinning/serial_validation.cpp"
#undef main
#include <OvRendering/Resources/Loaders/ShaderLoader.h>

using namespace OvRendering;
using namespace baregl::types;

// Fail engine assertions on stderr in this standalone test, without invoking
// Windows JIT debugging. Assertions remain enabled and terminate the test.
namespace OvDebug
{
    void _Assert(const char* expression, const std::string_view message, const std::source_location& location)
    {
        std::fprintf(stderr, "ASSERT: %s: %.*s at %s:%u\n", expression,
            static_cast<int>(message.size()), message.data(), location.file_name(), location.line());
        std::abort();
    }
}

struct ProbeRenderer : Core::ABaseRenderer
{
    using ABaseRenderer::ABaseRenderer;
    void DrawFrame() override {}
#ifdef GEOMETRY_COMMANDS_NEW_PATH
    using ABaseRenderer::PrepareGeometryDraw;
#endif
};

Data::StateMask Mask(unsigned bits)
{
    Data::StateMask mask;
    mask.depthWriting = (bits & 1) != 0;
    mask.colorWriting = (bits & 2) != 0;
    mask.blendable = (bits & 4) != 0;
    mask.frontfaceCulling = (bits & 8) != 0;
    mask.backfaceCulling = (bits & 16) != 0;
    mask.depthTest = (bits & 32) != 0;
    return mask;
}

void PreparationValidation()
{
#ifdef GEOMETRY_COMMANDS_NEW_PATH
    // No GL context exists here. Any mesh access during preparation is a failure.
    struct BorrowedMesh : Resources::IMesh
    {
        void Bind() const override { throw std::runtime_error("prepare bound mesh"); }
        void Unbind() const override { throw std::runtime_error("prepare unbound mesh"); }
        uint32_t GetVertexCount() const override { throw std::runtime_error("prepare read vertices"); }
        uint32_t GetIndexCount() const override { throw std::runtime_error("prepare read indices"); }
        const Geometry::BoundingSphere& GetBoundingSphere() const override { throw std::runtime_error("prepare read bounds"); }
    } mesh;
    for (unsigned bits = 0; bits < 64; ++bits)
    {
        Data::PipelineState base;
        base.stencilFuncRef = 37; base.stencilWriteMask = 0xA5;
        base.lineWidthPow2 = 2; base.scissorTest = true;
        base.cullFace = ECullFace::FRONT_AND_BACK;
        const auto command = [&]
        {
            Data::Material material;
            material.SetGPUInstances(7);
            Entities::Drawable drawable;
            drawable.mesh = mesh; drawable.material = material;
            drawable.stateMask = Mask(bits); drawable.primitiveMode = EPrimitiveMode::LINES;
            const auto prepared = ProbeRenderer::PrepareGeometryDraw(base, drawable);
            material.SetGPUInstances(1); drawable.stateMask = Mask(63 - bits);
            drawable.primitiveMode = EPrimitiveMode::TRIANGLES;
            return prepared;
        }(); // Material and Drawable are gone; only the mesh remains borrowed.
        const auto& p = command.pipelineState;
        Require(&command.mesh == &mesh && command.instances == 7 && command.primitiveMode == EPrimitiveMode::LINES, "command retained mutable draw input");
        Require(p.depthWriting == bool(bits & 1) && p.colorWriting.mask == ((bits & 2) ? 15 : 0) && p.blending == bool(bits & 4) && p.depthTest == bool(bits & 32), "captured draw state changed");
        Require(p.culling == bool(bits & 24), "captured culling changed");
        const auto expectedCull = (bits & 24) == 24 || !(bits & 24) ? ECullFace::FRONT_AND_BACK : (bits & 16) ? ECullFace::BACK : ECullFace::FRONT;
        Require(p.cullFace == expectedCull && p.stencilFuncRef == 37 && p.stencilWriteMask == 0xA5 && p.lineWidthPow2 == 2 && p.scissorTest, "base pass state was lost");
    }
    Data::Material material;
    Entities::Drawable drawable;
    auto reject = [&]
    {
        bool threw = false;
        try { (void)ProbeRenderer::PrepareGeometryDraw({}, drawable); }
        catch (const std::invalid_argument&) { threw = true; }
        Require(threw, "invalid geometry accepted");
    };
    reject(); drawable.mesh = mesh; reject(); drawable.material = material;
    material.SetGPUInstances(0); reject(); material.SetGPUInstances(-1); reject();
    std::puts("PASS: 64 CPU-only snapshots survive Drawable/Material mutation and destruction; invalid input rejected");
#endif
}

// Observe actual GL entry points and then forward to the real driver. This
// compares ordered submission state without any production test branch.
std::ostream* drawTrace = nullptr;
std::array<unsigned, 4> drawCounts{};
PFNGLDRAWELEMENTSPROC realElements;
PFNGLDRAWELEMENTSINSTANCEDPROC realElementsInstanced;
PFNGLDRAWARRAYSPROC realArrays;
PFNGLDRAWARRAYSINSTANCEDPROC realArraysInstanced;
void Record(unsigned kind, GLenum mode, GLsizei count, GLsizei instances, GLenum indexType = 0, GLint first = 0)
{
    ++drawCounts[kind];
    Write(*drawTrace, kind); Write(*drawTrace, mode); Write(*drawTrace, count);
    Write(*drawTrace, instances); Write(*drawTrace, indexType); Write(*drawTrace, first);
    for (auto name : {GL_DEPTH_WRITEMASK, GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_CULL_FACE_MODE, GL_DEPTH_FUNC, GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_EQUATION_RGB})
    {
        GLint value = 0; glGetIntegerv(name, &value); Write(*drawTrace, value);
    }
    GLint mask[4]{}; glGetIntegerv(GL_COLOR_WRITEMASK, mask);
    for (auto value : mask) Write(*drawTrace, value);
}
void APIENTRY Elements(GLenum mode, GLsizei count, GLenum type, const void* indices)
{ Record(0, mode, count, 1, type); realElements(mode, count, type, indices); }
void APIENTRY ElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instances)
{ Record(1, mode, count, instances, type); realElementsInstanced(mode, count, type, indices, instances); }
void APIENTRY Arrays(GLenum mode, GLint first, GLsizei count)
{ Record(2, mode, count, 1, 0, first); realArrays(mode, first, count); }
void APIENTRY ArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances)
{ Record(3, mode, count, instances, 0, first); realArraysInstanced(mode, first, count, instances); }

void GeometryValidation(const std::filesystem::path& dir)
{
    std::puts("Geometry: creating hidden context");
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion = 4; ds.contextMinorVersion = 5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible = false; ws.focused = false; ws.width = 64; ws.height = 64; ws.samples = 1;
    OvWindowing::Window window(device, ws); window.MakeCurrentContext();
    Context::Driver driver(Settings::DriverSettings{true});
    ProbeRenderer renderer(driver);
    std::puts("Geometry: creating shader and mesh");
    using Loader = Resources::Loaders::ShaderLoader;
    const auto destroyShader = [](Resources::Shader* value) { Loader::Destroy(value); };
    std::unique_ptr<Resources::Shader, decltype(destroyShader)> shader(Loader::CreateFromSource(
        "#version 450 core\nlayout(location=0) in vec3 pos; void main(){ gl_Position=vec4(pos,1); }",
        "#version 450 core\nuniform vec4 tint=vec4(0.9,0.2,0.1,0.5); out vec4 color; void main(){ color=tint; }"), destroyShader);
    Require(shader != nullptr, "test shader missing");
    Data::Material material(shader.get());
    const auto vertices = std::to_array<Geometry::Vertex>({{{-0.8f,-0.8f,0}}, {{0.8f,-0.8f,0}}, {{0,0.8f,0}}});
    const auto indices = std::to_array<uint32_t>({0,1,2});
    Resources::Mesh indexed(vertices, indices);
    // Mesh::Upload requires a nonempty index buffer to configure its VAO.
    // Reuse a valid VAO through IMesh while requesting nonindexed submission.
    struct ArrayView : Resources::IMesh
    {
        const Resources::Mesh& backing;
        explicit ArrayView(const Resources::Mesh& mesh) : backing(mesh) {}
        void Bind() const override { backing.Bind(); }
        void Unbind() const override { backing.Unbind(); }
        uint32_t GetVertexCount() const override { return backing.GetVertexCount(); }
        uint32_t GetIndexCount() const override { return 0; }
        const Geometry::BoundingSphere& GetBoundingSphere() const override { return backing.GetBoundingSphere(); }
    } arrays(indexed);
    Entities::Camera camera; camera.SetClearColor({0.1f,0.2f,0.3f});
    baregl::Framebuffer framebuffer("Geometry commands validation");
    OvCore::Rendering::FramebufferUtil::SetupFramebuffer(framebuffer, 64, 64, true, true, false);
    Data::FrameDescriptor frame{.renderWidth=64, .renderHeight=64, .camera=camera, .outputBuffer=framebuffer};
    realElements = glad_glDrawElements; glad_glDrawElements = Elements;
    realElementsInstanced = glad_glDrawElementsInstanced; glad_glDrawElementsInstanced = ElementsInstanced;
    realArrays = glad_glDrawArrays; glad_glDrawArrays = Arrays;
    realArraysInstanced = glad_glDrawArraysInstanced; glad_glDrawArraysInstanced = ArraysInstanced;
    std::ofstream traceFile(dir / "draws.bin", std::ios::binary);
    drawTrace = &traceFile;
    std::ofstream images(dir / "geometry-frames.bin", std::ios::binary);
    std::puts("Geometry: submitting draws");
    size_t visibleFrames = 0;
    // All six state-mask bits, each index/instance branch, alternating primitive
    // modes and shared-material property updates exercise state transitions.
    for (unsigned bits = 0; bits < 64; ++bits) for (unsigned branch = 0; branch < 4; ++branch)
    {
        if (branch == 0 && bits % 16 == 0) std::printf("Geometry: mask %u\n", bits);
        renderer.BeginFrame(frame);
        Entities::Drawable drawable;
        drawable.mesh = branch < 2 ? static_cast<Resources::IMesh&>(indexed) : static_cast<Resources::IMesh&>(arrays);
        drawable.material = material;
        drawable.stateMask = Mask(bits);
        drawable.primitiveMode = bits % 3 == 0 ? EPrimitiveMode::TRIANGLES : bits % 3 == 1 ? EPrimitiveMode::LINES : EPrimitiveMode::POINTS;
        material.SetGPUInstances(branch % 2 ? 3 : 1);
        material.SetProperty("tint", FVector4{float(bits + 1) / 65.0f, 0.8f, 0.2f, 0.5f});
        renderer.DrawEntity(renderer.CreatePipelineState(), drawable);
        std::vector<unsigned char> pixels(64 * 64 * 4);
        glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        bool visible = false;
        for (size_t i = 4; i < pixels.size(); i += 4) visible |= std::memcmp(pixels.data(), pixels.data() + i, 3) != 0;
        visibleFrames += visible;
        images.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        Require(glGetError() == GL_NO_ERROR, "geometry GL error");
        renderer.EndFrame(); TracyGpuCollect;
    }
    glad_glDrawElements = realElements; glad_glDrawElementsInstanced = realElementsInstanced;
    glad_glDrawArrays = realArrays; glad_glDrawArraysInstanced = realArraysInstanced;
    Require(images.good() && traceFile.good() && visibleFrames > 0, "empty geometry output");
    for (auto count : drawCounts) Require(count == 64, "missing index/instance dispatch branch");
    drawTrace = nullptr;
    std::printf("PASS: 256 ordered GL draws / frames, all four dispatch branches, %zu visible frames\n", visibleFrames);
}

#ifndef GEOMETRY_VALIDATION_NO_MAIN
int main(int argc, char** argv) try
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    Require(argc == 2, "pass an output directory");
    PreparationValidation(); GeometryValidation(argv[1]); ImageValidation(argv[1], false);
    return 0;
}
catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
#endif
