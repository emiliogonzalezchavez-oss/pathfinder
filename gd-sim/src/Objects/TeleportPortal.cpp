#include <Portals.hpp>
#include <Player.hpp>

TeleportPortal::TeleportPortal(Vec2D size, std::unordered_map<int, std::string>&& fields)
	: EffectObject(size, std::move(fields)), offset((float)stod_def(fields[54], 0)) {}

void TeleportPortal::collide(Player& p) const {
	EffectObject::collide(p);
	p.pos.y += offset;
}
