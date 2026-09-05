#pragma once

// Visibility belongs to the viewing player, not to the owner of the unit.
// Reflection eligibility must never override concealment or reveal shrouded units.
inline bool NativeReflectionDrawableVisible(bool authoredReflection, bool unit,
    bool hidden, bool shrouded, bool forceVisible, bool culled)
{
    return !hidden && !shrouded && (authoredReflection || unit || forceVisible) &&
        (forceVisible || !culled);
}
