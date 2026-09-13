#include <OvCore/Scripting/CSharp/HostABI.h>

int ovcs_host_abi_cpp_validation()
{
    return OVCS_HOST_ABI_VERSION == 1 && OVCS_HOST_API_SIZE == 184;
}
