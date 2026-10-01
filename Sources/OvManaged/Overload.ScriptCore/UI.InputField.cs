using System;
using System.Text;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public class InputField : Selectable, IUpdateSelectedHandler, IPointerClickHandler, ISubmitHandler, ICancelHandler
{
    public enum ContentType { Standard, Autocorrected, IntegerNumber, DecimalNumber, Alphanumeric, Name, EmailAddress, Password, Pin, Custom }
    public enum InputType { Standard, AutoCorrect, Password, Pin }
    public enum CharacterValidation { None, Integer, Decimal, Alphanumeric, Name, EmailAddress }
    public enum LineType { SingleLine, MultiLineSubmit, MultiLineNewline }

    [SerializeField] private Text? m_TextComponent;
    [SerializeField] private Text? m_Placeholder;
    [SerializeField] private ContentType m_ContentType = ContentType.Standard;
    [SerializeField] private InputType m_InputType = InputType.Standard;
    [SerializeField] private CharacterValidation m_CharacterValidation = CharacterValidation.None;
    [SerializeField] private LineType m_LineType = LineType.SingleLine;
    [SerializeField] private bool m_ReadOnly, m_ShouldActivateOnSelect = true;
    [SerializeField] private int m_CharacterLimit;
    [SerializeField] private char m_AsteriskChar = '*';
    [SerializeField] private string m_Text = "";
    [SerializeField] private int m_CaretPosition, m_SelectionAnchorPosition, m_SelectionFocusPosition;
    [SerializeField] private bool m_SelectionExists;

    private readonly UIEvent<string> m_OnValueChanged = new(), m_OnEndEdit = new();

    public UIEvent<string> onValueChanged => m_OnValueChanged;
    public UIEvent<string> onEndEdit => m_OnEndEdit;
    public Text? textComponent { get => m_TextComponent; set { m_TextComponent = value; UpdateLabel(); } }
    public Text? placeholder { get => m_Placeholder; set { m_Placeholder = value; UpdateLabel(); } }
    public ContentType contentType {
        get => m_ContentType;
        set {
            if (m_ContentType == value) return;
            m_ContentType = value;
            EnforceContentType();
            UpdateLabel();
        }
    }
    public InputType inputType {
        get => m_InputType;
        set {
            if (m_InputType == value) return;
            m_InputType = value;
            SetToCustomIfContentTypeIsNot(ContentType.Standard, ContentType.Autocorrected);
            UpdateLabel();
        }
    }
    public CharacterValidation characterValidation {
        get => m_CharacterValidation;
        set {
            if (m_CharacterValidation == value) return;
            m_CharacterValidation = value;
            SetToCustom();
            UpdateLabel();
        }
    }
    public LineType lineType {
        get => m_LineType;
        set {
            if (m_LineType == value) return;
            m_LineType = value;
            SetToCustomIfContentTypeIsNot(ContentType.Standard, ContentType.Autocorrected);
            UpdateLabel();
        }
    }
    public bool readOnly { get => m_ReadOnly; set => m_ReadOnly = value; }
    public bool shouldActivateOnSelect { get => m_ShouldActivateOnSelect; set => m_ShouldActivateOnSelect = value; }
    public int characterLimit {
        get => m_CharacterLimit;
        set { m_CharacterLimit = Math.Max(0, value); text = m_Text; }
    }
    public char asteriskChar {
        get => m_AsteriskChar;
        set {
            if (m_AsteriskChar == value) return;
            m_AsteriskChar = value;
            UpdateLabel();
        }
    }
    public string text {
        get => m_Text;
        set {
            if (value == null) value = "";
            if (textComponent != null && (textComponent.text == value || m_Text == value)) { UpdateLabel(); return; }
            if (m_CharacterLimit > 0 && value.Length > m_CharacterLimit) value = value[..m_CharacterLimit];
            m_Text = value;
            m_CaretPosition = Math.Min(m_CaretPosition, m_Text.Length);
            m_SelectionAnchorPosition = Math.Min(m_SelectionAnchorPosition, m_Text.Length);
            m_SelectionFocusPosition = Math.Min(m_SelectionFocusPosition, m_Text.Length);
            UpdateLabel();
            m_OnValueChanged.Invoke(m_Text);
        }
    }
    public bool isFocused { get; private set; }
    public int caretPosition {
        get => m_CaretPosition;
        set {
            int v = Math.Clamp(value, 0, m_Text.Length);
            if (m_CaretPosition == v) return;
            m_CaretPosition = v;
            if (!m_SelectionExists) { m_SelectionAnchorPosition = v; m_SelectionFocusPosition = v; }
        }
    }
    public int selectionAnchorPosition {
        get => m_SelectionAnchorPosition;
        set {
            m_SelectionAnchorPosition = Math.Clamp(value, 0, m_Text.Length);
            m_SelectionExists = m_SelectionAnchorPosition != m_SelectionFocusPosition;
        }
    }
    public int selectionFocusPosition {
        get => m_SelectionFocusPosition;
        set {
            m_SelectionFocusPosition = Math.Clamp(value, 0, m_Text.Length);
            m_SelectionExists = m_SelectionAnchorPosition != m_SelectionFocusPosition;
        }
    }
    public bool multiLine => m_LineType != LineType.SingleLine;
    public int selectionStart => Math.Min(m_SelectionAnchorPosition, m_SelectionFocusPosition);
    public int selectionEnd => Math.Max(m_SelectionAnchorPosition, m_SelectionFocusPosition);

    public void ActivateInputField() { isFocused = true; }
    public void DeactivateInputField() {
        if (!isFocused) return;
        isFocused = false;
        m_OnEndEdit.Invoke(m_Text);
    }
    public void SelectAll() {
        m_SelectionAnchorPosition = 0;
        m_SelectionFocusPosition = m_Text.Length;
        m_SelectionExists = m_Text.Length > 0;
        m_CaretPosition = m_Text.Length;
    }
    public void MoveTextStart(bool shift) {
        m_CaretPosition = 0;
        if (shift) { m_SelectionFocusPosition = 0; m_SelectionExists = m_SelectionAnchorPosition != 0; }
        else { m_SelectionAnchorPosition = m_SelectionFocusPosition = 0; m_SelectionExists = false; }
    }
    public void MoveTextEnd(bool shift) {
        m_CaretPosition = m_Text.Length;
        if (shift) { m_SelectionFocusPosition = m_Text.Length; m_SelectionExists = m_SelectionAnchorPosition != m_Text.Length; }
        else { m_SelectionAnchorPosition = m_SelectionFocusPosition = m_Text.Length; m_SelectionExists = false; }
    }

    public virtual void OnPointerClick(PointerEventData eventData) {
        if (!IsInteractable() || eventData.button != PointerEventData.InputButton.Left) return;
        if (m_ShouldActivateOnSelect) ActivateInputField();
    }
    public virtual void OnSubmit(BaseEventData eventData) {
        if (!IsInteractable()) return;
        ActivateInputField();
    }
    public virtual void OnCancel(BaseEventData eventData) {
        DeactivateInputField();
    }
    public virtual void OnUpdateSelected(BaseEventData eventData) {
        if (!isFocused || !IsInteractable() || m_ReadOnly) return;
        var system = EventSystem.GetCurrent(Owner);
        var input = system?.currentInputModule?.input;
        if (input == null) return;

        // Composition (IME) text is applied through inputString/composition hooks.
        if (input.inputString.Length > 0) Insert(input.inputString);
        if (input.GetKeyDown(256) || input.GetKeyRepeat(256)) Backspace();
        if (input.GetKeyDown(257) || input.GetKeyRepeat(257)) { // Enter
            if (multiLine && m_LineType == LineType.MultiLineNewline) Insert("\n");
            else DeactivateInputField();
        }
        bool shift = input.GetKey(340) || input.GetKey(344);
        if (input.GetKeyDown(262) || input.GetKeyRepeat(262)) MoveCaret(+1, shift);
        if (input.GetKeyDown(263) || input.GetKeyRepeat(263)) MoveCaret(-1, shift);
        if (input.GetKeyDown(268)) MoveTextStart(shift);
        if (input.GetKeyDown(269)) MoveTextEnd(shift);
        if (input.GetKeyDown(261)) {
            if (m_SelectionExists) DeleteSelection();
            else if (m_CaretPosition < m_Text.Length) {
                m_Text = m_Text.Remove(m_CaretPosition, 1);
                SanitizeCaret();
                UpdateLabel();
                m_OnValueChanged.Invoke(m_Text);
            }
        }
        bool ctrl = input.GetKey(341) || input.GetKey(345);
        if (ctrl) {
            if (input.GetKeyDown(65)) SelectAll();
            if (input.GetKeyDown(67)) { if (m_SelectionExists) input.clipboard = SelectedText(); }
            if (input.GetKeyDown(88)) { if (m_SelectionExists) { input.clipboard = SelectedText(); DeleteSelection(); } }
            if (input.GetKeyDown(86)) Insert(input.clipboard ?? "");
        }
    }

    private static bool IsDigit(char c) => c is >= '0' and <= '9';
    private bool IsValidChar(char c) {
        if (c == '\n' && !multiLine) return false;
        if (c == '\t') return true;
        return c switch {
            >= ' ' and <= '\uFFFD' => ValidateTyped(c),
            _ => false
        };
    }
    private bool ValidateTyped(char c) {
        return m_CharacterValidation switch {
            CharacterValidation.None => true,
            CharacterValidation.Integer => IsDigit(c) || (c == '-' && m_CaretPosition == 0),
            CharacterValidation.Decimal => IsDigit(c) || c == '.' || (c == '-' && m_CaretPosition == 0),
            CharacterValidation.Alphanumeric => char.IsLetterOrDigit(c),
            CharacterValidation.Name => char.IsLetter(c) || c == '\'' || c == '-' || c == ' ',
            CharacterValidation.EmailAddress => char.IsLetterOrDigit(c) || c is '.' or '_' or '-' or '@' or '+',
            _ => true
        };
    }

    public void Insert(string textToInsert) {
        if (m_ReadOnly || !IsInteractable() || textToInsert == null || textToInsert.Length == 0) return;
        if (m_SelectionExists) DeleteSelection();
        var builder = new StringBuilder();
        foreach (char c in textToInsert) {
            if (IsValidChar(c)) builder.Append(c);
        }
        string insert = builder.ToString();
        if (insert.Length == 0) return;
        if (m_CharacterLimit > 0) {
            int room = m_CharacterLimit - m_Text.Length;
            if (room <= 0) return;
            if (insert.Length > room) insert = insert[..room];
        }
        m_Text = m_Text.Insert(m_CaretPosition, insert);
        m_CaretPosition += insert.Length;
        m_SelectionAnchorPosition = m_SelectionFocusPosition = m_CaretPosition;
        m_SelectionExists = false;
        UpdateLabel();
        m_OnValueChanged.Invoke(m_Text);
    }
    private void Backspace() {
        if (m_ReadOnly || !IsInteractable()) return;
        if (m_SelectionExists) { DeleteSelection(); return; }
        if (m_CaretPosition <= 0) return;
        m_Text = m_Text.Remove(m_CaretPosition - 1, 1);
        --m_CaretPosition;
        SanitizeCaret();
        UpdateLabel();
        m_OnValueChanged.Invoke(m_Text);
    }
    private void DeleteSelection() {
        if (!m_SelectionExists) return;
        int start = selectionStart, end = selectionEnd;
        m_Text = m_Text.Remove(start, end - start);
        m_CaretPosition = start;
        SanitizeCaret();
        UpdateLabel();
        m_OnValueChanged.Invoke(m_Text);
    }
    private string SelectedText() {
        if (!m_SelectionExists) return "";
        return m_Text[selectionStart..selectionEnd];
    }
    private void MoveCaret(int direction, bool shift) {
        int next = Math.Clamp(m_CaretPosition + direction, 0, m_Text.Length);
        m_CaretPosition = next;
        if (shift) {
            m_SelectionFocusPosition = next;
            m_SelectionExists = m_SelectionAnchorPosition != next;
        } else {
            m_SelectionAnchorPosition = m_SelectionFocusPosition = next;
            m_SelectionExists = false;
        }
    }
    private void SanitizeCaret() {
        m_CaretPosition = Math.Clamp(m_CaretPosition, 0, m_Text.Length);
        m_SelectionAnchorPosition = Math.Clamp(m_SelectionAnchorPosition, 0, m_Text.Length);
        m_SelectionFocusPosition = Math.Clamp(m_SelectionFocusPosition, 0, m_Text.Length);
        m_SelectionExists = m_SelectionAnchorPosition != m_SelectionFocusPosition;
    }
    private string DisplayText() {
        if (m_InputType == InputType.Password || m_ContentType is ContentType.Password or ContentType.Pin)
            return new string(m_AsteriskChar, m_Text.Length);
        return m_Text;
    }
    private void UpdateLabel() {
        if (m_TextComponent == null) return;
        string display = DisplayText();
        m_TextComponent.text = display;
        if (m_Placeholder != null) m_Placeholder.text = display.Length == 0 && !isFocused ? m_Placeholder.text : "";
    }
    private void EnforceContentType() {
        switch (m_ContentType) {
            case ContentType.Standard:
                m_InputType = InputType.Standard;
                m_CharacterValidation = CharacterValidation.None;
                break;
            case ContentType.Autocorrected:
                m_InputType = InputType.AutoCorrect;
                m_CharacterValidation = CharacterValidation.None;
                break;
            case ContentType.IntegerNumber:
                m_LineType = LineType.SingleLine;
                m_InputType = InputType.Standard;
                m_CharacterValidation = CharacterValidation.Integer;
                break;
            case ContentType.DecimalNumber:
                m_LineType = LineType.SingleLine;
                m_InputType = InputType.Standard;
                m_CharacterValidation = CharacterValidation.Decimal;
                break;
            case ContentType.Alphanumeric:
                m_LineType = LineType.SingleLine;
                m_InputType = InputType.Standard;
                m_CharacterValidation = CharacterValidation.Alphanumeric;
                break;
            case ContentType.Name:
                m_LineType = LineType.SingleLine;
                m_InputType = InputType.Standard;
                m_CharacterValidation = CharacterValidation.Name;
                break;
            case ContentType.EmailAddress:
                m_LineType = LineType.SingleLine;
                m_InputType = InputType.Standard;
                m_CharacterValidation = CharacterValidation.EmailAddress;
                break;
            case ContentType.Password:
                m_LineType = LineType.SingleLine;
                m_InputType = InputType.Password;
                m_CharacterValidation = CharacterValidation.None;
                break;
            case ContentType.Pin:
                m_LineType = LineType.SingleLine;
                m_InputType = InputType.Pin;
                m_CharacterValidation = CharacterValidation.Integer;
                break;
        }
    }
    private void SetToCustomIfContentTypeIsNot(params ContentType[] allowed) {
        if (m_ContentType == ContentType.Custom) return;
        foreach (var t in allowed) if (m_ContentType == t) return;
        m_ContentType = ContentType.Custom;
    }
    private void SetToCustom() => m_ContentType = ContentType.Custom;
    public override void OnEnable() {
        base.OnEnable();
        EnforceContentType();
        UpdateLabel();
    }
}

