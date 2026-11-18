#include "app.h"
#include <cstdio>
#include <cstring>

namespace sa {

namespace {

inline uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }

// Siemens C35/C45-style yellow backlight, and plain black & white.
const uint16_t INK[2] = {rgb565(44, 36, 12), rgb565(18, 18, 18)};
const uint16_t PAPER[2] = {rgb565(230, 200, 70), rgb565(240, 240, 232)};

// Original runs at 15 ticks/s with one crane (like the phone), Fast at 20 with two.
const uint32_t MODE_HZ[2] = {15, 20};
const uint8_t MODE_CRANES[2] = {1, 2};

// Sounds of the Siemens C45 beeper as measured by the Flipper remake (Hz, ms),
// played an octave lower: the piezo sounds shriller than the phone did.
#define T(hz, ms) {(uint16_t)((hz) / 2), ms}
const Tone SND_JUMP[] = {T(4220, 140)};
const Tone SND_LAND[] = {T(1940, 150)};
const Tone SND_PUSH[] = {T(1770, 70), T(1640, 80), {0, 280}, T(1770, 80), T(1850, 75)};
const Tone SND_BLOW[] = {T(1640, 130), T(1940, 135)};
const Tone SND_SCORE[] = {T(1850, 10), T(4180, 55), {0, 145}, T(1770, 145), T(1980, 70),
                          T(4690, 35), T(3920, 15), T(4690, 35), T(3920, 10), T(4690, 45)};
const Tone SND_OVER[] = {T(1810, 155), T(1680, 70), T(1980, 70), T(1850, 95), {0, 50}, T(1770, 310)};
#undef T
#define N(a) (int)(sizeof(a) / sizeof(a[0]))

enum Str {
    T_TITLE, T_ORIGINAL, T_FAST, T_OPTIONS, T_EXIT, T_RECORD, T_SCHEME_SIEMENS, T_SCHEME_BW, T_SOUND_ON,
    T_SOUND_OFF, T_LANG, T_BACK, T_SELECT, T_PAUSE, T_CONTINUE, T_MENU, T_GAME_OVER, T_NEW_RECORD, T_OVER_KEYS,
    T_PLAY_KEYS, T_BAR_SCORE, T_BAR_HI, T_BAR_CRANES, T_COUNT
};
const char* const UA[T_COUNT] = {
    "Stack Attack", "Оригінал", "Швидка гра", "Опції", "Вихід", "Рекорд: %u", "Екран: Siemens",
    "Екран: чорно-білий", "Звук: увімкн.", "Звук: вимкн.", "Мова: українська", "B назад", "A вибрати",
    "Пауза", "Продовжити", "Головне меню", "Гру закінчено", "Новий рекорд!", "A - ще раз   B - меню",
    "Вгору/A - стрибок   START - пауза", "Очки", "Рекорд", "Крани",
};
const char* const EN[T_COUNT] = {
    "Stack Attack", "Original", "Fast", "Options", "Exit", "Best: %u", "Screen: Siemens",
    "Screen: black & white", "Sound: on", "Sound: off", "Language: English", "B back", "A select",
    "Pause", "Continue", "Main menu", "Game over", "New record!", "A - again   B - menu",
    "Up/A - jump   START - pause", "Score", "Best", "Cranes",
};

const int HEAD_H = 34, ROW_H = 26, FOOT_Y = 206;

} // namespace

const char* App::tr(int id) const { return (save_.lang ? EN : UA)[id]; }
uint16_t App::ink() const { return INK[save_.scheme & 1]; }
uint16_t App::paper() const { return PAPER[save_.scheme & 1]; }

void App::init(uint16_t* screen, const SaveData& save, uint32_t seed) {
    scr_ = screen;
    save_ = save.magic == SaveData().magic ? save : SaveData();
    seed_ = seed ? seed : 1;
    enter(TITLE);
    sel_ = save_.mode & 1;
}

void App::enter(State s) {
    state_ = s;
    sel_ = 0;
    redraw_ = true;
}

