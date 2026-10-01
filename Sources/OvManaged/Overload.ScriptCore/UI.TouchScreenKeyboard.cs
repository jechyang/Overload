using System;
using Overload.Maths;

namespace Overload;

/// <summary>
/// Mobile-style keyboard layout hint. Desktop input uses character/IME events;
/// the enum keeps InputField content-type shortcuts aligned with the reference API.
/// </summary>
public enum TouchScreenKeyboardType
{
    Default,
    ASCIICapable,
    NumbersAndPunctuation,
    URL,
    NumberPad,
    PhonePad,
    NamePhonePad,
    EmailAddress,
    DecimalPad,
    Search,
}

/// <summary>
/// Platform soft-keyboard session adapted to the desktop character/IME backend.
/// Open() starts a session that accepts injected text (tests) and host character
/// input; Done/Cancel end it. There is no on-screen keyboard surface on Windows
/// desktop hosts — physical/IME input is the equivalent engine path.
/// </summary>
public sealed class TouchScreenKeyboard
{
    public enum Status { Visible, Done, Canceled }

    public static bool hideInput { get; set; }

    // Desktop host provides Window character/IME events as the text input backend.
    public static bool isSupported => true;

    public Status status { get; private set; } = Status.Visible;
    public bool active { get; private set; }
    public string text { get; set; } = "";
    public int characterLimit { get; }
    public TouchScreenKeyboardType type { get; }
    public bool canGetSelection => true;
    public bool canSetSelection => true;
    public RangeInt selection { get; set; }

    private TouchScreenKeyboard(string text, TouchScreenKeyboardType type, int characterLimit)
    {
        this.text = text ?? "";
        this.type = type;
        this.characterLimit = Math.Max(0, characterLimit);
        if (this.characterLimit > 0 && this.text.Length > this.characterLimit)
            this.text = this.text[..this.characterLimit];
        selection = new RangeInt(this.text.Length, 0);
        active = true;
    }

    public static TouchScreenKeyboard Open(
        string text,
        TouchScreenKeyboardType keyboardType = TouchScreenKeyboardType.Default,
        bool autocorrection = false,
        bool multiline = false,
        bool secure = false,
        bool alert = false,
        string textPlaceholder = "",
        int characterLimit = 0)
    {
        _ = autocorrection; _ = multiline; _ = secure; _ = alert; _ = textPlaceholder;
        return new TouchScreenKeyboard(text, keyboardType, characterLimit);
    }

    /// <summary>Host character/IME input or test injection into the live session.</summary>
    public void Insert(string value)
    {
        if (!active || status != Status.Visible || string.IsNullOrEmpty(value)) return;
        if (characterLimit > 0)
        {
            var room = characterLimit - text.Length;
            if (room <= 0) return;
            if (value.Length > room) value = value[..room];
        }
        var start = Math.Clamp(selection.start, 0, text.Length);
        var length = Math.Clamp(selection.length, 0, text.Length - start);
        text = text.Remove(start, length).Insert(start, value);
        selection = new RangeInt(start + value.Length, 0);
    }

    public void Backspace()
    {
        if (!active || status != Status.Visible) return;
        if (selection.length > 0) { DeleteSelection(); return; }
        if (selection.start <= 0) return;
        var caret = selection.start;
        text = text.Remove(caret - 1, 1);
        selection = new RangeInt(caret - 1, 0);
    }

    public void DeleteSelection()
    {
        if (!active || status != Status.Visible || selection.length <= 0) return;
        var start = Math.Clamp(selection.start, 0, text.Length);
        var length = Math.Clamp(selection.length, 0, text.Length - start);
        text = text.Remove(start, length);
        selection = new RangeInt(start, 0);
    }

    public void Done() { if (active && status == Status.Visible) { status = Status.Done; active = false; } }
    public void Cancel() { if (active && status == Status.Visible) { status = Status.Canceled; active = false; } }
    public void Destroy() { active = false; if (status == Status.Visible) status = Status.Done; }
}
