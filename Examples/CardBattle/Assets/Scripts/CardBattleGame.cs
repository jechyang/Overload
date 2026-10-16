using System;
using System.Collections.Generic;
using Overload;
using Overload.EventSystems;
using Overload.Maths;
using Overload.UI;

namespace CardBattle;

public class CardBattleCanvas : CanvasScaler
{
    public CardBattleCanvas()
    {
        uiScaleMode = ScaleMode.ScaleWithScreenSize;
        referenceResolution = new Vector2(960, 540);
        screenMatchMode = ScreenMatchMode.MatchWidthOrHeight;
        matchWidthOrHeight = 0.5f;
    }
}

public class CardBattleInputBootstrap : UIBehaviour
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

public enum CardType { Attack, Defend, Skill }

public class Card
{
    public string Id = "";
    public string Name = "";
    public int Cost = 1;
    public CardType Type = CardType.Attack;
    public int Value = 0;
    public int Extra = 0;
    public Color Theme = Color.red;
    public string Desc = "";

    public Card Clone() => new Card
    {
        Id = Id, Name = Name, Cost = Cost, Type = Type,
        Value = Value, Extra = Extra, Theme = Theme, Desc = Desc
    };
}

public enum EnemyIntentType { Attack, Defend, HeavyAttack, Heal }

public class Enemy
{
    public string Name = "King Slime";
    public int MaxHp = 36;
    public int Hp = 36;
    public int Block = 0;
    public EnemyIntentType Intent = EnemyIntentType.Attack;
    public int IntentValue = 7;
    public Color Color = new Color(0.2f, 0.85f, 0.45f, 1f);
}

public class FloatingText
{
    public string Text = "";
    public float X, Y;
    public Color Color = Color.white;
    public float Life = 1.0f;
    public float MaxLife = 1.0f;
}

public class CardBattleGame : Graphic
{
    public enum GameState { PlayerTurn, EnemyTurn, Victory, Defeat }

    // Constants
    private const float CanvasW = 960, CanvasH = 540;
    private const int MaxPlayerHp = 50;

    // Player State
    public int PlayerHp { get; private set; } = 50;
    public int PlayerBlock { get; private set; } = 0;
    public int PlayerEnergy { get; private set; } = 3;
    public int Turn { get; private set; } = 1;
    public int Stage { get; private set; } = 1;

    // Enemy State
    public Enemy CurrentEnemy { get; private set; } = new Enemy();

    // Game Flow
    public GameState State { get; private set; } = GameState.PlayerTurn;
    public string BannerMessage { get; private set; } = "⚔️ HERO TURN 1";
    public string LastCombatLog { get; private set; } = "Prepare for battle! Choose a card to strike.";

    // Decks
    private readonly List<Card> m_DrawPile = new();
    private readonly List<Card> m_Hand = new();
    private readonly List<Card> m_DiscardPile = new();

    // Floating text list
    private readonly List<FloatingText> m_Floats = new();

    // Visuals & Animations
    private float m_AnimTime = 0f;
    private float m_EnemyFlashTimer = 0f;
    private float m_PlayerFlashTimer = 0f;
    private float m_EnemyTurnDelay = 0f;
    private int m_HoveredCardIndex = -1;
    private bool m_HoveredEndTurn = false;
    private ulong m_LastInputSeq = 0;
    private readonly Random m_Rnd = new Random(42);

    public int HandCount => m_Hand.Count;
    public int DrawPileCount => m_DrawPile.Count;
    public int DiscardPileCount => m_DiscardPile.Count;

    public override void OnStart() => InitBattle(true);
    public override void OnReloadRestore() { InitBattle(true); SetAllDirty(); }

    public void InitBattle(bool resetPlayer)
    {
        if (resetPlayer)
        {
            PlayerHp = MaxPlayerHp;
            Stage = 1;
        }
        PlayerBlock = 0;
        PlayerEnergy = 3;
        Turn = 1;
        State = GameState.PlayerTurn;

        SetupEnemyForStage(Stage);

        // Build Master Deck
        m_DrawPile.Clear();
        m_Hand.Clear();
        m_DiscardPile.Clear();
        m_Floats.Clear();

        BuildStartingDeck();
        Shuffle(m_DrawPile);

        DrawCards(4);

        BannerMessage = $"⚔️ BATTLE START - STAGE {Stage}";
        LastCombatLog = $"Encountered {CurrentEnemy.Name}! Defeat it to proceed.";
        SetVerticesDirty();
    }

