using System;
using System.Collections.Generic;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

[Serializable]
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

[Serializable]
public struct SpriteState
{
    public AssetRef highlightedSprite, pressedSprite, selectedSprite, disabledSprite;
}

[Serializable]
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
    [SerializeField, FieldId("m_TargetGraphic")] private ComponentRef<Graphic> m_TargetGraphicRef;
    private Graphic? m_TargetGraphic { get => ResolveUIReference(m_TargetGraphicRef); set => m_TargetGraphicRef = value == null ? default : ComponentRef<Graphic>.From(Owner, value); }

    private bool m_GroupsAllowInteraction = true;
    private bool m_IsPointerInside, m_IsPointerDown, m_HasSelection;
    private IUIAnimationDriver? m_AnimationDriver;
    private IDisposable? registration;
    [SerializeField] private SpriteState m_SpriteState;
    [SerializeField] private AnimationTriggers m_AnimationTriggers=new();
    [SerializeField] private ComponentRef<Selectable> m_Up,m_Down,m_Left,m_Right;
    private AssetRef normalSprite;
    public SpriteState spriteState {get=>m_SpriteState;set{m_SpriteState=value;EvaluateAndTransitionToSelectionState(true);}}
    public AnimationTriggers animationTriggers {get=>m_AnimationTriggers;set=>m_AnimationTriggers=value??new();}
    public Navigation navigation {
        get=>new(){mode=m_NavigationMode,selectOnUp=ResolveUIReference(m_Up),selectOnDown=ResolveUIReference(m_Down),selectOnLeft=ResolveUIReference(m_Left),selectOnRight=ResolveUIReference(m_Right)};
        set{m_NavigationMode=value.mode;m_Up=Reference(value.selectOnUp);m_Down=Reference(value.selectOnDown);m_Left=Reference(value.selectOnLeft);m_Right=Reference(value.selectOnRight);}
    }
    private ComponentRef<Selectable> Reference(Selectable? value)=>value==null?default:ComponentRef<Selectable>.From(Owner,value);
    public static Selectable[] AllSelectables(Actor context)=>UISessionRegistry<Selectable>.For(context).Snapshot();

    public override void OnEnable() {
        base.OnEnable();
        registration??=UISessionRegistry<Selectable>.For(Owner).Register(this,this);
        if(targetGraphic is Image image)normalSprite=image.sprite;
        RefreshCanvasGroups();
        DoStateTransition(currentSelectionState, true);
    }
    public override void OnDisable() {
        registration?.Dispose();registration=null;
        m_IsPointerInside=m_IsPointerDown=m_HasSelection=false;
        m_AnimationDriver?.Stop();
        base.OnDisable();
    }
    internal override void ReleaseNativeComponents() {
        registration?.Dispose();registration=null;
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
        set { m_TargetGraphic = value;if(value is Image image)normalSprite=image.sprite; DoStateTransition(currentSelectionState, true); }
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
    public bool IsInteractable() => m_Interactable && enabled && (owner==null || Owner.ActiveInHierarchy && GroupsAllowInteraction());

    public virtual void OnPointerDown(PointerEventData eventData) {
        if (eventData.button != PointerEventData.InputButton.Left) return;
        if(!IsInteractable())return;
        if(navigationMode!=NavigationMode.None)Select();
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
        if(!IsInteractable() || navigationMode==NavigationMode.None)return;
        var nav=navigation;
        Selectable? target=eventData.moveDir switch {MoveDirection.Up=>nav.selectOnUp,MoveDirection.Down=>nav.selectOnDown,MoveDirection.Left=>nav.selectOnLeft,MoveDirection.Right=>nav.selectOnRight,_=>null};
        if(navigationMode!=NavigationMode.Explicit){
            bool horizontal=eventData.moveDir is MoveDirection.Left or MoveDirection.Right;
            if(navigationMode==NavigationMode.Horizontal&&!horizontal||navigationMode==NavigationMode.Vertical&&horizontal)return;
            var direction=eventData.moveDir switch{MoveDirection.Right=>new Vector2(1,0),MoveDirection.Left=>new Vector2(-1,0),MoveDirection.Up=>new Vector2(0,1),_=>new Vector2(0,-1)};
            target=FindSelectable(direction);
        }
        if(target!=null&&target.IsActive()&&target.IsInteractable()){eventData.selectedObject=target.Owner;eventData.Use();}
    }

    public Selectable? FindSelectable(Vector2 direction) {
        if(owner==null)return null;
        Selectable? best=null;float score=0;var start=Clipping.CanvasRect(rectTransform);var center=start.min+start.size*0.5f;
        foreach(var candidate in AllSelectables(Owner)){
            if(ReferenceEquals(candidate,this)||!candidate.IsActive()||!candidate.IsInteractable()||candidate.navigationMode==NavigationMode.None||Owner.Access.Query(Owner.Handle,19,candidate.Owner.Handle)==0)continue;
            var bounds=Clipping.CanvasRect(candidate.rectTransform);var delta=bounds.min+bounds.size*0.5f-center;
            float dot=delta.x*direction.x+delta.y*direction.y;float length=delta.x*delta.x+delta.y*delta.y;
            if(dot<=0||length<0.0001f)continue;float value=dot/length;
            if(value>score){score=value;best=candidate;}
        }
        return best;
    }
    protected override void SetDirty(){base.SetDirty();if(IsActive())EvaluateAndTransitionToSelectionState(true);}
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
        } else if(m_Transition==Transition.SpriteSwap && m_TargetGraphic is Image image){
            AssetRef sprite=state switch{SelectionState.Highlighted=>m_SpriteState.highlightedSprite,SelectionState.Pressed=>m_SpriteState.pressedSprite,SelectionState.Selected=>m_SpriteState.selectedSprite,SelectionState.Disabled=>m_SpriteState.disabledSprite,_=>normalSprite};
            image.sprite=string.IsNullOrEmpty(sprite.Path)?normalSprite:sprite;
        } else if (m_Transition == Transition.Animation) {
            string trigger = state switch {
                SelectionState.Normal => m_AnimationTriggers.normalTrigger,
                SelectionState.Highlighted => m_AnimationTriggers.highlightedTrigger,
                SelectionState.Pressed => m_AnimationTriggers.pressedTrigger,
                SelectionState.Selected => m_AnimationTriggers.selectedTrigger,
                _ => m_AnimationTriggers.disabledTrigger
            };
            (m_AnimationDriver ??= new UIAnimationDriver(this)).Play(trigger);
        }
    }

    public virtual void Select() {
        EventSystem.GetCurrent(Owner)?.SetSelectedObject(Owner);
    }

    private bool GroupsAllowInteraction(){
        if(owner==null)return true;
        for(Actor? actor=Owner;actor!=null;actor=actor.Parent){bool stop=false;
            foreach(var group in actor.GetComponents<CanvasGroup>()){
                if(!group.IsActive())continue;if(!group.interactable)return false;if(group.ignoreParentGroups)stop=true;
            }
            if(stop)break;
        }
        return true;
    }
    public void RefreshCanvasGroups(){bool allow=GroupsAllowInteraction();if(allow==m_GroupsAllowInteraction)return;m_GroupsAllowInteraction=allow;EvaluateAndTransitionToSelectionState(false);}
}

/// <summary>Default state animation; projects can replace this driver with their animation system.</summary>
public sealed class UIAnimationDriver(Selectable owner) : IUIAnimationDriver
{
    public string currentTrigger {get;private set;}="";
    public void Play(string trigger){
        currentTrigger=trigger;
        if(owner.targetGraphic is not Graphic graphic)return;
        var colors=owner.colors;var triggers=owner.animationTriggers;
        Color tint=trigger==triggers.disabledTrigger?colors.disabledColor:trigger==triggers.pressedTrigger?colors.pressedColor:trigger==triggers.highlightedTrigger?colors.highlightedColor:trigger==triggers.selectedTrigger?colors.selectedColor:colors.normalColor;
        graphic.CrossFadeColor(tint,colors.fadeDuration,true,true);
    }
    public void Stop(){currentTrigger="";}
}
