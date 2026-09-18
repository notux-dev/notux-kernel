const MultibootTag: type = extern struct {
    tag_type: u32,
    size: u32,
};

pub const MULTIBOOT_TAG_TYPE_MMAP: u32 = 6;

const MemoryMapEntry: type = extern struct {
    base_addr: u64,
    length: u64,
    entry_type: u32,
    reserved: u32,
};

pub const MemoryRegion: type = struct {
    base: u64,
    length: u64,
};

pub const MAX_REGIONS: usize = 32;

pub fn parseMemoryMap(mb_info_ptr: u64, out: *[MAX_REGIONS]MemoryRegion) usize {
    var count: usize = 0;

    const total_size_ptr: *const u32 = @ptrFromInt(mb_info_ptr);
    const total_size: u32 = total_size_ptr.*;

    var offset: u64 = 8;

    while (offset < total_size) {
        const tag: *const MultibootTag = @ptrFromInt(mb_info_ptr + offset);

        if (tag.tag_type == 0) break;
        if (tag.size == 0) break;
        if (tag.size < 8) break;

        if (tag.tag_type == MULTIBOOT_TAG_TYPE_MMAP) {
            const entry_size_ptr: *const u32 = @ptrFromInt(mb_info_ptr + offset + 8);
            const entry_size: u32 = entry_size_ptr.*;

            if (entry_size == 0) break;

            var entry_offset: u64 = offset + 16;
            const tag_end: u64 = offset + tag.size;

            while (entry_offset + @sizeOf(MemoryMapEntry) <= tag_end and count < MAX_REGIONS) {
                const entry: *const MemoryMapEntry = @ptrFromInt(mb_info_ptr + entry_offset);

                if (entry.entry_type == 1) {
                    out[count] = MemoryRegion{
                        .base = entry.base_addr,
                        .length = entry.length,
                    };
                    count += 1;
                }

                entry_offset += entry_size;
            }
        }

        const aligned_size: u64 = (tag.size + 7) & ~@as(u64, 7);
        offset += aligned_size;
    }

    return count;
}

pub const MULTIBOOT_TAG_TYPE_MODULE: u32 = 3;

pub const ModuleInfo: type = extern struct {
    start: u64,
    end: u32,
};

const ModuleTag: type = extern struct {
    tag_type: u32,
    size: u32,
    mod_start: u32,
    mod_end: u32,
};

pub fn findFirstModule(mb_info_ptr: u64) ?ModuleInfo {
    const total_size_ptr: *const u32 = @ptrFromInt(mb_info_ptr);
    const total_size: u32 = total_size_ptr.*;

    var offset: u64 = 8;

    while (offset < total_size) {
        const tag: *const MultibootTag = @ptrFromInt(mb_info_ptr + offset);

        if (tag.tag_type == 0) break;
        if (tag.size == 0) break;
        if (tag.size < 8) break;

        if (tag.tag_type == MULTIBOOT_TAG_TYPE_MODULE) {
            const mod: *const ModuleTag = @ptrFromInt(mb_info_ptr + offset);
            return ModuleInfo{
                .start = mod.mod_start,
                .end = mod.mod_end,
            };
        }

        const aligned_size: u64 = (tag.size + 7) & ~@as(u64, 7);
        offset += aligned_size;
    }

    return null;
}

// добавь в multiboot.zig, для диагностики
pub const MultibootTagDebug: type = extern struct {
    tag_type: u32,
    size: u32,
};