    private void BuildStartingDeck()
    {
        // 4x Strike
        for (int i = 0; i < 4; ++i)
            m_DrawPile.Add(new Card { Id = "strike", Name = "斩击", Cost = 1, Type = CardType.Attack, Value = 7, Theme = new Color(0.9f, 0.3f, 0.25f, 1f), Desc = "造成 7 点伤害" });
        // 4x Defend
        for (int i = 0; i < 4; ++i)
            m_DrawPile.Add(new Card { Id = "defend", Name = "铁壁", Cost = 1, Type = CardType.Defend, Value = 6, Theme = new Color(0.25f, 0.55f, 0.95f, 1f), Desc = "获得 6 点护盾" });
        // 2x Fireball
        for (int i = 0; i < 2; ++i)
            m_DrawPile.Add(new Card { Id = "fireball", Name = "炎爆术", Cost = 2, Type = CardType.Attack, Value = 16, Theme = new Color(1.0f, 0.55f, 0.1f, 1f), Desc = "造成 16 点巨额伤害" });
        // 1x QuickSlash
        m_DrawPile.Add(new Card { Id = "quick", Name = "连击", Cost = 1, Type = CardType.Attack, Value = 4, Extra = 1, Theme = new Color(0.2f, 0.85f, 0.8f, 1f), Desc = "造成 4 伤并摸 1 牌" });
        // 1x HolyLight
        m_DrawPile.Add(new Card { Id = "heal", Name = "圣光", Cost = 1, Type = CardType.Skill, Value = 8, Theme = new Color(0.95f, 0.85f, 0.3f, 1f), Desc = "恢复 8 点生命" });
    }

    private void SetupEnemyForStage(int stage)
    {
        CurrentEnemy = new Enemy();
        if (stage == 1)
        {
            CurrentEnemy.Name = "史莱姆王 (King Slime)";
            CurrentEnemy.MaxHp = CurrentEnemy.Hp = 36;
            CurrentEnemy.Color = new Color(0.25f, 0.85f, 0.45f, 1f);
            CurrentEnemy.Intent = EnemyIntentType.Attack;
            CurrentEnemy.IntentValue = 7;
        }
        else if (stage == 2)
        {
            CurrentEnemy.Name = "哥布林督军 (Goblin Warlord)";
            CurrentEnemy.MaxHp = CurrentEnemy.Hp = 52;
            CurrentEnemy.Color = new Color(0.9f, 0.55f, 0.2f, 1f);
            CurrentEnemy.Intent = EnemyIntentType.HeavyAttack;
            CurrentEnemy.IntentValue = 12;
        }
        else
        {
            CurrentEnemy.Name = "远古赤龙 (Ancient Red Dragon)";
            CurrentEnemy.MaxHp = CurrentEnemy.Hp = 78;
            CurrentEnemy.Color = new Color(0.95f, 0.25f, 0.25f, 1f);
            CurrentEnemy.Intent = EnemyIntentType.Attack;
            CurrentEnemy.IntentValue = 15;
        }
    }

    private void DrawCards(int count)
    {
        for (int i = 0; i < count; ++i)
        {
            if (m_DrawPile.Count == 0)
            {
                if (m_DiscardPile.Count == 0) break;
                m_DrawPile.AddRange(m_DiscardPile);
                m_DiscardPile.Clear();
                Shuffle(m_DrawPile);
                AddFloatingText("洗牌!", -380, -110, new Color(0.8f, 0.9f, 1f, 1f));
            }
            if (m_DrawPile.Count > 0 && m_Hand.Count < 6)
            {
                m_Hand.Add(m_DrawPile[0]);
                m_DrawPile.RemoveAt(0);
            }
        }
    }

    private void Shuffle<T>(List<T> list)
    {
        for (int i = list.Count - 1; i > 0; --i)
        {
            int k = m_Rnd.Next(i + 1);
            (list[i], list[k]) = (list[k], list[i]);
        }
    }

