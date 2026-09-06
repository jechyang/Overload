/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <format>
#include <cstring>
#include <ranges>
#include <stdexcept>

#include <baregl/Buffer.h>
#include <baregl/Texture.h>
#include <baregl/data/UniformInfo.h>
#include <baregl/math/Vec2.h>
#include <baregl/math/Vec3.h>
#include <baregl/math/Vec4.h>
#include <baregl/math/Mat3.h>
#include <baregl/math/Mat4.h>
#include <tracy/Tracy.hpp>

#include <OvDebug/Assertion.h>
#include <OvDebug/Logger.h>

#include <OvRendering/Data/Material.h>
#include <OvRendering/Resources/Texture.h>

#include <OvTools/Utils/OptRef.h>

namespace
{
	template<typename T>
	inline void HashCombine(std::size_t& seed, const T& value)
	{
		// A large odd constant derived from the golden ratio (64-bit variant).
		// It helps distribute bits more uniformly and reduce clustering/collisions.
		constexpr std::size_t kHashCombineConstant = 0x9e3779b97f4a7c15ULL;

		seed ^= std::hash<T>{}(value)
			+ kHashCombineConstant 
			// Bit mixing. They spread entropy from earlier values so order matters
			// and nearby hashes don't collapse into similar outputs.
			+ (seed << 6)
			+ (seed >> 2);
	}

	OvRendering::Data::MaterialPropertyType UniformToPropertyValue(const std::any& p_uniformValue)
	{
		ZoneScoped;

		using namespace OvMaths;
		using namespace OvRendering;
		using namespace baregl::math;

		auto as = [&]<typename T>() -> std::optional<T> {
			return
				p_uniformValue.type() == typeid(T) ?
				std::optional<T>{std::any_cast<T>(p_uniformValue)} :
				std::nullopt;
		};

		if (auto value = as.operator()<bool>()) return *value;
		if (auto value = as.operator()<int>()) return *value;
		if (auto value = as.operator()<float>()) return *value;
		if (auto value = as.operator()<Vec2>()) return (FVector2&)*value;
		if (auto value = as.operator()<Vec3>()) return (FVector3&)*value;
		if (auto value = as.operator()<Vec4>()) return (FVector4&)*value;
		if (auto value = as.operator()<Mat3>()) return (FMatrix3&)*value;
		if (auto value = as.operator()<Mat4>()) return (FMatrix4&)*value;
		if (auto value = as.operator()<baregl::Texture*>())
		{
			// If the baregl::Texture has a value use it as is
			if (*value) return *value;

			// If it's nullptr, default to an empty OvRendering::Resources::Texture*
			return static_cast<Resources::Texture*>(nullptr);
		}
		if (auto value = as.operator()<Resources::Texture*>()) return *value;

		return std::monostate{};
	}

	void BindTexture(
		baregl::ShaderProgram& p_shader,
		const std::string& p_uniformName,
		baregl::Texture* p_texture,
		baregl::Texture* p_fallback,
		uint32_t p_textureSlot
	)
	{
		if (auto target = p_texture ? p_texture : p_fallback)
		{
			target->Bind(p_textureSlot);
			p_shader.SetUniform<int>(p_uniformName, p_textureSlot);
		}
	}
}

OvRendering::Data::Material::Material(OvRendering::Resources::Shader* p_shader)
{
	SetShader(p_shader);
}

void OvRendering::Data::Material::SetShader(OvRendering::Resources::Shader* p_shader)
{
	m_shader = p_shader;

	if (m_shader)
	{
		m_properties.clear();
		UpdateProperties();
	}
	else
	{
		m_properties.clear();
		InvalidatePropertySignature();
	}
}

OvTools::Utils::OptRef<baregl::ShaderProgram> OvRendering::Data::Material::GetVariant(
	std::optional<const std::string_view> p_pass,
	OvTools::Utils::OptRef<const Data::FeatureSet> p_override
) const
{
	if (m_shader)
	{
		return m_shader->GetVariant(
			p_pass,
			p_override.value_or(m_features)
		);
	}
	
	return std::nullopt;
}

