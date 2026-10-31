#include <Hazard.hpp>
#include <sstream>
#include <iomanip>
#include <Level.hpp>
#include <Orb.hpp>
#include <TouchTrigger.hpp>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <unordered_set>
#include <tuple>

// Otherwise gameStates copies every Player (instead of moving) whenever it grows
static_assert(std::is_nothrow_move_constructible_v<Player>);

void Level::initLevelSettings(std::string const& lvlSettings, Player& player) {
	std::unordered_map<std::string, std::string> obj;

	std::stringstream ss2(lvlSettings);
	std::string k, v;
	while (std::getline(ss2, k, ',')) {
		std::getline(ss2, v, ',');
		obj[k] = v;
	}

	// Helper to make a default value for nonexistant keys
	auto get_or = [&obj](std::string const& key, std::string const& def) {
		if (auto it = obj.find(key); it != obj.end())
			return it->second.c_str();
		return def.c_str();
	};

	player.speed = atoi(get_or("kA4", "0"));

	// Robtop stores 1x speed as 0 and slow speed as 1. Very silly
	if (player.speed == 0)
		player.speed = 1;
	else if (player.speed == 1)
		player.speed = 0;

	if ((player.small = atoi(get_or("kA3", "0"))))
		player.size = player.size * 0.6;

	player.upsideDown = atoi(get_or("kA11", "0"));
	player.vehicle = Vehicle::from(static_cast<VehicleType>(atoi(get_or("kA2", "0"))));
	// A level that starts as a wave starts with the wave's hitbox (6x6 mini, 10x10 normal), not the cube's:
	// verified in GD, a mini wave start rests on the ground at y 6 (sim), not 18
	if (player.vehicle.type == VehicleType::Wave)
		player.size = player.small ? Vec2D(6, 6) : Vec2D(10, 10);
	// ...and moves (down) on its very first step, like any wave: GD isn't grounded at the start there
	if (player.vehicle.type == VehicleType::Wave) {
		player.grounded = false;
		player.velocity = -player_speeds[player.speed] * (player.small ? 2 : 1);
	}

	player.floor = 0;
	player.ceiling = player.vehicle.bounds;
	player.dual = atoi(get_or("kA8", "0"));
}