    public override void OnUpdate(float dt)
    {
        if (!float.IsFinite(dt) || dt <= 0) return;
        m_AnimTime += dt;

        if (m_EnemyFlashTimer > 0) m_EnemyFlashTimer -= dt;
        if (m_PlayerFlashTimer > 0) m_PlayerFlashTimer -= dt;

        // Update floating texts
        for (int i = m_Floats.Count - 1; i >= 0; --i)
        {
            var ft = m_Floats[i];
            ft.Life -= dt;
            ft.Y += 45f * dt;
            if (ft.Life <= 0) m_Floats.RemoveAt(i);
        }

        // Process Enemy Turn Delay
        if (State == GameState.EnemyTurn)
        {
            m_EnemyTurnDelay -= dt;
            if (m_EnemyTurnDelay <= 0)
            {
                ExecuteEnemyAction();
            }
            SetVerticesDirty();
            return;
        }

        // Input Handling
        var system = EventSystem.GetCurrent(Owner);
        var input = system?.currentInputModule?.input ?? system?.Owner.GetComponent<BaseInput>();
        if (input == null) { SetVerticesDirty(); return; }

        var frame = input.Capture();
        bool fresh = frame.sequence != m_LastInputSeq;
        m_LastInputSeq = frame.sequence;
        if (!frame.focused) { SetVerticesDirty(); return; }

        // Convert mouse position to local canvas
        Vector2 mouseLocal = Vector2.zero;
        bool hasMouse = RectTransformUtility.ScreenPointToLocalPointInRectangle(rectTransform, frame.mousePosition, null, out mouseLocal);

        // Detect Card hover
        m_HoveredCardIndex = -1;
        m_HoveredEndTurn = false;

        if (hasMouse)
        {
            // End Turn Button bounds: X in [290, 430], Y in [-185, -135]
            if (mouseLocal.x >= 290 && mouseLocal.x <= 430 && mouseLocal.y >= -185 && mouseLocal.y <= -135)
                m_HoveredEndTurn = true;

            // Hand cards bounds
            float startX = -(m_Hand.Count * 135f) * 0.5f + 65f;
            for (int i = 0; i < m_Hand.Count; ++i)
            {
                float cx = startX + i * 135f;
                float cy = -155f;
                if (mouseLocal.x >= cx - 60 && mouseLocal.x <= cx + 60 && mouseLocal.y >= cy - 85 && mouseLocal.y <= cy + 85)
                {
                    m_HoveredCardIndex = i;
                }
            }
        }

        // Click / Shortcuts
        bool click = fresh && input.GetMouseButtonDown(0);

        // Restart on Victory / Defeat
        if (State == GameState.Victory || State == GameState.Defeat)
        {
            if (click || (fresh && (input.GetKeyDown(32) || input.GetKeyDown(257) || input.GetKeyDown(82))))
            {
                if (State == GameState.Victory)
                {
                    Stage++;
                    if (Stage > 3) Stage = 1;
                    PlayerHp = Math.Min(MaxPlayerHp, PlayerHp + 15);
                    InitBattle(false);
                }
                else
                {
                    InitBattle(true);
                }
            }
            SetVerticesDirty();
            return;
        }

        // Play card by click
        if (click && m_HoveredCardIndex >= 0 && m_HoveredCardIndex < m_Hand.Count)
        {
            PlayCard(m_HoveredCardIndex);
        }
        else if (click && m_HoveredEndTurn)
        {
            EndPlayerTurn();
        }

        // Keyboard shortcuts (1, 2, 3, 4, 5, 6)
        for (int k = 0; k < Math.Min(6, m_Hand.Count); ++k)
        {
            if (fresh && (input.GetKeyDown(49 + k) || input.GetKeyDown(321 + k))) // '1'..'6' and numpad
            {
                PlayCard(k);
                break;
            }
        }

        // Space / E / Enter -> End Turn
        if (fresh && (input.GetKeyDown(32) || input.GetKeyDown(69) || input.GetKeyDown(257)))
        {
            EndPlayerTurn();
        }

        // R -> Restart
        if (fresh && input.GetKeyDown(82))
        {
            InitBattle(true);
        }

        SetVerticesDirty();
    }

    public void PlayCard(int index)
    {
        if (State != GameState.PlayerTurn) return;
        if (index < 0 || index >= m_Hand.Count) return;

        var card = m_Hand[index];
        if (PlayerEnergy < card.Cost)
        {
            AddFloatingText("能量不足!", -400, -120, Color.red);
            return;
        }

        // Deduct energy & remove from hand
        PlayerEnergy -= card.Cost;
        m_Hand.RemoveAt(index);
        m_DiscardPile.Add(card);

        // Execute Card Effect
        if (card.Type == CardType.Attack)
        {
            int damage = card.Value;
            int dealt = ApplyDamageToEnemy(damage);
            AddFloatingText($"-{dealt}", 260, 60, new Color(1f, 0.3f, 0.2f, 1f));
            m_EnemyFlashTimer = 0.25f;
            LastCombatLog = $"打出【{card.Name}】，对敌方造成 {dealt} 点伤害！";

            if (card.Extra > 0) // e.g. draw card
            {
                DrawCards(card.Extra);
                AddFloatingText($"+{card.Extra} 牌", 0, -80, new Color(0.2f, 0.85f, 1f, 1f));
            }
        }
        else if (card.Type == CardType.Defend)
        {
            PlayerBlock += card.Value;
            AddFloatingText($"+{card.Value} 🛡️", -260, 60, new Color(0.3f, 0.6f, 1f, 1f));
            LastCombatLog = $"打出【{card.Name}】，获得 {card.Value} 点护盾！";
        }
        else if (card.Type == CardType.Skill)
        {
            if (card.Value > 0) // Heal
            {
                int heal = Math.Min(MaxPlayerHp - PlayerHp, card.Value);
                PlayerHp += heal;
                AddFloatingText($"+{heal} 💚", -260, 60, Color.green);
                LastCombatLog = $"打出【{card.Name}】，恢复了 {heal} 点生命值！";
            }
        }

        // Check if Enemy Defeated
        if (CurrentEnemy.Hp <= 0)
        {
            State = GameState.Victory;
            BannerMessage = "🏆 战斗胜利！VICTORY!";
            LastCombatLog = $"击溃了【{CurrentEnemy.Name}】！点击或按 [空格] 进入下一关。";
            AddFloatingText("VICTORY!", 0, 80, new Color(1f, 0.85f, 0.2f, 1f), 2.5f);
        }
    }

