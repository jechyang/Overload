using Overload;
using Overload.EventSystems;
using Overload.Maths;
using Overload.UI;

namespace UIGallery;

// Open Scenes/Controls.ovscene and press Play. All controls use production factories.
public class ControlsGallery : Behaviour
{
    private Actor? panel;
    private Text? status;
    private int clicks;
    private static void Place(Actor actor,float x,float y,float width,float height) {
        var rect=actor.GetComponent<RectTransform>()!;
        rect.anchorMin=rect.anchorMax=Vector2.zero;
        rect.anchoredPosition=new(x,y);rect.sizeDelta=new(width,height);
    }
    private Text Label(Actor parent,string value,float x,float y,float width=260) {
        var actor=DefaultControls.CreateText(parent,default);Place(actor,x,y,width,28);
        var text=actor.GetComponent<Text>()!;text.text=value;text.fontSize=17;
        text.alignment=TextAnchor.MiddleLeft;text.color=Color.white;return text;
    }
    public override void OnStart() => CreateControls();
    public override void OnReloadRestore() => CreateControls();
    private void CreateControls() {
        panel=Owner.CreateChild("Controls Gallery");
        var rect=panel.AddComponent<RectTransform>();rect.anchorMin=Vector2.zero;rect.anchorMax=Vector2.one;rect.sizeDelta=Vector2.zero;
        Owner.AddUIBehaviour<GraphicRaycaster>();
        var events=Owner.Find("EventSystem")!;events.AddUIBehaviour<BaseInput>();
        Label(panel,"Interactive uGUI controls",220,500,380);
        status=Label(panel,"Ready — click, type, drag or scroll",400,35,700);
        var button=DefaultControls.CreateButton(panel,default);Place(button,180,435,260,36);
        button.GetComponent<Button>()!.onClick.Subscribe(()=>status!.text=$"Button clicked {++clicks} times");
        var toggle=DefaultControls.CreateToggle(panel,default);Place(toggle,180,378,260,28);
        toggle.GetComponentsInChildren<Text>()[0].color=Color.white;
        toggle.GetComponent<Toggle>()!.onValueChanged.Subscribe(on=>status!.text="Toggle: "+on);
        var slider=DefaultControls.CreateSlider(panel,default);Place(slider,180,320,260,24);
        slider.GetComponent<Slider>()!.onValueChanged.Subscribe(value=>status!.text=$"Slider: {value:F2}");
        var input=DefaultControls.CreateInputField(panel,default);Place(input,180,257,260,36);
        input.GetComponent<InputField>()!.onEndEdit.Subscribe(value=>status!.text="Entered: "+value);
        var dropdown=DefaultControls.CreateDropdown(panel,default);Place(dropdown,180,193,260,36);
        dropdown.GetComponent<Dropdown>()!.AddOption("Option C");
        dropdown.GetComponent<Dropdown>()!.onValueChanged.Subscribe(value=>status!.text="Option: "+(value+1));
        Label(panel,"Drag the list or use its scrollbar",665,465,400);
        var list=DefaultControls.CreateScrollView(panel,default);Place(list,665,270,430,340);
        var scroll=list.GetComponent<ScrollRect>()!;scroll.content!.sizeDelta=new(0,720);
        scroll.horizontalScrollbarVisibility=ScrollRect.ScrollbarVisibility.AutoHideAndExpandViewport;
        for(int i=0;i<18;++i) {
            var row=DefaultControls.CreateButton(scroll.content.Owner,default);
            var rowRect=row.GetComponent<RectTransform>()!;rowRect.anchorMin=new(0,1);rowRect.anchorMax=new(1,1);
            rowRect.pivot=new(0.5f,1);rowRect.sizeDelta=new(-8,36);rowRect.anchoredPosition=new(0,-i*40);
            row.GetComponentsInChildren<Text>()[0].text="List item "+(i+1);
            int item=i+1;row.GetComponent<Button>()!.onClick.Subscribe(()=>status!.text="Selected list item "+item);
        }
    }
    public override void OnReloadRelease() { if(panel is {IsAlive:true})panel.Destroy();panel=null; }
    public override void OnDestroy() { if(panel is {IsAlive:true})panel.Destroy();panel=null; }
}
