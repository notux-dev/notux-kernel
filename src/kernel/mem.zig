export fn memset(dest: [*]u8, val: u8, len: usize) callconv(.c) [*]u8 {
    var i: usize = 0;
    while (i < len) : (i += 1) {
        dest[i] = val;
    }
    return dest;
}

export fn memcpy(dest: [*]u8, src: [*]const u8, len: usize) callconv(.c) [*]u8 {
    var i: usize = 0;
    while (i < len) : (i += 1) {
        dest[i] = src[i];
    }
    return dest;
}

export fn memmove(dest: [*]u8, src: [*]const u8, len: usize) callconv(.c) [*]u8 {
    if (@intFromPtr(dest) < @intFromPtr(src)) {
        var i: usize = 0;
        while (i < len) : (i += 1) {
            dest[i] = src[i];
        }
    } else {
        var i: usize = len;
        while (i > 0) {
            i -= 1;
            dest[i] = src[i];
        }
    }
    return dest;
}

export fn memcmp(a: [*]const u8, b: [*]const u8, len: usize) callconv(.c) c_int {
    var i: usize = 0;
    while (i < len) : (i += 1) {
        if (a[i] != b[i]) {
            return @as(c_int, a[i]) - @as(c_int, b[i]);
        }
    }
    return 0;
}
