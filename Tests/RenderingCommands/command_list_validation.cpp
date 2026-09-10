#define TARGET_STATE_VALIDATION_NO_MAIN
#include "target_state_validation.cpp"

struct GPUCounts {unsigned draws=0,uploads=0,allocations=0,clears=0,targets=0;};
GPUCounts gpuCounts;
PFNGLDRAWELEMENTSPROC commandDraw;
PFNGLNAMEDBUFFERSUBDATAPROC commandUpload;
PFNGLNAMEDBUFFERDATAPROC commandAllocate;
PFNGLCLEARPROC commandClear;
PFNGLBINDFRAMEBUFFERPROC commandTarget;
void APIENTRY CountDraw(GLenum mode,GLsizei count,GLenum type,const void* indices){++gpuCounts.draws;commandDraw(mode,count,type,indices);}
void APIENTRY CountUpload(GLuint buffer,GLintptr offset,GLsizeiptr size,const void* data){++gpuCounts.uploads;commandUpload(buffer,offset,size,data);}
void APIENTRY CountAllocate(GLuint buffer,GLsizeiptr size,const void* data,GLenum usage){++gpuCounts.allocations;commandAllocate(buffer,size,data,usage);}
void APIENTRY CountClear(GLbitfield mask){++gpuCounts.clears;commandClear(mask);}
void APIENTRY CountTarget(GLenum target,GLuint buffer){++gpuCounts.targets;commandTarget(target,buffer);}
struct CommandHooks
{
    CommandHooks(){gpuCounts={};commandDraw=glad_glDrawElements;glad_glDrawElements=CountDraw;commandUpload=glad_glNamedBufferSubData;glad_glNamedBufferSubData=CountUpload;
        commandAllocate=glad_glNamedBufferData;glad_glNamedBufferData=CountAllocate;commandClear=glad_glClear;glad_glClear=CountClear;commandTarget=glad_glBindFramebuffer;glad_glBindFramebuffer=CountTarget;}
    ~CommandHooks(){glad_glDrawElements=commandDraw;glad_glNamedBufferSubData=commandUpload;glad_glNamedBufferData=commandAllocate;glad_glClear=commandClear;glad_glBindFramebuffer=commandTarget;}
};

void HistoryValidation(Context::Driver& driver)
{
    using List=Data::RenderCommandList;
    List::History history;
    int calls=0;
    auto record=[&]() { List list; list.UseHistory(history); list.Append([&](auto&){++calls;}); list.Seal(); return list; };
    auto first=record();auto second=record();first.Execute(driver);second.Execute(driver);
    Require(calls==2 && !history.PrepareImmediate(),"ordered history did not complete");
    for(int mode=0;mode<4;++mode)
    {
        List parent;Require(!parent.UseHistory(history),"unexpected healthy history reset");
        parent.Append([&](auto&){++calls;throw std::runtime_error("history execution failure");});
        List child;child.UseHistory(history);child.Append([&](auto&){++calls;});child.Seal();
        if(mode==0)parent.Cancel();
        if(mode==1)parent.FailRecording();
        if(mode==2){parent.Seal();try{parent.Execute(driver);}catch(const std::runtime_error&){}}
        if(mode==3){List abandoned(std::move(parent));}
        const auto before=calls;bool rejected=false;
        try{child.Execute(driver);}catch(const std::logic_error&){rejected=true;}
        Require(rejected && calls==before && child.GetExecutedCount()==0,"invalid descendant executed GPU prefix");
        List recovery;Require(recovery.UseHistory(history),"invalid history did not request reset");
        Require(!recovery.UseHistory(history),"same-list history registered twice");recovery.Seal();recovery.Execute(driver);
    }
    {auto parent=record();auto child=record();const auto before=calls;bool rejected=false;
     try{child.Execute(driver);}catch(const std::logic_error&){rejected=true;}
     Require(rejected && calls==before,"out-of-order history was consumed");parent.Cancel();
     Require(history.PrepareImmediate(),"immediate fallback did not reset poisoned history");}
    {auto pending=record();bool rejected=false;try{history.PrepareImmediate();}catch(const std::logic_error&){rejected=true;}
     Require(rejected,"immediate work overtook a pending temporal frame");pending.Execute(driver);}
    {List::History other;List parent;parent.UseHistory(history);List bridge;bridge.UseHistory(history);bridge.UseHistory(other);bridge.Seal();
     List tail;tail.UseHistory(other);tail.Append([&](auto&){++calls;});tail.Seal();parent.Cancel();
     try{bridge.Execute(driver);}catch(const std::logic_error&){}const auto before=calls;
     try{tail.Execute(driver);}catch(const std::logic_error&){}
     Require(tail.GetState()==List::State::FAILED && calls==before,"failure did not propagate between histories");}
    {List::History detached;List survivor;survivor.UseHistory(detached);survivor.Seal();
     List moved(std::move(survivor));moved.Execute(driver);Require(!detached.PrepareImmediate(),"move invalidated history");}
    {List surviving;{List::History destroyed;surviving.UseHistory(destroyed);}surviving.Seal();surviving.Execute(driver);}
    std::puts("PASS: temporal ordering, cancel/record/execute/abandon failure, dependent rejection, reset, move and owner destruction");
}

