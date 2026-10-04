using Overload.Maths;
namespace Overload.UI;

/// <summary>Ready-to-use UI hierarchies. Call on the main thread beneath a Canvas.</summary>
public static class DefaultControls
{
    public struct Resources { public AssetRef standard, background, inputField, knob, checkmark, dropdown, mask; }
    internal static Actor Element(Actor parent, string name, float width, float height) {
        var actor = parent.CreateChild(name);
        var rect = actor.AddComponent<RectTransform>(); rect.sizeDelta = new(width,height);
        return actor;
    }
    internal static void Stretch(RectTransform rect, float left=0, float bottom=0, float right=0, float top=0) {
        rect.anchorMin=Vector2.zero; rect.anchorMax=Vector2.one;
        rect.offsetMin=new(left,bottom); rect.offsetMax=new(-right,-top);
    }
    internal static Image Background(Actor actor, AssetRef texture, Color color) {
        var image=actor.AddUIBehaviour<Image>(); image.color=color;
        if (!string.IsNullOrEmpty(texture.Path)) image.sprite=texture;
        return image;
    }
    internal static Text Label(Actor parent, string name, string text) {
        var actor=Element(parent,name,0,0); Stretch(actor.GetComponent<RectTransform>()!,6,2,6,2);
        var label=actor.AddUIBehaviour<Text>();label.text=text;label.alignment=TextAnchor.MiddleCenter;
        label.color=new(0.12f,0.12f,0.12f,1);label.raycastTarget=false;label.supportRichText=false;
        return label;
    }
    public static Actor CreatePanel(Actor parent, Resources res) {
        var actor=Element(parent,"Panel",300,200);Background(actor,res.background,new(0.2f,0.2f,0.2f,0.85f));return actor;
    }
    public static Actor CreateButton(Actor parent, Resources res) {
        var actor=Element(parent,"Button",160,30);var image=Background(actor,res.standard,new(1,1,1,1));
        actor.AddUIBehaviour<Button>().targetGraphic=image;Label(actor,"Text","Button");return actor;
    }
    public static Actor CreateText(Actor parent, Resources res) {
        var actor=Element(parent,"Text",160,30);var text=actor.AddUIBehaviour<Text>();text.text="Text";text.raycastTarget=false;return actor;
    }
    public static Actor CreateImage(Actor parent, Resources res) {
        var actor=Element(parent,"Image",100,100);Background(actor,res.standard,new(1,1,1,1));return actor;
    }
    public static Actor CreateRawImage(Actor parent, Resources res) {
        var actor=Element(parent,"RawImage",100,100);actor.AddUIBehaviour<RawImage>().texture=res.standard;return actor;
    }
    public static Actor CreateSlider(Actor parent, Resources res) {
        var actor=Element(parent,"Slider",160,20);Background(actor,res.background,new(0.2f,0.2f,0.2f,1));
        var fill=Element(actor,"Fill",0,0);Stretch(fill.GetComponent<RectTransform>()!);Background(fill,res.standard,new(0.3f,0.65f,1,1)).raycastTarget=false;
        var handle=Element(actor,"Handle",16,24);var knob=Background(handle,res.knob,new(1,1,1,1));knob.raycastTarget=false;
        var slider=actor.AddUIBehaviour<Slider>();slider.targetGraphic=knob;slider.fillRect=fill.GetComponent<RectTransform>();slider.handleRect=handle.GetComponent<RectTransform>();slider.value=0.5f;return actor;
    }
    public static Actor CreateScrollbar(Actor parent, Resources res) {
        var actor=Element(parent,"Scrollbar",160,20);Background(actor,res.background,new(0.2f,0.2f,0.2f,1));
        var handle=Element(actor,"Handle",0,0);var image=Background(handle,res.knob,new(1,1,1,1));image.raycastTarget=false;
        var bar=actor.AddUIBehaviour<Scrollbar>();bar.targetGraphic=image;bar.handleRect=handle.GetComponent<RectTransform>();return actor;
    }
    public static Actor CreateToggle(Actor parent, Resources res) {
        var actor=Element(parent,"Toggle",160,24);Background(actor,default,new(1,1,1,0));
        var background=Element(actor,"Background",20,20);var rect=background.GetComponent<RectTransform>()!;rect.anchorMin=rect.anchorMax=new(0,0.5f);rect.anchoredPosition=new(10,0);
        var image=Background(background,res.standard,new(1,1,1,1));image.raycastTarget=false;
        var check=Element(background,"Checkmark",12,12);var graphic=Background(check,res.checkmark,new(0.15f,0.6f,0.25f,1));graphic.raycastTarget=false;
        var toggle=actor.AddUIBehaviour<Toggle>();toggle.targetGraphic=image;toggle.graphic=graphic;
        var label=Label(actor,"Label","Toggle");Stretch(label.rectTransform,26,0,0,0);toggle.isOn=true;return actor;
    }
    public static Actor CreateInputField(Actor parent, Resources res) {
        var actor=Element(parent,"InputField",180,30);var image=Background(actor,res.inputField,new(1,1,1,1));actor.AddUIBehaviour<RectMask2D>();
        var text=Label(actor,"Text","");text.alignment=TextAnchor.MiddleLeft;
        var placeholder=Label(actor,"Placeholder","Enter text…");placeholder.alignment=TextAnchor.MiddleLeft;placeholder.color=new(0.5f,0.5f,0.5f,1);
        var field=actor.AddUIBehaviour<InputField>();field.targetGraphic=image;field.textComponent=text;field.placeholder=placeholder;return actor;
    }
    public static Actor CreateDropdown(Actor parent, Resources res) {
        var actor=Element(parent,"Dropdown",160,30);var image=Background(actor,res.standard,new(1,1,1,1));
        var dropdown=actor.AddUIBehaviour<Dropdown>();dropdown.targetGraphic=image;dropdown.captionText=Label(actor,"Label","");
        dropdown.AddOption("Option A");dropdown.AddOption("Option B");return actor;
    }
    public static Actor CreateScrollView(Actor parent, Resources res) {
        var actor=Element(parent,"Scroll View",200,200);Background(actor,res.background,new(0.18f,0.18f,0.18f,1));
        var viewport=Element(actor,"Viewport",0,0);Stretch(viewport.GetComponent<RectTransform>()!,0,20,20,0);viewport.AddUIBehaviour<RectMask2D>();
        var content=Element(viewport,"Content",0,300);var contentRect=content.GetComponent<RectTransform>()!;
        contentRect.anchorMin=new(0,1);contentRect.anchorMax=new(1,1);contentRect.pivot=new(0,1);contentRect.anchoredPosition=Vector2.zero;
        var horizontal=CreateScrollbar(actor,res);var hr=horizontal.GetComponent<RectTransform>()!;hr.anchorMin=new(0,0);hr.anchorMax=new(1,0);hr.pivot=new(0.5f,0);hr.sizeDelta=new(-20,20);hr.anchoredPosition=new(-10,0);
        var vertical=CreateScrollbar(actor,res);var vr=vertical.GetComponent<RectTransform>()!;vr.anchorMin=new(1,0);vr.anchorMax=new(1,1);vr.pivot=new(1,0.5f);vr.sizeDelta=new(20,-20);vr.anchoredPosition=new(0,10);
        var scroll=actor.AddUIBehaviour<ScrollRect>();scroll.content=contentRect;scroll.viewport=viewport.GetComponent<RectTransform>();
        scroll.horizontalScrollbar=horizontal.GetComponent<Scrollbar>();scroll.verticalScrollbar=vertical.GetComponent<Scrollbar>();scroll.verticalScrollbar!.direction=Scrollbar.Direction.BottomToTop;
        scroll.horizontal=false;scroll.vertical=true;return actor;
    }
}
