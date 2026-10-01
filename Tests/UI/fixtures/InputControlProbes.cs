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
    public override void OnUpdate(float dt)
    {
        if (created != 0) return;
        try {
            DefaultControls.CreateButton(Owner, default);
            DefaultControls.CreateText(Owner, default);
            DefaultControls.CreateInputField(Owner, default);
            DefaultControls.CreateDropdown(Owner, default);
            DefaultControls.CreateScrollView(Owner, default);
            created = 5;
        } catch {
            created = -1;
        }
    }
}
