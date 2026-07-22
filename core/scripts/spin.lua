-- spin.lua - Phase 16 test script: rotates the entity around Y at a fixed
-- rate, proving ScriptEngine's Transform binding round-trips both directions
-- (reads the current rotation, writes an updated one back).
function onUpdate(transform, dt)
    transform.rotationEulerDegrees.y = transform.rotationEulerDegrees.y + 90.0 * dt
end