Level::Level(std::string const& lvlString) {
	// Split by ';' to parse level string
	std::stringstream ss(lvlString);
	std::string objstr;
	bool first = true;

	// First player state
	auto player = Player();
	std::vector<std::pair<ObjectContainer, std::vector<int>>> pending;
	std::vector<std::tuple<float, char const*, int>> unsupportedPending;

	while (std::getline(ss, objstr, ';')) {
		// First entry is level settings object
		if (first) {
			initLevelSettings(objstr, player);
			// (two-player mode from the level's own settings, not a start position's)
			std::stringstream settings(objstr);
			std::string k, v;
			while (std::getline(settings, k, ',') && std::getline(settings, v, ','))
				if (k == "kA10")
					twoPlayer = atoi(v.c_str());
			first = false;
			continue;
		}

		std::unordered_map<int, std::string> obj;

		std::stringstream ss2(objstr);
		std::string k, v;
		while (std::getline(ss2, k, ',')) {
			std::getline(ss2, v, ',');
			if (atoi(k.c_str()) > 0)
				obj[atoi(k.c_str())] = v;
		}

		// PlayLayer::createObjectsFromSetupFinished puts the end portal 340 past the last object (any object), and
		// checkForEnd completes the level once player 1 reaches its spawn point, 300 before that
		if (obj.count(2))
			length = std::max(length, (float)stod_def(obj[2], 0) + 40);

		// Start positions are an editor/practice tool: a normal-mode run (what a macro is played in) starts at the
		// beginning, so they are ignored unless PF_STARTPOS is set
		static bool const useStartPos = std::getenv("PF_STARTPOS") != nullptr;
		if (obj[1] == "31" && useStartPos) {
			initLevelSettings(objstr, player);
			player.pos.x = stod_def(obj[2], 0);
			player.pos.y = stod_def(obj[3], 0);
		}

		std::vector<int> groups;
		if (obj.count(57)) {
			std::stringstream gs(obj[57]);
			std::string g;
			while (std::getline(gs, g, '.'))
				if (int gi = atoi(g.c_str()))
					groups.push_back(gi);
		}

		int objId = atoi(obj[1].c_str());
		if (objId == 1594 && atoi(obj[51].c_str()))
			orbToggleGroups[atoi(obj[51].c_str())] = true;
		if (obj.count(2))
			for (int g : groups)
				if (!groupCenters.count(g))
					groupCenters[g] = {Vec2D((float)stod_def(obj[2], 0), ((float)stod_def(obj[3], 0) + 90.f) - 90.f), groups};
		{
			static std::unordered_map<int, char const*> const names = {
				
				

{1814, "follow player Y trigger"},
				{3016, "advanced follow trigger"}, {3006, "area move trigger"}, {3007, "area rotate trigger"},
				{3008, "area scale trigger"}, {2067, "scale trigger"}, {1932, "player control trigger"},
				{2066, "gravity trigger"}, {1935, "time warp trigger"}, {3022, "teleport trigger"},
				{1811, "instant count trigger"}, {1817, "pickup trigger"}, {1611, "count trigger"},
				{1595, "touch trigger"}, {1812, "on death trigger"}, {2899, "options trigger"},
			};
			if (auto it = names.find(objId); it != names.end() && obj.count(2)) {
				// (triggers only matter when they target a group with gameplay objects: checked below)
				int target = std::string(it->second).find("trigger") != std::string::npos ? atoi(obj[51].c_str()) : -1;
				unsupportedPending.push_back({(float)stod_def(obj[2], 0), it->second, target});
			}
		}
		if (objId == 901 || objId == 1049 || objId == 1268 || objId == 1346 || objId == 1347 || objId == 1616) {
			Trigger t;
			t.kind = objId == 901 ? 0 : objId == 1049 ? 1 : objId == 1268 ? 2 : objId == 1346 ? 3 : objId == 1347 ? 4 : 5;
			t.x = stod_def(obj[2], 0);
			t.spawned = obj[62] == "1";
			t.touched = obj[11] == "1" && !t.spawned;
			t.groups = groups;
			t.target = atoi(obj[51].c_str());
			if (t.kind == 0) {
				t.dx = stod_def(obj[28], 0);
				t.dy = stod_def(obj[29], 0);
				t.duration = (int)std::lround(stod_def(obj[10], 0) * 240);
				t.easing = atoi(obj[30].c_str());
				t.rate = obj.count(85) ? stod_def(obj[85], 2) : 2;
				t.lockX = obj[58] == "1";
				t.modX = obj.count(143) ? stod_def(obj[143], 1) : 1;
				// 2.2 "use target" moves (100) and player-Y locks are left out
				if (obj[100] == "1") t.dx = t.dy = 0;
			} else if (t.kind == 4) {
				// Follow trigger: the target group copies the follow group's movement (times modX / modY) for its duration
				t.centerGroup = atoi(obj[71].c_str());
				t.modX = obj.count(72) ? stod_def(obj[72], 1) : 1;
				t.modY = obj.count(73) ? stod_def(obj[73], 1) : 1;
				t.duration = (int)std::lround(stod_def(obj[10], 0) * 240);
			} else if (t.kind == 3) {
				t.degrees = stod_def(obj[68], 0) + 360.0 * atoi(obj[69].c_str());
				t.duration = (int)std::lround(stod_def(obj[10], 0) * 240);
				t.easing = atoi(obj[30].c_str());
				t.rate = obj.count(85) ? stod_def(obj[85], 2) : 2;
				t.centerGroup = atoi(obj[71].c_str());
				t.lockRotation = obj[70] == "1";
			} else if (t.kind == 1) {
				t.on = obj[56] == "1";
			} else {
				t.delay = stod_def(obj[63], 0);
			}
			int index = triggers.size();
			triggers.push_back(t);
			if (t.touched) {
				// The player touching it fires it: an object in the level (30 x 30, GD's trigger box)
				TouchTrigger tt({30, 30}, {{1, "0"}, {2, obj[2]}, {3, obj[3]}});
				tt.trigger = index;
				ObjectContainer c(tt);
				c->id = objectCount++;
				pending.push_back({c, groups});
			}
			if (t.target) {
				if (t.kind == 0) movesByGroup[t.target].push_back(index);
				else if (t.kind == 3) rotatesByGroup[t.target].push_back(index);
				else if (t.kind == 4) followsByGroup[t.target].push_back(index);
				else if (t.kind == 1) togglesByGroup[t.target].push_back(index);
				else if (t.kind == 5) { if (obj[580].empty() || obj[580] == "0" || obj[580] == "1") stopsByGroup[t.target].push_back(index); }
				else spawnersByGroup[t.target].push_back(index);
			}
			continue;
		}

		// "No touch" (121) and passable (134) objects never collide (GameObject::m_isNoTouch / m_isPassable)
		if (obj[121] == "1" || obj[134] == "1")
			continue;


		if (auto ob_o = Object::create(std::move(obj))) {
			auto ob = ob_o.value();

			// Unique ID
			ob->id = objectCount++;
			pending.push_back({ob, std::move(groups)});
		}
	}

	for (auto& [ob, groups] : pending) {
		std::vector<int> moved, toggled, rotated;
		for (int g : groups) {
			if (movesByGroup.count(g) || followsByGroup.count(g)) moved.push_back(g);
			if (togglesByGroup.count(g) || orbToggleGroups.count(g)) toggled.push_back(g);
			if (rotatesByGroup.count(g)) rotated.push_back(g);
		}
		if (moved.empty() && toggled.empty() && rotated.empty()) {
			// Sections are divided by x position in increments of 100 (GJBaseGameLayer::addToSection)
			size_t sectionPos = std::max(0, (int)((double)0.01f * ob->pos.x));
			if (sectionPos >= sections.size())
				sections.resize(sectionPos + 1);
			sections[sectionPos].push_back(ob);
			continue;
		}
		DynamicSet* set = nullptr;
		for (auto& s : dynamicSets)
			if (s.moveGroups == moved && s.toggleGroups == toggled && s.rotateGroups == rotated) { set = &s; break; }
		if (!set) {
			dynamicSets.push_back({});
			set = &dynamicSets.back();
			set->moveGroups = moved;
			set->toggleGroups = toggled;
			set->rotateGroups = rotated;
			if (!rotated.empty())
				set->reach += 600;
			for (int g : moved) {
				if (followsByGroup.count(g))
					set->reach += 900;
				for (int i : movesByGroup[g]) {
					auto& t = triggers[i];
					set->reach += (std::abs(t.dx) + std::abs(t.dy)) * 1.7 + 30;
					if (t.lockX) set->reach += (t.duration / 240.0 + 0.1) * 1200 * std::abs(t.modX);
				}
			}
		}
		set->objects.push_back(dynamicObjects.size());
		set->minX = std::min(set->minX, ob->pos.x);
		set->maxX = std::max(set->maxX, ob->pos.x);
		dynamicBase.push_back(ob->pos);
		dynamicBaseRotation.push_back(ob->rotation);
		dynamicObjects.push_back(ob);
	}

	{
		std::unordered_set<int> gameplayGroups;
		for (auto& [ob, groups] : pending)
			gameplayGroups.insert(groups.begin(), groups.end());
		for (auto& [x, name, target] : unsupportedPending)
			if (target < 0 || gameplayGroups.count(target))
				unsupported.push_back({x, name});
	}
	std::sort(unsupported.begin(), unsupported.end(), [](auto& a, auto& b) { return a.first < b.first; });

	player.level = this;
	Player p2 = player;
	p2.isP2 = true;
	if (player.dual) {
		// A level that starts in dual mode: player 2 as at a dual portal (DualPortal::collide), with the dual
		// ground centred on GD's y 0 (no portal), which puts its floor on the ground
		p2.flipGravity(!player.upsideDown);
		player.dualPortalY = p2.dualPortalY = -90;
		player.partner = &p2;
		p2.partner = &player;
		player.updateGround(0);
		player.partner = p2.partner = nullptr;
	}
	gameStates.push_back(player);
	gameStates2.push_back(p2);
	if (player.dual) {
		// ...which GD steps from the first step on: off the ground, with a step of gravity before its first move
		Player& q = gameStates2.back();
		q.grounded = false;
		q.startDual();
	}
}

