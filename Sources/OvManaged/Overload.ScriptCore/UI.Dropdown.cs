using System;
using System.Collections.Generic;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public class Dropdown : Selectable, IPointerClickHandler
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

    [SerializeField] private RectTransform? m_Template;
    [SerializeField] private Text? m_CaptionText;
    [SerializeField] private Image? m_CaptionImage;
    [SerializeField] private OptionDataList m_Options = new();
    [SerializeField] private int m_Value;

    private readonly UIEvent<int> m_OnValueChanged = new();
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
        if (m_CaptionText == null) return;
        m_CaptionText.text = (m_Value >= 0 && m_Value < m_Options.options.Count) ? m_Options.options[m_Value].text : "";
    }
    private void Set(int input, bool sendCallback) {
        int v = Math.Clamp(input, 0, Math.Max(0, m_Options.options.Count - 1));
        if (m_Value == v) return;
        m_Value = v;
        RefreshShownValue();
        if (sendCallback) m_OnValueChanged.Invoke(m_Value);
    }

    /// <summary>Spawn option rows under the template parent using Actor.Create.</summary>
    public virtual void Show() {
        if (m_IsOpen) return;
        m_IsOpen = true;
        m_Items.Clear();
        var parent = m_Template?.Owner.Parent ?? Owner;
        for (int i = 0; i < m_Options.options.Count; ++i) {
            var item = parent.Create("Option " + i);
            item.AddComponent<RectTransform>();
            var labelActor = item.Create("Label");
            labelActor.AddComponent<RectTransform>();
            labelActor.AddBehaviour<Text>();
            m_Items.Add(item);
        }
    }
    public virtual void Hide() {
        if (!m_IsOpen) return;
        m_IsOpen = false;
        foreach (var item in m_Items) {
            if (item.IsAlive) item.Destroy();
        }
        m_Items.Clear();
    }
    public virtual void OnPointerClick(PointerEventData eventData) {
        if (!IsInteractable() || eventData.button != PointerEventData.InputButton.Left) return;
        if (m_IsOpen) Hide(); else Show();
    }
    public void SelectOption(int index) {
        value = index;
        Hide();
    }
}

/// <summary>Actor/Behaviour factory matching uGUI DefaultControls entry points.</summary>
public static class DefaultControls
{
    public struct Resources
    {
        public AssetRef standard, background, inputField, knob, checkmark, dropdown, mask;
    }

    private static Actor CreateElement(Actor parent, string name, float w, float h) {
        var actor = parent.Create(name);
        var rect = actor.AddComponent<RectTransform>();
        rect.sizeDelta = new Vector2(w, h);
        return actor;
    }

    public static Actor CreatePanel(Actor parent, Resources res) {
        var panel = CreateElement(parent, "Panel", 300, 200);
        panel.AddBehaviour<Image>();
        return panel;
    }
    public static Actor CreateButton(Actor parent, Resources res) {
        var root = CreateElement(parent, "Button", 160, 30);
        root.AddBehaviour<Image>();
        root.AddBehaviour<Button>();
        var label = CreateElement(root, "Text", 160, 30);
        label.AddBehaviour<Text>();
        return root;
    }
    public static Actor CreateText(Actor parent, Resources res) {
        var root = CreateElement(parent, "Text", 160, 20);
        root.AddBehaviour<Text>();
        return root;
    }
    public static Actor CreateImage(Actor parent, Resources res) {
        var root = CreateElement(parent, "Image", 100, 100);
        root.AddBehaviour<Image>();
        return root;
    }
    public static Actor CreateRawImage(Actor parent, Resources res) {
        var root = CreateElement(parent, "RawImage", 100, 100);
        root.AddBehaviour<RawImage>();
        return root;
    }
    public static Actor CreateSlider(Actor parent, Resources res) {
        var root = CreateElement(parent, "Slider", 160, 20);
        root.AddBehaviour<Image>();
        root.AddBehaviour<Slider>();
        return root;
    }
    public static Actor CreateScrollbar(Actor parent, Resources res) {
        var root = CreateElement(parent, "Scrollbar", 160, 20);
        root.AddBehaviour<Image>();
        root.AddBehaviour<Scrollbar>();
        return root;
    }
    public static Actor CreateToggle(Actor parent, Resources res) {
        var root = CreateElement(parent, "Toggle", 120, 20);
        root.AddBehaviour<Toggle>();
        var background = CreateElement(root, "Background", 20, 20);
        background.AddBehaviour<Image>();
        return root;
    }
    public static Actor CreateInputField(Actor parent, Resources res) {
        var root = CreateElement(parent, "InputField", 180, 30);
        root.AddBehaviour<Image>();
        root.AddBehaviour<InputField>();
        return root;
    }
    public static Actor CreateDropdown(Actor parent, Resources res) {
        var root = CreateElement(parent, "Dropdown", 160, 30);
        root.AddBehaviour<Image>();
        root.AddBehaviour<Dropdown>();
        return root;
    }
    public static Actor CreateScrollView(Actor parent, Resources res) {
        var root = CreateElement(parent, "Scroll View", 200, 200);
        root.AddBehaviour<ScrollRect>();
        var viewport = CreateElement(root, "Viewport", 180, 180);
        viewport.AddBehaviour<RectMask2D>();
        var content = CreateElement(viewport, "Content", 180, 300);
        return root;
    }
}
