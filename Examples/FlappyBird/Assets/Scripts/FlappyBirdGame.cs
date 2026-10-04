using System;
using Overload;
using Overload.EventSystems;
using Overload.Maths;
using Overload.UI;

namespace Flappy;

public class FlappyCanvas : CanvasScaler
{
    public FlappyCanvas()
    {
        uiScaleMode = ScaleMode.ScaleWithScreenSize;
        referenceResolution = new Vector2(400, 600);
        screenMatchMode = ScreenMatchMode.Expand;
    }
}

// Input belongs to the Play lifecycle. Rendering the edit-mode preview must
// not create actors which then get serialized into the user's scene.
public class FlappyInputBootstrap : Overload.EventSystems.UIBehaviour
{
    public override void OnStart() => EnsureInput();
    public override void OnReloadRestore() => EnsureInput();
    private void EnsureInput()
    {
        if (EventSystem.GetCurrent(Owner) != null) return;
        var events = Owner.Find("EventSystem") ?? Owner.Create("EventSystem");
        events.AddBehaviour<BaseInput>();
        events.AddBehaviour<StandaloneInputModule>();
        events.AddBehaviour<EventSystem>();
    }
}

/// <summary>A fixed 400x600 playfield in rect-local coordinates, independent of the viewport aspect ratio.</summary>
public class FlappyBirdGame : Graphic
{
    private const float Gravity = -1100, FlapVelocity = 360, PipeSpeed = 150;
    private const float PipeGap = 170, PipeWidth = 64, PipeSpacing = 230;
    private const float Left = -200, Right = 200, Bottom = -300, Top = 300;
    private const float GroundTop = -220, BirdX = -100, BirdRadius = 16;
    private enum State { Ready, Playing, Dead }
    private struct Pipe { public float x, gapY; public bool scored; }
    [SerializeField] private int m_Score, m_Best;
    private State state;
    private float birdY, velocity, idleTime;
    private bool initialized;
    private ulong lastInput;
    private readonly Pipe[] pipes = new Pipe[3];

    public int Score => m_Score;
    public int Best => m_Best;
    public bool IsReady => state == State.Ready;
    public bool IsGameOver => state == State.Dead;

    public override void OnStart() => ResetRound();
    public override void OnReloadRestore() { ResetRound(); SetAllDirty(); }
    private void ResetRound()
    {
        initialized = true;
        state = State.Ready;
        m_Score = 0;
        birdY = velocity = idleTime = 0;
        // First pipe is ahead of the bird, allowing more than a second to react.
        for (int i = 0; i < pipes.Length; ++i)
            pipes[i] = new Pipe { x = 170 + i * PipeSpacing, gapY = Gap(i) };
        SetVerticesDirty();
    }
    private static float Gap(int index) => ((index % 5) - 2) * 25;

