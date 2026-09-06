#define GEOMETRY_VALIDATION_NO_MAIN
#include "geometry_validation.cpp"
#include <sstream>
#include <OvRendering/Resources/Loaders/TextureLoader.h>

using Mat = Data::Material;
std::ostream* materialTrace = nullptr;
size_t materialCalls = 0;
std::unordered_map<GLuint, unsigned>* programTags = nullptr;
std::unordered_map<GLuint, unsigned>* textureTags = nullptr;
PFNGLUSEPROGRAMPROC oldUse;
PFNGLBINDTEXTUREUNITPROC oldTexture;
PFNGLUNIFORM1IPROC oldInt;
PFNGLUNIFORM1FPROC oldFloat;
PFNGLUNIFORM2FPROC oldVec2;
PFNGLUNIFORM3FPROC oldVec3;
PFNGLUNIFORM4FPROC oldVec4;
PFNGLUNIFORMMATRIX3FVPROC oldMat3;
PFNGLUNIFORMMATRIX4FVPROC oldMat4;

template<class... T> void MaterialEvent(unsigned kind, const T&... values)
{
    ++materialCalls;
    if (materialTrace) { Write(*materialTrace, kind); (Write(*materialTrace, values), ...); }
}
void APIENTRY Use(GLuint program) { MaterialEvent(0, programTags->at(program)); oldUse(program); }
void APIENTRY TextureUnit(GLuint slot, GLuint texture) { MaterialEvent(1, slot, textureTags->at(texture)); oldTexture(slot, texture); }
void APIENTRY Int(GLint location, GLint value) { MaterialEvent(2, location, value); oldInt(location, value); }
void APIENTRY Float(GLint location, GLfloat value) { MaterialEvent(3, location, value); oldFloat(location, value); }
void APIENTRY Vec2(GLint location, GLfloat x, GLfloat y) { MaterialEvent(4, location, x, y); oldVec2(location, x, y); }
void APIENTRY Vec3(GLint location, GLfloat x, GLfloat y, GLfloat z) { MaterialEvent(5, location, x, y, z); oldVec3(location, x, y, z); }
void APIENTRY Vec4(GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { MaterialEvent(6, location, x, y, z, w); oldVec4(location, x, y, z, w); }
void APIENTRY Matrix3(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)
{
    MaterialEvent(7, location, count, transpose);
    if (materialTrace) for (int i = 0; i < count * 9; ++i) Write(*materialTrace, value[i]);
    oldMat3(location, count, transpose, value);
}
void APIENTRY Matrix4(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)
{
    MaterialEvent(8, location, count, transpose);
    if (materialTrace) for (int i = 0; i < count * 16; ++i) Write(*materialTrace, value[i]);
    oldMat4(location, count, transpose, value);
}
struct MaterialHooks
{
    bool active = true;
    MaterialHooks()
    {
        oldUse = glad_glUseProgram; glad_glUseProgram = Use;
        oldTexture = glad_glBindTextureUnit; glad_glBindTextureUnit = TextureUnit;
        oldInt = glad_glUniform1i; glad_glUniform1i = Int;
        oldFloat = glad_glUniform1f; glad_glUniform1f = Float;
        oldVec2 = glad_glUniform2f; glad_glUniform2f = Vec2;
        oldVec3 = glad_glUniform3f; glad_glUniform3f = Vec3;
        oldVec4 = glad_glUniform4f; glad_glUniform4f = Vec4;
        oldMat3 = glad_glUniformMatrix3fv; glad_glUniformMatrix3fv = Matrix3;
        oldMat4 = glad_glUniformMatrix4fv; glad_glUniformMatrix4fv = Matrix4;
    }
    void Restore()
    {
        if (!active) return;
        active = false;
        glad_glUseProgram = oldUse; glad_glBindTextureUnit = oldTexture;
        glad_glUniform1i = oldInt; glad_glUniform1f = oldFloat;
        glad_glUniform2f = oldVec2; glad_glUniform3f = oldVec3; glad_glUniform4f = oldVec4;
        glad_glUniformMatrix3fv = oldMat3; glad_glUniformMatrix4fv = oldMat4;
        materialTrace = nullptr; programTags = nullptr; textureTags = nullptr;
    }
    ~MaterialHooks() { Restore(); }
};

std::string PropertyBytes(const Data::MaterialPropertyType& value)
{
    std::ostringstream out(std::ios::binary);
    Write(out, value.index());
    std::visit([&](const auto& v)
    {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) {}
        else if constexpr (std::is_same_v<T, baregl::Texture*>) Write(out, v ? textureTags->at(v->GetID()) : 0u);
        else if constexpr (std::is_same_v<T, Resources::Texture*>) Write(out, v ? textureTags->at(v->GetTexture().GetID()) : 0u);
        else if constexpr (std::is_same_v<T, FMatrix3> || std::is_same_v<T, FMatrix4>) for (auto element : v.data) Write(out, element);
        else Write(out, v);
    }, value);
    return out.str();
}

