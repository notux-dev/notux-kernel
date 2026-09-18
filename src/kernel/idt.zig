const isr: type = @import("isrex.zig");
const IdtEntry: type = packed struct {
    offset_low: u16,
    selector: u16,
    ist: u8,
    type_attr: u8,
    offset_mid: u16,
    offset_high: u32,
    reserved: u32,
};

const IdtPointer: type = packed struct {
    limit: u16,
    base: u64,
};

var idt: [256]IdtEntry = undefined;
var idt_ptr: IdtPointer = undefined;

const GATE_INTERRUPT: u8 = 0x8E;

fn setGate(vector: u8, handler: u64) void {
    idt[vector] = IdtEntry{
        .offset_low = @truncate(handler),
        .selector = 0x08,
        .ist = 0,
        .type_attr = GATE_INTERRUPT,
        .offset_mid = @truncate(handler >> 16),
        .offset_high = @truncate(handler >> 32),
        .reserved = 0,
    };
}

pub fn init() void {
    const handlers = [_]u64{
        @intFromPtr(&isr.isr0),  @intFromPtr(&isr.isr1),  @intFromPtr(&isr.isr2),
        @intFromPtr(&isr.isr3),  @intFromPtr(&isr.isr4),  @intFromPtr(&isr.isr5),
        @intFromPtr(&isr.isr6),  @intFromPtr(&isr.isr7),  @intFromPtr(&isr.isr8),
        @intFromPtr(&isr.isr9),  @intFromPtr(&isr.isr10), @intFromPtr(&isr.isr11),
        @intFromPtr(&isr.isr12), @intFromPtr(&isr.isr13), @intFromPtr(&isr.isr14),
        @intFromPtr(&isr.isr15), @intFromPtr(&isr.isr16), @intFromPtr(&isr.isr17),
        @intFromPtr(&isr.isr18), @intFromPtr(&isr.isr19), @intFromPtr(&isr.isr20),
        @intFromPtr(&isr.isr21), @intFromPtr(&isr.isr22), @intFromPtr(&isr.isr23),
        @intFromPtr(&isr.isr24), @intFromPtr(&isr.isr25), @intFromPtr(&isr.isr26),
        @intFromPtr(&isr.isr27), @intFromPtr(&isr.isr28), @intFromPtr(&isr.isr29),
        @intFromPtr(&isr.isr30), @intFromPtr(&isr.isr31),
    };

    for (handlers, 0..) |handler, vector| {
        setGate(@intCast(vector), handler);
    }

    idt_ptr = .{
        .limit = @sizeOf(@TypeOf(idt)) - 1,
        .base = @intFromPtr(&idt),
    };

    asm volatile ("lidt (%[ptr])"
        :
        : [ptr] "r" (&idt_ptr),
    );
}

export fn isr_common_handler(vector: u64, error_code: u64) callconv(.c) void {
    const vga: [*]volatile u16 = @ptrFromInt(0xB8000);

    const tens: u16 = @intCast(vector / 10);
    const ones: u16 = @intCast(vector % 10);

    vga[10] = 0x4F00 | ('0' + tens);
    vga[11] = 0x4F00 | ('0' + ones);

    _ = error_code;
    while (true) asm volatile ("hlt");
}
