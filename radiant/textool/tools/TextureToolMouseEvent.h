#pragma once

#include "itexturetoolview.h"
#include "imousetoolevent.h"

namespace ui
{

// Special event subtype for Texture Tool View events
class TextureToolMouseEvent :
    public OrthoViewMouseToolEvent
{
private:
    ITextureToolView& _view;

public:
    TextureToolMouseEvent(ITextureToolView& view, const Vector2& devicePosition) :
        OrthoViewMouseToolEvent(view, devicePosition),
        _view(view)
    {}

    TextureToolMouseEvent(ITextureToolView& view, const Vector2& devicePosition, const Vector2& delta) :
        OrthoViewMouseToolEvent(view, devicePosition, delta),
        _view(view)
    {}

    ITextureToolView& getView()
    {
        return _view;
    }
};

}