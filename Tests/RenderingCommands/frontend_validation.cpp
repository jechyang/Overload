// Reuse hidden GL context, standard assets, assertion handling and model fixture.
#define main UnusedSkinningValidationMain
#include "../Skinning/serial_validation.cpp"
#undef main
#include <OvCore/Rendering/RenderFrontend.h>
#include <OvCore/Rendering/EngineBufferRenderFeature.h>
#include <OvRendering/Data/RenderFrame.h>
#include <condition_variable>
#include <future>
#include <mutex>
#include <sstream>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

using Frontend = OvCore::Rendering::RenderFrontend;
using SceneSnapshot = OvRendering::Data::RenderSceneSnapshot;
using Prepared = OvRendering::Data::PreparedFrameData;
static_assert(std::is_same_v<decltype(std::declval<const SceneSnapshot&>().GetMeshes()), std::span<const OvRendering::Data::StaticMeshInput>>);
static_assert(std::is_same_v<decltype(std::declval<const Prepared&>().GetDraws()), std::span<const OvRendering::Data::PreparedStaticDraw>>);

// The runner inserts only this call into a test copy of ViewPreparer.cpp.
// No callback/extension is admitted to the production Worker API.
namespace
{
    std::mutex gateMutex;
    std::condition_variable gateChanged;
    bool gateEnabled = false, entered = false, released = false, failPrepare = false;
    std::thread::id prepareThread;
    std::weak_ptr<const SceneSnapshot> watched;
    std::ostream* traceOutput = nullptr;
    PFNGLDRAWELEMENTSPROC oldElements;
    PFNGLDRAWELEMENTSINSTANCEDPROC oldInstances;
    float expectedTime = 0;
    bool newPath = false;
    enum class TraceMode { Disabled, Staged, LiveDiagnostic };
    TraceMode traceMode = TraceMode::Staged;
    constexpr size_t kDrawCount = 8 * 4;
    using EngineConstants = std::array<float, 68>;
    using ReflectionConstants = std::array<float, 32>;
    struct DrawConstants { EngineConstants engine{}; ReflectionConstants reflection{}; };
    ReflectionConstants NeutralReflection()
    {
        ReflectionConstants result{};
        for (size_t i : {4, 9, 14, 19}) result[i] = 1;
        return result;
    }
    std::string bindingPolicy = "clean";
    GLuint poisonBuffer = 0, shortBuffer = 0;
    size_t drawOrdinal = 0;
    void PolluteBindings(unsigned step)
    {
        // Include absent, undersized range, and a full stale probe from another view.
        for (GLuint binding : {0u, 1u})
        {
            if (step % 3 == 0) glBindBufferBase(GL_UNIFORM_BUFFER, binding, 0);
            else if (step % 3 == 1) glBindBufferRange(GL_UNIFORM_BUFFER, binding, shortBuffer, 0, 16);
            else glBindBufferBase(GL_UNIFORM_BUFFER, binding, poisonBuffer);
        }
    }
    void AuditUniformBlocks(GLuint program, bool writeTrace)
    {
        GLint blocks = 0;
        glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &blocks);
        Require(blocks == 2, "Standard active uniform block coverage changed");
        if (writeTrace) Write(*traceOutput, blocks);
        for (GLint block = 0; block < blocks; ++block)
        {
            std::array<char, 256> name{}; GLsizei length = 0;
            GLint binding = 0, required = 0, buffer = 0;
            glGetActiveUniformBlockName(program, block, GLsizei(name.size()), &length, name.data());
            glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_BINDING, &binding);
            glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &required);
            const bool engine = std::string_view(name.data()) == "EngineUBO";
            Require(engine || std::string_view(name.data()) == "ReflectionUBO", "unexpected active uniform block");
            Require(binding == (engine ? 0 : 1) && required == (engine ? 272 : 128), "active uniform block contract changed");
            glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, binding, &buffer);
            Require(buffer != 0, "active uniform block has no buffer");
            GLint64 allocated = 0, start = 0, range = 0;
            glGetNamedBufferParameteri64v(buffer, GL_BUFFER_SIZE, &allocated);
            glGetInteger64i_v(GL_UNIFORM_BUFFER_START, binding, &start);
            glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE, binding, &range);
            // BindBufferBase reports zero start/size; the full data store is available.
            const GLint64 available = range == 0 ? allocated : range;
            Require(start >= 0 && available >= required && start + available <= allocated,
                "active uniform block buffer/range is undersized");
            if (writeTrace)
            {
                Write(*traceOutput, length); traceOutput->write(name.data(), length);
                Write(*traceOutput, binding); Write(*traceOutput, required);
            }
        }
        Require(glGetError() == GL_NO_ERROR, "GL error during uniform block audit");
    }
    GLuint traceBuffer = 0;
    struct ConstantReadback { size_t traceOffset; float time; bool checkTime; };
    std::vector<ConstantReadback> constantReadbacks;
    void TraceDraw(GLenum mode, GLsizei count, GLenum type, unsigned instances)
    {
        Write(*traceOutput, mode); Write(*traceOutput, count); Write(*traceOutput, type); Write(*traceOutput, instances);
        GLint buffer = 0, program = 0;
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &buffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        AuditUniformBlocks(program, true);
        GLint reflectionBuffer = 0;
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 1, &reflectionBuffer);
        GLint64 engineStart = 0, reflectionStart = 0;
        glGetInteger64i_v(GL_UNIFORM_BUFFER_START, 0, &engineStart);
        glGetInteger64i_v(GL_UNIFORM_BUFFER_START, 1, &reflectionStart);
        DrawConstants constants{};
        if (traceMode == TraceMode::LiveDiagnostic)
        {
            // Retain the original intrusive probe solely for reproducing the A1 diagnostic.
            glGetNamedBufferSubData(buffer, engineStart, sizeof(constants.engine), constants.engine.data());
            glGetNamedBufferSubData(reflectionBuffer, reflectionStart, sizeof(constants.reflection), constants.reflection.data());
            Require(constants.reflection == NeutralReflection(), "no-reflection constants changed");
            if (newPath) Require(constants.engine[51] == expectedTime, "captured elapsed time changed");
        }
        else
        {
            // Copy at the draw boundary, but never read the live UBO on the CPU here.
            // Its next object upload must not share a buffer with a synchronous readback.
            const size_t readbackOffset = constantReadbacks.size() * sizeof(constants);
            Require(constantReadbacks.size() < kDrawCount, "constant readback capacity exceeded");
            glCopyNamedBufferSubData(buffer, traceBuffer, engineStart, readbackOffset, sizeof(constants.engine));
            glCopyNamedBufferSubData(reflectionBuffer, traceBuffer, reflectionStart, readbackOffset + sizeof(constants.engine), sizeof(constants.reflection));
            constantReadbacks.push_back({static_cast<size_t>(traceOutput->tellp()), expectedTime, newPath});
        }
        constants.engine[51] = 0; // Old Feature samples wall time; unused by the default Standard variant.
        traceOutput->write(reinterpret_cast<const char*>(&constants), sizeof(constants));
        for (GLenum name : {GL_DEPTH_FUNC, GL_CULL_FACE_MODE, GL_STENCIL_REF, GL_STENCIL_VALUE_MASK,
            GL_STENCIL_WRITEMASK, GL_STENCIL_FUNC, GL_STENCIL_FAIL, GL_STENCIL_PASS_DEPTH_FAIL, GL_STENCIL_PASS_DEPTH_PASS, GL_DEPTH_WRITEMASK})
        { GLint value = 0; glGetIntegerv(name, &value); Write(*traceOutput, value); }
        for (GLenum name : {GL_BLEND, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_CULL_FACE, GL_SCISSOR_TEST, GL_POLYGON_OFFSET_FILL, GL_SAMPLE_ALPHA_TO_COVERAGE}) Write(*traceOutput, glIsEnabled(name));
        std::array<GLint,4> viewport{};glGetIntegerv(GL_VIEWPORT,viewport.data());for(auto value:viewport)Write(*traceOutput,value);
        std::array<GLboolean,4> colorMask{};glGetBooleanv(GL_COLOR_WRITEMASK,colorMask.data());for(auto value:colorMask)Write(*traceOutput,value);
        GLint active=0;glGetProgramiv(program,GL_ACTIVE_UNIFORMS,&active);
        for(GLint u=0;u<active;++u)
        {
            std::array<char,256> name{};GLsizei length=0;GLint size=0;GLenum kind=0;
            glGetActiveUniform(program,u,GLsizei(name.size()),&length,&size,&kind,name.data());
            const GLint location=glGetUniformLocation(program,name.data());if(location<0)continue;
            traceOutput->write(name.data(),length);Write(*traceOutput,kind);
            if(kind==GL_SAMPLER_2D || kind==GL_SAMPLER_CUBE)
            {
                GLint unit=0,previous=0,texture=0;glGetUniformiv(program,location,&unit);
                glGetIntegerv(GL_ACTIVE_TEXTURE,&previous);glActiveTexture(GL_TEXTURE0+unit);
                glGetIntegerv(kind==GL_SAMPLER_2D?GL_TEXTURE_BINDING_2D:GL_TEXTURE_BINDING_CUBE_MAP,&texture);
                glActiveTexture(previous);
                std::array<unsigned char,24> texels{};
                glGetTextureImage(texture,0,GL_RGBA,GL_UNSIGNED_BYTE,GLsizei(texels.size()),texels.data());
                traceOutput->write(reinterpret_cast<const char*>(texels.data()),texels.size());
            }
            else if(kind==GL_BOOL || kind==GL_INT){GLint value=0;glGetUniformiv(program,location,&value);Write(*traceOutput,value);}
            else {std::array<float,16> values{};glGetUniformfv(program,location,values.data());for(auto value:values)Write(*traceOutput,value);}
        }
        std::array<float, 3> emissive{};
        glGetUniformfv(program, glGetUniformLocation(program, "u_EmissiveColor"), emissive.data());
        for (auto f : emissive) Write(*traceOutput, f);
    }
    void APIENTRY Elements(GLenum mode, GLsizei count, GLenum type, const void* indices)
    { TraceDraw(mode,count,type,1); oldElements(mode,count,type,indices);
        if (newPath && bindingPolicy == "poison") PolluteBindings(static_cast<unsigned>(++drawOrdinal)); }
    void APIENTRY Instances(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instances)
    { TraceDraw(mode,count,type,instances); oldInstances(mode,count,type,indices,instances);
        if (newPath && bindingPolicy == "poison") PolluteBindings(static_cast<unsigned>(++drawOrdinal)); }
    struct Hooks
    {
        std::unique_ptr<baregl::Buffer> constants;
        Hooks(std::ostream& out)
        {
            if (traceMode == TraceMode::Disabled) return;
            if (traceMode == TraceMode::Staged)
            {
                constants = std::make_unique<baregl::Buffer>();
                constants->Allocate(kDrawCount * sizeof(DrawConstants), baregl::types::EAccessSpecifier::STREAM_READ);
                traceBuffer = constants->GetID();
            }
            constantReadbacks.clear();
            traceOutput = &out;
            oldElements = glad_glDrawElements; oldInstances = glad_glDrawElementsInstanced;
            glad_glDrawElements = Elements; glad_glDrawElementsInstanced = Instances;
        }
        std::string Finish(std::string trace)
        {
            if (traceMode != TraceMode::Staged) return trace;
            Require(constantReadbacks.size() == kDrawCount, "unexpected staged draw count");
            std::vector<DrawConstants> readbacks(constantReadbacks.size());
            glGetNamedBufferSubData(traceBuffer, 0, readbacks.size() * sizeof(DrawConstants), readbacks.data());
            Require(glGetError() == GL_NO_ERROR, "GL error reading staged constants");
            for (size_t i = 0; i < readbacks.size(); ++i)
            {
                auto& data = readbacks[i]; const auto& record = constantReadbacks[i];
                if (record.checkTime) Require(data.engine[51] == record.time, "captured elapsed time changed");
                Require(data.reflection == NeutralReflection(), "no-reflection constants changed or inherited pollution");
                data.engine[51] = 0;
                std::memcpy(trace.data() + record.traceOffset, &data, sizeof(data));
            }
            return trace;
        }
        ~Hooks()
        {
            if (traceMode == TraceMode::Disabled) return;
            glad_glDrawElements = oldElements; glad_glDrawElementsInstanced = oldInstances;
            traceOutput = nullptr; traceBuffer = 0;
        }
    };
}
void FrontendPrepareEntry()
{
    Require(glfwGetCurrentContext() == nullptr, "Prepare had a GL context");
    std::unique_lock lock(gateMutex);
    prepareThread = std::this_thread::get_id();
    if (!gateEnabled) return;
    entered = true; gateChanged.notify_all();
    Require(gateChanged.wait_for(lock, std::chrono::seconds(10), [] { return released; }), "prepare gate timeout");
    Require(!watched.expired(), "snapshot released while task was running");
}
void FrontendPrepareDraw()
{
    std::lock_guard lock(gateMutex);
    if (gateEnabled && failPrepare) throw std::runtime_error("injected failure after a prepared draw");
}

