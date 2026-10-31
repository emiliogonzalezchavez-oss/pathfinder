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
};

struct Orb : public EffectObject {
	OrbType type;
	/// Toggle orb: its target group, negative to turn it off (0 for other orbs)
	int toggleGroup = 0;
	/// GameObject::isFacingDown (a spider orb sends the player towards the side it faces)
	bool facingDown = false;

	Orb(Vec2D size, std::unordered_map<int, std::string>&& fields);
	bool touching(Player const&) const override;
	void collide(Player&) const override;
};