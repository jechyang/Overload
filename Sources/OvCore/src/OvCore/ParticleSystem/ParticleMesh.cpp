/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <array>

#include <OvMaths/FVector3.h>
#include <OvRendering/Settings/EAccessSpecifier.h>
#include <OvRendering/Settings/EDataType.h>
#include <OvRendering/Settings/VertexAttribute.h>

#include "OvCore/ParticleSystem/ParticleMesh.h"

OvCore::ParticleSystem::ParticleMesh::ParticleMesh()
{
	// Pre-allocate small buffers; they will grow on demand in Update().
}

void OvCore::ParticleSystem::ParticleMesh::Update(
	std::span<const OvRendering::Geometry::Vertex> p_vertices,
	std::span<const uint32_t> p_indices
)
{
	m_vertexCount = static_cast<uint32_t>(p_vertices.size());
	m_indexCount  = static_cast<uint32_t>(p_indices.size());

	if (m_vertexCount == 0 || m_indexCount == 0)
		return;

	const uint64_t vbSize = p_vertices.size_bytes();
	const uint64_t ibSize = p_indices.size_bytes();

	// Re-allocate only when the buffer is too small
	if (m_vertexBuffer.GetSize() < vbSize)
		m_vertexBuffer.Allocate(vbSize, OvRendering::Settings::EAccessSpecifier::DYNAMIC_DRAW);

	if (m_indexBuffer.GetSize() < ibSize)
		m_indexBuffer.Allocate(ibSize, OvRendering::Settings::EAccessSpecifier::DYNAMIC_DRAW);

	m_vertexBuffer.Upload(p_vertices.data());
	m_indexBuffer.Upload(p_indices.data());

	if (!m_layoutReady)
	{
		SetupLayout();
		m_layoutReady = true;
	}

	// Simple bounding sphere: centre at origin, radius = max distance from origin
	m_boundingSphere.position = OvMaths::FVector3::Zero;
	m_boundingSphere.radius   = 0.0f;
	for (const auto& v : p_vertices)
	{
		const auto& pos = reinterpret_cast<const OvMaths::FVector3&>(v.position);
		m_boundingSphere.radius = std::max(m_boundingSphere.radius, OvMaths::FVector3::Length(pos));
	}
}

void OvCore::ParticleSystem::ParticleMesh::Bind() const
{
	m_vertexArray.Bind();
}

void OvCore::ParticleSystem::ParticleMesh::Unbind() const
{
	m_vertexArray.Unbind();
}

uint32_t OvCore::ParticleSystem::ParticleMesh::GetVertexCount() const
{
	return m_vertexCount;
}

uint32_t OvCore::ParticleSystem::ParticleMesh::GetIndexCount() const
{
	return m_indexCount;
}

const OvRendering::Geometry::BoundingSphere& OvCore::ParticleSystem::ParticleMesh::GetBoundingSphere() const
{
	return m_boundingSphere;
}

void OvCore::ParticleSystem::ParticleMesh::SetupLayout()
{
	m_vertexArray.SetLayout(std::to_array<OvRendering::Settings::VertexAttribute>({
		{ OvRendering::Settings::EDataType::FLOAT, 3 }, // position
		{ OvRendering::Settings::EDataType::FLOAT, 2 }, // texCoords
		{ OvRendering::Settings::EDataType::FLOAT, 3 }, // normal
		{ OvRendering::Settings::EDataType::FLOAT, 3 }, // tangent
		{ OvRendering::Settings::EDataType::FLOAT, 3 }  // bitangent
	}), m_vertexBuffer, m_indexBuffer);
}
