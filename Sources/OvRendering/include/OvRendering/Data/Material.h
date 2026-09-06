/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <optional>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <variant>

#include <baregl/Texture.h>

#include <OvMaths/FMatrix3.h>
#include <OvMaths/FMatrix4.h>
#include <OvMaths/FVector4.h>

#include <OvRendering/Data/StateMask.h>
#include <OvRendering/Resources/Shader.h>
#include <OvRendering/Resources/Texture.h>
#include <OvRendering/Settings/EProjectionMode.h>
#include <OvTools/Utils/OptRef.h>

namespace OvRendering::Data
{
	using MaterialPropertyType = std::variant<
		std::monostate,
		bool,
		int,
		float,
		OvMaths::FVector2,
		OvMaths::FVector3,
		OvMaths::FVector4,
		OvMaths::FMatrix3,
		OvMaths::FMatrix4,
		baregl::Texture*,	// Texture graphics object
		OvRendering::Resources::Texture*	// Texture asset (serializable)
	>;

	/**
	* Represents a material property to be used as a shader uniform
	*/
	struct MaterialProperty
	{
		MaterialPropertyType value;
		MaterialPropertyType defaultValue;
		bool singleUse;
	};

	/**
	* Signature set for a material, used for caching purposes
	*/
	struct MaterialSignatureSet
	{
		std::size_t bindSignature;
		std::size_t stablePropertySignature;
		std::size_t singleUsePropertySignature;
	};

	/**
	* A material is a combination of a shader and some settings (Material settings and shader settings)
	*/
	class Material
	{
	public:
		using PropertyMap = std::unordered_map<std::string, MaterialProperty>;

		// Synchronous command boundaries. Program/metadata/textures are borrowed
		// and must stay alive and unchanged through execution. Scalar/vector/matrix
		// values are captured by value; no command retains a Material or property.
		// These commands do not permit cross-frame queuing or worker GL calls.
		struct BindingCommand
		{
			baregl::ShaderProgram& program;
			const MaterialSignatureSet signature;
			const bool bindRequired;
		};

		// GPU payloads have trivial copy semantics, avoiding repeated dispatch and
		// matrix copy constructors when returning a command in Debug builds.
		using UniformUploadValue = std::variant<std::monostate, int, float,
			baregl::math::Vec2, baregl::math::Vec3, baregl::math::Vec4,
			baregl::math::Mat3, baregl::math::Mat4, baregl::Texture*>;
		static_assert(std::is_trivially_copyable_v<UniformUploadValue>);

		struct UniformUploadCommand
		{
			const std::string& name; // Owned by the borrowed program's uniform metadata.
			const baregl::types::EUniformType type;
			const UniformUploadValue value; // Matrices are already transposed; textures resolved.
			const std::optional<uint32_t> textureIndex;
		};

		BindingCommand PrepareBinding(
			std::optional<const std::string_view> p_pass = std::nullopt,
			OvTools::Utils::OptRef<const Data::FeatureSet> p_featureSetOverride = std::nullopt,
			baregl::Texture* p_emptyTexture2D = nullptr,
			baregl::Texture* p_emptyTextureCube = nullptr,
			std::optional<MaterialSignatureSet> p_previousMaterialSignature = std::nullopt
		);
		static void ExecuteBinding(const BindingCommand& p_command);

		// CPU-only: reads cached shader metadata, captures one value, and resolves
		// texture assets/fallbacks. Missing uniforms return nullopt without consuming.
		static std::optional<UniformUploadCommand> PrepareUniformUpload(
			const baregl::ShaderProgram& p_program,
			const std::string& p_name,
			const MaterialPropertyType& p_value,
			baregl::Texture* p_emptyTexture2D = nullptr,
			baregl::Texture* p_emptyTextureCube = nullptr
		);
		// Execute on the GL owner with the same program bound and resources stable.
		static void ExecuteUniformUpload(baregl::ShaderProgram& p_program, const UniformUploadCommand& p_command);