void OvRendering::Data::Material::UpdateProperties()
{
	InvalidatePropertySignature();

	// Collect all uniform names currently used by the shader
	std::unordered_set<std::string> usedUniforms;

	auto variants_view = m_shader->GetVariants()
		| std::views::values
		| std::views::join
		| std::views::values;

	for (const auto& variant : variants_view)
	{
		for (const auto& [name, uniformInfo] : variant->GetUniforms())
		{
			usedUniforms.insert(name);

			if (!m_properties.contains(name))
			{
				const auto defaultValue = UniformToPropertyValue(uniformInfo.defaultValue);
				m_properties.emplace(name, MaterialProperty{
					.value = defaultValue,
					.defaultValue = defaultValue,
					.singleUse = false
				});
			}
		}
	}

	std::erase_if(m_properties, [&usedUniforms](const auto& property) {
		return !usedUniforms.contains(property.first);
	});
}

OvRendering::Data::Material::BindingCommand OvRendering::Data::Material::PrepareBinding(
	std::optional<const std::string_view> p_pass,
	OvTools::Utils::OptRef<const Data::FeatureSet> p_featureSetOverride,
	baregl::Texture* p_emptyTexture2D,
	baregl::Texture* p_emptyTextureCube,
	std::optional<MaterialSignatureSet> p_previousMaterialSignature
)
{
	ZoneScoped;

	OVASSERT(IsValid(), "Attempting to bind an invalid material.");

	auto& program = m_shader->GetVariant(
		p_pass,
		p_featureSetOverride.value_or(m_features)
	);

	const auto signature = CalculateSignature(
		program,
		p_emptyTexture2D,
		p_emptyTextureCube
	);

	return { program, signature, !p_previousMaterialSignature || signature.bindSignature != p_previousMaterialSignature->bindSignature };
}

void OvRendering::Data::Material::ExecuteBinding(const BindingCommand& p_command)
{
	ZoneScopedN("Material Execute Binding");
	if (p_command.bindRequired) p_command.program.Bind();
}

OvRendering::Data::MaterialSignatureSet OvRendering::Data::Material::Bind(
	std::optional<const std::string_view> p_pass,
	OvTools::Utils::OptRef<const Data::FeatureSet> p_featureSetOverride,
	baregl::Texture* p_emptyTexture2D,
	baregl::Texture* p_emptyTextureCube,
	std::optional<MaterialSignatureSet> p_previousMaterialSignature
)
{
	ZoneScoped;
	const auto command = PrepareBinding(p_pass, p_featureSetOverride, p_emptyTexture2D, p_emptyTextureCube, p_previousMaterialSignature);
	ExecuteBinding(command);
	m_programInUse = command.program;
	return command.signature;
}

