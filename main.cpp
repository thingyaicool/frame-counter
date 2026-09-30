// Frame Counter - click-gap HUD for Geometry Dash (Geode, Android-compatible).
//
// How it works:
//  * GJBaseGameLayer::handleButton is the single funnel every input goes through:
//    real touches, imported macros and bot replays (xdBot etc.) all end up there.
//  * GJBaseGameLayer::processQueuedButtons is hooked only to grab the sub-frame
//    timestamps (click-between-steps) of the commands it is about to dispatch.
//  * All gap math lives in logic.hpp (unit tested off-device).

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "logic.hpp"

using namespace geode::prelude;

class FCHud;

namespace {

fc::Tracker g_tr;
std::vector<PlayerButtonCommand> g_pending;

FCHud* g_hud = nullptr;

const char* kNames[fc::B_COUNT] = {"10-15", "7-9", "5-6", "4", "3", "2", "1", "<1 cbs", "16+"};

const ccColor3B kColors[fc::B_COUNT] = {
    {90, 255, 90},   // 10-15
    {170, 255, 80},  // 7-9
    {255, 230, 60},  // 5-6
    {255, 170, 40},  // 4
    {255, 120, 40},  // 3
    {255, 70, 50},   // 2
    {255, 40, 40},   // 1
    {255, 100, 255}, // cbs
    {170, 170, 170}  // 16+
};

std::string fmtFrames(double g) {
    if (std::abs(g - std::round(g)) < 0.005) return fmt::format("{}f", static_cast<int>(std::round(g)));
    return fmt::format("{:.2f}f", g);
}

bool isPlay(GJBaseGameLayer* l) {
    auto pl = PlayLayer::get();
    return pl && static_cast<GJBaseGameLayer*>(pl) == l;
}

std::string sanitize(std::string s) {
    for (auto& c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) c = '_';
    }
    return s.empty() ? std::string("level") : s;
}

} // namespace

class FCHud : public CCLayer {
protected:
    CCNode* m_root = nullptr;
    CCScale9Sprite* m_bg = nullptr;
    CCLabelBMFont* m_rows[fc::B_COUNT] = {};
    CCLabelBMFont* m_tight = nullptr;
    CCLabelBMFont* m_hold = nullptr;
    CCLabelBMFont* m_tps = nullptr;
    CCLabelBMFont* m_toast = nullptr;
    bool m_dirty = true;
    bool m_dragging = false;
    CCPoint m_dragOffset = {0.f, 0.f};

    CCLabelBMFont* makeLabel() {
        auto l = CCLabelBMFont::create("", "bigFont.fnt");
        l->setAnchorPoint({0.f, 1.f});
        m_root->addChild(l, 1);
        return l;
    }

public:
    static FCHud* create() {
        auto ret = new FCHud();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    ~FCHud() override {
        if (g_hud == this) g_hud = nullptr;
    }

    bool init() override {
        if (!CCLayer::init()) return false;

        this->setID("frame-counter-hud"_spr);

        m_root = CCNode::create();
        this->addChild(m_root);

        m_bg = CCScale9Sprite::create("square02b_001.png", {0.f, 0.f, 80.f, 80.f});
        m_bg->setAnchorPoint({0.f, 1.f});
        m_bg->setColor({0, 0, 0});
        m_root->addChild(m_bg, 0);

        for (int i = 0; i < fc::B_COUNT; i++) m_rows[i] = makeLabel();
        m_tight = makeLabel();
        m_hold = makeLabel();
        m_tps = makeLabel();

        auto win = CCDirector::get()->getWinSize();
        m_toast = CCLabelBMFont::create("(with cbs)", "bigFont.fnt");
        m_toast->setScale(0.5f);
        m_toast->setColor({255, 120, 255});
        m_toast->setPosition({win.width / 2.f, win.height - 52.f});
        m_toast->setOpacity(0);
        this->addChild(m_toast, 5);

        this->setTouchEnabled(true);
        this->scheduleUpdate();
        this->rebuild();
        this->applyPosition();
        return true;
    }

    void registerWithTouchDispatcher() override {
        CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -700, true);
    }

    void refresh() { m_dirty = true; }

    void update(float) override {
        if (m_dirty) {
            m_dirty = false;
            this->rebuild();
        }
    }

    void showCbs() {
        if (!Mod::get()->getSettingValue<bool>("cbs-toast")) return;
        m_toast->stopAllActions();
        m_toast->setOpacity(255);
        m_toast->runAction(CCSequence::create(CCDelayTime::create(0.7f), CCFadeOut::create(0.3f), nullptr));
    }

    void applyPosition() {
        auto win = CCDirector::get()->getWinSize();
        float fx = Mod::get()->getSavedValue<float>("hud-x", 0.008f);
        float fy = Mod::get()->getSavedValue<float>("hud-y", 0.985f);
        m_root->setPosition(this->clamp({fx * win.width, fy * win.height}));
    }

