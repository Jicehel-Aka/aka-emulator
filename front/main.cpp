// AKA front-end : ecran + boutons + joystick, dessines avec SDL2.
// Lit le framebuffer partage ecrit par QEMU (/dev/shm/aka-fb) et publie l'etat
// des entrees dans /dev/shm/aka-input (lu plus tard par l'emulation I2C/ADC).
#include <SDL2/SDL.h>
#include "platform.h"
#include <string>
#include <vector>
#include "flashimg.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// ---- masques identiques a gb_ll_common.h (EXPANDER_KEY_*) ----
enum : uint32_t {
    K_RUN = 0x0002, K_MENU = 0x0004, K_R1 = 0x0040, K_L1 = 0x0080,
    K_RIGHT = 0x0100, K_UP = 0x0200, K_DOWN = 0x0400, K_LEFT = 0x0800,
    K_D = 0x1000, K_B = 0x2000, K_C = 0x4000, K_A = 0x8000,
};
static const int JOY_MAX_MV = 3300, JOY_MID_MV = 1650;

// ---- zone partagee d'entree ----
struct InputShared { uint32_t magic, keys; int32_t joyx_mv, joyy_mv, battery_mv, pad[3]; };
static const uint32_t INPUT_MAGIC = 0x49414B41; // 'AKAI'

// ---- framebuffer partage (voir aka_lcdcam.h) ----
static const uint32_t FB_MAGIC = 0x46414B41, FB_HDR = 0x40, FB_MAX = 320;
static const int FB_FLAG_ON = 1;