std::vector<unsigned char> Pixels(const baregl::Framebuffer::Snapshot& target)
{
    target.Bind(); const auto [w,h] = target.GetSize(); std::vector<unsigned char> pixels(size_t(w)*h*4);
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    Require(glGetError() == GL_NO_ERROR, "GL error during frontend replay");
    return pixels;
}
void PopulateStatic(Scene& scene, Model& model, OvCore::Resources::Material& material, unsigned frame)
{
    const std::array<OvRendering::Geometry::Vertex, 3> vertices{{
        {{-.4f,-.4f,0},{0,0},{0,0,1}}, {{.4f,-.4f,0},{1,0},{0,0,1}}, {{0,.4f,0},{.5f,1},{0,0,1}}
    }};
    const std::array<uint32_t,3> indices{0,1,2};
    model.m_meshes.push_back(new OvRendering::Resources::Mesh(vertices,indices,0));
    for (unsigned i = 0; i < 6; ++i)
    {
        auto& actor = scene.CreateActor("static" + std::to_string(i));
        actor.SetGUID(100+i);
        actor.AddComponent<CModelRenderer>().SetModel(&model);
        actor.AddComponent<CMaterialRenderer>().FillWithMaterial(material);
        actor.transform.SetLocalPosition({i == 5 ? 100.f : (float(i%3)-1)*.9f,
            (i == 4 ? .1f : 0.f), -float(i%2)*.2f + float(frame)*.01f});
        if (i == 3) actor.SetActive(false);
        if (i == 4) actor.transform.SetLocalPosition(scene.GetActors()[0]->transform.GetWorldPosition());
    }
}

