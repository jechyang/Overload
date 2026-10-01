using System;
using System.Collections.Generic;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public struct ColorBlock
{
    public Color normalColor, highlightedColor, pressedColor, selectedColor, disabledColor;
    public float colorMultiplier, fadeDuration;
    public static ColorBlock defaultColorBlock => new() {
        normalColor = new(1, 1, 1, 1),
        highlightedColor = new(0.96f, 0.96f, 0.96f, 1),
        pressedColor = new(0.78f, 0.78f, 0.78f, 1),
        selectedColor = new(0.96f, 0.96f, 0.96f, 1),
        disabledColor = new(0.78f, 0.78f, 0.78f, 0.5f),
        colorMultiplier = 1,
        fadeDuration = 0.1f
    };
}

public enum NavigationMode { None, Horizontal, Vertical, Automatic, Explicit }

public struct Navigation
{
    public NavigationMode mode;
    public Selectable? selectOnUp, selectOnDown, selectOnLeft, selectOnRight;
    public static Navigation defaultNavigation => new() { mode = NavigationMode.Automatic };
}

public struct SpriteState
{
    public AssetRef highlightedSprite, pressedSprite, selectedSprite, disabledSprite;
}

public class AnimationTriggers
{
    public string normalTrigger = "Normal";
    public string highlightedTrigger = "Highlighted";
    public string pressedTrigger = "Pressed";
    public string selectedTrigger = "Selected";
    public string disabledTrigger = "Disabled";
}

public interface IUIAnimationDriver
{
    void Play(string trigger);
    void Stop();
}

