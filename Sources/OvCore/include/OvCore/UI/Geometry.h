#pragma once
#include <OvCore/ECS/Components/CUI.h>
#include <OvRendering/Resources/IMesh.h>
#include <baregl/Buffer.h>
#include <span>
#include <stdexcept>

namespace OvCore::UI
{
    // UI-owned VAO, independently retained by the prepared frame. No renderer-wide vertex ABI change.
    class Geometry final : public OvRendering::Resources::IMesh
    {
    public:
        Geometry(std::span<const ECS::Components::UIVertex> vertices, std::span<const uint32_t> indices)
            : m_vertices(static_cast<uint32_t>(vertices.size())), m_indices(static_cast<uint32_t>(indices.size()))
        {
            static_assert(sizeof(ECS::Components::UIVertex) == 88);
            if (!m_vertexBuffer.Allocate(vertices.size_bytes()) || !m_indexBuffer.Allocate(indices.size_bytes()))
                throw std::runtime_error("UI buffer allocation failed");
            m_vertexBuffer.Upload(vertices.data()); m_indexBuffer.Upload(indices.data());
            using baregl::data::FloatVertexAttribute;
            using baregl::types::EDataType;
            m_vao.SetLayout({
                FloatVertexAttribute{EDataType::FLOAT,3}, // location 0: position
                FloatVertexAttribute{EDataType::FLOAT,2}, // 1: UV0
                FloatVertexAttribute{EDataType::FLOAT,4}, // 2: color (normalized from Color32 on upload)
                FloatVertexAttribute{EDataType::FLOAT,3}, // 3: normal
                FloatVertexAttribute{EDataType::FLOAT,4}, // 4: tangent
                FloatVertexAttribute{EDataType::FLOAT,2}, // 5: UV1
                FloatVertexAttribute{EDataType::FLOAT,2}, // 6: UV2
                FloatVertexAttribute{EDataType::FLOAT,2}  // 7: UV3
            },m_vertexBuffer,m_indexBuffer);
            m_bounds.position = {}; m_bounds.radius=0;
            for (const auto& vertex : vertices) m_bounds.radius=std::max(m_bounds.radius,OvMaths::FVector3::Distance({},vertex.position));
        }
        void Bind() const override { m_vao.Bind(); }
        void Unbind() const override { m_vao.Unbind(); }
        uint32_t GetVertexCount() const override { return m_vertices; }
        uint32_t GetIndexCount() const override { return m_indices; }
        const OvRendering::Geometry::BoundingSphere& GetBoundingSphere() const override { return m_bounds; }
    private:
        uint32_t m_vertices, m_indices;
        baregl::VertexArray m_vao;
        baregl::Buffer m_vertexBuffer, m_indexBuffer;
        OvRendering::Geometry::BoundingSphere m_bounds;
    };
}
