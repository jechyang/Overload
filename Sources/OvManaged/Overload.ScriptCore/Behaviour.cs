using System;

namespace Overload;

// S2 exposes only the type contract. Owner, object construction and gameplay
// lifecycle dispatch are deliberately introduced together in S3.
public abstract class Behaviour { }

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class ScriptIdAttribute(string id) : Attribute
{
    public string Id { get; } = id;
}

[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = false)]
public sealed class ScriptAliasAttribute : Attribute
{
    public string Id { get; }
    public string Assembly { get; }
    public string Type { get; }
    public ScriptAliasAttribute(string id) { Id = id; Assembly = Type = ""; }
    public ScriptAliasAttribute(string assembly, string type) { Assembly = assembly; Type = type; Id = ""; }
}
