#include <Level.hpp>
#include <TouchTrigger.hpp>
#include <Player.hpp>
#include <cmath>
#include <algorithm>

// cocos2d's CCEase* actions, as GD's move trigger easings (EasingType 1-18); `rate` is the trigger's easing rate
static double bounceTime(double t) {
	if (t < 1 / 2.75) return 7.5625 * t * t;
	if (t < 2 / 2.75) { t -= 1.5 / 2.75; return 7.5625 * t * t + 0.75; }
	if (t < 2.5 / 2.75) { t -= 2.25 / 2.75; return 7.5625 * t * t + 0.9375; }
	t -= 2.625 / 2.75;
	return 7.5625 * t * t + 0.984375;
}

static double ease(int type, double t, double rate) {
	constexpr double pi = 3.14159265358979323846;
	// The elastic easings take the trigger's easing rate as their period (not cocos' default 0.3): verified in GD,
	// Grief's corridor (elastic out, rate 2) puts its slope spike where GD's potential slope is, frame for frame
	double period = rate > 0 ? rate : 0.3;
	switch (type) {
		case 1: t *= 2; return t < 1 ? 0.5 * std::pow(t, rate) : 1 - 0.5 * std::pow(2 - t, rate);
		case 2: return std::pow(t, rate);
		case 3: return std::pow(t, 1 / rate);
		case 4: {
			if (t == 0 || t == 1) return t;
			t = t * 2;
			double s = period / 4;
			t -= 1;
			if (t < 0) return -0.5 * std::pow(2, 10 * t) * std::sin((t - s) * pi * 2 / period);
			return std::pow(2, -10 * t) * std::sin((t - s) * pi * 2 / period) * 0.5 + 1;
		}
		case 5: {
			if (t == 0 || t == 1) return t;
			double s = period / 4;
			t -= 1;
			return -std::pow(2, 10 * t) * std::sin((t - s) * pi * 2 / period);
		}
		case 6: {
			if (t == 0 || t == 1) return t;
			double s = period / 4;
			return std::pow(2, -10 * t) * std::sin((t - s) * pi * 2 / period) + 1;
		}
		case 7: return t < 0.5 ? (1 - bounceTime(1 - t * 2)) * 0.5 : bounceTime(t * 2 - 1) * 0.5 + 0.5;
		case 8: return 1 - bounceTime(1 - t);
		case 9: return bounceTime(t);
		case 10: {
			if (t == 0 || t == 1) return t;
			t *= 2;
			if (t < 1) return 0.5 * std::pow(2, 10 * (t - 1));
			return 0.5 * (-std::pow(2, -10 * (t - 1)) + 2);
		}
		case 11: return t == 0 ? 0 : std::pow(2, 10 * (t - 1)) - 0.001;
		case 12: return t == 1 ? 1 : -std::pow(2, -10 * t) + 1;
		case 13: return -0.5 * (std::cos(pi * t) - 1);
		case 14: return -std::cos(t * pi / 2) + 1;
		case 15: return std::sin(t * pi / 2);
		case 16: {
			double overshoot = 1.70158 * 1.525;
			t *= 2;
			if (t < 1) return (t * t * ((overshoot + 1) * t - overshoot)) / 2;
			t -= 2;
			return (t * t * ((overshoot + 1) * t + overshoot)) / 2 + 1;
		}
		case 17: { double o = 1.70158; return t * t * ((o + 1) * t - o); }
		case 18: { double o = 1.70158; t -= 1; return t * t * ((o + 1) * t + o) + 1; }
		default: return t;
	}
}

double Level::xOfFrame(int frame) const {
	if (frame >= stepFrame)
		return stepX;
	return getState(frame).pos.x;
}

int Level::itemValue(int item, int frame) const {
	auto it = pickupsByItem.find(item);
	if (it == pickupsByItem.end())
		return 0;
	std::vector<std::pair<int, int>> fired;
	for (int i : it->second) {
		int a = triggerActivation(i);
		if (a != INT_MAX && a < frame)
			fired.push_back({a, i});
	}
	std::stable_sort(fired.begin(), fired.end());
	int v = 0;
	for (auto& [a, i] : fired)
		v = triggers[i].overrideCount ? triggers[i].count : v + triggers[i].count;
	return v;
}

