#include <OvCore/Rendering/ToolDrawCollector.h>
#include <OvRendering/Core/MeshFeatureProcessor.h>
#include <OvRendering/Context/Driver.h>
#include <OvRendering/Resources/Mesh.h>
using namespace OvCore::Rendering;
using namespace OvRendering;
ToolDrawCollector::ToolDrawCollector()
{
    m_engineBuffer=std::make_shared<baregl::Buffer>(); m_engineBuffer->Allocate(sizeof(Data::EngineConstants),baregl::types::EAccessSpecifier::STREAM_DRAW);
    m_lightBuffer=std::make_shared<baregl::Buffer>();
    m_reflectionBuffer=std::make_shared<baregl::Buffer>(); m_reflectionBuffer->Allocate(sizeof(Data::ReflectionConstants),baregl::types::EAccessSpecifier::STREAM_DRAW);
    m_skinBuffer=std::make_shared<baregl::Buffer>();
}
void ToolDrawCollector::Begin(Data::RenderView view,Data::PipelineState state,std::optional<baregl::Framebuffer::Snapshot> target,Data::RenderPlan* plan)
{
    m_plan=plan;m_passName="Editor.ToolDraw";
    m_commands=std::make_unique<Data::RenderCommandList>(); m_view=std::move(view); m_state=state; m_target=std::move(target);
    m_info={}; m_signature.reset(); m_previous.reset(); m_reflection={}; m_lights.clear();
}
Data::RenderCommandList ToolDrawCollector::Finish()
{
    m_commands->Append([](Context::Driver&) { baregl::Framebuffer::BindDefault(); });
    auto commands=std::move(*m_commands); m_commands.reset(); return commands;
}
void ToolDrawCollector::Clear(bool color,bool depth,bool stencil,const OvMaths::FVector4& value)
{
    AppendPass(m_passName+".Clear",[target=m_target,view=m_view.input,color,depth,stencil,value] {return [target,view,color,depth,stencil,value](Context::Driver& driver) {
        if(target)target->Bind();else baregl::Framebuffer::BindDefault();
        driver.SetViewport(0,0,view.width,view.height); driver.Clear(color,depth,stencil,value);
    };},TargetAccesses(color,depth,stencil,true));
}
void ToolDrawCollector::CollectModel(Data::PipelineState state,Resources::Model& model,Data::Material& material,const OvMaths::FMatrix4& transform)
{ CollectModelWithTextures(state,model,material,transform,{}); }
void ToolDrawCollector::CollectModelWithTextures(Data::PipelineState state,Resources::Model& model,Data::Material& material,
    const OvMaths::FMatrix4& transform,std::vector<std::shared_ptr<baregl::Texture>> textures)
{
    for(auto* mesh:model.GetMeshes())
    {
        ToolDrawInput draw;draw.mesh=*mesh;draw.material=material;draw.stateMask=material.GenerateStateMask();
        draw.model=transform;CollectMesh(state,draw,{},textures);
    }
}
void ToolDrawCollector::CollectMesh(Data::PipelineState state,const ToolDrawInput& draw,
    const Data::Material::PropertyOverrides& overrides,std::span<const std::shared_ptr<baregl::Texture>> textures)
{
    if(!draw.mesh||!draw.material||!draw.material->IsValid()||draw.material->GetGPUInstances()<=0||
        !draw.material->SupportsProjectionMode(m_view.input.projectionMode))return;
    auto* mesh=dynamic_cast<Resources::Mesh*>(&draw.mesh.value());
    if(!mesh)throw std::invalid_argument("Tool geometry requires an owned mesh generation");
    const auto features=draw.featureSetOverride?OvTools::Utils::OptRef<const Data::FeatureSet>(*draw.featureSetOverride):std::nullopt;
    const auto binding=draw.material->PrepareBinding(draw.pass,features,m_resources.GetFallback2D().get(),m_resources.GetFallbackCube().get(),m_signature);
    const bool stable=!m_signature||m_signature->stablePropertySignature!=binding.signature.stablePropertySignature;
    const bool once=!overrides.empty()||!m_signature||m_signature->singleUsePropertySignature!=binding.signature.singleUsePropertySignature;
    std::vector<std::shared_ptr<baregl::Texture>> owners(textures.begin(),textures.end());
    if(m_reflection.map)owners.push_back(m_reflection.map);
    auto properties=overrides;
    if(draw.material->HasProperty("_EnvironmentMap"))properties["_EnvironmentMap"]=m_reflection.map?m_reflection.map.get():m_resources.GetFallbackCube().get();
    auto material=draw.material->PrepareSnapshot(draw.pass,features,m_resources.GetFallback2D(),m_resources.GetFallbackCube(),owners,m_previous,stable,once,true,properties);
    m_previous=material;m_signature=binding.signature;
    Data::EngineConstants engine{OvMaths::FMatrix4::Identity,OvMaths::FMatrix4::Transpose(m_view.view),OvMaths::FMatrix4::Transpose(m_view.projection),m_view.input.position,m_view.input.elapsedTime,OvMaths::FMatrix4::Identity};
    engine.model=OvMaths::FMatrix4::Transpose(draw.model);engine.user=draw.user;
    const bool skin=(features&&features->contains("SKINNING"))||draw.material->HasFeature("SKINNING");
    auto palette=skin?draw.palette:std::vector<OvMaths::FMatrix4>{};
    if(skin&&palette.empty())palette.push_back(OvMaths::FMatrix4::Transpose(OvMaths::FMatrix4::Identity));
    m_info.Add(mesh->GetVertexCount(),mesh->GetIndexCount(),draw.material->GetGPUInstances());
    state=Core::MeshFeatureProcessor::BuildState(state,draw.stateMask,Data::RenderStage::Picking);
    auto accesses=TargetAccesses(state.colorWriting.mask!=0,state.depthTest||state.depthWriting,state.stencilTest);
    if(m_plan)for(const auto& storage:Data::Material::GetPreparedTextureReads(material)) {
        auto handle=m_plan->Find(storage);const auto desc=Data::PlanResourceDesc::From(storage);
        if(!handle)handle=m_plan->Import("Tool.Sampler",storage,Data::ResourceOrigin::Imported,{{desc.aspect,0,desc.mips,0,desc.layers}});
        if(std::none_of(accesses.begin(),accesses.end(),[&](const auto& access){return access.resource==*handle&&access.type==Data::ResourceAccessType::Sample;}))
            accesses.push_back({*handle,{desc.aspect,0,desc.mips,0,desc.layers},Data::ResourceAccessType::Sample});
    }
    AppendPass(m_passName,[material,geometry=mesh->RetainGeometry(),state,engine,palette=std::move(palette),lights=m_lights,
        reflection=m_reflection.constants,target=m_target,view=m_view.input,primitive=draw.primitiveMode,instances=draw.material->GetGPUInstances(),
        engineBuffer=m_engineBuffer,lightBuffer=m_lightBuffer,reflectionBuffer=m_reflectionBuffer,skinBuffer=m_skinBuffer] {
        auto payload=Data::Material::EncodeSnapshot(material);
        return [=](Context::Driver& driver){
            if(target)target->Bind();else baregl::Framebuffer::BindDefault();driver.SetViewport(0,0,view.width,view.height);
            engineBuffer->Upload(&engine);engineBuffer->Bind(baregl::types::EBufferType::UNIFORM,0);
            if(lightBuffer->Allocate(lights.size()*sizeof(OvMaths::FMatrix4),baregl::types::EAccessSpecifier::STREAM_DRAW))lightBuffer->Upload(lights.data());
            lightBuffer->Bind(baregl::types::EBufferType::SHADER_STORAGE,0);
            reflectionBuffer->Upload(&reflection);reflectionBuffer->Bind(baregl::types::EBufferType::UNIFORM,1);
            if(!palette.empty()){if(skinBuffer->Allocate(palette.size()*sizeof(OvMaths::FMatrix4),baregl::types::EAccessSpecifier::STREAM_DRAW))skinBuffer->Upload(palette.data());skinBuffer->Bind(baregl::types::EBufferType::SHADER_STORAGE,1);}
            payload->Execute();driver.Draw(state,*geometry,primitive,instances);
        };
    },std::move(accesses));
}

