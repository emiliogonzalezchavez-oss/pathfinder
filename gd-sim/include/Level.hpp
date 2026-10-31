#pragma once
#include <Object.hpp>
#include <Player.hpp>
#include <vector>
#include <unordered_map>
#include <climits>


/**
 * In order to use the simulator, you must create a Level. The Level class is
 * the root class of everything else, containing both objects and player states,
 * as well as the main update function. See Level.cpp for implementation info.
 */
class Level {
	/// Called by constructor, applies level settings to the initial player state
	void initLevelSettings(std::string const& lvlSettings, Player& player);
 public:
 	/**
 	 * All player states are stored, including previous states. This way, Pathfinder
 	 * is able to seamlessly rewind when searching for solutions.
 	 */
	std::vector<Player> gameStates;
	/// Player 2's states, one per gameStates entry (a copy of player 1 while not in dual mode)
	std::vector<Player> gameStates2;

	/// Frames before gameStates[0] (which is frame baseFrame + 1). Non-zero after `resume`.
	int baseFrame = 0;

	size_t objectCount = 0;

	/// Sections are used just like real GD. See Object.hpp for more info on ObjectContainer.
	std::vector<std::vector<ObjectContainer>> sections;

	/// x at which the level is complete: the last object's x + 40 (see Level::Level)
	float length = 0.0;

	/// Two-player mode (level setting kA10, LevelSettingsObject 0x154): in dual mode each player gets only its own
	/// inputs (GJBaseGameLayer::handleButton), which the sim's single button is player 1's, and a gravity flip
	/// doesn't flip the other player (GJBaseGameLayer::flipGravity)
	bool twoPlayer = false;

	/// Per-frame scratch lists for runFrame, kept here so they don't reallocate every frame
	std::vector<ObjectContainer const*> blocks;
	std::vector<ObjectContainer const*> hazards;
	std::vector<ObjectContainer> dynamicScratch;

	/// Triggers that change gameplay: move (901), toggle (1049) and spawn (1268). They fire when player 1's x
	/// passes theirs (or a spawn trigger's delay after it fired), which doesn't depend on the inputs, so the
	/// sim records player 1's x per frame and works out every group's offset and on/off state from the frame alone.
	struct Trigger {
		int kind = 0; // 0 move, 1 toggle, 2 spawn, 3 rotate, 4 follow, 5 stop, 6 pickup, 7 count, 8 instant count
		double x = 0;
		bool spawned = false;
		bool touched = false; // touch triggered: fires when the player touches it, not when passing its x
		std::vector<int> groups; // the trigger's own groups (what spawn triggers target)
		int target = 0;
		double dx = 0, dy = 0;
		int duration = 0; // frames
		int easing = 0;
		float rate = 2;
		bool lockX = false;
		double modX = 1;
		bool on = true;  // toggle: activate group
		double delay = 0; // spawn: seconds
		double degrees = 0; // rotate: total clockwise degrees (incl. full turns)
		int centerGroup = 0; // rotate: group of the object to rotate around (0: each object around itself)
		bool lockRotation = false; // rotate: objects keep their own rotation
		double modY = 1; // follow: y multiplier (modX is the x one)
		int item = 0, count = 0, compare = 0; // pickup / count / instant count: item ID (80), count (77), comparison (88)
		bool overrideCount = false; // pickup: set the item instead of adding (139)
		mutable int activation = INT_MAX;
	};
	std::vector<Trigger> triggers;
	/// Gameplay objects the sim doesn't model (x, name), sorted by x: where the search is likely to get stuck
	std::vector<std::pair<float, char const*>> unsupported; // (names are string literals: they outlive the Level)
	/// The first unsupported object between x0 and x1, or nullptr
	char const* unsupportedNear(float x0, float x1) const;
	/// The first unsupported object after x, or nullptr
	std::pair<float, char const*> const* nextUnsupported(float x) const;
	std::unordered_map<int, std::vector<int>> movesByGroup, togglesByGroup, spawnersByGroup, rotatesByGroup, followsByGroup;
	/// Stop triggers (1616) by the group of triggers they stop
	std::unordered_map<int, std::vector<int>> stopsByGroup;
	/// Pickup triggers by item ID
	std::unordered_map<int, std::vector<int>> pickupsByItem;
	/// An item's value at the start of a frame (pickup triggers fired before it)
	int itemValue(int item, int frame) const;
	/// The first frame a stop trigger halted trigger `index` (activated at `a`), INT_MAX if none
	int stopFrame(int index, int a) const;
	/// The first object of each group (any object, decoration too): where rotate triggers turn around
	struct GroupCenter { Vec2D pos; std::vector<int> groups; };
	std::unordered_map<int, GroupCenter> groupCenters;
	/// Player 1's x at each frame (index = frame), filled as frames are simulated
	std::vector<double> xAtFrame; // (unused: positions come from gameStates, the current path)
	/// The frame being stepped and player 1's x in it (not in gameStates yet)
	int stepFrame = 0;
	double stepX = 0;
	/// Player 1's x at a frame of the current path
	double xOfFrame(int frame) const;