int Level::triggerActivation(int index, int depth) const {
	auto const& t = triggers[index];
	if (depth > 16)
		return INT_MAX;
	if (t.kind == 7) {
		// Count trigger: the first frame its item reaches the count (a pickup changed it to that)
		auto it = pickupsByItem.find(t.item);
		if (it == pickupsByItem.end())
			return INT_MAX;
		std::vector<int> frames;
		for (int i : it->second) {
			int a = triggerActivation(i, depth + 1);
			if (a != INT_MAX)
				frames.push_back(a);
		}
		std::sort(frames.begin(), frames.end());
		for (int f : frames)
			if (itemValue(t.item, f + 1) == t.count)
				return f;
		return INT_MAX;
	}
	if (t.kind == 8) {
		// Instant count trigger: when it fires, only if its item compares (0 equal, 1 larger, 2 smaller) with the count
		const_cast<Level*>(this)->triggers[index].kind = 0;
		int a = triggerActivation(index, depth + 1);
		const_cast<Level*>(this)->triggers[index].kind = 8;
		if (a == INT_MAX)
			return INT_MAX;
		int v = itemValue(t.item, a + 1);
		bool ok = t.compare == 1 ? v > t.count : t.compare == 2 ? v < t.count : v == t.count;
		return ok ? a : INT_MAX;
	}
	int best = INT_MAX;
	if (t.touched) {
		if (stepTouches)
			for (auto& [f, i] : *stepTouches)
				if (i == index)
					return f;
		return INT_MAX;
	}
	if (!t.spawned) {
		// The first frame of the current path whose x reached the trigger's (player 1 only moves right). From the
		// path itself (gameStates), not a cache: a speed portal one path takes and another misses changes when.
		if (stepX >= t.x) {
			int lo = baseFrame + 1, hi = stepFrame;
			while (lo < hi) {
				int mid = (lo + hi) / 2;
				if (xOfFrame(mid) >= t.x)
					hi = mid;
				else
					lo = mid + 1;
			}
			best = lo;
		}
	} else {
		for (int g : t.groups) {
			auto sp = spawnersByGroup.find(g);
			if (sp == spawnersByGroup.end())
				continue;
			for (int s : sp->second) {
				int a = triggerActivation(s, depth + 1);
				if (a == INT_MAX)
					continue;
				best = std::min(best, a + (int)std::lround(triggers[s].delay * 240));
			}
		}
	}
	return best;
}



int Level::stopFrame(int index, int a) const {
	// A stop trigger (or pause, taken as a stop) whose target group holds this trigger, fired at or after it started
	int best = INT_MAX;
	for (int g : triggers[index].groups) {
		auto it = stopsByGroup.find(g);
		if (it == stopsByGroup.end())
			continue;
		for (int s : it->second) {
			int f = triggerActivation(s);
			if (f != INT_MAX && f >= a)
				best = std::min(best, f);
		}
	}
	return best;
}

Vec2D Level::groupOffset(int group, int frame, int depth) const {
	Vec2D off {0, 0};
	if (depth > 8)
		return off;
	// Follow triggers: the movement the followed group made while the trigger ran
	if (auto f = followsByGroup.find(group); f != followsByGroup.end()) {
		for (int i : f->second) {
			auto const& t = triggers[i];
			int a = triggerActivation(i);
			if (a == INT_MAX || a >= frame || !t.centerGroup)
				continue;
			int end = t.duration > 0 ? std::min(frame, a + t.duration) : frame;
			auto now = groupOffset(t.centerGroup, end, depth + 1);
			auto start = groupOffset(t.centerGroup, a, depth + 1);
			off.x += (now.x - start.x) * t.modX;
			off.y += (now.y - start.y) * t.modY;
		}
	}
	// Follow player Y: where player 1's path has pulled the group by then
	if (auto fy = followYByGroup.find(group); fy != followYByGroup.end() && frame > baseFrame && frame <= currentFrame()) {
		auto const& st = getState(frame);
		for (int i : fy->second) {
			int slot = triggers[i].fyIndex;
			if (slot >= 0 && slot < (int)st.followY.size())
				off.y += st.followY[slot];
		}
	}
	auto it = movesByGroup.find(group);
	if (it == movesByGroup.end())
		return off;
	for (int i : it->second) {
		auto const& t = triggers[i];
		int a = triggerActivation(i);
		if (a == INT_MAX || a >= frame)
			continue;
		int elapsed = std::min(frame, stopFrame(i, a)) - a;
		double progress = t.duration <= 0 ? 1 : std::min(1.0, (double)elapsed / t.duration);
		double e = ease(t.easing, progress, t.rate);
		if (t.lockX)
			off.x += (xOfFrame(std::min(frame - 1, a + t.duration)) - xOfFrame(a)) * t.modX;
		else
			off.x += t.dx * e;
		off.y += t.dy * e;
	}
	return off;
}

bool Level::groupEnabled(int group, int frame) const {
	int last = -1;
	bool on = true;
	// Toggle orbs this path clicked
	if (stepToggles)
		for (auto& [f, g] : *stepToggles)
			if (std::abs(g) == group && f < frame && f >= last) {
				last = f;
				on = g > 0;
			}
	auto it = togglesByGroup.find(group);
	if (it == togglesByGroup.end())
		return on;
	for (int i : it->second) {
		int a = triggerActivation(i);
		if (a == INT_MAX || a >= frame)
			continue;
		if (a >= last) {
			last = a;
			on = triggers[i].on;
		}
	}
	return on;
}