public struct ColorTween
{
    public Color startColor, targetColor;
    public float duration;
    public Action<Color>? onTween;
    public void TweenValue(float t) {
        if (onTween == null) return;
        if (duration <= 0) { onTween(targetColor); return; }
        t = Math.Clamp(t, 0, 1);
        Color L(Color a, Color b) => new(
            a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
        onTween(L(startColor, targetColor));
    }
}

public class Selectable : UIBehaviour,
    IMoveHandler, IPointerDownHandler, IPointerUpHandler,
    IPointerEnterHandler, IPointerExitHandler, ISelectHandler, IDeselectHandler
{
    public enum Transition { None, ColorTint, SpriteSwap, Animation }
    public enum SelectionState { Normal, Highlighted, Pressed, Selected, Disabled }

    [SerializeField] private NavigationMode m_NavigationMode = NavigationMode.Automatic;
    [SerializeField] private Transition m_Transition = Transition.ColorTint;
    [SerializeField] private float m_NormalR = 1, m_NormalG = 1, m_NormalB = 1, m_NormalA = 1;
    [SerializeField] private float m_HighlightedR = 0.96f, m_HighlightedG = 0.96f, m_HighlightedB = 0.96f, m_HighlightedA = 1;
    [SerializeField] private float m_PressedR = 0.78f, m_PressedG = 0.78f, m_PressedB = 0.78f, m_PressedA = 1;
    [SerializeField] private float m_SelectedR = 0.96f, m_SelectedG = 0.96f, m_SelectedB = 0.96f, m_SelectedA = 1;
    [SerializeField] private float m_DisabledR = 0.78f, m_DisabledG = 0.78f, m_DisabledB = 0.78f, m_DisabledA = 0.5f;
    [SerializeField] private float m_ColorMultiplier = 1, m_FadeDuration = 0.1f;
    [SerializeField] private bool m_Interactable = true;
    [SerializeField] private Graphic? m_TargetGraphic;

    private bool m_GroupsAllowInteraction = true;
    private bool m_IsPointerInside, m_IsPointerDown, m_HasSelection;
    private IUIAnimationDriver? m_AnimationDriver;
    private static readonly List<Selectable> s_List = new();

    public override void OnEnable() {
        base.OnEnable();
        if (!s_List.Contains(this)) s_List.Add(this);
        RefreshCanvasGroups();
        DoStateTransition(currentSelectionState, true);
    }
    public override void OnDisable() {
        s_List.Remove(this);
        base.OnDisable();
    }
    internal override void ReleaseNativeComponents() {
        s_List.Remove(this);
        base.ReleaseNativeComponents();
    }

    public NavigationMode navigationMode {
        get => m_NavigationMode;
        set { m_NavigationMode = value; DoStateTransition(currentSelectionState, true); }
    }
    public Transition transition {
        get => m_Transition;
        set { m_Transition = value; DoStateTransition(currentSelectionState, true); }
    }
    public ColorBlock colors {
        get => new() {
            normalColor = new(m_NormalR, m_NormalG, m_NormalB, m_NormalA),
            highlightedColor = new(m_HighlightedR, m_HighlightedG, m_HighlightedB, m_HighlightedA),
            pressedColor = new(m_PressedR, m_PressedG, m_PressedB, m_PressedA),
            selectedColor = new(m_SelectedR, m_SelectedG, m_SelectedB, m_SelectedA),
            disabledColor = new(m_DisabledR, m_DisabledG, m_DisabledB, m_DisabledA),
            colorMultiplier = m_ColorMultiplier,
            fadeDuration = m_FadeDuration
        };
        set {
            m_NormalR = value.normalColor.r; m_NormalG = value.normalColor.g;
            m_NormalB = value.normalColor.b; m_NormalA = value.normalColor.a;
            m_HighlightedR = value.highlightedColor.r; m_HighlightedG = value.highlightedColor.g;
            m_HighlightedB = value.highlightedColor.b; m_HighlightedA = value.highlightedColor.a;
            m_PressedR = value.pressedColor.r; m_PressedG = value.pressedColor.g;
            m_PressedB = value.pressedColor.b; m_PressedA = value.pressedColor.a;
            m_SelectedR = value.selectedColor.r; m_SelectedG = value.selectedColor.g;
            m_SelectedB = value.selectedColor.b; m_SelectedA = value.selectedColor.a;
            m_DisabledR = value.disabledColor.r; m_DisabledG = value.disabledColor.g;
            m_DisabledB = value.disabledColor.b; m_DisabledA = value.disabledColor.a;
            m_ColorMultiplier = value.colorMultiplier; m_FadeDuration = value.fadeDuration;
            DoStateTransition(currentSelectionState, true);
        }
    }
    public Graphic? targetGraphic {
        get => m_TargetGraphic;
        set { m_TargetGraphic = value; DoStateTransition(currentSelectionState, true); }
    }
    public bool interactable {
        get => m_Interactable;
        set {
            if (m_Interactable == value) return;
            m_Interactable = value;
            DoStateTransition(currentSelectionState, true);
        }
    }
    public bool isPointerInside => m_IsPointerInside;
    public bool isPointerDown => m_IsPointerDown;
    public bool hasSelection => m_HasSelection;
    public IUIAnimationDriver? animationDriver {
        get => m_AnimationDriver;
        set => m_AnimationDriver = value;
    }
    public SelectionState currentSelectionState {
        get {
            if (!IsInteractable()) return SelectionState.Disabled;
            if (m_IsPointerDown) return SelectionState.Pressed;
            if (m_HasSelection) return SelectionState.Selected;
            if (m_IsPointerInside) return SelectionState.Highlighted;
            return SelectionState.Normal;
        }
    }
    protected bool IsInteractable() => m_Interactable && m_GroupsAllowInteraction;

    public virtual void OnPointerDown(PointerEventData eventData) {
        if (eventData.button != PointerEventData.InputButton.Left) return;
        m_IsPointerDown = true;
        EvaluateAndTransitionToSelectionState(false);
    }
    public virtual void OnPointerUp(PointerEventData eventData) {
        if (eventData.button != PointerEventData.InputButton.Left) return;
        m_IsPointerDown = false;
        EvaluateAndTransitionToSelectionState(false);
    }
    public virtual void OnPointerEnter(PointerEventData eventData) {
        m_IsPointerInside = true;
        EvaluateAndTransitionToSelectionState(false);
    }
    public virtual void OnPointerExit(PointerEventData eventData) {
        m_IsPointerInside = false;
        EvaluateAndTransitionToSelectionState(false);
    }
    public virtual void OnSelect(BaseEventData eventData) {
        m_HasSelection = true;
        EvaluateAndTransitionToSelectionState(false);
    }
    public virtual void OnDeselect(BaseEventData eventData) {
        m_HasSelection = false;
        EvaluateAndTransitionToSelectionState(false);
    }
    public virtual void OnMove(AxisEventData eventData) {
        // Explicit navigation targets are provided by controls; automatic
        // directional search is completed with the EventSystem selection pass.
    }

    protected void EvaluateAndTransitionToSelectionState(bool instant)
        => DoStateTransition(currentSelectionState, instant);

    protected virtual void DoStateTransition(SelectionState state, bool instant) {
        if (!IsActive()) return;
        Color tint = state switch {
            SelectionState.Normal => new(m_NormalR, m_NormalG, m_NormalB, m_NormalA),
            SelectionState.Highlighted => new(m_HighlightedR, m_HighlightedG, m_HighlightedB, m_HighlightedA),
            SelectionState.Pressed => new(m_PressedR, m_PressedG, m_PressedB, m_PressedA),
            SelectionState.Selected => new(m_SelectedR, m_SelectedG, m_SelectedB, m_SelectedA),
            _ => new(m_DisabledR, m_DisabledG, m_DisabledB, m_DisabledA)
        };
        if (m_Transition == Transition.ColorTint && m_TargetGraphic != null) {
            tint = new Color(tint.r * m_ColorMultiplier, tint.g * m_ColorMultiplier,
                tint.b * m_ColorMultiplier, tint.a);
            m_TargetGraphic.CrossFadeColor(tint, instant ? 0 : m_FadeDuration, true, true);
        } else if (m_Transition == Transition.Animation) {
            string trigger = state switch {
                SelectionState.Normal => "Normal",
                SelectionState.Highlighted => "Highlighted",
                SelectionState.Pressed => "Pressed",
                SelectionState.Selected => "Selected",
                _ => "Disabled"
            };
            m_AnimationDriver?.Play(trigger);
        }
    }

    public virtual void Select() {
        EventSystem.GetCurrent(Owner)?.SetSelectedObject(Owner);
    }

    public void RefreshCanvasGroups() {
        bool allow = true;
        for (Actor? a = Owner; a != null; a = a.Parent) {
            foreach (var group in a.GetComponents<CanvasGroup>()) {
                if (!group.interactable) { allow = false; break; }
                if (group.ignoreParentGroups) break;
            }
            if (!allow) break;
        }
        if (allow != m_GroupsAllowInteraction) {
            m_GroupsAllowInteraction = allow;
            DoStateTransition(currentSelectionState, false);
        }
    }
}
