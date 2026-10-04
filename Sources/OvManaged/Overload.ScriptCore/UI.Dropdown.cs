using System;
using System.Collections.Generic;
using System.Linq;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public class Dropdown : Selectable, IPointerClickHandler, ISubmitHandler, ICancelHandler
{
    [Serializable]
    public class OptionData
    {
        public string text = "";
        public AssetRef image;
        public OptionData() { }
        public OptionData(string text) { this.text = text; }
    }
    [Serializable]
    public class OptionDataList
    {
        public List<OptionData> options = new();
    }

    [SerializeField, FieldId("m_Template")] private ComponentRef<RectTransform> m_TemplateRef;
    private RectTransform? m_Template { get => ResolveUIReference(m_TemplateRef); set => m_TemplateRef = value == null ? default : ComponentRef<RectTransform>.From(Owner, value); }
    [SerializeField, FieldId("m_CaptionText")] private ComponentRef<Text> m_CaptionTextRef;
    private Text? m_CaptionText { get => ResolveUIReference(m_CaptionTextRef); set => m_CaptionTextRef = value == null ? default : ComponentRef<Text>.From(Owner, value); }
    [SerializeField, FieldId("m_CaptionImage")] private ComponentRef<Image> m_CaptionImageRef;
    private Image? m_CaptionImage { get => ResolveUIReference(m_CaptionImageRef); set => m_CaptionImageRef = value == null ? default : ComponentRef<Image>.From(Owner, value); }
    [SerializeField] private OptionDataList m_Options = new();
    [SerializeField] private int m_Value;

    private readonly UIEvent<int> m_OnValueChanged;
    public Dropdown(){m_OnValueChanged=new(this);}
    private Actor? popup;
    private int shownOptionsHash;
    private int OptionsHash(){var hash=new HashCode();foreach(var option in m_Options.options){hash.Add(option.text);hash.Add(option.image);}return hash.ToHashCode();}
    private readonly List<Actor> m_Items = new();
    private bool m_IsOpen;

    public UIEvent<int> onValueChanged => m_OnValueChanged;
    public RectTransform? template { get => m_Template; set => m_Template = value; }
    public Text? captionText { get => m_CaptionText; set { m_CaptionText = value; RefreshShownValue(); } }
    public Image? captionImage { get => m_CaptionImage; set { m_CaptionImage = value; RefreshShownValue(); } }
    public OptionDataList options {
        get => m_Options;
        set { m_Options = value ?? new OptionDataList(); RefreshShownValue(); }
    }
    public int value {
        get => m_Value;
        set => Set(value, true);
    }
    public bool isExpanded => m_IsOpen;
    public int itemCount => m_Options.options.Count;
    public void SetValueWithoutNotify(int input) => Set(input, false);
    public void AddOption(string text) {
        m_Options.options.Add(new OptionData(text));
        RefreshShownValue();
    }
    public void ClearOptions() {
        m_Options.options.Clear();
        m_Value = 0;
        RefreshShownValue();
    }

    public virtual void RefreshShownValue() {
        m_Options??=new();m_Options.options??=new();
        m_Value=Math.Clamp(m_Value,0,Math.Max(0,itemCount-1));
        var option=itemCount>0?m_Options.options[m_Value]:null;
        if(m_CaptionText!=null)m_CaptionText.text=option?.text??"";
        if(m_CaptionImage!=null){m_CaptionImage.sprite=option?.image??default;m_CaptionImage.enabled=option!=null&&!string.IsNullOrEmpty(option.image.Path);}
    }
    private void Set(int input, bool sendCallback) {
        int v = Math.Clamp(input, 0, Math.Max(0, m_Options.options.Count - 1));
        if (m_Value == v) return;
        m_Value = v;
        RefreshShownValue();
        if (sendCallback) m_OnValueChanged.Invoke(m_Value);
    }

    public virtual void Show() {
        if(m_IsOpen||!IsActive()||!IsInteractable()||itemCount==0)return;
        var canvas=Owner.GetComponentInParentCanvas();if(canvas==null)return;
        try{
            popup=DefaultControls.Element(canvas.Owner,"Dropdown Popup",0,0);DefaultControls.Stretch(popup.GetComponent<RectTransform>()!);
            var popupCanvas=popup.AddComponent<Canvas>();popupCanvas.renderMode=canvas.renderMode;popupCanvas.worldCamera=canvas.worldCamera;popupCanvas.scaleFactor=canvas.scaleFactor;popupCanvas.overrideSorting=true;popupCanvas.sortingOrder=canvas.sortingOrder+100;
            popup.AddUIBehaviour<GraphicRaycaster>();
            var blocker=popup.AddUIBehaviour<DropdownBlocker>();blocker.dropdown=this;blocker.targetGraphic=DefaultControls.Background(popup,default,new(0,0,0,0.001f));
            Actor list=template!=null?template.Owner.Clone(popup):DefaultControls.CreateScrollView(popup,default);list.Active=true;
            var scroll=list.GetComponentsInChildren<ScrollRect>(true).FirstOrDefault();
            if(scroll==null||scroll.content==null)throw new InvalidOperationException("Dropdown template requires a ScrollRect with content");
            var itemParent=scroll.content.Owner;
            Actor? prototype=template==null?null:itemParent.ChildCount>0?itemParent.GetChild(0):null;
            if(template!=null&&prototype==null)throw new InvalidOperationException("Dropdown template requires an item under content");
            float rowHeight=prototype?.GetComponent<RectTransform>()?.rect.height??30;rowHeight=Math.Max(rowHeight,20);
            float width=Math.Max(80,rectTransform.rect.width),height=Math.Min(180,rowHeight*itemCount);
            var listRect=list.GetComponent<RectTransform>()!;listRect.anchorMin=listRect.anchorMax=Vector2.zero;listRect.pivot=new(0,1);listRect.sizeDelta=new(width,height);
            var buttonBounds=Clipping.CanvasRect(rectTransform);var canvasBounds=Clipping.CanvasRect(popup.GetComponent<RectTransform>()!);
            float x=Math.Clamp(buttonBounds.min.x-canvasBounds.min.x,0,Math.Max(0,canvasBounds.width-width));
            float y=buttonBounds.min.y-canvasBounds.min.y;
            if(y<height)y=buttonBounds.max.y-canvasBounds.min.y+height;
            y=Math.Clamp(y,height,Math.Max(height,canvasBounds.height));listRect.anchoredPosition=new(x,y);
            scroll.horizontal=false;scroll.vertical=true;scroll.movementType=ScrollRect.MovementType.Clamped;
            if(scroll.horizontalScrollbar!=null)scroll.horizontalScrollbar.Owner.Active=false;
            if(scroll.viewport!=null)DefaultControls.Stretch(scroll.viewport,0,0,16,0);
            scroll.content.sizeDelta=new(0,rowHeight*itemCount);
            if(prototype!=null)prototype.Active=false;
            for(int i=0;i<itemCount;++i){
                var item=prototype?.Clone(itemParent)??DefaultControls.CreateButton(itemParent,default);item.Active=true;
                var rect=item.GetComponent<RectTransform>()!;rect.anchorMin=new(0,1);rect.anchorMax=new(1,1);rect.pivot=new(0.5f,1);rect.sizeDelta=new(0,rowHeight);rect.anchoredPosition=new(0,-i*rowHeight);
                var label=item.GetComponentsInChildren<Text>(true).FirstOrDefault()??DefaultControls.Label(item,"Label","");label.text=m_Options.options[i].text;label.raycastTarget=false;
                var option=item.AddUIBehaviour<DropdownOption>();option.dropdown=this;option.index=i;option.selectOnEnable=i==m_Value;
                var selectable=item.GetComponent<Selectable>();if(selectable is Toggle toggle)toggle.SetIsOnWithoutNotify(i==m_Value);
                m_Items.Add(item);
            }
            shownOptionsHash=OptionsHash();m_IsOpen=true;
        }catch{Hide();throw;}
    }
    public virtual void Hide() {
        bool wasOpen=m_IsOpen;m_IsOpen=false;
        if(popup!=null&&popup.IsAlive)popup.Destroy();popup=null;m_Items.Clear();
        if(wasOpen&&IsActive())Select();
    }
    public virtual void OnSubmit(BaseEventData e){Show();e.Use();}
    public virtual void OnCancel(BaseEventData e){Hide();e.Use();}
    public override void OnDisable(){Hide();base.OnDisable();}
    internal override void ReleaseNativeComponents(){Hide();base.ReleaseNativeComponents();}
    public override void OnLateUpdate(float dt){base.OnLateUpdate(dt);RefreshShownValue();if(m_IsOpen&&(popup==null||!popup.IsAlive||OptionsHash()!=shownOptionsHash||m_TemplateRef.ActorGuid!=0&&template==null)){Hide();}}
    protected override void SetDirty(){base.SetDirty();RefreshShownValue();}
    public virtual void OnPointerClick(PointerEventData eventData) {
        if (!IsInteractable() || eventData.button != PointerEventData.InputButton.Left) return;
        if (m_IsOpen) Hide(); else Show();
    }
    public void SelectOption(int index) {
        value = index;
        Hide();
    }
}


