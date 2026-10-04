using System;
using System.Text;
using System.Globalization;
using System.Linq;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public partial class InputField : Selectable, IUpdateSelectedHandler, IPointerClickHandler, ISubmitHandler, ICancelHandler, IBeginDragHandler, IDragHandler, IEndDragHandler
{
    public enum ContentType { Standard, Autocorrected, IntegerNumber, DecimalNumber, Alphanumeric, Name, EmailAddress, Password, Pin, Custom }
    public enum InputType { Standard, AutoCorrect, Password, Pin }
    public enum CharacterValidation { None, Integer, Decimal, Alphanumeric, Name, EmailAddress }
    public enum LineType { SingleLine, MultiLineSubmit, MultiLineNewline }

    [SerializeField, FieldId("m_TextComponent")] private ComponentRef<Text> m_TextComponentRef;
    private Text? m_TextComponent { get => ResolveUIReference(m_TextComponentRef); set => m_TextComponentRef = value == null ? default : ComponentRef<Text>.From(Owner, value); }
    [SerializeField, FieldId("m_Placeholder")] private ComponentRef<Text> m_PlaceholderRef;
    private Text? m_Placeholder { get => ResolveUIReference(m_PlaceholderRef); set => m_PlaceholderRef = value == null ? default : ComponentRef<Text>.From(Owner, value); }
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
    [SerializeField] private TouchScreenKeyboardType m_KeyboardType = TouchScreenKeyboardType.Default;
    [SerializeField] private bool m_ShouldHideMobileInput;
    private TouchScreenKeyboard? m_Keyboard;

    private readonly UIEvent<string> m_OnValueChanged, m_OnEndEdit;
    public InputField(){m_OnValueChanged=new(this);m_OnEndEdit=new(this);}
    public delegate char OnValidateInput(string text, int charIndex, char addedChar);
    public OnValidateInput? onValidateInput {get;set;}
    private string originalText="",composition="";
    private bool selectingText;
    public bool wasCanceled {get;private set;}
    private bool Secure=>m_InputType is InputType.Password or InputType.Pin || m_ContentType is ContentType.Password or ContentType.Pin;
    private static string ValidUnicode(string text)=>string.Concat(text.EnumerateRunes().Select(r=>r.ToString()));
    private static string Limit(string text,int count){if(count<=0)return text;var builder=new StringBuilder();foreach(var rune in text.EnumerateRunes()){if(count--==0)break;builder.Append(rune.ToString());}return builder.ToString();}
    private int Boundary(int index){index=Math.Clamp(index,0,m_Text.Length);if(index==m_Text.Length)return index;var starts=StringInfo.ParseCombiningCharacters(m_Text);int last=0;foreach(int start in starts){if(start>index)break;last=start;}return last;}
    private int NextBoundary(int index){foreach(int start in StringInfo.ParseCombiningCharacters(m_Text))if(start>index)return start;return m_Text.Length;}
    private int PreviousBoundary(int index){int previous=0;foreach(int start in StringInfo.ParseCombiningCharacters(m_Text)){if(start>=index)break;previous=start;}return previous;}

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
            SetToCustom();
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
            value=Limit(ValidUnicode(value),m_CharacterLimit);
            if(m_Text==value){UpdateLabel();return;}
            m_Text = value;
            m_CaretPosition = Math.Min(m_CaretPosition, m_Text.Length);
            m_SelectionAnchorPosition = Math.Min(m_SelectionAnchorPosition, m_Text.Length);
            m_SelectionFocusPosition = Math.Min(m_SelectionFocusPosition, m_Text.Length);
            SyncSoftKeyboard();
            UpdateLabel();
            m_OnValueChanged.Invoke(m_Text);
        }
    }
    public bool isFocused { get; private set; }
    public int caretPosition {
        get => m_CaretPosition;
        set {
            int v = Boundary(value);
            if (m_CaretPosition == v) return;
            m_CaretPosition = v;
            if (!m_SelectionExists) { m_SelectionAnchorPosition = v; m_SelectionFocusPosition = v; }
            blinkTime=0;SyncSoftKeyboard();editingVisual?.SetVerticesDirty();
        }
    }
    public int selectionAnchorPosition {
        get => m_SelectionAnchorPosition;
        set {
            m_SelectionAnchorPosition = Boundary(value);
            m_SelectionExists = m_SelectionAnchorPosition != m_SelectionFocusPosition;
            blinkTime=0;SyncSoftKeyboard();editingVisual?.SetVerticesDirty();
        }
    }
    public int selectionFocusPosition {
        get => m_SelectionFocusPosition;
        set {
            m_SelectionFocusPosition = Boundary(value);
            m_SelectionExists = m_SelectionAnchorPosition != m_SelectionFocusPosition;
            blinkTime=0;SyncSoftKeyboard();editingVisual?.SetVerticesDirty();
        }
    }
    public bool multiLine => m_LineType != LineType.SingleLine;
    public int selectionStart => Math.Min(m_SelectionAnchorPosition, m_SelectionFocusPosition);
    public int selectionEnd => Math.Max(m_SelectionAnchorPosition, m_SelectionFocusPosition);
    public TouchScreenKeyboard? touchScreenKeyboard => m_Keyboard;
    public TouchScreenKeyboardType keyboardType {
        get => m_KeyboardType;
        set { if (m_KeyboardType == value) return; m_KeyboardType = value; SetToCustom(); }
    }
    public bool shouldHideMobileInput {
        get => m_ShouldHideMobileInput;
        set => m_ShouldHideMobileInput = value;
    }

    public void ActivateInputField() {
        if(isFocused||!IsInteractable())return;
        isFocused = true;originalText=m_Text;wasCanceled=false;composition="";blinkTime=0;
        if(owner!=null)EventSystem.GetCurrent(Owner)?.SetSelectedObject(Owner);
        OpenSoftKeyboard();UpdateLabel();
    }
    public void DeactivateInputField() {
        if (!isFocused) return;
        isFocused = false;
        CloseSoftKeyboard(completed: !wasCanceled);composition="";selectingText=false;
        UpdateLabel();ReleaseEditingVisual();
        m_OnEndEdit.Invoke(m_Text);
    }
    private void OpenSoftKeyboard() {
        if (!TouchScreenKeyboard.isSupported || m_Keyboard is { active: true }) return;
        TouchScreenKeyboard.hideInput = m_ShouldHideMobileInput;
        m_Keyboard = TouchScreenKeyboard.Open(m_Text, m_KeyboardType, false, multiLine,
            m_InputType == InputType.Password || m_InputType == InputType.Pin, false, "", m_CharacterLimit);
    }
    private void CloseSoftKeyboard(bool completed) {
        if (m_Keyboard == null) return;
        if (m_Keyboard.active) { if (completed) m_Keyboard.Done(); else m_Keyboard.Cancel(); }
        m_Keyboard = null;
    }
    private void SyncSoftKeyboard() {
        if (m_Keyboard == null) return;
        m_Keyboard.text = m_Text;
        m_Keyboard.selection = new Overload.Maths.RangeInt(selectionStart, selectionEnd - selectionStart);
    }
    public void SelectAll() {
        m_SelectionAnchorPosition = 0;
        m_SelectionFocusPosition = m_Text.Length;
        m_SelectionExists = m_Text.Length > 0;
        m_CaretPosition = m_Text.Length;
        blinkTime=0;SyncSoftKeyboard();editingVisual?.SetVerticesDirty();
    }
    public void MoveTextStart(bool shift) {
        m_CaretPosition = 0;
        if (shift) { m_SelectionFocusPosition = 0; m_SelectionExists = m_SelectionAnchorPosition != 0; }
        else { m_SelectionAnchorPosition = m_SelectionFocusPosition = 0; m_SelectionExists = false; }
        blinkTime=0;SyncSoftKeyboard();editingVisual?.SetVerticesDirty();
    }
    public void MoveTextEnd(bool shift) {
        m_CaretPosition = m_Text.Length;
        if (shift) { m_SelectionFocusPosition = m_Text.Length; m_SelectionExists = m_SelectionAnchorPosition != m_Text.Length; }
        else { m_SelectionAnchorPosition = m_SelectionFocusPosition = m_Text.Length; m_SelectionExists = false; }
        blinkTime=0;SyncSoftKeyboard();editingVisual?.SetVerticesDirty();
    }

    public virtual void OnPointerClick(PointerEventData eventData) {
        if (!IsInteractable() || eventData.button != PointerEventData.InputButton.Left) return;
        ActivateInputField();
        PlaceCaret(eventData.position,false);
    }
    public virtual void OnSubmit(BaseEventData eventData) {
        if (!IsInteractable()) return;
        ActivateInputField();
    }
    public virtual void OnCancel(BaseEventData eventData) {
        wasCanceled=true;text=originalText;DeactivateInputField();eventData.Use();
    }
    public override void OnSelect(BaseEventData e){base.OnSelect(e);if(shouldActivateOnSelect)ActivateInputField();}
    public override void OnDeselect(BaseEventData e){base.OnDeselect(e);DeactivateInputField();}
    public override void OnDisable(){DeactivateInputField();CloseSoftKeyboard(false);ReleaseEditingVisual();base.OnDisable();}
    internal override void ReleaseNativeComponents(){isFocused=false;CloseSoftKeyboard(false);ReleaseEditingVisual();base.ReleaseNativeComponents();}
    public void OnBeginDrag(PointerEventData e){if(e.button!=PointerEventData.InputButton.Left||!IsInteractable())return;selectingText=true;ActivateInputField();PlaceCaret(e.pressPosition,false);}
    public void OnDrag(PointerEventData e){if(selectingText)PlaceCaret(e.position,true);}
    public void OnEndDrag(PointerEventData e){selectingText=false;}
    public virtual void OnUpdateSelected(BaseEventData eventData) {
        if (!isFocused || !IsInteractable()) return;
        var input=eventData.currentInputModule?.input;
        if(input==null)return;
        bool Key(int code)=>input.GetKeyDown(code)||input.GetKeyRepeat(code);
        bool shift=input.GetKey(340)||input.GetKey(344),ctrl=input.GetKey(341)||input.GetKey(345);
        string nextComposition=Secure?new string(m_AsteriskChar,input.compositionString.Length):input.compositionString;
        if(composition!=nextComposition){composition=nextComposition;UpdateLabel();}
        if(!readOnly&&input.inputString.Length>0)Insert(input.inputString);
        if(Key(256)){OnCancel(eventData);return;}
        if(input.compositionString.Length==0){
            if(Key(259))Backspace();
            if(Key(261)&&!readOnly){if(m_SelectionExists)DeleteSelection();else if(m_CaretPosition<m_Text.Length){int end=NextBoundary(m_CaretPosition);m_Text=m_Text.Remove(m_CaretPosition,end-m_CaretPosition);Changed();}}
            if(Key(262)||Key(263)){int direction=Key(262)?1:-1;if(ctrl)MoveWord(direction,shift);else MoveCaret(direction,shift);}
            if(Key(265)||Key(264))MoveLine(Key(265)?-1:1,shift);
            if(Key(268)){if(ctrl)MoveTextStart(shift);else MoveLineEdge(false,shift);}
            if(Key(269)){if(ctrl)MoveTextEnd(shift);else MoveLineEdge(true,shift);}
            if(ctrl){
                if(Key(65))SelectAll();
                if(Key(67)&&!Secure&&m_SelectionExists)input.clipboard=SelectedText();
                if(Key(88)&&!Secure&&!readOnly&&m_SelectionExists){input.clipboard=SelectedText();DeleteSelection();}
                if(Key(86)&&!readOnly)Insert(input.clipboard??"");
            }
            if(Key(257)||Key(335)){if(m_LineType==LineType.MultiLineNewline&&!readOnly)Insert("\n");else DeactivateInputField();}
        }
        // Editing consumes the navigation/submit snapshot, preventing Enter from reopening the field.
        eventData.Use();SyncSoftKeyboard();
    }
    private void Changed(){blinkTime=0;SanitizeCaret();SyncSoftKeyboard();UpdateLabel();m_OnValueChanged.Invoke(m_Text);}
    private void SetCaret(int next,bool shift){next=Boundary(next);m_CaretPosition=next;if(shift){m_SelectionFocusPosition=next;m_SelectionExists=next!=m_SelectionAnchorPosition;}else{m_SelectionAnchorPosition=m_SelectionFocusPosition=next;m_SelectionExists=false;}blinkTime=0;SyncSoftKeyboard();UpdateLabel();}
    private void MoveWord(int direction,bool shift){int p=m_CaretPosition;if(direction<0){while(p>0&&char.IsWhiteSpace(m_Text[p-1]))p=PreviousBoundary(p);while(p>0&&!char.IsWhiteSpace(m_Text[p-1]))p=PreviousBoundary(p);}else{while(p<m_Text.Length&&!char.IsWhiteSpace(m_Text[p]))p=NextBoundary(p);while(p<m_Text.Length&&char.IsWhiteSpace(m_Text[p]))p=NextBoundary(p);}SetCaret(p,shift);}
    private void MoveLineEdge(bool end,bool shift){int p=m_CaretPosition;if(end){while(p<m_Text.Length&&m_Text[p]!='\n')p=NextBoundary(p);}else{while(p>0&&m_Text[p-1]!='\n')p=PreviousBoundary(p);}SetCaret(p,shift);}
    private static bool IsDigit(char c) => c is >= '0' and <= '9';
    private bool IsValidChar(char c) {
        if (c == '\n') return m_LineType==LineType.MultiLineNewline;
        if (c == '\t') return true;
        return c switch {
            >= ' ' and <= '\uFFFD' => ValidateTyped(c),
            _ => false
        };
    }
    private bool ValidateTyped(char c) {
        return m_CharacterValidation switch {
            CharacterValidation.None => true,
            CharacterValidation.Integer => IsDigit(c) || (c == '-' && m_CaretPosition == 0 && !m_Text.Contains('-')),
            CharacterValidation.Decimal => IsDigit(c) || (c == '.' && !m_Text.Contains('.')) || (c == '-' && m_CaretPosition == 0 && !m_Text.Contains('-')),
            CharacterValidation.Alphanumeric => char.IsLetterOrDigit(c),
            CharacterValidation.Name => char.IsLetter(c) || c == '\'' || c == '-' || c == ' ',
            CharacterValidation.EmailAddress => char.IsLetterOrDigit(c) || c is '.' or '_' or '-' or '@' or '+',
            _ => true
        };
    }

    public void Insert(string textToInsert) {
        if (m_ReadOnly || !IsInteractable() || textToInsert == null || textToInsert.Length == 0) return;
        string before=m_Text;int start=m_SelectionExists?selectionStart:m_CaretPosition,end=m_SelectionExists?selectionEnd:start;
        m_Text=m_Text.Remove(start,end-start);m_CaretPosition=start;
        foreach(var rune in ValidUnicode(textToInsert.Replace("\r\n","\n").Replace('\r','\n')).EnumerateRunes()){
            if(m_CharacterLimit>0&&m_Text.EnumerateRunes().Count()>=m_CharacterLimit)break;
            string addition=rune.ToString();
            if(rune.IsBmp){char c=(char)rune.Value;if(!IsValidChar(c))continue;if(onValidateInput!=null)c=onValidateInput(m_Text,m_CaretPosition,c);if(c=='\0'||char.IsSurrogate(c))continue;addition=c.ToString();}
            else if(m_CharacterValidation!=CharacterValidation.None)continue;
            m_Text=m_Text.Insert(m_CaretPosition,addition);m_CaretPosition+=addition.Length;
        }
        m_SelectionAnchorPosition=m_SelectionFocusPosition=m_CaretPosition;m_SelectionExists=false;
        if(before!=m_Text)Changed();else UpdateLabel();
    }
    private void Backspace() {
        if (m_ReadOnly || !IsInteractable()) return;
        if (m_SelectionExists) { DeleteSelection(); return; }
        if (m_CaretPosition <= 0) return;
        int previous=PreviousBoundary(m_CaretPosition);
        m_Text = m_Text.Remove(previous,m_CaretPosition-previous);m_CaretPosition=previous;
        m_SelectionAnchorPosition=m_SelectionFocusPosition=previous;m_SelectionExists=false;
        SanitizeCaret();
        SyncSoftKeyboard();
        UpdateLabel();
        m_OnValueChanged.Invoke(m_Text);
    }
    private void DeleteSelection() {
        if (!m_SelectionExists) return;
        int start = selectionStart, end = selectionEnd;
        m_Text = m_Text.Remove(start, end - start);
        m_CaretPosition = start;
        m_SelectionAnchorPosition=m_SelectionFocusPosition=start;m_SelectionExists=false;
        SanitizeCaret();
        SyncSoftKeyboard();
        UpdateLabel();
        m_OnValueChanged.Invoke(m_Text);
    }
    private string SelectedText() {
        if (!m_SelectionExists) return "";
        return m_Text[selectionStart..selectionEnd];
    }
    private void MoveCaret(int direction, bool shift) {
        if(!shift&&m_SelectionExists){SetCaret(direction<0?selectionStart:selectionEnd,false);return;}
        SetCaret(direction<0?PreviousBoundary(m_CaretPosition):NextBoundary(m_CaretPosition),shift);
    }
    private void SanitizeCaret() {
        m_CaretPosition = Boundary(m_CaretPosition);
        m_SelectionAnchorPosition = Boundary(m_SelectionAnchorPosition);
        m_SelectionFocusPosition = Boundary(m_SelectionFocusPosition);
        m_SelectionExists = m_SelectionAnchorPosition != m_SelectionFocusPosition;
    }
    private string DisplayText() {
        if (Secure)
            return new string(m_AsteriskChar, m_Text.Length);
        return m_Text;
    }
    private void UpdateLabel() {
        if (m_TextComponent == null) return;
        string display=DisplayText();
        if(isFocused&&composition.Length>0)display=display.Insert(Math.Clamp(m_CaretPosition,0,display.Length),composition);
        m_TextComponent.supportRichText=false;
        m_TextComponent.horizontalOverflow=multiLine?HorizontalWrapMode.Wrap:HorizontalWrapMode.Overflow;
        m_TextComponent.verticalOverflow=VerticalWrapMode.Overflow;
        m_TextComponent.text=display;
        if(m_Placeholder!=null)m_Placeholder.enabled=display.Length==0;
        editingVisual?.SetVerticesDirty();
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
    protected override void SetDirty() {
        base.SetDirty();
        if (owner == null) return;
        EnforceContentType();
        m_Text = Limit(ValidUnicode(m_Text ?? ""), Math.Max(0, m_CharacterLimit));
        SanitizeCaret();
        UpdateLabel();
    }
}
