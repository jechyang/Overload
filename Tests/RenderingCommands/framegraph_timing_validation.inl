    struct TimingHooks
    {
        inline static PFNGLGENQUERIESPROC generate;
        inline static PFNGLDELETEQUERIESPROC erase;
        inline static PFNGLQUERYCOUNTERPROC counter;
        inline static PFNGLGETQUERYOBJECTIVPROC available;
        inline static PFNGLGETQUERYOBJECTUI64VPROC read;
        inline static PFNGLFINISHPROC finish;
        inline static std::thread::id owner;
        inline static size_t allocations=0,deletions=0,counters=0,reads=0,finishes=0;
        inline static uint64_t clock=0;
        inline static bool unavailable=false,failRead=false;
        inline static std::map<GLuint,uint64_t> stamps;
        inline static std::set<GLuint> ready;
        static void Check(){Require(std::this_thread::get_id()==owner&&glfwGetCurrentContext(),"timing GL call escaped its owner");}
        static void APIENTRY Generate(GLsizei n,GLuint* ids){Check();generate(n,ids);allocations+=n;}
        static void APIENTRY Erase(GLsizei n,const GLuint* ids){Check();deletions+=n;erase(n,ids);}
        static void APIENTRY Counter(GLuint id,GLenum target){Check();++counters;stamps[id]=clock++;ready.erase(id);counter(id,target);}
        static void APIENTRY Available(GLuint id,GLenum field,GLint* value){Check();Require(field==GL_QUERY_RESULT_AVAILABLE,"timing used a blocking availability query");available(id,field,value);if(unavailable)*value=0;if(*value)ready.insert(id);}
        static void APIENTRY Read(GLuint id,GLenum field,GLuint64* value){Check();Require(field==GL_QUERY_RESULT&&ready.contains(id),"timing result read before query availability");++reads;if(failRead)throw std::runtime_error("timing query read failed");read(id,field,value);*value=stamps.at(id);}
        static void APIENTRY Finish(){Check();++finishes;finish();}
        TimingHooks() {
            owner=std::this_thread::get_id();allocations=deletions=counters=reads=finishes=clock=0;unavailable=failRead=false;stamps.clear();ready.clear();
            generate=glad_glGenQueries;erase=glad_glDeleteQueries;counter=glad_glQueryCounter;available=glad_glGetQueryObjectiv;read=glad_glGetQueryObjectui64v;finish=glad_glFinish;
            glad_glGenQueries=Generate;glad_glDeleteQueries=Erase;glad_glQueryCounter=Counter;glad_glGetQueryObjectiv=Available;glad_glGetQueryObjectui64v=Read;glad_glFinish=Finish;
        }
        ~TimingHooks(){glad_glGenQueries=generate;glad_glDeleteQueries=erase;glad_glQueryCounter=counter;glad_glGetQueryObjectiv=available;glad_glGetQueryObjectui64v=read;glad_glFinish=finish;}
    };
    void TimingValidation()
    {
        OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;
        OvWindowing::Context::Device device(ds);OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=64;ws.height=32;ws.samples=1;
        OvWindowing::Window window(device,ws);window.MakeCurrentContext();OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
        auto domain=std::make_unique<baregl::DeletionQueue>();TimingHooks hooks;PoolSyncHooks sync;
        RenderPlan plan;plan.passes.push_back(Node("Slow",{}));plan.passes.push_back(Node("Fast",{}));
        plan.passes[0].operation=[] {return [](auto&) {TimingHooks::clock+=100;glClearColor(.25f,.5f,.75f,1);glClear(GL_COLOR_BUFFER_BIT);};};
        plan.passes[1].operation=[] {return [](auto&) {TimingHooks::clock+=30;};};
        OvRendering::Core::RenderTargetPool pool;auto mapping=pool.Realize(plan,plan.Compile());
        auto graph=std::make_shared<const FrameGraphDiagnostics>(CaptureFrameGraphDiagnostics(plan,plan.Compile(),*mapping));mapping.reset();
        auto timer=std::make_unique<FrameGraphGpuTimer>();OvTools::Jobs::JobSystem jobs(2);
        const auto record=[&](RenderFrame& frame) {
            const auto timing=timer->Capture(graph,frame);Require(bool(timing),"timing admission unexpectedly failed");
            RenderPlanInspection preview;preview.beforePass=1;preview.operation=[] {return [](auto&){TimingHooks::clock+=500;};};
            RenderCommandList list;plan.RecordOperations(list,{},{},preview,timing);list.Seal(&jobs);frame.Append(std::move(list));frame.Seal(&jobs);return timing;
        };
        {
            RenderFrame frame(17);const auto timing=record(frame);Require(!TimingHooks::allocations&&!TimingHooks::counters&&!timing->GetResult(),"timing queried GL while recording or encoding");
            frame.Submit(driver);Require(!timing->GetResult()&&TimingHooks::counters==4&&!TimingHooks::reads,"timing published/read before GPU retirement");
            sync.timeout=true;Require(!frame.Poll()&&!TimingHooks::reads&&!timing->GetResult(),"timing blocked/read on a pending GPU fence");sync.timeout=false;
            frame.Wait();const auto result=timing->GetResult();Require(result&&result->graph==graph&&result->frameID==17&&result->passNanoseconds==std::vector<uint64_t>{101,31}&&result->totalNanoseconds==132,"Pass timings included preview work, reordered Passes or mismatched the measured graph/frame");
            std::vector<uint8_t> pixels(64*32*4);glReadPixels(0,0,64,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            for(size_t i=0;i<pixels.size();i+=4)Require(std::abs(int(pixels[i])-64)<=1&&std::abs(int(pixels[i+1])-128)<=1&&std::abs(int(pixels[i+2])-191)<=1,"timing changed product pixels");
            bool readable=false;std::thread worker([&]{readable=result->graph->passes[0].name=="Slow"&&result->totalNanoseconds==132&&!glfwGetCurrentContext();});worker.join();Require(readable,"frozen timing values required GL on a Worker");
        }
        for(int i=0;i<16;++i){RenderFrame frame;auto timing=record(frame);frame.Submit(driver);frame.Wait();Require(bool(timing->GetResult()),"warm timing failed");}
        Require(TimingHooks::allocations==4&&!TimingHooks::finishes,"warm timing reallocated queries or forced GPU completion");
        {
            const auto reads=TimingHooks::reads;RenderFrame frame;auto timing=record(frame);TimingHooks::unavailable=true;frame.Submit(driver);frame.Wait();TimingHooks::unavailable=false;
            Require(!timing->GetResult()&&TimingHooks::reads==reads,"unavailable timestamps caused a blocking read");
        }
        {
            RenderFrame frame;auto timing=record(frame);TimingHooks::failRead=true;frame.Submit(driver);frame.Wait();TimingHooks::failRead=false;
            Require(frame.GetCompletion().Succeeded()&&!timing->GetResult(),"diagnostic query failure failed the product frame or published partial results");
        }
        {
            const auto reads=TimingHooks::reads;RenderFrame frame;auto timing=record(frame);frame.Cancel();Require(timing->IsRetired()&&!timing->GetResult()&&TimingHooks::reads==reads,"cancelled frame queried/published timings");
        }
        {
            const auto reads=TimingHooks::reads;RenderFrame frame;auto timing=timer->Capture(graph,frame);RenderCommandList list;
            plan.RecordOperations(list,{},{},{},timing);list.Append([](auto&){throw std::runtime_error("timed frame failure");});list.Seal();frame.Append(std::move(list));frame.Seal();frame.Submit(driver);frame.Wait();
            Require(!frame.GetCompletion().Succeeded()&&!timing->GetResult()&&TimingHooks::reads==reads,"failed product frame published timings");
        }
        {
            RenderFrame a,b,c,d;auto first=record(a),second=record(b),third=record(c);
            Require(!timer->Capture(graph,d)&&timer->GetStatistics().pending==3,"timing sample capacity was not bounded");
            a.Submit(driver);b.Submit(driver);c.Submit(driver);a.Wait();b.Wait();c.Wait();
            Require(first->GetResult()&&second->GetResult()&&third->GetResult()&&timer->GetStatistics().queries==12,"in-flight samples overwrote each other or exceeded query capacity");
        }
        {
            auto oversized=std::make_shared<FrameGraphDiagnostics>(*graph);oversized->passes.resize(FrameGraphGpuTimer::MaxPasses+1);RenderFrame frame;
            Require(!timer->Capture(oversized,frame)&&timer->GetStatistics().skipped==2,"oversized graph exceeded bounded timing allocation");
        }
        {
            unsigned calls=0;bool success=false;RenderFrame frame;frame.OnRetired([&](bool value){++calls;success=value;});frame.OnRetired([](bool){throw std::runtime_error("observer failure");});
            RenderCommandList list;list.Seal();frame.Append(std::move(list));frame.Seal();frame.Submit(driver);frame.Wait();frame.Poll();
            Require(calls==1&&success&&frame.GetCompletion().Succeeded(),"retirement observer changed frame success or fired repeatedly");
        }
        Require(!TimingHooks::finishes&&timer->GetStatistics().pending==0,"timing stalled or retained completed samples");
        // Closing a native domain invalidates pooled queries; a surviving CPU
        // timer recreates queries on the next domain without reusing stale IDs.
        const auto allocated=TimingHooks::allocations;domain->Close();Require(TimingHooks::deletions==allocated,"domain close leaked pooled queries");domain.reset();domain=std::make_unique<baregl::DeletionQueue>();
        {RenderFrame frame;auto timing=record(frame);frame.Submit(driver);frame.Wait();Require(bool(timing->GetResult())&&TimingHooks::allocations==allocated+4,"GPU timing pool did not rebuild for a new deletion domain");}
        std::thread release([&]{timer.reset();});release.join();domain->Drain();Require(TimingHooks::deletions==TimingHooks::allocations,"Worker release leaked/double-deleted timing queries");
        Require(glGetError()==GL_NO_ERROR,"GPU timing validation introduced a GL error");
        std::puts("PASS: exact per-Pass timestamp intervals exclude preview work; delayed GPU publication, nonblocking availability, warm query reuse, 3-sample/512-Pass caps, cancellation/failure isolation, pixels, observer once-only, value-only Worker freeze, context rebuild and Worker query deletion");
    }