    private int ApplyDamageToEnemy(int damage)
    {
        if (CurrentEnemy.Block > 0)
        {
            if (CurrentEnemy.Block >= damage)
            {
                CurrentEnemy.Block -= damage;
                return damage;
            }
            int remain = damage - CurrentEnemy.Block;
            CurrentEnemy.Block = 0;
            CurrentEnemy.Hp = Math.Max(0, CurrentEnemy.Hp - remain);
            return damage;
        }
        CurrentEnemy.Hp = Math.Max(0, CurrentEnemy.Hp - damage);
        return damage;
    }

    private int ApplyDamageToPlayer(int damage)
    {
        if (PlayerBlock > 0)
        {
            if (PlayerBlock >= damage)
            {
                PlayerBlock -= damage;
                return damage;
            }
            int remain = damage - PlayerBlock;
            PlayerBlock = 0;
            PlayerHp = Math.Max(0, PlayerHp - remain);
            return damage;
        }
        PlayerHp = Math.Max(0, PlayerHp - damage);
        return damage;
    }

    public void EndPlayerTurn()
    {
        if (State != GameState.PlayerTurn) return;

        // Discard rest of hand
        m_DiscardPile.AddRange(m_Hand);
        m_Hand.Clear();

        State = GameState.EnemyTurn;
        m_EnemyTurnDelay = 0.7f;
        BannerMessage = "👾 敌方行动中... ENEMY TURN";
        LastCombatLog = $"{CurrentEnemy.Name} 准备执行行动！";
    }

    private void ExecuteEnemyAction()
    {
        if (CurrentEnemy.Intent == EnemyIntentType.Attack || CurrentEnemy.Intent == EnemyIntentType.HeavyAttack)
        {
            int damage = CurrentEnemy.IntentValue;
            int dealt = ApplyDamageToPlayer(damage);
            AddFloatingText($"-{dealt}", -260, 60, Color.red);
            m_PlayerFlashTimer = 0.3f;
            LastCombatLog = $"{CurrentEnemy.Name} 发动猛攻，对你造成 {dealt} 点伤害！";
        }
        else if (CurrentEnemy.Intent == EnemyIntentType.Defend)
        {
            CurrentEnemy.Block += CurrentEnemy.IntentValue;
            AddFloatingText($"+{CurrentEnemy.IntentValue} 🛡️", 260, 60, new Color(0.3f, 0.6f, 1f, 1f));
            LastCombatLog = $"{CurrentEnemy.Name} 获得 {CurrentEnemy.IntentValue} 点护盾！";
        }
        else if (CurrentEnemy.Intent == EnemyIntentType.Heal)
        {
            int heal = Math.Min(CurrentEnemy.MaxHp - CurrentEnemy.Hp, CurrentEnemy.IntentValue);
            CurrentEnemy.Hp += heal;
            AddFloatingText($"+{heal} 💚", 260, 60, Color.green);
            LastCombatLog = $"{CurrentEnemy.Name} 自愈恢复了 {heal} 点生命！";
        }

        // Check if player died
        if (PlayerHp <= 0)
        {
            State = GameState.Defeat;
            BannerMessage = "💀 战败！DEFEAT...";
            LastCombatLog = "你倒在了地牢深处... 点击或按 [R] 重新挑战！";
            AddFloatingText("DEFEAT!", 0, 80, Color.red, 2.5f);
            return;
        }

        // Roll next intent for enemy
        RollEnemyNextIntent();

        // Start Next Player Turn
        Turn++;
        PlayerEnergy = 3;
        PlayerBlock = 0;
        CurrentEnemy.Block = 0;
        DrawCards(4);

        State = GameState.PlayerTurn;
        BannerMessage = $"⚔️ HERO TURN {Turn}";
    }

