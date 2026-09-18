const idt: type = @import("idt.zig");
const multiboot: type = @import("mm/multiboot.zig");
const pmm: type = @import("mm/pmm.zig");
const mem: type = @import("mem.zig");
const vmm: type = @import("mm/vmm.zig");
const heap: type = @import("mm/heap.zig");

pub fn panic(msg: []const u8, error_return_trace: ?*anyopaque, ret_addr: ?usize) noreturn {
    _ = msg;
    _ = error_return_trace;
    _ = ret_addr;

    const vga: [*]volatile u16 = @ptrFromInt(0xB8000);
    vga[0] = 0x4F50;
    vga[1] = 0x4F41;
    vga[2] = 0x4F4E;
    vga[3] = 0x4F49;
    vga[4] = 0x4F43;

    while (true) {
        asm volatile ("hlt");
    }
}

extern var _kernel_start: u8;
extern var _kernel_end: u8;

var bitmap_storage: [32768]u8 = undefined;

export fn kernel(mb_info_ptr: u64) callconv(.c) noreturn {
    asm volatile ("cli");
    const vga: [*]volatile u16 = @ptrFromInt(0xB8000);

    var i: usize = 0;
    while (i < 80 * 25) : (i += 1) {
        vga[i] = 0x0F20;
    }

    vga[0] = 0x2F4F;
    vga[1] = 0x2F4B;

    vga[30] = 0x2F30 + @as(u16, @intCast((mb_info_ptr / 1000000) % 10));
    vga[31] = 0x2F30 + @as(u16, @intCast((mb_info_ptr / 100000) % 10));
    vga[32] = 0x2F30 + @as(u16, @intCast((mb_info_ptr / 10000) % 10));
    vga[33] = 0x2F30 + @as(u16, @intCast((mb_info_ptr / 1000) % 10));
    vga[34] = 0x2F30 + @as(u16, @intCast((mb_info_ptr / 100) % 10));
    vga[35] = 0x2F30 + @as(u16, @intCast((mb_info_ptr / 10) % 10));
    vga[36] = 0x2F30 + @as(u16, @intCast(mb_info_ptr % 10));

    idt.init();
    vga[2] = 0x2F31;

    // --- ДИАГНОСТИКА: смотрим что реально лежит в multiboot info ---
    const total_size_ptr: *const u32 = @ptrFromInt(mb_info_ptr);
    const total_size: u32 = total_size_ptr.*;

    // выводим total_size как 3 цифры (сотни, десятки, единицы) начиная с позиции 20
    vga[20] = 0x2F30 + @as(u16, @intCast((total_size / 100) % 10));
    vga[21] = 0x2F30 + @as(u16, @intCast((total_size / 10) % 10));
    vga[22] = 0x2F30 + @as(u16, @intCast(total_size % 10));

    // выводим tag_type первого тега (сразу после 8-байтового заголовка)
    const first_tag: *const multiboot.MultibootTagDebug = @ptrFromInt(mb_info_ptr + 8);
    vga[23] = 0x2F30 + @as(u16, @intCast(first_tag.tag_type % 10));

    var regions: [multiboot.MAX_REGIONS]multiboot.MemoryRegion = undefined;
    vga[3] = 0x2F32;

    const region_count: usize = multiboot.parseMemoryMap(mb_info_ptr, &regions);
    vga[4] = 0x2F33;
    vga[5] = 0x2F30 + @as(u16, @intCast(@min(region_count, 9)));

    pmm.init(&bitmap_storage, regions[0..region_count]);
    vga[6] = 0x2F34;

    const kernel_start_addr: u64 = @intFromPtr(&_kernel_start);
    const kernel_end_addr: u64 = @intFromPtr(&_kernel_end);
    vga[7] = 0x2F35;

    pmm.markUsed(kernel_start_addr, kernel_end_addr - kernel_start_addr);
    vga[8] = 0x2F36;

    const test_page: ?u64 = pmm.allocPage();
    if (test_page == null) {
        vga[9] = 0x4F50;
        while (true) asm volatile ("hlt");
    }
    vga[9] = 0x2F50;
    const pml4: *vmm.PageTable = vmm.getCurrentPML4();
    const test_vaddr: u64 = 0x400000000;
    const test_paddr: u64 = test_page.?;

    vmm.mapPage(pml4, test_vaddr, test_paddr, true, false) catch {
        vga[10] = 0x4F45; // E
        while (true) asm volatile ("hlt");
    };

    const test_ptr: *volatile u32 = @ptrFromInt(test_vaddr);
    test_ptr.* = 0xDEADBEEF;

    if (test_ptr.* == 0xDEADBEEF) {
        vga[10] = 0x2F56; // V
    } else {
        vga[10] = 0x4F58; // X
    }

    heap.init(pml4);

    const allocator = heap.allocator();
    const test_alloc = allocator.alloc(u8, 16) catch null;

    if (test_alloc) |mem_slice| {
        _ = mem_slice;
        vga[11] = 0x2F48;
    } else {
        vga[11] = 0x4F48;
    }

    const module_info = multiboot.findFirstModule(mb_info_ptr);

    if (module_info) |mod| {
        vga[12] = 0x2F4D;
        _ = mod.start;
        _ = mod.end;
    } else {
        vga[12] = 0x4F4D;
    }

    while (true) {
        asm volatile ("hlt");
    }
}
