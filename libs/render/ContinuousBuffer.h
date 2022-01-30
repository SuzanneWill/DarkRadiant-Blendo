#pragma once

#include <cstdint>
#include <stack>
#include <limits>
#include <vector>

namespace render
{

/**
 * Buffer object managing allocations within a continuous block of memory.
 * 
 * While the memory location itself might change when the buffer is growing,
 * the whole data is always stored in a single continuous memory block.
 * 
 * Use the allocate/deallocate methods to acquire or release a chunk of
 * a certain size. The chunk size is fixed and cannot be changed.
 */
template<typename ElementType>
class ContinuousBuffer
{
public:
    static constexpr std::size_t DefaultInitialSize = 65536;

    using Handle = std::uint32_t;

private:
    static constexpr std::size_t GrowthRate = 1; // 100% growth each time

    std::vector<ElementType> _buffer;

    struct SlotInfo
    {
        bool Occupied;      // whether this slot is free
        std::size_t Offset; // The index to the first element within the buffer
        std::size_t Size;   // Number of allocated elements

        SlotInfo(std::size_t offset, std::size_t size, bool occupied) :
            Occupied(occupied),
            Offset(offset),
            Size(size)
        {}
    };

    std::vector<SlotInfo> _slots;

    // A stack of slots that can be re-used instead
    std::stack<Handle> _emptySlots;

public:
    ContinuousBuffer(std::size_t initialSize = DefaultInitialSize)
    {
        // Pre-allocate some memory, but don't go all the way down to zero
        _buffer.resize(initialSize == 0 ? 16 : initialSize);

        // The initial slot info which is going to be cut into pieces
        createSlotInfo(0, _buffer.size());
    }

    Handle allocate(std::size_t requiredSize)
    {
        return getNextFreeSlotForSize(requiredSize);
    }

    ElementType* getBufferStart()
    {
        return _buffer.data();
    }

    std::size_t getSize(Handle handle) const
    {
        return _slots[handle].Size;
    }

    std::size_t getOffset(Handle handle) const
    {
        return _slots[handle].Offset;
    }

    void setData(Handle handle, const std::vector<ElementType>& elements)
    {
        const auto& slot = _slots[handle];

        if (elements.size() != slot.Size)
        {
            throw std::logic_error("Allocation size mismatch in GeometryStore::Buffer::setData");
        }

        std::copy(elements.begin(), elements.end(), _buffer.begin() + slot.Offset);
    }

    void deallocate(Handle handle)
    {
        auto& releasedSlot = _slots[handle];
        releasedSlot.Occupied = false;

        // Check if the slot can merge with an adjacent one
        Handle slotIndexToMerge = std::numeric_limits<Handle>::max();
        if (findLeftFreeSlot(releasedSlot, slotIndexToMerge))
        {
            auto& slotToMerge = _slots[slotIndexToMerge];
            
            releasedSlot.Offset = slotToMerge.Offset;
            releasedSlot.Size += slotToMerge.Size;

            // The merged handle goes to recycling, block it against future use
            slotToMerge.Size = 0;
            slotToMerge.Occupied = true;
            _emptySlots.push(slotIndexToMerge);
        }

        // Try to find an adjacent free slot to the right
        if (findRightFreeSlot(releasedSlot, slotIndexToMerge))
        {
            auto& slotToMerge = _slots[slotIndexToMerge];

            releasedSlot.Size += slotToMerge.Size;

            // The merged handle goes to recycling, block it against future use
            slotToMerge.Size = 0;
            slotToMerge.Occupied = true;
            _emptySlots.push(slotIndexToMerge);
        }
    }

private:
    bool findLeftFreeSlot(const SlotInfo& slotToTouch, Handle& found)
    {
        auto numSlots = _slots.size();

        for (Handle slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        {
            const auto& candidate = _slots[slotIndex];

            if (candidate.Offset + candidate.Size == slotToTouch.Offset)
            {
                // The slot coordinates match, return true if this block is free
                found = slotIndex;
                return !candidate.Occupied;
            }
        }

        return false;
    }

    bool findRightFreeSlot(const SlotInfo& slotToTouch, Handle& found)
    {
        auto numSlots = _slots.size();
        auto offsetToMatch = slotToTouch.Offset + slotToTouch.Size;

        for (Handle slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        {
            const auto& candidate = _slots[slotIndex];

            if (candidate.Offset == offsetToMatch)
            {
                // The slot coordinates match, return true if this block is free
                found = slotIndex;
                return !candidate.Occupied;
            }
        }

        return false;
    }

    Handle getNextFreeSlotForSize(std::size_t requiredSize)
    {
        auto numSlots = _slots.size();
        Handle rightmostFreeSlotIndex = 0;
        std::size_t rightmostFreeOffset = 0;

        for (Handle slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        {
            auto& slot = _slots[slotIndex];

            if (slot.Occupied) continue;

            // Keep track of the highest slot, we need that when re-allocating
            if (slot.Offset > rightmostFreeOffset)
            {
                rightmostFreeOffset = slot.Offset;
                rightmostFreeSlotIndex = slotIndex;
            }

            if (slot.Size < requiredSize) continue; // this slot is no use for us

            // Calculate the remaining size before assignment
            auto remainingSize = slot.Size - requiredSize;
            slot.Size = requiredSize;
            slot.Occupied = true;

            if (remainingSize > 0)
            {
                // Allocate a new free slot with the remaining space
                createSlotInfo(slot.Offset + requiredSize, remainingSize);
            }

            return slotIndex;
        }

        // No space wherever, we need to expand the buffer
        auto additionalSize = std::max(_buffer.size() * GrowthRate, requiredSize);
        _buffer.resize(_buffer.size() + additionalSize);

        // Use the right most slot for our requirement, then cut up the rest of the space
        auto& rightmostFreeSlot = _slots[rightmostFreeSlotIndex];

        assert(rightmostFreeSlot.Size < requiredSize); // otherwise we've run wrong above

        auto remainingSize = rightmostFreeSlot.Size + additionalSize - requiredSize;

        rightmostFreeSlot.Occupied = true;
        rightmostFreeSlot.Size = requiredSize;

        createSlotInfo(rightmostFreeSlot.Offset + rightmostFreeSlot.Size, remainingSize);

        return rightmostFreeSlotIndex;
    }

    SlotInfo& createSlotInfo(std::size_t offset, std::size_t size, bool occupied = false)
    {
        if (_emptySlots.empty())
        {
            return _slots.emplace_back(offset, size, occupied);
        }

        // Re-use an old slot
        auto& slot = _slots.at(_emptySlots.top());
        _emptySlots.pop();

        slot.Occupied = occupied;
        slot.Offset = offset;
        slot.Size = size;

        return slot;
    }
};

}