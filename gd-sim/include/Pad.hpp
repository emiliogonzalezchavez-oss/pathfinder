#pragma once
#include <EffectObject.hpp>

enum class PadType {
	Yellow,
	Blue,
	Pink,
	Red,
	Spider, // spider pad (3005): teleports the player like a spider's jump, in any mode
};

struct Pad : public EffectObject {
	PadType type;
	/// Gravity a blue pad sets
	bool flipsToUpsideDown;

	Pad(Vec2D size, std::unordered_map<int, std::string>&& fields);
	void collide(Player&) const override;
};