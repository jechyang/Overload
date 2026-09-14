using System;
using Overload;

namespace First { public class Same : Behaviour { } }
namespace Second { public class Same : Behaviour { } }
namespace Gameplay
{
    [ScriptId("stable-player")]
    [ScriptAlias("Old.Game", "Old.Player")]
    [ScriptAlias("old-player-id")]
#if RENAMED
    public class RenamedPlayer : Behaviour { }
#else
    public class Player : Behaviour { }
#endif
    [ScriptId("玩家-稳定")]
    [ScriptAlias("旧程序集", "旧命名空间.玩家")]
    public class UnicodePlayer : Behaviour { }
    public abstract class Abstract : Behaviour { }
    public class Open<T> : Behaviour { }
    public class NoDefault : Behaviour { public NoDefault(int value) { } }
    internal class Hidden : Behaviour { }
    public class ConstructorMustNotRun : Behaviour
    {
        public ConstructorMustNotRun() { throw new Exception("Metadata discovery must not construct scripts"); }
    }
}
#if DUPLICATE_ID
namespace Conflict { [ScriptId("stable-player")] public class Other : Behaviour { } }
#endif
#if DUPLICATE_ALIAS
namespace Conflict { [ScriptAlias("Old.Game", "Old.Player")] public class Other : Behaviour { } }
#endif
#if ALIAS_PRIMARY
namespace Conflict { [ScriptAlias("Game", "First.Same")] public class Other : Behaviour { } }
#endif
#if EMPTY_ID
namespace Conflict { [ScriptId("")] public class Other : Behaviour { } }
#endif