		// A material-only packet with owned values and pinned GPU generations.
		// Record/replay/destruction are restricted to the GL owner. Replay binds
		// and uploads every captured uniform. Before returning to an immediate
		// renderer, invalidate its cached signatures (e.g. start a new frame).
		// External viewport, target, geometry and feature buffers are not captured.
		class Snapshot
		{
		public:
			Snapshot(Snapshot&&) noexcept = default;
			Snapshot& operator=(Snapshot&&) noexcept = default;
			Snapshot(const Snapshot&) = delete;
			Snapshot& operator=(const Snapshot&) = delete;
			void Execute() const;
			uint64_t GetShaderVersion() const { return m_shaderVersion; }

		private:
			friend class Material;
			Snapshot() = default;
			std::shared_ptr<baregl::ShaderProgram> m_program;
			std::vector<std::shared_ptr<baregl::Texture>> m_textures;
			std::vector<UniformUploadCommand> m_uniforms;
			uint64_t m_shaderVersion = 0;
		};

		// CPU recording consumes used single-use properties only after the entire
		// packet is ready. Failure leaves them unchanged and issues no GL calls.
		// Asset textures are pinned automatically. Non-null raw textures require a
		// matching owning handle in p_retainedTextures or the fallback arguments;
		// unowned raw textures are rejected. Handles must own their GPU objects.
		// Every active sampler needs a texture/fallback; unsupported uniform types
		// are rejected rather than inheriting mutable GL state.
		Snapshot RecordSnapshot(
			std::optional<const std::string_view> p_pass = std::nullopt,
			OvTools::Utils::OptRef<const Data::FeatureSet> p_featureSetOverride = std::nullopt,
			const std::shared_ptr<baregl::Texture>& p_emptyTexture2D = {},
			const std::shared_ptr<baregl::Texture>& p_emptyTextureCube = {},
			std::span<const std::shared_ptr<baregl::Texture>> p_retainedTextures = {}
		);

		/**
		* Creates a material
		* @param p_shader
		*/
		Material(OvRendering::Resources::Shader* p_shader = nullptr);

		/**
		* Defines the shader to attach to this material instance
		* @param p_shader
		*/
		void SetShader(OvRendering::Resources::Shader* p_shader);

		/**
		* Returns the variant (shader program) given a pass and the current feature set (or an override)
		* @param p_pass
		* @param p_override
		*/
		OvTools::Utils::OptRef<baregl::ShaderProgram> GetVariant(
			std::optional<const std::string_view> p_pass = std::nullopt,
			OvTools::Utils::OptRef<const Data::FeatureSet> p_override = std::nullopt
		) const;

		/**
		* Add missing properties to the material based on the shader, and remove properties that are not used in the shader.
		*/
		void UpdateProperties();

		/**
		* Bind the appropriate shader program based on a given pass and potential feature set override
		* @return Material signature, used to skip redundant binding/upload operations
		* @param p_pass
		* @param p_featureSetOverride
		* @param p_emptyTexture2D (The texture to use if a texture uniform is null)
		* @param p_emptyTextureCube (The texture to use if a texture uniform is null)
		* @param p_previousMaterialSignature (Can be used to skip binding)
		*/
		MaterialSignatureSet Bind(
			std::optional<const std::string_view> p_pass = std::nullopt,
			OvTools::Utils::OptRef<const Data::FeatureSet> p_featureSetOverride = std::nullopt,
			baregl::Texture* p_emptyTexture2D = nullptr,
			baregl::Texture* p_emptyTextureCube = nullptr,
			std::optional<MaterialSignatureSet> p_previousMaterialSignature = std::nullopt
		);

		/**
		* Upload the material properties to the GPU.
		* @param bool uploadStableProperties
		* @parma bool uploadSingleUseProperties
		* @param p_emptyTexture2D (The texture to use if a texture uniform is null)
		* @param p_emptyTextureCube (The texture to use if a texture uniform is null)
		*/
		void UploadProperties(
			bool uploadStableProperties,
			bool uploadSingleUseProperties,
			baregl::Texture* p_emptyTexture2D = nullptr,
			baregl::Texture* p_emptyTextureCube = nullptr
		);

