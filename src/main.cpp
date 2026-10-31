#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/coro.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <UIBuilder.hpp>
#include "pathfinder.hpp"
#include <future>

using namespace geode::prelude;
using namespace geode::utils::file;

class PathfinderNode : public CCLayerColor {
    std::atomic_bool m_stop = false;
    std::atomic<double> m_progress = 0;
    std::future<std::vector<uint8_t>> m_result;
    std::string m_levelName;
    PathfindStats m_stats;

    // Live status: a bar for the furthest point, a spinner that only turns while the search is simulating
    // (if it stops turning, the search thread is hung), and what it's doing / what's blocking it
    CCLayerColor* m_barFill = nullptr;
    CCSprite* m_spinner = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_note = nullptr;
    CCLabelBMFont* m_eta = nullptr;
    float m_movingTime = 0;    // seconds spent while the furthest point was moving (not at a hard spot)
    float m_movingProgress = 0; // x gained during that time
    uint64_t m_lastFrames = 0;
    float m_lastBest = 0;
    float m_sinceFrames = 0;   // seconds since the sim last ran a frame
    float m_sinceBest = 0;     // seconds since the furthest point last moved
    float m_elapsed = 0;
    float m_rate = 0;          // sim frames per second (smoothed)
    bool m_done = false;

    void updateStatus(float dt) {
        if (m_done)
            return;
        m_elapsed += dt;
        uint64_t frames = m_stats.frames;
        float best = m_stats.bestX;
        float length = std::max(1.0f, (float)m_stats.length);

        if (frames != m_lastFrames) {
            float inst = (float)(frames - m_lastFrames) / std::max(dt, 0.001f);
            m_rate = m_rate * 0.8f + inst * 0.2f;
            m_lastFrames = frames;
            m_sinceFrames = 0;
            if (m_spinner)
                m_spinner->setRotation(m_spinner->getRotation() + 360 * dt);
        } else {
            m_sinceFrames += dt;
        }
        bool moving = m_sinceBest < 5;
        if (best > m_lastBest + 0.5f) {
            if (moving)
                m_movingProgress += best - m_lastBest;
            m_lastBest = best;
            m_sinceBest = 0;
        } else {
            m_sinceBest += dt;
        }
        if (moving)
            m_movingTime += dt;

        float pct = std::clamp(best / length, 0.0f, 1.0f);
        if (m_barFill)
            m_barFill->setScaleX(pct);

        int mins = (int)m_elapsed / 60, secs = (int)m_elapsed % 60;
        int stuckSecs = (int)m_sinceBest;
        std::string status;
        ccColor3B color;
        if (m_sinceFrames > 3) {
            status = "NOT RESPONDING - the search thread stopped";
            color = {255, 60, 60};
        } else if (m_sinceBest > 90) {
            status = fmt::format("Stuck: no progress for {}:{:02} ({} dead ends)", stuckSecs / 60, stuckSecs % 60,
                m_stats.deadEnds.load());
            color = {255, 170, 40};
        } else {
            status = fmt::format("Working x{} - {:.1f}M frames/s - {}:{:02} - {} dead ends", m_stats.racers.load(),
                m_rate / 1e6, mins, secs, m_stats.deadEnds.load());
            color = {120, 255, 120};
        }
        if (m_status) {
            m_status->setString(status.c_str());
            m_status->setColor(color);
        }
        // Time left. The search's time goes almost all into hard spots, where it waits for a lucky set of inputs:
        // the same level can take 3 seconds or 8 minutes depending on that luck, so no honest single number
        // exists. Shown instead: the time for the rest at the speed it moves between hard spots, and how long
        // the current hard spot has taken.
        if (m_eta) {
            auto fmtTime = [](float t) {
                int s = (int)std::max(0.0f, t);
                return s >= 3600 ? fmt::format("{}h {:02}m", s / 3600, s / 60 % 60) : fmt::format("{}:{:02}", s / 60, s % 60);
            };
            std::string eta;
            if (m_sinceFrames > 3) {
                eta = "";
            } else if (m_sinceBest >= 5) {
                eta = fmt::format("Parte dificil: {} (puede tardar segundos o minutos)", fmtTime(m_sinceBest));
            } else if (m_movingTime > 3 && m_movingProgress > 0) {
                float speed = m_movingProgress / m_movingTime;
                eta = fmt::format("Resto: ~{} si no hay mas partes dificiles", fmtTime((length - best) / speed));
            } else {
                eta = "Calculando tiempo...";
            }
            m_eta->setString(eta.c_str());
        }

        if (m_note) {
            char const* blocker = m_stats.blocker;
            char const* next = m_stats.nextBlocker;
            float nextX = m_stats.nextBlockerX;
            if (blocker && m_sinceBest > 20) {
                m_note->setString(fmt::format("Unsupported here: {}", blocker).c_str());
                m_note->setColor({255, 120, 120});
            } else if (next && nextX > 0) {
                m_note->setString(fmt::format("Mas adelante ({:.0f}%): {} - no soportado",
                    std::min(100.0f, nextX / length * 100), next).c_str());
                m_note->setColor({255, 200, 120});
            } else
                m_note->setString("");
        }
    }
public:
    static PathfinderNode* create(std::string const& levelName, std::string const& lvlString) {
        auto node = new PathfinderNode();
        if (node && node->init(levelName, lvlString)) {
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    ~PathfinderNode() {
        m_stop = true;
        if (m_result.valid())
            m_result.get();
    }

    void finalize(std::vector<uint8_t> macro) {
        m_done = true;
        if (m_eta)
            m_eta->setString("");
        if (m_note)
            m_note->setString("");
        if (m_status) {
            if (macro.empty()) {
                m_status->setString("Error: el nivel no esta descargado (abre su pagina y espera)");
                m_status->setColor({255, 80, 80});
            } else if (m_stats.complete) {
                m_status->setString("Listo: macro completo y verificado");
                m_status->setColor({120, 255, 120});
            } else {
                m_status->setString(fmt::format("Detenido en {:.1f}%: el macro llega hasta ahi", m_progress.load()).c_str());
                m_status->setColor({255, 200, 120});
            }
        }
        getChildByIDRecursive("stop")->setVisible(false);
 
        auto callback = [this, macro](this auto self) -> arc::Future<void> {
            auto saveDir = Mod::get()->getSaveDir();
            if (Loader::get()->isModLoaded("eclipse.eclipse-menu")) {
                saveDir = Loader::get()->getLoadedMod("eclipse.eclipse-menu")->getSaveDir() / "replays";
            }

            if (!exists(saveDir)) {
                create_directories(saveDir);
            }

            FilePickOptions opts(
                saveDir / fmt::format("{}.gdr2", m_levelName), {{
                std::string("Macro File"),
                std::unordered_set {std::string("gdr2")}
            }});

            if (auto path = co_await pick(PickMode::SaveFile, opts); path.isOk() && path.unwrap().has_value()) {
                (void)writeBinary(*path.unwrap(), macro);
                queueInMainThread([this] {
                    removeFromParentAndCleanup(true);
                });
            }
        };

        Build<ButtonSprite>::create("Export", "bigFont.fnt", "GJ_button_01.png")
            .intoMenuItem(async::wrapSpawn(callback))
            .scale(0.8)
            .move(0, -40)
            .parent(getChildByID("menu"));
    }

    void keyBackClicked() override  {
        m_stop = true;
        CCLayer::keyBackClicked();
        removeFromParentAndCleanup(true);
    }

    bool init(std::string const& levelName, std::string const& lvlString) {
        CCLayerColor::initWithColor({0, 0, 0, 100});
        setCascadeOpacityEnabled(true);

        m_levelName = levelName;

        m_result = std::async(std::launch::async, [lvlString, this]() {
            try {
            if (lvlString.empty())
                return std::vector<uint8_t>();
            return pathfindParallel(lvlString, m_stop, [this](double progress) {
                if (m_progress < progress)
                    m_progress = progress;
            }, &m_stats);
            } catch (std::exception& e) {
                log::error("{}", e.what());
                return std::vector<uint8_t>();
            }
        });

        setKeypadEnabled(true);

        Build(this).initTouch().schedule([this](float dt) {
                Build(this).intoChildRecurseID<CCLabelBMFont>("percent")
                    .string(fmt::format("{:.2f}%", m_progress).c_str());
                updateStatus(dt);

                if (m_result.valid() && m_result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    finalize(m_result.get());
                }
        });

        auto handle = [this](CCMenuItemSpriteExtra* it) {
            m_stop = true;

            if (it->getID() == "stop")
                finalize(m_result.get());
            else
                removeFromParentAndCleanup(true);
        };

        auto menu = Build<CCMenu>::create().parent(this).id("menu").children(
            Build<CCScale9Sprite>::create("GJ_square02.png")
                .contentSize(340, 190),
            Build<CCLabelBMFont>::create("Pathfinding...", "bigFont.fnt")
                .move(0, 70)
                .scale(0.8),
            Build<CCLabelBMFont>::create("0.00", "chatFont.fnt")
                .id("percent")
                .move(0, 40),
            Build<ButtonSprite>::create("Stop", "bigFont.fnt", "GJ_button_04.png")
                .scale(0.8)
                .intoMenuItem(handle)
                .id("stop")
                .move(0, -65),
            Build<CCSprite>::createSpriteName("GJ_closeBtn_001.png")
                .intoMenuItem(handle)
                .id("close")
                .move(-170, 95)
                .scale(0.8)
        ).collect();

        // Progress bar (furthest point reached)
        auto barBg = CCLayerColor::create({0, 0, 0, 150}, 220, 10);
        barBg->setPosition({-110, 18});
        menu->addChild(barBg);
        m_barFill = CCLayerColor::create({90, 255, 90, 255}, 220, 10);
        m_barFill->setAnchorPoint({0, 0});
        m_barFill->ignoreAnchorPointForPosition(false);
        m_barFill->setPosition({-110, 18});
        m_barFill->setScaleX(0);
        menu->addChild(m_barFill);

        // Spinner: turns only while the sim is running frames
        m_spinner = CCSprite::create("loadingCircle.png");
        if (m_spinner) {
            m_spinner->setScale(0.35f);
            m_spinner->setPosition({145, 70});
            m_spinner->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            menu->addChild(m_spinner);
        }

        m_status = CCLabelBMFont::create("Starting...", "chatFont.fnt");
        m_status->setScale(0.6f);
        m_status->setPosition({0, -2});
        menu->addChild(m_status);

        m_eta = CCLabelBMFont::create("Calculando tiempo...", "chatFont.fnt");
        m_eta->setScale(0.45f);
        m_eta->setColor({200, 220, 255});
        m_eta->setPosition({0, -38});
        menu->addChild(m_eta);

        m_note = CCLabelBMFont::create("", "chatFont.fnt");
        m_note->setScale(0.6f);
        m_note->setColor({255, 120, 120});
        m_note->setPosition({0, -22});
        menu->addChild(m_note);

        /*    .intoNewChild(CCMenu::create())
                .id("menu")
                .intoNewChild(CCScale9Sprite::create("GJ_square04.png"))
                    .contentSize(250, 140)
                .intoNewSibling(CCLabelBMFont::create("Pathfinding...", "bigFont.fnt"))
                    .move(0, 50)
                    .scale(0.8)
                .intoNewSibling(CCLabelBMFont::create("0.00", "chatFont.fnt"))
                    .id("percent")
                    .move(0, 10)
                .intoNewSibling(ButtonSprite::create("Stop", "bigFont.fnt", "GJ_button_04.png"))
                    .intoMenuItem([this]() {
                        m_stop = true;
                        finalize(m_result.get());
                    })
                    .scale(0.8)
                    .id("cancel")
                    .move(0, -40)
                .intoNewSibling(CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"))
                    .intoMenuItem([this]() {
                        m_stop = true;
                        this->removeFromParentAndCleanup(true);
                    })
                    .move(-125, 70)
                    .scale(0.8);*/
        ;

        return true;
    }

};

class $modify(EditLevelLayer) {
    bool init(GJGameLevel* p0) {
        EditLevelLayer::init(p0);

        auto btn = Build<BasedButtonSprite>::create(
            CCSprite::create("pathfinder.png"_spr),
            BaseType::Circle,
            4,
            3
        ).scale(0.8);

        btn->setTopRelativeScale(1.4);

        btn.intoMenuItem([this]() {
                std::string raw = m_level->m_levelString;
                auto lvlString = raw.empty() ? std::string() : std::string(ZipUtils::decompressString(raw, true, 0));
                Build<PathfinderNode>::create(m_level->m_levelName, lvlString).parent(this).zOrder(100);
        }).id("pathfinder-button")
          .intoNewParent(CCMenu::create())
          .parent(this)
          .id("pathfinder-menu")
          .matchPos(getChildByIDRecursive("delete-button"))
          .move(-45, 0);


        return true;
    }
};

class $modify(LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        LevelInfoLayer::init(level, challenge);

        auto btn = Build<BasedButtonSprite>::create(
            CCSprite::create("pathfinder.png"_spr),
            BaseType::Circle,
            4,
            3
        ).scale(0.8);

        btn->setTopRelativeScale(1.4);

        btn.intoMenuItem([this]() {
                std::string raw = m_level->m_levelString;
                auto lvlString = raw.empty() ? std::string() : std::string(ZipUtils::decompressString(raw, true, 0));
                Build<PathfinderNode>::create(m_level->m_levelName, lvlString).parent(this).zOrder(100);
        }).id("pathfinder-button")
          .parent(getChildByID("other-menu"))
          .matchPos(getChildByIDRecursive("list-button"))
          .move(0, 45);

        return true;
    }
};
