/**
* @project: baregl
* @author: Adrien Givry
* @licence: MIT
*/

#include <baregl/detail/NativeObject.h>

#include <baregl/debug/Assert.h>
#include <baregl/detail/glad/glad.h>
#include <baregl/detail/Types.h>
#include <baregl/Buffer.h>

namespace baregl::detail
{
	NativeObject::NativeObject(ResourceKind kind) :
		m_lifetime(kind), m_id{0}
	{
	}

	NativeID NativeObject::GetID() const
	{
		return m_lifetime.IsContextAlive() ? m_id : 0;
	}
	void NativeObject::AdoptNativeID() { m_lifetime.Adopt(m_id); }
}
