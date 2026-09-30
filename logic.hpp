#pragma once
// Pure logic (no Geode/GD deps) so it can be unit tested off-device.
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace fc {

enum Bucket : int { B_10_15 = 0, B_7_9, B_5_6, B_4, B_3, B_2, B_1, B_CBS, B_OTHER, B_COUNT };

inline const char* bucketName(int b) {
    static const char* N[B_COUNT] = {"10-15", "7-9", "5-6", "4", "3", "2", "1", "<1", "16+"};
    return N[b];
}

// gap in frames -> bucket, -1 = ignore
inline int bucketFor(double g) {
    if (!(g > 0.0)) return -1;
    if (g < 0.97) return B_CBS;  // sub-frame: only possible with click-between-steps
    int n = static_cast<int>(std::floor(g + 0.03));
    if (n <= 1) return B_1;
    if (n == 2) return B_2;
    if (n == 3) return B_3;
    if (n == 4) return B_4;
    if (n <= 6) return B_5_6;
    if (n <= 9) return B_7_9;
    if (n <= 15) return B_10_15;
    return B_OTHER;
}

constexpr double INF = std::numeric_limits<double>::infinity();

struct Stream {
    bool has = false;
    bool down = false;
    unsigned step = 0;
    double ts = -1.0;   // normalized seconds, -1 = unknown
    double raw = -1.0;  // raw timestamp as given
};

struct InputResult {
    bool counted = false;
    int bucket = -1;
    double gap = 0.0;
    bool isHold = false;
};

struct Tracker {
    int counts[B_COUNT] = {};
    int session[B_COUNT] = {};
    int total = 0, sessionTotal = 0;
    double tightest = INF, sessionTightest = INF;
    double shortestHold = INF, sessionShortestHold = INF;
    double stepDt = 1.0 / 240.0;
    double tsScale = 1.0;
    bool calValid = false;
    unsigned calStep = 0;
    double calLT = 0.0;
    Stream st[2];

    void resetAttempt() {
        for (int i = 0; i < B_COUNT; i++) { session[i] += counts[i]; counts[i] = 0; }
        sessionTotal += total; total = 0;
        sessionTightest = std::min(sessionTightest, tightest); tightest = INF;
        sessionShortestHold = std::min(sessionShortestHold, shortestHold); shortestHold = INF;
        st[0] = Stream{}; st[1] = Stream{};
        calValid = false;
    }

    void resetAll() { *this = Tracker{}; }
    double tps() const { return 1.0 / stepDt; }

    InputResult onInput(unsigned step, double levelTime, std::optional<double> rawTs, bool down, bool p1) {
        InputResult res;

        // self-calibrate seconds-per-step (follows TPS bypass)
        if (calValid && step > calStep) {
            double dt = (levelTime - calLT) / static_cast<double>(step - calStep);
            if (dt > 0.0005 && dt < 0.05) stepDt = dt;
        }
        calStep = step; calLT = levelTime; calValid = true;

        Stream& a = st[p1 ? 0 : 1];
        Stream& o = st[p1 ? 1 : 0];

        // timestamp unit auto-detect (s / ms / us / ns) from a long integer gap
        if (rawTs && a.has && a.raw >= 0.0 && step >= a.step + 5) {
            double r = (*rawTs - a.raw) / ((step - a.step) * stepDt);
            if (r > 0.3 && r < 3.0) tsScale = 1.0;
            else if (r > 300.0 && r < 3000.0) tsScale = 1e-3;
            else if (r > 3e5 && r < 3e6) tsScale = 1e-6;
            else if (r > 3e8 && r < 3e9) tsScale = 1e-9;
        }
        double ts = rawTs ? *rawTs * tsScale : -1.0;

        // dual mode: the game mirrors one click to both players -> count it once
        bool mirrored = o.has && o.step == step && o.down == down &&
                        (ts < 0.0 || o.ts < 0.0 || std::abs(ts - o.ts) < 1e-6);

        if (a.has && step >= a.step && !mirrored) {
            double g = static_cast<double>(step - a.step);
            if (ts >= 0.0 && a.ts >= 0.0) {
                double tsg = (ts - a.ts) / stepDt;
                if (tsg > 0.0 && std::abs(tsg - g) <= 1.0) g = tsg;
            }
            if (g <= 0.0) g = 0.5;  // two inputs, same step, can only be CBS
            int b = bucketFor(g);
            if (b >= 0) {
                counts[b]++; total++;
                tightest = std::min(tightest, g);
                res = {true, b, g, a.down && !down};
                if (res.isHold) shortestHold = std::min(shortestHold, g);
            }
        }
        a.has = true; a.down = down; a.step = step; a.ts = ts; a.raw = rawTs ? *rawTs : -1.0;
        return res;
    }
};

} // namespace fc
