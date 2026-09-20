using System;
using System.Collections;
using System.Collections.Generic;

namespace Overload.UI
{
    public static class SetPropertyUtility
    {
        public static bool SetColor(ref Color current, Color value) => SetStruct(ref current, value);
        public static bool SetStruct<T>(ref T current, T value) where T : struct => Assign(ref current, value);
        public static bool SetClass<T>(ref T current, T value) where T : class? => Assign(ref current, value);
        private static bool Assign<T>(ref T current, T value) {
            if (EqualityComparer<T>.Default.Equals(current, value)) return false;
            current = value;
            return true;
        }
    }

    /// <summary>Instance-owned reference pool. Dispose it with its managed session.</summary>
    public sealed class ObjectPool<T> : IDisposable where T : class, new()
    {
        private readonly Stack<T> available = new();
        private readonly HashSet<T> borrowed = new(ReferenceEqualityComparer.Instance);
        private Action<T>? onGet, onRelease;
        private readonly int capacity;
        private bool closed, inCallback;

        public ObjectPool(Action<T>? actionOnGet = null, Action<T>? actionOnRelease = null, int maxRetained = 256) {
            if (maxRetained < 0) throw new ArgumentOutOfRangeException(nameof(maxRetained));
            onGet = actionOnGet; onRelease = actionOnRelease; capacity = maxRetained;
        }
        public int countAll => countActive + countInactive;
        public int countActive => borrowed.Count;
        public int countInactive => available.Count;
        private void Check() {
            ObjectDisposedException.ThrowIf(closed, this);
            if (inCallback) throw new InvalidOperationException("Pool callbacks cannot re-enter the pool");
        }
        private void Notify(Action<T>? callback, T value) {
            inCallback = true;
            try { callback?.Invoke(value); }
            finally { inCallback = false; }
        }
        public T Get() {
            Check();
            T value = available.TryPop(out T? cached) ? cached : new T();
            // A failed initializer is discarded, never published as borrowed.
            Notify(onGet, value);
            borrowed.Add(value);
            return value;
        }
        public void Release(T value) {
            Check(); ArgumentNullException.ThrowIfNull(value);
            if (!borrowed.Contains(value)) throw new InvalidOperationException("Object was not borrowed from this pool, or was already released");
            // A failed reset remains borrowed so the caller can repair/retry it.
            Notify(onRelease, value);
            borrowed.Remove(value);
            if (available.Count < capacity) available.Push(value);
        }
        public void Clear() { Check(); available.Clear(); }
        public void Dispose() {
            if (closed) return;
            Check(); closed = true;
            available.Clear(); borrowed.Clear(); onGet = onRelease = null;
        }
    }

    /// <summary>Unlike a static generic pool, this cannot root a retired project session.</summary>
    public sealed class ListPool<T> : IDisposable
    {
        private readonly ObjectPool<List<T>> pool;
        public ListPool(int maxRetained = 64) => pool = new(actionOnRelease: list => list.Clear(), maxRetained: maxRetained);
        public List<T> Get() => pool.Get();
        public void Release(List<T> list) => pool.Release(list);
        public int countInactive => pool.countInactive;
        public void Clear() => pool.Clear();
        public void Dispose() => pool.Dispose();
    }
}

namespace Overload.UI.Collections
{
    /// <summary>Unique indexed collection with constant-time swap removal; order is not stable.</summary>
    public sealed class IndexedSet<T> : IList<T> where T : notnull
    {
        private readonly List<T> values = new();
        private readonly Dictionary<T, int> positions;
        public IndexedSet(IEqualityComparer<T>? comparer = null) => positions = new(comparer);
        public int Count => values.Count;
        public bool IsReadOnly => false;
        public T this[int index] {
            get => values[index];
            set {
                ArgumentNullException.ThrowIfNull(value);
                T previous = values[index];
                if (positions.Comparer.Equals(previous, value)) {
                    positions.Remove(previous); positions.Add(value, index); values[index] = value; return;
                }
                if (positions.ContainsKey(value)) throw new ArgumentException("Duplicate value", nameof(value));
                positions.Add(value, index);
                positions.Remove(previous);
                values[index] = value;
            }
        }
        public void Add(T value) {
            if (!AddUnique(value)) throw new ArgumentException("Duplicate value", nameof(value));
        }
        public bool AddUnique(T value) {
            ArgumentNullException.ThrowIfNull(value);
            if (!positions.TryAdd(value, values.Count)) return false;
            values.Add(value); return true;
        }
        public bool Contains(T value) => positions.ContainsKey(value);
        public int IndexOf(T value) => positions.TryGetValue(value, out int index) ? index : -1;
        public bool Remove(T value) {
            if (!positions.TryGetValue(value, out int index)) return false;
            RemoveAt(index); return true;
        }
        public void RemoveAt(int index) {
            T removed = values[index];
            int last = values.Count - 1;
            if (index != last) {
                T replacement = values[last];
                values[index] = replacement; positions[replacement] = index;
            }
            values.RemoveAt(last); positions.Remove(removed);
        }
        public void RemoveAll(Predicate<T> match) {
            ArgumentNullException.ThrowIfNull(match);
            for (int i = values.Count - 1; i >= 0; --i) if (match(values[i])) RemoveAt(i);
        }
        public void Sort(Comparison<T> comparison) {
            ArgumentNullException.ThrowIfNull(comparison);
            // A throwing comparison must not leave the index map inconsistent.
            var sorted = new List<T>(values); sorted.Sort(comparison);
            values.Clear(); values.AddRange(sorted);
            for (int i = 0; i < values.Count; ++i) positions[values[i]] = i;
        }
        public void Clear() { values.Clear(); positions.Clear(); }
        public void CopyTo(T[] array, int arrayIndex) => values.CopyTo(array, arrayIndex);
        public IEnumerator<T> GetEnumerator() => values.GetEnumerator();
        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
        public void Insert(int index, T value) => throw new NotSupportedException("IndexedSet does not preserve insertion order");
    }
}