Player& Level::runFrame(bool pressed, float dt) {
	Player p = gameStates.back();

	// Can't play if you're dead
	if (p.dead)
		return gameStates.back();

	// GJBaseGameLayer::update steps player 1 (update, collisions), then player 2 if dual mode was on before
	// player 1's step; both get the button. Each sees the other as `partner` (the dual ground, portals).
	Player q = gameStates2.back();
	bool wasDual = p.dual;
	p.partner = &q;
	q.partner = &p;
	// Both clicked a blue or green orb: GD's next step pushes player 1's (which turns player 2 the other way
	// round), then player 2's (from there), which turns player 1 against player 2's new gravity. So player 2 ends
	// up in player 1's new gravity with its orb's velocity, and player 1 flipped back.
	if (p.flipPartnerNext && q.flipPartnerNext) {
		p.flipPartnerNext = false;
		if (q.upsideDown != p.upsideDown && p.flipsPartnerKind()) {
			q.flipGravity(p.upsideDown);
			// (a cube's ring spin, ringJump's runNormalRotation, started in the gravity player 1's flip gave it: the
			// other way round. A ball's flipGravity restarts its spin anyway.)
			if (q.vehicle.type == VehicleType::Cube)
				q.rotationSpeed = -q.rotationSpeed;
		}
	}
	// Player 1's blue or green orb click flips player 2 before player 2's own click takes its orb: one that took
	// another orb with that click keeps that orb's velocity, in the new gravity
	if (p.flipPartnerNext && q.clickedOrb && q.button && !q.prevPlayer().button) {
		p.flipPartnerNext = false;
		if (p.flipsPartner()) {
			q.flipGravity(!p.upsideDown);
			// (its ring's spin, from there: the other way round for a cube)
			if (q.vehicle.type == VehicleType::Cube)
				q.rotationSpeed = -q.rotationSpeed;
		}
	}
	for (Player* pl : {&p, &q}) {
		if (pl->flipPartnerNext) {
			pl->flipPartnerNext = false;
			// (player 2's ball jump at the end of its last frame is GD's pushButton for it, after player 1's: player 1's
			// flip saw player 2 in its gravity from before that jump, which didn't flip it, or the jump wouldn't have
			// happened, see partnerFlipsNext)
			if (pl == &p && q.ballJumped)
				continue;
			pl->flipPartner(false);
		}
	}
	int collisions = stepPlayer(p, pressed, dt);
	if (wasDual && p.dual && !p.dead) {
		// (not once player 1 died: GD stops the step there)
		q.partnerFlipsNext = p.flipPartnerNext && p.flipsPartner();
		// A speed portal player 1 took this step gives player 2 the new speed from GD's next step on too
		// (updateTimeMod, also at the same speed): its collisions this step still see the old one
		if (p.tookSpeedPortal)
			q.pendingSpeed = p.speed;
		stepPlayer(q, twoPlayer ? q.button : pressed, dt);
		q.partnerFlipsNext = false;
		if (q.dead)
			p.dead = true;
		else
			repelBalls(p, q);
		// Player 2 left dual mode through a solo portal: player 1 took its place (DualPortal::collide) with
		// player 2's velocity as GD's collisions saw it, which GD's next step adds gravity to (like a new player 2)
		if (p.copiedFromP2) {
			p.copiedFromP2 = false;
			p.startDual();
		}
	} else if (p.dual) {
		// (with the speed GD's next step gives both players, updateTimeMod: a speed portal player 1 took this step)
		q.speed = p.speed;
		// GD turns player 2 in this step already (updateRotation runs for both players once dual mode is on): a
		// ball's spin from the dual portal's flipGravity
		q.updateRotation();
		q.startDual();
	}
	if (p.dual) {
		// A speed portal sets GD's speed for the next step, which updateTimeMod gives both players at its start
		// (updateTimeMod runs at the same speed too: it restarts a ball's roll; the player that took the portal had it
		// in its own step)
		int speed = wasDual && q.tookSpeedPortal ? q.speed : p.tookSpeedPortal ? p.speed : -1;
		for (Player* pl : {&p, &q}) {
			if (speed >= 0 && (pl->speed != speed || !pl->tookSpeedPortal)) {
				bool changed = pl->speed != speed;
				pl->speed = speed;
				// (player 1 after player 2's portal: the sim's velocity already has its next step's gravity, which
				// for a cube in the air is the new speed's m_gravity)
				// (a grounded one's next walk-off step falls with it too, see Player::preCollision)
				if (changed && pl->cubeLike()) {
					pl->acceleration = stepAcceleration(*pl);
					if (!pl->grounded && !pl->velocityOverride)
						pl->velocity = std::max(-810.0, roundVel(pl->collisionVelocity + pl->acceleration * pl->dt, pl->upsideDown));
				}
				if (pl->vehicle.type == VehicleType::Ball) {
					pl->stopRotation();
					// (a ball that jumped at the end of this frame jumps in GD's next updateJump, after updateTimeMod: its
					// flipGravity's spin, at the new speed)
					if (pl->ballJumped)
						pl->runBallRotation2();
					else
						pl->runBallRotation();
				}
			}
		}
	}
	// GJBaseGameLayer::update keeps where each player turned (m_shipRotation), player 2's only in dual mode
	p.shipRotation = p.pos;
	if (p.dual)
		q.shipRotation = q.pos;
	if (!p.dual) {
		// Player 2 idles; keep a copy of player 1 so its lane has states for every frame
		q.copyAttributes(p);
	}
	p.partner = q.partner = nullptr;
	p.partnerFlipped = q.partnerFlipped = false;

	if (debug) {
		std::cout << "Frame " << gameStates.size() << std::fixed << std::setprecision(8)
				  << " X " << p.pos.x << " Y " << p.pos.y - 15 << " Vel " << p.velocity
				  << " Accel " << p.acceleration << " Rot " << p.rotation << " Coll " << collisions
				  << " Mode " << p.modeString()
 				  << std::endl;
		if (p.dual)
			std::cout << "P2 " << gameStates.size() << " X " << q.pos.x << " Y " << q.pos.y - 15 << " Vel " << q.velocity
					  << " Rot " << q.rotation << " Mode " << q.modeString() << " floor " << p.floor << " ceiling " << p.ceiling
					  << std::endl;

		// `Flags <frame> ...`: internal state for debugging (set PF_FLAGS=1)
		static bool const flags = std::getenv("PF_FLAGS") != nullptr;
		if (flags) {
			for (Player const* pl : {&p, &q}) {
				if (pl == &q && !p.dual)
					break;
				std::cout << (pl == &p ? "Flags " : "Flags2 ") << gameStates.size() << " og " << pl->grounded
						  << " og2 " << pl->onGround2 << " rotating " << pl->isRotating << " spin " << pl->rotationSpeed
						  << " slope " << pl->slope.on << pl->slope.was << " boosted " << pl->boosted
						  << " acc " << pl->accelerating << " btn " << pl->jumpBuffered << " floor " << pl->floor << " ceiling "
						  << pl->ceiling << std::endl;
				// `Pot <frame> <player> <x> <y> slope <x> <y>`: the potential and current slopes (verifier: GD lines)
				auto at = [](Slope const* s) {
					return s ? std::to_string(s->pos.x) + " " + std::to_string(s->pos.y) : std::string("-1 -1");
				};
				std::cout << "Pot " << gameStates.size() << " " << (pl == &p ? 1 : 2) << " " << at(pl->slope.potential)
						  << " slope " << at(pl->slope.current) << std::endl;
			}
		}

		if (p.button != gameStates.back().button) {
			std::cout << "Input X " << p.pos.x << " Y " << p.pos.y - 15 << std::endl;
		}
	}

	gameStates.push_back(std::move(p));
	gameStates2.push_back(std::move(q));
	return gameStates.back();
}

