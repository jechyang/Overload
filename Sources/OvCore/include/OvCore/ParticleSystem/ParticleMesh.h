/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <span>
#include <vector>

#include <OvRendering/Geometry/Vertex.h>
#include <OvRendering/Geometry/BoundingSphere.h>
#include <OvRendering/HAL/VertexArray.h>
#include <OvRendering/HAL/VertexBuffer.h>
#include <OvRendering/HAL/IndexBuffer.h>
#include <OvRendering/Resources/IMesh.h>

namespace OvCore::ParticleSystem
{
	/**
	* A dynamic mesh that can be re-uploaded every frame.
	* Used to render billboard quads for each live particle.
	*/
	class ParticleMesh : public OvRendering::Resources::IMesh
	{
	public:
		ParticleMesh();

		/**
		* Re-upload vertex and index data to the GPU.
		* Call this once per frame before submitting the drawable.
		*/
		void Update(
			std::span<const OvRendering::Geometry::Vertex> p_vertices,
			std::span<const uint32_t> p_indices
		);

		virtual void Bind() const override;
		virtual void Unbind() const override;
		virtual uint32_t GetVertexCount() const override;
		virtual uint32_t GetIndexCount() const override;
		virtual const OvRendering::Geometry::BoundingSphere& GetBoundingSphere() const override;

	private:
		void SetupLayout();

	private:
		OvRendering::HAL::VertexArray  m_vertexArray;
		OvRendering::HAL::VertexBuffer m_vertexBuffer;
		OvRendering::HAL::IndexBuffer  m_indexBuffer;
		OvRendering::Geometry::BoundingSphere m_boundingSphere{};

		uint32_t m_vertexCount = 0;
		uint32_t m_indexCount  = 0;
		bool     m_layoutReady = false;
	};
}
