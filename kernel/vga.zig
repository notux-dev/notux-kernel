const VGA_WIDTH: comptime_int = 80;
const VGA_CELL_SIZE: comptime_int = 2;

export fn print(msg: [*:0]const u8, rom: c_int, color: u8) void {
    const video = @as([*]volatile u8, @ptrFromInt(0xB8000));
    const offset: usize = @as(usize, @intCast(rom)) * VGA_WIDTH * VGA_CELL_SIZE;
    var i: usize = 0;
    while (msg[i] != 0) : (i += 1) {
        video[offset + i * 2] = msg[i];
        video[offset + i * 2 + 1] = color;
    }
}
