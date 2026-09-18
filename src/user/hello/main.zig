export fn _start() callconv(.naked) noreturn {
    while (true) {
        asm volatile ("nop");
    }
}
