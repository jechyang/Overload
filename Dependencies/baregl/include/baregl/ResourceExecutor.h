#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace baregl
{
    // Bind on the resource-producing thread. The dispatcher must execute and
    // finish work synchronously (including exception propagation) on the GL
    // owner. Calls without a binding retain the legacy/current-owner path.
    // This routes operations; it does not permit concurrent CPU mutation or
    // sharing borrowed metadata while a resource is being changed.
    // Bind/draw/unbind sequences that depend on GL state must be submitted as
    // one Invoke operation; separate calls may have frame work between them.
    class ResourceExecutor final
    {
        static thread_local ResourceExecutor* s_current;
        ResourceExecutor* m_previous;
        std::shared_ptr<const void> m_identity=std::make_shared<char>();
        std::thread::id m_thread = std::this_thread::get_id();
        std::function<void(std::function<void()>)> m_dispatch;
    public:
        explicit ResourceExecutor(std::function<void(std::function<void()>)> dispatch);
        ~ResourceExecutor();
        ResourceExecutor(const ResourceExecutor&) = delete;
        ResourceExecutor& operator=(const ResourceExecutor&) = delete;
        // CPU identity of this producer binding, retained by caches across scopes.
        static std::shared_ptr<const void> CurrentBinding() { return s_current?s_current->m_identity:nullptr; }
        template<class F> static auto Invoke(F&& function)
        {
            using Result = std::invoke_result_t<F>;
            static_assert(!std::is_reference_v<Result>, "Resource work must return an owning value");
            if (!s_current) return std::invoke(std::forward<F>(function));
            if constexpr (std::is_void_v<Result>)
                s_current->m_dispatch([&] { std::invoke(std::forward<F>(function)); });
            else
            {
                std::optional<Result> result;
                s_current->m_dispatch([&] { result.emplace(std::invoke(std::forward<F>(function))); });
                return Result(std::move(result.value()));
            }
        }
    };
}