void MaterialValidation(const std::filesystem::path& dir, bool benchmark = false)
{
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion = 4; ds.contextMinorVersion = 5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible = false; ws.focused = false; ws.width = 64; ws.height = 64; ws.samples = 1;
    OvWindowing::Window window(device, ws); window.MakeCurrentContext();
    Context::Driver driver(Settings::DriverSettings{true});
    const auto shaderFile = dir / "material.ovfx";
    {
        std::ofstream file(shaderFile);
        file << R"(#pass SPECIAL
#feature EXTRA
#shader vertex
#version 450 core
layout(location=0) in vec3 pos;
void main(){ gl_Position=vec4(pos,1); }
#shader fragment
#version 450 core
uniform bool flag=true;
uniform int count=2;
uniform float scalar=0.3;
uniform vec2 pair=vec2(0.1,0.2);
uniform vec3 triple=vec3(0.1,0.2,0.3);
uniform vec4 tint=vec4(1);
uniform mat3 smallMatrix=mat3(1);
uniform mat4 matrix=mat4(1);
uniform sampler2D tex;
uniform samplerCube sky;
#if defined(EXTRA)
uniform float featureValue=0.1;
#endif
#if defined(SPECIAL)
uniform float passValue=0.2;
#endif
out vec4 color;
void main(){
  vec4 v=matrix*vec4(smallMatrix*triple,1);
  v+=texture(tex,pair)+texture(sky,triple)+tint+float(count)+scalar+float(flag);
#if defined(EXTRA)
  v+=featureValue;
#endif
#if defined(SPECIAL)
  v+=passValue;
#endif
  color=v;
})";
    }
    using ShaderLoader = Resources::Loaders::ShaderLoader;
    const auto deleteShader = [](Resources::Shader* p) { ShaderLoader::Destroy(p); };
    std::unique_ptr<Resources::Shader, decltype(deleteShader)> shader(ShaderLoader::Create(shaderFile.string()), deleteShader);
    Require(shader != nullptr, "material test shader missing");
    using TextureLoader = Resources::Loaders::TextureLoader;
    const auto deleteTexture = [](Resources::Texture* p) { TextureLoader::Destroy(p); };
    std::unique_ptr<Resources::Texture, decltype(deleteTexture)> asset(TextureLoader::CreatePixel(30,60,90,255), deleteTexture);
    Require(asset != nullptr, "texture asset missing");
    baregl::Texture fallback(ETextureType::TEXTURE_2D), cube(ETextureType::TEXTURE_CUBE);
    fallback.Allocate({.width=1, .height=1, .internalFormat=EInternalFormat::RGBA8});
    cube.Allocate({.width=1, .height=1, .internalFormat=EInternalFormat::RGBA8});
    std::unordered_map<GLuint, unsigned> programs{{0,0}}, textures{{0,0},{asset->GetTexture().GetID(),1},{fallback.GetID(),2},{cube.GetID(),3}};
    Data::FeatureSet extra{"EXTRA"}, none;
    for (auto pass : {"", "SPECIAL"}) for (const auto* features : {&none, &extra})
    {
        const auto id = shader->GetVariant(std::string_view(pass), *features).GetID();
        programs.try_emplace(id, static_cast<unsigned>(programs.size()));
    }
    programTags = &programs; textureTags = &textures;
    Mat material(shader.get()), other(shader.get());
    std::ofstream states(dir / "material-state.bin", std::ios::binary), trace(dir / "material-trace.bin", std::ios::binary);
    MaterialHooks hooks;
    materialTrace = &trace;
    std::optional<Data::MaterialSignatureSet> previous;
    size_t transactions = 0;
    auto step = [&](Mat& target, std::optional<const std::string_view> pass = std::nullopt, OvTools::Utils::OptRef<const Data::FeatureSet> features = std::nullopt,
        baregl::Texture* empty2D = nullptr, baregl::Texture* emptyCube = nullptr)
    {
        const auto before = materialCalls;
        const auto signature = target.Bind(pass, features, empty2D, emptyCube, previous);
        const bool stable = !previous || signature.stablePropertySignature != previous->stablePropertySignature;
        const bool once = !previous || signature.singleUsePropertySignature != previous->singleUsePropertySignature;
        Write(states, stable); Write(states, once);
        if (stable || once) target.UploadProperties(stable, once, empty2D, emptyCube);
        auto& program = target.GetVariant(pass, features).value();
        std::vector<std::string> names;
        for (const auto& [name, prop] : target.GetProperties()) names.push_back(name);
        std::sort(names.begin(), names.end());
        for (const auto& name : names)
        {
            const auto& prop = target.GetProperties().at(name);
            Write(states, prop.singleUse);
            const auto bytes = PropertyBytes(prop.value); Write(states, bytes.size()); states.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            const auto info = program.GetUniformInfo(name); Write(states, info.has_value());
            if (info)
            {
                const auto location = glGetUniformLocation(program.GetID(), name.c_str());
                float values[16]{};
                glGetUniformfv(program.GetID(), location, values);
                for (auto value : values) Write(states, value);
            }
        }
        previous = signature; target.Unbind();
        Require(glGetError() == GL_NO_ERROR, "material GL error");
        ++transactions;
        return materialCalls - before;
    };
    step(material, {}, {}, &fallback, &cube);
    Require(step(material, {}, {}, &fallback, &cube) == 0, "unchanged material did not reuse signatures");
    material.SetProperty("scalar", 0.75f, true);
    step(material, {}, {}, &fallback, &cube);
    Require(std::get<float>(material.GetProperty("scalar")->value) == 0.3f, "single use was not reset");
    Require(step(material, {}, {}, &fallback, &cube) == 0, "consumption changed legacy cache semantics");
    material.SetProperty("count", 7); step(material, {}, {}, &fallback, &cube);
    material.SetProperty("scalar", 0.42f, true); step(material, {}, {}, &fallback, &cube);
    step(other, {}, {}, &fallback, &cube); step(material, {}, {}, &fallback, &cube);
    material.SetProperty("featureValue", 0.8f, true);
    material.SetProperty("passValue", 0.9f, true);
    step(material, {}, none, &fallback, &cube);
    Require(std::get<float>(material.GetProperty("featureValue")->value) == 0.8f && std::get<float>(material.GetProperty("passValue")->value) == 0.9f, "absent uniforms consumed");
    step(material, "SPECIAL", extra, &fallback, &cube);
    Require(std::get<float>(material.GetProperty("featureValue")->value) == 0.1f && std::get<float>(material.GetProperty("passValue")->value) == 0.2f, "variant uniforms not consumed");
    for (int i = 0; i < 32; ++i)
    {
        material.SetProperty("flag", (i % 2) != 0, i % 3 == 0);
        material.SetProperty("pair", FVector2{float(i), 2}, i % 3 == 1);
        material.SetProperty("triple", FVector3{3,float(i),5});
        material.SetProperty("tint", FVector4{1,2,float(i),4});
        material.SetProperty("smallMatrix", FMatrix3{1,2,3,4,5,6,7,8,float(i+1)});
        material.SetProperty("matrix", FMatrix4{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,float(i+1)}, true);
        material.SetProperty("tex", i % 3 == 0 ? Data::MaterialPropertyType(asset.get()) : i % 3 == 1 ? Data::MaterialPropertyType(&asset->GetTexture()) : Data::MaterialPropertyType(static_cast<baregl::Texture*>(nullptr)), true);
        material.SetProperty("sky", &cube);
        step(material, i % 2 ? "SPECIAL" : "missing-pass", i % 3 ? extra : none, i % 4 ? &fallback : nullptr, &cube);
    }
    // Filter flags and partial type failure retain per-property consumption order.
    material.Bind("SPECIAL", extra, &fallback, &cube);
    material.SetProperty("scalar", 0.9f, true);
    material.UploadProperties(true, false, &fallback, &cube);
    Require(std::get<float>(material.GetProperty("scalar")->value) == 0.9f, "disabled single-use upload consumed value");
    material.UploadProperties(false, true, &fallback, &cube);
    Require(std::get<float>(material.GetProperty("scalar")->value) == 0.3f, "single-use-only upload failed");
    std::vector<std::pair<std::string,std::string>> originals;
    std::string failing;
    for (auto& [name, prop] : material.GetProperties())
    {
        prop.singleUse = true;
        originals.emplace_back(name, PropertyBytes(prop.value));
        if (std::holds_alternative<float>(prop.value)) failing = name;
    }
    Require(!failing.empty(), "no failure injection target");
    material.GetProperties().at(failing).value = std::monostate{};
    bool threw = false;
    try { material.UploadProperties(true, true, &fallback, &cube); } catch (const std::bad_variant_access&) { threw = true; }
    Require(threw, "wrong uniform type accepted");
    bool afterFailure = false;
    for (const auto& [name, before] : originals)
    {
        const auto& prop = material.GetProperties().at(name);
        if (name == failing) { afterFailure = true; Require(std::holds_alternative<std::monostate>(prop.value), "failing property consumed"); }
        else Require(PropertyBytes(prop.value) == (afterFailure ? before : PropertyBytes(prop.defaultValue)), "partial failure consumption order changed");
        const auto bytes = PropertyBytes(prop.value); Write(states, bytes.size()); states.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    material.Unbind();
    materialTrace = nullptr;
#ifdef MATERIAL_COMMANDS_NEW_PATH
    // Preparation must make no GL calls and retain no source material/value.
    const auto beforePrepare = materialCalls;
    std::vector<Mat::UniformUploadCommand> commands;
    const auto binding = [&]
    {
        Mat temporary(shader.get());
        temporary.SetProperty("scalar", 0.625f, true);
        temporary.SetProperty("matrix", FMatrix4{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16});
        temporary.SetProperty("tex", asset.get());
        const auto command = temporary.PrepareBinding("SPECIAL", extra, &fallback, &cube);
        for (const auto& [name, prop] : temporary.GetProperties())
            if (auto upload = Mat::PrepareUniformUpload(command.program, name, prop.value, &fallback, &cube)) commands.push_back(*upload);
        Require(!Mat::PrepareUniformUpload(command.program, "absent", 1.0f), "absent upload recorded");
        Require(std::get<float>(temporary.GetProperty("scalar")->value) == 0.625f, "preparation consumed source");
        temporary.SetShader(nullptr);
        return command;
    }();
    Require(materialCalls == beforePrepare, "CPU preparation issued GL calls");
    Mat::ExecuteBinding(binding);
    for (const auto& command : commands) Mat::ExecuteUniformUpload(binding.program, command);
    Require(binding.program.GetUniform<float>("scalar") == 0.625f, "snapshot lost value after material destruction");
    const auto matrix = binding.program.GetUniform<baregl::math::Mat4>("matrix");
    const auto expected = FMatrix4::Transpose(FMatrix4{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16});
    Require(std::memcmp(&matrix, expected.data, sizeof(matrix)) == 0, "matrix snapshot transposed incorrectly");
    for (const auto& command : commands) if (command.name == "tex") Require(std::get<baregl::Texture*>(command.value) == &asset->GetTexture(), "texture asset was not resolved during preparation");
    Require(glGetError() == GL_NO_ERROR, "snapshot GL error");
    std::puts("PASS: material/value lifetime isolation, CPU-only preparation, resolved texture and matrix snapshots");
#endif
    Require(states.good() && trace.good(), "material output failed");
    std::printf("PASS: %zu material transactions, stable/single-use caches, all uniform types, pass/feature/fallback, textures and partial failure\n", transactions);
    if (benchmark)
    {
        hooks.Restore(); // Timings exclude all tracing/interception overhead.
        material.SetShader(shader.get());
        material.SetFeatures(none);
        previous.reset();
        std::vector<double> samples;
        for (int sample = -32; sample < 256; ++sample)
        {
            const auto start = std::chrono::steady_clock::now();
            for (int repeat = 0; repeat < 32; ++repeat)
            {
                material.SetProperty("scalar", float(repeat) * 0.01f);
                const auto signature = material.Bind({}, {}, &fallback, &cube, previous);
                material.UploadProperties(true, !previous || signature.singleUsePropertySignature != previous->singleUsePropertySignature, &fallback, &cube);
                previous = signature;
                material.Unbind();
            }
            const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 32;
            if (sample >= 0) samples.push_back(elapsed);
        }
        std::sort(samples.begin(), samples.end());
        std::printf("BENCH: material 10 uniforms median=%.3f us p95=%.3f us (256 x 32, no hooks)\n", samples[128], samples[243]);
    }
}

int main(int argc, char** argv) try
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    Require(argc == 2 || argc == 3, "pass an output directory, optionally --benchmark");
    MaterialValidation(argv[1], argc == 3);
    if (argc == 2) { PreparationValidation(); GeometryValidation(argv[1]); ImageValidation(argv[1], false); }
    return 0;
}
catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
