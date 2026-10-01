using System;
using System.Collections.Generic;
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

/// <summary>Float tween, receives TweenValue and sets the value on the target callback.</summary>
public struct FloatTween : ITweenValue
{
    /// <summary>Overload stand-in for UnityEvent&lt;float&gt;.</summary>
    public sealed class FloatTweenCallback : UIEvent<float>
    {
    }

    private FloatTweenCallback? m_Target;
    private float m_StartValue;
    private float m_TargetValue;
    private float m_Duration;
    private bool m_IgnoreTimeScale;

    public float startValue {
        get => m_StartValue;
        set {
            if (!float.IsFinite(value)) throw new ArgumentException("startValue must be finite");
            m_StartValue = value;
        }
    }
    public float targetValue {
        get => m_TargetValue;
        set {
            if (!float.IsFinite(value)) throw new ArgumentException("targetValue must be finite");
            m_TargetValue = value;
        }
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
        m_Target!.Invoke(m_StartValue + (m_TargetValue - m_StartValue) * t);
    }

    public void AddOnChangedCallback(Action<float> callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        m_Target ??= new FloatTweenCallback();
        m_Target.AddListener(callback);
    }

    public bool GetIgnoreTimescale() => m_IgnoreTimeScale;
    public float GetDuration() => m_Duration;
    public bool ValidTarget() => m_Target != null;
}

/// <summary>
/// Session-safe tween runner (uGUI TweenRunner without coroutines).
/// Call <see cref="Tick"/> from the UI tick; Init binds lifetime to a Behaviour.
/// </summary>
public class TweenRunner<T> where T : struct, ITweenValue
{
    private T m_Tween;
    private float m_Elapsed;
    private bool m_Running;
    private Behaviour? m_Container;
    private WeakReference<Behaviour>? m_ContainerRef;

    public bool isRunning => m_Running;

    public void Init(Behaviour coroutineContainer)
    {
        ArgumentNullException.ThrowIfNull(coroutineContainer);
        m_Container = coroutineContainer;
        m_ContainerRef = new WeakReference<Behaviour>(coroutineContainer);
    }

    public void StartTween(T info)
    {
        if (!info.ValidTarget()) return;
        StopTween();
        if (m_Container != null && !(m_Container.Owner.IsAlive && m_Container.Owner.ActiveInHierarchy)) {
            info.TweenValue(1.0f);
            return;
        }
        m_Tween = info;
        m_Elapsed = 0;
        m_Running = true;
        if (info.duration <= 0) {
            m_Tween.TweenValue(1.0f);
            m_Running = false;
        }
    }

    public void StopTween()
    {
        m_Running = false;
        m_Elapsed = 0;
    }

    /// <summary>Advance active tweens. unscaledDelta is used when ignoreTimeScale is set.</summary>
    public void Tick(float deltaTime, float unscaledDeltaTime)
    {
        if (!m_Running) return;
        if (m_ContainerRef != null && (!m_ContainerRef.TryGetTarget(out var owner) || owner == null || !(owner.Owner.IsAlive && owner.Owner.ActiveInHierarchy))) {
            StopTween();
            return;
        }
        float step = m_Tween.ignoreTimeScale ? unscaledDeltaTime : deltaTime;
        if (!float.IsFinite(step) || step < 0) step = 0;
        m_Elapsed += step;
        if (m_Elapsed >= m_Tween.duration) {
            m_Tween.TweenValue(1.0f);
            m_Running = false;
            return;
        }
        m_Tween.TweenValue(m_Elapsed / m_Tween.duration);
    }
}

/// <summary>Session-owned registry of active runners advanced by UI.TickUI.</summary>
public static class TweenRunnerRegistry
{
    private static readonly List<ITweenTicker> s_Running = new();
    private static readonly List<ITweenTicker> s_Pending = new();

    public interface ITweenTicker
    {
        void Tick(float deltaTime, float unscaledDeltaTime);
        bool IsRunning { get; }
    }

    private sealed class Adapter<T> : ITweenTicker where T : struct, ITweenValue
    {
        private readonly TweenRunner<T> runner;
        public Adapter(TweenRunner<T> runner) { this.runner = runner; }
        public bool IsRunning => runner.isRunning;
        public void Tick(float deltaTime, float unscaledDeltaTime) => runner.Tick(deltaTime, unscaledDeltaTime);
    }

    public static void Track<T>(TweenRunner<T> runner) where T : struct, ITweenValue
    {
        ArgumentNullException.ThrowIfNull(runner);
        s_Pending.Add(new Adapter<T>(runner));
    }

    public static void Tick(float deltaTime, float unscaledDeltaTime)
    {
        for (int i = 0; i < s_Pending.Count; ++i) s_Running.Add(s_Pending[i]);
        s_Pending.Clear();
        for (int i = s_Running.Count - 1; i >= 0; --i) {
            var item = s_Running[i];
            item.Tick(deltaTime, unscaledDeltaTime);
            if (!item.IsRunning) s_Running.RemoveAt(i);
        }
    }

    public static void Clear()
    {
        s_Running.Clear();
        s_Pending.Clear();
    }
}