void BindingContractValidation(const std::filesystem::path& assets, OvRendering::Resources::Shader& standard,
    const std::filesystem::path& dir)
{
    std::ofstream report(dir / "uniform-blocks.txt");
    size_t variants = 0;
    for (const auto& [features, program] : standard.GetVariants().at(""))
    {
        if (features.contains("SKINNING")) continue;
        OvRendering::Core::StaticOpaqueCommandRecorder::ValidateProgram(*program);
        ++variants;
    }
    const auto program = standard.GetVariant().GetID();
    GLint blocks = 0; glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &blocks);
    for (GLint block = 0; block < blocks; ++block)
    {
        std::array<char,256> name{};
        GLint binding = 0, size = 0, count = 0;
        glGetActiveUniformBlockName(program, block, GLsizei(name.size()), nullptr, name.data());
        glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_BINDING, &binding);
        glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
        report << name.data() << " binding=" << binding << " bytes=" << size << '\n';
        glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &count);
        std::vector<GLint> indices(count);
        glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES, indices.data());
        for (auto index : indices)
        {
            const GLuint uniform = static_cast<GLuint>(index);
            GLint offset = 0, stride = 0, row = 0, array = 0; GLenum type = 0;
            glGetActiveUniform(program, uniform, GLsizei(name.size()), nullptr, &array, &type, name.data());
            glGetActiveUniformsiv(program, 1, &uniform, GL_UNIFORM_OFFSET, &offset);
            glGetActiveUniformsiv(program, 1, &uniform, GL_UNIFORM_MATRIX_STRIDE, &stride);
            glGetActiveUniformsiv(program, 1, &uniform, GL_UNIFORM_IS_ROW_MAJOR, &row);
            report << "  " << name.data() << " offset=" << offset << " type=" << type
                << " array=" << array << " matrixStride=" << stride << " rowMajor=" << row << '\n';
        }
    }
    report << "Validated non-skinning default Standard variants: " << variants << '\n';
    const std::array<std::pair<std::string, std::string>, 6> badBlocks{{
        {"layout(std140,binding=2) uniform CustomUBO { vec4 value; };", "value"},
        {"layout(std140,binding=0) uniform ReflectionUBO { vec4 position; };", "position"},
        {"layout(std140,binding=1) uniform ReflectionUBO { vec4 extra[16]; };", "extra[15]"},
        {"layout(std140,binding=1) uniform ReflectionUBO { float position; };", "vec4(position)"},
        {"layout(std140,binding=1) uniform ReflectionUBO { vec4 position; vec4 boxCenter; };", "boxCenter"},
        {"layout(std140,row_major,binding=1) uniform ReflectionUBO { vec4 position; mat4 rotation; };", "rotation[0]"}
    }};
    Frontend frontend;
    for (const auto& [declaration, value] : badBlocks)
    {
        auto* shader = OvRendering::Resources::Loaders::ShaderLoader::CreateFromSource(
            "#version 450 core\nlayout(location=0) in vec3 position; void main(){gl_Position=vec4(position,1);}",
            "#version 450 core\n" + declaration + "\nout vec4 color; void main(){color=" + value + ";}");
        Require(shader != nullptr, "invalid block fixture failed to compile");
        {
            Scene scene(assets,assets); Model model("invalid UBO");
            OvCore::Resources::Material material; material.SetShader(shader);
            material.SetReceiveShadows(false); material.SetReceiveReflections(false);
            PopulateStatic(scene,model,material,0); OvRendering::Entities::Camera camera;
            bool rejected = false;
            try { frontend.Extract({scene,camera,96,64}); } catch (const std::invalid_argument&) { rejected = true; }
            Require(rejected, "unsupported block was published by Extract");
        }
        OvRendering::Resources::Loaders::ShaderLoader::Destroy(shader);
    }
    std::printf("PASS: %zu static Standard variants validated; unknown/binding/size/type/offset/row-major UBOs rejected before publication\n", variants);
}

