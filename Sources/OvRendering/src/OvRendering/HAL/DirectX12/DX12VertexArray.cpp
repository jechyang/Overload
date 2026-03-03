/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/HAL/DirectX12/DX12VertexArray.h>
#include <OvRendering/HAL/DirectX12/DX12Device.h>
#include <OvRendering/Utils/Conversions.h>

#include <OvDebug/Logger.h>

namespace OvRendering::HAL::DX12
{
	// ========================================================================
	// DX12VertexArray Constructor
	// ========================================================================
	DX12VertexArray::DX12VertexArray()
	{
	}

	// ========================================================================
	// DX12VertexArray Destructor
	// ========================================================================
	DX12VertexArray::~DX12VertexArray()
	{
		Shutdown();
	}

	// ========================================================================
	// SetLayout
	// ========================================================================
	void DX12VertexArray::SetLayout(const VertexAttribute* p_attributes, uint32_t p_attributeCount)
	{
		m_inputLayout.clear();
		m_inputLayout.reserve(p_attributeCount);

		for (uint32_t i = 0; i < p_attributeCount; ++i)
		{
			const VertexAttribute& attr = p_attributes[i];

			// Determine format from size and data type
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

			if (attr.dataType == Settings::EDataType::FLOAT)
			{
				switch (attr.size)
				{
				case 1: format = DXGI_FORMAT_R32_FLOAT; break;
				case 2: format = DXGI_FORMAT_R32G32_FLOAT; break;
				case 3: format = DXGI_FORMAT_R32G32B32_FLOAT; break;
				case 4: format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
				}
			}
			else if (attr.dataType == Settings::EDataType::UNSIGNED_BYTE)
			{
				switch (attr.size)
				{
				case 1: format = DXGI_FORMAT_R8_UINT; break;
				case 2: format = DXGI_FORMAT_R8G8_UINT; break;
				case 3: format = DXGI_FORMAT_R8G8B8A8_UINT; break;
				case 4: format = DXGI_FORMAT_R8G8B8A8_UINT; break;
				}
			}
			else if (attr.dataType == Settings::EDataType::INT)
			{
				switch (attr.size)
				{
				case 1: format = DXGI_FORMAT_R32_SINT; break;
				case 2: format = DXGI_FORMAT_R32G32_SINT; break;
				case 3: format = DXGI_FORMAT_R32G32B32_SINT; break;
				case 4: format = DXGI_FORMAT_R32G32B32A32_SINT; break;
				}
			}

			D3D12_INPUT_ELEMENT_DESC elementDesc = {};
			elementDesc.SemanticName = "TEXCOORD"; // Default semantic
			elementDesc.SemanticIndex = attr.location;
			elementDesc.Format = format;
			elementDesc.InputSlot = attr.bindingSlot;
			elementDesc.AlignedByteOffset = attr.offset;
			elementDesc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			elementDesc.InstanceDataStepRate = 0;

			m_inputLayout.push_back(elementDesc);
		}
	}

	// ========================================================================
	// BindVertexBuffer
	// ========================================================================
	void DX12VertexArray::BindVertexBuffer(ID3D12Resource* p_buffer, uint32_t p_bindingSlot, uint32_t p_stride, uint32_t p_offset)
	{
		if (!p_buffer)
		{
			return;
		}

		// Ensure we have enough slots
		while (m_vertexBuffers.size() <= p_bindingSlot)
		{
			m_vertexBuffers.emplace_back();
			m_vertexStrides.push_back(0);
		}

		D3D12_VERTEX_BUFFER_VIEW& view = m_vertexBuffers[p_bindingSlot];
		view.BufferLocation = p_buffer->GetGPUVirtualAddress();
		view.SizeInBytes = p_stride; // This should be buffer size
		view.StrideInBytes = p_stride;

		m_vertexStrides[p_bindingSlot] = p_stride;
	}

	// ========================================================================
	// BindIndexBuffer
	// ========================================================================
	void DX12VertexArray::BindIndexBuffer(ID3D12Resource* p_buffer, DXGI_FORMAT p_format)
	{
		if (!p_buffer)
		{
			return;
		}

		m_indexBufferResource = p_buffer;
		m_indexBuffer.BufferLocation = p_buffer->GetGPUVirtualAddress();
		m_indexBuffer.SizeInBytes = 0; // Unknown size
		m_indexBuffer.Format = p_format;
	}

	// ========================================================================
	// SetPrimitiveTopology
	// ========================================================================
	void DX12VertexArray::SetPrimitiveTopology(Settings::EPrimitiveMode p_mode)
	{
		m_primitiveTopology = OvRendering::Utils::Conversions::ToD3D12PrimitiveTopology(p_mode);
	}

	// ========================================================================
	// Bind
	// ========================================================================
	void DX12VertexArray::Bind(ID3D12GraphicsCommandList* p_commandList)
	{
		if (!p_commandList)
		{
			return;
		}

		// Bind vertex buffers
		if (!m_vertexBuffers.empty())
		{
			p_commandList->IASetVertexBuffers(0, static_cast<UINT>(m_vertexBuffers.size()), m_vertexBuffers.data());
		}

		// Bind index buffer
		if (m_indexBufferResource)
		{
			p_commandList->IASetIndexBuffer(&m_indexBuffer);
		}

		// Set primitive topology
		p_commandList->IASetPrimitiveTopology(m_primitiveTopology);
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12VertexArray::Shutdown()
	{
		m_inputLayout.clear();
		m_vertexBuffers.clear();
		m_vertexStrides.clear();
		m_indexBuffer = {};
		m_indexBufferResource = nullptr;
	}
}