std::optional<OvRendering::Data::Material::UniformUploadCommand> OvRendering::Data::Material::PrepareUniformUpload(
	const baregl::ShaderProgram& p_program,
	const std::string& p_name,
	const MaterialPropertyType& p_value,
	baregl::Texture* p_emptyTexture2D,
	baregl::Texture* p_emptyTextureCube
)
{
	ZoneScopedN("Material Prepare Uniform");
	const auto info = p_program.GetUniformInfo(p_name);
	if (!info) return std::nullopt;
	const auto& uniform = info->get();
	using namespace OvMaths;
	using enum baregl::types::EUniformType;
	UniformUploadValue value;
	// Type checking, matrix transposition and asset lookup happen before GL.
	switch (uniform.type)
	{
	case BOOL: value = static_cast<int>(std::get<bool>(p_value)); break;
	case INT: value = std::get<int>(p_value); break;
	case FLOAT: value = std::get<float>(p_value); break;
	case FLOAT_VEC2:
	{
		const auto& v = std::get<FVector2>(p_value);
		value = baregl::math::Vec2{ v.x, v.y }; break;
	}
	case FLOAT_VEC3:
	{
		const auto& v = std::get<FVector3>(p_value);
		value = baregl::math::Vec3{ v.x, v.y, v.z }; break;
	}
	case FLOAT_VEC4:
	{
		const auto& v = std::get<FVector4>(p_value);
		value = baregl::math::Vec4{ v.x, v.y, v.z, v.w }; break;
	}
	case FLOAT_MAT3:
	{
		const auto t = FMatrix3::Transpose(std::get<FMatrix3>(p_value));
		baregl::math::Mat3 payload;
		static_assert(sizeof(payload) == sizeof(t.data));
		std::memcpy(&payload, t.data, sizeof(payload));
		value = payload; break;
	}
	case FLOAT_MAT4:
	{
		const auto t = FMatrix4::Transpose(std::get<FMatrix4>(p_value));
		baregl::math::Mat4 payload;
		static_assert(sizeof(payload) == sizeof(t.data));
		std::memcpy(&payload, t.data, sizeof(payload));
		value = payload; break;
	}
	case SAMPLER_2D:
	case SAMPLER_CUBE:
	{
		baregl::Texture* handle = nullptr;
		if (auto textureHandle = std::get_if<baregl::Texture*>(&p_value)) handle = *textureHandle;
		else if (auto texture = std::get_if<Resources::Texture*>(&p_value); texture && *texture)
			handle = &(*texture)->GetTexture();
		if (!handle) handle = uniform.type == SAMPLER_2D ? p_emptyTexture2D : p_emptyTextureCube;
		OVASSERT(uniform.textureIndex.has_value(), std::format("No texture index found for uniform: {}", p_name));
		value = handle;
		break;
	}
	default: break; // Preserve legacy no-upload behavior for unsupported uniform types.
	}
	return UniformUploadCommand{ uniform.name, uniform.type, std::move(value), uniform.textureIndex };
}

void OvRendering::Data::Material::ExecuteUniformUpload(baregl::ShaderProgram& p_program, const UniformUploadCommand& p_command)
{
	ZoneScopedN("Material Execute Uniform");
	using namespace OvMaths;
	using namespace baregl::math;
	using enum baregl::types::EUniformType;
	const auto& name = p_command.name;
	const auto& value = p_command.value;
	switch (p_command.type)
	{
	case BOOL:
	case INT: p_program.SetUniform<int>(name, std::get<int>(value)); break;
	case FLOAT: p_program.SetUniform<float>(name, std::get<float>(value)); break;
	case FLOAT_VEC2: p_program.SetUniform<Vec2>(name, std::get<Vec2>(value)); break;
	case FLOAT_VEC3: p_program.SetUniform<Vec3>(name, std::get<Vec3>(value)); break;
	case FLOAT_VEC4: p_program.SetUniform<Vec4>(name, std::get<Vec4>(value)); break;
	case FLOAT_MAT3: p_program.SetUniform<Mat3>(name, std::get<Mat3>(value)); break;
	case FLOAT_MAT4: p_program.SetUniform<Mat4>(name, std::get<Mat4>(value)); break;
	case SAMPLER_2D:
	case SAMPLER_CUBE:
		BindTexture(p_program, name, std::get<baregl::Texture*>(value), nullptr, p_command.textureIndex.value());
		break;
	default: break;
	}
}

void OvRendering::Data::Material::UploadProperties(
	bool uploadStableProperties,
	bool uploadSingleUseProperties,
	baregl::Texture* p_emptyTexture2D,
	baregl::Texture* p_emptyTextureCube
)
{
	ZoneScoped;
	OVASSERT(m_programInUse.has_value(), "Cannot upload properties before binding");
	auto& program = m_programInUse.value();
	// Keep traversal and per-property consumption order, including partial failure.
	// No draw-sized allocation or delayed reset of single-use values is introduced.
	for (auto& [name, prop] : m_properties)
	{
		if (!uploadStableProperties && !prop.singleUse) continue;
		if (!uploadSingleUseProperties && prop.singleUse) continue;
		const auto command = PrepareUniformUpload(program, name, prop.value, p_emptyTexture2D, p_emptyTextureCube);
		if (!command) continue;
		ExecuteUniformUpload(program, *command);
		if (prop.singleUse) prop.value = prop.defaultValue;
	}
}

