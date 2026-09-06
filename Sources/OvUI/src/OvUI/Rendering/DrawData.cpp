#include <OvUI/Rendering/DrawData.h>
#include <OvUI/Internal/TextureID.h>
#include <imgui_impl_opengl3.h>
#include <baregl/detail/glad/glad.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace OvUI::Internal
{
    thread_local std::vector<TextureID>* TextureCaptureScope::s_current = nullptr;
    TextureCaptureScope::TextureCaptureScope(std::vector<TextureID>& textures) : m_previous(s_current) { s_current = &textures; }
    TextureCaptureScope::~TextureCaptureScope() { s_current = m_previous; }
    void TextureID::RetainForCapture() const
    {
        if (TextureCaptureScope::s_current && IsRetained())
            TextureCaptureScope::s_current->push_back(*this);
    }
}

namespace OvUI::Rendering
{
    namespace
    {
        struct UnpackState
        {
            GLint buffer, alignment, rowLength, skipRows, skipPixels;
            UnpackState()
            {
                glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer); glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
                glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength); glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows); glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            }
            ~UnpackState()
            {
                glPixelStorei(GL_UNPACK_ALIGNMENT, alignment); glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
                glPixelStorei(GL_UNPACK_SKIP_ROWS, skipRows); glPixelStorei(GL_UNPACK_SKIP_PIXELS, skipPixels);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
            }
        };
        struct FontGeneration
        {
            uint32_t width, height;
            ImTextureID logicalID;
            std::vector<uint8_t> pixels;
            std::shared_ptr<baregl::Texture> texture; // Only the GL consumer accesses this field.
            uint32_t GetID()
            {
                if (!texture)
                {
                    UnpackState unpack;
                    auto value = std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_2D);
                    value->Allocate({.width=width, .height=height,
                        .minFilter=baregl::types::ETextureFilteringMode::LINEAR, .magFilter=baregl::types::ETextureFilteringMode::LINEAR,
                        .horizontalWrap=baregl::types::ETextureWrapMode::CLAMP_TO_EDGE, .verticalWrap=baregl::types::ETextureWrapMode::CLAMP_TO_EDGE,
                        .internalFormat=baregl::types::EInternalFormat::RGBA8, .useMipMaps=false});
                    value->Upload(pixels.data(), baregl::types::EFormat::RGBA, baregl::types::EPixelDataType::UNSIGNED_BYTE);
                    texture = std::move(value);
                }
                return texture->GetID();
            }
        };
        struct Binding { Internal::TextureID texture; std::shared_ptr<FontGeneration> font; };
        template<class T> void PointTo(ImVector<T>& view, std::vector<T>& owned)
        { view.Data = owned.data(); view.Size = view.Capacity = static_cast<int>(owned.size()); }
        template<class T> void Detach(ImVector<T>& view) { view.Data = nullptr; view.Size = view.Capacity = 0; }
        struct OwnedList
        {
            std::vector<ImDrawVert> vertices;
            std::vector<ImDrawIdx> indices;
            std::vector<ImDrawCmd> commands;
            std::vector<Binding> bindings;
            ImDrawList view{nullptr}; // No pointer to ImGui's shared draw-list state.
            ~OwnedList() { Detach(view.VtxBuffer); Detach(view.IdxBuffer); Detach(view.CmdBuffer); }
        };
        struct OwnedDataView
        {
            ImDrawData view;
            ~OwnedDataView() { Detach(view.CmdLists); }
        };
    }

    struct DrawData::Storage
    {
        std::vector<std::unique_ptr<OwnedList>> lists;
        std::vector<ImDrawList*> views;
        std::vector<std::shared_ptr<FontGeneration>> retiredFonts;
        std::shared_ptr<ImGui_ImplOpenGL3_RenderState> backend;
        ImVec2 position, size, scale;
        int vertexCount=0, indexCount=0;
        bool consumed=false;
    };
    struct DrawDataRecorder::State
    {
        struct FontEntry { int uniqueID; std::shared_ptr<FontGeneration> generation; };
        std::unordered_map<const ImTextureData*, FontEntry> fonts; // Producer only; never dereferenced by Execute.
        ImTextureID nextID = (ImTextureID(1) << 63);
    };
    DrawDataRecorder::DrawDataRecorder() : m_state(std::make_unique<State>()) { static_assert(sizeof(ImTextureID) >= 8); }
    DrawDataRecorder::~DrawDataRecorder() = default;
    void DrawDataRecorder::ResetFontIdentifiers()
    {
        for (auto* texture : ImGui::GetPlatformIO().Textures)
            if (const auto found = m_state->fonts.find(texture); found != m_state->fonts.end() &&
                found->second.uniqueID == texture->UniqueID && texture->TexID == found->second.generation->logicalID)
            { texture->SetTexID(ImTextureID_Invalid); texture->SetStatus(ImTextureStatus_Destroyed); }
        m_state->fonts.clear();
    }

    DrawData DrawDataRecorder::Capture(const ImDrawData& data, std::span<const std::shared_ptr<baregl::Texture>> textures)
    {
        std::vector<Internal::TextureID> bindings(textures.begin(), textures.end());
        return CaptureBindings(data, bindings);
    }

    DrawData DrawDataRecorder::CaptureBindings(const ImDrawData& data, std::span<const Internal::TextureID> textures)
    {
        if (!data.Valid) throw std::invalid_argument("ImGui draw data is not finalized");
        auto finite=[](float value){return std::isfinite(value);};
        if (!finite(data.DisplayPos.x) || !finite(data.DisplayPos.y) || !finite(data.DisplaySize.x) || !finite(data.DisplaySize.y) ||
            !finite(data.FramebufferScale.x) || !finite(data.FramebufferScale.y) || data.DisplaySize.x < 0 || data.DisplaySize.y < 0 ||
            data.FramebufferScale.x <= 0 || data.FramebufferScale.y <= 0 ||
            double(data.DisplaySize.x)*data.FramebufferScale.x > std::numeric_limits<int>::max() ||
            double(data.DisplaySize.y)*data.FramebufferScale.y > std::numeric_limits<int>::max())
            throw std::invalid_argument("Invalid ImGui framebuffer dimensions");
        auto result = std::make_shared<DrawData::Storage>();
        result->backend=ImGui_ImplOpenGL3_CaptureRenderState();
        result->position=data.DisplayPos; result->size=data.DisplaySize; result->scale=data.FramebufferScale;
        std::unordered_map<ImTextureID,Internal::TextureID> owners;
        for (const auto& texture : textures) if (texture.IsRetained()) owners.emplace(texture.id,texture);
        auto staged=m_state->fonts;
        std::unordered_map<const ImTextureData*,std::shared_ptr<FontGeneration>> resolved;
        std::vector<ImTextureData*> acknowledge, retire;
        auto stageFont=[&](ImTextureData* texture) -> std::shared_ptr<FontGeneration>
        {
            if (auto found=resolved.find(texture);found!=resolved.end()) return found->second;
            if (!texture || texture->Status == ImTextureStatus_WantDestroy || texture->Status == ImTextureStatus_Destroyed || !texture->Pixels || texture->Width<=0 || texture->Height<=0 || texture->BackendUserData)
                throw std::invalid_argument("Unsupported ImGui managed texture");
            auto found=staged.find(texture);
            if (found!=staged.end() && found->second.uniqueID==texture->UniqueID &&
                texture->Status==ImTextureStatus_OK && texture->TexID==found->second.generation->logicalID)
                return resolved.emplace(texture,found->second.generation).first->second;
            if (texture->TexID!=ImTextureID_Invalid && (found==staged.end() || texture->TexID!=found->second.generation->logicalID))
                throw std::invalid_argument("Switch the ImGui renderer before capturing managed textures");
            const auto pixels=size_t(texture->Width)*texture->Height;
            if (pixels>std::numeric_limits<size_t>::max()/4) throw std::length_error("ImGui texture is too large");
            auto generation=std::make_shared<FontGeneration>();
            generation->width=texture->Width;generation->height=texture->Height;generation->logicalID=++m_state->nextID;
            generation->pixels.resize(pixels*4);
            if (texture->Format==ImTextureFormat_RGBA32 && texture->BytesPerPixel==4)
                std::copy_n(texture->Pixels,pixels*4,generation->pixels.begin());
            else if (texture->Format==ImTextureFormat_Alpha8 && texture->BytesPerPixel==1)
                for(size_t i=0;i<pixels;++i){generation->pixels[i*4]=generation->pixels[i*4+1]=generation->pixels[i*4+2]=255;generation->pixels[i*4+3]=texture->Pixels[i];}
            else throw std::invalid_argument("Unsupported ImGui texture format");
            staged[texture]={texture->UniqueID,generation};resolved.emplace(texture,generation);acknowledge.push_back(texture);return generation;
        };
        if(data.Textures) for(auto* texture:*data.Textures)
        {
            if(texture->Status==ImTextureStatus_WantDestroy && texture->UnusedFrames>0)
            {staged.erase(texture);retire.push_back(texture);}
            else if(texture->Status!=ImTextureStatus_Destroyed && texture->Status!=ImTextureStatus_WantDestroy) stageFont(texture);
        }
        for(const auto* source:data.CmdLists)
        {
            auto list=std::make_unique<OwnedList>();
            list->vertices.assign(source->VtxBuffer.begin(),source->VtxBuffer.end());
            list->indices.assign(source->IdxBuffer.begin(),source->IdxBuffer.end());
            list->commands.assign(source->CmdBuffer.begin(),source->CmdBuffer.end());
            list->bindings.reserve(list->commands.size());
            for(auto& command:list->commands)
            {
                if(command.UserCallback && command.UserCallback!=ImDrawCallback_ResetRenderState)
                    throw std::invalid_argument("ImGui callback has no owned command representation");
                Binding binding;
                if(!command.UserCallback)
                {
                    if (!finite(command.ClipRect.x) || !finite(command.ClipRect.y) || !finite(command.ClipRect.z) || !finite(command.ClipRect.w))
                        throw std::invalid_argument("Invalid ImGui clip rectangle");
                    if(command.IdxOffset>list->indices.size() || command.ElemCount>list->indices.size()-command.IdxOffset ||
                        command.ElemCount>unsigned(std::numeric_limits<int>::max())) throw std::invalid_argument("Invalid ImGui draw index range");
                    for(size_t i=command.IdxOffset;i<size_t(command.IdxOffset)+command.ElemCount;++i)
                        if(size_t(list->indices[i])+command.VtxOffset>=list->vertices.size()) throw std::invalid_argument("Invalid ImGui vertex offset");
                    if(command.TexRef._TexData) binding.font=stageFont(command.TexRef._TexData);
                    else if(command.GetTexID()!=ImTextureID_Invalid)
                    {
                        if(const auto owner=owners.find(command.GetTexID());owner!=owners.end()) binding.texture=owner->second;
                        else
                        {
                            for(const auto& entry:staged) if(entry.second.generation->logicalID==command.GetTexID()) binding.font=entry.second.generation;
                            if(!binding.font) throw std::invalid_argument("ImGui texture has no owning generation");
                        }
                    }
                }
                command.TexRef=ImTextureRef(ImTextureID_Invalid);command.UserCallbackData=nullptr;
                command.UserCallbackDataSize=0;command.UserCallbackDataOffset=0;
                list->bindings.push_back(std::move(binding));
            }
            if(list->vertices.size()>size_t(std::numeric_limits<int>::max()-result->vertexCount) ||
                list->indices.size()>size_t(std::numeric_limits<int>::max()-result->indexCount)) throw std::length_error("ImGui geometry is too large");
            result->vertexCount+=static_cast<int>(list->vertices.size());result->indexCount+=static_cast<int>(list->indices.size());
            PointTo(list->view.VtxBuffer,list->vertices);PointTo(list->view.IdxBuffer,list->indices);PointTo(list->view.CmdBuffer,list->commands);
            result->views.push_back(&list->view);result->lists.push_back(std::move(list));
        }
        // Move obsolete GPU generations into the frame: capture never performs their final GL release.
        for (const auto& [key, entry] : m_state->fonts)
            if (const auto next = staged.find(key); next == staged.end() || next->second.generation != entry.generation)
                result->retiredFonts.push_back(entry.generation);
        // Only publish producer-side acknowledgements after the complete capture succeeds.
        m_state->fonts.swap(staged);
        for(auto* texture:acknowledge){texture->SetTexID(m_state->fonts.at(texture).generation->logicalID);texture->SetStatus(ImTextureStatus_OK);}
        for(auto* texture:retire){texture->SetTexID(ImTextureID_Invalid);texture->SetStatus(ImTextureStatus_Destroyed);}
        return DrawData(std::move(result));
    }

    void DrawData::Execute() const
    {
        if(!m_storage) return;
        if(m_storage->consumed) throw std::logic_error("ImGui draw data was already consumed");
        m_storage->consumed=true;
        for(auto& list:m_storage->lists) for(size_t i=0;i<list->commands.size();++i)
        {
            auto& binding=list->bindings[i];
            const uint32_t id=binding.font?binding.font->GetID():binding.texture.id;
            list->commands[i].TexRef=ImTextureRef(ImTextureID(id));
        }
        OwnedDataView data;data.view.Valid=true;data.view.DisplayPos=m_storage->position;data.view.DisplaySize=m_storage->size;
        data.view.FramebufferScale=m_storage->scale;data.view.TotalVtxCount=m_storage->vertexCount;data.view.TotalIdxCount=m_storage->indexCount;
        data.view.CmdListsCount=static_cast<int>(m_storage->views.size());PointTo(data.view.CmdLists,m_storage->views);
        data.view.Textures=nullptr;data.view.OwnerViewport=nullptr;
        ImGui_ImplOpenGL3_RenderDrawDataRetained(&data.view,m_storage->backend);
    }
    size_t DrawData::GetVertexCount() const {return m_storage?m_storage->vertexCount:0;}
    size_t DrawData::GetIndexCount() const {return m_storage?m_storage->indexCount:0;}
}
