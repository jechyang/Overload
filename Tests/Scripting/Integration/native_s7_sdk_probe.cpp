#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>
int wmain(int argc,wchar_t** argv){
    if(argc<3)return 2;
    HANDLE token=nullptr;DWORD app=0,size=0;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)||!GetTokenInformation(token,TokenIsAppContainer,&app,sizeof(app),&size)||!app)return 3;
    CloseHandle(token);
    for(int i=1;i<argc;++i){
        if(i==2)continue;
        HANDLE sdk=CreateFileW(argv[i],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);DWORD sdkError=GetLastError();
        if(sdk!=INVALID_HANDLE_VALUE){CloseHandle(sdk);std::puts("SDK_READABLE: isolation insufficient");return 4;}
        if(sdkError!=ERROR_ACCESS_DENIED){std::printf("SDK denial expected ACCESS_DENIED, got %lu\n",sdkError);return 5;}
    }
    HANDLE runtime=CreateFileW(argv[2],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(runtime==INVALID_HANDLE_VALUE)return 6;CloseHandle(runtime);
    wchar_t located[32768]{};const auto found=SearchPathW(nullptr,L"dotnet.exe",nullptr,32768,located,nullptr);
    if(found){std::puts("GLOBAL_DOTNET_DISCOVERABLE");return 7;}
    std::puts("PASS AppContainer=1 SDK_READ=ACCESS_DENIED PRIVATE_RUNTIME_READ=OK GLOBAL_DOTNET=ABSENT");return 0;
}