		/**
		* Unbind the material
		* @param p_resetBoundProgram (ensures the last used program is fully unbound to minimize context leaks, only use after your done drawing with this material)
		*/
		void Unbind(bool p_resetBoundProgram = false);

		/**
		* Returns true if the material has a given property
		* @param p_name
		*/
		bool HasProperty(const std::string& p_name) const;

		/**
		* Sets a material property value
		* @note This method expects that the property already exists. If unsure, use TrySetProperty instead.
		* @param p_name
		* @param p_value
		* @param p_singleUse (automatically consume the value after the first use)
		*/
		void SetProperty(const std::string p_name, const MaterialPropertyType& p_value, bool p_singleUse = false);

		/**
		* Sets a material property value if the property exists
		* @param p_name
		* @param p_value
		* @param p_singleUse (automatically consume the value after the first use)
		*/
		bool TrySetProperty(const std::string& p_name, const MaterialPropertyType& p_value, bool p_singleUse = false);

		/**
		* Gets a material property
		* @param p_name
		*/
		OvTools::Utils::OptRef<const MaterialProperty> GetProperty(const std::string p_name) const;

		/**
		* Returns the attached shader
		*/
		OvRendering::Resources::Shader*& GetShader();

		/**
		* Returns true if the material has a shader attached
		*/
		bool HasShader() const;

		/**
		* Returns true if the material is valid
		*/
		bool IsValid() const;

		/**
		* Sets the shader support for orthographic projection
		* @param p_supportOrthographic
		*/
		void SetOrthographicSupport(bool p_supportOrthographic);

		/**
		* Sets the shader support for perspective projection
		* @param p_supportPerspective
		*/
		void SetPerspectiveSupport(bool p_supportPerspective);

		/**
		* Sets the draw order of the material
		* @param p_order
		*/
		void SetDrawOrder(int p_order);

		/**
		* Defines if the material is blendable
		* @param p_blendable
		*/
		void SetBlendable(bool p_blendable);

		/**
		* Defines if the material is used on UI
		* @param p_blendable
		*/
		void SetUserInterface(bool p_userInterface);

		/**
		* Defines if the material has backface culling
		* @param p_backfaceCulling
		*/
		void SetBackfaceCulling(bool p_backfaceCulling);

		/**
		* Defines if the material has frontface culling
		* @param p_frontfaceCulling
		*/
		void SetFrontfaceCulling(bool p_frontfaceCulling);

		/**
		* Defines if the material has depth test
		* @param p_depthTest
		*/
		void SetDepthTest(bool p_depthTest);

		/**
		* Defines if the material has depth writting
		* @param p_depthWriting
		*/
		void SetDepthWriting(bool p_depthWriting);

		/**
		* Defines if the material has color writting
		* @param p_colorWriting
		*/
		void SetColorWriting(bool p_colorWriting);

		/**
		* Sets the shadow casting state of the material
		* @param p_castShadows
		*/
		void SetCastShadows(bool p_castShadows);

		/**
		* Sets the shadow receiving state of the material
		* @param p_receiveShadows
		*/
		void SetReceiveShadows(bool p_receiveShadows);

		/**
		* Sets whether this material should be captured by reflection probes
		* @param p_capturedByReflectionProbes
		*/
		void SetCapturedByReflectionProbes(bool p_capturedByReflectionProbes);

		/**
		* Sets whether this material should receive reflections from reflection probes
		* @param p_receiveReflections
		*/
		void SetReceiveReflections(bool p_receiveReflections);

		/**
		* Defines the number of instances
		* @param p_instances
		*/
		void SetGPUInstances(int p_instances);

		/**
		* Returns the draw order of the material
		*/
		int GetDrawOrder() const;

