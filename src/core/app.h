#pragma once
// Stack Attack for Lilka: screens, menus, options, sounds and saves around the
// platform-independent game core (src/game, from Negenii/flipper-stack-attack, MIT).
#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "../game/game.h"
#include "../game/render.h"
}

namespace sa {

static const int SCR_W = 280, SCR_H = 240;  // Lilka screen (landscape)
static const int SCALE = 3;                       // the 104x64 field, x3, crisp pixels
static const int BAR_H = 48;                      // score bar on top
static const int FIELD_Y = BAR_H;                 // 64 * 3 = 192 rows below the bar
static const int FIELD_CROP = (G_FIELD_W * SCALE - SCR_W) / 2;  // 16 px cut on each side (the walls)

enum Button { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, BTN_C, BTN_D, BTN_SELECT, BTN_START, BTN_COUNT };

struct TextItem {
    int x, y;  // top of the text line
    std::string s;
    uint16_t color;
    uint8_t size;   // 0 small, 1 normal, 2 big
    uint8_t align;  // 0 left, 1 center, 2 right
};

struct Tone { uint16_t hz, ms; };  // hz = 0: silence

struct SaveData {
    uint32_t magic = 0x53544B31;  // "STK1"
    uint32_t hiscore[2] = {0, 0};  // Original, Fast
    uint8_t sound = 1, scheme = 0, lang = 0;  // scheme 0 = Siemens yellow, 1 = black & white; lang 0 = UA
    uint8_t mode = 0;
};

class App {
public:
    void init(uint16_t* screen, const SaveData& save, uint32_t seed);
    void setButton(Button b, bool down);
    bool update(uint32_t ms);  // true if the screen must be redrawn
    void render();
    const std::vector<TextItem>& texts() const { return texts_; }

    // A sound to start now (nullptr if none); `n` gets the number of tones.
    const Tone* takeSound(int& n);
    bool saveDirty() { bool d = dirty_save_; dirty_save_ = false; return d; }
    const SaveData& saveData() const { return save_; }
    bool exitRequested() const { return exit_; }

private:
    enum State { TITLE, OPTIONS, PLAY, PAUSE, OVER };
    void enter(State s);
    void tick();
    void startGame();
    void playEvents();
    const char* tr(int id) const;
    uint16_t ink() const;
    uint16_t paper() const;
    void text(int x, int y, const std::string& s, uint16_t c, int size = 1, int align = 0);
    void rect(int x, int y, int w, int h, uint16_t c);
    void frame(int x, int y, int w, int h, int t, uint16_t c);
    void blitFb(const uint8_t* fb, int x, int y, int scale, int w = FB_W, int h = FB_H);
    void header(const std::string& title);
    void footer(bool back, bool select);
    void arrow(int x, int y);
    void list(const std::vector<std::string>& items, int sel, int y0, int step);
    void popup(int x, int y, int w, int h);
    void drawField();
    void blitField(const uint8_t* fb);  // field part of the frame buffer, x3, cropped to the screen
    void scoreBar();

    uint16_t* scr_ = nullptr;
    SaveData save_;
    bool dirty_save_ = false, exit_ = false, redraw_ = true;
    std::vector<TextItem> texts_;

    State state_ = TITLE, optReturn_ = TITLE;
    int sel_ = 0;
    Game game_;
    uint8_t held_ = 0, pressed_ = 0;
    bool btn_[BTN_COUNT] = {false}, edge_[BTN_COUNT] = {false};
    uint32_t acc_ = 0;
    uint32_t seed_ = 1;
    uint32_t soundUntil_ = 0;
    bool newRecord_ = false;
    int overTicks_ = 0;
    const Tone* snd_ = nullptr;
    int sndN_ = 0;
    uint8_t fb_[FB_SIZE];
};

} // namespace sa