    private void RollEnemyNextIntent()
    {
        int roll = m_Rnd.Next(100);
        if (Stage == 1)
        {
            if (roll < 60) { CurrentEnemy.Intent = EnemyIntentType.Attack; CurrentEnemy.IntentValue = 7; }
            else if (roll < 85) { CurrentEnemy.Intent = EnemyIntentType.Defend; CurrentEnemy.IntentValue = 6; }
            else { CurrentEnemy.Intent = EnemyIntentType.HeavyAttack; CurrentEnemy.IntentValue = 11; }
        }
        else if (Stage == 2)
        {
            if (roll < 50) { CurrentEnemy.Intent = EnemyIntentType.Attack; CurrentEnemy.IntentValue = 10; }
            else if (roll < 80) { CurrentEnemy.Intent = EnemyIntentType.HeavyAttack; CurrentEnemy.IntentValue = 15; }
            else { CurrentEnemy.Intent = EnemyIntentType.Defend; CurrentEnemy.IntentValue = 8; }
        }
        else
        {
            if (roll < 45) { CurrentEnemy.Intent = EnemyIntentType.HeavyAttack; CurrentEnemy.IntentValue = 18; }
            else if (roll < 80) { CurrentEnemy.Intent = EnemyIntentType.Attack; CurrentEnemy.IntentValue = 13; }
            else { CurrentEnemy.Intent = EnemyIntentType.Defend; CurrentEnemy.IntentValue = 12; }
        }
    }

    private void AddFloatingText(string text, float x, float y, Color col, float life = 1.0f)
    {
        m_Floats.Add(new FloatingText { Text = text, X = x, Y = y, Color = col, Life = life, MaxLife = life });
    }

    protected override void OnPopulateMesh(VertexHelper h)
    {
        h.Clear();

        // 1. Background Scene (Dark Dungeon Arena)
        // Background gradient / backdrop
        AddQuad(h, -480, -270, 480, 270, new Color(0.08f, 0.10f, 0.16f, 1f));
        AddQuad(h, -470, -260, 470, 260, new Color(0.12f, 0.14f, 0.22f, 1f));

        // Arena Platform
        AddQuad(h, -460, -75, 460, -68, new Color(0.24f, 0.28f, 0.40f, 1f));
        AddQuad(h, -460, -78, 460, -75, new Color(0.18f, 0.20f, 0.30f, 1f));

        // Top Header Banner Plate
        AddQuad(h, -360, 160, 360, 245, new Color(0.09f, 0.11f, 0.18f, 0.9f));
        AddQuad(h, -360, 158, 360, 160, new Color(0.40f, 0.50f, 0.75f, 1f));

        // 2. Player Entity (Left: -260, 20)
        DrawPlayer(h, -260, 20);

        // 3. Enemy Entity (Right: 260, 20)
        DrawEnemy(h, 260, 20);

        // 4. Energy Orb (Left Bottom: -400, -160)
        DrawEnergyOrb(h, -400, -155);

        // 5. Draw Pile & Discard Pile Indicators
        DrawDeckIndicators(h);

        // 6. End Turn Button (Right Bottom: 360, -155)
        DrawEndTurnButton(h, 360, -155);

        // 7. Cards in Hand
        DrawHandCards(h);

        // 8. Floating numbers / visual effects
        DrawFloatingNumbers(h);

        // 9. Victory / Defeat Modal Plate
        if (State == GameState.Victory || State == GameState.Defeat)
        {
            DrawGameOverModal(h);
        }
    }