void Level::repelBalls(Player& p, Player& q) const {
	// (a ball that jumped this frame is still on the ground in its old gravity in GD, see ballJumped)
	if (twoPlayer || p.vehicle.type != VehicleType::Ball || q.vehicle.type != VehicleType::Ball
		|| (p.upsideDown != p.ballJumped) != (q.upsideDown != q.ballJumped))
		return;
	// (the radius is half the ball's 30 at its scale)
	double reach = (p.small ? 0.6 : 1) * 15 + (q.small ? 0.6 : 1) * 15 + 5;
	if (!(std::abs(p.pos.y - q.pos.y) < reach))
		return;
	// The one whose partner is on the ground (m_isOnGround2) flips: player 2 if player 1 is down, else player 1
	Player* flip = p.onGround2 || p.ballJumped ? &q : q.onGround2 || q.ballJumped ? &p : nullptr;
	// (both jumping from a fresh press, which GD made in pushButton, before this step: no repel. A held one is GD's
	// next updateJump, after it)
	auto fresh = [](Player const& b) { return b.ballJumped && b.button && !b.prevPlayer().button; };
	if (!flip || (flip->ballJumped && (flip == &q ? p.ballJumped : q.ballJumped) && (fresh(p) || fresh(q))))
		return;
	// PlayerObject::flipGravity (no partner flip) and a y velocity of 2 towards its new floor, which GD's next
	// step adds gravity to (the sim's velocity has that step already). A ball the sim has jumped this frame is
	// still on the ground in GD: the repel flips it first (the same new gravity), so it never jumps and keeps
	// holding the button.
	if (flip->ballJumped) {
		flip->ballJumped = false;
		flip->input = flip->button;
		// (nor clears m_jumpBuffered: it jumps once it is on the ground again)
		flip->boosted = flip->beforeBallJump.boosted;
		flip->jumpBuffered = flip->beforeBallJump.jumpBuffered;
		flip->vehicleBuffer = flip->beforeBallJump.vehicleBuffer;
		flip->clickUsed = flip->beforeBallJump.clickUsed;
	} else
		flip->flipGravity(!flip->upsideDown);
	flip->velocity = roundVel(-2 * 54 + flip->acceleration * flip->dt, flip->upsideDown);
	flip->grounded = false;
	// (GD's next updateJump: a boosted ball now falling ends m_maybeIsBoosted, see Player::postCollision)
	if (flip->boosted && flip->fallingBugged()) {
		flip->boosted = false;
		flip->onGround2 = false;
	}
}

