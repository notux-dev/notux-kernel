#include <stdint.h>
#define MB2_MAGIC 0x36d76289u
uint64_t mb2_acpi_rsdp;
uint8_t mb2_rsdp_copy[36];
uint32_t mb2_rsdp_len;
static uint8_t mb1_buf[0x10000] __attribute__((aligned(4096)));
static void put32(uint32_t off, uint32_t v) { *(uint32_t *)(mb1_buf + off) = v; }
static void put64(uint32_t off, uint64_t v) { *(uint64_t *)(mb1_buf + off) = v; }
int mb2_is(uint64_t magic) { return magic == MB2_MAGIC; }
uint64_t mb2_convert(uint64_t info)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)info;
    uint32_t total = *(const uint32_t *)p;
    uint32_t flags = 0, nmod = 0, nmmap = 0, strpos = 0x3000;
    const uint32_t MMAP_AT = 0x100, MODS_AT = 0x2000, STR_AT = 0x3000;
    for (uint32_t i = 0; i < sizeof mb1_buf; i++) mb1_buf[i] = 0;
    for (uint32_t off = 8; off + 8 <= total;) {
        const uint8_t *t = p + off;
        uint32_t type = *(const uint32_t *)t, size = *(const uint32_t *)(t + 4);
        if (type == 0 || size < 8) break;
        if (type == 4) {
            put32(4, *(const uint32_t *)(t + 8));
            put32(8, *(const uint32_t *)(t + 12));
            flags |= 1;
        } else if (type == 3 && nmod < 64) {
            uint32_t m = MODS_AT + nmod * 16;
            put32(m, *(const uint32_t *)(t + 8));
            put32(m + 4, *(const uint32_t *)(t + 12));
            uint32_t slen = size - 16;
            if (strpos + slen + 1 < sizeof mb1_buf) {
                for (uint32_t k = 0; k < slen; k++) mb1_buf[strpos + k] = t[16 + k];
                put32(m + 8, (uint32_t)(uintptr_t)(mb1_buf + strpos));
                strpos += slen + 1;
            }
            nmod++;
        } else if (type == 6) {
            uint32_t esz = *(const uint32_t *)(t + 8);
            for (uint32_t e = 16; e + esz <= size && nmmap < 128; e += esz, nmmap++) {
                uint32_t o = MMAP_AT + nmmap * 24;
                put32(o, 20);
                put64(o + 4, *(const uint64_t *)(t + e));
                put64(o + 12, *(const uint64_t *)(t + e + 8));
                put32(o + 20, *(const uint32_t *)(t + e + 16));
            }
        } else if (type == 8) {
            put64(88, *(const uint64_t *)(t + 8));
            put32(96, *(const uint32_t *)(t + 16));
            put32(100, *(const uint32_t *)(t + 20));
            put32(104, *(const uint32_t *)(t + 24));
            mb1_buf[108] = t[28];
            mb1_buf[109] = t[29];
            flags |= 1u << 12;
        } else if ((type == 14 || type == 15) && size - 8 <= sizeof mb2_rsdp_copy) {
            for (uint32_t k = 0; k < size - 8; k++) mb2_rsdp_copy[k] = t[8 + k];
            mb2_rsdp_len = size - 8;
            mb2_acpi_rsdp = (uint64_t)(uintptr_t)mb2_rsdp_copy;
        }
        off += (size + 7) & ~7u;
    }
    if (nmod) { put32(20, nmod); put32(24, (uint32_t)(uintptr_t)(mb1_buf + MODS_AT)); flags |= 1u << 3; }
    if (nmmap) { put32(44, nmmap * 24); put32(48, (uint32_t)(uintptr_t)(mb1_buf + MMAP_AT)); flags |= 1u << 6; }
    (void)STR_AT;
    put32(0, flags);
    return (uint64_t)(uintptr_t)mb1_buf;
}
