#include "OpenGLShader.h"

#include "GLProgramFactory.h"
#include "../OpenGLRenderSystem.h"
#include "DepthFillPass.h"
#include "InteractionPass.h"

#include "icolourscheme.h"
#include "ishaders.h"
#include "ifilter.h"
#include "irender.h"
#include "texturelib.h"
#include "string/predicate.h"

#include <functional>

namespace render
{

namespace
{
    TexturePtr getDefaultInteractionTexture(IShaderLayer::Type type)
    {
        return GlobalMaterialManager().getDefaultInteractionTexture(type);
    }

    TexturePtr getTextureOrInteractionDefault(const IShaderLayer::Ptr& layer)
    {
        auto texture = layer->getTexture();
        return texture ? texture : getDefaultInteractionTexture(layer->getType());
    }
}

// !!SPECIAL BLENDO HACK!!
// Define the names of materials here to force DarkRadiant to always render them translucent
// (we want to reserve this for certain tool textures, mostly)
const auto translucentToolTextures = {
    "textures/common/aas_solid",
    "textures/common/aasobstacle",
    "textures/common/actor_clip",
    "textures/common/clip",
    "textures/common/confined",
    "textures/common/monster_clip",
    "textures/common/climbclip",
    "textures/common/climbclip_actorclip",
    "textures/common/climbclip_playerclip",
    "textures/common/nodraw",
    "textures/common/nodrawsolid",
    "textures/common/player_clip",
    "textures/common/shadow",
    "textures/common/shadow_cheap",
    "textures/common/shadow_sunlight",
    "textures/common/trigmulti",
    "textures/common/trigonce",
    "textures/common/visportal"
};

// Triplet of diffuse, bump and specular shaders
struct OpenGLShader::DBSTriplet
{
    // DBS layers
    IShaderLayer::Ptr diffuse;
    IShaderLayer::Ptr bump;
    IShaderLayer::Ptr specular;

    // Need-depth-fill flag
    bool needDepthFill;

    // Initialise
    DBSTriplet()
    : needDepthFill(true)
    { }

    // Clear pointers
    void reset()
    {
        diffuse.reset();
        bump.reset();
        specular.reset();
        needDepthFill = false;
    }
};

OpenGLShader::OpenGLShader(const std::string& name, OpenGLRenderSystem& renderSystem) :
    _name(name),
    _renderSystem(renderSystem),
    _isVisible(true),
    _useCount(0),
    _geometryRenderer(renderSystem.getGeometryStore()),
    _surfaceRenderer(renderSystem.getGeometryStore()),
    _enabledViewTypes(0),
    _mergeModeActive(false)
{
    _windingRenderer.reset(new WindingRenderer<WindingIndexer_Triangles>(renderSystem.getGeometryStore(), this));
}

OpenGLShader::~OpenGLShader()
{
    destroy();
}

OpenGLRenderSystem& OpenGLShader::getRenderSystem()
{
    return _renderSystem;
}

void OpenGLShader::destroy()
{
    _enabledViewTypes = 0;
    _materialChanged.disconnect();
    _material.reset();
    clearPasses();
}

void OpenGLShader::addRenderable(const OpenGLRenderable& renderable,
								 const Matrix4& modelview)
{
    if (!_isVisible) return;

    // Add the renderable to all of our shader passes
    for (const OpenGLShaderPassPtr& pass : _shaderPasses)
    {
        // Submit the renderable to each pass
		pass->addRenderable(renderable, modelview);
    }
}

void OpenGLShader::drawSurfaces(const VolumeTest& view, const RenderInfo& info)
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    
    // Always using CW culling by default
    glFrontFace(GL_CW);

    if (hasSurfaces())
    {
        _geometryRenderer.render();

        // Surfaces are not allowed to render vertex colours (for now)
        // otherwise they don't show up in their parent entity's colour
        glDisableClientState(GL_COLOR_ARRAY);
        _surfaceRenderer.render(view);
    }

