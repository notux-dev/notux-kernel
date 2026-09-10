#include "gdt.h"
#include "idt.h"
#include "vga.h"
#include "vbe.h"
#include "serial.h"
#include "font.h"
#include "console.h"
#include "pic.h"
#include "syscall.h"
#include "vfs.h"
#include "process.h"
#include "pmm.h"
#include "paging.h"
#include "vmm.h"
#include "ahci/ahci.h"
#include "panic.h"
#include "logo.h"
#include "gfx.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "Xjail/xv6/xv6fs_api.h"
#include "IGPU/igpu.h"
#ifndef _UINT_DEFINED
#define _UINT_DEFINED
typedef unsigned int uint;
#endif
#define MULTIBOOT_MAGIC_EXPECTED 0x2BADB002ULL
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
static inline void outb(uint16_t port, uint8_t value) {
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static void serwrite_hex64(uint64_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int shift = 60 - i * 4;
        buf[2 + i] = hex[(value >> shift) & 0xF];
    }
    buf[18] = '\0';
    serwrite(buf);
}
static void serwrite_hex32(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        int shift = 28 - i * 4;
        buf[2 + i] = hex[(value >> shift) & 0xF];
    }
    buf[10] = '\0';
    serwrite(buf);
}
void init_timer(uint32_t freq) {
    if (freq == 0) return;
    uint32_t divisor = 1193180 / freq;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}
