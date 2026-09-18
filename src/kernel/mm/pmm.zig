const multiboot = @import("multiboot.zig");

const PAGE_SIZE: u64 = 4096;

var bitmap: [*]u8 = undefined;
var bitmap_size_bytes: usize = 0;
var highest_page: u64 = 0;

fn setBit(page_idx: u64) void {
    const byte_idx: u64 = page_idx / 8;
    const bit_idx: u3 = @intCast(page_idx % 8);
    bitmap[byte_idx] |= @as(u8, 1) << bit_idx;
}

fn clearBit(page_idx: u64) void {
    const byte_idx: u64 = page_idx / 8;
    const bit_idx: u3 = @intCast(page_idx % 8);
    bitmap[byte_idx] &= ~(@as(u8, 1) << bit_idx);
}

fn testBit(page_idx: u64) bool {
    const byte_idx: u64 = page_idx / 8;
    const bit_idx: u3 = @intCast(page_idx % 8);
    return (bitmap[byte_idx] & (@as(u8, 1) << bit_idx)) != 0;
}

pub fn init(bitmap_storage: [*]u8, regions: []const multiboot.MemoryRegion) void {
    bitmap = bitmap_storage;

    for (regions) |r| {
        const end: u64 = r.base + r.length;
        if (end > highest_page) highest_page = end;
    }

    const total_pages: u64 = highest_page / PAGE_SIZE;
    bitmap_size_bytes = @intCast((total_pages + 7) / 8);

    var i: usize = 0;
    while (i < bitmap_size_bytes) : (i += 1) {
        bitmap[i] = 0xFF;
    }

    for (regions) |r| {
        var addr: u64 = r.base;
        const end: u64 = r.base + r.length;
        while (addr < end) : (addr += PAGE_SIZE) {
            clearBit(addr / PAGE_SIZE);
        }
    }
}

pub fn markUsed(base: u64, length: u64) void {
    var addr: u64 = base & ~(PAGE_SIZE - 1);
    const end: u64 = base + length;
    while (addr < end) : (addr += PAGE_SIZE) {
        setBit(addr / PAGE_SIZE);
    }
}

pub fn allocPage() ?u64 {
    const total_pages: u64 = highest_page / PAGE_SIZE;
    var i: u64 = 0;
    while (i < total_pages) : (i += 1) {
        if (!testBit(i)) {
            setBit(i);
            return i * PAGE_SIZE;
        }
    }
    return null;
}

pub fn freePage(addr: u64) void {
    clearBit(addr / PAGE_SIZE);
}