void Level::updateDynamicSet(DynamicSet& set, int frame) {
	size_t toggles = (stepToggles ? stepToggles->size() : 0) + 1000 * (stepTouches ? stepTouches->size() : 0);
	if (set.cacheFrame == frame && set.cacheX == stepX && set.cacheToggles == toggles)
		return;
	set.cacheFrame = frame;
	set.cacheX = stepX;
	set.cacheToggles = toggles;
	// GD moves objects (its move / rotate actions) after the player's collisions in a step: the collisions of the
	// step for frame f see the objects where the actions left them at the end of the step before. Verified in GD
	// (Unity: a block rising under the cube is where the sim has it a step earlier).
	int moved = frame - 1;
	set.offset = {0, 0};
	double prevY = 0;
	for (int g : set.moveGroups) {
		auto o = groupOffset(g, moved);
		set.offset.x += o.x;
		set.offset.y += o.y;
		prevY += groupOffset(g, moved - 1).y;
	}
	float moveDy = (float)(set.offset.y - prevY);
	set.enabled = true;
	for (int g : set.toggleGroups)
		if (!groupEnabled(g, frame)) {
			set.enabled = false;
			break;
		}
	if (!set.enabled)
		return;

	// Rotate triggers active on this set: (trigger, angle so far), in activation order
	std::vector<std::pair<int, double>> turns;
	for (int g : set.rotateGroups) {
		auto it = rotatesByGroup.find(g);
		if (it == rotatesByGroup.end())
			continue;
		for (int i : it->second) {
			auto const& t = triggers[i];
			int a = triggerActivation(i);
			if (a == INT_MAX || a >= moved)
				continue;
			double progress = t.duration <= 0 ? 1 : std::min(1.0, (double)(std::min(moved, stopFrame(i, a)) - a) / t.duration);
			turns.push_back({i, t.degrees * ease(t.easing, progress, t.rate)});
		}
	}
	std::stable_sort(turns.begin(), turns.end(), [&](auto& a, auto& b) { return triggerActivation(a.first) < triggerActivation(b.first); });

	// Scale triggers on this set: (factor x, factor y, centre group) so far
	std::vector<std::tuple<double, double, int>> scales;
	for (int g : set.rotateGroups) {
		auto it = scalesByGroup.find(g);
		if (it == scalesByGroup.end())
			continue;
		for (int i : it->second) {
			auto const& t = triggers[i];
			int a = triggerActivation(i);
			if (a == INT_MAX || a >= moved)
				continue;
			double progress = t.duration <= 0 ? 1 : std::min(1.0, (double)(std::min(moved, stopFrame(i, a)) - a) / t.duration);
			double e = ease(t.easing, progress, t.rate);
			scales.push_back({1 + (t.scaleX - 1) * e, 1 + (t.scaleY - 1) * e, t.centerGroup});
		}
	}

	for (int i : set.objects) {
		auto& o = dynamicObjects[i];
		double x = dynamicBase[i].x + set.offset.x;
		double y = dynamicBase[i].y + set.offset.y;
		if (!scales.empty()) {
			Vec2D size = dynamicBaseSize[i];
			for (auto& [sx, sy, cg] : scales) {
				size.x *= (float)sx;
				size.y *= (float)sy;
				auto c = cg ? groupCenters.find(cg) : groupCenters.end();
				if (c != groupCenters.end()) {
					double cx = c->second.pos.x, cy = c->second.pos.y;
					x = cx + (x - cx) * sx;
					y = cy + (y - cy) * sy;
				}
			}
			o->size = size;
		}
		double rot = dynamicBaseRotation[i];
		for (auto& [ti, angle] : turns) {
			auto const& t = triggers[ti];
			// (a centre group that is the target group itself: each object turns in place)
			if (t.centerGroup && t.centerGroup != t.target) {
				auto c = groupCenters.find(t.centerGroup);
				if (c != groupCenters.end()) {
					// the centre object moves with its own groups
					double cx = c->second.pos.x, cy = c->second.pos.y;
					for (int g : c->second.groups) {
						auto off = groupOffset(g, moved);
						cx += off.x;
						cy += off.y;
					}
					// GD's degrees are clockwise
					double r = angle * 3.14159265358979323846 / 180;
					double dx = x - cx, dy = y - cy;
					x = cx + dx * std::cos(r) + dy * std::sin(r);
					y = cy - dx * std::sin(r) + dy * std::cos(r);
				}
			}
			// (the sim's rotation is counter-clockwise: GD's negated)
			if (!t.lockRotation)
				rot -= angle;
		}
		o->pos.x = (float)x;
		o->pos.y = (float)y;
		o->rotation = (float)rot;
		o->moveDy = moveDy;
		o->ySection = std::max(0, (int)((double)0.01f * (double)(o->pos.y + 90)));
	}
}

void TouchTrigger::collide(Player& player) const {
	EffectObject::collide(player);
	player.touchEvents.push_back({player.frame, trigger});
}
