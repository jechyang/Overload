using System;
using System.Linq;
using Overload.Maths;
using Overload;
using Overload.EventSystems;
using Overload.UI;

[assembly: EnableUI]

namespace InputFixture;

public class EditBox : InputField
{
    [SerializeField] public string seedText = "Hello";
    [SerializeField] public int seedsApplied;
    [SerializeField] public int keyboardSupported = -1;
    [SerializeField] public int keyboardStatus = -1;
    [SerializeField] public string keyboardText = "";
    [SerializeField] public int keyboardProbeRan;
    [SerializeField] public int keyboardProbeRequest;
    public override void OnEnable()
    {
        base.OnEnable();
        if (seedsApplied == 0) {
            text = seedText + seedText;
            seedsApplied = 1;
        }
    }
    public override void OnUpdate(float dt)
    {
        if (keyboardProbeRequest != 0 && keyboardProbeRan == 0) {
            ProbeSoftKeyboard("!");
            keyboardProbeRan = 1;
        }
    }
    public void TypeText(string s) => Insert(s);
    public void ProbeSoftKeyboard(string inject)
    {
        keyboardSupported = TouchScreenKeyboard.isSupported ? 1 : 0;
        ActivateInputField();
        var keyboard = touchScreenKeyboard;
        if (keyboard == null) {
            keyboardStatus = -2;
            return;
        }
        keyboard.Insert(inject);
        text = keyboard.text;
        keyboard.Done();
        keyboardStatus = (int)keyboard.status;
        keyboardText = keyboard.text;
    }
}

public class ChoiceBox : Dropdown
{
    public ChoiceBox()
    {
        AddOption("Alpha");
        AddOption("Beta");
        AddOption("Gamma");
    }
}

