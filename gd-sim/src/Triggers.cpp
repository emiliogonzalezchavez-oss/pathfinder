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

int Level::triggerActivation(int index, int depth) const {
	auto const& t = triggers[index];
	if (depth > 16)
		return INT_MAX;
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
	auto it = movesByGroup.find(group);
	if (it == movesByGroup.end())
		return off;
	for (int i : it->second) {
		auto const& t = triggers[i];
		int a = triggerActivation(i);
		if (a == INT_MAX || a >= frame)
			continue;
		int elapsed = frame - a;
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
	set.offset = {0, 0};
	for (int g : set.moveGroups) {
		auto o = groupOffset(g, frame);
		set.offset.x += o.x;
		set.offset.y += o.y;
	}
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
			if (a == INT_MAX || a >= frame)
				continue;
			double progress = t.duration <= 0 ? 1 : std::min(1.0, (double)(frame - a) / t.duration);
			turns.push_back({i, t.degrees * ease(t.easing, progress, t.rate)});
		}
	}
	std::stable_sort(turns.begin(), turns.end(), [&](auto& a, auto& b) { return triggerActivation(a.first) < triggerActivation(b.first); });

	for (int i : set.objects) {
		auto& o = dynamicObjects[i];
		double x = dynamicBase[i].x + set.offset.x;
		double y = dynamicBase[i].y + set.offset.y;
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
						auto off = groupOffset(g, frame);
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
		o->ySection = std::max(0, (int)((double)0.01f * (double)(o->pos.y + 90)));
	}
}

void TouchTrigger::collide(Player& player) const {
	EffectObject::collide(player);
	player.touchEvents.push_back({player.frame, trigger});
}