void FailureValidation(Frontend& frontend, OvTools::Jobs::JobSystem& jobs,
    std::array<std::shared_ptr<const SceneSnapshot>,3> snapshots, OvRendering::Data::PipelineState state)
{
    const auto main = std::this_thread::get_id();
    for (bool cancel : {false, true})
    {
        std::atomic_bool cancelled{false};
        { std::lock_guard lock(gateMutex); gateEnabled = true; entered = false; released = false;
            failPrepare = !cancel; watched = snapshots[cancel ? 1 : 0]; }
        auto signal = std::async(std::launch::async, [&] {
            std::unique_lock lock(gateMutex);
            Require(gateChanged.wait_for(lock,std::chrono::seconds(10),[]{return entered;}), "Worker did not enter");
            Require(prepareThread != main && !watched.expired(), "task owner/lifetime mismatch");
            if (cancel) cancelled.store(true,std::memory_order_release);
            released = true; gateChanged.notify_all();
        });
        std::shared_ptr<const Prepared> published;
        bool rejected = false;
        try { published = frontend.Prepare(std::move(snapshots[cancel ? 1 : 0]),state,&jobs,&cancelled); }
        catch (const std::runtime_error&) { rejected = true; }
        signal.get();
        Require(watched.expired(), "joined failed task retained its input");
        Require(rejected && !published && prepareThread != main, "failure published a frame or ran on main");
        { std::lock_guard lock(gateMutex); gateEnabled = false; }
    }
    bool invalid = false;
    try { frontend.Prepare({},state,&jobs); } catch (const std::invalid_argument&) { invalid = true; }
    Require(invalid, "missing input was published");
    auto recovered = frontend.Prepare(std::move(snapshots[2]),state,&jobs);
    Require(recovered && recovered->GetPreparationThread()!=main, "pool did not recover after failure");
    std::puts("PASS: Worker gate, no GL, failure/cancellation do not publish, input retained through join, recovery");
}