public class FactoryProbe : Behaviour
{
    public int created;
    public int checks;
    public string failure = "";
    public int clicks;
    private Actor? buttonActor;
    private InputField? editField;
    private Dropdown? dropdown;
    private ScrollRect? scroll;
    private int phase;
    private void Check(bool value,string label) { if (!value) throw new Exception(label); ++checks; }
    public override void OnUpdate(float dt)
    {
        if (created < 0 || phase >= 2) return;
        try {
            if (phase == 0) {
                buttonActor=DefaultControls.CreateButton(Owner,default);
                var button=buttonActor.GetComponent<Button>()!;
                button.onClick.Subscribe(()=>++clicks);
                DefaultControls.CreateText(Owner,default);
                editField=DefaultControls.CreateInputField(Owner,default).GetComponent<InputField>()!;
                dropdown=DefaultControls.CreateDropdown(Owner,default).GetComponent<Dropdown>()!;
                scroll=DefaultControls.CreateScrollView(Owner,default).GetComponent<ScrollRect>()!;
                Check(buttonActor.Parent?.Guid==Owner.Guid,"factory parent");
                Check(button.targetGraphic!=null&&buttonActor.GetComponentsInChildren<Text>().Length==1,"button references and label");
                Check(editField.textComponent!=null&&editField.placeholder!=null,"input references");
                Check(scroll.content?.parent?.Owner.Guid==scroll.viewport?.Owner.Guid,"scroll content hierarchy");
                editField.lineType=InputField.LineType.MultiLineNewline;editField.Insert("a\nb");
                Check(editField.text=="a\nb","multiline retains newline");
                editField.text="";editField.characterLimit=1;editField.Insert("😀z");
                Check(editField.text=="😀","Unicode character limit");
                editField.characterLimit=0;editField.text="1234";editField.inputType=InputField.InputType.Pin;
                Check(editField.textComponent!.text=="****","PIN masks display");
                var placeholder=editField.placeholder!.text;editField.text="x";editField.text="";
                Check(editField.placeholder.text==placeholder&&editField.placeholder.enabled,"placeholder survives edits");
                editField.ActivateInputField();var keyboard=editField.touchScreenKeyboard!;
                editField.OnDeselect(new BaseEventData(EventSystem.GetCurrent(Owner)!));
                Check(!editField.isFocused&&!keyboard.active,"deselection closes keyboard");
                editField.ActivateInputField();keyboard=editField.touchScreenKeyboard!;editField.enabled=false;
                Check(!editField.isFocused&&!keyboard.active,"disable closes keyboard");editField.enabled=true;
                var soft=TouchScreenKeyboard.Open("😀",characterLimit:1);soft.Backspace();
                Check(soft.text=="","soft keyboard deletes whole grapheme");soft.Insert("😀z");Check(soft.text=="😀","soft keyboard limit");soft.Destroy();
                var slider=DefaultControls.CreateSlider(Owner,default).GetComponent<Slider>()!;
                slider.direction=Slider.Direction.RightToLeft;slider.value=0.25f;
                Check(Math.Abs(slider.fillRect!.anchorMin.x-0.75f)<0.001f&&slider.fillRect.anchorMax.x==1,"reverse slider fill");
                var bar=DefaultControls.CreateScrollbar(Owner,default).GetComponent<Scrollbar>()!;bar.numberOfSteps=3;bar.value=0.3f;
                Check(bar.value==0.5f,"scrollbar quantization");
                var group=Owner.AddUIBehaviour<ToggleGroup>();
                var first=DefaultControls.CreateToggle(Owner,default).GetComponent<Toggle>()!;
                var second=DefaultControls.CreateToggle(Owner,default).GetComponent<Toggle>()!;
                first.group=group;second.group=group;second.isOn=false;
                Check(second.isOn&&!first.isOn,"toggle group retains last selection");
                first.isOn=true;Check(first.isOn&&!second.isOn,"toggle group switches exclusively");
                created=5;phase=1;return;
            }
            dropdown!.Show();Check(dropdown.isExpanded,"dropdown opens");
            Check(Owner.Parent!.GetComponentsInChildren<DropdownOption>().Length==2,"dropdown creates option handlers");
            dropdown.SelectOption(1);Check(dropdown.value==1&&!dropdown.isExpanded&&dropdown.captionText!.text=="Option B","dropdown selection and caption");
            for(int i=0;i<3;++i){dropdown.Show();dropdown.Hide();}
            Check(!dropdown.isExpanded,"dropdown repeated close");
            var sourceText=buttonActor!.GetComponentsInChildren<Text>().Single();
            buttonActor.AddUIBehaviour<ReferenceProbe>().links=new ReferenceLinks { texts=new[]{ComponentRef<Text>.From(buttonActor,sourceText)} };
            var clone=buttonActor.Clone(Owner);var cloneButton=clone.GetComponent<Button>()!;
            Check(cloneButton.targetGraphic?.Owner.Guid==clone.Guid,"clone remaps target graphic");
            Check(clone.GetComponentsInChildren<Text>().Single().text=="Button","clone preserves live child text");
            var clonedText=clone.GetComponent<ReferenceProbe>()!.links.texts[0].Resolve(clone);
            Check(clonedText!=null&&clonedText.Owner.Guid!=sourceText.Owner.Guid&&clonedText.Owner.Parent!.Guid==clone.Guid,"clone remaps references nested in records and arrays");clone.Destroy();
            var dropdownClone=dropdown.Owner.Clone(Owner);var clonedDropdown=dropdownClone.GetComponent<Dropdown>()!;
            Check(clonedDropdown.itemCount==2&&clonedDropdown.value==1&&clonedDropdown.captionText!.Owner.Parent!.Guid==dropdownClone.Guid,"dropdown clone preserves options and remaps caption");dropdownClone.Destroy();
            scroll!.verticalNormalizedPosition=0;Check(Math.Abs(scroll.verticalNormalizedPosition)<0.001f,"scroll normalized lower bound");
            scroll.verticalNormalizedPosition=1;Check(Math.Abs(scroll.verticalNormalizedPosition-1)<0.001f,"scroll normalized upper bound");
            phase=2;
        } catch (Exception e) {
            failure=e.ToString();Owner.Log(failure);created = -1;
        }
    }
}

[Serializable]
public class ReferenceLinks { public ComponentRef<Text>[] texts=Array.Empty<ComponentRef<Text>>(); }
public class ReferenceProbe : UIBehaviour { public ReferenceLinks links=new(); }
