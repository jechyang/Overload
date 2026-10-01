using Overload;
using Overload.EventSystems;
using Overload.UI;

[assembly: EnableUI]

namespace InputFixture;

public class EditBox : InputField
{
    [SerializeField] public string seedText = "Hello";
    [SerializeField] public int seedsApplied;
    public override void OnEnable()
    {
        base.OnEnable();
        if (seedsApplied == 0) {
            text = seedText + seedText;
            seedsApplied = 1;
        }
    }
    public void TypeText(string s) => Insert(s);
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
