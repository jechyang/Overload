/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

#include <OvTools/Jobs/JobHandle.h>
#include <OvTools/Jobs/JobSystem.h>

namespace OvTools::Jobs
{
	template<typename F>
	JobHandle JobSystem::ParallelFor(
		uint64_t p_count,
		uint64_t p_grainSize,
		F&& p_range,
		std::span<const JobHandle> p_dependencies
	)
	{
		static_assert(
			std::is_invocable_v<F, uint64_t, uint64_t>,
			"ParallelFor callable must be invocable as p_range(uint64_t begin, uint64_t end)"
		);

		if (p_count == 0)
		{
			// No work: preserve dependency semantics with a no-op job.
			return Submit([] {}, p_dependencies);
		}

		// Share the callable across chunk jobs so it is captured once.
		auto callable = std::make_shared<std::decay_t<F>>(std::forward<F>(p_range));

		const uint64_t grainSize = std::max<uint64_t>(p_grainSize, 1);
		const uint64_t maxJobsByGrain = p_count / grainSize + (p_count % grainSize != 0 ? 1 : 0);
		const uint64_t targetParallelism = static_cast<uint64_t>(std::max(GetWorkerCount(), 1u)) * 4ull;
		uint64_t chunkCount = std::min(std::max<uint64_t>(maxJobsByGrain, 1ull), targetParallelism);
		chunkCount = std::min(chunkCount, p_count);

		if (chunkCount == 1)
		{
			const uint64_t count = p_count;
			return Submit([callable, count] { (*callable)(0, count); }, p_dependencies);
		}

		// Balanced contiguous split; the first (p_count % chunkCount) chunks get
		// one extra element. Chunk order matches element order for determinism.
		std::vector<JobHandle> chunks;
		chunks.reserve(static_cast<size_t>(chunkCount));

		const uint64_t baseSize = p_count / chunkCount;
		const uint64_t remainder = p_count % chunkCount;
		uint64_t begin = 0;
		try
		{
			for (uint64_t chunk = 0; chunk < chunkCount; ++chunk)
			{
				const uint64_t end = begin + baseSize + (chunk < remainder ? 1 : 0);
				chunks.push_back(Submit([callable, begin, end] { (*callable)(begin, end); }, p_dependencies));
				begin = end;
			}
		}
		catch (...)
		{
			// The caller may destroy captured state when submission throws.
			WaitAll(chunks);
			throw;
		}

		// Aggregate handle completing when every chunk has completed (chunks
		// already carry p_dependencies, so the aggregate covers them transitively).
		try
		{
			return Submit([] {}, std::span<const JobHandle>{ chunks });
		}
		catch (...)
		{
			WaitAll(chunks);
			throw;
		}
	}
}