void CommandLifetimeValidation(const std::filesystem::path& dir)
{
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=64;ws.height=32;ws.samples=1;
    OvWindowing::Window window(device,ws);window.MakeCurrentContext();Context::Driver driver(Settings::DriverSettings{true});
    std::ofstream images(dir/"command-lifetime-frames.bin",std::ios::binary);
    HistoryValidation(driver);
    using List=Data::RenderCommandList;
    {List allocation;bool failed=false;allocationFailureSize=sizeof(List::Operation);allocationFailureCountdown=0;
     try{allocation.Append([](auto&){});}catch(const std::bad_alloc&){failed=true;}allocationFailureCountdown=-1;
     Require(failed && allocation.GetState()==List::State::FAILED,"append allocation failure did not invalidate sequence");}
    std::vector<int> order;List failure;
    failure.Append([&](auto&){order.push_back(1);});failure.Append([&](auto&){order.push_back(2);throw std::runtime_error("expected");});failure.Append([&](auto&){order.push_back(3);});failure.Seal();
    bool failed=false;try{failure.Execute(driver);}catch(const std::runtime_error&){failed=true;}
    Require(failed && failure.GetState()==List::State::FAILED && failure.GetExecutedCount()==1 && order==std::vector<int>{1,2},"failure ran suffix or reported completion");
    bool rejected=false;try{failure.Execute(driver);}catch(const std::logic_error&){rejected=true;}Require(rejected,"failed sequence replay accepted");
    auto retained=std::make_shared<int>(7);std::weak_ptr<int> weak=retained;List cancelled;cancelled.Append([retained](auto&){});retained.reset();cancelled.Seal();cancelled.Cancel();Require(weak.expired() && cancelled.GetState()==List::State::CANCELLED,"cancel did not release captures");
    List source;source.Append([](auto&){});source.Seal();List moved(std::move(source));Require(source.GetState()==List::State::CANCELLED,"move source remained executable");moved.Execute(driver);
    Require(glGetError()==GL_NO_ERROR && images.good(),"command lifetime GL/output failure");std::puts("PASS: terminal failure, cancellation and move ownership");
}

#ifndef COMMAND_LIST_VALIDATION_MAIN
#define COMMAND_LIST_VALIDATION_MAIN main
#endif
int COMMAND_LIST_VALIDATION_MAIN(int argc,char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);Require(argc==2,"pass output directory");
    PreparationValidation();GeometryValidation(argv[1]);MaterialValidation(argv[1]);SnapshotValidation(argv[1]);CommandLifetimeValidation(argv[1]);TargetStateValidation();
    const auto immediate=std::filesystem::path(argv[1])/"immediate",delayed=std::filesystem::path(argv[1])/"delayed";std::filesystem::create_directories(immediate);std::filesystem::create_directories(delayed);
    ImageValidation(immediate,false,false);ImageValidation(delayed,false,true,true);
    const auto read=[](const auto& p){std::ifstream f(p,std::ios::binary);return std::vector<char>(std::istreambuf_iterator<char>(f),{});};Require(read(immediate/"frames.bin")==read(delayed/"frames.bin"),"RenderFrontend delayed command images differ");
    std::puts("PASS: 24 real Mannequin serial/Worker full RenderFrontend command frames exactly match");
    const auto dynamicImmediate=std::filesystem::path(argv[1])/"dynamic-immediate",dynamicDelayed=std::filesystem::path(argv[1])/"dynamic-delayed";
    std::filesystem::create_directories(dynamicImmediate);std::filesystem::create_directories(dynamicDelayed);
    ImageValidation(dynamicImmediate,false,false,false,true);ImageValidation(dynamicDelayed,false,true,true,true);
    const auto dynamicRepeat=std::filesystem::path(argv[1])/"dynamic-repeat",dynamicPacket=std::filesystem::path(argv[1])/"dynamic-packet";
    std::filesystem::create_directories(dynamicRepeat);std::filesystem::create_directories(dynamicPacket);
    ImageValidation(dynamicRepeat,false,false,false,true);ImageValidation(dynamicPacket,false,true,false,true);
    Require(read(dynamicImmediate/"frames.bin")==read(dynamicRepeat/"frames.bin"),"repeated dynamic immediate image changed");
    Require(read(dynamicImmediate/"frames.bin")==read(dynamicPacket/"frames.bin"),"dynamic same-draw packet image differs");
    Require(read(dynamicImmediate/"frames.bin")==read(dynamicDelayed/"frames.bin"),"dynamic shadow/reflection/exposure/bloom command images differ");
    const auto recovered=std::filesystem::path(argv[1])/"dynamic-recovered";
    std::filesystem::create_directories(recovered);ImageValidation(recovered,false,true,true,true,true);
    Require(read(dynamicImmediate/"frames.bin")==read(recovered/"frames.bin"),"cancelled/failed temporal history did not rebuild exact dynamic image");
    std::puts("PASS: 24 dynamic frames recover exactly after cancellation, partial execution failure, abandonment and dependent cancellation; progressive exposure and on-demand progressive reflection reset");
    std::puts("PASS: 24 dynamic shadow/reflection/mipmap/exposure/bloom/tonemapping/FXAA immediate-delayed image pairs");return 0;
}
catch(const std::exception& e){allocationFailureCountdown=-1;std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
