using System;
using System.Collections.Generic;
using Overload;
using Overload.EventSystems;
using Overload.Maths;
using Overload.UI;

namespace Flappy;

/// <summary>Filled circle graphic used for the bird body.</summary>
public class CircleGraphic : Graphic
{
    [SerializeField] private int m_Segments = 28;
    protected override void OnPopulateMesh(VertexHelper helper)
    {
        helper.Clear();
        var r = rectTransform.rect;
        Color32 tint = color;
        float cx = (r.min.x + r.max.x) * 0.5f, cy = (r.min.y + r.max.y) * 0.5f;
        float rad = MathF.Min(r.width, r.height) * 0.5f;
        helper.AddVert(new Vector3(cx, cy, 0), tint, new Vector2(0.5f, 0.5f));
        int segments = Math.Clamp(m_Segments, 8, 64);
        for (int i = 0; i <= segments; i++) {
            float a = i * MathF.PI * 2f / segments;
            helper.AddVert(new Vector3(cx + MathF.Cos(a) * rad, cy + MathF.Sin(a) * rad, 0), tint,
                new Vector2(0.5f + MathF.Cos(a) * 0.5f, 0.5f + MathF.Sin(a) * 0.5f));
        }
        for (int i = 0; i < segments; i++) helper.AddTriangle(0, i + 1, i + 2);
    }
}

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
/// Flappy Bird on the uGUI runtime: Image/CircleGraphic/Text, EventSystem input,
/// and Behaviour.OnUpdate for simulation. Build happens once from OnEnable.
/// </summary>
public class FlappyBirdGame : Behaviour
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

    private State state = State.Ready;
    private float birdY, velocity, idleTime;
    private int score, best;
    private bool built;
    private Actor? bird, scoreText, messageText, gameOverPanel, gameOverScore, bestScoreText;
    private readonly List<(Actor top, Actor bottom, bool scored)> pipes = new();

    public override void OnEnable()
    {
        base.OnEnable();
        if (!built) Build();
    }

    public override void OnUpdate(float dt)
    {
        if (!built) Build();
        if (dt <= 0 || dt > 0.1f) dt = 1f / 60f;
        idleTime += dt;

        bool flap = FlapPressed();
        switch (state) {
            case State.Ready:
                birdY = 40f + MathF.Sin(idleTime * 4f) * 12f;
                velocity = 0;
                if (flap) StartGame();
                break;
            case State.Playing:
                if (flap) velocity = FlapVelocity;
                velocity += Gravity * dt;
                birdY += velocity * dt;
                ScrollPipes(dt);
                if (birdY < GroundY + 20f || HitsPipe()) Die();
                break;
            case State.Dead:
                velocity += Gravity * dt;
                birdY += velocity * dt;
                if (birdY < GroundY + 20f) { birdY = GroundY + 20f; velocity = 0; }
                if (flap) Restart();
                break;
        }
        PlaceBird();
    }

    private bool FlapPressed()
    {
        var input = EventSystem.GetCurrent(Owner)?.currentInputModule?.input ?? Owner.GetComponent<BaseInput>();
        if (input == null) return false;
        return input.GetKeyDown(32) || input.GetKeyDown(335) || input.GetMouseButtonDown(0) || input.GetMouseButtonDown(1);
    }

    private void StartGame()
    {
        state = State.Playing;
        score = 0;
        velocity = FlapVelocity;
        for (int i = 0; i < pipes.Count; i++) ResetPipe(i, 380f + i * PipeSpacing);
        SetText(scoreText, "0");
        SetText(messageText, "");
        if (gameOverPanel != null) gameOverPanel.Active = false;
    }

    private void Restart()
    {
        state = State.Ready;
        birdY = 40f;
        velocity = 0;
        idleTime = 0;
        SetText(messageText, "Tap / Space to flap");
        if (gameOverPanel != null) gameOverPanel.Active = false;
    }

    private void Die()
    {
        if (state == State.Dead) return;
        state = State.Dead;
        if (score > best) best = score;
        SetText(gameOverScore, $"Score  {score}");
        SetText(bestScoreText, $"Best  {best}");
        SetText(messageText, "Space / Click to retry");
        if (gameOverPanel != null) gameOverPanel.Active = true;
    }

    private bool HitsPipe()
    {
        const float birdR = 16f;
        foreach (var pipe in pipes) {
            var tr = pipe.top.GetComponent<RectTransform>();
            if (tr == null) continue;
            float x = tr.anchoredPosition.x;
            if (MathF.Abs(x - BirdX) > PipeWidth * 0.5f + birdR) continue;
            float gapCenter = GapCenter(pipe.top);
            if (birdY + birdR > gapCenter + PipeGap * 0.5f || birdY - birdR < gapCenter - PipeGap * 0.5f)
                return true;
        }
        return false;
    }

    private static float GapCenter(Actor top)
    {
        var tr = top.GetComponent<RectTransform>();
        if (tr == null) return 0;
        // Top pipe pivot is at the bottom of the top segment (gap top).
        return tr.anchoredPosition.y - PipeGap * 0.5f;
    }

    private void ScrollPipes(float dt)
    {
        for (int i = 0; i < pipes.Count; i++) {
            var (top, bottom, scored) = pipes[i];
            var tr = top.GetComponent<RectTransform>();
            if (tr == null) continue;
            var pos = tr.anchoredPosition;
            pos.x -= PipeSpeed * dt;
            tr.anchoredPosition = pos;
            var br = bottom.GetComponent<RectTransform>();
            if (br != null) {
                var bp = br.anchoredPosition;
                bp.x = pos.x;
                br.anchoredPosition = bp;
            }
            if (!scored && pos.x < BirdX) {
                score++;
                SetText(scoreText, score.ToString());
                pipes[i] = (top, bottom, true);
            }
            if (pos.x < -240f) {
                float rightmost = 360f;
                foreach (var p in pipes) {
                    float x = p.top.GetComponent<RectTransform>()?.anchoredPosition.x ?? 0;
                    if (x > rightmost) rightmost = x;
                }
                ResetPipe(i, rightmost + PipeSpacing);
            }
        }
    }

    private void ResetPipe(int index, float x)
    {
        var (top, bottom, _) = pipes[index];
        float gapCenter = Random.Shared.Next(-120, 130);
        var tr = top.GetComponent<RectTransform>();
        if (tr != null) {
            tr.anchorMin = tr.anchorMax = new Vector2(0.5f, 0.5f);
            tr.pivot = new Vector2(0.5f, 0f); // bottom of top pipe = gap top
            tr.sizeDelta = new Vector2(PipeWidth, 500f);
            tr.anchoredPosition = new Vector2(x, gapCenter + PipeGap * 0.5f);
        }
        var br = bottom.GetComponent<RectTransform>();
        if (br != null) {
            br.anchorMin = br.anchorMax = new Vector2(0.5f, 0.5f);
            br.pivot = new Vector2(0.5f, 1f); // top of bottom pipe = gap bottom
            br.sizeDelta = new Vector2(PipeWidth, 500f);
            br.anchoredPosition = new Vector2(x, gapCenter - PipeGap * 0.5f);
        }
        pipes[index] = (top, bottom, false);
    }

    private void PlaceBird()
    {
        var tr = bird?.GetComponent<RectTransform>();
        if (tr == null) return;
        tr.anchoredPosition = new Vector2(BirdX, birdY);
    }

    private static void SetText(Actor? actor, string value)
    {
        actor?.GetComponent<Text>()?.text = value;
    }

    private static void SetColor(Actor actor, Color color, bool raycast = false)
    {
        var image = actor.GetComponent<Image>();
        if (image != null) { image.color = color; image.raycastTarget = raycast; return; }
        var graphic = actor.GetComponent<Graphic>();
        if (graphic != null) { graphic.color = color; graphic.raycastTarget = raycast; }
    }

    private Actor MakeImage(Actor parent, string name, Color color, float w, float h, Vector2 anchor, Vector2 pos)
    {
        var actor = parent.Create(name);
        var rect = actor.AddComponent<RectTransform>();
        rect.anchorMin = anchor;
        rect.anchorMax = anchor;
        rect.pivot = new Vector2(0.5f, 0.5f);
        rect.sizeDelta = new Vector2(w, h);
        rect.anchoredPosition = pos;
        actor.AddBehaviour<Image>();
        SetColor(actor, color);
        return actor;
    }

    private Actor MakeText(Actor parent, string name, string value, int size, Vector2 anchor, Vector2 pos)
    {
        var actor = parent.Create(name);
        var rect = actor.AddComponent<RectTransform>();
        rect.anchorMin = rect.anchorMax = anchor;
        rect.pivot = new Vector2(0.5f, 0.5f);
        rect.sizeDelta = new Vector2(280, size + 16);
        rect.anchoredPosition = pos;
        actor.AddBehaviour<Text>();
        var text = actor.GetComponent<Text>();
        if (text != null) {
            text.text = value;
            text.fontSize = size;
            text.alignment = TextAnchor.MiddleCenter;
            text.color = Color.white;
            text.raycastTarget = false;
        }
        return actor;
    }

    private void Build()
    {
        built = true;
        if (Owner.GetComponent<Canvas>() != null && Owner.GetComponent<CanvasScaler>() == null)
            Owner.AddBehaviour<FlappyCanvas>();

        if (EventSystem.GetCurrent(Owner) == null) {
            var es = Owner.Create("EventSystem");
            es.AddBehaviour<BaseInput>();
            es.AddBehaviour<StandaloneInputModule>();
            es.AddBehaviour<EventSystem>();
        }

        var sky = new Color(0.45f, 0.78f, 0.95f, 1f);
        var ground = new Color(0.86f, 0.74f, 0.42f, 1f);
        var pipe = new Color(0.22f, 0.72f, 0.28f, 1f);
        var pipeDark = new Color(0.16f, 0.55f, 0.22f, 1f);
        var birdColor = new Color(1f, 0.86f, 0.18f, 1f);

        MakeImage(Owner, "Background", sky, 420, 620, new Vector2(0.5f, 0.5f), Vector2.zero);
        MakeImage(Owner, "Ground", ground, 420, 80, new Vector2(0.5f, 0.5f), new Vector2(0, GroundY));

        for (int i = 0; i < PipeCount; i++) {
            var top = MakeImage(Owner, $"PipeTop{i}", pipe, PipeWidth, 500, new Vector2(0.5f, 0.5f), Vector2.zero);
            var bottom = MakeImage(Owner, $"PipeBottom{i}", pipeDark, PipeWidth, 500, new Vector2(0.5f, 0.5f), Vector2.zero);
            pipes.Add((top, bottom, false));
            ResetPipe(i, 380f + i * PipeSpacing);
        }

        bird = Owner.Create("Bird");
        bird.AddComponent<RectTransform>();
        var birdRect = bird.GetComponent<RectTransform>();
        if (birdRect != null) {
            birdRect.anchorMin = birdRect.anchorMax = new Vector2(0.5f, 0.5f);
            birdRect.pivot = new Vector2(0.5f, 0.5f);
            birdRect.sizeDelta = new Vector2(36, 36);
            birdRect.anchoredPosition = new Vector2(BirdX, birdY);
        }
        bird.AddBehaviour<CircleGraphic>();
        SetColor(bird, birdColor);

        scoreText = MakeText(Owner, "Score", "0", 36, new Vector2(0.5f, 1f), new Vector2(0, -48));
        messageText = MakeText(Owner, "Message", "Tap / Space to flap", 18, new Vector2(0.5f, 0.5f), new Vector2(0, -120));

        gameOverPanel = Owner.Create("GameOver");
        var panelRect = gameOverPanel.AddComponent<RectTransform>();
        if (panelRect != null) {
            panelRect.anchorMin = panelRect.anchorMax = new Vector2(0.5f, 0.5f);
            panelRect.pivot = new Vector2(0.5f, 0.5f);
            panelRect.sizeDelta = new Vector2(280, 200);
            panelRect.anchoredPosition = Vector2.zero;
        }
        gameOverPanel.AddBehaviour<Image>();
        SetColor(gameOverPanel, new Color(0.08f, 0.08f, 0.12f, 0.85f));
        MakeText(gameOverPanel, "Title", "Game Over", 28, new Vector2(0.5f, 1f), new Vector2(0, -40));
        gameOverScore = MakeText(gameOverPanel, "ScoreLabel", "Score  0", 22, new Vector2(0.5f, 0.5f), new Vector2(0, -8));
        bestScoreText = MakeText(gameOverPanel, "BestLabel", "Best  0", 16, new Vector2(0.5f, 0.5f), new Vector2(0, -40));
        MakeText(gameOverPanel, "RetryHint", "Space / Click to retry", 14, new Vector2(0.5f, 0f), new Vector2(0, 36));
        gameOverPanel.Active = false;

        PlaceBird();
    }
}