		/**
		* Returns true if the material is blendable
		*/
		bool IsBlendable() const;

		/**
		* Returns true if the material will be used on UI
		*/
		bool IsUserInterface() const;

		/**
		* Returns true if the material has backface culling
		*/
		bool HasBackfaceCulling() const;

		/**
		* Returns true if the material has frontface culling
		*/
		bool HasFrontfaceCulling() const;

		/**
		* Returns true if the material has depth test
		*/
		bool HasDepthTest() const;

		/**
		* Returns true if the material has depth writing
		*/
		bool HasDepthWriting() const;

		/**
		* Returns true if the material has color writing
		*/
		bool HasColorWriting() const;

		/**
		* Returns true if the material is set to cast shadows
		*/
		bool IsShadowCaster() const;

		/**
		* Returns true if the material is set to receive shadows
		*/
		bool IsShadowReceiver() const;

		/**
		* Returns true if the material is set to be captured by reflection probes
		*/
		bool IsCapturedByReflectionProbes() const;

		/**
		* Returns true if the material is set to receive reflections
		*/
		bool IsReflectionReceiver() const;

		/**
		* Returns the number of instances
		*/
		int GetGPUInstances() const;

		/**
		* Generate a state mask with the current material settings
		*/
		const StateMask GenerateStateMask() const;

		/**
		* Returns the uniforms data of the material
		*/
		PropertyMap& GetProperties();

		/**
		* Returns the feature set of this material
		*/
		Data::FeatureSet& GetFeatures();

		/**
		* Defines the feature set this material should use
		* @param p_features
		*/
		void SetFeatures(const Data::FeatureSet& p_features);

		/**
		* Adds a feature to the material
		* @param p_feature
		*/
		void AddFeature(const std::string& p_feature);

		/**
		* Removes a feature from the material
		* @param p_feature
		*/
		void RemoveFeature(const std::string& p_feature);

		/**
		* Returns true if the material has a feature
		* @param p_feature
		*/
		bool HasFeature(const std::string& p_feature) const;

		/**
		* Returns true if the material supports a feature
		* @param p_feature
		*/
		bool SupportsFeature(const std::string& p_feature) const;

		/**
		* Returns true if the material has a pass
		* @param p_pass
		*/
		bool HasPass(const std::string& p_pass) const;

		/**
		* Returns true if the material supports orthopgraphic projection
		*/
		bool SupportsOrthographic() const;

		/**
		* Returns true if the material supports perspective projection
		*/
		bool SupportsPerspective() const;

		/**
		* Returns true if the material supports the given projection mode
		* @param p_projectionMode
		*/
		bool SupportsProjectionMode(OvRendering::Settings::EProjectionMode p_projectionMode) const;

	protected:
		void InvalidatePropertySignature();

		MaterialSignatureSet CalculateSignature(
			baregl::ShaderProgram& p_selectedProgram,
			baregl::Texture* p_emptyTexture2D = nullptr,
			baregl::Texture* p_emptyTextureCube = nullptr
		);

	protected:
		OvRendering::Resources::Shader* m_shader = nullptr;
		PropertyMap m_properties;
		Data::FeatureSet m_features;
		size_t m_stablePropertySignatureVersion = 0ULL;
		size_t m_singleUsePropertySignatureVersion = 0ULL;
		OvTools::Utils::OptRef<baregl::ShaderProgram> m_programInUse = std::nullopt;

		bool m_supportOrthographic = true;
		bool m_supportPerspective = true;
		bool m_userInterface = false;

		bool m_blendable = false;
		bool m_backfaceCulling = true;
		bool m_frontfaceCulling = false;
		bool m_depthTest = true;
		bool m_depthWriting = true;
		bool m_colorWriting = true;

		bool m_castShadows = true;
		bool m_receiveShadows = true;
		bool m_capturedByReflectionProbes = true;
		bool m_receiveReflections = true;

		int m_gpuInstances = 1;
		int m_drawOrder = 1000;
	};
}