	/// Objects in a moved or toggled group: kept out of `sections` and placed every frame
	struct DynamicSet {
		std::vector<int> moveGroups, toggleGroups, rotateGroups;
		std::vector<int> objects; // indices into dynamicObjects
		float minX = 1e30f, maxX = -1e30f;
		double reach = 0;
		int cacheFrame = -1;
		double cacheX = -1;
		size_t cacheToggles = 0; // (the path's x then: another path can be elsewhere at the same frame)
		Vec2D offset {0, 0};
		bool enabled = true;
	};
	std::vector<ObjectContainer> dynamicObjects;
	std::vector<Vec2D> dynamicBase;
	std::vector<float> dynamicBaseRotation;
	std::vector<DynamicSet> dynamicSets;

	int triggerActivation(int index, int depth = 0) const;
	Vec2D groupOffset(int group, int frame, int depth = 0) const;
	bool groupEnabled(int group, int frame) const;
	/// Groups toggle orbs target, and the toggle orbs the path being stepped has clicked
	std::unordered_map<int, bool> orbToggleGroups;
	std::vector<std::pair<int, int>> const* stepToggles = nullptr;
	std::vector<std::pair<int, int>> const* stepTouches = nullptr;
	void updateDynamicSet(DynamicSet& set, int frame);

	/// PlayerObject::spiderTestJumpInternal: teleports the player to the nearest surface on its head side (or the
	/// mode bound) and flips its gravity
	void spiderJump(Player& p, bool wide = false);

 	static constexpr uint32_t sectionSize = 100;
 	/// GD's window width in points (320 high, so 569 is 16:9). Mirror portal sprites move relative to it.
 	float screenWidth = 569;
 	bool debug = false;
	/// Safety margin (units) the search keeps from hazards, and a wave from blocks: GD's own floats and timing can
	/// differ from the sim's by a hair, so a path that only just scrapes past something can die in the real game.
	/// 0 = exact hitboxes (what the final check uses).
	float margin = 0;

 	Level(std::string const& lvlString);

 	/// The main update function. Every frame is associated with a press/release state.
 	Player& runFrame(bool pressed, float dt = 1/240.);
 	/// One player's part of a frame (move, collisions); returns how many objects it touched
 	int stepPlayer(Player& p, bool pressed, float dt);
 	/// GJBaseGameLayer::checkRepellPlayer, after both players' steps in dual mode: two balls of the same gravity
 	/// closer in y than their radii plus 5 push apart, the one in the air flipping away from the grounded one
 	void repelBalls(Player& p, Player& q) const;

 	/// Go back to a certain frame. Used in Pathfinder. Never drops gameStates[0].
 	void rollback(int frame);

 	/**
 	 * Continue from a player state made outside the sim (e.g. copied from GD) instead of the level start.
 	 * `state.frame` is kept, so frame numbers stay those of the whole level; earlier frames don't exist
 	 * and getState returns the first state for them.
 	 */
 	void resume(Player state);

 	int currentFrame() const;
 	Player const& getState(int frame, bool p2 = false) const;
 	Player& latestState();
};