// ---- police 5x7 minimale (seulement les glyphes utiles) ----
struct Glyph { char c; uint8_t r[7]; };
static const Glyph FONT[] = {
    {'A',{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C',{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, {'D',{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}}, {'M',{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
    {'N',{0x11,0x19,0x15,0x13,0x11,0x11,0x11}}, {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}}, {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}}, {'X',{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y',{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, {'V',{0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}},
    {'=',{0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}}, {'m',{0x00,0x00,0x1A,0x15,0x15,0x15,0x15}},
    {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'2',{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
    {'3',{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}}, {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
    {'5',{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}}, {'6',{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},
    {'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}}, {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
    {'9',{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}}, {'-',{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
    {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'G',{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}},
    {'I',{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}}, {'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
};

static SDL_Renderer *R;
static inline float BOUND_F(float v) { return v < -1.f ? -1.f : (v > 1.f ? 1.f : v); }

static void col(uint32_t c) { SDL_SetRenderDrawColor(R, (c >> 16) & 255, (c >> 8) & 255, c & 255, 255); }

static void text(const std::string &s, int x, int y, int sc, uint32_t c, bool center = true)
{
    int w = (int)s.size() * 6 * sc - sc;
    if (center) x -= w / 2;
    col(c);
    for (char ch : s) {
        for (const Glyph &g : FONT)
            if (g.c == ch)
                for (int ry = 0; ry < 7; ry++)
                    for (int rx = 0; rx < 5; rx++)
                        if (g.r[ry] & (0x10 >> rx)) {
                            SDL_Rect p = {x + rx * sc, y + ry * sc, sc, sc};
                            SDL_RenderFillRect(R, &p);
                        }
        x += 6 * sc;
    }
}

static void disc(int cx, int cy, int r, uint32_t c)
{
    col(c);
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)std::sqrt((double)(r * r - dy * dy));
        SDL_RenderDrawLine(R, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static void rbox(SDL_Rect b, int rad, uint32_t c)
{
    col(c);
    SDL_Rect a = {b.x + rad, b.y, b.w - 2 * rad, b.h}, d = {b.x, b.y + rad, b.w, b.h - 2 * rad};
    SDL_RenderFillRect(R, &a); SDL_RenderFillRect(R, &d);
    disc(b.x + rad, b.y + rad, rad, c); disc(b.x + b.w - rad - 1, b.y + rad, rad, c);
    disc(b.x + rad, b.y + b.h - rad - 1, rad, c); disc(b.x + b.w - rad - 1, b.y + b.h - rad - 1, rad, c);
}

struct Btn { const char *label; uint32_t mask; SDL_Rect r; bool round; uint32_t on; };

static const int LW = 900, LH = 560;
static Btn BT[] = {
    {"L1", K_L1, {40, 24, 130, 34}, false, 0xE8B030},   {"R1", K_R1, {730, 24, 130, 34}, false, 0xE8B030},
    {"",   K_UP, {80, 140, 50, 50}, false, 0x40C0F0},   {"",   K_DOWN, {80, 240, 50, 50}, false, 0x40C0F0},
    {"",   K_LEFT, {30, 190, 50, 50}, false, 0x40C0F0}, {"",  K_RIGHT, {130, 190, 50, 50}, false, 0x40C0F0},
    {"A", K_A, {815, 170, 56, 56}, true, 0xE04848},     {"B", K_B, {765, 220, 56, 56}, true, 0x48B848},
    {"C", K_C, {765, 120, 56, 56}, true, 0x4878E0},     {"D", K_D, {715, 170, 56, 56}, true, 0xE0C030},
    {"RUN", K_RUN, {360, 490, 90, 32}, false, 0xC0C0C8}, {"MENU", K_MENU, {450, 490, 90, 32}, false, 0xC0C0C8},
};

static uint32_t keys_kbd, keys_mouse, keys_pad;
static float joy_kx, joy_ky, joy_px, joy_py;    // -1..1
static bool joy_drag; static float joy_mx, joy_my;
static const SDL_Point JOY_C = {105, 410}; static const int JOY_R = 52;

static uint32_t key_mask(SDL_Scancode s)
{
    switch (s) {
    case SDL_SCANCODE_UP: return K_UP;      case SDL_SCANCODE_DOWN: return K_DOWN;
    case SDL_SCANCODE_LEFT: return K_LEFT;  case SDL_SCANCODE_RIGHT: return K_RIGHT;
    case SDL_SCANCODE_X: return K_A;        case SDL_SCANCODE_Z: return K_B;
    case SDL_SCANCODE_S: return K_C;        case SDL_SCANCODE_D: return K_D;
    case SDL_SCANCODE_Q: return K_L1;       case SDL_SCANCODE_E: return K_R1;
    case SDL_SCANCODE_RETURN: return K_RUN; case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_TAB: return K_MENU;
    default: return 0;
    }
}

// ---------------------------------------------------------------------------
// Audio : lit l'anneau partage ecrit par le modele I2S de QEMU (/dev/shm/aka-audio)
// ---------------------------------------------------------------------------
struct AudioShared {
    uint32_t magic, rate, channels, ring_frames, write_pos, pad[11];
    int16_t ring[16384];
};
static const AudioShared *aud = nullptr;
static uint32_t aud_rp = 0; static bool aud_sync = false, aud_mute = false;
static SDL_AudioDeviceID aud_dev = 0;
struct Press { uint32_t mask, t0, t1; };
static std::vector<Press> presses;   // --press masque:debut_ms:duree_ms (scripts de test)
static std::string audpath_s;
static const char *audpath = "";

static FILE *wavf = nullptr;
static void audio_cb(void *, Uint8 *out, int len)
{
    int16_t *o = (int16_t *)out; int n = len / 2;
    if (!aud || aud->magic != 0x41414B41u || aud_mute) { memset(out, 0, len); return; }
    const uint32_t N = aud->ring_frames ? aud->ring_frames : 16384, target = 2048;
    uint32_t wp = __atomic_load_n(&aud->write_pos, __ATOMIC_ACQUIRE);
    uint32_t avail = wp - aud_rp;
    if (!aud_sync || avail > N / 2) { aud_rp = wp - target; avail = target; aud_sync = true; }   // (re)synchronise
    static int16_t last = 0;
    for (int i = 0; i < n; i++) {
        if (avail > 0) { last = aud->ring[aud_rp % N]; aud_rp++; avail--; }
        else { last = (int16_t)(last * 0.98f); }                                              // sous-alimentation : fondu doux
        o[i] = last;
    }
    if (wavf) fwrite(out, 1, len, wavf);
    if (getenv("AKA_AUDIO_DEBUG")) {
        static int c = 0; static int mx = 0;
        for (int i = 0; i < n; i++) mx = std::max(mx, (int)std::abs((int)o[i]));
        if (++c % 86 == 0) { fprintf(stderr, "[aud] wp=%u rp=%u avail=%u max=%d\n", wp, aud_rp, wp - aud_rp, mx); mx = 0; }
    }
    // trop de retard accumule : on rattrape en sautant (evite la latence qui derive)
    if (avail > target * 3) aud_rp = wp - target;
}

static void audio_open_map()
{
    if (aud) return;
    aud = (const AudioShared *)plat::map_file(audpath, sizeof(AudioShared), false);
}

static void audio_init()
{
    if (!(SDL_WasInit(SDL_INIT_AUDIO))) SDL_InitSubSystem(SDL_INIT_AUDIO);
    SDL_AudioSpec want; SDL_zero(want);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512; want.callback = audio_cb;
    aud_dev = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (aud_dev) SDL_PauseAudioDevice(aud_dev, 0);
}

// ---------------------------------------------------------------------------
// Pilotage de QEMU : prepare la flash (comme l'AKA, depuis la table de
// partitions) et la carte SD, lance le processus et le surveille.
// ---------------------------------------------------------------------------
struct Config {
    std::string qemu = "qemu-system-xtensa", biosDir, partitions, bootloader, launcher, game;
    std::string flash = "aka-flash.bin", sdDir, sdImg;
    int launcherSlot = 1; bool bootGame = false, fresh = false, noQemu = false, singleThread = true;
    int flashMB = 8; std::string extra;
};
static Config cfg;
static plat::proc_t qpid = plat::proc_none();
static std::string status_msg;               // affiche a la place de l'ecran si QEMU ne tourne pas

static bool file_exists(const std::string &p) { return plat::file_exists(p); }

static bool prepare_flash(std::string &err)
{
    if (!cfg.fresh && file_exists(cfg.flash)) return true;     // flash persistante (jeux installes)
    flashimg::Options o;
    o.partitions = cfg.partitions; o.bootloader = cfg.bootloader; o.launcher = cfg.launcher; o.game = cfg.game;
    o.launcherSlot = cfg.launcherSlot; o.bootGame = cfg.bootGame; o.flashMB = cfg.flashMB;
    std::vector<uint8_t> img;
    if (cfg.partitions.empty() || cfg.bootloader.empty()) { err = "--partitions et --bootloader requis pour creer la flash"; return false; }
    if (!flashimg::build(o, img, err)) return false;
    FILE *f = fopen(cfg.flash.c_str(), "wb");
    if (!f) { err = "ecriture impossible: " + cfg.flash; return false; }
    fwrite(img.data(), 1, img.size(), f); fclose(f);
    return true;
}

static void stop_qemu()
{
    plat::kill_wait(qpid);
}

static bool start_qemu(const char *fbpath, const char *inpath)
{
    stop_qemu(); status_msg.clear();
    std::string err;
    if (!prepare_flash(err)) { status_msg = err; return false; }
    plat::remove_shared(fbpath); plat::remove_shared(audpath); aud = nullptr; aud_sync = false;
    std::vector<std::string> a = {cfg.qemu, "-nographic"};
    if (cfg.singleThread) { a.push_back("-accel"); a.push_back("tcg,thread=single"); }
    for (const char *x : {"-machine", "esp32s3", "-m", "8M"}) a.push_back(x);
    a.push_back("-drive"); a.push_back("file=" + cfg.flash + ",if=mtd,format=raw");
    if (!cfg.sdImg.empty()) { a.push_back("-drive"); a.push_back("file=" + cfg.sdImg + ",if=sd,format=raw"); }
    else if (!cfg.sdDir.empty()) { a.push_back("-drive"); a.push_back("file=fat:rw:" + cfg.sdDir + ",if=sd,format=raw"); }
    for (const std::string &x : {std::string("driver=ssi_psram,property=is_octal,value=true"),
         std::string("driver=misc.aka.lcdcam,property=fb-path,value=") + fbpath,
         std::string("driver=aka.expander,property=input-path,value=") + inpath,
         std::string("driver=misc.aka.sens,property=input-path,value=") + inpath,
         std::string("driver=misc.aka.i2s,property=audio-path,value=") + audpath}) {
        a.push_back("-global"); a.push_back(x);
    }
    if (!cfg.biosDir.empty()) { a.push_back("-L"); a.push_back(cfg.biosDir); }
    qpid = plat::spawn(a);
    return plat::proc_valid(qpid);
}

static void poll_qemu()
{
    if (plat::exited(qpid)) status_msg = "QEMU s'est arrete ou est introuvable (--qemu) - F5 : relancer";
}

static void read_cfg_file(const char *path, std::vector<std::string> &args)
{
    FILE *f = fopen(path, "r"); if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string l = line; while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.empty() || l[0] == '#') continue;
        size_t e = l.find('='); if (e == std::string::npos) continue;
        args.push_back("--" + l.substr(0, e)); args.push_back(l.substr(e + 1));
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    static std::string d_fb = plat::shared_dir() + "fb", d_in = plat::shared_dir() + "input";
    audpath_s = plat::shared_dir() + "audio"; audpath = audpath_s.c_str();
    const char *fbpath = d_fb.c_str(), *inpath = d_in.c_str(), *shot = nullptr;
    uint32_t demo_keys = 0; float demo_jx = 0, demo_jy = 0; int shot_after = 5; uint32_t shot_ms = 100;
    std::vector<std::string> args;
    read_cfg_file("aka-emu.cfg", args);                       // valeurs par defaut (cle=valeur)
    for (int i = 1; i < argc; i++) args.push_back(argv[i]);
    static std::string s_fb, s_in, s_shot;
    for (size_t i = 0; i < args.size(); i++) {
        const std::string &a = args[i];
        auto nx = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (a == "--fb") { s_fb = nx(); fbpath = s_fb.c_str(); }
        else if (a == "--input") { s_in = nx(); inpath = s_in.c_str(); }
        else if (a == "--screenshot") { s_shot = nx(); shot = s_shot.c_str(); }
        else if (a == "--demo-keys") demo_keys = strtoul(nx().c_str(), 0, 0);
        else if (a == "--demo-joy") { demo_jx = atof(nx().c_str()); demo_jy = atof(nx().c_str()); }
        else if (a == "--qemu") cfg.qemu = nx();
        else if (a == "--bios") cfg.biosDir = nx();
        else if (a == "--partitions") cfg.partitions = nx();
        else if (a == "--bootloader") cfg.bootloader = nx();
        else if (a == "--launcher") cfg.launcher = nx();
        else if (a == "--game") cfg.game = nx();
        else if (a == "--flash") cfg.flash = nx();
        else if (a == "--sd") cfg.sdDir = nx();
        else if (a == "--sd-img") cfg.sdImg = nx();
        else if (a == "--launcher-slot") cfg.launcherSlot = atoi(nx().c_str()) ? 1 : 0;
        else if (a == "--boot-game") cfg.bootGame = true;
        else if (a == "--fresh") cfg.fresh = true;
        else if (a == "--flash-mb") cfg.flashMB = atoi(nx().c_str());
        else if (a == "--no-qemu") cfg.noQemu = true;
        else if (a == "--audio") { static std::string s_au; s_au = nx(); audpath = s_au.c_str(); }
        else if (a == "--mute") aud_mute = true;
        else if (a == "--press") { unsigned m, t, d; if (sscanf(nx().c_str(), "%i:%u:%u", &m, &t, &d) == 3) presses.push_back({m, t, t + d}); }
        else if (a == "--record-raw") wavf = fopen(nx().c_str(), "wb");   // PCM S16 mono 44100 brut (tests)
        else if (a == "--shot-delay") shot_ms = (uint32_t)atoi(nx().c_str());
        else if (a == "--multi-thread") cfg.singleThread = false;
        else {
            printf("usage: %s [--qemu bin] [--bios dir] --partitions partitions.bin --bootloader bootloader.bin\n"
                   "          [--launcher launcher.bin] [--game firmware.bin] [--launcher-slot 0|1] [--boot-game]\n"
                   "          [--flash flash.bin] [--fresh] [--sd dossier | --sd-img carte.img] [--no-qemu]\n"
                   "          [--fb f] [--input f] [--screenshot out.bmp] [--demo-keys mask] [--demo-joy x y]\n"
                   "Touches : F5 relancer, F4 quitter, F6 reinitialiser la flash, F7 son on/off.\n"
                   "Options aussi lisibles dans aka-emu.cfg (une ligne cle=valeur par option).\n", argv[0]);
            return 1;
        }
    }
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO);
    SDL_Window *win = SDL_CreateWindow("Gamebuino AKA emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       LW, LH, SDL_WINDOW_RESIZABLE);
    R = SDL_CreateRenderer(win, -1, shot ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(R, LW, LH);
    // MADCTL.BGR (flag b2) : le panneau inverse rouge et bleu
    SDL_Texture *tex_rgb = SDL_CreateTexture(R, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, FB_MAX, FB_MAX);
    SDL_Texture *tex_bgr = SDL_CreateTexture(R, SDL_PIXELFORMAT_BGR565, SDL_TEXTUREACCESS_STREAMING, FB_MAX, FB_MAX);

    // zone d'entree partagee
    InputShared *in = (InputShared *)plat::map_file(inpath, sizeof(InputShared), true);
    static InputShared local; if (!in) in = &local;
    in->magic = INPUT_MAGIC; in->battery_mv = 3900; // tension de l'ADC batterie (pont diviseur non modelise)

    if (!shot) audio_init();
    if (!cfg.noQemu) start_qemu(fbpath, inpath);

    SDL_GameController *pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }

    const uint8_t *fbmap = nullptr; size_t fbsz = FB_HDR + FB_MAX * FB_MAX * 2;
    uint32_t last_frame = 0, frames_seen = 0, fps_t0 = SDL_GetTicks(), fps = 0;
    bool run = true; uint32_t t_start = SDL_GetTicks();

    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) run = false;
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                bool d = e.type == SDL_KEYDOWN;
                if (d && e.key.keysym.scancode == SDL_SCANCODE_F4) run = false;
                if (d && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_F7) aud_mute = !aud_mute;
                if (d && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_F5 && !cfg.noQemu) start_qemu(fbpath, inpath);
                if (d && !e.key.repeat && e.key.keysym.scancode == SDL_SCANCODE_F6 && !cfg.noQemu) { cfg.fresh = true; start_qemu(fbpath, inpath); cfg.fresh = false; }
                uint32_t m = key_mask(e.key.keysym.scancode);
                if (m) keys_kbd = d ? (keys_kbd | m) : (keys_kbd & ~m);
                float v = d ? 1.f : 0.f;      // joystick analogique au clavier : IJKL
                switch (e.key.keysym.scancode) {
                case SDL_SCANCODE_J: joy_kx = d ? -1 : (joy_kx < 0 ? 0 : joy_kx); break;
                case SDL_SCANCODE_L: joy_kx = d ? 1 : (joy_kx > 0 ? 0 : joy_kx); break;
                case SDL_SCANCODE_I: joy_ky = d ? -1 : (joy_ky < 0 ? 0 : joy_ky); break;
                case SDL_SCANCODE_K: joy_ky = d ? 1 : (joy_ky > 0 ? 0 : joy_ky); break;
                default: (void)v; break;
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION) {
                float fx, fy; SDL_RenderWindowToLogical(R, e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x,
                                                        e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y, &fx, &fy);
                bool down = e.type == SDL_MOUSEBUTTONUP ? false :
                            (e.type == SDL_MOUSEBUTTONDOWN ? true : (e.motion.state & SDL_BUTTON_LMASK) != 0);
                keys_mouse = 0;
                if (down) {
                    for (const Btn &b : BT) {
                        SDL_Point p = {(int)fx, (int)fy};
                        if (SDL_PointInRect(&p, &b.r)) keys_mouse |= b.mask;
                    }
                }
                if (e.type == SDL_MOUSEBUTTONDOWN && std::hypot(fx - JOY_C.x, fy - JOY_C.y) <= JOY_R + 10) joy_drag = true;
                if (!down) joy_drag = false;
                if (joy_drag) {
                    joy_mx = BOUND_F((fx - JOY_C.x) / JOY_R); joy_my = BOUND_F((fy - JOY_C.y) / JOY_R);
                } else joy_mx = joy_my = 0;
            }
        }
        poll_qemu(); audio_open_map();
        { static std::string shown; if (shown != status_msg) { shown = status_msg;
            SDL_SetWindowTitle(win, shown.empty() ? "Gamebuino AKA emulator" : ("Gamebuino AKA emulator - " + shown).c_str());
            if (!shown.empty()) fprintf(stderr, "%s\n", shown.c_str()); } }
        // manette
        keys_pad = 0; joy_px = joy_py = 0;
        if (pad) {
            auto B = [&](SDL_GameControllerButton b, uint32_t m) { if (SDL_GameControllerGetButton(pad, b)) keys_pad |= m; };
            B(SDL_CONTROLLER_BUTTON_DPAD_UP, K_UP); B(SDL_CONTROLLER_BUTTON_DPAD_DOWN, K_DOWN);
            B(SDL_CONTROLLER_BUTTON_DPAD_LEFT, K_LEFT); B(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, K_RIGHT);
            B(SDL_CONTROLLER_BUTTON_B, K_A); B(SDL_CONTROLLER_BUTTON_A, K_B);
            B(SDL_CONTROLLER_BUTTON_Y, K_C); B(SDL_CONTROLLER_BUTTON_X, K_D);
            B(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, K_L1); B(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, K_R1);
            B(SDL_CONTROLLER_BUTTON_START, K_RUN); B(SDL_CONTROLLER_BUTTON_BACK, K_MENU);
            joy_px = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.f;
            joy_py = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.f;
            if (std::fabs(joy_px) < .1f) joy_px = 0; if (std::fabs(joy_py) < .1f) joy_py = 0;
        }
        uint32_t keys = keys_kbd | keys_mouse | keys_pad | demo_keys;
        for (const Press &p : presses) { uint32_t t = SDL_GetTicks() - t_start; if (t >= p.t0 && t < p.t1) keys |= p.mask; }
        float jx = BOUND_F(joy_kx + joy_mx + joy_px + demo_jx), jy = BOUND_F(joy_ky + joy_my + joy_py + demo_jy);

        // publier l'etat (l'axe Y de l'ecran est inverse : haut = tension haute)
        in->keys = keys;
        in->joyx_mv = JOY_MID_MV + (int)(jx * JOY_MID_MV);
        in->joyy_mv = JOY_MID_MV + (int)(-jy * JOY_MID_MV);

        // framebuffer partage (ouvert a la volee : QEMU peut demarrer apres nous)
        if (!fbmap) {
            fbmap = (const uint8_t *)plat::map_file(fbpath, fbsz, false);
        }
        uint32_t w = 320, h = 240, flags = 0, frame = 0;
        bool have = false;
        if (fbmap) {
            const uint32_t *hd = (const uint32_t *)fbmap;
            if (hd[0] == FB_MAGIC && hd[2] && hd[2] <= FB_MAX && hd[3] && hd[3] <= FB_MAX) {
                w = hd[2]; h = hd[3]; frame = hd[4]; flags = hd[5]; have = true;
            }
        }
        if (have && frame != last_frame) { frames_seen += frame - last_frame; last_frame = frame; }
        if (SDL_GetTicks() - fps_t0 >= 1000) { fps = frames_seen; frames_seen = 0; fps_t0 = SDL_GetTicks(); }

        // ---- dessin ----
        col(0x20232A); SDL_RenderClear(R);
        rbox({10, 8, LW - 20, LH - 16}, 40, 0x2E3340);     // coque
        rbox({225, 70, 450, 380}, 14, 0x0B0C10);             // cadre de l'ecran
        SDL_Rect scr = {240, 95, 420, 315};                  // 320x240 @ x1.3125
        if (have && (flags & FB_FLAG_ON)) {
            SDL_Texture *tex = (flags & 4) ? tex_bgr : tex_rgb;
            void *pix; int pitch;
            if (SDL_LockTexture(tex, nullptr, &pix, &pitch) == 0) {
                for (uint32_t y = 0; y < h; y++)
                    memcpy((uint8_t *)pix + y * pitch, fbmap + FB_HDR + y * w * 2, w * 2);
                SDL_UnlockTexture(tex);
            }
            SDL_Rect src = {0, 0, (int)w, (int)h};
            SDL_RenderCopy(R, tex, &src, &scr);
        } else {
            col(0x101418); SDL_RenderFillRect(R, &scr);
            text(have ? "LCD OFF" : "NO SIGNAL", scr.x + scr.w / 2, scr.y + scr.h / 2 - 7, 2, 0x50586A);
        }

        // croix directionnelle (fond)
        rbox({30, 140, 150, 150}, 20, 0x262A35);
        for (Btn &b : BT) {
            bool on = (keys & b.mask) != 0;
            uint32_t c = on ? b.on : 0x5A6070;
            if (b.round) {
                disc(b.r.x + b.r.w / 2, b.r.y + b.r.h / 2 + 2, b.r.w / 2, 0x15171D);
                disc(b.r.x + b.r.w / 2, b.r.y + b.r.h / 2, b.r.w / 2 - (on ? 3 : 0), c);
                text(b.label, b.r.x + b.r.w / 2, b.r.y + b.r.h / 2 - 7, 2, on ? 0x101010 : 0xDDE0E8);
            } else {
                rbox(b.r, 8, c);
                if (b.label[0]) text(b.label, b.r.x + b.r.w / 2, b.r.y + b.r.h / 2 - 7, 2, on ? 0x101010 : 0xDDE0E8);
            }
        }
        // fleches de la croix
        col(0xDDE0E8);
        auto tri = [&](int cx, int cy, int dx, int dy) {
            for (int i = 0; i < 9; i++) {
                int px = cx + dx * (4 - i) , py = cy + dy * (4 - i);
                SDL_RenderDrawLine(R, px - dy * i, py + dx * i, px + dy * i, py - dx * i);
            }
        };
        tri(105, 160, 0, -1); tri(105, 270, 0, 1); tri(60, 215, -1, 0); tri(150, 215, 1, 0);

        // joystick analogique
        disc(JOY_C.x, JOY_C.y, JOY_R + 8, 0x1B1E26);
        disc(JOY_C.x, JOY_C.y, JOY_R, 0x2A2E3A);
        col(0x3C4252); SDL_RenderDrawLine(R, JOY_C.x - JOY_R, JOY_C.y, JOY_C.x + JOY_R, JOY_C.y);
        SDL_RenderDrawLine(R, JOY_C.x, JOY_C.y - JOY_R, JOY_C.x, JOY_C.y + JOY_R);
        bool act = std::fabs(jx) > .05f || std::fabs(jy) > .05f;
        disc(JOY_C.x + (int)(jx * (JOY_R - 20)), JOY_C.y + (int)(jy * (JOY_R - 20)), 24, act ? 0x40C0F0 : 0x70788C);
        char buf[64];
        snprintf(buf, sizeof buf, "X=%d", in->joyx_mv); text(buf, JOY_C.x, JOY_C.y + 78, 2, 0x9AA2B4);
        snprintf(buf, sizeof buf, "Y=%d", in->joyy_mv); text(buf, JOY_C.x, JOY_C.y + 98, 2, 0x9AA2B4);
        text("MV", JOY_C.x, JOY_C.y - 78, 2, 0x6A7286);

        // bandeau d'etat : masque des touches + images/s
        snprintf(buf, sizeof buf, "KEYS=%04X", keys); text(buf, 760, 500, 2, 0x9AA2B4);
        snprintf(buf, sizeof buf, "FPS=%u", fps); text(buf, 760, 520, 2, have ? 0x70D070 : 0xD07070);
        SDL_RenderPresent(R);

        if (shot && SDL_GetTicks() - t_start > shot_ms) {
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, LW, LH, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(R, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
            SDL_SaveBMP(s, shot); SDL_FreeSurface(s); run = false;
        }
        if (!shot) SDL_Delay(2);
    }
    stop_qemu();
    SDL_Quit();
    return 0;
}
