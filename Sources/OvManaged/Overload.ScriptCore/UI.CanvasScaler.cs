using System;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public class CanvasScaler : UIBehaviour
{
    public enum ScaleMode { ConstantPixelSize, ScaleWithScreenSize, ConstantPhysicalSize }
    public enum ScreenMatchMode { MatchWidthOrHeight, Expand, Shrink }
    public enum Unit { Centimeters, Millimeters, Inches, Points, Picas }
    [SerializeField] private ScaleMode m_UiScaleMode;
    [SerializeField] private ScreenMatchMode m_ScreenMatchMode;
    [SerializeField] private Unit m_PhysicalUnit = Unit.Points;
    [SerializeField] private float m_ReferenceWidth = 800, m_ReferenceHeight = 600;
    [SerializeField] private float m_ScaleFactor = 1, m_MatchWidthOrHeight;
    [SerializeField] private float m_ReferencePixelsPerUnit = 100, m_DefaultSpriteDPI = 96, m_FallbackScreenDPI = 96;
    [SerializeField] private float m_DynamicPixelsPerUnit = 1;
    public ScaleMode uiScaleMode { get => m_UiScaleMode; set => m_UiScaleMode = value; }
    public ScreenMatchMode screenMatchMode { get => m_ScreenMatchMode; set => m_ScreenMatchMode = value; }
    public Unit physicalUnit { get => m_PhysicalUnit; set => m_PhysicalUnit = value; }
    public Vector2 referenceResolution { get => new(m_ReferenceWidth, m_ReferenceHeight); set { m_ReferenceWidth = Positive(value.x); m_ReferenceHeight = Positive(value.y); } }
    public float scaleFactor { get => m_ScaleFactor; set => m_ScaleFactor = Math.Max(0.01f, Finite(value)); }
    public float matchWidthOrHeight { get => m_MatchWidthOrHeight; set => m_MatchWidthOrHeight = Math.Clamp(Finite(value), 0, 1); }
    public float referencePixelsPerUnit { get => m_ReferencePixelsPerUnit; set => m_ReferencePixelsPerUnit = Positive(value); }
    public float fallbackScreenDPI { get => m_FallbackScreenDPI; set => m_FallbackScreenDPI = Positive(value); }
    public float defaultSpriteDPI { get => m_DefaultSpriteDPI; set => m_DefaultSpriteDPI = Math.Max(1, Finite(value)); }
    public float dynamicPixelsPerUnit { get => m_DynamicPixelsPerUnit; set => m_DynamicPixelsPerUnit = Positive(value); }
    protected Canvas canvas => Owner.GetComponent<Canvas>() ?? throw new InvalidOperationException("CanvasScaler requires Canvas");
    protected Vector2 screenSize { get; private set; }
    private bool handled;
    internal void UpdateScale(Vector2 viewport) { if (IsActive()) { Handle(viewport); handled = true; } else if (handled) OnDisable(); }
    // Window DPI is not exposed yet; custom scalers can provide platform-specific DPI.
    protected virtual float GetScreenDPI() => 0;
    protected virtual void SetScaleFactor(float value) => canvas.scaleFactor = Positive(value);
    protected virtual void SetReferencePixelsPerUnit(float value) { effectiveReferencePixelsPerUnit = Positive(value); }
    public float effectiveReferencePixelsPerUnit { get; private set; } = 100;
    protected internal virtual void Handle(Vector2 viewport) {
        if (!canvas.isRootCanvas) return;
        screenSize = viewport;
        if (canvas.renderMode == RenderMode.WorldSpace) { HandleWorldCanvas(); return; }
        switch (uiScaleMode) {
            case ScaleMode.ConstantPixelSize: HandleConstantPixelSize(); break;
            case ScaleMode.ScaleWithScreenSize: HandleScaleWithScreenSize(); break;
            case ScaleMode.ConstantPhysicalSize: HandleConstantPhysicalSize(); break;
            default: throw new InvalidOperationException("Unknown CanvasScaler mode");
        }
    }
    protected virtual void HandleWorldCanvas() { SetScaleFactor(dynamicPixelsPerUnit); SetReferencePixelsPerUnit(referencePixelsPerUnit); }
    protected virtual void HandleConstantPixelSize() { SetScaleFactor(scaleFactor); SetReferencePixelsPerUnit(referencePixelsPerUnit); }
    protected virtual void HandleScaleWithScreenSize() {
        float x = screenSize.x / Positive(referenceResolution.x), y = screenSize.y / Positive(referenceResolution.y);
        float scale = screenMatchMode switch {
            ScreenMatchMode.MatchWidthOrHeight => MathF.Pow(2, MathF.Log2(x) * (1 - matchWidthOrHeight) + MathF.Log2(y) * matchWidthOrHeight),
            ScreenMatchMode.Expand => Math.Min(x, y), ScreenMatchMode.Shrink => Math.Max(x, y),
            _ => throw new InvalidOperationException("Unknown screen match mode")
        };
        SetScaleFactor(scale); SetReferencePixelsPerUnit(referencePixelsPerUnit);
    }
    protected virtual void HandleConstantPhysicalSize() {
        float dpi = GetScreenDPI(); if (dpi <= 0) dpi = fallbackScreenDPI;
        float unit = physicalUnit switch {
            Unit.Centimeters => 2.54f, Unit.Millimeters => 25.4f, Unit.Inches => 1, Unit.Points => 72, Unit.Picas => 6,
            _ => throw new InvalidOperationException("Unknown physical unit")
        };
        SetScaleFactor(dpi / unit); SetReferencePixelsPerUnit(referencePixelsPerUnit * unit / Positive(defaultSpriteDPI));
    }
    public override void OnDisable() { handled = false; if (owner != null && Owner.CanModify && Owner.GetComponent<Canvas>() is Canvas c) c.scaleFactor = 1; }
    internal static float Finite(float value) => float.IsFinite(value) ? value : throw new ArgumentOutOfRangeException(nameof(value));
    internal static float Positive(float value) => Finite(value) > 0 ? value : throw new ArgumentOutOfRangeException(nameof(value));
}
