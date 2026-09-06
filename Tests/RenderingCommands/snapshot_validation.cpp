#define MATERIAL_VALIDATION_NO_MAIN
#include "material_validation.cpp"

#ifdef SNAPSHOT_NEW_PATH

void SnapshotUniformValidation(const std::filesystem::path& dir)
{
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion = 4; ds.contextMinorVersion = 5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible = false; ws.focused = false; ws.width = 32; ws.height = 32;
    OvWindowing::Window window(device, ws); window.MakeCurrentContext();
    Context::Driver driver(Settings::DriverSettings{true});
    using SL = Resources::Loaders::ShaderLoader;
    using TL = Resources::Loaders::TextureLoader;
    const auto deleteShader = [](Resources::Shader* p) { SL::Destroy(p); };
    const auto deleteTexture = [](Resources::Texture* p) { TL::Destroy(p); };
    std::unique_ptr<Resources::Shader, decltype(deleteShader)> shader(SL::Create((dir / "material.ovfx").string()), deleteShader);
    std::unique_ptr<Resources::Texture, decltype(deleteTexture)> asset(TL::CreatePixel(30,60,90,255), deleteTexture);
    Require(shader && asset, "uniform snapshot assets missing");
    auto fallback = std::make_shared<baregl::Texture>(ETextureType::TEXTURE_2D);
    auto cube = std::make_shared<baregl::Texture>(ETextureType::TEXTURE_CUBE);
    fallback->Allocate({.width=1, .height=1, .internalFormat=EInternalFormat::RGBA8});
    cube->Allocate({.width=1, .height=1, .internalFormat=EInternalFormat::RGBA8});
    const std::array owners{asset->RetainTexture(), cube};
    Data::FeatureSet extra{"EXTRA"}, none, missing{"missing-feature"};
    std::unordered_map<GLuint,unsigned> programs{{0,0}}, textures{{0,0},{owners[0]->GetID(),1},{fallback->GetID(),2},{cube->GetID(),3}};
    for (auto pass : {"", "SPECIAL"}) for (const auto* features : {&none, &extra})
        programs.try_emplace(shader->GetVariant(std::string_view(pass), *features).GetID(), static_cast<unsigned>(programs.size()));
    programTags = &programs; textureTags = &textures;
    MaterialHooks hooks;
    auto fill = [&](Mat& material, int seed)
    {
        const float v = static_cast<float>(seed + 1);
        material.SetProperty("flag", seed % 2 == 0, true);
        material.SetProperty("count", seed, true);
        material.SetProperty("scalar", v * 0.01f, true);
        material.SetProperty("pair", FVector2{v,2});
        material.SetProperty("triple", FVector3{3,v,5}, true);
        material.SetProperty("tint", FVector4{1,2,v,4});
        material.SetProperty("smallMatrix", FMatrix3{1,2,3,4,5,6,7,8,v}, true);
        material.SetProperty("matrix", FMatrix4{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,v}, true);
        material.SetProperty("tex", seed % 3 == 0 ? Data::MaterialPropertyType(asset.get()) : seed % 3 == 1 ? Data::MaterialPropertyType(owners[0].get()) : Data::MaterialPropertyType(static_cast<baregl::Texture*>(nullptr)), true);
        material.SetProperty("sky", seed % 2 ? cube.get() : static_cast<baregl::Texture*>(nullptr), true);
        material.SetProperty("featureValue", v * 0.02f, true);
        material.SetProperty("passValue", v * 0.03f, true);
    };
    auto gpuState = [&](baregl::ShaderProgram& program)
    {
        std::ostringstream out(std::ios::binary);
        GLint bound = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &bound); Write(out, bound);
        std::vector<std::string> names;
        for (const auto& [name, info] : program.GetUniforms()) names.push_back(name);
        std::sort(names.begin(), names.end());
        for (const auto& name : names)
        {
            float values[16]{};
            glGetUniformfv(program.GetID(), glGetUniformLocation(program.GetID(), name.c_str()), values);
            for (auto value : values) Write(out, value);
        }
        return out.str();
    };
    for (int i = 0; i < 32; ++i)
    {
        const std::string_view pass = i % 3 == 0 ? "missing-pass" : i % 3 == 1 ? "SPECIAL" : "";
        const auto& features = i % 3 == 0 ? missing : i % 3 == 1 ? extra : none;
        auto source = std::make_unique<Mat>(shader.get());
        Mat oracle(shader.get()), contaminate(shader.get());
        fill(*source, i); fill(oracle, i); fill(contaminate, i+37);
        const auto before = materialCalls;
        auto packet = source->RecordSnapshot(pass, features, fallback, cube, owners);
        Require(materialCalls == before, "uniform snapshot recording called GL");
        std::ostringstream expectedTrace(std::ios::binary), replayTrace(std::ios::binary);
        materialTrace = &expectedTrace;
        oracle.Bind(pass, features, fallback.get(), cube.get());
        oracle.UploadProperties(true,true,fallback.get(),cube.get()); oracle.Unbind();
        materialTrace = nullptr;
        auto& program = shader->GetVariant(pass,features);
        const auto expected = gpuState(program);
        for (const auto& [name, property] : source->GetProperties())
            Require(PropertyBytes(property.value) == PropertyBytes(oracle.GetProperties().at(name).value), "snapshot single-use/absent-uniform consumption differs");
        source.reset();
        // Change every value in the same program, then leave another program bound.
        contaminate.Bind(pass, features, fallback.get(), cube.get());
        contaminate.UploadProperties(true,true,fallback.get(),cube.get()); contaminate.Unbind();
        shader->GetVariant(pass == "SPECIAL" ? "" : "SPECIAL").Bind();
        materialTrace = &replayTrace; packet.Execute(); materialTrace = nullptr;
        Require(replayTrace.str() == expectedTrace.str(), "snapshot GL trace differs from full immediate upload");
        Require(gpuState(program) == expected, "snapshot inherited contaminated uniform state");
        Require(glGetError() == GL_NO_ERROR, "uniform snapshot GL error");
    }
    hooks.Restore(); glUseProgram(0);
    // An active type the material uploader cannot represent must fail at record time.
    const auto unsupportedFile = dir / "unsupported-snapshot.ovfx";
    std::ofstream unsupported(unsupportedFile);
    unsupported << "#shader vertex\n#version 450 core\nvoid main(){gl_Position=vec4(0);}\n#shader fragment\n#version 450 core\nuniform uint unsupported; out vec4 color; void main(){color=vec4(float(unsupported));}\n";
    unsupported.close();
    std::unique_ptr<Resources::Shader, decltype(deleteShader)> invalid(SL::Create(unsupportedFile.string()), deleteShader);
    Require(invalid != nullptr, "unsupported-type shader missing");
    Mat invalidMaterial(invalid.get());
    bool rejected = false;
    try { (void)invalidMaterial.RecordSnapshot(); } catch (const std::invalid_argument&) { rejected = true; }
    Require(rejected, "unsupported uniform inherited external GL state");
    std::puts("PASS: 32 full material snapshots match immediate GL traces and GPU values after contamination, all uniform types/pass/feature/2D/cube/fallback, unsupported type rejection");
}

