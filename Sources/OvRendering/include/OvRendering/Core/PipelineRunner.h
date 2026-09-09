#pragma once
#include <OvRendering/Data/PreparedFrameData.h>
namespace OvRendering::Core
{
    // A pass only selects its declared View/Stage slice and calls the same submit
    // operation. It cannot query scene descriptors, features or the old renderer.
    class PipelineRunner final
    {
    public:
        template<class SubmitStage>
        static void Run(const Data::PreparedFrameData& frame, SubmitStage&& submit)
        {
            for (const auto& list : frame.GetLists())
            {
                if (list.viewId != frame.GetView().input.id || !Data::DescribeStage(list.stage).staticSceneSupported)
                    throw std::invalid_argument("Invalid View/Stage submission");
                submit(list.stage, frame.GetDraws().subspan(list.firstDraw, list.drawCount));
            }
        }
    };
}
