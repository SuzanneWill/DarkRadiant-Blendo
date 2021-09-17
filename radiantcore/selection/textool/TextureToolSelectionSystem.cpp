#include "TextureToolSelectionSystem.h"

#include "itextstream.h"
#include "module/StaticModule.h"
#include "../manipulators/TextureToolRotateManipulator.h"

namespace textool
{

const std::string& TextureToolSelectionSystem::getName() const
{
    static std::string _name(MODULE_TEXTOOL_SELECTIONSYSTEM);
    return _name;
}

const StringSet& TextureToolSelectionSystem::getDependencies() const
{
    static StringSet _dependencies{ MODULE_TEXTOOL_SCENEGRAPH };
    return _dependencies;
}

void TextureToolSelectionSystem::initialiseModule(const IApplicationContext& ctx)
{
    rMessage() << getName() << "::initialiseModule called." << std::endl;

    registerManipulator(std::make_shared<selection::TextureToolRotateManipulator>());

    _defaultManipulatorType = selection::IManipulator::Rotate;
    setActiveManipulator(_defaultManipulatorType);
}

void TextureToolSelectionSystem::shutdownModule()
{
    _sigActiveManipulatorChanged.clear();
    _manipulators.clear();
}

void TextureToolSelectionSystem::foreachSelectedNode(const std::function<bool(const INode::Ptr&)>& functor)
{
    GlobalTextureToolSceneGraph().foreachNode([&](const INode::Ptr& node)
    {
        if (node->isSelected())
        {
            return functor(node);
        }

        return true;
    });
}

std::size_t TextureToolSelectionSystem::registerManipulator(const selection::ITextureToolManipulator::Ptr& manipulator)
{
    std::size_t newId = 1;

    while (_manipulators.count(newId) > 0)
    {
        ++newId;

        if (newId == std::numeric_limits<std::size_t>::max())
        {
            throw std::runtime_error("Out of manipulator IDs");
        }
    }

    _manipulators.emplace(newId, manipulator);

    manipulator->setId(newId);

    if (!_activeManipulator)
    {
        _activeManipulator = manipulator;
    }

    return newId;
}

void TextureToolSelectionSystem::unregisterManipulator(const selection::ITextureToolManipulator::Ptr& manipulator)
{
    for (auto i = _manipulators.begin(); i != _manipulators.end(); ++i)
    {
        if (i->second == manipulator)
        {
            i->second->setId(0);
            _manipulators.erase(i);
            return;
        }
    }
}

selection::IManipulator::Type TextureToolSelectionSystem::getActiveManipulatorType()
{
    return _activeManipulator->getType();
}

const selection::ITextureToolManipulator::Ptr& TextureToolSelectionSystem::getActiveManipulator()
{
    return _activeManipulator;
}

void TextureToolSelectionSystem::setActiveManipulator(std::size_t manipulatorId)
{
    auto found = _manipulators.find(manipulatorId);

    if (found == _manipulators.end())
    {
        rError() << "Cannot activate non-existent manipulator ID " << manipulatorId << std::endl;
        return;
    }

    _activeManipulator = found->second;
#if 0
    // Release the user lock when switching manipulators
    _pivot.setUserLocked(false);

    pivotChanged();
#endif
}

void TextureToolSelectionSystem::setActiveManipulator(selection::IManipulator::Type manipulatorType)
{
    for (const auto& pair : _manipulators)
    {
        if (pair.second->getType() == manipulatorType)
        {
            _activeManipulator = pair.second;
#if 0
            // Release the user lock when switching manipulators
            _pivot.setUserLocked(false);

            pivotChanged();
#endif
            return;
        }
    }

    rError() << "Cannot activate non-existent manipulator by type " << manipulatorType << std::endl;
}

sigc::signal<void, selection::IManipulator::Type>& TextureToolSelectionSystem::signal_activeManipulatorChanged()
{
    return _sigActiveManipulatorChanged;
}

module::StaticModule<TextureToolSelectionSystem> _textureToolSelectionSystemModule;

}