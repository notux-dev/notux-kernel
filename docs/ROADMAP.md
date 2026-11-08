# Дорожная карта notux (версия 0.0.16 и дальше)

Это «лестница вверх»: что уже есть и в каком порядке строим остальное. Каждая ступень имеет
критерий, который проверяется в QEMU; программы-эталоны — в `COMPAT_LADDER.md`, подробности
по ходу работы — в `DEVLOG.md`. Порядок — предложение, его можно менять. Лицензия ядра — MIT:
код пишется по спецификациям и man-страницам, GPL-программы (busybox, tcc) остаются внешними.

## Сделано

mksh · fastfetch · tty/termios · ext2 (чтение/запись, корень на ext2) · sbase · tcc (внутри notux) · vi (toybox) ·
динамическая линковка (`ld-musl`, `dlopen`) · память (`mremap`, `MAP_FIXED_NOREPLACE`, 512 VMA, COW общих PT) ·
`clone`/`vfork` как fork-копия · BusyBox init → OpenRC → сервис · UEFI через GRUB-EFI (framebuffer из Multiboot).

## Ступени

### 1. Доводка уже сделанного (быстро, снимает мелкие грабли)
- `/proc/<pid>/` (stat, cmdline, exe, fd), `flock`, fifo (`mknod S_IFIFO`) на ext2.
- Настоящие tmpfs (`/run`, `/tmp`) и devtmpfs вместо заглушек `mount`; `/etc/fstab`, `umount2`.
- `/dev/console`, `setrlimit/prlimit`, utmp.
- Критерий: штатный набор runlevel'ов OpenRC (sysinit/boot/default) проходит без урезания.

### 2. Блочные устройства и разделы
- Абстракция `blkdev` (сейчас ext2/xv6fs привязаны к AHCI), ATA PIO для IDE.
- Разбор MBR и GPT, корень на разделе; затем NVMe (polling).
- Критерий: тот же ext2-корень грузится с AHCI, IDE, NVMe и с раздела GPT.

### 3. Основа IPC: сокеты и общая память
- `AF_UNIX` (stream, `socketpair`, `SCM_RIGHTS`), `epoll`, `eventfd`, `signalfd`, `timerfd`.
- `MAP_SHARED` между процессами (и write-back файла), `shm_open`/`shmget`; `/dev/log` для OpenRC.
- Критерий: dinit / runit или s6 поднимают дерево сервисов; тест на передачу fd и shared memory.

### 4. Потоки
- `clone(CLONE_THREAD)`, `futex`, `set_tid_address`, TLS на поток, `tgkill`, `madvise`.
- Критерий: pthread-тесты musl, затем `python3`/make -j.

### 5. Сеть
- Драйвер e1000 (затем virtio-net), ARP/IPv4/ICMP/UDP/DHCP, TCP; предпочтительно lwIP как внешняя зависимость
  (BSD-3), наши — драйвер и слой Linux-сокетов (`AF_INET`).
- Критерий: `ping`, DHCP-адрес, `wget`/`curl` скачивают страницу в QEMU user-net; в OpenRC сервис `net`.

### 6. Самохостинг и инструменты
- `make`, затем gcc/binutils (или clang) внутри notux; vim (terminfo/ncurses), python.
- Критерий: notux собирает свой userspace (tcc → make → gcc) без хоста.

### 7. Графика
- `/dev/fb0` (ioctl `FBIOGET_*`, mmap), ввод (`/dev/input/event*`: клавиатура, мышь).
- X11: Xvfb → Xfbdev (kdrive) + xterm/xeyes; затем Wayland (Weston или проще, `tinywl`/wlroots) — нужны ступени 3–4.
- Критерий: xterm в окне на экране 800x600; затем hello-клиент Wayland.

### 8. Дистрибутив (rezzveOS на основе Alpine)
- Динамический rootfs Alpine грузится из чужого образа (apk, busybox), `/proc/self/*`.
- Пакеты, установка на диск, UEFI-загрузка без GRUB (свой `BOOTX64.EFI`, ACPI).
- Критерий: образ Alpine минимального набора стартует под OpenRC на notux.

## Параллельные направления (без жёсткого места в очереди)
- **Модель прав через токены/capabilities** — сначала только документ с дизайном (`docs/TOKENS.md`), реализация позже.
- **Безопасность и память:** `PROT_EXEC`/NX, demand paging, ASLR, принуждение прав доступа (сейчас права не принуждаются).
- **Устойчивость:** ext2 с журналом, `fsck`-проверка при загрузке, больше KiB в `pmm` (>1 ГиБ).
- **Контрибьюторы и релизы:** `CONTRIBUTING.md`, чеклист релиза (регрессия `/tmp/t2.sh`, `/tmp/t.sh`, `test_openrc.sh`, `e2fsck`), CI в QEMU.
- **Служебные прогоны после каждой ступени:** musl libc-test, LTP (выбранные вызовы), busybox testsuite.

## Почему такой порядок
Ступень 1 дёшево убирает заглушки. Ступень 2 нужна, чтобы система вообще ставилась на разные диски и разделы. Ступени 3 и 4
— общий фундамент для X11/Wayland, init-систем и большинства серверных программ. Сеть (5) независима и самая объёмная,
её можно вести параллельно с 3–4. Самохостинг (6) и графика (7) — это уже «потребители» фундамента, дистрибутив (8) замыкает цепочку.
