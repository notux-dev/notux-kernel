// font.zig
extern const font_8x8: [345][8]u8;
extern fn vbeaddr() u64;

const SCREEN_W: usize = 800;

export fn drawchar(x: c_int, y: c_int, c: u8, color: u32) void {
    const index: usize = if (c >= 0x20 and c <= 0x7E)
        @as(usize, c - 0x20)
    else
        0;

    const glyph = &font_8x8[index];
    const fb: [*]volatile u32 = @ptrFromInt(vbeaddr());
    const base: usize = @intCast(y * @as(c_int, SCREEN_W) + x);

    inline for (0..8) |row| {
        const bits = glyph[row];
        const row_base = base + row * SCREEN_W;
        inline for (0..8) |col| {
            if (bits & (@as(u8, 0x80) >> col) != 0) {
                fb[row_base + col] = color;
            }
        }
    }
}

export fn drawstring(x: c_int, y: c_int, s: [*:0]const u8, color: u32) void {
    var cx = x;
    var i: usize = 0;
    while (s[i] != 0) : (i += 1) {
        drawchar(cx, y, s[i], color);
        cx += 8;
    }
}
