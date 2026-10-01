using Overload.Maths;
using Overload.UI;

[assembly: EnableUI]

namespace ImageFixture;

public class SimpleFill : Image
{
    public SimpleFill()
    {
        type = ImageType.Simple;
        spriteRect = new Rect(new Vector2(0, 0), new Vector2(32, 32));
        textureSize = new Vector2(32, 32);
        color = new Color(1, 0, 0, 1);
    }
}

public class SimpleAspect : Image
{
    public SimpleAspect()
    {
        type = ImageType.Simple;
        preserveAspect = true;
        spriteRect = new Rect(new Vector2(0, 0), new Vector2(100, 50));
        textureSize = new Vector2(100, 50);
        color = new Color(0, 1, 0, 1);
    }
}

public class SlicedFill : Image
{
    public SlicedFill()
    {
        type = ImageType.Sliced;
        fillCenter = true;
        spriteRect = new Rect(new Vector2(0, 0), new Vector2(32, 32));
        textureSize = new Vector2(32, 32);
        border = new Vector4(8, 8, 8, 8);
        color = new Color(0, 0, 1, 1);
    }
}

public class SlicedNoCenter : Image
{
    public SlicedNoCenter()
    {
        type = ImageType.Sliced;
        fillCenter = false;
        spriteRect = new Rect(new Vector2(0, 0), new Vector2(32, 32));
        textureSize = new Vector2(32, 32);
        border = new Vector4(8, 8, 8, 8);
        color = new Color(1, 1, 0, 1);
    }
}

public class TiledFill : Image
{
    public TiledFill()
    {
        type = ImageType.Tiled;
        fillCenter = true;
        spriteRect = new Rect(new Vector2(0, 0), new Vector2(16, 16));
        textureSize = new Vector2(16, 16);
        color = new Color(1, 0, 1, 1);
    }
}

public class FillH : Image
{
    public FillH()
    {
        type = ImageType.Filled;
        fillMethod = FillMethod.Horizontal;
        fillOrigin = 0;
        fillAmount = 0.5f;
        spriteRect = new Rect(new Vector2(0, 0), new Vector2(32, 32));
        textureSize = new Vector2(32, 32);
        color = new Color(0, 1, 1, 1);
    }
}

public class FillHOrigin : Image
{
    public FillHOrigin()
    {
        type = ImageType.Filled;
        fillMethod = FillMethod.Horizontal;
        fillOrigin = 1;
        fillAmount = 0.5f;
        spriteRect = new Rect(new Vector2(0, 0), new Vector2(32, 32));
        textureSize = new Vector2(32, 32);
        color = new Color(0.2f, 0.8f, 0.2f, 1);
    }
}

public class FillV : Image
{
    public FillV()
    {
        type = ImageType.Filled;
        fillMethod = FillMethod.Vertical;
        fillOrigin = 0;
        fillAmount = 0.5f;
        spriteRect = new Rect(new Vector2(0, 0), new Vector2(32, 32));
        textureSize = new Vector2(32, 32);
        color = new Color(0.9f, 0.4f, 0.1f, 1);
    }
}

public class FillEmpty : Image
{
    public FillEmpty()
    {
        type = ImageType.Filled;
        fillMethod = FillMethod.Horizontal;
        fillAmount = 0f;
        spriteRect = new Rect(new Vector2(0, 0), new Vector2(32, 32));
        textureSize = new Vector2(32, 32);
        color = Color.red;
    }
}

public class FillRadial90 : Image
{
    public FillRadial90()
    {
        type = ImageType.Filled;
        fillMethod = FillMethod.Radial90;
        fillOrigin = 0;
        fillAmount = 0.5f;
        fillClockwise = true;
        spriteRect = new Rect(new Vector2(0, 0), new Vector2(32, 32));
        textureSize = new Vector2(32, 32);
        color = new Color(0.5f, 0.2f, 0.9f, 1);
    }
}

public class FillRadial360 : Image
{
    public FillRadial360()
    {
        type = ImageType.Filled;
        fillMethod = FillMethod.Radial360;
        fillOrigin = 0;
        fillAmount = 0.25f;
        fillClockwise = true;
        spriteRect = new Rect(new Vector2(0, 0), new Vector2(32, 32));
        textureSize = new Vector2(32, 32);
        color = new Color(0.1f, 0.6f, 0.9f, 1);
    }
}