void App::setButton(Button b, bool down) {
    if (down && !btn_[b]) edge_[b] = true;
    btn_[b] = down;
    // in play the game itself tracks held keys and presses between ticks
    uint8_t bit = b == BTN_LEFT ? G_KEY_LEFT : b == BTN_RIGHT ? G_KEY_RIGHT : (b == BTN_UP || b == BTN_A) ? G_KEY_JUMP : 0;
    if (!bit) return;
    if (down) {
        if (!(held_ & bit)) pressed_ |= bit;
        held_ |= bit;
    } else {
        held_ &= ~bit;
    }
}

void App::startGame() {
    seed_ = seed_ * 1664525u + 1013904223u;
    game_init(&game_, seed_, MODE_CRANES[save_.mode & 1]);
    held_ = pressed_ = 0;
    newRecord_ = false;
    soundUntil_ = 0;
    acc_ = 0;
    enter(PLAY);
}

const Tone* App::takeSound(int& n) {
    const Tone* s = snd_;
    n = sndN_;
    snd_ = nullptr;
    return s;
}

// One sound per tick, most important first; minor ones wait for the current one to end.
void App::playEvents() {
    uint8_t ev = game_.events;
    const Tone* s = nullptr;
    int n = 0, ms = 0;
    bool always = false;
    if (ev & G_EV_DEATH) s = SND_OVER, n = N(SND_OVER), ms = 750, always = true;
    else if (ev & G_EV_CLEAR) s = SND_SCORE, n = N(SND_SCORE), ms = 580, always = true;
    else if (ev & G_EV_BREAK) s = SND_BLOW, n = N(SND_BLOW), ms = 265;
    else if (ev & G_EV_PUSH) s = SND_PUSH, n = N(SND_PUSH), ms = 585;
    else if (ev & G_EV_JUMP) s = SND_JUMP, n = N(SND_JUMP), ms = 140;
    else if (ev & G_EV_WLAND) s = SND_LAND, n = N(SND_LAND), ms = 150;
    uint32_t now = game_.tick;
    if (s && save_.sound && (always || now >= soundUntil_)) {
        snd_ = s;
        sndN_ = n;
        soundUntil_ = now + (ms * MODE_HZ[save_.mode & 1] + 999) / 1000;
    }
}

bool App::update(uint32_t ms) {
    bool changed = redraw_;
    auto took = [&](Button b) { bool e = edge_[b]; return e; };
    auto menuMove = [&](int n) {
        if (took(BTN_UP)) { sel_ = (sel_ + n - 1) % n; changed = true; }
        if (took(BTN_DOWN)) { sel_ = (sel_ + 1) % n; changed = true; }
    };

    switch (state_) {
        case TITLE:
            menuMove(4);
            if (took(BTN_A) || took(BTN_START)) {
                if (sel_ < 2) {
                    save_.mode = (uint8_t)sel_;
                    dirty_save_ = true;
                    startGame();
                } else if (sel_ == 2) {
                    optReturn_ = TITLE;
                    enter(OPTIONS);
                } else {
                    exit_ = true;
                }
                changed = true;
            }
            break;

        case OPTIONS:
            menuMove(3);
            if (took(BTN_A) || took(BTN_LEFT) || took(BTN_RIGHT)) {
                if (sel_ == 0) save_.scheme ^= 1;
                else if (sel_ == 1) save_.sound ^= 1;
                else save_.lang ^= 1;
                dirty_save_ = true;
                changed = true;
            }
            if (took(BTN_B)) {
                State back = optReturn_;
                enter(back);
                if (back == TITLE) sel_ = 2;
                else sel_ = 1;
                changed = true;
            }
            break;

        case PLAY:
            if (took(BTN_START) || took(BTN_B)) {
                enter(PAUSE);
                changed = true;
                break;
            }
            acc_ += ms;
            while (acc_ >= 1000 / MODE_HZ[save_.mode & 1] && state_ == PLAY) {
                acc_ -= 1000 / MODE_HZ[save_.mode & 1];
                game_tick(&game_, held_, pressed_);
                pressed_ = 0;
                playEvents();
                if (game_.events & G_EV_DEATH) {
                    uint32_t& hi = save_.hiscore[save_.mode & 1];
                    if (game_.score > hi) {
                        hi = game_.score;
                        newRecord_ = true;
                        dirty_save_ = true;
                    }
                    overTicks_ = 0;
                    enter(OVER);
                }
                changed = true;
            }
            break;

        case PAUSE:
            menuMove(3);
            if (took(BTN_B) || took(BTN_START) || (took(BTN_A) && sel_ == 0)) {
                held_ = pressed_ = 0;
                acc_ = 0;
                enter(PLAY);
            } else if (took(BTN_A) && sel_ == 1) {
                optReturn_ = PAUSE;
                enter(OPTIONS);
            } else if (took(BTN_A)) {
                enter(TITLE);
                sel_ = save_.mode & 1;
            }
            changed = true;
            break;

        case OVER:
            acc_ += ms;
            if (acc_ >= 66) { acc_ = 0; overTicks_++; }
            if (overTicks_ >= 10) {
                if (took(BTN_A) || took(BTN_START)) startGame();
                else if (took(BTN_B)) { enter(TITLE); sel_ = save_.mode & 1; }
            }
            if (overTicks_ == 10) changed = true;
            break;
    }
    for (auto& e : edge_) e = false;
    redraw_ = false;
    return changed;
}