void OvRendering::Data::Material::Unbind(bool p_resetBoundProgram)
{
	ZoneScoped;

	OVASSERT(IsValid(), "Attempting to unbind an invalid material.");
	OVASSERT(m_programInUse.has_value(), "Attempting to unbind a material that hasn't been bound");

	if (p_resetBoundProgram)
	{
		m_programInUse->Unbind();
	}

	m_programInUse.reset();
}

bool OvRendering::Data::Material::HasProperty(const std::string& p_name) const
{
	OVASSERT(IsValid(), "Attempting to call HasProperty on an invalid material.");
	return m_properties.contains(p_name);
}

void OvRendering::Data::Material::SetProperty(const std::string p_name, const MaterialPropertyType& p_value, bool p_singleUse)
{
	OVASSERT(IsValid(), "Attempting to SetProperty on an invalid material.");
	OVASSERT(HasProperty(p_name), "Attempting to SetProperty on a non-existing property.");

	m_properties[p_name].value = p_value;
	m_properties[p_name].singleUse = p_singleUse;

	if (p_singleUse)
	{
		++m_singleUsePropertySignatureVersion;
	}
	else
	{
		++m_stablePropertySignatureVersion;
	}
}

bool OvRendering::Data::Material::TrySetProperty(const std::string& p_name, const MaterialPropertyType& p_value, bool p_singleUse)
{
	if (HasProperty(p_name))
	{
		SetProperty(p_name, p_value, p_singleUse);
		return true;
	}

	return false;
}

OvTools::Utils::OptRef<const OvRendering::Data::MaterialProperty> OvRendering::Data::Material::GetProperty(const std::string p_key) const
{
	OVASSERT(IsValid(), "Attempting to GetProperty on an invalid material.");

	if (m_properties.find(p_key) != m_properties.end())
	{
		return m_properties.at(p_key);
	}

	return std::nullopt;
}

OvRendering::Resources::Shader*& OvRendering::Data::Material::GetShader()
{
	return m_shader;
}

bool OvRendering::Data::Material::HasShader() const
{
	return m_shader;
}

bool OvRendering::Data::Material::IsValid() const
{
	return HasShader();
}

void OvRendering::Data::Material::SetOrthographicSupport(bool p_supportOrthographic)
{
	m_supportOrthographic = p_supportOrthographic;
}

void OvRendering::Data::Material::SetPerspectiveSupport(bool p_supportPerspective)
{
	m_supportPerspective = p_supportPerspective;
}

void OvRendering::Data::Material::SetDrawOrder(int p_order)
{
	m_drawOrder = p_order;
}

void OvRendering::Data::Material::SetBlendable(bool p_transparent)
{
	m_blendable = p_transparent;
}

void OvRendering::Data::Material::SetUserInterface(bool p_userInterface)
{
	m_userInterface = p_userInterface;
}

void OvRendering::Data::Material::SetBackfaceCulling(bool p_backfaceCulling)
{
	m_backfaceCulling = p_backfaceCulling;
}

void OvRendering::Data::Material::SetFrontfaceCulling(bool p_frontfaceCulling)
{
	m_frontfaceCulling = p_frontfaceCulling;
}

void OvRendering::Data::Material::SetDepthTest(bool p_depthTest)
{
	m_depthTest = p_depthTest;
}

void OvRendering::Data::Material::SetDepthWriting(bool p_depthWriting)
{
	m_depthWriting = p_depthWriting;
}

void OvRendering::Data::Material::SetColorWriting(bool p_colorWriting)
{
	m_colorWriting = p_colorWriting;
}

void OvRendering::Data::Material::SetCastShadows(bool p_castShadows)
{
	m_castShadows = p_castShadows;
}

void OvRendering::Data::Material::SetReceiveShadows(bool p_receiveShadows)
{
	m_receiveShadows = p_receiveShadows;
}

void OvRendering::Data::Material::SetCapturedByReflectionProbes(bool p_capturedByReflectionProbes)
{
	m_capturedByReflectionProbes = p_capturedByReflectionProbes;
}

void OvRendering::Data::Material::SetReceiveReflections(bool p_receiveReflections)
{
	m_receiveReflections = p_receiveReflections;
}

