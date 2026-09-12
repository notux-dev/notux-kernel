const std: type = @import("std");
extern fn ramfs_init(disk_image: [*]const u8) void;
extern fn ramfs_open(name: [*:0]const u8) c_int;
extern fn ramfs_size(fd: c_int) u32;
extern fn ramfs_data(fd: c_int) [*]const u8;

export fn vfs_init(disk_image: [*]const u8) void {
    ramfs_init(disk_image);
}

export fn vfs_open(name: [*:0]const u8) c_int {
    return ramfs_open(name);
}

//homer lets the barts out  tu tu tu tu

export fn vfs_size(fd: c_int) u32 {
    return ramfs_size(fd);
}

export fn vfs_data(fd: c_int) [*]const u8 {
    return ramfs_data(fd);
}