// ------------------------------------------------------------------ drawing

void App::text(int x, int y, const std::string& s, uint16_t c, int size, int align) {
    texts_.push_back(TextItem{x, y, s, c, (uint8_t)size, (uint8_t)align});
}

void App::rect(int x, int y, int w, int h, uint16_t c) {
    for (int j = y < 0 ? 0 : y; j < y + h && j < SCR_H; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < SCR_W; i++) scr_[j * SCR_W + i] = c;
}

void App::frame(int x, int y, int w, int h, int t, uint16_t c) {
    rect(x, y, w, t, c);
    rect(x, y + h - t, w, t, c);
    rect(x, y, t, h, c);
    rect(x + w - t, y, t, h, c);
}

// 1-bit framebuffer (XBM layout, LSB = leftmost) scaled up onto the screen.
void App::blitFb(const uint8_t* fb, int x0, int y0, int s, int w, int h) {
    uint16_t on = ink(), off = paper();
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t c = (fb[y * FB_STRIDE + (x >> 3)] >> (x & 7)) & 1 ? on : off;
            for (int j = 0; j < s; j++) {
                uint16_t* p = scr_ + (y0 + y * s + j) * SCR_W + x0 + x * s;
                for (int i = 0; i < s; i++) p[i] = c;
            }
        }
}

void App::header(const std::string& title) {
    rect(0, 0, SCR_W, SCR_H, paper());
    rect(0, 0, SCR_W, HEAD_H, ink());
    text(10, 7, title, paper(), 2, 0);
}

void App::footer(bool back, bool select) {
    rect(0, FOOT_Y, SCR_W, 2, ink());
    if (back) text(10, FOOT_Y + 10, tr(T_BACK), ink(), 2, 0);
    if (select) text(SCR_W - 10, FOOT_Y + 10, tr(T_SELECT), ink(), 2, 2);
}

void App::arrow(int x, int y) {
    for (int i = 0; i < 8; i++) rect(x + i, y + i, 1, 16 - 2 * i, ink());
}

void App::list(const std::vector<std::string>& items, int sel, int y0, int step) {
    for (size_t i = 0; i < items.size(); i++) {
        int y = y0 + (int)i * step;
        if ((int)i == sel) arrow(10, y + 2);
        text(28, y, items[i], ink(), 2, 0);
    }
}

void App::popup(int x, int y, int w, int h) {
    rect(x - 2, y - 2, w + 4, h + 4, paper());
    frame(x, y, w, h, 2, ink());
    frame(x + 4, y + 4, w - 8, h - 8, 1, ink());
}

// Only the playing field (x < G_FIELD_W) is shown; the side panel is replaced by the top bar.
void App::blitField(const uint8_t* fb) {
    uint16_t on = ink(), off = paper();
    for (int y = 0; y < FB_H; y++) {
        const uint8_t* row = fb + y * FB_STRIDE;
        uint16_t* dst = scr_ + (FIELD_Y + y * SCALE) * SCR_W;
        for (int sx = 0; sx < SCR_W; sx++) {
            int x = (sx + FIELD_CROP) / SCALE;
            dst[sx] = (row[x >> 3] >> (x & 7)) & 1 ? on : off;
        }
        for (int j = 1; j < SCALE; j++) memcpy(dst + j * SCR_W, dst, SCR_W * 2);
    }
}

