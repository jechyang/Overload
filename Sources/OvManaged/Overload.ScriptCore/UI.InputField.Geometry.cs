using System;
using Overload.Maths;
namespace Overload.UI;

public partial class InputField
{
    [SerializeField] private float m_CaretBlinkRate=0.85f,m_CaretWidth=1;
    [SerializeField] private Color m_CaretColor=new(0.1f,0.1f,0.1f,1),m_SelectionColor=new(0.2f,0.45f,1,0.35f);
    private float blinkTime;
    private bool previousBlink;
    private InputFieldOverlay? editingVisual;
    private Vector2? originalLabelPosition;
    public float caretBlinkRate {get=>m_CaretBlinkRate;set=>m_CaretBlinkRate=Math.Max(0,CanvasScaler.Finite(value));}
    public float caretWidth {get=>m_CaretWidth;set=>m_CaretWidth=Math.Max(1,CanvasScaler.Finite(value));}
    public Color caretColor {get=>m_CaretColor;set{m_CaretColor=value;editingVisual?.SetVerticesDirty();}}
    public Color selectionColor {get=>m_SelectionColor;set{m_SelectionColor=value;editingVisual?.SetVerticesDirty();}}
    public Vector2 scrollOffset=>m_TextComponent==null||originalLabelPosition==null?Vector2.zero:m_TextComponent.rectTransform.anchoredPosition-originalLabelPosition.Value;
    private bool ShowCaret=>isFocused&&(caretBlinkRate<=0||(blinkTime*caretBlinkRate)%1<0.5f);
    private Vector2 CaretPoint(int index){
        var text=m_TextComponent;if(text==null)return default;
        Vector2 p=new(text.rectTransform.rect.min.x,text.rectTransform.rect.max.y-text.fontSize);
        foreach(var ch in text.characters){if(ch.utf16Index>=index)return ch.origin;p=ch.scalar==10?new(text.rectTransform.rect.min.x,ch.origin.y-text.fontSize*text.lineSpacing):ch.origin+new Vector2(ch.advance,0);}
        return p;
    }
    private int ClosestCharacter(Vector2 point){
        var text=m_TextComponent;if(text==null||text.characters.Count==0)return 0;
        float best=float.PositiveInfinity;int index=0;
        foreach(var ch in text.characters){
            float y=ch.origin.y+text.fontSize*0.4f;float dy=Math.Abs(y-point.y);
            foreach(bool after in new[]{false,true}){float x=ch.origin.x+(after?ch.advance:0);float score=dy*10000+Math.Abs(x-point.x);if(score<best){best=score;index=ch.utf16Index+(after?ch.utf16Length:0);}}
        }
        return Boundary(index);
    }
    private void PlaceCaret(Vector2 screen,bool extend){var text=m_TextComponent;if(text==null)return;if(RectTransformUtility.ScreenPointToLocalPointInRectangle(text.rectTransform,screen,null,out var point))SetCaret(ClosestCharacter(point),extend);}
    private void MoveLine(int direction,bool shift){var text=m_TextComponent;if(text==null){SetCaret(direction<0?0:m_Text.Length,shift);return;}var p=CaretPoint(m_CaretPosition);p.y-=direction*text.fontSize*text.lineSpacing;p.y+=text.fontSize*0.4f;SetCaret(ClosestCharacter(p),shift);}
    public override void OnLateUpdate(float dt){
        base.OnLateUpdate(dt);if(!isFocused||m_TextComponent is not Text text)return;
        if(editingVisual==null||editingVisual.owner==null||!editingVisual.Owner.IsAlive){
            var actor=text.Owner.CreateChild("Input Caret and Selection");var rect=actor.AddComponent<RectTransform>();DefaultControls.Stretch(rect);rect.pivot=text.rectTransform.pivot;
            editingVisual=actor.AddUIBehaviour<InputFieldOverlay>();editingVisual.field=this;editingVisual.raycastTarget=false;
        }
        blinkTime+=Math.Max(0,dt);bool visible=ShowCaret;if(previousBlink!=visible){previousBlink=visible;editingVisual.SetVerticesDirty();}
        originalLabelPosition??=text.rectTransform.anchoredPosition;
        var point=Clipping.LocalToCanvas(text.rectTransform,CaretPoint(m_CaretPosition));var visibleRect=Clipping.CanvasRect(rectTransform);
        var position=text.rectTransform.anchoredPosition;var previous=position;float padding=6;
        if(!multiLine){if(point.x>visibleRect.max.x-padding)position.x-=point.x-visibleRect.max.x+padding;else if(point.x<visibleRect.min.x+padding)position.x+=visibleRect.min.x+padding-point.x;}
        else{if(point.y<visibleRect.min.y+2)position.y+=visibleRect.min.y+2-point.y;else if(point.y+text.fontSize>visibleRect.max.y-2)position.y-=point.y+text.fontSize-visibleRect.max.y+2;}
        if(previous!=position){text.rectTransform.anchoredPosition=position;editingVisual.SetVerticesDirty();}
    }
    private void ReleaseEditingVisual(){
        if(editingVisual?.owner!=null&&editingVisual.Owner.IsAlive)editingVisual.Owner.Destroy();editingVisual=null;
        if(m_TextComponent is Text text&&originalLabelPosition.HasValue)text.rectTransform.anchoredPosition=originalLabelPosition.Value;
        originalLabelPosition=null;
    }
    internal void PopulateEditingMesh(VertexHelper mesh){
        mesh.Clear();if(!isFocused||m_TextComponent is not Text text)return;
        void Quad(float x,float y,float w,float h,Color color){int start=mesh.currentVertCount;mesh.AddVert(new(x,y,0),color,Vector2.zero);mesh.AddVert(new(x,y+h,0),color,Vector2.zero);mesh.AddVert(new(x+w,y+h,0),color,Vector2.zero);mesh.AddVert(new(x+w,y,0),color,Vector2.zero);mesh.AddTriangle(start,start+1,start+2);mesh.AddTriangle(start,start+2,start+3);}
        float height=text.fontSize*text.lineSpacing;
        if(m_SelectionExists){foreach(var ch in text.characters)if(ch.utf16Index<selectionEnd&&ch.utf16Index+ch.utf16Length>selectionStart)Quad(ch.origin.x,ch.origin.y-text.fontSize*0.2f,Math.Max(ch.advance,1),height,selectionColor);}
        else if(ShowCaret){var point=CaretPoint(m_CaretPosition+composition.Length);Quad(point.x,point.y-text.fontSize*0.2f,caretWidth,height,caretColor);}
    }
}

public class InputFieldOverlay : MaskableGraphic
{
    [SerializeField] private ComponentRef<InputField> m_Field;
    public InputField? field {get=>ResolveUIReference(m_Field);set=>m_Field=value==null?default:ComponentRef<InputField>.From(Owner,value);}
    protected override void OnPopulateMesh(VertexHelper helper){helper.Clear();field?.PopulateEditingMesh(helper);}
}