/// `SimDeath <frame> <player> at <x> <y> size <w> <h> rot <r>`: the object that killed the player (set PF_FLAGS=1)
static void logDeath(Player const& p, Object const& o) {
	static bool const on = std::getenv("PF_FLAGS") != nullptr;
	if (on)
		std::cout << "SimDeath " << p.frame - 1 << " " << (p.isP2 ? 2 : 1) << " at " << o.pos.x << " " << o.pos.y
				  << " size " << o.size.x << " " << o.size.y << " rot " << -o.rotation << std::endl;
}

int Level::stepPlayer(Player& p, bool pressed, float dt) {
	p.dt = dt;
	p.preCollision(pressed);

	// GJBaseGameLayer::checkCollisions: the 3 x 3 sections around the player (clamped to the level), column by
	// column (x * 0.01f: an object at x 14700 is in column 146); within a column the sim keeps level order
	int column = (int)((float)p.pos.x * 0.01f);
	int row = std::max(0, (int)((float)(p.pos.y + 90) * 0.01f));
	int firstColumn = std::max(0, column - 1), lastColumn = std::min((int)this->sections.size() - 1, column + 1);
	std::vector<std::vector<ObjectContainer>*> sections;
	for (int c = firstColumn; c <= lastColumn; ++c)
		sections.push_back(&this->sections[c]);

	// Blocks are hazards processed separately
	blocks.clear();
	hazards.clear();

	size_t numCollisions = 0;

	// `Trace <frame> <player> id <unique id> at <x> <y> -> y <Y> vel <v>` after every object collision (set PF_TRACE=1)
	static bool const tracing = std::getenv("PF_TRACE") != nullptr;
	auto trace = [&](Object const& o) {
		if (tracing)
			std::cout << "Trace " << p.frame - 1 << " " << (p.isP2 ? 2 : 1) << " id " << o.id << " at " << o.pos.x << " "
					  << o.pos.y << " -> y " << p.pos.y - 15 << " vel " << p.velocity << " g " << p.grounded << std::endl;
	};

	// A click is handled by PlayerObject::pushButton, after this step's collisions, which tries the rings in
	// m_touchingRings (touched this step or the last) in the order they were first touched: a ring stays
	// in it while the player keeps touching it (resetTouchedRings).
	bool clicked = p.button && !p.prevPlayer().button;
	std::vector<Orb const*> clickOrbs;

	// Moved and toggled groups: placed for this frame (player 1's x so far decides which triggers fired)
	if (!p.isP2) {
		stepFrame = p.frame;
		stepX = p.pos.x;
		stepToggles = &p.toggleEvents;
		stepTouches = &p.touchEvents;
	}
	std::vector<ObjectContainer*> dynamicNear;
	for (auto& set : dynamicSets) {
		if (set.maxX + set.reach < p.pos.x - 250 || set.minX - set.reach > p.pos.x + 250)
			continue;
		updateDynamicSet(set, p.frame);
		if (!set.enabled)
			continue;
		for (int i : set.objects) {
			auto& o = dynamicObjects[i];
			int c = (int)((float)o->pos.x * 0.01f);
			if (c < firstColumn || c > lastColumn)
				continue;
			dynamicNear.push_back(&o);
		}
	}

	// D blocks first: whether the wave may slide this frame
	p.dSlide = false;
	auto checkD = [&](ObjectContainer const& o) {
		if (o->prio == 3 && !p.dSlide && o->ySection >= row - 1 && o->ySection <= row + 1 && o->Object::touching(p))
			p.dSlide = true;
	};
	for (auto section : sections)
		if (section)
			for (auto& o : *section)
				checkD(o);
	for (auto o : dynamicNear)
		checkD(*o);

	auto visit = [&](ObjectContainer& o) {
			if (p.dead) return;
			if (o->prio == 3)
				return;
			if (o->ySection < row - 1 || o->ySection > row + 1)
				return;
			if (o->prio == 1)
				blocks.push_back(&o);
			else if (o->prio == 2)
				hazards.push_back(&o);
			else if (o->touching(p)) {
				++numCollisions;
				if (auto orb = clicked ? dynamic_cast<Orb const*>(o.operator->()) : nullptr)
					clickOrbs.push_back(orb);
				else
					o->collide(p);
				trace(*o.operator->());
				if (p.dead)
					logDeath(p, *o.operator->());
			}
	};
	for (auto section : sections) {
		if (section == nullptr) continue;
		for (auto& o : *section) {
			if (p.dead) break;
			visit(o);
		}
	}
	for (auto o : dynamicNear) {
		if (p.dead) break;
		visit(*o);
	}

	if (clickOrbs.size() > 1) {
		auto firstTouched = [&](Orb const* orb) {
			int f = orb->Object::touching(p) ? p.frame : p.frame - 1;
			while (f - 1 > baseFrame && orb->Object::touching(getState(f - 1, p.isP2)))
				--f;
			return f;
		};
		std::vector<std::pair<int, Orb const*>> order;
		for (auto orb : clickOrbs)
			order.push_back({firstTouched(orb), orb});
		std::stable_sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first < b.first; });
		for (size_t i = 0; i < order.size(); ++i)
			clickOrbs[i] = order[i].second;
	}
	for (auto orb : clickOrbs) {
		if (p.dead) break;
		if (orb->touching(p))
			orb->collide(p);
		trace(*orb);
		if (p.dead)
			logDeath(p, *orb);
	}

	if (p.scaleRefreshed)
		p.applyResize();

	// Blocks are processed in descending order. GD's loop doesn't stop when a block kills the player: the
	// remaining blocks still push it (the position GD shows at the death)
	bool deadBefore = p.dead;
	for (int i = blocks.size() - 1; i >= 0 && !deadBefore; --i) {
		auto& b = *blocks[i];
		if (b->touching(p)) {
			bool wasDead = p.dead;
			++numCollisions;
			b->collide(p);
			trace(*b.operator->());
			if (p.dead && !wasDead)
				logDeath(p, *b.operator->());
		}
	}

	for (auto hp : hazards) {
		auto& h = *hp;
		if (p.dead) break;
		if (h->touching(p)) {
			++numCollisions;
			h->collide(p);
			trace(*h.operator->());
			if (p.dead)
				logDeath(p, *h.operator->());
		}
	}

	// The search's safety margin: a near miss counts as a death (hazards for every mode, blocks for the wave)
	if (margin > 0 && !p.dead) {
		auto grown = [&](ObjectContainer const& o) {
			ObjectContainer c = o;
			c->size.x += o->size.x < 0 ? -2 * margin : 2 * margin;
			c->size.y += o->size.y < 0 ? -2 * margin : 2 * margin;
			return c;
		};
		for (auto hp : hazards) {
			auto c = grown(*hp);
			if (c->touching(p)) {
				p.dead = true;
				break;
			}
		}
		if (!p.dead && p.vehicle.type == VehicleType::Wave && !p.dSlide) {
			for (auto bp : blocks) {
				auto c = grown(*bp);
				if (c->Object::touching(p)) {
					p.dead = true;
					break;
				}
			}
		}
	}

	if (!p.dead)
		p.postCollision();
	else
		p.vehicle.clamp(p); // GD keeps the player within the mode's bounds before object collisions kill it

	return numCollisions;
}


