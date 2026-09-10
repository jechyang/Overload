/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvCore/Rendering/ToolDrawCollector.h>
#include <OvRendering/Resources/Mesh.h>
#include <OvMaths/FQuaternion.h>

namespace OvEditor::Rendering
{
	/**
	* The ShapeDrawer handles the drawing of basic shapes
	*/
	class DebugShapeCollector
	{
	public:
		/**
		* Constructor
		* @param p_renderer
		* @param p_executionPolicy
		*/
		DebugShapeCollector(
			OvCore::Rendering::ToolDrawCollector& collector
		);

		/**
		* Destructor
		*/
		virtual ~DebugShapeCollector();

		/**
		* Draw a line in world space
		* @param p_pso
		* @param p_start
		* @param p_end
		* @param p_color
		* @param p_lineWidth
		*/
		void DrawLine(
			OvRendering::Data::PipelineState p_pso,
			const OvMaths::FVector3& p_start,
			const OvMaths::FVector3& p_end,
			const OvMaths::FVector3& p_color,
			float p_lineWidth = 1.0f,
			bool p_depthTest = true
		);

		/**
		* Draw a box in world space
		* @param p_pso
		* @param p_position
		* @param p_rotation
		* @param p_size
		* @param p_color
		* @param p_lineWidth
		*/
		void DrawBox(
			OvRendering::Data::PipelineState p_pso,
			const OvMaths::FVector3& p_position,
			const OvMaths::FQuaternion& p_rotation,
			const OvMaths::FVector3& p_size,
			const OvMaths::FVector3& p_color,
			float p_lineWidth = 1.0f,
			bool p_depthTest = true
		);

		/**
		* Draw a sphere in world space
		* @param p_pso
		* @param p_position
		* @param p_rotation
		* @param p_radius
		* @param p_color
		* @param p_lineWidth
		*/
		void DrawSphere(
			OvRendering::Data::PipelineState p_pso,
			const OvMaths::FVector3& p_position,
			const OvMaths::FQuaternion& p_rotation,
			float p_radius,
			const OvMaths::FVector3& p_color,
			float p_lineWidth = 1.0f,
			bool p_depthTest = true
		);

		/**
		* Draw a capsule in world space
		* @param p_pso
		* @param p_position
		* @param p_rotation
		* @param p_radius
		* @param p_height
		* @param p_color
		* @param p_lineWidth
		*/
		void DrawCapsule(
			OvRendering::Data::PipelineState p_pso,
			const OvMaths::FVector3& p_position,
			const OvMaths::FQuaternion& p_rotation,
			float p_radius,
			float p_height,
			const OvMaths::FVector3& p_color,
			float p_lineWidth = 1.0f,
			bool p_depthTest = true
		);

	public:
		void Begin(const OvRendering::Data::RenderView& view);

	private:
        OvCore::Rendering::ToolDrawCollector& m_collector;
		OvRendering::Resources::Shader* m_lineShader = nullptr;

		std::unique_ptr<OvRendering::Resources::Mesh> m_lineMesh;
		std::unique_ptr<OvRendering::Data::Material> m_lineMaterial;
	};
}