void OvRendering::Data::Material::SetGPUInstances(int p_instances)
{
	m_gpuInstances = p_instances;
}

int OvRendering::Data::Material::GetDrawOrder() const
{
	return m_drawOrder;
}

bool OvRendering::Data::Material::IsBlendable() const
{
	return m_blendable;
}

bool OvRendering::Data::Material::IsUserInterface() const
{
	return m_userInterface;
}

bool OvRendering::Data::Material::HasBackfaceCulling() const
{
	return m_backfaceCulling;
}

bool OvRendering::Data::Material::HasFrontfaceCulling() const
{
	return m_frontfaceCulling;
}

bool OvRendering::Data::Material::HasDepthTest() const
{
	return m_depthTest;
}

bool OvRendering::Data::Material::HasDepthWriting() const
{
	return m_depthWriting;
}

bool OvRendering::Data::Material::HasColorWriting() const
{
	return m_colorWriting;
}

bool OvRendering::Data::Material::IsShadowCaster() const
{
	return m_castShadows;
}

bool OvRendering::Data::Material::IsShadowReceiver() const
{
	return m_receiveShadows;
}

bool OvRendering::Data::Material::IsCapturedByReflectionProbes() const
{
	return m_capturedByReflectionProbes;
}

bool OvRendering::Data::Material::IsReflectionReceiver() const
{
	return m_receiveReflections;
}

int OvRendering::Data::Material::GetGPUInstances() const
{
	return m_gpuInstances;
}

const OvRendering::Data::StateMask OvRendering::Data::Material::GenerateStateMask() const
{
	StateMask stateMask;
	stateMask.depthWriting = m_depthWriting;
	stateMask.colorWriting = m_colorWriting;
	stateMask.blendable = m_blendable;
	stateMask.depthTest = m_depthTest;
	stateMask.frontfaceCulling = m_frontfaceCulling;
	stateMask.backfaceCulling = m_backfaceCulling;
	return stateMask;
}

OvRendering::Data::Material::PropertyMap& OvRendering::Data::Material::GetProperties()
{
	return m_properties;
}

OvRendering::Data::FeatureSet& OvRendering::Data::Material::GetFeatures()
{
	return m_features;
}

void OvRendering::Data::Material::SetFeatures(const Data::FeatureSet& p_features)
{
	m_features = p_features;
}

void OvRendering::Data::Material::AddFeature(const std::string& p_feature)
{
	m_features.insert(p_feature);
}

void OvRendering::Data::Material::RemoveFeature(const std::string& p_feature)
{
	m_features.erase(p_feature);
}

bool OvRendering::Data::Material::HasFeature(const std::string& p_feature) const
{
	return m_features.contains(p_feature);
}

bool OvRendering::Data::Material::SupportsFeature(const std::string& p_feature) const
{
	return m_shader &&
		(m_shader->GetFeatures().contains(p_feature) ||
		 m_shader->GetEngineFeatures().contains(p_feature));
}

bool OvRendering::Data::Material::HasPass(const std::string& p_pass) const
{
	return m_shader->GetPasses().contains(p_pass);
}

bool OvRendering::Data::Material::SupportsOrthographic() const
{
	return m_supportOrthographic;
}

bool OvRendering::Data::Material::SupportsPerspective() const
{
	return m_supportPerspective;
}

bool OvRendering::Data::Material::SupportsProjectionMode(OvRendering::Settings::EProjectionMode p_projectionMode) const
{
	using enum OvRendering::Settings::EProjectionMode;

	switch (p_projectionMode)
	{
	case ORTHOGRAPHIC: return SupportsOrthographic();
	case PERSPECTIVE: return SupportsPerspective();
	}

	return true;
}

void OvRendering::Data::Material::InvalidatePropertySignature()
{
	++m_stablePropertySignatureVersion;
	++m_singleUsePropertySignatureVersion;
}