void Level::rollback(int frame) {
	gameStates.resize(std::max(frame - baseFrame, 1));
	gameStates2.resize(gameStates.size());
}

void Level::resume(Player state) {
	state.level = this;
	baseFrame = state.frame - 1;
	gameStates.clear();
	gameStates2.clear();
	gameStates.push_back(state);
	state.isP2 = true;
	gameStates2.push_back(std::move(state));
}

int Level::currentFrame() const {
	return gameStates.size() + baseFrame;
}

Player const& Level::getState(int frame, bool p2) const {
	auto const& states = p2 ? gameStates2 : gameStates;
	int index = frame - 1 - baseFrame;
	if (index <= 0)
		return states[0];
	if (index >= (int)states.size())
		return states.back();
	return states[index];
}

Player& Level::latestState() {
	return gameStates.back();
}

char const* Level::unsupportedNear(float x0, float x1) const {
	auto it = std::lower_bound(unsupported.begin(), unsupported.end(), x0, [](auto& a, float x) { return a.first < x; });
	if (it != unsupported.end() && it->first <= x1)
		return it->second;
	return nullptr;
}

std::pair<float, char const*> const* Level::nextUnsupported(float x) const {
	auto it = std::upper_bound(unsupported.begin(), unsupported.end(), x, [](float v, auto& a) { return v < a.first; });
	return it == unsupported.end() ? nullptr : &*it;
}