ToolDrawCollector::GpuScope::GpuScope(Data::RenderPlan* plan,Data::RenderCommandList* commands,const tracy::SourceLocationData* source)
{
    if(plan)m_planScope.emplace(*plan,*commands,source);else m_commandScope.emplace(commands,source);
}
void ToolDrawCollector::AppendPass(std::string name,Data::RenderCommandList::OperationFactory operation,std::vector<Data::ResourceAccess> accesses)
{
    if(m_plan) {
        Data::RenderPlanPass pass;pass.name=std::move(name);pass.view=Data::RenderPlanPass::NoView;pass.target=m_target;
        pass.operation=std::move(operation);pass.accesses=std::move(accesses);m_plan->passes.push_back(std::move(pass));
    } else m_commands->AppendDeferred(std::move(operation));
}
std::vector<Data::ResourceAccess> ToolDrawCollector::TargetAccesses(bool color,bool depth,bool stencil,bool clear)
{
    std::vector<Data::ResourceAccess> result;if(!m_plan||!m_target)return result;
    for(const auto& attachment:m_target->GetTextureAttachments())if(attachment.writable) {
        const auto desc=Data::PlanResourceDesc::From(attachment.storage);
        if((desc.aspect==Data::ResourceAspect::Color&&!color)||(desc.aspect==Data::ResourceAspect::Depth&&!depth))continue;
        auto handle=m_plan->Find(attachment.storage);if(!handle)handle=m_plan->Import("Tool.Target",attachment.storage,Data::ResourceOrigin::CurrentFrame);
        result.push_back({*handle,{desc.aspect,0,1,attachment.layer?uint16_t(*attachment.layer):uint16_t(0),1},Data::ResourceAccessType::Attachment,
            clear?Data::AttachmentLoad::Clear:Data::AttachmentLoad::Load});
    }
    for(const auto& attachment:m_target->GetRenderbufferAttachments()) {
        using enum baregl::types::EFramebufferAttachment;
        const bool isDepth=attachment.attachment==DEPTH;const bool isStencil=attachment.attachment==STENCIL;
        if((isDepth&&!depth)||(isStencil&&!stencil))continue;
        auto aspect=isDepth?Data::ResourceAspect::Depth:isStencil?Data::ResourceAspect::Stencil:Data::ResourceAspect::DepthStencil;
        if(!isDepth&&!isStencil) {
            if(!depth&&!stencil)continue;aspect=depth&&stencil?Data::ResourceAspect::DepthStencil:depth?Data::ResourceAspect::Depth:Data::ResourceAspect::Stencil;
        }
        const auto handle=m_plan->Import("Tool.DepthStencil",attachment.storage,Data::ResourceOrigin::CurrentFrame);
        result.push_back({handle,{aspect},Data::ResourceAccessType::Attachment,clear?Data::AttachmentLoad::Clear:Data::AttachmentLoad::Load});
    }
    return result;
}
void ToolDrawCollector::SubmitRenderOperation(Data::RenderCommandList::Operation operation)
{
    if(!m_plan){m_commands->Append(std::move(operation));return;}
    auto accesses=TargetAccesses(true,false,false);
    for(auto& access:accesses)access.type=Data::ResourceAccessType::Readback;
    AppendPass("Picking.Readback",[operation=std::move(operation)] {return operation;},std::move(accesses));
}
