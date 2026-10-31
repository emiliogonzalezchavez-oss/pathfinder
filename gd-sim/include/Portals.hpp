#pragma once
#include <EffectObject.hpp>
#include <Vehicle.hpp>


struct VehiclePortal : public EffectObject {
    VehicleType type;
    VehiclePortal(Vec2D size, std::unordered_map<int, std::string>&& fields);
    void collide(Player&) const override;
};

struct GravityPortal : public EffectObject {
    bool upsideDown;
    GravityPortal(Vec2D size, std::unordered_map<int, std::string>&& fields);
    void collide(Player&) const override;
};

struct SizePortal : public EffectObject {
    bool small;
    SizePortal(Vec2D size, std::unordered_map<int, std::string>&& fields);
    void collide(Player&) const override;
};

/// Mirror portal (45 on, 46 off). Only its effect on block sprites matters, see Player::flip
struct MirrorPortal : public EffectObject {
    bool mirrored;
    MirrorPortal(Vec2D size, std::unordered_map<int, std::string>&& fields);
    void collide(Player&) const override;
};

/// Dual portal (286 on) and solo portal (287 off), GJBaseGameLayer::toggleDualMode
struct DualPortal : public EffectObject {
    bool on;
    DualPortal(Vec2D size, std::unordered_map<int, std::string>&& fields);
    bool touching(Player const&) const override;
    void collide(Player&) const override;
};

struct SpeedPortal : public EffectObject {
    int speed;
    SpeedPortal(Vec2D size, std::unordered_map<int, std::string>&& fields);
    bool touching(Player const&) const override;
    void collide(Player&) const override;
};

/// Teleport portal (747): moves the player by its exit portal's y offset (key 54)
struct TeleportPortal : public EffectObject {
    float offset;
    /// 2.2 teleport portal (2902) / teleport orb (3027): the group whose object it sends the player to (key 51), 0 for 747
    int target = 0;
    bool orb = false;
    /// TeleportPortalObject options: m_ignoreX / Y (352, 353), m_gravityMode (354: 1 normal, 2 flipped, 3 toggle),
    /// m_staticForceEnabled / m_staticForce / m_staticForceAdditive (345, 346, 443)
    bool ignoreX = false, ignoreY = false, staticForce = false, additive = false;
    int gravityMode = 0;
    float force = 0, angle = 90;
    TeleportPortal(Vec2D size, std::unordered_map<int, std::string>&& fields);
    void collide(Player&) const override;
};
