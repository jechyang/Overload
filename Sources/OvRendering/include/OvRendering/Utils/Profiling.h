/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <tracy/Tracy.hpp>

#include <baregl/detail/glad/glad.h>
#include <tracy/TracyOpenGL.hpp>

#include <OvRendering/Data/RenderCommandList.h>
#include <exception>
#include <optional>

namespace tracy { struct SourceLocationData; }
namespace OvRendering::Utils
{
    // The producer scope only groups owning commands. The actual GPU scope is
    // lexical on the consumer, including exception unwinding before GPU retirement.
    class GpuScope final
    {
    public:
        GpuScope(Data::RenderCommandList* commands, const tracy::SourceLocationData* source)
#ifdef TRACY_ENABLE
            : m_commands(commands), m_first(commands ? commands->GetPendingCount() : 0),
              m_exceptions(std::uncaught_exceptions()), m_source(source)
#endif
        {
#ifdef TRACY_ENABLE
            if (!commands) m_immediate.emplace(source, true);
#endif
        }
        GpuScope(const GpuScope&) = delete;
        GpuScope& operator=(const GpuScope&) = delete;
        ~GpuScope()
        {
#ifdef TRACY_ENABLE
            if (!m_commands || m_commands->GetState() != Data::RenderCommandList::State::RECORDING) return;
            if (std::uncaught_exceptions() > m_exceptions) { m_commands->FailRecording(); return; }
            try
            {
                m_commands->WrapOperations(m_first, [source = m_source](auto& driver, const auto& operations) {
                    tracy::GpuCtxScope scope(source, true);
                    for (const auto& operation : operations) operation(driver);
                });
            }
            catch (...) { m_commands->FailRecording(); }
#endif
        }
    private:
#ifdef TRACY_ENABLE
        Data::RenderCommandList* m_commands;
        size_t m_first;
        int m_exceptions;
        const tracy::SourceLocationData* m_source;
        std::optional<tracy::GpuCtxScope> m_immediate;
#endif
    };
}

#ifdef TRACY_ENABLE
#define OV_GPU_ZONE(renderer, name) \
    static constexpr tracy::SourceLocationData TracyConcat(__ov_gpu_source, TracyLine){ name, TracyFunction, TracyFile, (uint32_t)TracyLine, 0 }; \
    auto TracyConcat(__ov_gpu_scope, TracyLine) = (renderer).ProfileGpuScope(&TracyConcat(__ov_gpu_source, TracyLine))
#else
#define OV_GPU_ZONE(renderer, name)
#endif
