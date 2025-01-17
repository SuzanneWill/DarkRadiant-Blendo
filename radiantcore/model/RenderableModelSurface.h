#pragma once

#include "imodelsurface.h"
#include "render/RenderableSurface.h"

namespace model
{

// Wraps an IIndexedModelSurface to implement the IRenderableSurface interface
// required to draw a composite mesh in the scene.
class RenderableModelSurface final :
    public render::RenderableSurface
{
private:
    const IIndexedModelSurface& _surface;
    const IRenderEntity* _entity;
    const Matrix4& _localToWorld;

public:
    using Ptr = std::shared_ptr<RenderableModelSurface>;

    // Construct this renderable around the existing surface.
    // The reference to the orientation matrix is stored and needs to remain valid
    RenderableModelSurface(const IIndexedModelSurface& surface, const IRenderEntity* entity, const Matrix4& localToWorld) :
        _surface(surface),
        _entity(entity),
        _localToWorld(localToWorld)
    {}

    RenderableModelSurface(const RenderableModelSurface& other) = delete;
    RenderableModelSurface& operator=(const RenderableModelSurface& other) = delete;

    const IIndexedModelSurface& getSurface() const
    {
        return _surface;
    }

    bool isVisible() override
    {
        return !_surface.getIndexArray().empty();
    }

    const std::vector<MeshVertex>& getVertices() override
    {
        return _surface.getVertexArray();
    }

    const std::vector<unsigned int>& getIndices() override
    {
        return _surface.getIndexArray();
    }

    bool isOriented() override
    {
        return true;
    }

    const Matrix4& getObjectTransform() override
    {
        return _localToWorld;
    }

    const AABB& getObjectBounds() override
    {
        return _surface.getSurfaceBounds();
    }

    bool isShadowCasting() override
    {
        return _entity != nullptr && _entity->isShadowCasting();
    }
};

}
