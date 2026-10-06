#include <OvRendering/Resources/Texture.h>
// Verify the texture actually loaded by the product material, on its GL owner.
void ValidateLabEnvironment(OvEditor::Core::Context& context, OvCore::SceneSystem::Scene& scene,
    const std::filesystem::path& output) {
    auto* actor = scene.FindActorByName("08 HDRI Skysphere");
    if (!actor) throw std::runtime_error("Missing HDRI sky");
    auto* material = actor->GetComponent<OvCore::ECS::Components::CMaterialRenderer>()->GetMaterialAtIndex(0);
    const auto property = material->GetProperty("u_DiffuseMap");
    if (!property) throw std::runtime_error("Missing sky environment texture");
    auto* asset = std::get<OvRendering::Resources::Texture*>(property->value);
    if (!asset || asset->GetTexture().GetDesc().useMipMaps) throw std::runtime_error("Sky texture unexpectedly missing or mipmapped");
    auto* texture = &asset->GetTexture();
    constexpr size_t bytes = 1024 * 512 * 16;
    std::vector<float> pixels(bytes / sizeof(float));
    const auto read = [&](auto&) {
        GLint width=0, height=0, format=0;
        glGetTextureLevelParameteriv(texture->GetID(), 0, GL_TEXTURE_WIDTH, &width);
        glGetTextureLevelParameteriv(texture->GetID(), 0, GL_TEXTURE_HEIGHT, &height);
        glGetTextureLevelParameteriv(texture->GetID(), 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
        if (width!=1024 || height!=512 || format!=GL_RGBA32F)
            throw std::runtime_error("Sky GPU texture dimensions/format exceeded the baseline budget");
        glGetTextureImage(texture->GetID(), 0, GL_RGBA, GL_FLOAT, static_cast<GLsizei>(bytes), pixels.data());
        if (glGetError()!=GL_NO_ERROR) throw std::runtime_error("Sky texture GPU readback failed");
    };
    if (context.renderThread) context.renderThread->Invoke(read); else read(*context.driver);
    float peak=0;
    for (size_t i=0; i<pixels.size(); i+=4)
        for (size_t c=0; c<3; ++c) peak=std::max(peak, pixels[i+c]);
    if (peak<=1) throw std::runtime_error("Sky texture lost its HDR range");
    std::ofstream file(output / "environment-info.json");
    file << "{\"width\":1024,\"height\":512,\"format\":\"RGBA32F\",\"mipmapped\":false,\"estimatedBytes\":"
        << bytes << ",\"hdrMaxValue\":" << peak << "}\n";
    if (!file) throw std::runtime_error("Cannot write environment texture evidence");
    std::cout << "FrameGraphLab HDR environment texture verification completed\n";
}
