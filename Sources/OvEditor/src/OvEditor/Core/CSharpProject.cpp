#include <OvEditor/Core/CSharpProject.h>
#include <OvEditor/Core/Context.h>
#include <OvCore/Scripting/DispatchScope.h>
#include <OvDebug/Logger.h>
#include <tinyxml2.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <set>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#undef APIENTRY
#include <Windows.h>
#include <objbase.h>
#include <wincrypt.h>

namespace OvEditor::Core
{
    namespace
    {
        namespace fs = std::filesystem;
        using Paths = OvCore::Scripting::CSharp::HostPaths;
        std::string Utf8(const fs::path& p) { auto s=p.generic_u8string();return {reinterpret_cast<const char*>(s.data()),s.size()}; }
        std::string Read(const fs::path& p) { std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}}; }
        void Write(const fs::path& p,const std::string& s) {std::ofstream f(p,std::ios::binary);f<<s;if(!f)throw std::runtime_error("Cannot write "+Utf8(p));}
        std::string Replace(std::string text,const std::string& key,const std::string& value) {size_t pos=0;while((pos=text.find(key,pos))!=std::string::npos){text.replace(pos,key.size(),value);pos+=value.size();}return text;}
        std::string Xml(std::string s) {s=Replace(s,"&","&amp;");s=Replace(s,"\"","&quot;");return Replace(s,"<","&lt;");}
        std::string Digest(const fs::path& file) {
            HCRYPTPROV provider=0;HCRYPTHASH hash=0;
            if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))throw std::runtime_error("SHA-256 provider unavailable");
            const auto cleanup=[&]{if(hash)CryptDestroyHash(hash);CryptReleaseContext(provider,0);};
            try {
                if(!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash))throw std::runtime_error("SHA-256 initialization failed");
                std::ifstream stream(file,std::ios::binary);char bytes[65536];
                while(stream.read(bytes,sizeof(bytes))||stream.gcount())if(!CryptHashData(hash,reinterpret_cast<const BYTE*>(bytes),static_cast<DWORD>(stream.gcount()),0))throw std::runtime_error("SHA-256 read failed");
                if(!stream.eof())throw std::runtime_error("Cannot read generation file");
                BYTE digest[32]{};DWORD length=32;if(!CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0))throw std::runtime_error("SHA-256 finalization failed");
                std::string result;constexpr char hex[]="0123456789abcdef";for(auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}cleanup();return result;
            }catch(...){cleanup();throw;}
        }
        fs::path FindExe(const wchar_t* name) {wchar_t p[32768]{};const auto n=SearchPathW(nullptr,name,nullptr,32768,p,nullptr);return n && n<32768?fs::path(p):fs::path{};}
        std::wstring Quote(const std::wstring& value) {
            std::wstring result=L"\"";size_t slashes=0;
            for(auto c:value){if(c==L'\\'){++slashes;continue;}if(c==L'"')result.append(slashes*2+1,L'\\');else result.append(slashes,L'\\');slashes=0;result+=c;}
            result.append(slashes*2,L'\\');return result+L'"';
        }
        std::string Unique() {GUID g{};if(FAILED(CoCreateGuid(&g)))throw std::runtime_error("GUID creation failed");wchar_t s[40]{};StringFromGUID2(g,s,40);return Utf8(fs::path(s)).substr(1,36);}
    }
    CSharpProject::CSharpProject(Context& context):m_context(context)
    {
        auto& settings=context.projectSettings;
        ResolveTools();
        watch=settings.GetOrDefault<bool>("csharp_auto_build",true);
        hotReload=settings.GetOrDefault<bool>("csharp_hot_reload",false);
        m_configuration=settings.GetOrDefault<std::string>("csharp_configuration","Debug");
        if(m_configuration!="Debug"&&m_configuration!="Release")m_configuration="Debug";
        const auto assembly=Paths::FromUtf8(settings.GetOrDefault<std::string>("csharp_project_assembly",""));
        if(context.csharpHost && !context.scriptEngine->CSharpTypes().empty() && assembly.filename()=="GameScripts.dll"&&fs::exists(assembly.parent_path()/"manifest.xml"))m_applied=assembly.parent_path();
        m_snapshot=Snapshot();
        if(watch&&(fs::exists(context.projectFolder/"GameScripts.csproj")||!m_snapshot.empty()))RequestBuild(m_configuration);
    }
    void CSharpProject::ResolveTools()
    {
        // Read fresh settings on each retry so fixing a tool path needs no restart.
        auto& settings=m_context.projectSettings;
        settings.Reload();
        m_engine=Paths::FromUtf8(settings.GetOrDefault<std::string>("csharp_engine_root",""));
        if(m_engine.empty()) {
            auto p=OvCore::Scripting::CSharp::RuntimeHost::ExecutableDirectory();
            while(!p.empty()){if(fs::exists(p/"Scripts/Windows/BuildGameScripts.mjs")){m_engine=p;break;}auto parent=p.parent_path();if(parent==p)break;p=parent;}
        }
        m_node=Paths::FromUtf8(settings.GetOrDefault<std::string>("csharp_node",Utf8(FindExe(L"node.exe"))));
        if(m_node.empty()&&fs::is_regular_file(m_engine/"Build/Toolchains/node/node.exe"))m_node=m_engine/"Build/Toolchains/node/node.exe";
        // Version probes and Python discovery belong to the background worker.
        m_python=Paths::FromUtf8(settings.GetOrDefault<std::string>("csharp_python",""));
        m_cache=Paths::FromUtf8(settings.GetOrDefault<std::string>("csharp_build_cache",Utf8(m_context.projectFolder/".overload/csharp")));
    }
    CSharpProject::~CSharpProject() {if(m_job)CloseHandle(m_job);if(m_process)CloseHandle(m_process);}
    void CSharpProject::CheckMain() const {if(std::this_thread::get_id()!=m_main)throw std::logic_error("C# editor workflow requires Main");}
    void CSharpProject::Fail(const std::string& error, bool log) {m_status=m_applied.empty()?"C#: build failed; fix the error and save or retry":"C#: failed; previous version retained";m_diagnostics=error;if(log)OVLOG_ERROR(error);}
    std::vector<std::string> CSharpProject::ReportDiagnostics(const std::string& log)
    {
        // MSBuild prints "file(line,col): error CS1002: message [project]", once inline and once in its summary.
        static const std::regex pattern(R"(^\s*(.*?)(?:\((\d+),(\d+)\))?\s*:\s*(error|warning)\s+([A-Za-z]+\d+)\s*:\s*(.*?)(?:\s+\[[^\]]*\])?\s*$)");
        std::vector<std::string> errors;std::set<std::string> seen;std::istringstream lines(log);std::string line,location;
        while(std::getline(lines,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            std::smatch m;if(!std::regex_match(line,m,pattern))continue;
            location=m[1].str();
            const auto relative=fs::path(Paths::FromUtf8(location)).lexically_relative(m_context.projectFolder);
            if(!relative.empty()&&*relative.begin()!="..")location=Utf8(relative);
            const auto text=location+(m[2].matched?"("+m[2].str()+","+m[3].str()+")":"")+": "+m[4].str()+" "+m[5].str()+": "+m[6].str();
            if(!seen.insert(text).second)continue;
            if(m[4]=="error"){errors.push_back(text);OVLOG_ERROR(text);}else OVLOG_WARNING(text);
        }
        return errors;
    }
    std::string CSharpProject::Tail(const std::string& text, size_t lines)
    {
        size_t position=text.size();
        while(position>0&&lines>0){position=text.find_last_of('\n',position-1);if(position==std::string::npos){position=0;break;}--lines;}
        return text.substr(position==0?0:position+1);
    }
    void CSharpProject::SetAutomaticBuild(bool enabled)
    {
        CheckMain();watch=enabled;m_dirty=false;
        auto& settings=m_context.projectSettings;settings.Reload();
        if(!settings.Set("csharp_auto_build",enabled))settings.Add("csharp_auto_build",enabled);settings.Rewrite();
        if(enabled&&(fs::exists(m_context.projectFolder/"GameScripts.csproj")||!Snapshot().empty()))RequestBuild(m_configuration);
    }
    void CSharpProject::SetHotReload(bool enabled)
    {
        CheckMain();hotReload=enabled;
        auto& settings=m_context.projectSettings;settings.Reload();
        if(!settings.Set("csharp_hot_reload",enabled))settings.Add("csharp_hot_reload",enabled);settings.Rewrite();
    }
    void CSharpProject::EnsureProject()
    {
        ResolveTools();
        if(!m_engine.is_absolute()||!fs::exists(m_engine/"Scripts/Windows/BuildGameScripts.mjs"))throw std::runtime_error("Set csharp_engine_root to the Overload source directory");
        const auto project=m_context.projectFolder/"GameScripts.csproj";
        if(!fs::exists(project)) {
            Write(project,Replace(Read(m_engine/"Tools/OvCSharp/Templates/GameScripts.csproj"),"@ENGINE@",Xml(Utf8(m_engine))));
            if(!fs::exists(m_context.projectFolder/"NuGet.Config"))Write(m_context.projectFolder/"NuGet.Config","<configuration><packageSources><clear /></packageSources></configuration>\n");
            if(!fs::exists(m_context.projectFolder/"global.json"))fs::copy_file(m_engine/"global.json",m_context.projectFolder/"global.json");
            if(!fs::exists(m_context.projectFolder/"packages.lock.json"))Write(m_context.projectFolder/"packages.lock.json","{\"version\":1,\"dependencies\":{\"net10.0\":{\"Overload.ScriptCore\":{\"type\":\"Project\"}}}}\n");
        }
    }
    bool CSharpProject::CreateScript(const fs::path& destination)
    {
        CheckMain();try {
            const auto relative=fs::weakly_canonical(destination).lexically_relative(fs::weakly_canonical(m_context.projectAssetsPath));
            if(relative.empty()||*relative.begin()==".."||destination.extension()!=".cs"||fs::exists(destination))throw std::runtime_error("Choose a new .cs file inside Assets");
            EnsureProject();const auto id=Unique();auto name=Utf8(destination.stem());
            if(!std::regex_match(name,std::regex("[A-Za-z_][A-Za-z0-9_]*")))name="Script_"+Replace(id,"-","");
            fs::create_directories(destination.parent_path());
            Write(destination,Replace(Replace(Read(m_engine/"Tools/OvCSharp/Templates/Behaviour.cs.txt"),"@NAME@",name),"@ID@",id));
            RequestBuild(m_configuration);return true;
        }catch(const std::exception& e){Fail(e.what());return false;}
    }
    void CSharpProject::RequestBuild(const std::string& configuration)
    {
        CheckMain();if(configuration!="Debug"&&configuration!="Release"){Fail("Expected Debug or Release");return;}
        m_configuration=configuration;m_requested=true;m_pending.clear();
        m_status=m_process?"C#: building; latest request queued":"C#: build queued";
    }
    std::string CSharpProject::Snapshot() const
    {
        std::set<std::string> entries;
        const bool hasProject=fs::exists(m_context.projectFolder/"GameScripts.csproj");
        std::error_code ec;
        for(fs::recursive_directory_iterator it(m_context.projectFolder,fs::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)) {
            if(ec){ec.clear();continue;}const auto& p=it->path();auto name=p.filename().string();std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(it->is_directory()) {if(name=="bin"||name=="obj"||name==".overload"||name==".git"||name=="build"||name=="generations"||name=="artifacts"||fs::is_symlink(it->symlink_status())||fs::weakly_canonical(p)==fs::weakly_canonical(m_cache))it.disable_recursion_pending();continue;}
            auto ext=p.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(!hasProject && (ext!=".cs" || p.lexically_relative(m_context.projectAssetsPath).empty() || *p.lexically_relative(m_context.projectAssetsPath).begin()==".."))continue;
            if(ext!=".cs"&&ext!=".csproj"&&ext!=".props"&&ext!=".targets"&&name!="global.json"&&name!="nuget.config"&&name!="packages.lock.json")continue;
            entries.insert(Utf8(p)+":"+std::to_string(it->last_write_time().time_since_epoch().count())+":"+std::to_string(it->file_size()));
        }
        std::string result;for(const auto& s:entries)result+=s+'\n';return result;
    }
    void CSharpProject::Launch()
    {
        EnsureProject();if(!m_node.is_absolute()||!fs::is_regular_file(m_node))throw std::runtime_error("Node.js was not found. Set csharp_node to an absolute node.exe path in the .ovproject file, then retry Build.");
        if(!m_python.empty()&&(!m_python.is_absolute()||!fs::is_regular_file(m_python)))throw std::runtime_error("Invalid csharp_python. Set an absolute Python 3.10+ executable path in the .ovproject file, or remove the setting for automatic discovery, then retry Build.");
        if(!m_cache.is_absolute())throw std::runtime_error("csharp_build_cache must be absolute");
        const auto requests=m_context.projectFolder/".overload/requests";fs::create_directories(requests);
        const auto id=Unique();m_result=requests/(id+".xml");m_log=requests/(id+".log");
        std::vector<std::wstring> args={m_node.wstring(),(m_engine/"Scripts/Windows/BuildGameScripts.mjs").wstring(),L"--project",m_context.projectFolder.wstring(),L"--cache",m_cache.wstring(),L"--configuration",Paths::FromUtf8(m_configuration).wstring(),L"--result",m_result.wstring()};
        if(!m_python.empty()){args.push_back(L"--python");args.push_back(m_python.wstring());}
        std::wstring command;for(const auto& arg:args){if(!command.empty())command+=L' ';command+=Quote(arg);}
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE log=CreateFileW(m_log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(log==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create build log");
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;startup.hStdOutput=log;startup.hStdError=log;
        PROCESS_INFORMATION process{};
        HANDLE job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){if(job)CloseHandle(job);CloseHandle(log);throw std::runtime_error("Cannot create build job");}
        const BOOL created=CreateProcessW(m_node.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,m_engine.c_str(),&startup,&process);CloseHandle(log);
        if(!created){CloseHandle(job);throw std::runtime_error("Cannot start Node build worker");}
        if(!AssignProcessToJobObject(job,process.hProcess)){TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(job);throw std::runtime_error("Cannot own build worker");}
        ResumeThread(process.hThread);CloseHandle(process.hThread);m_job=job;m_process=process.hProcess;
        m_requested=false;m_dirty=false;m_snapshot=Snapshot();m_buildConfiguration=m_configuration;m_diagnostics.clear();m_status="C#: building "+m_buildConfiguration;
    }
    bool CSharpProject::Tick(bool stopped)
    {
        CheckMain();if(OvCore::Scripting::DispatchScope::Active())return false;
        try {
            const auto now=std::chrono::steady_clock::now();
            if(watch&&now-m_scan>std::chrono::milliseconds(250)) {
                m_scan=now;const auto snapshot=Snapshot();if(snapshot!=m_snapshot){m_snapshot=snapshot;m_changed=now;m_dirty=true;}
                if(m_dirty&&now-m_changed>std::chrono::milliseconds(600)){m_dirty=false;RequestBuild(m_configuration);}
            }
            if(m_process && WaitForSingleObject(m_process,0)==WAIT_OBJECT_0) {
                DWORD code=1;GetExitCodeProcess(m_process,&code);CloseHandle(m_process);m_process=nullptr;CloseHandle(m_job);m_job=nullptr;
                const auto log=Read(m_log);
                if(code==3) {
                    // Sources changed mid-build: not an error; the watcher queues the next build.
                    m_diagnostics.clear();m_status="C#: sources changed; rebuilding";
                    if(!m_requested&&!m_dirty){m_dirty=true;m_changed=now;}
                }
                else if(code!=0) {
                    const auto errors=ReportDiagnostics(log);
                    Fail(errors.empty()?Tail(log,40)+"\nLog: "+Utf8(m_log):std::to_string(errors.size())+(errors.size()==1?" compiler error":" compiler errors")+"; see the Console.\nLog: "+Utf8(m_log),errors.empty());
                    if(!errors.empty())for(const auto& error:errors)m_diagnostics+="\n"+error;
                }
                else if(ReportDiagnostics(log);!m_requested&&!m_dirty&&m_buildConfiguration==m_configuration) {
                    tinyxml2::XMLDocument doc;const auto text=Read(m_result);
                    if(doc.Parse(text.c_str())!=tinyxml2::XML_SUCCESS||!doc.RootElement()||!doc.RootElement()->Attribute("generation"))throw std::runtime_error("Missing complete build receipt");
                    m_pending=Paths::FromUtf8(doc.RootElement()->Attribute("generation"));m_status=hotReload?"C#: built; reload pending Main safe point":"C#: built; reload pending Stop";
                }
            }
            if(!m_process&&m_requested)Launch();
            if(!m_pending.empty()&&(stopped||hotReload)&&!m_requested&&!m_dirty&&!m_process) {
                const auto generation=m_pending;m_pending.clear();
                if(generation==m_applied){m_status="C#: up to date";return false;}
                tinyxml2::XMLDocument manifest;const auto text=Read(generation/"manifest.xml");
                if(manifest.Parse(text.c_str())!=tinyxml2::XML_SUCCESS||!manifest.RootElement())throw std::runtime_error("Incomplete generation manifest");
                std::set<std::string> complete;
                for(auto* file=manifest.RootElement()->FirstChildElement("file");file;file=file->NextSiblingElement("file")) {
                    if(!file->Attribute("path"))throw std::runtime_error("Invalid generation entry");
                    const auto p=Paths::FromUtf8(file->Attribute("path"));
                    if(p.empty()||p.is_absolute()||*p.begin()==".."||!file->Attribute("size")||!fs::is_regular_file(generation/p)||fs::file_size(generation/p)!=std::stoull(file->Attribute("size")))throw std::runtime_error("Incomplete generation dependency");
                    for(const auto& part:p)if(part=="..")throw std::runtime_error("Generation dependency escapes directory");
                    if(!file->Attribute("sha256")||Digest(generation/p)!=file->Attribute("sha256"))throw std::runtime_error("Generation dependency hash mismatch");
                    if(!complete.insert(Utf8(p)).second)throw std::runtime_error("Duplicate generation dependency");
                }
                for(const auto* required:{"GameScripts.dll","GameScripts.deps.json","Overload.ScriptCore.dll","host/Overload.ScriptCore.dll","host/Overload.ManagedHost.dll","host/Overload.ManagedHost.deps.json","host/Overload.ManagedHost.runtimeconfig.json"})
                    if(!complete.contains(required))throw std::runtime_error("Missing generation dependency");
                const auto runtime=m_engine/"Build/Toolchains/dotnet-10.0.401";
                if(!m_context.csharpHost) {
                    auto host=std::make_unique<OvCore::Scripting::CSharp::RuntimeHost>(m_context.scriptingMain);
                    host->Start(Paths::Resolve(OvCore::Scripting::CSharp::RuntimeHost::ExecutableDirectory(),runtime,generation/"host"));host->OpenSession(m_context.projectFolder);m_context.csharpHost=std::move(host);
                }
                if(!m_context.scriptEngine->LoadCSharpProject(*m_context.csharpHost,m_context.projectFolder,generation/"GameScripts.dll","GameScripts",!stopped))throw std::runtime_error(m_context.scriptEngine->LastError());
                m_applied=generation;m_status="C#: applied "+m_configuration;
                auto& settings=m_context.projectSettings;settings.Reload();
                const auto save=[&](const std::string& key,const std::string& value){if(!settings.Set(key,value))settings.Add(key,value);};
                if(!settings.Set("csharp_enabled",true))settings.Add("csharp_enabled",true);
                save("csharp_runtime_root",Utf8(runtime));save("csharp_managed_root",Utf8(generation/"host"));save("csharp_project_assembly",Utf8(generation/"GameScripts.dll"));save("csharp_assembly_name","GameScripts");save("csharp_configuration",m_configuration);settings.Rewrite();
                return true;
            }
        }catch(const std::exception& e){m_requested=false;m_pending.clear();Fail(e.what());}
        return false;
    }
}
