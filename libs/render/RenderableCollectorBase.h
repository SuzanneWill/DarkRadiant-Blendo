#pragma once

#include "irenderable.h"
#include "imapmerge.h"
#include "inode.h"
#include "iselection.h"

namespace render
{

/**
 * Front end renderer base implementation shared by CamRenderer and XYRenderer
 * 
 * Provides support for highlighting selected objects by activating the
 * RenderableCollector's "highlight" flags based on the renderable object's
 * selection state.
 */
class RenderableCollectorBase :
    public IRenderableCollector
{
public:
    virtual void processNode(const scene::INodePtr& node, const VolumeTest& volume)
    {
        node->onPreRender(volume);

        // greebo: Highlighting propagates to child nodes
        scene::INodePtr parent = node->getParent();

        node->viewChanged();

        std::size_t highlightFlags = node->getHighlightFlags();

        auto nodeType = node->getNodeType();

        // Particle nodes shouldn't inherit the highlight flags from their parent, 
        // as it obstructs the view when their wireframe gets rendered (#5682)
        if (parent && nodeType != scene::INode::Type::Particle)
        {
            highlightFlags |= parent->getHighlightFlags();
        }

        if (nodeType == scene::INode::Type::MergeAction)
        {
            setHighlightFlag(Highlight::MergeAction, true);

            auto mergeActionNode = std::dynamic_pointer_cast<scene::IMergeActionNode>(node);
            assert(mergeActionNode);

            switch (mergeActionNode->getActionType())
            {
            case scene::merge::ActionType::AddChildNode:
            case scene::merge::ActionType::AddEntity:
                setHighlightFlag(Highlight::MergeActionAdd, true);
                setHighlightFlag(Highlight::MergeActionChange, false);
                setHighlightFlag(Highlight::MergeActionRemove, false);
                setHighlightFlag(Highlight::MergeActionConflict, false);
                break;

            case scene::merge::ActionType::AddKeyValue:
            case scene::merge::ActionType::ChangeKeyValue:
            case scene::merge::ActionType::RemoveKeyValue:
                setHighlightFlag(Highlight::MergeActionChange, true);
                setHighlightFlag(Highlight::MergeActionAdd, false);
                setHighlightFlag(Highlight::MergeActionRemove, false);
                setHighlightFlag(Highlight::MergeActionConflict, false);
                break;

            case scene::merge::ActionType::RemoveChildNode:
            case scene::merge::ActionType::RemoveEntity:
                setHighlightFlag(Highlight::MergeActionRemove, true);
                setHighlightFlag(Highlight::MergeActionAdd, false);
                setHighlightFlag(Highlight::MergeActionChange, false);
                setHighlightFlag(Highlight::MergeActionConflict, false);
                break;

            case scene::merge::ActionType::ConflictResolution:
                setHighlightFlag(Highlight::MergeActionConflict, true);
                setHighlightFlag(Highlight::MergeActionAdd, false);
                setHighlightFlag(Highlight::MergeActionChange, false);
                setHighlightFlag(Highlight::MergeActionRemove, false);
                break;
            }
        }
        else
        {
            setHighlightFlag(Highlight::MergeAction, false);
            setHighlightFlag(Highlight::MergeActionAdd, false);
            setHighlightFlag(Highlight::MergeActionChange, false);
            setHighlightFlag(Highlight::MergeActionRemove, false);
            setHighlightFlag(Highlight::MergeActionConflict, false);
        }

        if (highlightFlags & Renderable::Highlight::Selected)
        {
            if (GlobalSelectionSystem().Mode() != selection::SelectionSystem::eComponent)
            {
                setHighlightFlag(Highlight::Faces, true);
            }
            else
            {
                setHighlightFlag(Highlight::Faces, false);
                node->renderComponents(*this, volume);
            }

            setHighlightFlag(Highlight::Primitives, true);

            // Pass on the info about whether we have a group member selected
            if (highlightFlags & Renderable::Highlight::GroupMember)
            {
                setHighlightFlag(Highlight::GroupMember, true);
            }
            else
            {
                setHighlightFlag(Highlight::GroupMember, false);
            }
        }
        else
        {
            setHighlightFlag(Highlight::Primitives, false);
            setHighlightFlag(Highlight::Faces, false);
            setHighlightFlag(Highlight::GroupMember, false);
        }

        // If any of the above concludes that this node should be highlighted,
        // ask it to submit its geometry. Oriented nodes are submitting every frame.
        if (hasHighlightFlags() || node->isOriented())
        {
            processRenderable(*node, volume);
        }
    }
};

}