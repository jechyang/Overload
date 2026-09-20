using System;
using System.Collections.Generic;
using Overload.UI;

namespace Overload.EventSystems;

public class EventSystem : UIBehaviour
{
    private IDisposable? registration;
    private bool selecting, processing;
    private ulong lastSequence;
    private BaseInputModule? activeModule;
    [SerializeField] private ActorRef m_FirstSelected;
    public Actor? firstSelected {
        get => m_FirstSelected.Guid == 0 ? null : m_FirstSelected.Resolve(Owner);
        set => m_FirstSelected = value == null ? default : ActorRef.From(Owner,value);
    }
    public Actor? currentSelected { get; private set; }
    public BaseInputModule? currentInputModule => activeModule;
    public bool alreadySelecting => selecting;
    [SerializeField] private bool m_SendNavigationEvents = true;
    [SerializeField] private int m_PixelDragThreshold = 5;
    public bool sendNavigationEvents { get => m_SendNavigationEvents; set => m_SendNavigationEvents = value; }
    public int pixelDragThreshold { get => m_PixelDragThreshold; set { if (value < 0) throw new ArgumentOutOfRangeException(nameof(value)); m_PixelDragThreshold = value; } }
    public bool isFocused { get; private set; } = true;
    // Explicit context avoids a global "current" retaining a previous managed session.
    public static EventSystem? GetCurrent(Actor context) {
        foreach (var system in UISessionRegistry<EventSystem>.For(context).Snapshot())
            if (system.IsActive() && system.Owner.Access.Query(system.Owner.Handle,19,context.Handle) != 0) return system;
        return null;
    }
    public override void OnEnable() => registration ??= UISessionRegistry<EventSystem>.For(Owner).Register(this, this);
    public override void OnDisable() {
        activeModule?.DeactivateModule(); activeModule = null;
        SetSelectedObject(null); registration?.Dispose(); registration = null;
    }
    public override void OnDestroy() => OnDisable();
    public override void OnReloadRelease() => OnDisable();
    public override void OnReloadRestore() { if (IsActive()) OnEnable(); }
    public void SetSelectedObject(Actor? selected, BaseEventData? data = null) {
        if (selecting || ExecuteEvents.Same(currentSelected, selected)) return;
        if (selected != null && !ReferenceEquals(selected.Access.UIRegistry, Owner.Access.UIRegistry)) throw new InvalidOperationException("Cross-session selection");
        if (selected != null && !ExecuteEvents.Live(selected)) selected = null;
        selecting = true;
        try {
            data ??= new BaseEventData(this);
            Actor? previous = currentSelected; currentSelected = null;
            ExecuteEvents.Execute(previous, data, ExecuteEvents.deselectHandler);
            if (ExecuteEvents.Live(selected)) {
                currentSelected = selected;
                ExecuteEvents.Execute(selected, data, ExecuteEvents.selectHandler);
                if (!ExecuteEvents.Live(selected)) currentSelected = null;
            }
        } finally { selecting = false; }
    }
    public bool IsPointerOverObject(int pointerId = -1) => activeModule?.IsPointerOverObject(pointerId) ?? false;
    public void RaycastAll(PointerEventData data, List<RaycastResult> results) {
        ArgumentNullException.ThrowIfNull(results); results.Clear();
        foreach (var raycaster in RaycasterManager.GetRaycasters(Owner)) if (raycaster.IsActive()) raycaster.Raycast(data, results);
        results.RemoveAll(r => !r.isValid || !ExecuteEvents.Live(r.actor) || !float.IsFinite(r.distance) || r.distance < 0);
        for (int i = 0; i < results.Count; ++i) { var r = results[i]; r.index = i; results[i] = r; }
        results.Sort(CompareRaycasts);
    }
    public static int CompareRaycasts(RaycastResult a, RaycastResult b) {
        int comparison;
        if (!ReferenceEquals(a.module, b.module)) {
            comparison = (b.module?.cameraDepth ?? 0).CompareTo(a.module?.cameraDepth ?? 0); if (comparison != 0) return comparison;
            comparison = (b.module?.sortOrderPriority ?? 0).CompareTo(a.module?.sortOrderPriority ?? 0); if (comparison != 0) return comparison;
            comparison = (b.module?.renderOrderPriority ?? 0).CompareTo(a.module?.renderOrderPriority ?? 0); if (comparison != 0) return comparison;
        }
        comparison = b.sortingLayer.CompareTo(a.sortingLayer); if (comparison != 0) return comparison;
        comparison = b.sortingOrder.CompareTo(a.sortingOrder); if (comparison != 0) return comparison;
        if (ReferenceEquals(a.module?.rootRaycaster, b.module?.rootRaycaster)) { comparison = b.depth.CompareTo(a.depth); if (comparison != 0) return comparison; }
        comparison = a.distance.CompareTo(b.distance); return comparison != 0 ? comparison : a.index.CompareTo(b.index);
    }
    public override void OnUpdate(float deltaTime) => Process();
    public void Process() {
        if (!IsActive() || processing || !ReferenceEquals(GetCurrent(Owner), this)) return;
        processing = true;
        try {
            BaseInputModule? chosen = null;
            var capturedInputs = new HashSet<BaseInput>();
            foreach (var module in Owner.GetComponents<BaseInputModule>()) {
                if (!module.IsActive() || !module.IsModuleSupported()) continue;
                if (capturedInputs.Add(module.input)) module.input.Capture();
                module.UpdateModule();
                if (chosen == null && module.ShouldActivateModule()) chosen = module;
            }
            if (chosen == null) { activeModule?.DeactivateModule(); activeModule = null; return; }
            bool changed = !ReferenceEquals(chosen, activeModule);
            if (changed) { activeModule?.DeactivateModule(); activeModule = chosen; activeModule.ActivateModule(); }
            var frame = chosen.input.snapshot;
            if (!changed && frame.sequence == lastSequence) return;
            lastSequence = frame.sequence; isFocused = frame.focused;
            // Even a focus loss followed by regain in one tick must revoke capture.
            foreach (var e in frame.events) if (e.kind == UIInputKind.Focus && e.id == 0) { chosen.CancelInput(); SetSelectedObject(null); }
            if (!isFocused) { chosen.CancelInput(); SetSelectedObject(null); return; }
            if (!ExecuteEvents.Live(currentSelected)) currentSelected = null;
            chosen.Process();
        } finally { processing = false; }
    }
    protected override void SetDirty() { }
    internal override void EnsureNativeComponents() { }
}

