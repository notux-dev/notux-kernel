<div align="center">

<img src="logo.png" alt="notux-kernel logo" width="180"/>

# notux-kernel

**A GPL-free, Linux-like, clean-room 64-bit kernel — solo dev project**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Language: C](https://img.shields.io/badge/language-C-blue.svg)](#)
[![Language: C++](https://img.shields.io/badge/language-C%2B%2B-blue.svg)](#)
[![Language: Zig](https://img.shields.io/badge/language-Zig-orange.svg)](#)
[![Arch: x86_64](https://img.shields.io/badge/arch-x86__64-lightgrey.svg)](#)
[![Status: WIP](https://img.shields.io/badge/status-work--in--progress-red.svg)](#)

[English](#english) • [Русский](#русский)

</div>

---

<a name="english"></a>
## 🇬🇧 English

### About

**notux-kernel** is a solo-developer, clean-room, 64-bit kernel inspired by Linux but built without any GPL-licensed code. It's an educational/experimental project — **not intended for production use** — exploring what it takes to build a kernel capable of natively running Linux-style applications, complete with its own filesystem stack and a "token" root system.

The filesystem layer is built on a ported version of **xv6fs** (from the [xv6](https://github.com/mit-pdos/xv6-public) teaching operating system for x86/RISC-V), originally authored by Russ Cox, Frans Kaashoek, Robert Morris, Eddie Kohler, and others at MIT PDOS.

### Features

- 🧠 Freestanding **x86_64** kernel, boots via GRUB/Multiboot
- ⚙️ Core low-level subsystems: GDT, IDT, PIC, LAPIC, interrupt/exception handling (ISR/IRQ)
- 🧩 Memory management: physical memory manager (PMM), virtual memory manager (VMM), paging
- 🔀 Basic process management and scheduler
- 📞 Syscall interface (C + assembly stubs)
- 🖥️ Graphics: VGA and VBE framebuffer drivers (C and Zig implementations), 8x8 bitmap font renderer
- ⌨️ Drivers: PS/2 keyboard, serial (UART), PCI enumeration, AHCI (SATA), Intel iGPU probing
- 🗂️ Multiple filesystem backends:
  - **xv6fs** — ported from xv6
  - **RAM FS** — simple in-memory filesystem
  - **chachaFS** — custom C++ filesystem experiment
- 🌐 Mixed-language kernel: **C**, **C++**, **Zig**, and **x86-64 assembly (NASM)** side by side
- 📦 Boots as an ISO image via GRUB, runnable in QEMU

### Project structure

```
notux-kernel/
├── LICENSE
├── README.md
├── makefile              # Main build system (GNU Make)
├── linker.ld             # Kernel linker script
├── boot/
│   └── boot.asm          # Bootstrap assembly (Multiboot entry)
└── kernel/
    ├── build.zig         # Partial/experimental Zig build definition
    ├── console.c/.h      # Text console output
    ├── gdt.*, idt.*      # x86 descriptor tables
    ├── isr.asm, irq.asm, isrc.c   # Interrupt/exception handling
    ├── pic.*, lapic/     # Interrupt controllers (PIC / Local APIC)
    ├── paging.*, pmm.*, vmm.*     # Memory management
    ├── process.*, scheduler.c    # Processes & scheduling
    ├── syscall.*, syscalls.asm   # System call interface
    ├── keyboard.*, serial.*      # Input / UART drivers
    ├── pci.*, ahci/               # PCI bus, AHCI/SATA driver
    ├── IGPU/                      # Intel iGPU probing
    ├── vga.*, vbe.*, gfx.*, font*.* , logo.h  # Graphics stack
    ├── ramfs.*, ramfspawn.c       # RAM-backed filesystem
    ├── xv6fs/                     # Ported xv6 filesystem glue
    ├── chachaFS/                  # Custom experimental filesystem (C++)
    ├── Xjail/xv6/                 # Ported xv6 headers & filesystem core
    ├── onion.c                    # Kernel entry / init glue
    └── types.h, stdint.h, io.h    # Common definitions
```

### Requirements

To build notux-kernel you'll need:

- `gcc` / `g++` (with freestanding support)
- `nasm` (x86 assembler)
- `ld` (GNU linker)
- `zig` (for the Zig-based modules)
- `grub-mkrescue` + `xorriso` (to build the bootable ISO)
- `qemu-system-x86_64` (to run it)

### Build & run

```bash
git clone https://github.com/notux-dev/notux-kernel.git
cd notux-kernel

make          # build the bootable ISO
make run      # run it in QEMU
make clean    # clean build artifacts
```

> ⚠️ Note: the build currently expects a pre-built `fs.img` (produced via xv6's `mkfs` tool) and a userland binary (`user.elf`) — see the `makefile` for details. As this is an active work-in-progress, some build steps may need manual tweaking depending on your toolchain versions.

### Roadmap

- [ ] Stabilize and unify the memory management subsystem (PMM/VMM/paging)
- [ ] Flesh out the process scheduler (preemptive multitasking)
- [ ] Expand syscall coverage toward Linux-like ABI compatibility
- [ ] Mature chachaFS into a usable, documented filesystem
- [ ] SMP support (multi-core, beyond single LAPIC init)
- [ ] Networking stack (basic NIC driver + IP stack)
- [ ] Userland libc / basic toolchain for native apps
- [ ] Automated CI build across supported toolchains
- [ ] Broader hardware/driver support (beyond AHCI/PCI/iGPU basics)

*(Roadmap is indicative and subject to change — this is a solo, experimental project.)*

### Disclaimer

This kernel is written **for learning and experimentation purposes**. It is **not production-ready**, not security-audited, and should not be used to run untrusted code or in any critical environment.

### Contributing

This started as a solo project, but contributions, bug reports, and ideas are welcome:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Commit your changes with clear messages
4. Open a Pull Request describing what/why

Please keep clean-room principles in mind — **do not copy GPL-licensed code** into the project (this includes Linux kernel sources). Ported code (like xv6fs) must retain proper attribution to its original authors.

Bug reports and design discussions are welcome via [Issues](https://github.com/notux-dev/notux-kernel/issues).

### License

Licensed under the **MIT License** — see [LICENSE](LICENSE) for details.

Portions of the filesystem code are ported from **xv6**, © Russ Cox, Frans Kaashoek, Robert Morris, Eddie Kohler and contributors (MIT-licensed teaching OS from MIT PDOS).

---

<a name="русский"></a>
## 🇷🇺 Русский

### О проекте

**notux-kernel** — это solo-проект, clean-room 64-битное ядро, вдохновлённое Linux, но написанное без единой строчки GPL-кода. Это учебный/экспериментальный проект — **не предназначен для production** — цель которого исследовать, что нужно для создания ядра, способного нативно запускать Linux-подобные приложения, со своей файловой системой и «токенной» root-системой.

Файловая система построена на портированной версии **xv6fs** (из учебной ОС [xv6](https://github.com/mit-pdos/xv6-public) для x86/RISC-V), изначально написанной Russ Cox, Frans Kaashoek, Robert Morris, Eddie Kohler и другими в MIT PDOS.

### Возможности

- 🧠 Freestanding-ядро под **x86_64**, загружается через GRUB/Multiboot
- ⚙️ Низкоуровневые подсистемы: GDT, IDT, PIC, LAPIC, обработка прерываний и исключений (ISR/IRQ)
- 🧩 Управление памятью: менеджер физической памяти (PMM), виртуальной памяти (VMM), пейджинг
- 🔀 Базовое управление процессами и планировщик
- 📞 Интерфейс системных вызовов (C + ассемблерные заглушки)
- 🖥️ Графика: драйверы VGA и VBE-фреймбуфера (реализации на C и Zig), рендер битмап-шрифта 8x8
- ⌨️ Драйверы: PS/2 клавиатура, serial (UART), перечисление PCI, AHCI (SATA), обнаружение Intel iGPU
- 🗂️ Несколько файловых систем:
  - **xv6fs** — портирована из xv6
  - **RAM FS** — простая файловая система в памяти
  - **chachaFS** — собственный экспериментальный проект на C++
- 🌐 Смешанный стек языков: **C**, **C++**, **Zig** и ассемблер **x86-64 (NASM)**
- 📦 Загружается как ISO-образ через GRUB, запускается в QEMU

### Структура проекта

```
notux-kernel/
├── LICENSE
├── README.md
├── makefile              # Основная система сборки (GNU Make)
├── linker.ld             # Линкер-скрипт ядра
├── boot/
│   └── boot.asm          # Загрузочный ассемблер (точка входа Multiboot)
└── kernel/
    ├── build.zig         # Частичное/экспериментальное описание сборки на Zig
    ├── console.c/.h      # Вывод в текстовую консоль
    ├── gdt.*, idt.*      # Дескрипторные таблицы x86
    ├── isr.asm, irq.asm, isrc.c   # Обработка прерываний/исключений
    ├── pic.*, lapic/     # Контроллеры прерываний (PIC / Local APIC)
    ├── paging.*, pmm.*, vmm.*     # Управление памятью
    ├── process.*, scheduler.c    # Процессы и планировщик
    ├── syscall.*, syscalls.asm   # Интерфейс системных вызовов
    ├── keyboard.*, serial.*      # Драйверы ввода / UART
    ├── pci.*, ahci/               # Шина PCI, драйвер AHCI/SATA
    ├── IGPU/                      # Обнаружение Intel iGPU
    ├── vga.*, vbe.*, gfx.*, font*.* , logo.h  # Графический стек
    ├── ramfs.*, ramfspawn.c       # Файловая система на базе RAM
    ├── xv6fs/                     # Прослойка портированной xv6-файловой системы
    ├── chachaFS/                  # Экспериментальная ФС собственной разработки (C++)
    ├── Xjail/xv6/                 # Портированные заголовки и ядро ФС xv6
    ├── onion.c                    # Точка входа ядра / инициализация
    └── types.h, stdint.h, io.h    # Общие определения
```

### Требования

Для сборки notux-kernel понадобятся:

- `gcc` / `g++` (с поддержкой freestanding-сборки)
- `nasm` (ассемблер x86)
- `ld` (линковщик GNU)
- `zig` (для модулей на Zig)
- `grub-mkrescue` + `xorriso` (для сборки загрузочного ISO)
- `qemu-system-x86_64` (для запуска)

### Сборка и запуск

```bash
git clone https://github.com/notux-dev/notux-kernel.git
cd notux-kernel

make          # собрать загрузочный ISO
make run      # запустить в QEMU
make clean    # почистить артефакты сборки
```

> ⚠️ Обратите внимание: сборка на данный момент ожидает заранее собранный `fs.img` (создаётся утилитой `mkfs` из xv6) и userland-бинарник (`user.elf`) — подробности смотрите в `makefile`. Проект активно развивается, поэтому некоторые шаги сборки могут потребовать ручной донастройки в зависимости от версий тулчейна.

### Дорожная карта (Roadmap)

- [ ] Стабилизировать и унифицировать подсистему управления памятью (PMM/VMM/пейджинг)
- [ ] Доработать планировщик процессов (вытесняющая многозадачность)
- [ ] Расширить набор системных вызовов до совместимости с Linux-подобным ABI
- [ ] Довести chachaFS до рабочего, документированного состояния
- [ ] Поддержка SMP (многоядерность, помимо базовой инициализации LAPIC)
- [ ] Сетевой стек (базовый драйвер NIC + IP-стек)
- [ ] Userland libc / базовый тулчейн для нативных приложений
- [ ] Автоматизированная CI-сборка для поддерживаемых тулчейнов
- [ ] Расширение поддержки железа/драйверов (за пределами базовых AHCI/PCI/iGPU)

*(Roadmap ориентировочный и может меняться — это solo-экспериментальный проект.)*

### Дисклеймер

Это ядро написано **в учебных и экспериментальных целях**. Оно **не готово к production-использованию**, не проходило аудит безопасности и не должно использоваться для запуска недоверенного кода или в каких-либо критичных окружениях.

### Как помочь проекту (Contributing)

Проект начинался как solo-разработка, но контрибуции, баг-репорты и идеи приветствуются:

1. Форкните репозиторий
2. Создайте ветку для фичи (`git checkout -b feature/my-feature`)
3. Закоммитьте изменения с понятными сообщениями
4. Откройте Pull Request с описанием, что и зачем сделано

Пожалуйста, соблюдайте принципы clean-room-разработки — **не копируйте GPL-код** в проект (в том числе исходники ядра Linux). Портированный код (как xv6fs) должен сохранять корректную атрибуцию оригинальным авторам.

Баг-репорты и обсуждение дизайна — через [Issues](https://github.com/notux-dev/notux-kernel/issues).

### Лицензия

Проект распространяется под лицензией **MIT** — подробности в файле [LICENSE](LICENSE).

Часть кода файловой системы портирована из **xv6**, © Russ Cox, Frans Kaashoek, Robert Morris, Eddie Kohler и другие участники (учебная ОС MIT PDOS под лицензией MIT).