typedef struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
} __attribute__((packed)) multiboot_info_t;
typedef struct multiboot_mod_list {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} __attribute__((packed)) multiboot_mod_list_t;
typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;
extern void xv6fs_full_init(ahci_device_t *dev);
extern int xv6fs_bridge_read_file(const char *path, uint8_t *out, unsigned int maxlen);
int load_elf_into_process(process_t *proc, const uint8_t *file, uint64_t file_size) {
    serwrite("ELF: loader entered\n");
    if (!proc) {
        serwrite("ELF ERROR: proc == NULL\n");
        return -1;
    }
    if (!proc->pml4) {
        serwrite("ELF ERROR: proc->pml4 == NULL\n");
        return -1;
    }
    if (!file) {
        serwrite("ELF ERROR: file == NULL\n");
        return -1;
    }
    serwrite("ELF file size = ");
    serwrite_hex64(file_size);
    serwrite("\n");
    if (file_size < sizeof(elf64_ehdr_t)) {
        serwrite("ELF ERROR: file too small\n");
        return -1;
    }
    elf64_ehdr_t *ehdr = (elf64_ehdr_t *)file;
    uint32_t magic = ((uint32_t)ehdr->e_ident[0] << 24) |
                     ((uint32_t)ehdr->e_ident[1] << 16) |
                     ((uint32_t)ehdr->e_ident[2] << 8)  |
                     ((uint32_t)ehdr->e_ident[3]);
    serwrite("ELF magic: ");
    serwrite_hex32(magic);
    serwrite("\n");
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L'  || ehdr->e_ident[3] != 'F') {
        serwrite("ELF ERROR: bad magic\n");
        return -1;
    }
    serwrite("ELF: magic OK\n");
    if (ehdr->e_ident[4] != 2) {
        serwrite("ELF ERROR: not ELF64\n");
        return -1;
    }
    serwrite("ELF: ELF64 OK\n");
    if (ehdr->e_ident[5] != 1) {
        serwrite("ELF ERROR: not little endian\n");
        return -1;
    }
    serwrite("ELF machine = ");
    serwrite_hex64(ehdr->e_machine);
    serwrite("\n");
    if (ehdr->e_machine != 62) {
        serwrite("ELF ERROR: wrong machine\n");
        return -1;
    }
    if (ehdr->e_type != 2 && ehdr->e_type != 3) {
        serwrite("ELF ERROR: unsupported type\n");
        return -1;
    }
    serwrite("ELF entry = ");
    serwrite_hex64(ehdr->e_entry);
    serwrite("\n");
    if (ehdr->e_phnum == 0) {
        serwrite("ELF ERROR: no program headers\n");
        return -1;
    }
    if (ehdr->e_phentsize != sizeof(elf64_phdr_t)) {
        serwrite("ELF ERROR: bad e_phentsize\n");
        return -1;
    }
    uint64_t ph_size = (uint64_t)ehdr->e_phnum * (uint64_t)ehdr->e_phentsize;
    if (ehdr->e_phoff > file_size) {
        serwrite("ELF ERROR: e_phoff outside file\n");
        return -1;
    }
    if (ph_size > file_size - ehdr->e_phoff) {
        serwrite("ELF ERROR: phdr outside file\n");
        return -1;
    }
    elf64_phdr_t *phdr = (elf64_phdr_t *)(file + ehdr->e_phoff);
    int load_segments = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        elf64_phdr_t *ph = &phdr[i];
        if (ph->p_type != 1) continue;
        load_segments++;
        if (ph->p_filesz > ph->p_memsz) {
            serwrite("ELF ERROR: filesz > memsz\n");
            return -1;
        }
        if (ph->p_offset > file_size) {
            serwrite("ELF ERROR: bad p_offset\n");
            return -1;
        }
        if (ph->p_filesz > file_size - ph->p_offset) {
            serwrite("ELF ERROR: segment outside file\n");
            return -1;
        }
        uint64_t seg_end = ph->p_vaddr + ph->p_memsz;
        if (seg_end < ph->p_vaddr) {
            serwrite("ELF ERROR: address overflow\n");
            return -1;
        }
        if (ph->p_memsz == 0) continue;
        uint64_t page_start = ph->p_vaddr & ~0xFFFULL;
        if (seg_end > 0xFFFFFFFFFFFFF000ULL) {
            serwrite("ELF ERROR: segment too high\n");
            return -1;
        }
        uint64_t page_end = (seg_end + 0xFFFULL) & ~0xFFFULL;
        if (page_end < seg_end) {
            serwrite("ELF ERROR: page overflow\n");
            return -1;
        }
        uint64_t flags = PAGE_USER;
        if (ph->p_flags & 2) flags |= PAGE_WRITE;
        for (uint64_t page = page_start; page < page_end; page += PAGE_SIZE) {
            uint8_t *phys = (uint8_t *)pmm_alloc_page();
            if (!phys) {
                serwrite("ELF ERROR: pmm_alloc_page failed\n");
                return -1;
            }
            for (uint64_t i = 0; i < PAGE_SIZE; i++) phys[i] = 0;
            vmm_map_page(proc->pml4, page, (uint64_t)phys, flags);
            uint64_t copy_start = page;
            if (copy_start < ph->p_vaddr) copy_start = ph->p_vaddr;
            uint64_t file_end = ph->p_vaddr + ph->p_filesz;
            uint64_t copy_end = page + PAGE_SIZE;
            if (copy_end > file_end) copy_end = file_end;
            if (copy_start >= copy_end) continue;
            uint64_t len = copy_end - copy_start;
            uint64_t src_off = ph->p_offset + (copy_start - ph->p_vaddr);
            if (src_off > file_size) {
                serwrite("ELF ERROR: src_off outside\n");
                return -1;
            }
            if (len > file_size - src_off) {
                serwrite("ELF ERROR: copy outside\n");
                return -1;
            }
            uint8_t *dst = phys + (copy_start - page);
            const uint8_t *src = file + src_off;
            for (uint64_t j = 0; j < len; j++) dst[j] = src[j];
        }
    }
    if (load_segments == 0) {
        serwrite("ELF ERROR: no PT_LOAD\n");
        return -1;
    }
    serwrite("ELF: all PT_LOAD loaded\n");
    return 0;
}
void kprint_logo(void) {
    if (console_get_col() != 0) {
        kprint("\n", 0x00000000);
    }
    int col = console_get_col();
    int row = console_get_row();
    int start_x = col * CHAR_W;
    int start_y = row * CHAR_H;
    for (uint32_t y = 0; y < image_height; y++) {
        for (uint32_t x = 0; x < image_width; x++) {
            uint32_t color = image_data[y * image_width + x];
            putpixel(start_x + x, start_y + y, color);
        }
    }
    console_set_pos(0, row + 3);
}
void create_default_dirs(void) {
    struct inode *ip;
    ip = xv6fs_create("/etc", 1, 0, 0);
    if (ip) {
        iunlock(ip);
        iput(ip);
    }
    ip = xv6fs_create("/root", 1, 0, 0);
    if (ip) {
        iunlock(ip);
        iput(ip);
    }
}
extern uint8_t _kernel_end[];
void kernel(uint64_t magic, uint64_t mb_info_addr) {
    serinit();
    serwrite("KERNEL START\n");
    serwrite("Multiboot magic = ");
    serwrite_hex64(magic);
    serwrite("\n");
    if (magic != MULTIBOOT_MAGIC_EXPECTED) {
        panic("Invalid Multiboot magic number!");
    }
    multiboot_info_t *mb_info = (multiboot_info_t *)mb_info_addr;
    if (!mb_info) {
        panic("Multiboot info is NULL");
    }
    serwrite("Multiboot flags = ");
    serwrite_hex32(mb_info->flags);
    serwrite("\n");
    serwrite("Multiboot mods_count = ");
    serwrite_hex32(mb_info->mods_count);
    serwrite("\n");
    multiboot_mod_list_t *mods = 0;
    uint32_t mods_count = 0;
    if ((mb_info->flags & (1 << 3)) && mb_info->mods_count > 0) {
        mods_count = mb_info->mods_count;
        mods = (multiboot_mod_list_t *)(uint64_t)mb_info->mods_addr;
    }
    uint64_t kernel_end_addr = (uint64_t)_kernel_end;
    uint32_t kernel_reserved_pages = (uint32_t)((kernel_end_addr + 4095) / 4096);
    pmm_init(kernel_reserved_pages);
    serwrite("PMMI\n");
    pmm_reserve_region(mb_info_addr, mb_info_addr + sizeof(multiboot_info_t));
    if (mods && mods_count > 0) {
        uint64_t mods_start = (uint64_t)mods;
        uint64_t mods_end = mods_start + ((uint64_t)mods_count * sizeof(multiboot_mod_list_t));
        pmm_reserve_region(mods_start, mods_end);
        for (uint32_t i = 0; i < mods_count; i++) {
            uint64_t start = (uint64_t)mods[i].mod_start;
            uint64_t end = (uint64_t)mods[i].mod_end;
            serwrite("Reserve module ");
            serwrite_hex64(i);
            serwrite(": ");
            serwrite_hex64(start);
            serwrite(" - ");
            serwrite_hex64(end);
            serwrite("\n");
            if (end > start) {
                pmm_reserve_region(start, end);
            }
        }
    }
    if (mods && mods_count > 0) {
        uint8_t *test_elf = (uint8_t *)(uint64_t)mods[0].mod_start;
        serwrite("EARLY MODULE0: ");
        for (int i = 0; i < 4; i++) {
            serwrite_hex64(test_elf[i]);
            if (i != 3) serwrite(" ");
        }
        serwrite("\n");
    }
    paging_init();
    paging_enable();
    serwrite("PON\n");
    vmm_init();
    serwrite("VMMI\n");
    iinit();
    picremap();
    syscall_init();
    vbemode(800, 600, 32);
    vbefill(0x00FFFFFF);
    cinit();
    igpu_pci_init();
    kprint("64-bit Kernel\n", 0x00000000);
    int disks = ahci_init();
    if (disks <= 0) {
        kprint("AHCI: No SATA disks found!\n", 0x00FF0000);
    } else {
        kprint("AHCI: Disk found, mounting xv6fs...\n", 0x00000000);
        ahci_device_t *disk = ahci_get_device(0);
        if (disk) {
            xv6fs_full_init(disk);
            uint8_t buf[512];
            int n = xv6fs_bridge_read_file("/README", buf, sizeof(buf) - 1);
            if (n >= 0) {
                buf[n] = 0;
                kprint("xv6fs: /README content:\n", 0x00000000);
                kprint((char *)buf, 0x00000000);
                kprint("\n", 0x00000000);
            } else {
                kprint("xv6fs: Failed to read /README from disk\n", 0x00FF0000);
            }
        }
    }
    if (!mods || mods_count == 0) {
        panic("No init ELF module found!");
    }
    multiboot_mod_list_t *mod = &mods[0];
    uint8_t *elf_image = (uint8_t *)(uint64_t)mod->mod_start;
    uint64_t module_start = (uint64_t)mod->mod_start;
    uint64_t module_end = (uint64_t)mod->mod_end;
    if (module_end <= module_start) {
        panic("Invalid init ELF module range");
    }
    uint64_t module_size = module_end - module_start;
    serwrite("Module 0 start = ");
    serwrite_hex64(module_start);
    serwrite("\n");
    serwrite("Module 0 end   = ");
    serwrite_hex64(module_end);
    serwrite("\n");
    serwrite("Module 0 size  = ");
    serwrite_hex64(module_size);
    serwrite("\n");
    serwrite("Module ELF bytes: ");
    for (int i = 0; i < 4; i++) {
        serwrite_hex64(elf_image[i]);
        if (i != 3) serwrite(" ");
    }
    serwrite("\n");
    kprint("Found binary module\n", 0x00000000);
    scheduler_init();
    kprint("Scheduler initialized\n", 0x00000000);
    int init_pid = create_process(0);
    if (init_pid < 0) {
        panic("Cannot create init process");
    }
    process_t *init_proc = get_process_by_pid((uint32_t)init_pid);
    if (!init_proc) {
        panic("Cannot find init process");
    }
    serwrite("init PID = ");
    serwrite_hex64(init_pid);
    serwrite("\n");
    if (load_elf_into_process(init_proc, elf_image, module_size) < 0) {
        panic("Cannot load init ELF");
    }
    elf64_ehdr_t *init_ehdr = (elf64_ehdr_t *)elf_image;
    serwrite("Setting entry = ");
    serwrite_hex64(init_ehdr->e_entry);
    serwrite("\n");
    set_process_entry(init_proc, init_ehdr->e_entry);
    kprint("ELF loaded\n", 0x00000000);
    kprint("Process created\n", 0x00000000);
    init_timer(100);
    kprint("Timer initialized\n", 0x00000000);
    kprint_logo();
    uint8_t current_mask = inb(0x21);
    serwrite("PIC1 Mask before STI: ");
    char hex_buf[3] = {
        "0123456789ABCDEF"[(current_mask >> 4) & 0xF],
        "0123456789ABCDEF"[current_mask & 0xF],
        '\n'
    };
    serwrite(hex_buf);
    serwrite("presti");
    asm volatile("sti");
    kprint("sti done :)\n", 0x00000000);
    for (;;) {
        asm volatile("hlt");
    }
}
