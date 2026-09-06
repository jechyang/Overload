#pragma once
struct ProductFenceHook
{
    inline static PFNGLFENCESYNCPROC original=nullptr;
    inline static OvWindowing::Window* window=nullptr;
    inline static int count=0,limit=0;
    static GLsync APIENTRY Fence(GLenum condition,GLbitfield flags)
    {auto result=original(condition,flags);if(++count==limit&&window)window->SetShouldClose(true);return result;}
    explicit ProductFenceHook(OvWindowing::Window* target=nullptr,int stopAfter=0)
    {original=glad_glFenceSync;count=0;limit=stopAfter;window=target;glad_glFenceSync=Fence;}
    ~ProductFenceHook(){glad_glFenceSync=original;window=nullptr;}
};
template<class F> void RejectProductCall(F&& fn,const char* message)
{bool rejected=false;try{fn();}catch(const std::logic_error&){rejected=true;}Require(rejected,message);}