    // Render all windings
    _windingRenderer->renderAllWindings(info);

    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

bool OpenGLShader::hasSurfaces() const
{
    return !_geometryRenderer.empty() || !_surfaceRenderer.empty();
}

IGeometryRenderer::Slot OpenGLShader::addGeometry(GeometryType indexType,
    const std::vector<ArbitraryMeshVertex>& vertices, const std::vector<unsigned int>& indices)
{
    return _geometryRenderer.addGeometry(indexType, vertices, indices);
}

void OpenGLShader::removeGeometry(IGeometryRenderer::Slot slot)
{
    _geometryRenderer.removeGeometry(slot);
}

void OpenGLShader::updateGeometry(IGeometryRenderer::Slot slot, const std::vector<ArbitraryMeshVertex>& vertices,
    const std::vector<unsigned int>& indices)
{
    _geometryRenderer.updateGeometry(slot, vertices, indices);
}

void OpenGLShader::renderGeometry(IGeometryRenderer::Slot slot)
{
    _geometryRenderer.renderGeometry(slot);
}

AABB OpenGLShader::getGeometryBounds(IGeometryRenderer::Slot slot)
{
    return _geometryRenderer.getGeometryBounds(slot);
}

IGeometryStore::Slot OpenGLShader::getGeometryStorageLocation(IGeometryRenderer::Slot slot)
{
    return _geometryRenderer.getGeometryStorageLocation(slot);
}

ISurfaceRenderer::Slot OpenGLShader::addSurface(IRenderableSurface& surface)
{
    return _surfaceRenderer.addSurface(surface);
}

void OpenGLShader::removeSurface(ISurfaceRenderer::Slot slot)
{
    _surfaceRenderer.removeSurface(slot);
}

void OpenGLShader::updateSurface(ISurfaceRenderer::Slot slot)
{
    _surfaceRenderer.updateSurface(slot);
}

void OpenGLShader::renderSurface(ISurfaceRenderer::Slot slot)
{
    _surfaceRenderer.renderSurface(slot);
}

IGeometryStore::Slot OpenGLShader::getSurfaceStorageLocation(ISurfaceRenderer::Slot slot)
{
    return _surfaceRenderer.getSurfaceStorageLocation(slot);
}

IWindingRenderer::Slot OpenGLShader::addWinding(const std::vector<ArbitraryMeshVertex>& vertices, IRenderEntity* entity)
{
    return _windingRenderer->addWinding(vertices, entity);
}

void OpenGLShader::removeWinding(IWindingRenderer::Slot slot)
{
    _windingRenderer->removeWinding(slot);
}

void OpenGLShader::updateWinding(IWindingRenderer::Slot slot, const std::vector<ArbitraryMeshVertex>& vertices)
{
    _windingRenderer->updateWinding(slot, vertices);
}

bool OpenGLShader::hasWindings() const
{
    return !_windingRenderer->empty();
}

void OpenGLShader::renderWinding(IWindingRenderer::RenderMode mode, IWindingRenderer::Slot slot)
{
    _windingRenderer->renderWinding(mode, slot);
}

void OpenGLShader::setVisible(bool visible)
{
    // Control visibility by inserting or removing our shader passes from the GL
    // state manager
    if (!_isVisible && visible)
    {
        insertPasses();
    }
    else if (_isVisible && !visible)
    {
        removePasses();
    }

    _isVisible = visible;
}

bool OpenGLShader::isVisible() const
{
    return _isVisible && (!_material || _material->isVisible());
}

void OpenGLShader::incrementUsed()
{
    if (++_useCount == 1 && _material)
    {
		_material->SetInUse(true);
    }
}

void OpenGLShader::decrementUsed()
{
    if (--_useCount == 0 && _material)
    {
		_material->SetInUse(false);
    }
}

void OpenGLShader::attachObserver(Observer& observer)
{
	std::pair<Observers::iterator, bool> result = _observers.insert(&observer);

	// Prevent double-attach operations in debug mode
	assert(result.second);

	// Emit the signal immediately if we're in realised state
	if (isRealised())
	{
		observer.onShaderRealised();
	}
}

void OpenGLShader::detachObserver(Observer& observer)
{
	// Emit the signal immediately if we're in realised state
	if (isRealised())
	{
		observer.onShaderUnrealised();
	}

	// Prevent invalid detach operations in debug mode
	assert(_observers.find(&observer) != _observers.end());

	_observers.erase(&observer);
}

bool OpenGLShader::isRealised()
{
    return _material != 0;
}

void OpenGLShader::realise()
{
    // Construct the shader passes based on the name
    construct();

    if (_material)
	{
		// greebo: Check the filtersystem whether we're filtered
		_material->setVisible(GlobalFilterSystem().isVisible(FilterRule::TYPE_TEXTURE, _name));

		if (_useCount != 0)
		{
			_material->SetInUse(true);
		}
    }

    insertPasses();

	for (Observer* observer : _observers)
	{
		observer->onShaderRealised();
	}
}

void OpenGLShader::insertPasses()
{
    // Insert all shader passes into the GL state manager
    for (auto& shaderPass : _shaderPasses)
    {
    	_renderSystem.insertSortedState(std::make_pair(shaderPass->statePtr(), shaderPass));
    }
}

void OpenGLShader::removePasses()
{
    // Remove shader passes from the GL state manager
    for (auto& shaderPass : _shaderPasses)
	{
        _renderSystem.eraseSortedState(shaderPass->statePtr());
    }
}

void OpenGLShader::clearPasses()
{
    _interactionPass.reset();
    _depthFillPass.reset();
    _shaderPasses.clear();
}

void OpenGLShader::unrealise()
{
	for (Observer* observer : _observers)
	{
		observer->onShaderUnrealised();
	}

    removePasses();

    destroy();
}

const MaterialPtr& OpenGLShader::getMaterial() const
{
    return _material;
}

unsigned int OpenGLShader::getFlags() const
{
    return _material->getMaterialFlags();
}

// Append a default shader pass onto the back of the state list
OpenGLState& OpenGLShader::appendDefaultPass()
{
    _shaderPasses.push_back(std::make_shared<OpenGLShaderPass>(*this));
    OpenGLState& state = _shaderPasses.back()->state();
    return state;
}

OpenGLState& OpenGLShader::appendDepthFillPass()
{
    _depthFillPass = _shaderPasses.emplace_back(std::make_shared<DepthFillPass>(*this, _renderSystem));
    return _depthFillPass->state();
}

OpenGLState& OpenGLShader::appendInteractionPass()
{
    _interactionPass = _shaderPasses.emplace_back(std::make_shared<InteractionPass>(*this, _renderSystem));
    return _interactionPass->state();
}

// Test if we can render in bump map mode
bool OpenGLShader::canUseLightingMode() const
{
    return _renderSystem.shaderProgramsAvailable() &&
        _renderSystem.getCurrentShaderProgram() == RenderSystem::SHADER_PROGRAM_INTERACTION;
}

void OpenGLShader::setGLTexturesFromTriplet(OpenGLState& pass,
                                            const DBSTriplet& triplet)
{
    // Get texture components. If any of the triplet is missing, look up the
    // default from the shader system.
    if (triplet.diffuse)
    {
        pass.texture0 = getTextureOrInteractionDefault(triplet.diffuse)->getGLTexNum();
		pass.stage0 = triplet.diffuse;
    }
    else
    {
        pass.texture0 = getDefaultInteractionTexture(IShaderLayer::DIFFUSE)->getGLTexNum();
    }

    if (triplet.bump)
    {
        pass.texture1 = getTextureOrInteractionDefault(triplet.bump)->getGLTexNum();
		pass.stage1 = triplet.bump;
    }
    else
    {
        pass.texture1 = getDefaultInteractionTexture(IShaderLayer::BUMP)->getGLTexNum();
    }

    if (triplet.specular)
    {
        pass.texture2 = getTextureOrInteractionDefault(triplet.specular)->getGLTexNum();
		pass.stage2 = triplet.specular;
    }
    else
    {
        pass.texture2 = getDefaultInteractionTexture(IShaderLayer::SPECULAR)->getGLTexNum();
    }
}

// Add an interaction layer
void OpenGLShader::appendInteractionLayer(const DBSTriplet& triplet)
{
	// Set layer vertex colour mode and alphatest parameters
    IShaderLayer::VertexColourMode vcolMode = IShaderLayer::VERTEX_COLOUR_NONE;
    double alphaTest = -1;

    if (triplet.diffuse)
    {
        vcolMode = triplet.diffuse->getVertexColourMode();
        alphaTest = triplet.diffuse->getAlphaTest();
    }

    // Append a depthfill shader pass if requested
    if (triplet.needDepthFill && triplet.diffuse)
    {
        // Create depth-buffer fill pass with alpha test
        auto& zPass = appendDepthFillPass();

        // Store the alpha test value
        zPass.alphaThreshold = static_cast<GLfloat>(alphaTest);

        // We need a diffuse stage to be able to performthe alpha test
        zPass.stage0 = triplet.diffuse;
        zPass.texture0 = getTextureOrInteractionDefault(triplet.diffuse)->getGLTexNum();
    }

    // Add the DBS pass
    auto& dbsPass = appendInteractionPass();

    // Populate the textures and remember the stage reference
    setGLTexturesFromTriplet(dbsPass, triplet);

    if (vcolMode != IShaderLayer::VERTEX_COLOUR_NONE)
    {
        // Vertex colours allowed
        dbsPass.setRenderFlag(RENDER_VERTEX_COLOUR);

        if (vcolMode == IShaderLayer::VERTEX_COLOUR_INVERSE_MULTIPLY)
        {
            // Vertex colours are inverted
            dbsPass.setColourInverted(true);
        }
    }

    applyAlphaTestToPass(dbsPass, alphaTest);

	// Apply the diffuse colour modulation
	if (triplet.diffuse)
	{
		dbsPass.setColour(triplet.diffuse->getColour());
	} 
}

void OpenGLShader::applyAlphaTestToPass(OpenGLState& pass, double alphaTest)
{
    if (alphaTest > 0)
    {
        pass.setRenderFlag(RENDER_ALPHATEST);
        pass.alphaFunc = GL_GEQUAL; // alpha >= threshold
        pass.alphaThreshold = static_cast<GLfloat>(alphaTest);
    }
}

// Construct lighting mode render passes
void OpenGLShader::constructLightingPassesFromMaterial()
{
    // Build up and add shader passes for DBS triplets as they are found. A
    // new triplet is found when (1) the same DBS layer type is seen twice, (2)
    // we have at least one DBS layer then see a blend layer, or (3) we have at
    // least one DBS layer then reach the end of the layers.

    DBSTriplet triplet;
    const IShaderLayerVector allLayers = _material->getAllLayers();

    for (const auto& layer : allLayers)
    {
        // Skip programmatically disabled layers
        if (!layer->isEnabled()) continue;

		// Make sure we had at least one evaluation call to fill the material registers
		layer->evaluateExpressions(0);

        switch (layer->getType())
        {
        case IShaderLayer::DIFFUSE:
            if (triplet.diffuse)
            {
                appendInteractionLayer(triplet);
                triplet.reset();
            }
            triplet.diffuse = layer;
            break;

        case IShaderLayer::BUMP:
            if (triplet.bump)
            {
                appendInteractionLayer(triplet);
                triplet.reset();
            }
            triplet.bump = layer;
            break;

        case IShaderLayer::SPECULAR:
            if (triplet.specular)
            {
                appendInteractionLayer(triplet);
                triplet.reset();
            }
            triplet.specular = layer;
            break;

        case IShaderLayer::BLEND:
            if (triplet.specular || triplet.bump || triplet.diffuse)
            {
                appendInteractionLayer(triplet);
                triplet.reset();
            }

            appendBlendLayer(layer);
        }
    }

    // Submit final pass if we reach the end
    if (triplet.specular || triplet.bump || triplet.diffuse)
	{
		appendInteractionLayer(triplet);
	}
}

void OpenGLShader::determineBlendModeForEditorPass(OpenGLState& pass)
{
    bool hasDiffuseLayer = false;

    // Determine alphatest and colouration from first diffuse layer
    const IShaderLayerVector allLayers = _material->getAllLayers();

    for (IShaderLayerVector::const_iterator i = allLayers.begin();
         i != allLayers.end();
         ++i)
    {
        const IShaderLayer::Ptr& layer = *i;

        // Make sure we had at least one evaluation call to fill the material registers
        layer->evaluateExpressions(0);

        if (layer->getType() == IShaderLayer::DIFFUSE)
        {
            hasDiffuseLayer = true;

            if (layer->getAlphaTest() > 0)
            {
                applyAlphaTestToPass(pass, layer->getAlphaTest());
            }

            pass.setColour(layer->getColour());

            // Set the diffuse layer as a stage so that it gets evaluated properly
            // (normally only lit shaders are evaluated at render time, but we want diffuse colouration to happen in unlit view)
            pass.stage0 = layer;

            break;
        }
    }

    // Prep for incoming Blendo hack
    auto begin = translucentToolTextures.begin();
    auto end = translucentToolTextures.end();
    auto materialName = _material->getName();

    if (!hasDiffuseLayer)
    {
        pass.setColour(Colour4::WHITE());
    }

    // If this is a purely blend material (no DBS layers), set the editor blend
    // mode from the first blend layer.
	// greebo: Hack to let "shader not found" textures be handled as diffusemaps
    if (!hasDiffuseLayer && !allLayers.empty() && _material->getName() != "_default")
    {
		pass.setRenderFlag(RENDER_BLEND);
		pass.setSortPosition(OpenGLState::SORT_TRANSLUCENT);

		BlendFunc bf = allLayers[0]->getBlendFunc();
		pass.m_blend_src = bf.src;
		pass.m_blend_dst = bf.dest;
    }

    // !!SPECIAL BLENDO HACK!!
    // Forces certain textures to show up translucent so I don't lose my mind building vents
    else if (!hasDiffuseLayer && std::find(begin, end, materialName) != end)
    {
        pass.setRenderFlag(RENDER_BLEND);
        pass.setSortPosition(OpenGLState::SORT_TRANSLUCENT);
        BlendFunc* bf = new BlendFunc(GL_DST_COLOR, GL_ZERO);
        pass.m_blend_src = bf->src;
        pass.m_blend_dst = bf->dest;
    }
}

// Construct editor-image-only render passes
void OpenGLShader::constructEditorPreviewPassFromMaterial()
{
    OpenGLState& previewPass = appendDefaultPass();

    // Render the editor texture in legacy mode
    auto editorTex = _material->getEditorImage();
    previewPass.texture0 = editorTex ? editorTex->getGLTexNum() : 0;

    previewPass.setRenderFlag(RENDER_FILL);
    previewPass.setRenderFlag(RENDER_TEXTURE_2D);
    previewPass.setRenderFlag(RENDER_DEPTHTEST);
    previewPass.setRenderFlag(RENDER_LIGHTING);
    previewPass.setRenderFlag(RENDER_SMOOTH);

	// Don't let translucent materials write to the depth buffer
	if (!(_material->getMaterialFlags() & Material::FLAG_TRANSLUCENT))
	{
		previewPass.setRenderFlag(RENDER_DEPTHWRITE);
	}

    // Handle certain shader flags
	if (_material->getCullType() != Material::CULL_NONE)
    {
        previewPass.setRenderFlag(RENDER_CULLFACE);
    }

    // Set up blend properties
    determineBlendModeForEditorPass(previewPass);

    // Sort position
    if (_material->getSortRequest() >= Material::SORT_DECAL)
    {
        previewPass.setSortPosition(OpenGLState::SORT_OVERLAY_FIRST);
    }
    else if (previewPass.getSortPosition() != OpenGLState::SORT_TRANSLUCENT)
    {
        previewPass.setSortPosition(OpenGLState::SORT_FULLBRIGHT);
    }

    // Polygon offset
    previewPass.polygonOffset = _material->getPolygonOffset();
}

// Append a blend (non-interaction) layer
void OpenGLShader::appendBlendLayer(const IShaderLayer::Ptr& layer)
{
    TexturePtr layerTex = layer->getTexture();

    if (!layerTex) return;

    OpenGLState& state = appendDefaultPass();
    state.setRenderFlag(RENDER_FILL);
    state.setRenderFlag(RENDER_BLEND);
    state.setRenderFlag(RENDER_DEPTHTEST);
    state.setDepthFunc(GL_LEQUAL);

	// Remember the stage for later evaluation of shader expressions
	state.stage0 = layer;

    // Set the texture
    state.texture0 = layerTex->getGLTexNum();

    // Get the blend function
    BlendFunc blendFunc = layer->getBlendFunc();
    state.m_blend_src = blendFunc.src;
    state.m_blend_dst = blendFunc.dest;

    if (_material->getCoverage() == Material::MC_TRANSLUCENT)
    {
        // Material is blending with the background, don't write to the depth buffer
        state.clearRenderFlag(RENDER_DEPTHWRITE);
    }
	// Alpha-tested stages or one-over-zero blends should use the depth buffer
    else if (state.m_blend_src == GL_SRC_ALPHA || state.m_blend_dst == GL_SRC_ALPHA ||
		     (state.m_blend_src == GL_ONE && state.m_blend_dst == GL_ZERO))
    {
		state.setRenderFlag(RENDER_DEPTHWRITE);
    }

    // Set texture dimensionality (cube map or 2D)
    state.cubeMapMode = layer->getCubeMapMode();
    if (state.cubeMapMode == IShaderLayer::CUBE_MAP_CAMERA)
    {
        state.glProgram = _renderSystem.getGLProgramFactory().getBuiltInProgram(ShaderProgram::CubeMap);
        state.setRenderFlag(RENDER_PROGRAM);
        state.setRenderFlag(RENDER_TEXTURE_CUBEMAP);
        state.clearRenderFlag(RENDER_TEXTURE_2D);
    }
    else
    {
        state.setRenderFlag(RENDER_TEXTURE_2D);
    }

    // Colour modulation
    state.setColour(layer->getColour());

	// Sort position
    if (_material->getSortRequest() >= Material::SORT_DECAL)
    {
        state.setSortPosition(OpenGLState::SORT_OVERLAY_FIRST);
    }
    else
    {
        state.setSortPosition(OpenGLState::SORT_FULLBRIGHT);
	}

    // Polygon offset
    state.polygonOffset = _material->getPolygonOffset();

#if 0
    if (!layer->getVertexProgram().empty() || !layer->getFragmentProgram().empty())
    {
        try
        {
            state.glProgram = _renderSystem.getGLProgramFactory().getProgram(
                layer->getVertexProgram(),
                layer->getFragmentProgram()
            );
        }
        catch (std::runtime_error& ex)
        {
            rError() << "Failed to create GL program for material " <<
                _material->getName() << ": " << ex.what() << std::endl;

            state.glProgram = nullptr;
        }
    }
#endif
}

// Construct a normal shader
void OpenGLShader::constructNormalShader()
{
    constructFromMaterial(GlobalMaterialManager().getMaterial(_name));
}

void OpenGLShader::constructFromMaterial(const MaterialPtr& material)
{
    assert(material);

    _material = material;

    _materialChanged = _material->sig_materialChanged().connect(
        sigc::mem_fun(this, &OpenGLShader::onMaterialChanged));

    // Determine whether we can render this shader in lighting/bump-map mode,
    // and construct the appropriate shader passes
    if (canUseLightingMode())
    {
        // Full lighting, DBS and blend modes
        constructLightingPassesFromMaterial();
    }
    else
    {
        // Editor image rendering only
        constructEditorPreviewPassFromMaterial();
    }
}

void OpenGLShader::construct()
{
    switch (_name[0])
    {
    // greebo: For a small amount of commits, I'll leave these here to catch my attention
    case '(': // fill shader
    case '[':
    case '<': // wireframe shader
    case '{': // cam + wireframe shader
    case '$': // hardcoded legacy stuff
    {
        rWarning() << "Legacy shader request encountered" << std::endl;
        assert(false);
        return;
    }
    }

    // Construct the shader from the material definition
    constructNormalShader();
    enableViewType(RenderViewType::Camera);
}

void OpenGLShader::onMaterialChanged()
{
    // It's possible that the name of the material got changed, update it
    if (_material && _material->getName() != _name)
    {
        _name = _material->getName();
    }

    unrealise();
    realise();
}

bool OpenGLShader::isApplicableTo(RenderViewType renderViewType) const
{
    return (_enabledViewTypes & static_cast<std::size_t>(renderViewType)) != 0;
}

void OpenGLShader::enableViewType(RenderViewType renderViewType)
{
    _enabledViewTypes |= static_cast<std::size_t>(renderViewType);
}

const IBackendWindingRenderer& OpenGLShader::getWindingRenderer() const
{
    return *_windingRenderer;
}

void OpenGLShader::setWindingRenderer(std::unique_ptr<IBackendWindingRenderer> renderer)
{
    _windingRenderer = std::move(renderer);
}

bool OpenGLShader::isMergeModeEnabled() const
{
    return _mergeModeActive;
}

void OpenGLShader::setMergeModeEnabled(bool enabled)
{
    if (_mergeModeActive == enabled) return;

    _mergeModeActive = enabled;

    onMergeModeChanged();
}

void OpenGLShader::foreachPass(const std::function<void(OpenGLShaderPass&)>& functor)
{
    for (auto& pass : _shaderPasses)
    {
        functor(*pass);
    }
}

void OpenGLShader::foreachNonInteractionPass(const std::function<void(OpenGLShaderPass&)>& functor)
{
    for (auto& pass : _shaderPasses)
    {
        if (pass != _depthFillPass && pass != _interactionPass)
        {
            functor(*pass);
        }
    }
}

OpenGLShaderPass* OpenGLShader::getDepthFillPass() const
{
    return _depthFillPass.get();
}

OpenGLShaderPass* OpenGLShader::getInteractionPass() const
{
    return _interactionPass.get();
}

}

