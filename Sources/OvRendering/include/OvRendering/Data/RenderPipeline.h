#pragma once
#include <vector>
#include <OvRendering/Data/RenderStage.h>

namespace OvRendering::Data
{
    // Fixed ordered stage submissions for a single view. A4 adds resource uses.
    struct RenderPipeline
    {
        std::vector<RenderStage> stages;
        void Validate() const
        {
            std::vector<RenderStage> seen;
            for (auto stage : stages)
            {
                if (!DescribeStage(stage).staticSceneSupported)
                    throw std::invalid_argument("Stage has no static scene preparation/recording provider");
                for (auto previous : seen) if (previous == stage)
                    throw std::invalid_argument("Duplicate stage submission");
                seen.push_back(stage);
            }
        }
    };
}
