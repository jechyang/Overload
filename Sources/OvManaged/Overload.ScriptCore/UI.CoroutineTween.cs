using System;
using Overload.UI;

namespace Overload.UI.CoroutineTween;

/// <summary>Session-safe tween contract (uGUI CoroutineTween.ITweenValue).</summary>
public interface ITweenValue
{
    void TweenValue(float floatPercentage);
    bool ignoreTimeScale { get; }
    float duration { get; }
    bool ValidTarget();
}

/// <summary>Color tween, receives TweenValue and sets the value on the target callback.</summary>
public struct ColorTween : ITweenValue
{
    public enum ColorTweenMode
    {
        All,
        RGB,
        Alpha
    }

    /// <summary>Overload stand-in for UnityEvent&lt;Color&gt;.</summary>
    public sealed class ColorTweenCallback : UIEvent<Color>
    {
    }

    private ColorTweenCallback? m_Target;
    private Color m_StartColor;
    private Color m_TargetColor;
    private ColorTweenMode m_TweenMode;
    private float m_Duration;
    private bool m_IgnoreTimeScale;

    public Color startColor {
        get => m_StartColor;
        set => m_StartColor = value;
    }
    public Color targetColor {
        get => m_TargetColor;
        set => m_TargetColor = value;
    }
    public ColorTweenMode tweenMode {
        get => m_TweenMode;
        set => m_TweenMode = value;
    }
    public float duration {
        get => m_Duration;
        set {
            if (!float.IsFinite(value) || value < 0) throw new ArgumentOutOfRangeException(nameof(value));
            m_Duration = value;
        }
    }
    public bool ignoreTimeScale {
        get => m_IgnoreTimeScale;
        set => m_IgnoreTimeScale = value;
    }

    public void TweenValue(float floatPercentage)
    {
        if (!ValidTarget()) return;
        float t = Math.Clamp(floatPercentage, 0, 1);
        Color newColor = Lerp(m_StartColor, m_TargetColor, t);
        if (m_TweenMode == ColorTweenMode.Alpha) {
            newColor = new Color(m_StartColor.r, m_StartColor.g, m_StartColor.b, newColor.a);
        } else if (m_TweenMode == ColorTweenMode.RGB) {
            newColor = new Color(newColor.r, newColor.g, newColor.b, m_StartColor.a);
        }
        m_Target!.Invoke(newColor);
    }

    public void AddOnChangedCallback(Action<Color> callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        m_Target ??= new ColorTweenCallback();
        m_Target.AddListener(callback);
    }

    public bool GetIgnoreTimescale() => m_IgnoreTimeScale;
    public float GetDuration() => m_Duration;
    public bool ValidTarget() => m_Target != null;

    private static Color Lerp(Color a, Color b, float t) => new(
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t);
}