void Level::spiderJump(Player& p, bool wide) {
	// PlayerObject::spiderTestJumpInternal (GD 2.2081), not sideways, not platformer, moving right. The player goes to
	// the nearest solid (block, slope, breakable block) on its head side whose underside (top, upside down) it can
	// stand against, a hazard in the 8-wide column above it if that is nearer, or else the mode's bound.
	bool up = !p.upsideDown;
	float w = p.size.x, h = p.size.y;
	float px = p.pos.x, py = p.pos.y;
	float left = px - w / 2, bottom = py - h / 2, top = py + h / 2;

	// The bounds: a ship, ufo, wave, ball or spider uses the mode's floor and ceiling (getMinPortalY /
	// getMaxPortalY: the ground the mode shows, else the screen, at least GD's ground); any other mode 3000 around it
	float lo, hi;
	bool modeBounds = p.vehicle.type != VehicleType::Cube && p.vehicle.type != VehicleType::Robot;
	if (!modeBounds) {
		lo = std::max(0.f, py - 3000);
		hi = py + 3000;
	} else if (p.ceiling < 1e6f && (p.dual || p.vehicle.type == VehicleType::Ship || p.vehicle.type == VehicleType::Ufo
		|| p.vehicle.type == VehicleType::Wave || p.vehicle.type == VehicleType::Ball || p.vehicle.type == VehicleType::Swing)) {
		lo = p.floor;
		hi = p.ceiling;
	} else {
		// (no ground shown: the screen, 320 high, which the sim doesn't track; centred on the player)
		lo = std::max(0.f, py - 160);
		hi = lo + 320;
	}

	// Search rects: from 2 into the player on its head side to the bound
	float y0 = up ? top - 2 : lo, y1 = up ? hi : bottom + 2;
	struct R { float x0, y0, x1, y1; };
	auto meets = [](R const& a, float ax0, float ay0, float ax1, float ay1) {
		return !(a.x1 < ax0 || ax1 < a.x0 || a.y1 < ay0 || ay1 < a.y0);
	};
	R search = wide ? R {left, y0, left + w + 4, y1} : R {px, y0, px + w / 2 + 1, y1};
	float col = (p.small ? 0.6f : 1.f) * 8;
	R column {px - col / 2, y0, px + col / 2, y1};

	std::vector<Object const*> statics, damaging;
	auto consider = [&](Object const* o) {
		float ox0 = o->getLeft(), ox1 = o->getRight(), oy0 = o->getBottom(), oy1 = o->getTop();
		if (o->prio == 1) {
			if (meets(search, ox0, oy0, ox1, oy1))
				statics.push_back(o);
		} else if (dynamic_cast<Hazard const*>(o)) {
			if (meets(column, ox0, oy0, ox1, oy1))
				damaging.push_back(o);
		}
	};
	int c0 = std::max(0, (int)((search.x0 - 60) * 0.01f)), c1 = std::min((int)sections.size() - 1, (int)((search.x1 + 60) * 0.01f));
	for (int c = c0; c <= c1; ++c)
		for (auto& o : sections[c])
			consider(o.operator->());
	for (auto& set : dynamicSets) {
		if (set.maxX + set.reach < search.x0 - 100 || set.minX - set.reach > search.x1 + 100)
			continue;
		updateDynamicSet(set, p.frame);
		if (!set.enabled)
			continue;
		for (int i : set.objects)
			consider(dynamicObjects[i].operator->());
	}
	// (the comparators: int(minY * 10) ascending, upside down int(maxY * 10) descending)
	auto order = [&](Object const* a, Object const* b) {
		return up ? (int)(a->getBottom() * 10) < (int)(b->getBottom() * 10) : (int)(a->getTop() * 10) > (int)(b->getTop() * 10);
	};
	std::stable_sort(statics.begin(), statics.end(), order);
	std::stable_sort(damaging.begin(), damaging.end(), order);

	if (statics.empty() && !damaging.empty() && !wide)
		return spiderJump(p, true);

	float target = 0;
	bool fromHazard = false;
	if (statics.empty()) {
		if (!damaging.empty()) {
			target = damaging.front()->pos.y;
			fromHazard = true;
		}
	} else {
		for (size_t i = 0; i < statics.size(); ++i) {
			Object const* o = statics[i];
			float cand = up ? o->getBottom() - h / 2 : o->getTop() + h / 2;
			// (one on the other side of the player's centre, more than 10 away, doesn't count)
			if (up ? cand < py - 10 : py + 10 < cand)
				continue;
			// An object that starts right of the player's centre only counts if the player fits there: no other
			// of them (shrunk by 2 at the top and bottom) overlaps it
			if (px < o->getLeft()) {
				float ry0 = cand - h / 2, ry1 = cand + h / 2;
				bool blocked = false;
				for (size_t j = 0; j < statics.size() && !blocked; ++j) {
					if (j == i)
						continue;
					Object const* q = statics[j];
					R shrunk {q->getLeft(), q->getBottom() + 2, q->getRight(), q->getTop() - 2};
					blocked = meets(shrunk, left, ry0, left + w, ry1);
				}
				if (blocked)
					continue;
			}
			target = cand;
			break;
		}
	}
	if (target == 0)
		target = up ? hi - h / 2 : lo + h / 2;
	if (!fromHazard && !damaging.empty()) {
		float d = damaging.front()->pos.y;
		if (up ? target > d : target < d) {
			if (!wide)
				return spiderJump(p, true);
			target = d;
		}
	}

	p.pos.y = ((float)(target + 90.f)) - 90.f;
	p.flipGravity(up);
	// setYVelocity(+-1): towards the new floor
	p.velocity = -54;
	p.grounded = false;
	p.onGround2 = false;
	p.input = false;
}
