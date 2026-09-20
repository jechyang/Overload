using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Overload.UI;
using Overload.UI.Collections;

namespace UIFixture.Foundation;

public static class FoundationCollections
{
    public sealed class Item { public int Value; }
    private static void Check(bool value, string message) {
        if (!value) throw new InvalidOperationException(message);
    }
    private static void Throws<T>(Action action) where T : Exception {
        try { action(); }
        catch (T) { return; }
        throw new InvalidOperationException("Expected " + typeof(T).Name);
    }
    public static int Main() {
        Run(); Console.WriteLine("PASS U1-COLLECTIONS: indexing, randomized mutations, pool ownership, callback rollback, event dispatch and cleanup"); return 0;
    }
    public static void Run() {
        var values = new IndexedSet<int>();
        var expected = new HashSet<int>();
        var random = new Random(8721);
        for (int step = 0; step < 3000; ++step) {
            int value = random.Next(128);
            switch (random.Next(5)) {
                case 0: Check(values.AddUnique(value) == expected.Add(value), "unique insertion"); break;
                case 1: Check(values.Remove(value) == expected.Remove(value), "removal result"); break;
                case 2:
                    if (values.Count > 0) {
                        int index = random.Next(values.Count), old = values[index];
                        if (old != value && expected.Contains(value)) Throws<ArgumentException>(() => values[index] = value);
                        else { values[index] = value; expected.Remove(old); expected.Add(value); }
                    }
                    break;
                case 3: values.Sort((a, b) => a.CompareTo(b)); break;
                case 4:
                    values.RemoveAll(x => x % 11 == value % 11);
                    expected.RemoveWhere(x => x % 11 == value % 11); break;
            }
            Check(values.Count == expected.Count && expected.SetEquals(values), "set membership after mutation");
            for (int i = 0; i < values.Count; ++i) Check(values.IndexOf(values[i]) == i, "index cache after mutation");
            Check(values.IndexOf(999) == -1, "missing index sentinel");
        }
        values.Clear(); values.Add(1); values.Add(2);
        Throws<ArgumentException>(() => values.Add(1));
        Throws<InvalidOperationException>(() => values.Sort((_, _) => throw new Exception("comparer failed")));
        Check(values.SequenceEqual(new[] { 1, 2 }) && values.IndexOf(2) == 1, "throwing sort leaves collection unchanged");
        Throws<NotSupportedException>(() => values.Insert(0, 3));
        var copy = new int[4]; values.CopyTo(copy, 1);
        Check(copy.SequenceEqual(new[] { 0, 1, 2, 0 }), "copy offset");

        int gets = 0, resets = 0;
        using var pool = new ObjectPool<Item>(_ => ++gets, item => { item.Value = 0; ++resets; }, maxRetained: 1);
        Item first = pool.Get(), second = pool.Get(); first.Value = 42;
        Check(pool.countActive == 2 && pool.countAll == 2, "borrow counts");
        pool.Release(first); pool.Release(second);
        Check(pool.countAll == 1 && pool.countInactive == 1 && resets == 2, "bounded retention resets discarded objects too");
        Throws<InvalidOperationException>(() => pool.Release(first));
        Throws<InvalidOperationException>(() => pool.Release(new Item()));
        Item reused = pool.Get(); Check(ReferenceEquals(reused, first) && reused.Value == 0 && gets == 3, "reuse resets data");
        pool.Clear(); Check(pool.countActive == 1 && pool.countAll == 1, "clear does not forget outstanding borrow");
        pool.Release(reused); pool.Dispose();
        Check(pool.countAll == 0, "dispose releases owned references");
        Throws<ObjectDisposedException>(() => pool.Get());

        bool fail = true;
        using var rollback = new ObjectPool<Item>(actionOnRelease: _ => { if (fail) throw new InvalidOperationException(); });
        Item borrowed = rollback.Get();
        Throws<InvalidOperationException>(() => rollback.Release(borrowed));
        Check(rollback.countActive == 1 && rollback.countInactive == 0, "failed reset keeps borrow valid");
        fail = false; rollback.Release(borrowed);
        using var failedGet = new ObjectPool<Item>(actionOnGet: _ => throw new InvalidOperationException());
        Throws<InvalidOperationException>(() => failedGet.Get()); Check(failedGet.countAll == 0, "failed get does not leak accounting");
        using var lists = new ListPool<Item>();
        List<Item> list = lists.Get(); list.Add(borrowed); lists.Release(list);
        Check(lists.Get().Count == 0, "released list drops project references");

        int number = 2;
        Check(!SetPropertyUtility.SetStruct(ref number, 2) && SetPropertyUtility.SetStruct(ref number, 3) && number == 3, "struct dirty decision");
        string? text = null;
        Check(!SetPropertyUtility.SetClass(ref text, null) && SetPropertyUtility.SetClass(ref text, "x"), "nullable class dirty decision");
        Color color = Color.white;
        Check(!SetPropertyUtility.SetColor(ref color, Color.white) && SetPropertyUtility.SetColor(ref color, Color.red), "color dirty decision");
        using var events = new UIEvent<int>();
        var calls = new List<int>();
        Action<int> later = value => calls.Add(value * 100);
        Action<int> removed = value => calls.Add(value * 10);
        events.AddListener(value => { calls.Add(value); events.RemoveListener(removed); events.AddListener(later); });
        events.AddListener(removed);
        events.Invoke(1);
        Check(calls.SequenceEqual(new[] { 1 }), "dispatch skips removed listeners and defers additions");
        calls.Clear(); events.Invoke(2);
        Check(calls.SequenceEqual(new[] { 2, 200 }), "next invocation sees deferred listener once");
        events.RemoveAllListeners(); calls.Clear();
        using var subscription = events.Subscribe(value => calls.Add(value));
        Task.Run(() => Throws<InvalidOperationException>(() => subscription.Dispose())).GetAwaiter().GetResult();
        events.Invoke(3); Check(calls.SequenceEqual(new[] { 3 }), "failed worker cancellation preserves main-thread receipt");
        subscription.Dispose(); events.Invoke(4);
        Check(calls.Count == 1, "explicit unsubscribe");
        events.AddListener(_ => events.Dispose());
        events.AddListener(_ => throw new Exception("disposed source continued dispatch"));
        events.Invoke(5);
        Throws<ObjectDisposedException>(() => events.AddListener(later));
        using var click = new UIEvent(); int clicks = 0;
        Action action = () => ++clicks;
        click.AddListener(action); click.AddListener(action); click.Invoke();
        Check(clicks == 1, "duplicate runtime listeners collapse");
        click.RemoveListener(action); click.Invoke(); Check(clicks == 1, "untyped event removal");
    }
}