    CCPoint clamp(CCPoint p) {
        auto win = CCDirector::get()->getWinSize();
        float s = m_root->getScale();
        auto sz = m_bg->getContentSize();
        float w = sz.width * s, h = sz.height * s;
        p.x = std::max(0.f, std::min(p.x, win.width - w));
        p.y = std::max(h, std::min(p.y, win.height));
        return p;
    }

    void rebuild() {
        auto mod = Mod::get();
        bool compact = mod->getSettingValue<bool>("compact-mode");
        bool colorCode = mod->getSettingValue<bool>("color-code");
        bool showSession = mod->getSettingValue<bool>("show-session");
        bool showTight = mod->getSettingValue<bool>("show-tightest") && !compact;
        bool showHold = mod->getSettingValue<bool>("show-hold") && !compact;
        bool showTps = mod->getSettingValue<bool>("show-tps") && !compact;
        float scale = static_cast<float>(mod->getSettingValue<double>("hud-scale"));
        float opacity = static_cast<float>(mod->getSettingValue<double>("hud-opacity"));
        GLubyte textOp = static_cast<GLubyte>(255.f * opacity);

        m_root->setScale(scale);
        m_root->setVisible(mod->getSettingValue<bool>("enabled"));

        float fontScale = compact ? 0.30f : 0.36f;
        float pad = compact ? 4.f : 6.f;
        float gap = compact ? 1.f : 2.f;

        float y = -pad;
        float maxW = 0.f;

        auto place = [&](CCLabelBMFont* l, bool visible) {
            l->setVisible(visible);
            if (!visible) return;
            l->setScale(fontScale);
            l->setOpacity(textOp);
            l->setPosition({pad, y});
            auto cs = l->getContentSize();
            y -= cs.height * fontScale + gap;
            maxW = std::max(maxW, cs.width * fontScale);
        };

        for (int b = 0; b < fc::B_COUNT; b++) {
            bool extra = (b == fc::B_CBS || b == fc::B_OTHER);
            bool vis = true;
            if (extra && compact && g_tr.counts[b] == 0 && g_tr.session[b] + g_tr.counts[b] == 0) vis = false;
            std::string txt = fmt::format("{}: {}", kNames[b], g_tr.counts[b]);
            if (showSession) txt += fmt::format(" ({})", g_tr.session[b] + g_tr.counts[b]);
            m_rows[b]->setString(txt.c_str());
            m_rows[b]->setColor(colorCode ? kColors[b] : ccColor3B{255, 255, 255});
            place(m_rows[b], vis);
        }

        ccColor3B dim = {200, 200, 200};
        bool hasTight = g_tr.tightest != fc::INF;
        m_tight->setString(hasTight ? fmt::format("best gap: {}", fmtFrames(g_tr.tightest)).c_str() : "best gap: -");
        m_tight->setColor(dim);
        place(m_tight, showTight);

        bool hasHold = g_tr.shortestHold != fc::INF;
        m_hold->setString(hasHold ? fmt::format("min hold: {}", fmtFrames(g_tr.shortestHold)).c_str() : "min hold: -");
        m_hold->setColor(dim);
        place(m_hold, showHold);

        m_tps->setString(fmt::format("{:.0f} TPS", g_tr.tps()).c_str());
        m_tps->setColor(dim);
        place(m_tps, showTps);

        float totalH = -y + pad - gap;
        m_bg->setContentSize({maxW + pad * 2.f, totalH});
        m_bg->setPosition({0.f, 0.f});
        m_bg->setOpacity(static_cast<GLubyte>(255.f * opacity * 0.55f));

        this->applyPosition();
    }

    // dragging: only while the pause menu is open, so it never eats gameplay taps
    bool ccTouchBegan(CCTouch* t, CCEvent*) override {
        if (!m_root || !m_root->isVisible()) return false;
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene || !scene->getChildByType<PauseLayer>(0)) return false;
        auto p = m_root->convertToNodeSpace(t->getLocation());
        auto sz = m_bg->getContentSize();
        CCRect r(0.f, -sz.height, sz.width, sz.height);
        if (!r.containsPoint(p)) return false;
        m_dragging = true;
        m_dragOffset = m_root->getPosition() - this->convertToNodeSpace(t->getLocation());
        return true;
    }

    void ccTouchMoved(CCTouch* t, CCEvent*) override {
        if (!m_dragging) return;
        m_root->setPosition(this->clamp(this->convertToNodeSpace(t->getLocation()) + m_dragOffset));
    }

    void ccTouchEnded(CCTouch*, CCEvent*) override { this->finishDrag(); }
    void ccTouchCancelled(CCTouch*, CCEvent*) override { this->finishDrag(); }

    void finishDrag() {
        if (!m_dragging) return;
        m_dragging = false;
        auto win = CCDirector::get()->getWinSize();
        auto p = m_root->getPosition();
        Mod::get()->setSavedValue<float>("hud-x", p.x / win.width);
        Mod::get()->setSavedValue<float>("hud-y", p.y / win.height);
    }
};

