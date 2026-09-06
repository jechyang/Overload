#include <baregl/ResourceExecutor.h>
#include <stdexcept>

namespace baregl
{
    thread_local ResourceExecutor* ResourceExecutor::s_current = nullptr;
    ResourceExecutor::ResourceExecutor(std::function<void(std::function<void()>)> dispatch) :
        m_previous(s_current), m_dispatch(std::move(dispatch))
    {
        if (!m_dispatch) throw std::invalid_argument("Resource executor requires a synchronous dispatcher");
        s_current = this;
    }
    ResourceExecutor::~ResourceExecutor()
    {
        if (m_thread != std::this_thread::get_id() || s_current != this) std::terminate();
        s_current = m_previous;
    }
}
