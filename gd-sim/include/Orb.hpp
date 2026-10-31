#pragma once
#include <EffectObject.hpp>

enum class OrbType {
	Yellow,
	Blue,
	Pink,
	Red,
	Green,
	Black,
	Toggle, // toggle orb (1594): a click turns its target group on or off, no jump
	Spider, // spider orb (3004): a click teleports the player like a spider's jump, in any mode
	Dash, // dash orb (1704): a click dashes at the orb's angle while the button is held
	GravityDash, // gravity dash orb (1751): flips gravity, then dashes
};

struct Orb : public EffectObject {
	OrbType type;
	/// Toggle orb: its target group, negative to turn it off (0 for other orbs)
	int toggleGroup = 0;
	/// GameObject::isFacingDown (a spider orb sends the player towards the side it faces)
	bool facingDown = false;
	/// Dash orbs: tan of the dash angle (PlayerObject::startDashing: the orb's rotation, mirrored into +-90, capped at 70)
	double dashSlope = 0;

	Orb(Vec2D size, std::unordered_map<int, std::string>&& fields);
	bool touching(Player const&) const override;
	void collide(Player&) const override;
};