    private void DrawPlayer(VertexHelper h, float x, float y)
    {
        Color armorColor = m_PlayerFlashTimer > 0 ? Color.white : new Color(0.35f, 0.65f, 0.95f, 1f);
        Color goldColor = new Color(0.95f, 0.8f, 0.2f, 1f);

        // Shield-shaped Knight Body
        AddQuad(h, x - 40, y - 40, x + 40, y + 40, armorColor);
        AddQuad(h, x - 35, y - 35, x + 35, y + 35, new Color(0.18f, 0.24f, 0.38f, 1f));

        // Golden Visor & Crest
        AddQuad(h, x - 25, y + 5, x + 25, y + 15, goldColor);
        AddQuad(h, x - 6, y + 25, x + 6, y + 45, goldColor);
        AddCircle(h, x, y - 10, 12, new Color(0.2f, 0.9f, 1f, 1f)); // Core gem

        // Player HP Bar (X: x-80, Y: y-65, W: 160, H: 16)
        float hpRatio = Math.Clamp((float)PlayerHp / MaxPlayerHp, 0f, 1f);
        AddQuad(h, x - 82, y - 67, x + 82, y - 49, new Color(0.08f, 0.1f, 0.14f, 1f)); // Border
        AddQuad(h, x - 80, y - 65, x + 80, y - 51, new Color(0.25f, 0.1f, 0.1f, 1f)); // Background
        AddQuad(h, x - 80, y - 65, x - 80 + 160 * hpRatio, y - 51, new Color(0.25f, 0.85f, 0.35f, 1f)); // Green Fill

        // Player Block badge (Shield at x - 95, y - 58)
        if (PlayerBlock > 0)
        {
            AddCircle(h, x - 95, y - 58, 14, new Color(0.2f, 0.5f, 0.95f, 1f));
            AddQuad(h, x - 101, y - 64, x - 89, y - 52, new Color(0.8f, 0.92f, 1f, 1f));
        }
    }

    private void DrawEnemy(VertexHelper h, float x, float y)
    {
        float breathe = MathF.Sin(m_AnimTime * 3.5f) * 4f;
        y += breathe;

        Color enemyColor = m_EnemyFlashTimer > 0 ? Color.white : CurrentEnemy.Color;

        // Enemy avatar
        if (Stage == 1) // Slime
        {
            AddCircle(h, x, y - 5, 42, enemyColor);
            AddCircle(h, x - 15, y + 5, 7, Color.white);
            AddCircle(h, x + 15, y + 5, 7, Color.white);
            AddCircle(h, x - 13, y + 5, 3.5f, Color.black);
            AddCircle(h, x + 17, y + 5, 3.5f, Color.black);
            // Crown on top
            AddQuad(h, x - 18, y + 36, x + 18, y + 48, new Color(0.95f, 0.8f, 0.2f, 1f));
        }
        else if (Stage == 2) // Goblin Warlord
        {
            AddQuad(h, x - 42, y - 40, x + 42, y + 35, enemyColor);
            // Red eyes
            AddQuad(h, x - 20, y + 10, x - 8, y + 16, Color.red);
            AddQuad(h, x + 8, y + 10, x + 20, y + 16, Color.red);
            // Horns
            AddQuad(h, x - 35, y + 35, x - 20, y + 55, new Color(0.4f, 0.3f, 0.2f, 1f));
            AddQuad(h, x + 20, y + 35, x + 35, y + 55, new Color(0.4f, 0.3f, 0.2f, 1f));
        }
        else // Dragon
        {
            AddCircle(h, x, y, 50, enemyColor);
            // Dragon Horns
            AddQuad(h, x - 45, y + 25, x - 25, y + 65, new Color(0.9f, 0.8f, 0.3f, 1f));
            AddQuad(h, x + 25, y + 25, x + 45, y + 65, new Color(0.9f, 0.8f, 0.3f, 1f));
            // Fierce yellow slit eyes
            AddQuad(h, x - 22, y + 5, x - 10, y + 15, new Color(1f, 0.9f, 0.2f, 1f));
            AddQuad(h, x + 10, y + 5, x + 22, y + 15, new Color(1f, 0.9f, 0.2f, 1f));
        }

        // Enemy HP Bar
        float hpRatio = Math.Clamp((float)CurrentEnemy.Hp / CurrentEnemy.MaxHp, 0f, 1f);
        AddQuad(h, x - 82, y - 67, x + 82, y - 49, new Color(0.08f, 0.1f, 0.14f, 1f));
        AddQuad(h, x - 80, y - 65, x + 80, y - 51, new Color(0.25f, 0.1f, 0.1f, 1f));
        AddQuad(h, x - 80, y - 65, x - 80 + 160 * hpRatio, y - 51, new Color(0.9f, 0.25f, 0.25f, 1f));

        // Enemy Block badge
        if (CurrentEnemy.Block > 0)
        {
            AddCircle(h, x - 95, y - 58, 14, new Color(0.2f, 0.5f, 0.95f, 1f));
            AddQuad(h, x - 101, y - 64, x - 89, y - 52, new Color(0.8f, 0.92f, 1f, 1f));
        }

        // Intent Icon Plate above enemy head
        float intentY = y + 75;
        AddQuad(h, x - 50, intentY - 14, x + 50, intentY + 14, new Color(0.1f, 0.12f, 0.18f, 0.9f));
        AddQuad(h, x - 50, intentY - 15, x + 50, intentY - 13, new Color(1f, 0.9f, 0.2f, 1f));

        if (CurrentEnemy.Intent == EnemyIntentType.Attack)
        {
            // Crossed sword icon
            AddQuad(h, x - 25, intentY - 8, x - 15, intentY + 8, new Color(1f, 0.3f, 0.2f, 1f));
        }
        else if (CurrentEnemy.Intent == EnemyIntentType.HeavyAttack)
        {
            // Skull/Fire icon
            AddCircle(h, x - 20, intentY, 9, new Color(1f, 0.2f, 0.1f, 1f));
        }
        else
        {
            // Shield icon
            AddQuad(h, x - 25, intentY - 8, x - 15, intentY + 8, new Color(0.3f, 0.6f, 1f, 1f));
        }
    }

