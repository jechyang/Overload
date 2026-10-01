using System;
using System.Collections.Generic;
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
        matchWidthOrHeight = 0.5f;
    }
}

/// <summary>Creates EventSystem/input so Play-mode flaps work without manual wiring.</summary>
public class FlappyInputBootstrap : Overload.EventSystems.UIBehaviour
{
    private bool ready;
    protected override void OnRectTransformDimensionsChange()
    {
        base.OnRectTransformDimensionsChange();
        EnsureInput();
    }
    public override void OnEnable()
    {
        base.OnEnable();
        EnsureInput();
    }
    private void EnsureInput()
    {
        if (ready) return;
        if (EventSystem.GetCurrent(Owner) != null) { ready = true; return; }
        var es = Owner.Create("EventSystem");
        es.AddBehaviour<BaseInput>();
        es.AddBehaviour<StandaloneInputModule>();
        es.AddBehaviour<EventSystem>();
        ready = true;
    }
}

/// <summary>
/// Flappy Bird as one project Graphic (edit-mode visible like ManagedUI RedTriangle).
/// All geometry is in rectTransform local space: rect.min .. rect.max.
/// </summary>
public class FlappyBirdGame : Graphic
{
    private const float Gravity = -1100f;
    private const float FlapVelocity = 360f;
    private const float PipeSpeed = 150f;
    private const float PipeGap = 150f;
    private const float PipeWidth = 64f;
    private const float PipeSpacing = 210f;
    private const float GroundHeight = 80f;
    private const int PipeCount = 5;

    private enum State { Ready, Playing, Dead }

    [SerializeField] private int m_Score;
    [SerializeField] private int m_Best;
    private State state = State.Ready;
    private float birdY, velocity, idleTime;
    private bool started;
    private readonly List<(float x, float gapY, bool scored)> pipes = new();

    protected override void OnPopulateMesh(VertexHelper helper)
    {
        helper.Clear();
        var r = rectTransform.rect;
        if (!started) ResetPipes(r);

        // Sky fills the whole rect.
        AddQuad(helper, r.min.x, r.min.y, r.max.x, r.max.y, new Color(0.45f, 0.78f, 0.95f, 1f));
        // Ground band along the bottom of the rect.
        AddQuad(helper, r.min.x, r.min.y, r.max.x, r.min.y + GroundHeight, new Color(0.86f, 0.74f, 0.42f, 1f));

        float birdX = r.min.x + 100f;
        foreach (var (x, gapY, _) in pipes) {
            float top = gapY + PipeGap * 0.5f, bottom = gapY - PipeGap * 0.5f;
            AddQuad(helper, x - PipeWidth * 0.5f, top, x + PipeWidth * 0.5f, r.max.y, new Color(0.22f, 0.72f, 0.28f, 1f));
            AddQuad(helper, x - PipeWidth * 0.5f, r.min.y + GroundHeight, x + PipeWidth * 0.5f, bottom, new Color(0.16f, 0.55f, 0.22f, 1f));
        }

        // Bird.
        float s = 18f, by = birdY;
        AddQuad(helper, birdX - s, by - s, birdX + s, by + s, new Color(1f, 0.86f, 0.18f, 1f));
        AddQuad(helper, birdX + s - 2f, by - 5f, birdX + s + 12f, by + 5f, new Color(0.95f, 0.45f, 0.15f, 1f));

        // Score marks along the top.
        for (int i = 0; i < Math.Min(m_Score, 12); i++) {
            float x0 = r.min.x + 20f + i * 16f;
            AddQuad(helper, x0, r.max.y - 40f, x0 + 12f, r.max.y - 22f, Color.white);
        }
        if (state == State.Dead) {
            float cx = (r.min.x + r.max.x) * 0.5f, cy = (r.min.y + r.max.y) * 0.5f;
            AddQuad(helper, cx - 110f, cy - 50f, cx + 110f, cy + 50f, new Color(0.08f, 0.08f, 0.12f, 0.85f));
        }
    }

