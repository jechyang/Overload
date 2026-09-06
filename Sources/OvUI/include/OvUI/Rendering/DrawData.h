#pragma once
#include <memory>
#include <span>
#include <vector>
#include <imgui.h>
#include <baregl/Texture.h>

namespace OvUI::Rendering
{
    // Single consumer. Captures own geometry, bindings and font pixels; execution
    // and final GPU resource release remain on the GL owner.
    class DrawData
    {
        struct Storage;
        std::shared_ptr<Storage> m_storage;
        explicit DrawData(std::shared_ptr<Storage> storage) : m_storage(std::move(storage)) {}
        friend class DrawDataRecorder;
    public:
        DrawData() = default;
        void Execute() const;
        size_t GetVertexCount() const;
        size_t GetIndexCount() const;
    };

    // Producer only. ImGui font requests are acknowledged after successful capture,
    // using logical IDs; the immutable pixel generations are realized by Execute.
    class DrawDataRecorder
    {
        struct State;
        std::unique_ptr<State> m_state;
    public:
        DrawDataRecorder();
        ~DrawDataRecorder();
        DrawData Capture(const ImDrawData& data, std::span<const std::shared_ptr<baregl::Texture>> textures = {});
        void ResetFontIdentifiers(); // Before switching back to the legacy backend/shutdown.
    };
}
