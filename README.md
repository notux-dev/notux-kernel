<p align="center">
  <img src="logo.png" alt="notux-kernel logo" width="180">
</p>

<h1 align="center">notux-kernel</h1>

<p align="center">
  Clean-room, GPL-free, Linux-like x86_64 kernel — solo dev, educational project.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/arch-x86__64-blue">
  <img src="https://img.shields.io/badge/status-work%20in%20progress-yellow">
  <img src="https://img.shields.io/badge/license-MIT-green">
</p>

---

## О проекте

**notux-kernel** — экспериментальное 64-битное ядро, которое пишется с нуля в учебных целях. Цель — собрать Linux-подобное окружение (совместимость с частью Linux-приложений в перспективе) **без единой строчки GPL-кода**, включая собственную минимальную «token root»-систему.

Файловая система построена на портированном на x86_64 `xv6fs` (изначально из [xv6](https://github.com/mit-pdos/xv6-public) для x86/RISC-V, MIT). Это не production-ready ОС — это личный полигон для изучения внутренностей ядра.

## Что уже реализовано

- **Загрузка**: Multiboot-совместимый boot (`boot.asm`), переход в long mode, разметка PML4/PDPT/PD
- **Память**: физический менеджер памяти (PMM), виртуальная память и paging (VMM)
- **Прерывания**: GDT, IDT, ISR/IRQ, PIC и LAPIC
- **Планировщик и процессы**: базовый scheduler, управление процессами, системные вызовы (syscall)
- **Файловые системы**: VFS-прослойка, ramfs, порт xv6fs (см. `kernel/Xjail/xv6` и `kernel/xv6fs`)
- **Драйверы**: AHCI (диск), PCI, клавиатура, serial, VBE/VGA-графика, заготовка под IGPU
- **Вывод**: консоль, растровый шрифт, простая графика (gfx), обработчик паники

## Известные проблемы

- Ядро грузится и работает в QEMU, но **шелл нестабилен** — он не является частью самого ядра, баги там отдельные
- **xv6fs** временами ведёт себя непредсказуемо: архитектура xv6 изначально не рассчитана на x86_64, порт местами «натянут», из-за этого возможны артефакты в работе с файловой системой
- Сборочных скриптов (Makefile / linker script) в репозитории пока нет — добавь свою актуальную цепочку сборки, если хочешь, чтобы другие могли собрать проект без танцев с бубном

## Структура репозитория

```
notux-kernel/
├── boot/            # multiboot loader, переход в long mode
└── kernel/
    ├── ahci/         # драйвер AHCI
    ├── IGPU/         # заготовка под встроенную графику Intel
    ├── lapic/        # Local APIC
    ├── xv6fs/        # адаптация xv6 FS
    ├── Xjail/xv6/    # оригинальные заголовки/исходники xv6 (портирование)
    └── *.c / *.h     # GDT, IDT, PMM, VMM, paging, scheduler, syscalls, драйверы вывода и т.д.
```

## Сборка и запуск

Сборка через Makefile (собирает `notux.iso` и `fs.img`):

```bash
make clean && make
```
Запуск в QEMU (диск подключается через эмулируемый AHCI-контроллер, вывод serial идёт в stdio):

```bash
qemu-system-x86_64 \
    -m 512M \
    -cdrom notux.iso \
    -drive id=disk,file=fs.img,if=none,format=raw \
    -device ahci,id=ahci \
    -device ide-hd,drive=disk,bus=ahci.0 \
    -serial stdio \
    -no-reboot
```

## Лицензия и атрибуция

Код проекта распространяется по лицензии **MIT** (см. [LICENSE](./LICENSE)).

Часть кода файловой системы портирована из **xv6**, разработанного Russ Cox, Frans Kaashoek, Robert Morris, Eddie Kohler и др. (MIT License).

## Дисклеймер

Проект создан **исключительно в учебных целях**. Не предназначен для использования в продакшене.
