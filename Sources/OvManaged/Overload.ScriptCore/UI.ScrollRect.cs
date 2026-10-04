using System;
using Overload.EventSystems;
using Overload.Maths;
namespace Overload.UI;

public class ScrollRect : UIBehaviour, IDragHandler, IInitializePotentialDragHandler, IBeginDragHandler, IEndDragHandler, IScrollHandler, ICanvasElement
{
    public enum MovementType { Unrestricted, Elastic, Clamped }
    public enum ScrollbarVisibility { Permanent, AutoHide, AutoHideAndExpandViewport }
    [SerializeField, FieldId("m_Content")] private ComponentRef<RectTransform> m_ContentRef;
    [SerializeField, FieldId("m_Viewport")] private ComponentRef<RectTransform> m_ViewportRef;
    [SerializeField] private ComponentRef<Scrollbar> m_HorizontalScrollbar, m_VerticalScrollbar;
    [SerializeField] private bool m_Horizontal=true, m_Vertical=true, m_Inertia=true;
    [SerializeField] private MovementType m_MovementType=MovementType.Elastic;
    [SerializeField] private float m_Elasticity=0.1f,m_DecelerationRate=0.135f,m_ScrollSensitivity=1;
    [SerializeField] private ScrollbarVisibility m_HorizontalScrollbarVisibility,m_VerticalScrollbarVisibility;
    [SerializeField] private float m_HorizontalScrollbarSpacing,m_VerticalScrollbarSpacing;
    private Vector2 m_Velocity, pointerStart, contentStart, previousPosition, previousSize, previousViewSize;
    private Vector2 previousNormalized=new(float.NaN,float.NaN);
    private bool dragging, havePrevious, updatingBars;
    private ScrollRect? forwarding;
    private Scrollbar? boundHorizontal,boundVertical;
    private IDisposable? horizontalSubscription,verticalSubscription;
    private readonly UIEvent<Vector2> changed;
    private readonly DrivenRectTransformTracker tracker=new();
    private Vector2? viewportBaseDelta;
    public ScrollRect() { changed=new(this); }
    public UIEvent<Vector2> onValueChanged=>changed;
    public RectTransform? content {get=>ResolveUIReference(m_ContentRef);set{m_ContentRef=value==null?default:ComponentRef<RectTransform>.From(Owner,value);havePrevious=false;}}
    public RectTransform? viewport {get=>ResolveUIReference(m_ViewportRef);set{tracker.Clear();viewportBaseDelta=null;m_ViewportRef=value==null?default:ComponentRef<RectTransform>.From(Owner,value);}}
    public Scrollbar? horizontalScrollbar {get=>ResolveUIReference(m_HorizontalScrollbar);set{m_HorizontalScrollbar=value==null?default:ComponentRef<Scrollbar>.From(Owner,value);BindBars();}}
    public Scrollbar? verticalScrollbar {get=>ResolveUIReference(m_VerticalScrollbar);set{m_VerticalScrollbar=value==null?default:ComponentRef<Scrollbar>.From(Owner,value);BindBars();}}
    public ScrollbarVisibility horizontalScrollbarVisibility {get=>m_HorizontalScrollbarVisibility;set{m_HorizontalScrollbarVisibility=value;UpdateBars();}}
    public ScrollbarVisibility verticalScrollbarVisibility {get=>m_VerticalScrollbarVisibility;set{m_VerticalScrollbarVisibility=value;UpdateBars();}}
    public float horizontalScrollbarSpacing {get=>m_HorizontalScrollbarSpacing;set=>m_HorizontalScrollbarSpacing=CanvasScaler.Finite(value);}
    public float verticalScrollbarSpacing {get=>m_VerticalScrollbarSpacing;set=>m_VerticalScrollbarSpacing=CanvasScaler.Finite(value);}
    public bool horizontal {get=>m_Horizontal;set=>m_Horizontal=value;}
    public bool vertical {get=>m_Vertical;set=>m_Vertical=value;}
    public MovementType movementType {get=>m_MovementType;set=>m_MovementType=value;}
    public float elasticity {get=>m_Elasticity;set=>m_Elasticity=Math.Max(0.001f,CanvasScaler.Finite(value));}
    public bool inertia {get=>m_Inertia;set{m_Inertia=value;if(!value)StopMovement();}}
    public float decelerationRate {get=>m_DecelerationRate;set=>m_DecelerationRate=Math.Clamp(CanvasScaler.Finite(value),0,1);}
    public float scrollSensitivity {get=>m_ScrollSensitivity;set=>m_ScrollSensitivity=CanvasScaler.Finite(value);}
    public Vector2 velocity {get=>m_Velocity;set{m_Velocity=new(CanvasScaler.Finite(value.x),CanvasScaler.Finite(value.y));}}
    public bool isDragging=>dragging;
    private RectTransform View=>viewport??rectTransform;
    public Vector2 normalizedPosition {get=>new(horizontalNormalizedPosition,verticalNormalizedPosition);set{SetNormalizedPosition(value.x,0);SetNormalizedPosition(value.y,1);}}
    public float horizontalNormalizedPosition {get=>Normalized(0);set=>SetNormalizedPosition(value,0);}
    public float verticalNormalizedPosition {get=>Normalized(1);set=>SetNormalizedPosition(value,1);}
    public RectTransform transform=>rectTransform;
    public void Rebuild(CanvasUpdate stage) {if(stage==CanvasUpdate.PostLayout||stage==CanvasUpdate.PreRender)UpdateBars();}
    public void LayoutComplete(){} public void GraphicUpdateComplete(){}
    public void StopMovement()=>m_Velocity=Vector2.zero;
    private float Normalized(int axis) {
        var c=content;if(c==null)return 0;
        var view=Clipping.CanvasRect(View);var bounds=Clipping.CanvasRect(c);float hidden=bounds.size[axis]-view.size[axis];
        return hidden>0.001f?Math.Clamp((view.min[axis]-bounds.min[axis])/hidden,0,1):0;
    }
    private void SetNormalizedPosition(float value,int axis) {
        value=Math.Clamp(CanvasScaler.Finite(value),0,1);var c=content;if(c==null)return;
        var view=Clipping.CanvasRect(View);var bounds=Clipping.CanvasRect(c);float hidden=bounds.size[axis]-view.size[axis];
        if(hidden<=0.001f)return;
        var pos=c.anchoredPosition;pos[axis]+=view.min[axis]-bounds.min[axis]-value*hidden;c.anchoredPosition=pos;m_Velocity[axis]=0;UpdateBars();
    }
    private Vector2 Offset(Vector2 delta) {
        if(content is not RectTransform c||movementType==MovementType.Unrestricted)return Vector2.zero;
        var view=Clipping.CanvasRect(View);var bounds=Clipping.CanvasRect(c);var min=bounds.min+delta;var size=bounds.size;
        Vector2 result=Vector2.zero;
        for(int axis=0;axis<2;++axis){
            if(axis==0&&!horizontal||axis==1&&!vertical)continue;
            if(size[axis]<view.size[axis]){min[axis]-=(view.size[axis]-size[axis])*c.pivot[axis];size[axis]=view.size[axis];}
            if(min[axis]>view.min[axis])result[axis]=view.min[axis]-min[axis];
            else if(min[axis]+size[axis]<view.max[axis])result[axis]=view.max[axis]-min[axis]-size[axis];
        }
        return result;
    }
    public void OnInitializePotentialDrag(PointerEventData e){if(e.button==PointerEventData.InputButton.Left)StopMovement();}
    public void OnBeginDrag(PointerEventData e) {
        if(e.button!=PointerEventData.InputButton.Left||!IsActive()||content is not RectTransform c)return;
        bool x=Math.Abs(e.delta.x)>Math.Abs(e.delta.y);
        if(x&&!horizontal||!x&&!vertical){
            for(Actor? a=Owner.Parent;a!=null;a=a.Parent)if(a.GetComponent<ScrollRect>() is ScrollRect parent&&parent.IsActive()){
                forwarding=parent;parent.OnInitializePotentialDrag(e);parent.OnBeginDrag(e);return;
            }
        }
        if(!RectTransformUtility.ScreenPointToLocalPointInRectangle(View,e.pressPosition,null,out pointerStart))return;
        contentStart=c.anchoredPosition;previousPosition=contentStart;havePrevious=true;dragging=true;
    }
    public void OnDrag(PointerEventData e) {
        if(forwarding!=null){forwarding.OnDrag(e);return;}
        if(!dragging||!IsActive()||content is not RectTransform c)return;
        if(!RectTransformUtility.ScreenPointToLocalPointInRectangle(View,e.position,null,out var pointer))return;
        var delta=pointer-pointerStart;var position=contentStart+new Vector2(horizontal?delta.x:0,vertical?delta.y:0);
        var offset=Offset(position-c.anchoredPosition);position+=offset;
        if(movementType==MovementType.Elastic)for(int axis=0;axis<2;++axis){
            float length=Math.Max(1,View.rect.size[axis]);position[axis]-=(1-1/(Math.Abs(offset[axis])*0.55f/length+1))*length*Math.Sign(offset[axis]);
        }
        c.anchoredPosition=position;UpdateBars();
    }
    public void OnEndDrag(PointerEventData e){if(e.button!=PointerEventData.InputButton.Left)return;forwarding?.OnEndDrag(e);forwarding=null;dragging=false;}
    public void OnScroll(PointerEventData e) {
        if(!IsActive()||content is not RectTransform c)return;
        var delta=e.scrollDelta;delta.y=-delta.y;
        if(vertical&&!horizontal){if(Math.Abs(delta.x)>Math.Abs(delta.y))delta.y=delta.x;delta.x=0;}
        if(horizontal&&!vertical){if(Math.Abs(delta.y)>Math.Abs(delta.x))delta.x=delta.y;delta.y=0;}
        delta=new(horizontal?delta.x*scrollSensitivity:0,vertical?delta.y*scrollSensitivity:0);
        if(movementType==MovementType.Clamped)delta+=Offset(delta);
        if(delta==Vector2.zero){for(Actor? a=Owner.Parent;a!=null;a=a.Parent)if(a.GetComponent<ScrollRect>() is ScrollRect parent){parent.OnScroll(e);return;}}
        c.anchoredPosition+=delta;UpdateBars();e.Use();
    }
    public override void OnLateUpdate(float dt) {
        base.OnLateUpdate(dt);if(!IsActive()||content is not RectTransform c){StopMovement();dragging=false;return;}
        BindBars();dt=Math.Clamp(dt,0,0.25f);var position=c.anchoredPosition;
        if(dragging&&inertia&&havePrevious&&dt>0){var measured=(position-previousPosition)/dt;m_Velocity+= (measured-m_Velocity)*Math.Min(1,dt*10);}
        if(!dragging&&dt>0){
            float remaining=dt;
            while(remaining>0){float step=Math.Min(remaining,1f/60);remaining-=step;var offset=Offset(Vector2.zero);position=c.anchoredPosition;
                for(int axis=0;axis<2;++axis){if(axis==0&&!horizontal||axis==1&&!vertical){m_Velocity[axis]=0;continue;}
                    if(movementType==MovementType.Elastic&&Math.Abs(offset[axis])>0.001f){
                        float omega=2/Math.Max(0.001f,elasticity),x=omega*step,decay=1/(1+x+0.48f*x*x+0.235f*x*x*x);
                        float difference=-offset[axis],temp=(m_Velocity[axis]+omega*difference)*step;
                        m_Velocity[axis]=(m_Velocity[axis]-omega*temp)*decay;position[axis]=position[axis]+offset[axis]+(difference+temp)*decay;
                    }else if(inertia){m_Velocity[axis]*=MathF.Pow(decelerationRate,step);if(Math.Abs(m_Velocity[axis])<1)m_Velocity[axis]=0;position[axis]+=m_Velocity[axis]*step;}
                    else m_Velocity[axis]=0;
                }
                if(movementType==MovementType.Clamped)position+=Offset(position-c.anchoredPosition);
                c.anchoredPosition=position;
            }
        }
        previousPosition=c.anchoredPosition;havePrevious=true;UpdateBars();
    }
    private void BindBars(){
        var h=horizontalScrollbar;var v=verticalScrollbar;
        if(!ReferenceEquals(h,boundHorizontal)){horizontalSubscription?.Dispose();boundHorizontal=h;horizontalSubscription=h?.onValueChanged.Subscribe(HorizontalChanged);}
        if(!ReferenceEquals(v,boundVertical)){verticalSubscription?.Dispose();boundVertical=v;verticalSubscription=v?.onValueChanged.Subscribe(VerticalChanged);}
    }
    private void HorizontalChanged(float value){if(!updatingBars)horizontalNormalizedPosition=value;}
    private void VerticalChanged(float value){if(!updatingBars)verticalNormalizedPosition=value;}
    private void UpdateBars(){
        if(owner==null||content is not RectTransform c||updatingBars)return;updatingBars=true;
        try{
            var size=c.rect.size;var view=View.rect.size;var offset=Offset(Vector2.zero);var normalized=normalizedPosition;
            var h=horizontalScrollbar;var v=verticalScrollbar;
            bool showH=horizontal&&size.x>view.x+0.01f,showV=vertical&&size.y>view.y+0.01f;
            if(h!=null){h.size=size.x<=0?1:Math.Clamp((view.x-Math.Abs(offset.x))/size.x,0,1);h.SetValueWithoutNotify(normalized.x);if(horizontalScrollbarVisibility!=ScrollbarVisibility.Permanent&&h.Owner.Active!=showH)h.Owner.Active=showH;}
            if(v!=null){v.size=size.y<=0?1:Math.Clamp((view.y-Math.Abs(offset.y))/size.y,0,1);v.SetValueWithoutNotify(normalized.y);if(verticalScrollbarVisibility!=ScrollbarVisibility.Permanent&&v.Owner.Active!=showV)v.Owner.Active=showV;}
            if(viewport is RectTransform vp&&(horizontalScrollbarVisibility==ScrollbarVisibility.AutoHideAndExpandViewport||verticalScrollbarVisibility==ScrollbarVisibility.AutoHideAndExpandViewport)){
                viewportBaseDelta??=vp.sizeDelta;tracker.Add(this,vp,DrivenTransformProperties.SizeDelta);
                var delta=viewportBaseDelta.Value;
                if(h!=null&&!showH&&horizontalScrollbarVisibility==ScrollbarVisibility.AutoHideAndExpandViewport)delta.y+=h.rectTransform.rect.height+horizontalScrollbarSpacing;
                if(v!=null&&!showV&&verticalScrollbarVisibility==ScrollbarVisibility.AutoHideAndExpandViewport)delta.x+=v.rectTransform.rect.width+verticalScrollbarSpacing;
                if(vp.sizeDelta!=delta)vp.sizeDelta=delta;
            }
            if(normalized!=previousNormalized||size!=previousSize||view!=previousViewSize){previousNormalized=normalized;previousSize=size;previousViewSize=view;changed.Invoke(normalized);}
        }finally{updatingBars=false;}
    }
    public override void OnEnable(){base.OnEnable();BindBars();havePrevious=false;}
    public override void OnDisable(){dragging=false;forwarding=null;StopMovement();horizontalSubscription?.Dispose();verticalSubscription?.Dispose();horizontalSubscription=verticalSubscription=null;boundHorizontal=boundVertical=null;tracker.Clear();viewportBaseDelta=null;base.OnDisable();}
    internal override void ReleaseNativeComponents(){OnDisable();base.ReleaseNativeComponents();}
    protected override void SetDirty(){base.SetDirty();if(IsActive())UpdateBars();}
}