    private static void AddQuad(VertexHelper helper, float x0, float y0, float x1, float y1, Color color)
    {
        Color32 tint = color;
        int i = helper.currentVertCount;
        helper.AddVert(new Vector3(x0, y0, 0), tint, new Vector2(0, 0));
        helper.AddVert(new Vector3(x1, y0, 0), tint, new Vector2(1, 0));
        helper.AddVert(new Vector3(x1, y1, 0), tint, new Vector2(1, 1));
        helper.AddVert(new Vector3(x0, y1, 0), tint, new Vector2(0, 1));
        helper.AddTriangle(i, i + 1, i + 2);
        helper.AddTriangle(i + 2, i + 3, i);
    }

    private void ResetPipes(Rect r)
    {
        started = true;
        pipes.Clear();
        // Keep pipes inside the rect so the edit-mode preview shows the board.
        // Local X is relative to the canvas pivot; visible range is roughly r.min.x .. r.max.x.
        for (int i = 0; i < PipeCount; i++) {
            float x = r.min.x + 90f + i * 130f;
            float gapY = (r.min.y + r.max.y) * 0.5f + ((i % 3) - 1) * 40f;
            pipes.Add((x, gapY, false));
        }
        birdY = (r.min.y + r.max.y) * 0.5f;
    }

    public override void OnUpdate(float dt)
    {
        if (dt <= 0 || dt > 0.1f) dt = 1f / 60f;
        idleTime += dt;
        var r = rectTransform.rect;
        if (!started) ResetPipes(r);

        float groundTop = r.min.y + GroundHeight;
        float birdX = r.min.x + 100f;
        float minY = groundTop + 20f, maxY = r.max.y - 20f;

        bool flap = FlapPressed();
        switch (state) {
            case State.Ready:
                birdY = (r.min.y + r.max.y) * 0.5f + MathF.Sin(idleTime * 4f) * 16f;
                velocity = 0;
                if (flap) {
                    state = State.Playing;
                    m_Score = 0;
                    velocity = FlapVelocity;
                    ResetPipes(r);
                    birdY = (r.min.y + r.max.y) * 0.5f;
                }
                break;
            case State.Playing:
                if (flap) velocity = FlapVelocity;
                velocity += Gravity * dt;
                birdY += velocity * dt;
                for (int i = 0; i < pipes.Count; i++) {
                    var (x, gapY, scored) = pipes[i];
                    x -= PipeSpeed * dt;
                    if (!scored && x < birdX) { m_Score++; scored = true; }
                    if (x < r.min.x - 80f) {
                        float right = r.max.x;
                        foreach (var p in pipes) if (p.x > right) right = p.x;
                        x = right + 130f;
                        gapY = (r.min.y + r.max.y) * 0.5f + (((m_Score + i) % 5) - 2) * 36f;
                        scored = false;
                    }
                    pipes[i] = (x, gapY, scored);
                }
                if (birdY < minY || birdY > maxY || HitsPipe(birdX, groundTop, r)) {
                    state = State.Dead;
                    if (m_Score > m_Best) m_Best = m_Score;
                }
                break;
            case State.Dead:
                velocity += Gravity * dt;
                birdY += velocity * dt;
                if (birdY < minY) { birdY = minY; velocity = 0; }
                if (flap) {
                    state = State.Ready;
                    idleTime = 0;
                    ResetPipes(r);
                }
                break;
        }
        SetVerticesDirty();
    }

    private bool HitsPipe(float birdX, float groundTop, Rect r)
    {
        const float birdR = 16f;
        foreach (var (x, gapY, _) in pipes) {
            if (MathF.Abs(x - birdX) > PipeWidth * 0.5f + birdR) continue;
            if (birdY + birdR > gapY + PipeGap * 0.5f || birdY - birdR < gapY - PipeGap * 0.5f)
                return true;
            if (birdY - birdR < groundTop) return true;
            if (birdY + birdR > r.max.y) return true;
        }
        return false;
    }

    private bool FlapPressed()
    {
        var input = EventSystem.GetCurrent(Owner)?.currentInputModule?.input ?? Owner.GetComponent<BaseInput>();
        if (input == null) return false;
        return input.GetKeyDown(32) || input.GetKeyDown(335) || input.GetMouseButtonDown(0) || input.GetMouseButtonDown(1);
    }
}
