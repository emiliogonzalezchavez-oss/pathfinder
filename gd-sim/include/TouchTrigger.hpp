#pragma once
#include <EffectObject.hpp>

/// A touch-triggered trigger: touching it records the touch on the player (Level::triggerActivation reads it)
struct TouchTrigger : public EffectObject {
    int trigger = 0;
    using EffectObject::EffectObject;
    void collide(Player&) const override;
};
