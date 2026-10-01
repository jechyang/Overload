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

/// <summary>
/// Flappy Bird drawn as one project Graphic (same edit-mode path as ManagedUI's
/// RedTriangle). OnPopulateMesh builds sky/ground/pipes/bird; OnUpdate runs the
/// simulation only during Play. Edit mode shows the initial frame immediately.
/// </summary>
public class FlappyBirdGame : Graphic
{
    private const float Gravity = -1100f;
    private const float FlapVelocity = 360f;
    private const float PipeSpeed = 150f;
    private const float PipeGap = 150f;
    private const float PipeWidth = 64f;
    private const float PipeSpacing = 210f;
    private const float BirdX = -70f;
    private const float GroundY = -265f;
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
        if (!started) ResetPipes();
        var r = rectTransform.rect;
        // Full-canvas sky.
        AddQuad(helper, r.min.x, r.min.y, r.max.x, r.max.y, new Color(0.45f, 0.78f, 0.95f, 1f));
        // Ground band.
        AddQuad(helper, r.min.x, r.min.y, r.max.x, r.min.y + 80f, new Color(0.86f, 0.74f, 0.42f, 1f));
        // Pipes.
        foreach (var (x, gapY, _) in pipes) {
            float top = gapY + PipeGap * 0.5f, bottom = gapY - PipeGap * 0.5f;
            AddQuad(helper, x - PipeWidth * 0.5f, top, x + PipeWidth * 0.5f, r.max.y, new Color(0.22f, 0.72f, 0.28f, 1f));
            AddQuad(helper, x - PipeWidth * 0.5f, r.min.y + 80f, x + PipeWidth * 0.5f, bottom, new Color(0.16f, 0.55f, 0.22f, 1f));
        }
        // Bird (circle-ish diamond so it reads without textures).
        float bx = BirdX, by = birdY, s = 18f;
        AddQuad(helper, bx - s, by - s, bx + s, by + s, new Color(1f, 0.86f, 0.18f, 1f));
        AddQuad(helper, bx + s - 6f, by - 4f, bx + s + 10f, by + 4f, new Color(0.95f, 0.45f, 0.15f, 1f)); // beak
        // Score pips along the top so the number is visible without Text.
        for (int i = 0; i < Math.Min(m_Score, 12); i++)
            AddQuad(helper, r.min.x + 16f + i * 14f, r.max.y - 36f, r.min.x + 26f + i * 14f, r.max.y - 20f, Color.white);
        if (state == State.Dead)
            AddQuad(helper, -120f, -40f, 120f, 60f, new Color(0.08f, 0.08f, 0.12f, 0.85f));
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

    private void ResetPipes()
    {
        started = true;
        pipes.Clear();
        for (int i = 0; i < PipeCount; i++)
            pipes.Add((380f + i * PipeSpacing, (i % 3) * 40f - 40f, false));
    }

    public override void OnUpdate(float dt)
    {
        if (dt <= 0 || dt > 0.1f) dt = 1f / 60f;
        idleTime += dt;
        if (!started) ResetPipes();

        bool flap = FlapPressed();
        switch (state) {
            case State.Ready:
                birdY = 40f + MathF.Sin(idleTime * 4f) * 12f;
                velocity = 0;
                if (flap) {
                    state = State.Playing;
                    m_Score = 0;
                    velocity = FlapVelocity;
                    ResetPipes();
                }
                break;
            case State.Playing:
                if (flap) velocity = FlapVelocity;
                velocity += Gravity * dt;
                birdY += velocity * dt;
                for (int i = 0; i < pipes.Count; i++) {
                    var (x, gapY, scored) = pipes[i];
                    x -= PipeSpeed * dt;
                    if (!scored && x < BirdX) { m_Score++; scored = true; }
                    if (x < -240f) {
                        float right = 360f;
                        foreach (var p in pipes) if (p.x > right) right = p.x;
                        x = right + PipeSpacing;
                        gapY = ((m_Score + i) % 5) * 48f - 96f;
                        scored = false;
                    }
                    pipes[i] = (x, gapY, scored);
                }
                if (birdY < GroundY + 20f || HitsPipe()) {
                    state = State.Dead;
                    if (m_Score > m_Best) m_Best = m_Score;
                }
                break;
            case State.Dead:
                velocity += Gravity * dt;
                birdY += velocity * dt;
                if (birdY < GroundY + 20f) { birdY = GroundY + 20f; velocity = 0; }
                if (flap) {
                    state = State.Ready;
                    birdY = 40f;
                    velocity = 0;
                    idleTime = 0;
                    ResetPipes();
                }
                break;
        }
        SetVerticesDirty();
    }

    private bool HitsPipe()
    {
        const float birdR = 16f;
        foreach (var (x, gapY, _) in pipes) {
            if (MathF.Abs(x - BirdX) > PipeWidth * 0.5f + birdR) continue;
            if (birdY + birdR > gapY + PipeGap * 0.5f || birdY - birdR < gapY - PipeGap * 0.5f)
                return true;
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
