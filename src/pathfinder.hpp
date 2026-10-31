#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// Work counters of one search (pathfinder-cli prints them)
struct PathfindStats {
	/// Sim frames run (runFrame calls), the search's cost independent of machine load. Updated live.
	std::atomic<uint64_t> frames = 0;
	/// Batches of candidates tried
	std::atomic<uint64_t> rounds = 0;
	/// Dead ends backed off from
	std::atomic<uint64_t> deadEnds = 0;
	/// Furthest x reached and the level's length
	std::atomic<float> bestX = 0, length = 1;
	/// Unsupported mechanic (static string) right around the furthest point, or nullptr
	std::atomic<char const*> blocker = nullptr;
	/// The next unsupported mechanic past the furthest point (x, static name), or x < 0 for none
	std::atomic<float> nextBlockerX = -1;
	std::atomic<char const*> nextBlocker = nullptr;
	/// The search reached the end and its macro passed the final check (a fresh sim, exact hitboxes)
	std::atomic<bool> complete = false;
	/// Searches running side by side (pathfindParallel)
	std::atomic<int> racers = 1;
};

/// Several searches with different seeds side by side (one per spare CPU core); the first that finishes and passes
/// the final check wins. Stopped early, the one that got furthest is exported.
std::vector<uint8_t> pathfindParallel(std::string const& lvlString, std::atomic_bool& stop, std::function<void(double)> callback,
	PathfindStats* stats = nullptr, int threads = 0);

/// seed 0 = random
std::vector<uint8_t> pathfind(std::string const& lvlString, std::atomic_bool& stop, std::function<void(double)> callback,
	uint32_t seed = 0, PathfindStats* stats = nullptr);
