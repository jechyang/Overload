/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <any>
#include <typeindex>
#include <unordered_map>

namespace OvRendering::FrameGraph
{
	/**
	* A per-frame typed key-value store for sharing data between FrameGraph passes.
	*/
	class FrameGraphBlackboard
	{
	public:
		template<typename T>
		void Put(T&& value)
		{
			m_data[typeid(T)] = std::forward<T>(value);
		}

		template<typename T>
		T& Get()
		{
			return std::any_cast<T&>(m_data.at(typeid(T)));
		}

		template<typename T>
		const T& Get() const
		{
			return std::any_cast<const T&>(m_data.at(typeid(T)));
		}

		template<typename T>
		bool Has() const
		{
			return m_data.contains(typeid(T));
		}

		void Clear()
		{
			m_data.clear();
		}

	private:
		std::unordered_map<std::type_index, std::any> m_data;
	};
}
