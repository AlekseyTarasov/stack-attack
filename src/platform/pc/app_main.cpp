// PC harness: plays a scripted button sequence, writes 280x240 screens (PPM) and the
// text overlay of each frame (JSON lines). One script unit = one 1/15 s frame.
// usage: stack_app <outdir> "<script>" [lang] [scheme] [every]
// tokens: <btn><frames>; btn: U D L R A B S(select) T(start) _ (wait)
#include "../../core/app.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace sa;
int main(int argc, char** argv) {
    if (argc < 3) return 1;
    std::string out = argv[1], script = argv[2];
    static uint16_t screen[SCR_W * SCR_H];
    SaveData s;
    if (argc > 3) s.lang = (uint8_t)atoi(argv[3]);
    if (argc > 4) s.scheme = (uint8_t)atoi(argv[4]);
    int every = argc > 5 ? atoi(argv[5]) : 1;
    static App app;
    app.init(screen, s, 12345);
    FILE* tj = fopen((out + "/texts.jsonl").c_str(), "w");
    if (!tj) return 1;
    int frame = 0, shot = 0;
    for (size_t p = 0; p < script.size();) {
        while (p < script.size() && script[p] == ' ') p++;
        if (p >= script.size()) break;
        char c = script[p++];
        int n = 0;
        while (p < script.size() && isdigit((unsigned char)script[p])) n = n * 10 + (script[p++] - '0');
        if (!n) n = 1;
        int b = -1;
        switch (c) {
            case 'U': b = BTN_UP; break; case 'D': b = BTN_DOWN; break; case 'L': b = BTN_LEFT; break;
            case 'R': b = BTN_RIGHT; break; case 'A': b = BTN_A; break; case 'B': b = BTN_B; break;
            case 'S': b = BTN_SELECT; break; case 'T': b = BTN_START; break;
        }
        for (int i = 0; i < n; i++, frame++) {
            if (b >= 0 && i == 0) app.setButton((Button)b, true);
            app.update(67);
            if (b >= 0 && i == n - 1) app.setButton((Button)b, false);
            int k; if (app.takeSound(k)) printf("frame %d sound (%d tones)\n", frame, k);
            if (frame % every) continue;
            app.render();
            char path[512];
            snprintf(path, sizeof path, "%s/f%04d.ppm", out.c_str(), shot++);
            FILE* o = fopen(path, "wb");
            fprintf(o, "P6\n%d %d\n255\n", SCR_W, SCR_H);
            for (int q = 0; q < SCR_W * SCR_H; q++) {
                uint16_t v = screen[q];
                uint8_t rgb[3] = {(uint8_t)(((v >> 11) & 31) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31)};
                fwrite(rgb, 1, 3, o);
            }
            fclose(o);
            fprintf(tj, "[");
            bool first = true;
            for (auto& t : app.texts()) {
                std::string e;
                for (char ch : t.s) { if (ch == '"' || ch == '\\') e += '\\'; e += ch; }
                fprintf(tj, "%s[%d,%d,\"%s\",%d,%d,%d]", first ? "" : ",", t.x, t.y, e.c_str(), t.color, t.size, t.align);
                first = false;
            }
            fprintf(tj, "]\n");
        }
    }
    fclose(tj);
    printf("%d frames, %d shots\n", frame, shot);
}
