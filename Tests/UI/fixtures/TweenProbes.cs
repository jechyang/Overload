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

/// <summary>Self-checks FloatTween (step 2) after prepare.</summary>
public class FloatTweenProbe : Behaviour
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

        var empty = new FloatTween();
        Need(!empty.ValidTarget(), "empty FloatTween has no target");
        Need(empty.GetDuration() == 0 && !empty.GetIgnoreTimescale(), "empty FloatTween defaults");

        var tween = new FloatTween {
            startValue = 10f,
            targetValue = 20f,
            duration = 1f,
            ignoreTimeScale = false
        };
        Need(!tween.ValidTarget(), "no target before callback");
        float? seen = null;
        tween.AddOnChangedCallback(v => seen = v);
        Need(tween.ValidTarget(), "callback registers a target");
        Need(tween.GetDuration() == 1f && !tween.GetIgnoreTimescale(), "duration/ignoreTimeScale");

        tween.TweenValue(0f);
        Need(seen != null && seen.Value == 10f, "t=0 is startValue");
        tween.TweenValue(0.25f);
        Need(seen != null && Math.Abs(seen.Value - 12.5f) < 1e-4f, "t=0.25 lerps");
        tween.TweenValue(1f);
        Need(seen != null && seen.Value == 20f, "t=1 is targetValue");
        tween.TweenValue(3f);
        Need(seen != null && seen.Value == 20f, "percentage clamped high");
        tween.TweenValue(-2f);
        Need(seen != null && seen.Value == 10f, "percentage clamped low");

        bool threw = false;
        try { _ = tween.startValue = float.NaN; } catch (ArgumentException) { threw = true; }
        Need(threw, "NaN startValue rejected");
        threw = false;
        try { _ = tween.duration = -0.1f; } catch (ArgumentOutOfRangeException) { threw = true; }
        Need(threw, "negative duration rejected");
    }
}

/// <summary>Self-checks TweenRunner&lt;T&gt; (step 3) after prepare.</summary>
public class TweenRunnerProbe : Behaviour
{
    [SerializeField] public int passed;
    [SerializeField] public string lastError = "";

    public override void OnUpdate(float dt)
    {
        if (passed != 0 || lastError.Length != 0) return;
        try {
            Check(this);
            passed = 1;
        } catch (Exception e) {
            lastError = e.Message;
            passed = -1;
        }
    }

    private static void Check(Behaviour host)
    {
        static void Need(bool ok, string message) { if (!ok) throw new Exception(message); }

        float? last = null;
        var tween = new FloatTween { startValue = 0f, targetValue = 100f, duration = 1f, ignoreTimeScale = true };
        tween.AddOnChangedCallback(v => last = v);
        var runner = new TweenRunner<FloatTween>();
        Need(!runner.isRunning, "runner idle before start");
        runner.Init(host);
        TweenRunnerRegistry.Track(runner);
        runner.StartTween(tween);
        Need(runner.isRunning, "runner starts with valid target");

        // Duration 0 jumps to end.
        var instant = new FloatTween { startValue = 5f, targetValue = 9f, duration = 0f };
        float? instantValue = null;
        instant.AddOnChangedCallback(v => instantValue = v);
        var instantRunner = new TweenRunner<FloatTween>();
        instantRunner.Init(host);
        instantRunner.StartTween(instant);
        Need(!instantRunner.isRunning && instantValue == 9f, "zero duration completes immediately");

        // Inactive container snaps to end.
        // ValidTarget false is ignored.
        var invalid = new FloatTween();
        var invalidRunner = new TweenRunner<FloatTween>();
        invalidRunner.Init(host);
        invalidRunner.StartTween(invalid);
        Need(!invalidRunner.isRunning, "invalid target does not start");

        // Drive the real registry tick with ignoreTimeScale=true.
        TweenRunnerRegistry.Tick(0.25f, 0.5f);
        Need(last != null && Math.Abs(last.Value - 50f) < 1e-3f, "tick uses unscaled time when ignoreTimeScale");
        TweenRunnerRegistry.Tick(0.25f, 0.5f);
        Need(last != null && Math.Abs(last.Value - 100f) < 1e-3f, "runner completes at duration and holds end");
        Need(!runner.isRunning, "runner stops after completion");

        // Restart and stop early.
        runner.StartTween(tween);
        Need(runner.isRunning, "restart works");
        runner.StopTween();
        Need(!runner.isRunning, "StopTween clears running");
    }
}
