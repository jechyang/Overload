using System;
using Overload;
using Overload.UI;
using Overload.UI.CoroutineTween;

[assembly: Overload.UI.EnableUI]

namespace TweenFixture;

/// <summary>Self-checks ColorTween (step 1) after prepare.</summary>
public class ColorTweenProbe : Behaviour
{
    [SerializeField] public int passed;
    [SerializeField] public string lastError = "";

    public override void OnUpdate(float dt)
    {
        if (passed != 0 || lastError.Length != 0) return;
        try {
            Check();
            passed = 1;
        } catch (Exception e) {
            lastError = e.Message;
            passed = -1;
        }
    }

    private static void Check()
    {
        static void Need(bool ok, string message) { if (!ok) throw new Exception(message); }

        var empty = new ColorTween();
        Need(!empty.ValidTarget(), "empty ColorTween has no target");
        Need(empty.GetDuration() == 0 && !empty.GetIgnoreTimescale(), "empty ColorTween defaults");

        var tween = new ColorTween {
            startColor = new Color(0, 0, 0, 0),
            targetColor = new Color(1, 1, 1, 1),
            duration = 0.5f,
            ignoreTimeScale = true,
            tweenMode = ColorTween.ColorTweenMode.All
        };
        Need(tween.ValidTarget() == false, "target still missing before callback");
        Color? seen = null;
        tween.AddOnChangedCallback(c => seen = c);
        Need(tween.ValidTarget(), "callback registers a target");
        Need(tween.GetDuration() == 0.5f && tween.GetIgnoreTimescale(), "duration/ignoreTimeScale");

        tween.TweenValue(0f);
        Need(seen != null && seen.Value.r == 0 && seen.Value.a == 0, "t=0 is start color");
        tween.TweenValue(0.5f);
        Need(seen != null && Math.Abs(seen.Value.r - 0.5f) < 1e-4f && Math.Abs(seen.Value.a - 0.5f) < 1e-4f, "t=0.5 lerps all channels");
        tween.TweenValue(1f);
        Need(seen != null && seen.Value.r == 1 && seen.Value.a == 1, "t=1 is target color");

        var rgb = tween;
        rgb.tweenMode = ColorTween.ColorTweenMode.RGB;
        rgb.AddOnChangedCallback(c => seen = c);
        rgb.TweenValue(0.5f);
        Need(seen != null && Math.Abs(seen.Value.a - 0f) < 1e-4f, "RGB mode keeps start alpha");

        var alpha = tween;
        alpha.tweenMode = ColorTween.ColorTweenMode.Alpha;
        alpha.AddOnChangedCallback(c => seen = c);
        alpha.TweenValue(0.5f);
        Need(seen != null && Math.Abs(seen.Value.r - 0f) < 1e-4f && Math.Abs(seen.Value.a - 0.5f) < 1e-4f, "Alpha mode keeps start RGB");

        bool threw = false;
        try { _ = tween.duration = -1; } catch (ArgumentOutOfRangeException) { threw = true; }
        Need(threw, "negative duration rejected");

        // Invalid percentage is clamped.
        tween.TweenValue(2f);
        Need(seen != null && seen.Value.a == 1, "percentage clamped to 1");
        tween.TweenValue(-1f);
        Need(seen != null && seen.Value.a == 0, "percentage clamped to 0");
    }
}
