const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "kernel",
        .target = target, // freestanding
        .optimize = optimize,
    });
    exe.addCSourceFiles(.{
        .files = &.{ "onion.c", "ramfs.c" },
        .flags = &.{ "-std=c11", "-ffreestanding" },
    });
    exe.addIncludePath(b.path("."));
    exe.addAnonymousModule("vfs", .{ .source_file = b.path("vfs.zig") });
    const vfs_obj = b.addObject(.{
        .name = "vfs",
        .root_source_file = b.path("vfs.zig"),
        .target = target,
        .optimize = optimize,
    });
    exe.addObject(vfs_obj);
    exe.addObjectFile(b.path("boot.o"));

    b.installArtifact(exe);
}
