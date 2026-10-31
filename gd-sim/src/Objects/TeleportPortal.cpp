#include <Portals.hpp>
#include <Player.hpp>
#include <Level.hpp>
#include <cstdlib>

TeleportPortal::TeleportPortal(Vec2D size, std::unordered_map<int, std::string>&& fields)
	: EffectObject(size, std::move(fields)), offset((float)stod_def(fields[54], 0)) {
	int id = atoi(fields[1].c_str());
	if (id == 2902 || id == 3027) {
		target = atoi(fields[51].c_str());
		orb = id == 3027;
	}
}

void TeleportPortal::collide(Player& p) const {
	if (orb && !(p.buffer || (p.prevPlayer().buffer && !p.button)))
		return;
	EffectObject::collide(p);
	if (!target) {
		p.pos.y += offset;
		return;
	}
	// GJBaseGameLayer::teleportPlayer (default options): to the target object's position (moved with its groups)
	auto it = p.level->groupCenters.find(target);
	if (it == p.level->groupCenters.end())
		return;
	Vec2D to = it->second.pos;
	for (int g : it->second.groups) {
		auto off = p.level->groupOffset(g, p.frame - 1);
		to.x += off.x;
		to.y += off.y;
	}
	if (orb) {
		p.buffer = false;
		p.clickedOrb = true;
		p.clickUsed = true;
	}
	if (to.x > p.pos.x)
		p.pos.x = to.x;
	p.pos.y = to.y;
}