OvRendering::Data::MaterialSignatureSet OvRendering::Data::Material::CalculateSignature(
	baregl::ShaderProgram& p_selectedProgram,
	baregl::Texture* p_emptyTexture2D,
	baregl::Texture* p_emptyTextureCube
)
{
	ZoneScoped;

	MaterialSignatureSet signature{};

	// Bind
	HashCombine(signature.bindSignature, this);
	HashCombine(signature.bindSignature, &p_selectedProgram);

	// Stable Properties
	signature.stablePropertySignature = signature.bindSignature;
	HashCombine(signature.stablePropertySignature, m_stablePropertySignatureVersion);
	HashCombine(signature.stablePropertySignature, p_emptyTexture2D);
	HashCombine(signature.stablePropertySignature, p_emptyTextureCube);

	// Single Use Properties
	signature.singleUsePropertySignature = signature.bindSignature;
	HashCombine(signature.singleUsePropertySignature, m_singleUsePropertySignatureVersion);
	HashCombine(signature.singleUsePropertySignature, p_emptyTexture2D);
	HashCombine(signature.singleUsePropertySignature, p_emptyTextureCube);

	return signature;
}

OvRendering::Data::Material::Snapshot OvRendering::Data::Material::RecordSnapshot(
	std::optional<const std::string_view> p_pass,
	OvTools::Utils::OptRef<const Data::FeatureSet> p_featureSetOverride,
	const std::shared_ptr<baregl::Texture>& p_emptyTexture2D,
	const std::shared_ptr<baregl::Texture>& p_emptyTextureCube,
	std::span<const std::shared_ptr<baregl::Texture>> p_retainedTextures)
{
	ZoneScopedN("Material Record Snapshot");
	if (!IsValid()) throw std::invalid_argument("Cannot record an invalid material");
	Snapshot result;
	result.m_program = m_shader->RetainVariant(p_pass, p_featureSetOverride.value_or(m_features));
	result.m_shaderVersion = m_shader->GetVersion();
	result.m_uniforms.reserve(m_properties.size());
	std::vector<MaterialProperty*> consume;
	consume.reserve(m_properties.size());
	for (auto& [name, property] : m_properties)
	{
		const auto command = PrepareUniformUpload(*result.m_program, name, property.value,
			p_emptyTexture2D.get(), p_emptyTextureCube.get());
		if (!command) continue;
		if (std::holds_alternative<std::monostate>(command->value))
			throw std::invalid_argument("Material snapshot cannot capture an unsupported uniform type");
		if (auto texture = std::get_if<baregl::Texture*>(&command->value))
		{
			if (!*texture)
				throw std::invalid_argument("Material snapshot requires a texture or owned fallback for every sampler");
			std::shared_ptr<baregl::Texture> retained;
			if (auto asset = std::get_if<Resources::Texture*>(&property.value); asset && *asset)
				retained = (*asset)->RetainTexture();
			else if (*texture == p_emptyTexture2D.get()) retained = p_emptyTexture2D;
			else if (*texture == p_emptyTextureCube.get()) retained = p_emptyTextureCube;
			else
				for (const auto& candidate : p_retainedTextures)
					if (candidate.get() == *texture) { retained = candidate; break; }
			if (!retained || retained.use_count() == 0 || retained.get() != *texture)
				throw std::invalid_argument("Material snapshot requires ownership of every non-null texture");
			result.m_textures.push_back(std::move(retained));
		}
		result.m_uniforms.push_back(*command);
		if (property.singleUse) consume.push_back(&property);
	}
	if (result.m_uniforms.size() != result.m_program->GetUniforms().size())
		throw std::invalid_argument("Material snapshot is missing active shader properties; refresh the material after reload");
	// Current property alternatives are numeric values/matrices and pointers;
	// restoring defaults allocates nothing. No user callbacks run in this phase.
	for (auto* property : consume) property->value = property->defaultValue;
	return result;
}

void OvRendering::Data::Material::Snapshot::Execute() const
{
	ZoneScopedN("Material Replay Snapshot");
	if (!m_program) throw std::logic_error("Cannot replay a moved-from material snapshot");
	m_program->Bind();
	for (const auto& uniform : m_uniforms) Material::ExecuteUniformUpload(*m_program, uniform);
}
