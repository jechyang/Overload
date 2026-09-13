using Overload.Generated.Fixture;
public static class ScriptReload
{
    public static int Run(System.IntPtr entry, ulong context, ulong handle)
    {
        var api = new Api(entry, context);
        return api.R6_Values_call_Add(handle, 20, 22);
    }
}