void FrontendValidation(const std::filesystem::path& dir, std::optional<unsigned> selectedMode)
{
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion=4; ds.contextMinorVersion=5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible=false; ws.focused=false; ws.width=96; ws.height=64; ws.samples=1;
    OvWindowing::Window window(device,ws); window.MakeCurrentContext();
    OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
    OvTools::Jobs::JobSystem jobs(1); ServiceLocator::Provide(jobs);
    OvCore::ResourceManagement::ShaderManager shaders; ServiceLocator::Provide(shaders);
    const auto assets=std::filesystem::current_path()/"Data/Engine"; shaders.ProvideAssetPaths(assets,assets);
    auto* shader=shaders.GetResource(":Shaders/Standard.ovfx"); Require(shader,"missing Standard shader");
    BindingContractValidation(assets, *shader, dir);
    baregl::Buffer neutral, poison, undersized;
    neutral.Allocate(128, baregl::types::EAccessSpecifier::STATIC_DRAW);
    const auto neutralData = NeutralReflection(); neutral.Upload(neutralData.data());
    poison.Allocate(512, baregl::types::EAccessSpecifier::STATIC_DRAW);
    std::array<float,128> poisonData{}; poisonData.fill(71.f); poison.Upload(poisonData.data());
    undersized.Allocate(512, baregl::types::EAccessSpecifier::STATIC_DRAW); undersized.Upload(poisonData.data());
    poisonBuffer = poison.GetID(); shortBuffer = undersized.GetID();
    // Negative controls prove the audit catches the legacy missing/124-byte input.
    const auto program = shader->GetVariant().GetID();
    for (unsigned bad = 0; bad < 3; ++bad)
    {
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, poison.GetID());
        if (bad == 0) glBindBufferBase(GL_UNIFORM_BUFFER, 1, 0);
        else glBindBufferRange(GL_UNIFORM_BUFFER, 1, poison.GetID(), 0, bad == 1 ? 124 : 16);
        bool rejected = false;
        try { AuditUniformBlocks(program, false); } catch (const std::runtime_error&) { rejected = true; }
        Require(rejected, "uniform block audit accepted invalid legacy input");
    }
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0); glBindBufferBase(GL_UNIFORM_BUFFER, 1, 0);
    std::puts("PASS: all active UBOs audited; missing/124-byte/short-range negative controls rejected");
    std::array<std::vector<unsigned char>,3> allPixels;
    std::array<std::string,3> allTraces;
    for (unsigned mode=selectedMode.value_or(0); mode<selectedMode.value_or(2)+1; ++mode)
    {
        std::ostringstream trace(std::ios::binary); Hooks hooks(trace);
        newPath = mode != 0;
        for (unsigned frame=0; frame<8; ++frame)
        {
            auto scene=std::make_unique<Scene>(assets,assets);
            auto model=std::make_unique<Model>("A1 triangle");
            auto material=std::make_unique<OvCore::Resources::Material>(); material->SetShader(shader);
            material->SetReceiveShadows(false); material->SetReceiveReflections(false); material->SetBackfaceCulling(false);
            material->SetGPUInstances(frame%2 ? 2 : 1);
            material->SetProperty("u_EmissiveIntensity",.7f);
            material->SetProperty("u_EmissiveColor",FVector3{.3f+float(frame)*.05f,.5f,.2f});
            PopulateStatic(*scene,*model,*material,frame);
            OvRendering::Entities::Camera camera; camera.SetPosition({0,0,3}); camera.SetRotation(FQuaternion(FVector3{0,180,0}));
            camera.SetClearColor({.1f,.15f,.25f});
            if (frame%2) {camera.SetProjectionMode(OvRendering::Settings::EProjectionMode::ORTHOGRAPHIC);camera.SetSize(1.5f);}
            auto target=std::make_unique<baregl::Framebuffer>();
            OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*target,96,64,true,true,true);
            const auto output=target->RecordTarget();
            expectedTime=float(frame)*.125f;
            if (mode==0)
            {
                OvCore::Rendering::SceneRenderer renderer(driver);
                // An explicit valid external input for the unchanged A0 renderer.
                // It does not provide a no-reflection binding when the receiver is disabled.
                neutral.Bind(baregl::types::EBufferType::UNIFORM, 1);

#ifndef FRONTEND_IMMEDIATE_ORACLE
                renderer.SetDrawRecordingEnabled(true);
                renderer.SetCommandRecordingJobs(nullptr);
                renderer.BeginCommandRecording();
#endif
                renderer.AddDescriptor<OvCore::Rendering::SceneRenderer::SceneDescriptor>({.scene=*scene});
                renderer.BeginFrame({.renderWidth=96,.renderHeight=64,.camera=camera,.outputBuffer=*target});
                renderer.DrawFrame(); renderer.EndFrame();

#ifndef FRONTEND_IMMEDIATE_ORACLE
                auto legacy=renderer.FinishCommandRecording(); legacy.Execute(driver);
#endif
            }
            else
            {
                auto frontend=std::make_unique<Frontend>();
                if (frame==0)
                {
                    material->SetProperty("u_EmissiveIntensity",.7f,true);
                    bool rejected=false;
                    try { frontend->Extract({*scene,camera,96,64}); } catch(const std::invalid_argument&) { rejected=true; }
                    Require(rejected && std::get<float>(material->GetProperty("u_EmissiveIntensity")->value)==.7f,
                        "unsupported singleUse was consumed or silently accepted");
                    material->SetProperty("u_EmissiveIntensity",.7f,false);
                    material->SetBlendable(true); rejected=false;
                    try { frontend->Extract({*scene,camera,96,64}); } catch(const std::invalid_argument&) { rejected=true; }
                    Require(rejected,"transparent input silently entered the static pipeline");
                    material->SetBlendable(false);
                    auto invalidView=frontend->Extract({*scene,camera,0,64});
                    const auto baseState=driver.CreatePipelineState();
                    glfwMakeContextCurrent(nullptr); rejected=false;
                    try { frontend->Prepare(std::move(invalidView),baseState,mode==2?&jobs:nullptr); }
                    catch(const std::invalid_argument&) { rejected=true; }
                    Require(rejected,"invalid view was published");
                    window.MakeCurrentContext();
                }
                auto snapshot=frontend->Extract({*scene,camera,96,64,7,9,expectedTime});
                Require(snapshot->GetMeshes().size()==5,"active candidate ownership mismatch");
                auto state=driver.CreatePipelineState();
                std::array<std::shared_ptr<const SceneSnapshot>,3> failures;
                if(mode==2 && frame==0)for(auto& failure:failures)failure=frontend->Extract({*scene,camera,96,64,7,9,expectedTime});
                camera.SetPosition({500,500,500});
                material->SetProperty("u_EmissiveIntensity",0.f); material->SetGPUInstances(7);
                scene.reset(); material.reset(); model.reset();
                glfwMakeContextCurrent(nullptr);
                auto prepared=frontend->Prepare(snapshot,state,mode==2?&jobs:nullptr);
                Require(prepared->GetDraws().size()==4,"frustum list mismatch");
                Require((prepared->GetPreparationThread()==std::this_thread::get_id())==(mode==1),"Prepare execution owner mismatch");
                const auto meshes=prepared->GetScene().GetMeshes();
                std::vector<uint64_t> ordered;
                for(const auto& draw:prepared->GetDraws())ordered.push_back(meshes[draw.sourceIndex].actorId);
                Require(std::find(ordered.begin(),ordered.end(),103)==ordered.end() &&
                    std::find(ordered.begin(),ordered.end(),105)==ordered.end(),"inactive/culled object entered draw list");
                auto commands=frontend->Record(prepared,output);
                if(mode==2 && frame==0)FailureValidation(*frontend,jobs,std::move(failures),state);
                snapshot.reset(); prepared.reset(); frontend.reset();
                window.MakeCurrentContext(); target->Resize(48,32); target.reset();
                OvRendering::Data::RenderFrame product;
                product.Append(std::move(commands)); product.Seal();
                if (bindingPolicy == "poison") PolluteBindings(frame);
                else { glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0); glBindBufferBase(GL_UNIFORM_BUFFER, 1, 0); }
                product.Submit(driver);
                product.Wait();
                product.GetCompletion().RethrowIfFailed();
                Require(product.GetCompletion().Succeeded(),"frontend product frame did not retire successfully");
            }
            auto pixels=Pixels(output);
            size_t nonBackground=0;for(size_t i=4;i<pixels.size();i+=4)if(std::memcmp(pixels.data(),pixels.data()+i,3))++nonBackground;
            Require(nonBackground>100,"empty static image");
            allPixels[mode].insert(allPixels[mode].end(),pixels.begin(),pixels.end());
            TracyGpuCollect;
        }
        allTraces[mode]=hooks.Finish(trace.str());
        std::ofstream imageFile(dir/("frontend-"+std::to_string(mode)+".rgba"),std::ios::binary);
        imageFile.write(reinterpret_cast<const char*>(allPixels[mode].data()),allPixels[mode].size());
        std::ofstream traceFile(dir/("frontend-"+std::to_string(mode)+".draws"),std::ios::binary); traceFile<<allTraces[mode];
    }
    {
        Scene empty(assets,assets); Frontend frontend; OvRendering::Entities::Camera camera;
        baregl::Framebuffer target; OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,16,16,true,true,true);
        const auto output=target.RecordTarget(); const auto state=driver.CreatePipelineState();
        auto snapshot=frontend.Extract({empty,camera,16,16});
        glfwMakeContextCurrent(nullptr);
        auto prepared=frontend.Prepare(std::move(snapshot),state,&jobs);
        Require(prepared->GetDraws().empty(),"empty scene produced geometry");
        auto commands=frontend.Record(std::move(prepared),output);
        window.MakeCurrentContext(); commands.Execute(driver);
        Require(commands.GetState()==OvRendering::Data::RenderCommandList::State::COMPLETED,"empty view did not complete");
        Pixels(output);
    }
    std::puts("PASS: empty view");
    if (!selectedMode || *selectedMode != 0)
        std::puts("PASS: unsupported singleUse/Transparent rejection, invalid view does not publish");
    if (!selectedMode)
    {
        Require(allPixels[0]==allPixels[1] && allPixels[1]==allPixels[2],"old/serial/Worker pixels differ");
        if (traceMode != TraceMode::Disabled)
            Require(allTraces[0]==allTraces[1] && allTraces[1]==allTraces[2],"old/serial/Worker logical draw data differ");
        std::puts("PASS: 8 frames x old/serial/Worker, exact pixels and enabled draw traces");
    }
    else std::printf("PASS: cold mode %u completed 8 frames; cross-process comparison is performed by the runner\n", *selectedMode);
    if (!selectedMode || *selectedMode != 0)
        std::puts("PASS: source scene/material/model destroyed, target resized/destroyed, frontend destroyed before RenderFrame replay");
}
int main(int argc,char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    Require(argc>1,"pass output directory [0|1|2] [staged|none|live]");
    std::optional<unsigned> selectedMode;
    if (argc > 2) { selectedMode = static_cast<unsigned>(std::stoul(argv[2])); Require(*selectedMode < 3, "invalid mode"); }
    if (argc > 3)
    {
        const std::string policy = argv[3];
        Require(policy == "staged" || policy == "none" || policy == "live", "invalid trace policy");
        traceMode = policy == "none" ? TraceMode::Disabled : policy == "live" ? TraceMode::LiveDiagnostic : TraceMode::Staged;
    }
    if (argc > 4) { bindingPolicy = argv[4]; Require(bindingPolicy == "clean" || bindingPolicy == "poison", "invalid binding policy"); }
    FrontendValidation(argv[1], selectedMode); return 0;
}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
