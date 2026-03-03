/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/HAL/DirectX12/DX12Prerequisites.h>
#include <OvRendering/Settings/EDataType.h>
#include <OvRendering/Settings/EPrimitiveMode.h>

#include <vector>

namespace OvRendering::HAL
{
	/**
	* Vertex attribute layout
	*/
	struct VertexAttribute
	{
		uint32_t location = 0;        // Shader input location
		uint32_t size = 3;            // Number of components (1-4)
		Settings::EDataType dataType = Settings::EDataType::FLOAT;
		bool normalized = false;
		uint32_t offset = 0;          // Offset in bytes
		uint32_t bindingSlot = 0;     // Vertex buffer binding slot
	};

	// ========================================================================
	// Vertex buffer binding
	// ========================================================================
	struct VertexBufferBinding
	{
		ID3D12Resource* buffer = nullptr;
		uint32_t stride = 0;
		uint32_t offset = 0;
	};
}

namespace OvRendering::HAL::DX12
{
	/**
	* DX12 Vertex Array (Input Layout)
	*/
	class DX12VertexArray
	{
	public:
		/**
		* Constructor
		*/
		DX12VertexArray();

		/**
		* Destructor
		*/
		~DX12VertexArray();

		/**
		* Set the vertex layout
		* @param p_attributes Array of vertex attributes
		* @param p_attributeCount Number of attributes
		*/
		void SetLayout(const VertexAttribute* p_attributes, uint32_t p_attributeCount);

		/**
		* Bind a vertex buffer
		* @param p_buffer The vertex buffer
		* @param p_bindingSlot Binding slot index
		* @param p_stride Vertex stride
		* @param p_offset Offset in bytes
		*/
		void BindVertexBuffer(ID3D12Resource* p_buffer, uint32_t p_bindingSlot, uint32_t p_stride, uint32_t p_offset = 0);

		/**
		* Bind an index buffer
		*/
		void BindIndexBuffer(ID3D12Resource* p_buffer, DXGI_FORMAT p_format = DXGI_FORMAT_R32_UINT);

		/**
		* Get the input layout
		*/
		const std::vector<D3D12_INPUT_ELEMENT_DESC>& GetInputLayout() const { return m_inputLayout; }

		/**
		* Get the vertex buffer views
		*/
		const std::vector<D3D12_VERTEX_BUFFER_VIEW>& GetVertexBuffers() const { return m_vertexBuffers; }

		/**
		* Get the index buffer view
		*/
		const D3D12_INDEX_BUFFER_VIEW& GetIndexBuffer() const { return m_indexBuffer; }

		/**
		* Get primitive topology
		*/
		D3D_PRIMITIVE_TOPOLOGY GetPrimitiveTopology() const { return m_primitiveTopology; }

		/**
		* Set primitive topology
		*/
		void SetPrimitiveTopology(Settings::EPrimitiveMode p_mode);

		/**
		* Bind the vertex array for rendering
		*/
		void Bind(ID3D12GraphicsCommandList* p_commandList);

		/**
		* Cleanup
		*/
		void Shutdown();

	private:
		std::vector<D3D12_INPUT_ELEMENT_DESC> m_inputLayout;
		std::vector<D3D12_VERTEX_BUFFER_VIEW> m_vertexBuffers;
		std::vector<uint32_t> m_vertexStrides;
		D3D12_INDEX_BUFFER_VIEW m_indexBuffer = {};
		D3D_PRIMITIVE_TOPOLOGY m_primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		ID3D12Resource* m_indexBufferResource = nullptr;
	};
}
