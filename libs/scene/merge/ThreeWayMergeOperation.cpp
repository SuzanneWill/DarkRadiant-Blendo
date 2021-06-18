#include "ThreeWayMergeOperation.h"

#include "itextstream.h"
#include "NodeUtils.h"

namespace scene
{

namespace merge
{

ThreeWayMergeOperation::ThreeWayMergeOperation(const scene::IMapRootNodePtr& baseRoot,
    const scene::IMapRootNodePtr& sourceRoot, const scene::IMapRootNodePtr& targetRoot) :
    _baseRoot(baseRoot),
    _sourceRoot(sourceRoot),
    _targetRoot(targetRoot)
{}

ThreeWayMergeOperation::~ThreeWayMergeOperation()
{
    // Clear the actions held by the base class before the root nodes are cleared
    clearActions();
}

std::list<ComparisonResult::KeyValueDifference>::const_iterator ThreeWayMergeOperation::FindTargetDiffByKey(
    const std::list<ComparisonResult::KeyValueDifference>& targetKeyValueDiffs, const std::string& key)
{
    return std::find_if(targetKeyValueDiffs.begin(), targetKeyValueDiffs.end(),
        [&](const ComparisonResult::KeyValueDifference& diff)
    {
        return string::iequals(diff.key, key);
    });
}

bool ThreeWayMergeOperation::KeyValueDiffHasConflicts(const ComparisonResult::KeyValueDifference& sourceKeyValueDiff, 
    const ComparisonResult::KeyValueDifference& targetKeyValueDiff)
{
    assert(string::iequals(targetKeyValueDiff.key, sourceKeyValueDiff.key));

    // Key is matching, there's still a chance that this is not a conflict
    switch (targetKeyValueDiff.type)
    {
    // If both are removing the key, that's fine
    case ComparisonResult::KeyValueDifference::Type::KeyValueRemoved:
        return targetKeyValueDiff.type != sourceKeyValueDiff.type;

    // On key value change or add, the value must be the same to not conflict
    case ComparisonResult::KeyValueDifference::Type::KeyValueAdded:
    case ComparisonResult::KeyValueDifference::Type::KeyValueChanged:
        return sourceKeyValueDiff.type == ComparisonResult::KeyValueDifference::Type::KeyValueRemoved ||
            sourceKeyValueDiff.value != targetKeyValueDiff.value;
    }

    throw std::logic_error("Unhandled key value diff type in ThreeWayMergeOperation::KeyValueDiffHasConflicts");
}

void ThreeWayMergeOperation::processEntityModification(const ComparisonResult::EntityDifference& sourceDiff, 
    const ComparisonResult::EntityDifference& targetDiff)
{
    assert(sourceDiff.type == ComparisonResult::EntityDifference::Type::EntityPresentButDifferent);

    if (targetDiff.type == ComparisonResult::EntityDifference::Type::EntityMissingInBase)
    {
        // The target cannot possibly add this entity when in the source diff it's present in the base
        throw std::logic_error("Entity " + sourceDiff.entityName + " is marked as modified in source, but as added in the target.");
    }

    if (targetDiff.type == ComparisonResult::EntityDifference::Type::EntityMissingInSource)
    {
        // This is a conflicting change, the source modified it, the target removed it
        // When the user chooses to import the change, it will be an AddEntity action
        addAction(std::make_shared<EntityConflictResolutionAction>(
            targetDiff.sourceNode,
            std::make_shared<AddEntityAction>(sourceDiff.sourceNode, _targetRoot)
        ));
        return;
    }

    // Both graphs modified this entity, do an in-depth comparison
    auto targetChildren = NodeUtils::CollectPrimitiveFingerprints(targetDiff.sourceNode);

    // Every primitive change that has been done to the target map can be applied
    // to the source map, since we can't detect whether one of them has been moved or retextured
    for (const auto& primitiveDiff : sourceDiff.differingChildren)
    {
        bool primitivePresentInTargetMap = targetChildren.count(primitiveDiff.fingerprint) != 0;

        switch (primitiveDiff.type)
        {
        case ComparisonResult::PrimitiveDifference::Type::PrimitiveAdded:
        {
            // Add this primitive if it isn't there already
            if (!primitivePresentInTargetMap)
            {
                addAction(std::make_shared<AddChildAction>(primitiveDiff.node, targetDiff.sourceNode));
            }
            break;
        }

        case ComparisonResult::PrimitiveDifference::Type::PrimitiveRemoved:
            // Check if this primitive is still present in the target map, otherwise we can't remove it
            if (primitivePresentInTargetMap)
            {
                addAction(std::make_shared<RemoveChildAction>(targetChildren[primitiveDiff.fingerprint]));
            }
            break;
        }
    }

    // The key value changes can be applied only if they are not targeting the same key
    // unless the change has actually the same outcome
    for (const auto& sourceKeyValueDiff : sourceDiff.differingKeyValues)
    {
        auto targetKeyValueDiff = FindTargetDiffByKey(targetDiff.differingKeyValues, sourceKeyValueDiff.key);

        if (targetKeyValueDiff == targetDiff.differingKeyValues.end())
        {
            // Not a key that changed in the target, accept this change
            addActionForKeyValueDiff(sourceKeyValueDiff, targetDiff.sourceNode);
            continue;
        }

        // Ignore NOP changes, when the target obviously made the same change
        if (sourceKeyValueDiff == *targetKeyValueDiff)
        {
            continue;
        }

        // Check if this key change is conflicting with the target change
        if (!KeyValueDiffHasConflicts(sourceKeyValueDiff, *targetKeyValueDiff))
        {
            // Accept this change
            addActionForKeyValueDiff(sourceKeyValueDiff, targetDiff.sourceNode);
        }
        else
        {
            // Create a conflict resolution action for this key value change
            addAction(std::make_shared<EntityKeyValueConflictResolutionAction>(
                targetDiff.sourceNode, // conflicting entity
                createActionForKeyValueDiff(sourceKeyValueDiff, targetDiff.sourceNode), // conflicting source change 
                createActionForKeyValueDiff(*targetKeyValueDiff, targetDiff.sourceNode) // conflicting target change
            ));
        }
    }
}

void ThreeWayMergeOperation::processEntityDifferences(const std::list<ComparisonResult::EntityDifference>& sourceDiffs,
    const std::list<ComparisonResult::EntityDifference>& targetDiffs)
{
    // Create source and target entity diff dictionaries (by entity name)
    for (auto it = sourceDiffs.begin(); it != sourceDiffs.end(); ++it)
    {
        _sourceDifferences[it->entityName] = it;
    }

    for (auto it = targetDiffs.begin(); it != targetDiffs.end(); ++it)
    {
        _targetDifferences[it->entityName] = it;
    }

    // Collect a map of all target entities for faster lookup later
    _targetRoot->foreachNode([&](const INodePtr& candidate)
    {
        if (candidate->getNodeType() == INode::Type::Entity)
        {
            _targetEntities.emplace(NodeUtils::GetEntityName(candidate), candidate);
        }

        return true;
    });

    // Check each entity difference from the base to the source map
    // fully accept only those entities that are not altered in the target map, and detect conflicts
    for (const auto& pair : _sourceDifferences)
    {
        auto targetDiff = _targetDifferences.find(pair.first);

        if (targetDiff == _targetDifferences.end())
        {
            // Change is targeting an entity that has not been altered in the source map => accept
            switch (pair.second->type)
            {
            case ComparisonResult::EntityDifference::Type::EntityMissingInSource:
                {
                    auto entityToRemove = findTargetEntityByName(pair.first);
                    assert(entityToRemove);
                    addAction(std::make_shared<RemoveEntityAction>(entityToRemove));
                }
                break;

            case ComparisonResult::EntityDifference::Type::EntityMissingInBase:
                addAction(std::make_shared<AddEntityAction>(pair.second->sourceNode, _targetRoot));
                break;

            case ComparisonResult::EntityDifference::Type::EntityPresentButDifferent:
                {
                    auto entityToModify = findTargetEntityByName(pair.first);
                    assert(entityToModify);

                    for (const auto& keyValueDiff : pair.second->differingKeyValues)
                    {
                        addActionForKeyValueDiff(keyValueDiff, entityToModify);
                    }

                    for (const auto& primitiveDiff : pair.second->differingChildren)
                    {
                        addActionsForPrimitiveDiff(primitiveDiff, entityToModify);
                    }
                }
                break;
            };
            continue;
        }

        // Check diff type (entity add/remove)
        switch (pair.second->type)
        {
        case ComparisonResult::EntityDifference::Type::EntityMissingInBase: // entity was added to source
            
            if (targetDiff->second->type == ComparisonResult::EntityDifference::Type::EntityMissingInSource ||
                targetDiff->second->type == ComparisonResult::EntityDifference::Type::EntityPresentButDifferent)
            {
                // The target cannot remove or modify an entity that has been marked as added in the source diff
                throw std::logic_error("Error " + pair.first + " was marked as added in source, but removed/modified in target");
            }

            // Both graphs had this entity added, mark this for inclusion
            // unless it turns out this added entity in the source is 100% the same as in the target
            if (pair.second->sourceFingerprint != targetDiff->second->sourceFingerprint)
            {
                addAction(std::make_shared<AddEntityAction>(pair.second->sourceNode, _targetRoot));
            }
            break;

        case ComparisonResult::EntityDifference::Type::EntityMissingInSource: // entity was removed in source
            
            if (targetDiff->second->type == ComparisonResult::EntityDifference::Type::EntityMissingInBase)
            {
                // The target cannot add an entity that has been marked as removed in the source diff, it was already there
                throw std::logic_error("Error " + pair.first + " was marked as removed in source, but as added in target");
            }

            if (targetDiff->second->type == ComparisonResult::EntityDifference::Type::EntityMissingInSource)
            {
                // Entity is gone in the target too, nothing to do here
                continue;
            }

            // Entity has been removed in source, but modified in target, this is a conflict
            addAction(std::make_shared<EntityConflictResolutionAction>(
                targetDiff->second->sourceNode, // conflicting entity
                std::make_shared<RemoveEntityAction>(targetDiff->second->sourceNode) // conflicting change 
            ));

            break;
        
        case ComparisonResult::EntityDifference::Type::EntityPresentButDifferent:
            // This entity has been modified in the source, check the target diff
            processEntityModification(*pair.second, *targetDiff->second);
            break;
        }
    }

    // Cleanup temporary data
    _sourceDifferences.clear();
    _targetDifferences.clear();
    _targetEntities.clear();
}

ThreeWayMergeOperation::Ptr ThreeWayMergeOperation::CreateFromComparisonResults(
    const ComparisonResult& baseToSource, const ComparisonResult& baseToTarget)
{
    if (baseToSource.getBaseRootNode() != baseToTarget.getBaseRootNode())
    {
        throw std::runtime_error("The base scene of the two comparison results must be the same");
    }

    auto operation = std::make_shared<ThreeWayMergeOperation>(baseToSource.getBaseRootNode(), 
        baseToSource.getSourceRootNode(), baseToTarget.getSourceRootNode());

    operation->processEntityDifferences(baseToSource.differingEntities, baseToTarget.differingEntities);

    return operation;
}

void ThreeWayMergeOperation::setMergeSelectionGroups(bool enabled)
{
    // TODO
}

void ThreeWayMergeOperation::setMergeLayers(bool enabled)
{
    // TODO
}

INodePtr ThreeWayMergeOperation::findTargetEntityByName(const std::string& name)
{
    auto found = _targetEntities.find(name);
    return found != _targetEntities.end() ? found->second : INodePtr();
}

}

}