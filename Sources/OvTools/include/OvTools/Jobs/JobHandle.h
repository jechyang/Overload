/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <memory>
#include <utility>

#include <OvTools/Jobs/Detail/ControlBlock.h>

namespace OvTools::Jobs::Detail
{
	class Scheduler;
}

namespace OvTools::Jobs
{
	/**
	* An opaque reference to a submitted job, used to express dependencies
	* and to wait for completion
	*/
	class JobHandle
	{
	public:
		/**
		* Default-constructed handles are invalid; invalid handles behave as
		* already completed and are ignored in dependency lists
		*/
		JobHandle() = default;

		/**
		* Returns false for default-constructed handles
		*/
		bool IsValid() const
		{
			return m_block != nullptr;
		}

		/**
		* Returns true for invalid handles and for jobs that completed,
		* failed, or were dropped at system shutdown
		*/
		bool IsCompleted() const
		{
			return !m_block || m_block->IsTerminal();
		}

	private:
		friend class JobSystem;
		friend class Detail::Scheduler;

		explicit JobHandle(std::shared_ptr<Detail::ControlBlock> p_block)
			: m_block(std::move(p_block))
		{
		}

		std::shared_ptr<Detail::ControlBlock> m_block;
	};
}