void SnapshotValidation(const std::filesystem::path& dir)
{
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion = 4; ds.contextMinorVersion = 5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible = false; ws.focused = false; ws.width = 32; ws.height = 32; ws.samples = 1;
    OvWindowing::Window window(device, ws); window.MakeCurrentContext();
    Context::Driver driver(Settings::DriverSettings{true});
    baregl::Framebuffer target("Retained material validation");
    OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target, 32, 32, true, true, false);
    const auto vertices = std::to_array<Geometry::Vertex>({{{-0.8f,-0.8f,0}}, {{0.8f,-0.8f,0}}, {{0,0.8f,0}}});
    const auto indices = std::to_array<uint32_t>({0,1,2});
    Resources::Mesh mesh(vertices, indices);
    auto pso = driver.CreatePipelineState(); pso.culling = false; pso.depthTest = false;
    using SL = Resources::Loaders::ShaderLoader;
    using TL = Resources::Loaders::TextureLoader;
    const auto deleteShader = [](Resources::Shader* p) { SL::Destroy(p); };
    const auto deleteTexture = [](Resources::Texture* p) { TL::Destroy(p); };
    const auto file = dir / "retained.ovfx";
    auto writeShader = [&](bool replacement)
    {
        std::ofstream out(file);
        out << "#pass SPECIAL\n#feature EXTRA\n#shader vertex\n#version 450 core\nlayout(location=0) in vec3 pos; void main(){gl_Position=vec4(pos,1);}\n"
            "#shader fragment\n#version 450 core\nuniform vec4 tint=vec4(1); uniform sampler2D tex; out vec4 color; void main(){color=texture(tex,vec2(0.5))*tint";
        if (replacement) out << "*vec4(0.25,0.25,0.25,1)";
        out << ";}\n";
    };
    auto capture = [&](const auto& setup)
    {
        target.Bind(); driver.SetViewport(0,0,32,32);
        driver.Clear(true, true, true, {0,0,0,1});
        setup(); driver.Draw(pso, mesh);
        std::vector<unsigned char> pixels(32*32*4);
        glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        Require(glGetError() == GL_NO_ERROR, "snapshot replay GL error");
        target.Unbind(); TracyGpuCollect;
        return pixels;
    };
    std::ofstream images(dir / "snapshot-frames.bin", std::ios::binary);
    for (unsigned cycle = 0; cycle < 8; ++cycle)
    {
        writeShader(false);
        std::unique_ptr<Resources::Shader, decltype(deleteShader)> shader(SL::Create(file.string()), deleteShader);
        std::unique_ptr<Resources::Texture, decltype(deleteTexture)> texture(TL::CreatePixel(255,0,0,255), deleteTexture);
        Require(shader && texture, "snapshot assets missing");
        auto source = std::make_unique<Mat>(shader.get());
        source->SetProperty("tex", texture.get());
        const FVector4 tint{0.5f,0.5f,0.5f,1};
        source->SetProperty("tint", tint, true);
        const auto expected = capture([&] { source->Bind(); source->UploadProperties(true,true); source->Unbind(); });
        Require(expected[(16*32+16)*4] > 0, "snapshot reference framebuffer empty");
        source->SetProperty("tint", tint, true);

        // Snapshot failure is atomic for CPU consumption, unlike partial
        // immediate uploads: unowned textures, bad types, and allocation failure.
        source->SetProperty("tex", &texture->GetTexture(), true);
        bool rejected = false;
        try { (void)source->RecordSnapshot(); } catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected && std::get<FVector4>(source->GetProperty("tint")->value).x == 0.5f, "unowned texture accepted or failure consumed values");
        source->SetProperty("tex", static_cast<baregl::Texture*>(nullptr), true);
        rejected = false;
        try { (void)source->RecordSnapshot(); } catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected && std::get<FVector4>(source->GetProperty("tint")->value).x == 0.5f, "null sampler without fallback accepted or consumed source");
        source->SetProperty("tex", texture.get(), true);
        source->SetProperty("tint", 0.25f, true);
        rejected = false;
        try { (void)source->RecordSnapshot(); } catch (const std::bad_variant_access&) { rejected = true; }
        Require(rejected && std::get<Resources::Texture*>(source->GetProperty("tex")->value) == texture.get(), "type failure partially consumed snapshot");
        source->SetProperty("tint", tint, true);
        const auto removed = source->GetProperties().at("tex");
        source->GetProperties().erase("tex");
        rejected = false;
        try { (void)source->RecordSnapshot(); } catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected && std::get<FVector4>(source->GetProperty("tint")->value).x == 0.5f, "incomplete property map accepted or consumed source");
        source->GetProperties().emplace("tex", removed);
        allocationFailureSize = source->GetProperties().size() * sizeof(Mat::UniformUploadCommand);
        allocationFailureCountdown = 0;
        rejected = false;
        try { (void)source->RecordSnapshot(); } catch (const std::bad_alloc&) { rejected = true; }
        allocationFailureCountdown = -1;
        Require(rejected && std::get<FVector4>(source->GetProperty("tint")->value).x == 0.5f, "allocation failure consumed source");

        // Explicit raw/fallback ownership is accepted without taking GL action.
        {
            auto owned = texture->RetainTexture();
            const std::array owners{owned};
            source->SetProperty("tex", owned.get(), true);
            auto raw = source->RecordSnapshot({}, {}, {}, {}, owners);
            Require(capture([&] { raw.Execute(); }) == expected, "owned raw texture replay differs");
            source->SetProperty("tex", static_cast<baregl::Texture*>(nullptr), true);
            source->SetProperty("tint", tint, true);
            auto fallback = source->RecordSnapshot({}, {}, owned);
            Require(capture([&] { fallback.Execute(); }) == expected, "retained fallback replay differs");
        }
        source->SetProperty("tex", texture.get());
        source->SetProperty("tint", tint, true);
        std::optional<Mat::Snapshot> oldPacket;
        {
            std::unordered_map<GLuint,unsigned> programs{{0,0},{shader->GetVariant().GetID(),1}};
            std::unordered_map<GLuint,unsigned> textures{{0,0},{texture->GetTexture().GetID(),1}};
            programTags = &programs; textureTags = &textures;
            MaterialHooks hooks;
            const auto calls = materialCalls;
            oldPacket.emplace(source->RecordSnapshot());
            Require(materialCalls == calls, "recording issued GL calls");
        }
        Require(std::get<FVector4>(source->GetProperty("tint")->value).x == 1, "successful recording did not consume single-use value");
        const auto oldShaderVersion = shader->GetVersion(), oldTextureVersion = texture->GetVersion();
        Require(oldPacket->GetShaderVersion() == oldShaderVersion, "snapshot version mismatch");
        auto program = shader->RetainVariant(); auto gpuTexture = texture->RetainTexture();
        std::weak_ptr<baregl::ShaderProgram> oldProgram = program;
        std::weak_ptr<baregl::Texture> oldTexture = gpuTexture;
        const auto programID = program->GetID(), textureID = gpuTexture->GetID();
        Require(shader->RetainVariant("missing-pass").get() == program.get(), "retained pass fallback differs");
        Require(shader->RetainVariant({}, Data::FeatureSet{"missing-feature"}).get() == program.get(), "retained feature fallback differs");
        program.reset(); gpuTexture.reset();

        // Failed compilation must not publish a generation or invalidate leases.
        if (cycle == 0)
        {
            std::ofstream invalid(file); invalid << "#shader vertex\n#version 450 core\ninvalid shader\n#shader fragment\n#version 450 core\ninvalid shader\n"; invalid.close();
            std::puts("EXPECTED: invalid shader reload rejected below");
            SL::Recompile(*shader, file.string());
            Require(shader->GetVersion() == oldShaderVersion && shader->GetVariant().GetID() == programID, "failed reload published shader");
        }
        writeShader(true); SL::Recompile(*shader, file.string());
        const std::array<uint8_t,4> green{0,255,0,255};
        TL::ReloadFromMemory(*texture, green.data(),1,1,ETextureFilteringMode::NEAREST,ETextureFilteringMode::NEAREST,ETextureWrapMode::REPEAT,ETextureWrapMode::REPEAT,false);
        Require(shader->GetVersion() == oldShaderVersion+1 && texture->GetVersion() == oldTextureVersion+1, "successful reload version mismatch");
        Require(shader->GetVariant().GetID() != programID && texture->GetTexture().GetID() != textureID, "reload modified pinned GPU objects in place");
        Require(!oldProgram.expired() && !oldTexture.expired(), "reload destroyed pinned generation");
        source->SetShader(shader.get()); source->SetProperty("tex", texture.get()); source->SetProperty("tint", tint, true);
        const auto newerExpected = capture([&] { source->Bind(); source->UploadProperties(true,true); source->Unbind(); });
        source->SetProperty("tint", tint, true);
        std::optional<Mat::Snapshot> newPacket(source->RecordSnapshot());
        Require(newerExpected != expected, "replacement shader/texture image did not change");
        std::weak_ptr<baregl::ShaderProgram> newProgram = shader->RetainVariant();
        std::weak_ptr<baregl::Texture> newTexture = texture->RetainTexture();
        source.reset(); shader.reset(); texture.reset();
        Require(!oldProgram.expired() && !newProgram.expired() && !oldTexture.expired() && !newTexture.expired(), "asset destruction invalidated packet");
        // Alternate generations and repeat the same packet after asset deletion.
        for (int replay = 0; replay < 4; ++replay)
        {
            const auto oldImage = capture([&] { oldPacket->Execute(); });
            const auto newImage = capture([&] { newPacket->Execute(); });
            Require(oldImage == expected && newImage == newerExpected, "packet replay mixed resource generations");
            images.write(reinterpret_cast<const char*>(oldImage.data()), static_cast<std::streamsize>(oldImage.size()));
            images.write(reinterpret_cast<const char*>(newImage.data()), static_cast<std::streamsize>(newImage.size()));
        }
        auto moved = std::move(*oldPacket);
        rejected = false;
        try { oldPacket->Execute(); } catch (const std::logic_error&) { rejected = true; }
        Require(rejected, "moved-from snapshot accepted");
        oldPacket.reset(); oldPacket.emplace(std::move(moved));
        glUseProgram(0);
        oldPacket.reset();
        Require(oldProgram.expired() && oldTexture.expired(), "last old packet did not retire resources");
        Require(!glIsProgram(programID) && !glIsTexture(textureID), "old GPU resources leaked");
        newPacket.reset();
        Require(newProgram.expired() && newTexture.expired(), "last new packet did not retire resources");
    }
    Require(images.good(), "snapshot image output failed");
    std::puts("PASS: 8 reload/destroy/retire cycles, 64 exact retained-generation frames, recording rollback, raw/fallback ownership, moved-from rejection");
}
#endif


#ifndef SNAPSHOT_VALIDATION_NO_MAIN
int main(int argc, char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    Require(argc == 2, "pass an output directory");
    MaterialValidation(argv[1]); PreparationValidation(); GeometryValidation(argv[1]); ImageValidation(argv[1],false);
#ifdef SNAPSHOT_NEW_PATH
    SnapshotUniformValidation(argv[1]); SnapshotValidation(argv[1]);
#endif
    return 0;
}
catch (const std::exception& error) { allocationFailureCountdown = -1; std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }

#endif
