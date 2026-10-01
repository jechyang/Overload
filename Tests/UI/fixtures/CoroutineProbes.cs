using System;
using System.Collections;
using System.Threading.Tasks;
using Overload;
using Overload.UI;

[assembly: Overload.UI.EnableUI]

namespace CoroutineFixture;

public class CoroutineProbe : Behaviour
{
    [SerializeField] public int frames;
    [SerializeField] public int waits;
    [SerializeField] public int nested;
    [SerializeField] public int stopped;
    [SerializeField] public int asyncFrames;
    [SerializeField] public int passed;
    [SerializeField] public string lastError = "";

    private Coroutine? running;

    public override void OnEnable()
    {
        base.OnEnable();
        StartCoroutine(Root());
        running = StartCoroutine(Stoppable());
        _ = RunAsync();
    }

    public override void OnUpdate(float dt)
    {
        if (running != null && frames >= 2 && !running.isDone) {
            StopCoroutine(running);
            stopped = 1;
        }
        if (passed == 0 && lastError.Length == 0 && frames >= 3 && asyncFrames >= 2) {
            try {
                Check();
                passed = 1;
            } catch (Exception e) {
                lastError = e.Message;
                passed = -1;
            }
        }
    }

    private IEnumerator Root()
    {
        ++frames;
        yield return null;
        ++frames;
        yield return new WaitForSeconds(0.05f);
        ++waits;
        yield return Nested();
        ++nested;
        yield return null;
        ++frames;
    }

    private IEnumerator Nested()
    {
        yield return null;
        ++nested;
    }

    private IEnumerator Stoppable()
    {
        while (true) yield return null;
    }

    private async Task RunAsync()
    {
        await AsyncYield.NextFrame();
        ++asyncFrames;
        await AsyncYield.NextFrame();
        ++asyncFrames;
    }

    private void Check()
    {
        static void Need(bool ok, string message) { if (!ok) throw new Exception(message); }
        Need(frames >= 3, "coroutine advanced frames");
        Need(waits >= 1, "WaitForSeconds resumed");
        Need(nested >= 2, "nested IEnumerator completed");
        Need(stopped == 1, "StopCoroutine stopped a routine");
        Need(asyncFrames >= 2, "async NextFrame continuations ran");
    }
}
