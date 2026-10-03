#include "flashimg.h"

#include <cstdio>
#include <cstring>

namespace flashimg {
namespace {

constexpr uint32_t kTableOffset = 0x8000;
constexpr uint8_t kTypeApp = 0x00, kTypeData = 0x01;
constexpr uint8_t kSubOta0 = 0x10, kSubOtaData = 0x00;   // data/ota = 0x00

bool readFile(const std::string& p, std::vector<uint8_t>& v)
{
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    v.resize(n > 0 ? (size_t)n : 0);
    bool ok = n <= 0 || fread(v.data(), 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok;
}

// zlib crc32 avec valeur initiale (comme zlib.crc32(data, init) de Python).
uint32_t crc32z(const uint8_t* d, size_t n, uint32_t init)
{
    uint32_t c = init ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return c ^ 0xFFFFFFFFu;
}

// ---- SHA-256 minimal (hash de fin d'image du bootloader) ----
struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64]; size_t blen = 0; uint64_t total = 0;
    static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const uint8_t* p)
    {
        static const uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | p[4*i+1] << 16 | p[4*i+2] << 8 | p[4*i+3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i-15], 7) ^ ror(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = ror(w[i-2], 17) ^ ror(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ror(e,6)^ror(e,11)^ror(e,25), ch = (e&f)^(~e&g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = ror(a,2)^ror(a,13)^ror(a,22), mj = (a&b)^(a&c)^(b&c);
            uint32_t t2 = S0 + mj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    void update(const uint8_t* p, size_t n)
    {
        total += n;
        while (n) {
            size_t k = 64 - blen < n ? 64 - blen : n;
            memcpy(buf + blen, p, k); blen += k; p += k; n -= k;
            if (blen == 64) { block(buf); blen = 0; }
        }
    }
    void final(uint8_t out[32])
    {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80; update(&pad, 1);
        uint8_t z = 0; while (blen != 56) update(&z, 1);
        uint8_t l[8]; for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
        update(l, 8);
        for (int i = 0; i < 8; i++) { out[4*i] = h[i] >> 24; out[4*i+1] = h[i] >> 16; out[4*i+2] = h[i] >> 8; out[4*i+3] = h[i]; }
    }
};

// Reecrit l'en-tete comme `esptool merge_bin --flash_mode dio --flash_freq 80m
// --flash_size NMB` (S3) et recalcule le SHA-256 de fin d'image s'il existe.
void patchBootloader(std::vector<uint8_t>& bl, int flashMB)
{
    if (bl.size() < 24 || bl[0] != 0xE9) return;
    uint8_t sizeCode = flashMB >= 16 ? 0x4 : flashMB >= 8 ? 0x3 : flashMB >= 4 ? 0x2 : flashMB >= 2 ? 0x1 : 0x0;
    bl[2] = 0x02;                       // DIO
    bl[3] = (uint8_t)(sizeCode << 4 | 0x0F);  // taille | 80 MHz (S3)
    if (bl[23] == 1 && bl.size() > 32) {
        Sha256 s; s.update(bl.data(), bl.size() - 32);
        s.final(bl.data() + bl.size() - 32);
    }
}

void putU32(uint8_t* p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }

}  // namespace

bool loadTable(const std::string& path, std::vector<Partition>& t, std::string& err)
{
    std::vector<uint8_t> raw;
    if (!readFile(path, raw) || raw.size() < 32 || raw[0] != 0xAA || raw[1] != 0x50) {
        err = "table de partitions invalide (partitions.bin attendu): " + path;
        return false;
    }
    t.clear();
    for (size_t i = 0; i + 32 <= raw.size() && i < 0xC00; i += 32) {
        const uint8_t* e = &raw[i];
        if (e[0] == 0xEB && e[1] == 0xEB) continue;       // entree MD5
        if (e[0] != 0xAA || e[1] != 0x50) break;
        Partition p;
        p.type = e[2]; p.subtype = e[3];
        p.offset = e[4] | e[5] << 8 | e[6] << 16 | (uint32_t)e[7] << 24;
        p.size = e[8] | e[9] << 8 | e[10] << 16 | (uint32_t)e[11] << 24;
        p.name.assign((const char*)e + 12, strnlen((const char*)e + 12, 16));
        t.push_back(p);
    }
    return !t.empty();
}

bool build(const Options& o, std::vector<uint8_t>& out, std::string& err)
{
    std::vector<Partition> table;
    if (!loadTable(o.partitions, table, err)) return false;

    std::vector<uint8_t> raw, bl;
    readFile(o.partitions, raw);
    if (!readFile(o.bootloader, bl)) { err = "bootloader introuvable: " + o.bootloader; return false; }
    patchBootloader(bl, o.flashMB);

    out.assign((size_t)o.flashMB * 1024 * 1024, 0xFF);
    if (bl.size() > kTableOffset) { err = "bootloader trop gros"; return false; }
    memcpy(out.data(), bl.data(), bl.size());
    if (raw.size() > 0x1000) raw.resize(0x1000);
    memcpy(out.data() + kTableOffset, raw.data(), raw.size());

    auto findApp = [&](int idx) -> const Partition* {
        for (const auto& p : table) if (p.type == kTypeApp && p.subtype == kSubOta0 + idx) return &p;
        return nullptr;
    };
    const Partition* slot[2] = {findApp(0), findApp(1)};
    const Partition* ldr = slot[o.launcherSlot ? 1 : 0];
    const Partition* gam = slot[o.launcherSlot ? 0 : 1];

    auto place = [&](const std::string& file, const Partition* p, const char* what) -> bool {
        if (file.empty()) return true;
        std::vector<uint8_t> d;
        if (!p) { err = std::string("pas de partition pour ") + what; return false; }
        if (!readFile(file, d)) { err = std::string("fichier introuvable: ") + file; return false; }
        if (d.size() > p->size) { err = std::string(what) + " ne tient pas dans " + p->name; return false; }
        if ((size_t)p->offset + d.size() > out.size()) { err = "depasse la flash"; return false; }
        memcpy(out.data() + p->offset, d.data(), d.size());
        return true;
    };
    if (!place(o.launcher, ldr, "le launcher")) return false;
    if (!place(o.game, gam, "le jeu")) return false;

    for (const auto& p : table) {
        if (p.type == kTypeData && p.subtype == kSubOtaData) {
            const Partition* want = o.bootGame ? gam : ldr;
            if (!want) break;
            // seq = index de l'OTA + 1 (slot = (seq-1) % n)
            uint32_t seq = (want->subtype - kSubOta0) + 1;
            uint8_t e[32]; memset(e, 0xFF, sizeof e);
            putU32(e, seq);
            putU32(e + 28, crc32z(e, 4, 0xFFFFFFFFu));
            memcpy(out.data() + p.offset, e, sizeof e);
            break;
        }
    }
    return true;
}

}  // namespace flashimg
