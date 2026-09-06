/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <cstdint>

namespace OvTools::Jobs
{
	/**
	* Defines which threads are allowed to execute a job
	*/
	enum class EAffinity : uint8_t
	{
		/**
		* The job can be executed by any worker thread, or by the main
		* thread while it participates inside JobSystem::Wait
		*/
		Any,

		/**
		* The job is only executed by the thread that constructed the
		* JobSystem, through JobSystem::PumpMainThread or Wait. Worker
		* threads never pick up such jobs. Enforced structurally by the
		* scheduler; misuse (a worker waiting on such a job) is caught by
		* a debug-only assertion. Reserved for the future render thread
		* topology
		*/
		MainThread
	};
}