    public override void OnUpdate(float dt)
    {
        if (!float.IsFinite(dt) || dt <= 0) return;
        if (!initialized) ResetRound();
        var system = EventSystem.GetCurrent(Owner);
        var input = system?.currentInputModule?.input ?? system?.Owner.GetComponent<BaseInput>();
        if (input == null) return;
        // Capture the current native tick here; Actor iteration order must not
        // delay gameplay input until EventSystem.OnUpdate in a later frame.
        var frame = input.Capture();
        bool fresh = frame.sequence != lastInput;
        lastInput = frame.sequence;
        if (!frame.focused) return;
        bool flap = fresh && (input.GetKeyDown(32) || input.GetKeyDown(257) ||
            input.GetKeyDown(335) || input.GetMouseButtonDown(0));
        if (flap && state == State.Dead) { ResetRound(); return; }
        if (state == State.Ready)
        {
            idleTime += Math.Min(dt, .1f);
            birdY = MathF.Sin(idleTime * 4) * 12;
            if (flap) { state = State.Playing; birdY = 0; velocity = FlapVelocity; }
            SetVerticesDirty();
            return;
        }
        if (flap) velocity = FlapVelocity;
        if (state == State.Dead && birdY <= GroundTop + BirdRadius) return;
        // Bound catch-up and subdivide slow frames to keep collision and gravity stable.
        for (float remaining = Math.Min(dt, .1f); remaining > 0;)
        {
            float step = Math.Min(remaining, 1f / 120);
            remaining -= step;
            velocity += Gravity * step;
            birdY += velocity * step;
            if (state == State.Dead)
            {
                birdY = Math.Max(birdY, GroundTop + BirdRadius);
                continue;
            }
            for (int i = 0; i < pipes.Length; ++i) pipes[i].x -= PipeSpeed * step;
            if (birdY - BirdRadius <= GroundTop || birdY + BirdRadius >= Top || HitsPipe())
            {
                state = State.Dead;
                m_Best = Math.Max(m_Best, m_Score);
                continue;
            }
            for (int i = 0; i < pipes.Length; ++i)
            {
                ref var pipe = ref pipes[i];
                // Award only after the whole bird clears the trailing edge.
                if (!pipe.scored && pipe.x + PipeWidth * .5f < BirdX - BirdRadius)
                { ++m_Score; pipe.scored = true; }
                if (pipe.x + PipeWidth * .5f < Left)
                {
                    float rightmost = Right;
                    foreach (var other in pipes) rightmost = Math.Max(rightmost, other.x);
                    pipe.x = rightmost + PipeSpacing;
                    pipe.gapY = Gap(m_Score + pipes.Length - 1);
                    pipe.scored = false;
                }
            }
        }
        SetVerticesDirty();
    }
    private bool HitsPipe()
    {
        foreach (var pipe in pipes)
            if (MathF.Abs(pipe.x - BirdX) < PipeWidth * .5f + BirdRadius &&
                (birdY + BirdRadius > pipe.gapY + PipeGap * .5f || birdY - BirdRadius < pipe.gapY - PipeGap * .5f)) return true;
        return false;
    }
    protected override void OnPopulateMesh(VertexHelper helper)
    {
        helper.Clear();
        if (!initialized) ResetRound();
        var r = rectTransform.rect;
        AddQuad(helper, r.min.x, r.min.y, r.max.x, r.max.y, new Color(.10f, .18f, .25f, 1));
        AddQuad(helper, Left, Bottom, Right, Top, new Color(.45f, .78f, .95f, 1));
        // Small cloud bands and a distant skyline.
        for (int i = 0; i < 4; ++i)
        {
            float x = Left + 25 + i * 105;
            AddQuad(helper, x, 170 + i % 2 * 35, Math.Min(x + 65, Right), 182 + i % 2 * 35, new Color(.82f, .93f, 1, 1));
            AddQuad(helper, x, GroundTop, Math.Min(x + 55, Right), GroundTop + 32 + i % 2 * 20, new Color(.36f, .66f, .73f, 1));
        }
        foreach (var pipe in pipes)
        {
            float x0 = Math.Max(Left, pipe.x - PipeWidth * .5f), x1 = Math.Min(Right, pipe.x + PipeWidth * .5f);
            if (x1 <= x0) continue; // Do not upload off-screen pipe geometry.
            float low = pipe.gapY - PipeGap * .5f, high = pipe.gapY + PipeGap * .5f;
            AddQuad(helper, x0, high, x1, Top, new Color(.22f, .72f, .28f, 1));
            AddQuad(helper, x0, GroundTop, x1, low, new Color(.16f, .55f, .22f, 1));
            AddQuad(helper, x0, high, x1, high + 12, new Color(.12f, .43f, .19f, 1));
            AddQuad(helper, x0, low - 12, x1, low, new Color(.12f, .43f, .19f, 1));
        }
        AddQuad(helper, Left, Bottom, Right, GroundTop, new Color(.86f, .74f, .42f, 1));
        AddQuad(helper, Left, GroundTop - 8, Right, GroundTop, new Color(.52f, .72f, .30f, 1));
        AddCircle(helper, BirdX, birdY, 18, new Color(1, .86f, .18f, 1));
        AddCircle(helper, BirdX - 8, birdY - 3, 8, new Color(.96f, .64f, .12f, 1));
        AddQuad(helper, BirdX + 12, birdY - 5, BirdX + 28, birdY + 3, new Color(.95f, .45f, .15f, 1));
        AddCircle(helper, BirdX + 7, birdY + 7, 6, Color.white);
        AddCircle(helper, BirdX + 9, birdY + 7, 2.5f, new Color(.08f, .12f, .18f, 1));
        if (state == State.Dead)
            AddQuad(helper, -155, -65, 155, 65, new Color(.08f, .12f, .18f, .94f));
    }
    private static void AddCircle(VertexHelper helper, float x, float y, float radius, Color color)
    {
        int start = helper.currentVertCount;
        helper.AddVert(new Vector3(x, y, 0), color, Vector2.zero);
        const int segments = 20;
        for (int n = 0; n <= segments; ++n)
        {
            float angle = n * (2 * MathF.PI / segments);
            helper.AddVert(new Vector3(x + MathF.Cos(angle) * radius, y + MathF.Sin(angle) * radius, 0), color, Vector2.zero);
            if (n > 0) helper.AddTriangle(start, start + n, start + n + 1);
        }
    }
    private static void AddQuad(VertexHelper helper, float x0, float y0, float x1, float y1, Color color)
    {
        int i = helper.currentVertCount;
        helper.AddVert(new Vector3(x0, y0, 0), color, new Vector2(0, 0));
        helper.AddVert(new Vector3(x1, y0, 0), color, new Vector2(1, 0));
        helper.AddVert(new Vector3(x1, y1, 0), color, new Vector2(1, 1));
        helper.AddVert(new Vector3(x0, y1, 0), color, new Vector2(0, 1));
        helper.AddTriangle(i, i + 1, i + 2);
        helper.AddTriangle(i + 2, i + 3, i);
    }
}

// Real uGUI Text components; unchanged strings do not rebuild their meshes.
public class FlappyScore : Text
{
    public FlappyScore() { text = "0"; fontSize = 38; alignment = TextAnchor.MiddleCenter; raycastTarget = false; }
    public override void OnUpdate(float dt)
    { if (Owner.Parent?.GetComponent<FlappyBirdGame>() is { } game) text = game.Score.ToString(); }
}
public class FlappyHint : Text
{
    public FlappyHint() { text = "SPACE / CLICK TO START"; fontSize = 18; alignment = TextAnchor.MiddleCenter; raycastTarget = false; }
    public override void OnUpdate(float dt)
    { if (Owner.Parent?.GetComponent<FlappyBirdGame>() is { } game) text = game.IsReady ? "SPACE / CLICK TO START" : game.IsGameOver ? "SPACE / CLICK TO RESET" : "SPACE / CLICK TO FLAP"; }
}
public class FlappyResult : Text
{
    public FlappyResult() { fontSize = 26; alignment = TextAnchor.MiddleCenter; raycastTarget = false; }
    public override void OnUpdate(float dt)
    { if (Owner.Parent?.GetComponent<FlappyBirdGame>() is { } game) text = game.IsGameOver ? $"GAME OVER\nScore {game.Score}   Best {game.Best}" : ""; }
}
