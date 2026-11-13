# MyOS

Небольшая учебная операционная система для x86, написанная на C и Assembly.
Поддерживаются две ветки: **32-битная** и **64-битная**. Загрузка выполняется
через **GRUB (Multiboot)**, ядро реализует базовые механизмы: обработку
прерываний, PIC, драйвер ATA (PIO) и простейший аллокатор кучи.

## Возможности

- 32-битное и 64-битное ядро
- Загрузка через GRUB / Multiboot
- IDT и обработчики прерываний (ISR)
- Программируемый контроллер прерываний (PIC)
- Драйвер ATA (PIO) для чтения диска
- Простейший менеджер кучи (`heap`)
- Минимальная библиотека строковых функций (`string`)

## Структура репозитория

```
.
├── boot.asm           # точка входа 32-битного ядра
├── boot64.asm         # точка входа 64-битного ядра
├── kernel.c           # основная логика 32-битного ядра
├── kernel64.c         # основная логика 64-битного ядра
├── ata.c / ata.h      # драйвер ATA
├── pic.c / pic.h      # контроллер прерываний
├── idt.c / idt.h      # таблица дескрипторов прерываний
├── isr.asm            # обработчики прерываний (stub'ы)
├── heap.c / heap.h    # аллокатор кучи
├── string.c / string.h# строковые функции
├── linker.ld          # linker-скрипт для 32-бит
├── linker64.ld        # linker-скрипт для 64-бит
├── grub64.cfg         # конфигурация GRUB для 64-битной сборки
├── myos.iso           # готовый 32-битный образ
└── myos64.iso         # готовый 64-битный образ
```

## Зависимости

Для сборки из исходников нужны:

- **NASM** — ассемблер
- **GCC** — кросс-компилятор (рекомендуется `i686-elf-gcc` и `x86_64-elf-gcc`;
  системный `gcc` с флагами `-m32` / `-m64` тоже может подойти)
- **GNU ld** — линкер
- **GRUB** (`grub-mkrescue`) и **xorriso**, **mtools** — для сборки ISO
- **QEMU** — для запуска (опционально)
- **make** — если используете Makefile

Установка на Debian/Ubuntu:

```bash
sudo apt install nasm gcc make grub-pc-bin grub-common xorriso mtools qemu-system-x86
```

На Arch:

```bash
sudo pacman -S nasm gcc make grub xorriso mtools qemu-system-x86
```

## Быстрый запуск (без сборки)

В репозитории уже лежат готовые образы:

- `myos.iso` — 32-битная версия
- `myos64.iso` — 64-битная версия

### QEMU

```bash
# 32-бит
qemu-system-i386 -cdrom myos.iso -m 128M

# 64-бит
qemu-system-x86_64 -cdrom myos64.iso -m 128M
```

Полезные флаги для отладки:

```bash
-serial stdio          # вывод в терминал
-d int -no-reboot      # лог прерываний и тройных сбоев
-S -s                  # ожидание GDB на :1234
```

### VirtualBox

1. Создать новую ВМ (тип: Other/Unknown, 32- или 64-бит).
2. В настройках → Носители подключить `myos.iso` или `myos64.iso` как CD/DVD.
3. Отключить EFI (проект использует BIOS/Multiboot).
4. Запустить ВМ.

### Реальное железо

```bash
sudo dd if=myos64.iso of=/dev/sdX bs=4M status=progress && sync
```

> ⚠️ `of=/dev/sdX` замените на **свой** USB-накопитель. Ошибка в имени устройства
> приведёт к потере данных на диске.

## Сборка из исходников

> Ниже — примерные команды. Замените имена файлов/префикс компилятора,
> если ваша сборка отличается.

### 32-битное ядро

```bash
# ассемблер
nasm -f elf32 boot.asm -o boot.o
nasm -f elf32 isr.asm  -o isr.o

# C-файлы
gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 -Wall -Wextra \
    -c kernel.c ata.c pic.c idt.c heap.c string.c

# линковка
ld -m elf_i386 -T linker.ld -o kernel.bin \
   boot.o isr.o kernel.o ata.o pic.o idt.o heap.o string.o
```

### 64-битное ядро

```bash
nasm -f elf64 boot64.asm -o boot64.o
nasm -f elf64 isr.asm    -o isr64.o

gcc -m64 -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone \
    -O2 -Wall -Wextra -c kernel64.c ata.c pic.c idt.c heap.c string.c

ld -m elf_x86_64 -T linker64.ld -o kernel64.bin \
   boot64.o isr64.o kernel64.o ata.o pic.o idt.o heap.o string.o
```

### Сборка ISO через GRUB

```bash
mkdir -p iso/boot/grub
cp kernel.bin  iso/boot/kernel.bin
cp grub.cfg    iso/boot/grub/grub.cfg     # при необходимости
grub-mkrescue -o myos.iso iso
```

Для 64-битной сборки используйте `kernel64.bin`, `grub64.cfg` и имя `myos64.iso`.

## Известные ограничения

- Нет пользовательского пространства и системных вызовов
- Нет файловой системы
- Нет планировщика и многозадачности
- Нет графического интерфейса
- ATA-драйвер работает в режиме PIO (медленно, но просто)
- 64-битная ветка — экспериментальная

## Лицензия

Укажите лицензию по вашему выбору (например, MIT). Если лицензия не указана,
по умолчанию действует «все права защищены».
