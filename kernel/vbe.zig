extern fn pcibar0() u32;

const VBE_INDEX: u16 = 0x1CE;
const VBE_DATA: u16 = 0x1CF;

var fb_addr: u32 = 0;

fn outw(port: u16, value: u16) void {
    asm volatile ("outw %[value], %[port]"
        :
        : [value] "{ax}" (value),
          [port] "N{dx}" (port),
    );
}

fn vbewrite(index: u16, value: u16) void {
    outw(VBE_INDEX, index);
    outw(VBE_DATA, value);
}

export fn vbemode(width: u16, height: u16, bpp: u16) void {
    vbewrite(4, 0);
    vbewrite(1, width);
    vbewrite(2, height);
    vbewrite(3, bpp);
    vbewrite(4, 0x01 | 0x40);
    fb_addr = pcibar0();
}

export fn vbefill(color: u32) void {
    const total_pixels: usize = 800 * 600;
    const color64: u64 = (@as(u64, color) << 32) | @as(u64, color);
    const qwords: usize = total_pixels / 2;

    asm volatile (
        \\cld
        \\rep stosq
        :
        : [dst] "{rdi}" (@as(usize, fb_addr)),
          [val] "{rax}" (color64),
          [cnt] "{rcx}" (qwords),
        : .{ .memory = true, .rdi = true, .rax = true, .rcx = true });

    if (total_pixels % 2 != 0) {
        const fb32: [*]volatile u32 = @ptrFromInt(@as(usize, fb_addr));
        fb32[total_pixels - 1] = color;
    }
}

export fn vbeaddr() u32 {
    return fb_addr;
}