namespace {

// Appends this level's stats (session + current attempt) to <save dir>/stats/<level>.txt
void saveStats(GJGameLevel* level) {
    if (!level || !Mod::get()->getSettingValue<bool>("save-stats")) return;

    int counts[fc::B_COUNT];
    int total = g_tr.sessionTotal + g_tr.total;
    for (int i = 0; i < fc::B_COUNT; i++) counts[i] = g_tr.session[i] + g_tr.counts[i];
    if (total <= 0) return;

    std::string name = level->m_levelName;
    int id = level->m_levelID.value();
    double best = std::min(g_tr.sessionTightest, g_tr.tightest);
    double hold = std::min(g_tr.sessionShortestHold, g_tr.shortestHold);

    std::error_code ec;
    auto dir = Mod::get()->getSaveDir() / "stats";
    std::filesystem::create_directories(dir, ec);
    auto file = dir / (sanitize(name) + "-" + std::to_string(id) + ".txt");

    std::ofstream out(file, std::ios::app);
    if (!out) return;

    auto now = std::time(nullptr);
    char buf[64] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    out << "=== " << name << " (" << id << ") - " << buf << " ===\n";
    for (int i = 0; i < fc::B_COUNT; i++) out << kNames[i] << ": " << counts[i] << "\n";
    out << "total inputs counted: " << total << "\n";
    if (best != fc::INF) out << "tightest gap: " << fmtFrames(best) << "\n";
    if (hold != fc::INF) out << "shortest hold: " << fmtFrames(hold) << "\n";
    out << "\n";
}

} // namespace

class $modify(FCGameLayer, GJBaseGameLayer) {
    void processQueuedButtons(float dt, bool clearInputQueue) {
        if (g_hud && isPlay(this)) {
            g_pending.assign(m_queuedButtons.begin(), m_queuedButtons.end());
        }
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
        g_pending.clear();
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        if (!g_hud || button != 1 || !isPlay(this)) return;
        if (m_player1 && m_player1->m_isDead) return;

        // find this command's sub-frame timestamp (queued/live inputs only;
        // bots that call handleButton directly just get whole-frame gaps)
        std::optional<double> ts;
        for (auto it = g_pending.begin(); it != g_pending.end(); ++it) {
            if (static_cast<int>(it->m_button) == button && it->m_isPush == down &&
                it->m_isPlayer2 == !isPlayer1) {
                ts = it->m_timestamp;
                g_pending.erase(it);
                break;
            }
        }

        auto r = g_tr.onInput(m_gameState.m_currentProgress, m_gameState.m_levelTime, ts, down, isPlayer1);
        if (r.counted) {
            g_hud->refresh();
            if (r.bucket == fc::B_CBS) g_hud->showCbs();
        }
    }
};

class $modify(FCPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        g_tr.resetAll();
        g_pending.clear();
        g_hud = nullptr;

        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled")) return;

        if (mod->getSettingValue<bool>("reset-position")) {
            mod->setSavedValue<float>("hud-x", 0.008f);
            mod->setSavedValue<float>("hud-y", 0.985f);
            mod->setSettingValue<bool>("reset-position", false);
        }

        if (auto hud = FCHud::create()) {
            g_hud = hud;
            this->addChild(hud, 1000);
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        g_tr.resetAttempt();
        g_pending.clear();
        if (g_hud) g_hud->refresh();
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (!g_hud) return;

        saveStats(m_level);

        if (Mod::get()->getSettingValue<bool>("summary-on-complete") && !m_isPracticeMode && !m_isTestMode) {
            std::string txt;
            for (int i = 0; i < fc::B_COUNT; i++) {
                if (i >= fc::B_CBS && g_tr.counts[i] == 0) continue;
                txt += fmt::format("{}: {}\n", kNames[i], g_tr.counts[i]);
            }
            if (g_tr.tightest != fc::INF) txt += fmt::format("best gap: {}\n", fmtFrames(g_tr.tightest));
            if (g_tr.shortestHold != fc::INF) txt += fmt::format("min hold: {}\n", fmtFrames(g_tr.shortestHold));
            txt += fmt::format("inputs counted: {}", g_tr.total);
            FLAlertLayer::create("Frame Counter", txt, "OK")->show();
        }
    }

    void onQuit() {
        if (g_hud) saveStats(m_level);
        PlayLayer::onQuit();
    }
};