// The phone's own 3x5 font, like the original side panel, moved into a strip on top (x3).
// The groups keep away from the screen's rounded corners.
void App::scoreBar() {
    static uint8_t bar[FB_SIZE];
    fb_clear(bar);
    uint32_t hi = save_.hiscore[save_.mode & 1];
    if (game_.score > hi) hi = game_.score;
    char v[3][12];
    snprintf(v[0], 12, "%u", (unsigned)game_.score);
    snprintf(v[1], 12, "%u", (unsigned)hi);
    snprintf(v[2], 12, "%u", (unsigned)game_.cranes);
    const int W = SCR_W / SCALE;  // 93 bar pixels across the screen
    const int INSET = 8;          // clear of the rounded corners
    auto at = [&](const char* s, int align, int y) {
        int w = 4 * (int)strlen(s) - 1;
        int x = align == 0 ? INSET : align == 1 ? (W - w) / 2 : W - INSET - w;
        fb_text(bar, x, y, s);
    };
    at("SCORE", 0, 3);
    at(v[0], 0, 9);
    at("HI", 1, 3);
    at(v[1], 1, 9);
    at("CR", 2, 3);
    at(v[2], 2, 9);
    uint16_t on = ink(), off = paper();
    for (int y = 0; y < BAR_H / SCALE; y++) {
        const uint8_t* row = bar + y * FB_STRIDE;
        uint16_t* dst = scr_ + y * SCALE * SCR_W;
        for (int sx = 0; sx < SCR_W; sx++) {
            int x = sx / SCALE;
            dst[sx] = (row[x >> 3] >> (x & 7)) & 1 ? on : off;
        }
        for (int j = 1; j < SCALE; j++) memcpy(dst + j * SCR_W, dst, SCR_W * 2);
    }
}

void App::drawField() {
    render_game(fb_, &game_, save_.hiscore[save_.mode & 1]);
    scoreBar();
    blitField(fb_);
}

void App::render() {
    texts_.clear();
    switch (state_) {
        case TITLE: {
            rect(0, 0, SCR_W, SCR_H, paper());
            render_title(fb_);
            // only the logo part of the title frame (it sits at the top of the 128x64 buffer)
            blitFb(fb_, (SCR_W - FB_W * 2) / 2, 2, 2, FB_W, 50);
            std::vector<std::string> items = {tr(T_ORIGINAL), tr(T_FAST), tr(T_OPTIONS), tr(T_EXIT)};
            list(items, sel_, 106, 25);
            if (sel_ < 2) {
                char buf[48];
                snprintf(buf, sizeof buf, tr(T_RECORD), (unsigned)save_.hiscore[sel_]);
                text(SCR_W - 10, 110 + sel_ * 25, buf, ink(), 0, 2);
            }
            break;
        }
        case OPTIONS:
            header(tr(T_OPTIONS));
            list({tr(save_.scheme ? T_SCHEME_BW : T_SCHEME_SIEMENS), tr(save_.sound ? T_SOUND_ON : T_SOUND_OFF), tr(T_LANG)},
                 sel_, 44, ROW_H);
            footer(true, true);
            break;
        case PLAY:
            drawField();
            break;
        case PAUSE:
            header(tr(T_PAUSE));
            list({tr(T_CONTINUE), tr(T_OPTIONS), tr(T_MENU)}, sel_, 44, ROW_H);
            footer(true, true);
            break;
        case OVER: {
            drawField();
            int w = 200, h = 96, x = (SCR_W - w) / 2, y = FIELD_Y + (FB_H * SCALE - h) / 2;
            popup(x, y, w, h);
            char buf[24];
            snprintf(buf, sizeof buf, "%u", (unsigned)game_.score);
            text(x + w / 2, y + 12, buf, ink(), 2, 1);
            text(x + w / 2, y + 40, tr(newRecord_ ? T_NEW_RECORD : T_GAME_OVER), ink(), 1, 1);
            if (overTicks_ >= 10) text(x + w / 2, y + 66, tr(T_OVER_KEYS), ink(), 0, 1);
            break;
        }
    }
}

} // namespace sa
