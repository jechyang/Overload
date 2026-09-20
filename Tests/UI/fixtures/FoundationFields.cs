using System;
using Overload;
using Overload.Maths;
using Overload.UI;

namespace UIFixture.Foundation;

public enum SparseMode : short { None, Horizontal = 2, Vertical = 4 }
[Flags] public enum FieldFlags : byte { None = 0, First = 1, Last = 8 }
public sealed class ValueFields : Behaviour
{
    public Vector2 point = new(1, 2);
    public Vector4 vector = new(1, 2, 3, 4);
    public Rect rect = new(new(-2, -3), new(10, 20));
    public RectOffset padding = new(1, 2, 3, 4);
    public Color tint = new(1, 0.5f, 0.25f, 1);
    public Vector3 position = new(1, 2, 3);
    public Quaternion rotation = new(0, 0, 0, 1);
    public float scalar = 0.5f;
    public double precise = 0.25;
    public SparseMode mode = SparseMode.Horizontal;
    public FieldFlags flags = FieldFlags.First;
    public string text = "field";
    public ComponentRef<RectTransform> rectTarget;
    public ComponentRef<ValueFields> scriptTarget;
    public int referencesPassed;
    public int hierarchyPassed;
    public int restored;
    public override void OnReloadRestore() => ++restored;
    private void HierarchyContracts()
    {
        if (hierarchyPassed != 0) {
            if (hierarchyPassed == 3) {
                if (Owner.Find("U1 hierarchy parent") != null) throw new Exception("Destroyed hierarchy survived frame boundary");
                hierarchyPassed = 4; return;
            }
            var root = Owner.Find("U1 hierarchy parent") ?? throw new Exception("Hierarchy root missing");
            var child = Owner.Find("U1 child B") ?? throw new Exception("Hierarchy child missing");
            var leaf = Owner.Find("U1 grandchild") ?? throw new Exception("Hierarchy leaf missing");
            if (hierarchyPassed == 1) {
                if (leaf.ActiveInHierarchy || root.GetComponentsInChildren<Transform>().Length != 3 ||
                    root.GetComponentsInChildren<Transform>(true).Length != 5 ||
                    leaf.GetComponentsInParent<Transform>(true).Length != 3 || leaf.GetComponentsInParent<Transform>().Length != 1)
                    throw new Exception("Inactive subtree and parent enumeration failed after dispatch boundary");
                child.Active = true; hierarchyPassed = 2; return;
            }
            if (!leaf.ActiveInHierarchy) throw new Exception("Inherited activation failed after dispatch boundary");
            bool rejected = false;
            try { root.Parent = leaf; } catch (Exception) { rejected = true; }
            if (!rejected || root.Parent != null) throw new Exception("Cycle rejection changed hierarchy");
            root.Destroy(); if (root.IsAlive) throw new Exception("Pending destroy remains live");
            hierarchyPassed = 3; return;
        }
        var parent = Owner.Create("U1 hierarchy parent");
        var a = Owner.Create("U1 child A"); a.Parent = parent;
        var b = Owner.Create("U1 child B"); b.Parent = parent;
        var c = Owner.Create("U1 child C"); c.Parent = parent;
        var grandchild = Owner.Create("U1 grandchild"); grandchild.Parent = b;
        void Check(bool condition, string label) { if (!condition) throw new Exception("Hierarchy contract: " + label); }
        Check(a.GetSiblingIndex() == 0 && b.GetSiblingIndex() == 1 && c.GetSiblingIndex() == 2, "initial child order");
        c.SetAsFirstSibling(); a.SetAsLastSibling();
        Check(parent.GetChild(0).Guid == c.Guid && parent.GetChild(1).Guid == b.Guid && parent.GetChild(2).Guid == a.Guid, "child reorder snapshot");
        b.SetSiblingIndex(int.MaxValue);
        Check(b.GetSiblingIndex() == 2 && b.Parent?.Guid == parent.Guid, "clamped sibling index preserves parent");
        bool negativeRejected = false;
        try { b.SetSiblingIndex(-1); } catch (ArgumentOutOfRangeException) { negativeRejected = true; }
        Check(negativeRejected, "negative sibling index");
        int last = parent.GetSiblingIndex(); parent.SetAsFirstSibling();
        Check(parent.GetSiblingIndex() == 0, "root first sibling");
        parent.SetAsLastSibling(); Check(parent.GetSiblingIndex() == last, "root last sibling");
        var order = parent.GetComponentsInChildren<Transform>();
        Check(order.Length == 5 && order[0].Owner.Guid == parent.Guid && order[1].Owner.Guid == c.Guid &&
            order[2].Owner.Guid == a.Guid && order[3].Owner.Guid == b.Guid && order[4].Owner.Guid == grandchild.Guid, "ordered depth first component snapshot");
        // Activation is queued until the current native/managed dispatch returns.
        b.Active = false; hierarchyPassed = 1;
    }

    public override void OnUpdate(float deltaTime)
    {
        if (Owner.Find("UI value fields")?.Guid != Owner.Guid) return;
        if (deltaTime == 1) {
            var rect = rectTarget.Resolve(Owner) ?? throw new Exception("Native component reference did not resolve");
            var script = scriptTarget.Resolve(Owner) ?? throw new Exception("Managed component reference did not resolve");
            if (ComponentRef<RectTransform>.From(Owner, rect) != rectTarget ||
                ComponentRef<ValueFields>.From(Owner, script) != scriptTarget ||
                ActorRef.From(Owner, script.Owner).Guid != scriptTarget.ActorGuid)
                throw new Exception("Reference identity roundtrip failed");
            HierarchyContracts();
            ++referencesPassed;
        } else if (deltaTime == 2) {
            if (rectTarget.Resolve(Owner) != null || scriptTarget.Resolve(Owner) != null)
                throw new Exception("Removed component reference must resolve null");
            ++referencesPassed;
        }
    }
}
