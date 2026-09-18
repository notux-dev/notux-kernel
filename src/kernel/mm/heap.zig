const std: type = @import("std");
const vmm: type = @import("vmm.zig");
const pmm: type = @import("pmm.zig");

const PAGE_SIZE: u64 = 4096;
const HEAP_START: u64 = 0x500000000;
var heap_current: u64 = HEAP_START;
var heap_pml4: *vmm.PageTable = undefined;

pub fn init(pml4: *vmm.PageTable) void {
    heap_pml4 = pml4;
    heap_current = HEAP_START;
}

fn growHeap(base: u64, size: usize) !void {
    const end: u64 = base + size;
    var vaddr: u64 = base & ~(PAGE_SIZE - 1);

    while (vaddr < end) : (vaddr += PAGE_SIZE) {
        const phys: u64 = pmm.allocPage() orelse return error.OutOfMemory;
        try vmm.mapPage(heap_pml4, vaddr, phys, true, false);
    }
}

fn alloc(ctx: *anyopaque, len: usize, alignment: std.mem.Alignment, ret_addr: usize) ?[*]u8 {
    _ = ctx;
    _ = ret_addr;
    const align_bytes: u64 = @as(u64, 1) << @intFromEnum(alignment);
    const aligned_current: u64 = (heap_current + align_bytes - 1) & ~(align_bytes - 1);
    growHeap(aligned_current, len) catch return null;
    heap_current = aligned_current + len;
    const result: [*]u8 = @ptrFromInt(aligned_current);
    return result;
}

fn resize(ctx: *anyopaque, buf: []u8, alignment: std.mem.Alignment, new_len: usize, ret_addr: usize) bool {
    _ = ctx;
    _ = buf;
    _ = alignment;
    _ = new_len;
    _ = ret_addr;
    return false;
}

fn remap(ctx: *anyopaque, memory: []u8, alignment: std.mem.Alignment, new_len: usize, ret_addr: usize) ?[*]u8 {
    _ = ctx;
    _ = memory;
    _ = alignment;
    _ = new_len;
    _ = ret_addr;
    return null;
}

fn free(ctx: *anyopaque, buf: []u8, alignment: std.mem.Alignment, ret_addr: usize) void {
    _ = ctx;
    _ = buf;
    _ = alignment;
    _ = ret_addr;
}

const vtable: std.mem.Allocator.VTable = .{
    .alloc = alloc,
    .resize = resize,
    .remap = remap,
    .free = free,
};

pub fn allocator() std.mem.Allocator {
    return std.mem.Allocator{
        .ptr = undefined,
        .vtable = &vtable,
    };
}
