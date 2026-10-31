#include <Portals.hpp>
#include <Player.hpp>
#include <Level.hpp>
#include <cstdlib>
#include <cmath>

TeleportPortal::TeleportPortal(Vec2D size, std::unordered_map<int, std::string>&& fields)
	: EffectObject(size, std::move(fields)), offset((float)stod_def(fields[54], 0)) {
	int id = atoi(fields[1].c_str());
	if (id == 2902 || id == 3027) {
		target = atoi(fields[51].c_str());
		orb = id == 3027;
		ignoreX = fields[352] == "1";
		ignoreY = fields[353] == "1";
		gravityMode = atoi(fields[354].c_str());
		staticForce = fields[345] == "1";
		force = (float)stod_def(fields[346], 0);
		additive = fields[443] == "1";
		// (the force's direction: 90 minus the object's clockwise rotation, +180 flipped in x)
		angle = 90 - (float)stod_def(fields[6], 0) + (fields[4] == "1" ? 180 : 0);
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
	if (!ignoreX && to.x > p.pos.x)
		p.pos.x = to.x;
	if (!ignoreY)
		p.pos.y = to.y;
	// teleportPlayer: the gravity option, then the static force (its y part, world up; 0 and not additive: stopped)
	if (gravityMode == 1 && p.upsideDown) p.flipGravity(false);
	else if (gravityMode == 2 && !p.upsideDown) p.flipGravity(true);
	else if (gravityMode == 3) p.flipGravity(!p.upsideDown);
	if (staticForce) {
		if (force == 0 && !additive) {
			p.accelerating = false;
			p.velocity = 0;
		} else {
			float rad = angle * 0.017453292f;
			double vy = (double)(std::sin(rad) * force);
			double world = (additive ? p.velocity / 54 * (p.upsideDown ? -1 : 1) : 0) + vy;
			p.accelerating = true;
			p.velocity = world * 54 * (p.upsideDown ? -1 : 1);
			p.grounded = false;
		}
	}
}
