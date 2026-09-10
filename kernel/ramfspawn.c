#include "ramfs.h"
#include "console.h"
#include "process.h"
extern int load_elf_into_process(process_t *proc, const uint8_t *file, uint64_t file_size);
extern int create_process(uint64_t entry_point);
extern process_t *get_process_by_pid(uint32_t pid);
extern void set_process_entry(process_t *proc, uint64_t entry_point);
extern uint32_t ramfs_size(int fd);
int spawn_from_ramfs(const char *filename) {
    int fd = ramfs_open(filename);
    if (fd < 0) {
        kprint("[RAMFS] File not found: ", 0x00FF0000);
        kprint(filename, 0x00FF0000);
        kprint("\n", 0x00FF0000);
        return -1;
    }
    const uint8_t *elf_data = ramfs_data(fd);
    uint64_t size = ramfs_size(fd);
    int pid = create_process(0);
    if (pid < 0) {
        kprint("[RAMFS] Failed to create process: ", 0x00FF0000);
        kprint(filename, 0x00FF0000);
        kprint("\n", 0x00FF0000);
        return -1;
    }
    process_t *proc = get_process_by_pid((uint32_t)pid);
    if (!proc) {
        return -1;
    }
    if (load_elf_into_process(proc, elf_data, size) < 0) {
        kprint("[RAMFS] Failed to parse ELF: ", 0x00FF0000);
        kprint(filename, 0x00FF0000);
        kprint("\n", 0x00FF0000);
        return -1;
    }
    uint64_t entry_point = *(const uint64_t *)(elf_data + 24);
    set_process_entry(proc, entry_point);
    kprint("[RAMFS] Process spawned: ", 0x0000FF00);
    kprint(filename, 0x0000FF00);
    kprint("\n", 0x0000FF00);
    return 0;
}