public class DropdownOption : Overload.EventSystems.UIBehaviour, IPointerClickHandler, ISubmitHandler, ICancelHandler
{
    [SerializeField] private ComponentRef<Dropdown> m_Dropdown;
    [SerializeField] public int index;
    [NonSerialized] public bool selectOnEnable;
    public Dropdown? dropdown {get=>ResolveUIReference(m_Dropdown);set=>m_Dropdown=value==null?default:ComponentRef<Dropdown>.From(Owner,value);}
    public override void OnEnable(){base.OnEnable();if(selectOnEnable)EventSystem.GetCurrent(Owner)?.SetSelectedObject(Owner);}
    public void OnPointerClick(PointerEventData e){if(e.button==PointerEventData.InputButton.Left)dropdown?.SelectOption(index);}
    public void OnSubmit(BaseEventData e){dropdown?.SelectOption(index);e.Use();}
    public void OnCancel(BaseEventData e){dropdown?.Hide();e.Use();}
}
public class DropdownBlocker : Button
{
    [SerializeField] private ComponentRef<Dropdown> m_Dropdown;
    public Dropdown? dropdown {get=>ResolveUIReference(m_Dropdown);set=>m_Dropdown=value==null?default:ComponentRef<Dropdown>.From(Owner,value);}
    public override void OnPointerClick(PointerEventData e){if(e.button==PointerEventData.InputButton.Left)dropdown?.Hide();}
}