public abstract class BaseInputModule : UIBehaviour
{
    [SerializeField] private ComponentRef<BaseInput> m_InputOverride;
    public BaseInput? inputOverride {
        get => m_InputOverride.ActorGuid == 0 ? null : m_InputOverride.Resolve(Owner);
        set => m_InputOverride = value == null ? default : ComponentRef<BaseInput>.From(Owner,value);
    }
    public BaseInput input => inputOverride ?? Owner.GetComponent<BaseInput>() ?? throw new InvalidOperationException("InputModule requires a BaseInput on its Actor");
    public EventSystem eventSystem => Owner.GetComponent<EventSystem>() ?? throw new InvalidOperationException("InputModule requires EventSystem");
    public virtual void UpdateModule() { }
    public virtual bool IsModuleSupported() => true;
    public virtual bool ShouldActivateModule() => IsActive();
    public virtual void ActivateModule() { if (eventSystem.currentSelected == null) eventSystem.SetSelectedObject(eventSystem.firstSelected); }
    public virtual void DeactivateModule() => CancelInput();
    public virtual void CancelInput() { }
    public virtual bool IsPointerOverObject(int pointerId) => false;
    public abstract void Process();
    public override void OnDisable() => DeactivateModule();
    public override void OnDestroy() => DeactivateModule();
    public override void OnReloadRelease() => DeactivateModule();
    protected override void SetDirty() { }
    internal override void EnsureNativeComponents() { }
}
