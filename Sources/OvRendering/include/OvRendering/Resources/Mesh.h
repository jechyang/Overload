/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <memory>
#include <span>

#include <baregl/Buffer.h>
#include <baregl/VertexArray.h>
#include <OvRendering/Geometry/Vertex.h>
#include <OvRendering/Geometry/BoundingSphere.h>
#include <OvRendering/Resources/IMesh.h>

namespace OvRendering::Resources
{
	/**
	* Standard mesh of OvRendering
	*/
	class Mesh : public IMesh
	{
	public:
		/**
		* Create a non-skinned mesh with the given vertices, indices and material index
		* @param p_vertices
		* @param p_indices
		* @param p_materialIndex
		*/
		Mesh(
			std::span<const Geometry::Vertex> p_vertices,
			std::span<const uint32_t> p_indices,
			uint32_t p_materialIndex = 0
		);

		/**
		* Create a skinned mesh with the given vertices, indices and material index
		* @param p_vertices
		* @param p_indices
		* @param p_materialIndex
		*/
		Mesh(
			std::span<const Geometry::SkinnedVertex> p_vertices,
			std::span<const uint32_t> p_indices,
			uint32_t p_materialIndex = 0
		);

		/**
		* Bind the mesh (Actually bind its VAO)
		*/
		virtual void Bind() const override;

		/**
		* Unbind the mesh (Actually unbind its VAO)
		*/
		virtual void Unbind() const override;

		/**
		* Returns the number of vertices
		*/
		virtual uint32_t GetVertexCount() const override;

		/**
		* Returns the number of indices
		*/
		virtual uint32_t GetIndexCount() const override;

		/**
		* Returns the bounding sphere of the mesh
		*/
		virtual const OvRendering::Geometry::BoundingSphere& GetBoundingSphere() const override;

		/**
		* Returns the material index of the mesh
		*/
		uint32_t GetMaterialIndex() const;

		/**
		* Returns true if this mesh stores skinning vertex attributes
		*/
		bool HasSkinningData() const;

		Mesh(const Mesh&) = delete;
		Mesh& operator=(const Mesh&) = delete;

		// Retain immutable GPU geometry independently of the Mesh/Model wrapper.
		// Acquire/use/final release on the GL owner before context shutdown.
		std::shared_ptr<const IMesh> RetainGeometry() const;

	private:
		void Upload(std::span<const Geometry::Vertex> p_vertices, std::span<const uint32_t> p_indices);
		void Upload(std::span<const Geometry::SkinnedVertex> p_vertices, std::span<const uint32_t> p_indices);

	private:
		const uint32_t m_vertexCount;
		const uint32_t m_indicesCount;
		const uint32_t m_materialIndex;
		const bool m_hasSkinningData;

		struct GeometryStorage;
		std::shared_ptr<GeometryStorage> m_geometry;
	};
}