    private void DrawEnergyOrb(VertexHelper h, float x, float y)
    {
        float pulse = MathF.Sin(m_AnimTime * 3.5f) * 2f;
        AddCircle(h, x, y, 32 + pulse, new Color(0.1f, 0.7f, 0.95f, 0.35f));
        AddCircle(h, x, y, 26, new Color(0.12f, 0.65f, 0.95f, 1f));
        AddCircle(h, x, y, 22, new Color(0.25f, 0.85f, 1f, 1f));
        // Lightning bolt rune
        AddQuad(h, x - 4, y - 10, x + 4, y + 10, Color.white);
    }

    private void DrawDeckIndicators(VertexHelper h)
    {
        // Draw Pile icon (x: -330, y: -155)
        AddQuad(h, -350, -175, -310, -135, new Color(0.2f, 0.25f, 0.35f, 1f));
        AddQuad(h, -346, -171, -314, -139, new Color(0.3f, 0.4f, 0.55f, 1f));

        // Discard Pile icon (x: 230, y: -155)
        AddQuad(h, 210, -175, 250, -135, new Color(0.25f, 0.2f, 0.2f, 1f));
        AddQuad(h, 214, -171, 246, -139, new Color(0.45f, 0.3f, 0.3f, 1f));
    }

    private void DrawEndTurnButton(VertexHelper h, float x, float y)
    {
        Color btnCol = m_HoveredEndTurn ? new Color(0.95f, 0.75f, 0.25f, 1f) : new Color(0.85f, 0.65f, 0.2f, 1f);
        // Outer glow/border
        AddQuad(h, x - 72, y - 27, x + 72, y + 27, new Color(0.1f, 0.1f, 0.15f, 1f));
        AddQuad(h, x - 70, y - 25, x + 70, y + 25, btnCol);
        AddQuad(h, x - 66, y - 22, x + 66, y + 22, new Color(0.35f, 0.25f, 0.1f, 1f));
    }

    private void DrawHandCards(VertexHelper h)
    {
        float startX = -(m_Hand.Count * 135f) * 0.5f + 65f;
        for (int i = 0; i < m_Hand.Count; ++i)
        {
            var card = m_Hand[i];
            bool hovered = (i == m_HoveredCardIndex);
            float cx = startX + i * 135f;
            float cy = hovered ? -130f : -155f;

            // Card Shadow
            AddQuad(h, cx - 58, cy - 88, cx + 62, cy + 82, new Color(0f, 0f, 0f, 0.4f));

            // Card Border (Gold glow if hovered, dark metallic otherwise)
            Color borderColor = hovered ? new Color(1f, 0.9f, 0.35f, 1f) : new Color(0.45f, 0.48f, 0.55f, 1f);
            AddQuad(h, cx - 60, cy - 85, cx + 60, cy + 85, borderColor);

            // Card Face Background
            AddQuad(h, cx - 56, cy - 81, cx + 56, cy + 81, new Color(0.14f, 0.16f, 0.22f, 1f));

            // Card Header Banner
            AddQuad(h, cx - 54, cy + 52, cx + 54, cy + 78, card.Theme);

            // Energy Gem in top-left
            AddCircle(h, cx - 42, cy + 65, 12, new Color(0.15f, 0.7f, 1f, 1f));

            // Card Art Window
            AddQuad(h, cx - 46, cy - 10, cx + 46, cy + 46, new Color(0.18f, 0.22f, 0.32f, 1f));

            // Card Art Emblem
            if (card.Type == CardType.Attack)
            {
                // Sword blade
                AddQuad(h, cx - 6, cy + 2, cx + 6, cy + 40, card.Theme);
                AddQuad(h, cx - 14, cy + 8, cx + 14, cy + 14, new Color(1f, 0.9f, 0.2f, 1f));
            }
            else if (card.Type == CardType.Defend)
            {
                // Shield badge
                AddQuad(h, cx - 22, cy - 2, cx + 22, cy + 36, card.Theme);
                AddQuad(h, cx - 14, cy + 6, cx + 14, cy + 28, new Color(0.6f, 0.85f, 1f, 1f));
            }
            else // Skill
            {
                // Radiant Sun / Magic Rune
                AddCircle(h, cx, cy + 18, 18, card.Theme);
                AddCircle(h, cx, cy + 18, 9, Color.white);
            }

            // Description Box Background
            AddQuad(h, cx - 50, cy - 72, cx + 50, cy - 16, new Color(0.10f, 0.12f, 0.16f, 0.9f));

            // Hotkey Badge at bottom "[1]"
            AddQuad(h, cx - 14, cy - 80, cx + 14, cy - 64, new Color(0.2f, 0.25f, 0.35f, 1f));
        }
    }

