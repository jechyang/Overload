using System;
using System.Globalization;
using System.Linq;
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
    private string value = "";
    public string text { get => value; set { this.value = Normalize(value ?? "", characterLimit); selection = new RangeInt(Math.Clamp(selection.start, 0, this.value.Length), 0); } }
    private static string Normalize(string input, int limit) => string.Concat(input.EnumerateRunes().Take(limit > 0 ? limit : int.MaxValue).Select(r => r.ToString()));
    private int Boundary(int offset) { offset=Math.Clamp(offset,0,text.Length); if(offset==text.Length)return offset; return StringInfo.ParseCombiningCharacters(text).LastOrDefault(i=>i<=offset); }
    public int characterLimit { get; }
    public TouchScreenKeyboardType type { get; }
    public bool canGetSelection => true;
    public bool canSetSelection => true;
    public RangeInt selection { get; set; }

    private TouchScreenKeyboard(string text, TouchScreenKeyboardType type, int characterLimit)
    {
        this.type = type;
        this.characterLimit = Math.Max(0, characterLimit);
        this.text = text ?? "";
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
        var start = Boundary(selection.start);
        var end = Boundary(selection.start + Math.Max(0, selection.length));
        var remaining = text.Remove(start, end - start);
        int room = characterLimit > 0 ? characterLimit - remaining.EnumerateRunes().Count() : int.MaxValue;
        value = room <= 0 ? "" : Normalize(value, room);
        text = remaining.Insert(start, value);
        selection = new RangeInt(start + value.Length, 0);
    }

    public void Backspace()
    {
        if (!active || status != Status.Visible) return;
        if (selection.length > 0) { DeleteSelection(); return; }
        if (selection.start <= 0) return;
        var caret = Boundary(selection.start);
        var previous = StringInfo.ParseCombiningCharacters(text).LastOrDefault(i => i < caret);
        text = text.Remove(previous, caret - previous);
        selection = new RangeInt(previous, 0);
    }

    public void DeleteSelection()
    {
        if (!active || status != Status.Visible || selection.length <= 0) return;
        var start = Boundary(selection.start);
        var length = Boundary(selection.start + Math.Max(0, selection.length)) - start;
        text = text.Remove(start, length);
        selection = new RangeInt(start, 0);
    }

    public void Done() { if (active && status == Status.Visible) { status = Status.Done; active = false; } }
    public void Cancel() { if (active && status == Status.Visible) { status = Status.Canceled; active = false; } }
    public void Destroy() { active = false; if (status == Status.Visible) status = Status.Done; }
}
