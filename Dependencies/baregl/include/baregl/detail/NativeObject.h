/**
* @project: baregl
* @author: Adrien Givry
* @licence: MIT
*/

#pragma once

#include <cstdint>
#include <baregl/ResourceLifetime.h>

namespace baregl::detail
{
	using NativeID = uint32_t;
	
	/**
	* Wrapper for any OpenGL object
	*/
	class NativeObject
	{
	public:
		/**
		* Creates an instance of native object for the OpenGL object identified by the given native ID
		* @param p_id
		*/
		NativeObject(ResourceKind kind = ResourceKind::NONE);

		/**
		* Destroys the native object (no-op)
		*/
		virtual ~NativeObject() = default;

		/**
		* Deleted copy constructor
		*/
		NativeObject(const NativeObject&) = delete;

		/**
		* Deleted assignment constructor
		*/
		NativeObject& operator=(const NativeObject&) = delete;

		/**
		* Deleted move constructor
		*/
		NativeObject(NativeObject&&) = delete;

		/**
		* Deleted move assignment constructor
		*/
		NativeObject& operator=(NativeObject&&) = delete;

		/**
		* Returns the ID of the underlying object
		*/
		NativeID GetID() const;

	protected:
		void AdoptNativeID();
		ResourceLifetime m_lifetime;
		NativeID m_id;
	};
}