    private void DrawFloatingNumbers(VertexHelper h)
    {
        foreach (var ft in m_Floats)
        {
            float alpha = Math.Clamp(ft.Life / ft.MaxLife, 0f, 1f);
            Color col = new Color(ft.Color.r, ft.Color.g, ft.Color.b, alpha);
            // Floating text marker / pill
            AddQuad(h, ft.X - 35, ft.Y - 14, ft.X + 35, ft.Y + 14, new Color(0.1f, 0.1f, 0.15f, alpha * 0.75f));
            AddQuad(h, ft.X - 32, ft.Y - 12, ft.X + 32, ft.Y + 12, col);
        }
    }

    private void DrawGameOverModal(VertexHelper h)
    {
        // Darkened backdrop
        AddQuad(h, -480, -270, 480, 270, new Color(0.04f, 0.05f, 0.08f, 0.75f));

        // Modal Frame
        Color frameBorder = State == GameState.Victory ? new Color(1f, 0.85f, 0.2f, 1f) : new Color(0.9f, 0.2f, 0.2f, 1f);
        AddQuad(h, -260, -90, 260, 90, frameBorder);
        AddQuad(h, -256, -86, 256, 86, new Color(0.12f, 0.14f, 0.20f, 0.98f));

        // Button plate
        AddQuad(h, -100, -65, 100, -25, frameBorder);
        AddQuad(h, -96, -62, 96, -28, new Color(0.2f, 0.22f, 0.3f, 1f));
    }

    private static void AddCircle(VertexHelper helper, float x, float y, float radius, Color color)
    {
        int start = helper.currentVertCount;
        helper.AddVert(new Vector3(x, y, 0), color, Vector2.zero);
        const int segments = 18;
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

// Text Components for Dynamic HUD
public class CardBattleBannerText : Text
{
    public CardBattleBannerText()
    {
        text = "⚔️ HERO TURN 1";
        fontSize = 24;
        alignment = TextAnchor.MiddleCenter;
        raycastTarget = false;
    }

    public override void OnUpdate(float dt)
    {
        if (Owner.Parent?.GetComponent<CardBattleGame>() is { } game)
            text = game.BannerMessage;
    }
}

public class CardBattleStatusText : Text
{
    public CardBattleStatusText()
    {
        text = "Choose a card to play.";
        fontSize = 17;
        alignment = TextAnchor.MiddleCenter;
        raycastTarget = false;
    }

    public override void OnUpdate(float dt)
    {
        if (Owner.Parent?.GetComponent<CardBattleGame>() is { } game)
        {
            text = $"Player HP: {game.PlayerHp}/50  🛡️:{game.PlayerBlock}  ⚡:{game.PlayerEnergy}/3   |   {game.CurrentEnemy.Name} HP: {game.CurrentEnemy.Hp}/{game.CurrentEnemy.MaxHp} 🛡️:{game.CurrentEnemy.Block}";
        }
    }
}

public class CardBattleHintText : Text
{
    public CardBattleHintText()
    {
        text = "Click card or press [1]-[5] to play  •  [SPACE] End Turn  •  [R] Restart";
        fontSize = 15;
        alignment = TextAnchor.MiddleCenter;
        raycastTarget = false;
    }

    public override void OnUpdate(float dt)
    {
        if (Owner.Parent?.GetComponent<CardBattleGame>() is { } game)
        {
            if (game.State == CardBattleGame.GameState.Victory)
                text = "🏆 STAGE CLEARED! Click anywhere or press [SPACE] for Next Stage!";
            else if (game.State == CardBattleGame.GameState.Defeat)
                text = "💀 DEFEAT! Click anywhere or press [R] to Restart Battle!";
            else
                text = $"【Draw: {game.DrawPileCount}】  Click / [1]-[5] to Play  •  [SPACE] End Turn  •  【Discard: {game.DiscardPileCount}】";
        }
    }
}
