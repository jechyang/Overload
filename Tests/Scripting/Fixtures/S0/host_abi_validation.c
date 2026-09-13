#include <OvCore/Scripting/CSharp/HostABI.h>

_Static_assert(sizeof(OvCsVector3) == 12, "Vector3 ABI drift");
_Static_assert(sizeof(OvCsQuaternion) == 16, "Quaternion ABI drift");
_Static_assert(sizeof(OvCsFieldScalar) == 16, "field scalar ABI drift");
_Static_assert(sizeof(OvCsFieldValueView) == 48, "field input ABI drift");
_Static_assert(sizeof(OvCsOwnedFieldValue) == 48, "field output ABI drift");
_Static_assert(sizeof(OvCsSessionCreateInfo) == 64, "session create ABI drift");
_Static_assert(sizeof(OvCsTypeInfo) == 72, "type info ABI drift");
_Static_assert(sizeof(OvCsFieldInfo) == 80, "field info ABI drift");
_Static_assert(sizeof(OvCsInstanceCreateInfo) == 40, "instance create ABI drift");
_Static_assert(sizeof(OvCsLifecycleCall) == 40, "lifecycle ABI drift");
_Static_assert(offsetof(OvCsHostApi, release_owned_utf8) == 40, "table prefix ABI drift");
_Static_assert(sizeof(OvCsHostApi) == 184, "host table ABI drift");

int ovcs_host_abi_c_validation(void)
{
    return OVCS_HOST_ABI_VERSION == 1 && OVCS_HOST_API_SIZE == 184;
}
