// Lilka (ESP32-S3) front end for Stack Attack.
#include <lilka.h>
#include <esp_random.h>
#include <esp_system.h>
#include <sys/stat.h>
#include "../../core/app.h"

namespace {

sa::App* app = nullptr;
lilka::Canvas* canvas = nullptr;
const char* SAVE_DIR = "/sd/stack_attack";
const char* SAVE_PATH = "/sd/stack_attack/save.bin";
uint32_t lastMs = 0;

// tone sequencer: the buzzer plays one tone at a time, we switch them on schedule
const sa::Tone* seq = nullptr;
int seqN = 0, seqPos = 0;
uint32_t seqNext = 0;

void soundStart(const sa::Tone* t, int n) {
    seq = t;
    seqN = n;
    seqPos = -1;
    seqNext = 0;
}

void soundUpdate(uint32_t now) {
    if (!seq || now < seqNext) return;
    if (++seqPos >= seqN) {
        lilka::buzzer.stop();
        seq = nullptr;
        return;
    }
    const sa::Tone& t = seq[seqPos];
    if (t.hz) lilka::buzzer.play(t.hz);
    else lilka::buzzer.stop();
    seqNext = now + t.ms;
}

sa::SaveData loadSave() {
    sa::SaveData s;
    FILE* f = fopen(SAVE_PATH, "rb");
    if (f) {
        sa::SaveData t;
        if (fread(&t, 1, sizeof t, f) == sizeof t) s = t;
        fclose(f);
    }
    return s;
}

void writeSave(const sa::SaveData& s) {
    mkdir(SAVE_DIR, 0777);
    FILE* f = fopen(SAVE_PATH, "wb");
    if (!f) return;
    fwrite(&s, 1, sizeof s, f);
    fclose(f);
}

void drawTexts() {
    static const uint8_t* fonts[3] = {FONT_6x13, FONT_8x13, FONT_10x20};
    for (const auto& t : app->texts()) {
        canvas->setFont(fonts[t.size > 2 ? 2 : t.size]);
        canvas->setTextColor(t.color);
        lilka::Alignment h = t.align == 1 ? lilka::ALIGN_CENTER : (t.align == 2 ? lilka::ALIGN_END : lilka::ALIGN_START);
        canvas->drawTextAligned(t.s.c_str(), t.x, t.y, h, lilka::ALIGN_START);
    }
}

} // namespace

// Start-up steps go to the serial port and to the screen. On screen they are drawn black on
// black: the timing stays exactly as in the debug build that was confirmed to start on a device.
// Define STACK_DEBUG to see them.
int stageY = 150;
void stage(const char* msg) {
    Serial.printf("[stack] %s\n", msg);
    lilka::display.setFont(FONT_6x13);
#ifdef STACK_DEBUG
    lilka::display.setTextColor(0xFFE0);
#else
    lilka::display.setTextColor(0x0000);
#endif
    lilka::display.drawTextAligned(msg, 10, stageY, lilka::ALIGN_START, lilka::ALIGN_START);
    stageY += 14;
}

void setup() {
    Serial.begin(115200);
    lilka::begin();
    lilka::display.fillScreen(0);
    lilka::display.setFont(FONT_8x13);
    lilka::display.setTextColor(0xFFFF);
    lilka::display.drawTextAligned("Stack Attack...", sa::SCR_W / 2, 120, lilka::ALIGN_CENTER, lilka::ALIGN_CENTER);
    stage("1 lilka ok");
    canvas = new lilka::Canvas(sa::SCR_W, sa::SCR_H);
    stage(canvas->getFramebuffer() ? "2 canvas ok" : "2 canvas NULL");
    app = new sa::App();
    stage("3 app ok");
    sa::SaveData save = loadSave();
    stage("4 save ok");
    app->init((uint16_t*)canvas->getFramebuffer(), save, esp_random());
    stage("5 init ok");
    lastMs = millis();
}

void loop() {
    lilka::State st = lilka::controller.getState();
    const lilka::ButtonState* bs[sa::BTN_COUNT] = {&st.up, &st.down, &st.left, &st.right, &st.a,
                                                     &st.b, &st.c, &st.d, &st.select, &st.start};
    for (int i = 0; i < sa::BTN_COUNT; i++) {
        if (bs[i]->justPressed) app->setButton((sa::Button)i, true);
        app->setButton((sa::Button)i, bs[i]->pressed);
    }
    uint32_t now = millis();
    uint32_t dt = now - lastMs;
    lastMs = now;
    static bool first = true;
    if (first) stage("6 loop");
    if (app->update(dt)) {
        if (first) stage("7 update ok");
        app->render();
        if (first) stage("8 render ok");
        drawTexts();
        if (first) { stage("9 texts ok"); delay(1500); }
        lilka::display.drawCanvas(canvas);
        first = false;
    }
    int n;
    if (const sa::Tone* t = app->takeSound(n)) soundStart(t, n);
    soundUpdate(millis());
    if (app->saveDirty()) writeSave(app->saveData());
    if (app->exitRequested()) {
        lilka::buzzer.stop();
        esp_restart();  // back to KeiraOS
    }
    vTaskDelay(1);
}
