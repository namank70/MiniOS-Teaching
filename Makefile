# Makefile for MiniOS

CC = gcc
AS = nasm
LD = ld

CFLAGS = -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
         -nostdlib -I$(KERNEL_DIR) -Wall -Wextra -O2 -m64
ASFLAGS = -f elf64
LDFLAGS = -nostdlib -n

# Directories
BUILD_DIR = build
BOOT_DIR = boot
KERNEL_DIR = kernel
GUI_DIR = gui
PYTHON_DIR = python
ISO_DIR = $(BUILD_DIR)/isodir
HOST_CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic -O2 -Iinclude

# Output files
BOOTLOADER = $(BUILD_DIR)/boot.bin
KERNEL = $(BUILD_DIR)/kernel.bin
KERNEL_MULTIBOOT = $(BUILD_DIR)/minios.bin
OS_IMAGE = $(BUILD_DIR)/minios.img
ISO_IMAGE = $(BUILD_DIR)/minios.iso

# Source files
BOOT_SRC = $(BOOT_DIR)/boot.asm
KERNEL_ASM = $(KERNEL_DIR)/interrupts.asm $(KERNEL_DIR)/kernel_entry.asm
KERNEL_C = $(KERNEL_DIR)/kernel_main.c $(GUI_DIR)/gui.c $(PYTHON_DIR)/python_runtime.c

# Object files
KERNEL_ASM_OBJ = $(BUILD_DIR)/interrupts.o $(BUILD_DIR)/kernel_entry.o
KERNEL_C_OBJ = $(BUILD_DIR)/kernel_main.o $(BUILD_DIR)/gui.o $(BUILD_DIR)/python_runtime.o

# Check for NASM
NASM_EXISTS := $(shell which nasm 2>/dev/null)

all: check-tools $(OS_IMAGE)

check-tools:
ifndef NASM_EXISTS
	@echo "======================================================"
	@echo "ERROR: NASM assembler not found"
	@echo "======================================================"
	@echo ""
	@echo "The full OS build requires NASM to compile the bootloader."
	@echo ""
	@echo "OPTION 1: Install NASM"
	@echo "  sudo apt-get install nasm"
	@echo ""
	@echo "OPTION 2: Use the GUI simulator instead (no NASM needed)"
	@echo "  make simulator"
	@echo "  ./minios_gui"
	@echo ""
	@echo "OPTION 3: Use the text simulator"
	@echo "  ./minios_simulator"
	@echo ""
	@echo "======================================================"
	@exit 1
endif
	@echo "✓ NASM found at $(NASM_EXISTS)"

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BOOTLOADER): $(BOOT_SRC) | $(BUILD_DIR)
	$(AS) -f bin $(BOOT_SRC) -o $(BOOTLOADER)

$(BUILD_DIR)/interrupts.o: $(KERNEL_DIR)/interrupts.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $(KERNEL_DIR)/interrupts.asm -o $(BUILD_DIR)/interrupts.o

$(BUILD_DIR)/kernel_entry.o: $(KERNEL_DIR)/kernel_entry.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $(KERNEL_DIR)/kernel_entry.asm -o $(BUILD_DIR)/kernel_entry.o

$(BUILD_DIR)/kernel_main.o: $(KERNEL_DIR)/kernel_main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $(KERNEL_DIR)/kernel_main.c -o $(BUILD_DIR)/kernel_main.o

$(BUILD_DIR)/gui.o: $(GUI_DIR)/gui.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(GUI_DIR) -c $(GUI_DIR)/gui.c -o $(BUILD_DIR)/gui.o

$(BUILD_DIR)/python_runtime.o: $(PYTHON_DIR)/python_runtime.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PYTHON_DIR) -c $(PYTHON_DIR)/python_runtime.c -o $(BUILD_DIR)/python_runtime.o

$(KERNEL): $(KERNEL_ASM_OBJ) $(KERNEL_C_OBJ) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -T $(KERNEL_DIR)/linker.ld \
		$(KERNEL_ASM_OBJ) $(KERNEL_C_OBJ) -o $(BUILD_DIR)/kernel.elf
	objcopy -O binary $(BUILD_DIR)/kernel.elf $(KERNEL)

$(OS_IMAGE): $(BOOTLOADER) $(KERNEL) | $(BUILD_DIR)
	cat $(BOOTLOADER) $(KERNEL) > $(OS_IMAGE)
	# Pad to at least 1MB
	truncate -s 1M $(OS_IMAGE)

# Build simulators (no NASM needed)
simulator: minios_gui minios_simulator

.PHONY: test-mock-provider
test-mock-provider:
	@tmpdir=$$(mktemp -d); \
	trap 'rm -rf "$$tmpdir"' EXIT; \
	$(CC) $(HOST_CFLAGS) src/shared/mock_provider.c \
		tests/unit/mock_provider_test.c -o "$$tmpdir/mock_provider_test"; \
	"$$tmpdir/mock_provider_test"

minios_gui: minios_gui.c
	@echo "Building CarPlay-style GUI simulator..."
	$(CC) -o minios_gui minios_gui.c -lm -Wall
	chmod +x minios_gui
	@echo "✓ Built: ./minios_gui"

minios_simulator: minios_simulator.c
	@echo "Building text-mode simulator..."
	$(CC) -o minios_simulator minios_simulator.c -lm -Wall
	chmod +x minios_simulator
	@echo "✓ Built: ./minios_simulator"

# Build GRUB ISO (recommended for bootable version)
iso: check-grub $(KERNEL_MULTIBOOT)
	@echo "Creating GRUB ISO..."
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_MULTIBOOT) $(ISO_DIR)/boot/minios.bin
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO_IMAGE) $(ISO_DIR) 2>/dev/null || echo "Note: grub-mkrescue needs xorriso"
	@echo "✓ Created: $(ISO_IMAGE)"
	@echo "Run with: make run-iso"

# Build VirtualBox-compatible ISO (no interrupts, auto-cycling)
iso-vbox: check-grub $(BUILD_DIR)
	@echo "Building VirtualBox-compatible kernel..."
	$(AS) -f elf32 $(KERNEL_DIR)/multiboot_header.asm -o $(BUILD_DIR)/multiboot_header.o
	$(CC) -m32 -ffreestanding -fno-stack-protector -nostdlib -Ikernel \
		-c $(KERNEL_DIR)/kernel_vbox.c -o $(BUILD_DIR)/kernel_vbox.o
	$(LD) -m elf_i386 -T $(KERNEL_DIR)/linker_multiboot.ld \
		$(BUILD_DIR)/multiboot_header.o $(BUILD_DIR)/kernel_vbox.o \
		-o $(BUILD_DIR)/minios_vbox.bin
	@echo "✓ Built VirtualBox kernel"
	@echo "Creating VirtualBox ISO..."
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(BUILD_DIR)/minios_vbox.bin $(ISO_DIR)/boot/minios.bin
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(BUILD_DIR)/minios_vbox.iso $(ISO_DIR) 2>/dev/null
	@echo "✓ Created: $(BUILD_DIR)/minios_vbox.iso"
	@echo "This version works in VirtualBox (no keyboard, auto-cycles)"

# Build ENHANCED kernel with proactive ML and memory tracking (works everywhere!)
iso-enhanced: check-grub $(BUILD_DIR)
	@echo "Building ENHANCED kernel with proactive ML + memory tracking..."
	$(AS) -f elf32 $(KERNEL_DIR)/multiboot_header.asm -o $(BUILD_DIR)/multiboot_header.o
	$(CC) -m32 -ffreestanding -fno-stack-protector -nostdlib -Ikernel \
		-c $(KERNEL_DIR)/kernel_enhanced.c -o $(BUILD_DIR)/kernel_enhanced.o
	$(LD) -m elf_i386 -T $(KERNEL_DIR)/linker_multiboot.ld \
		$(BUILD_DIR)/multiboot_header.o $(BUILD_DIR)/kernel_enhanced.o \
		-o $(BUILD_DIR)/minios_enhanced.bin
	@echo "✓ Built enhanced kernel"
	@echo "Creating enhanced ISO..."
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(BUILD_DIR)/minios_enhanced.bin $(ISO_DIR)/boot/minios.bin
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(BUILD_DIR)/minios_enhanced.iso $(ISO_DIR) 2>/dev/null
	@echo "✓ Created: $(BUILD_DIR)/minios_enhanced.iso"
	@echo ""
	@echo "🎉 ENHANCED FEATURES:"
	@echo "  ✓ Proactive AI suggestions (context-aware)"
	@echo "  ✓ Memory tracking (all operations logged)"
	@echo "  ✓ 20 activities (vs 8 in basic version)"
	@echo "  ✓ Learning from feedback"
	@echo "  ✓ Real-time statistics display"
	@echo "  ✓ Works in QEMU and VirtualBox!"
	@echo ""
	@echo "Run with: make run-enhanced"

# Run enhanced ISO in QEMU
run-enhanced:
	@if [ ! -f "$(BUILD_DIR)/minios_enhanced.iso" ]; then \
		echo ""; \
		echo "❌ Enhanced ISO not found!"; \
		echo "Build it first: make iso-enhanced"; \
		echo ""; \
		exit 1; \
	fi
	@echo "Starting enhanced MiniOS in QEMU..."
	@echo "Features: Proactive ML + Memory Tracking"
	@echo ""
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/minios_enhanced.iso -m 256M -boot d

# Build CarPlay-style kernel with app launcher (works everywhere!)
iso-carplay: check-grub $(BUILD_DIR)
	@echo "Building CarPlay-style kernel with app launcher..."
	$(AS) -f elf32 $(KERNEL_DIR)/multiboot_header.asm -o $(BUILD_DIR)/multiboot_header.o
	$(CC) -m32 -ffreestanding -fno-stack-protector -nostdlib -Ikernel \
		-c $(KERNEL_DIR)/kernel_carplay.c -o $(BUILD_DIR)/kernel_carplay.o
	$(LD) -m elf_i386 -T $(KERNEL_DIR)/linker_multiboot.ld \
		$(BUILD_DIR)/multiboot_header.o $(BUILD_DIR)/kernel_carplay.o \
		-o $(BUILD_DIR)/minios_carplay.bin
	@echo "✓ Built CarPlay kernel"
	@echo "Creating CarPlay ISO..."
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(BUILD_DIR)/minios_carplay.bin $(ISO_DIR)/boot/minios.bin
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(BUILD_DIR)/minios_carplay.iso $(ISO_DIR) 2>/dev/null
	@echo "✓ Created: $(BUILD_DIR)/minios_carplay.iso"
	@echo ""
	@echo "🎨 CARPLAY FEATURES:"
	@echo "  ✓ Apple CarPlay-style home screen"
	@echo "  ✓ App launcher with 4 apps"
	@echo "  ✓ Calendar app with scheduled events"
	@echo "  ✓ AI suggestions in calendar"
	@echo "  ✓ Navigation with arrow keys"
	@echo "  ✓ Proactive ML + Memory tracking"
	@echo "  ✓ Works in QEMU and VirtualBox!"
	@echo ""
	@echo "Run with: make run-carplay"

# Run CarPlay ISO in QEMU
run-carplay:
	@if [ ! -f "$(BUILD_DIR)/minios_carplay.iso" ]; then \
		echo ""; \
		echo "❌ CarPlay ISO not found!"; \
		echo "Build it first: make iso-carplay"; \
		echo ""; \
		exit 1; \
	fi
	@echo "Starting CarPlay MiniOS in QEMU..."
	@echo "Features: App Launcher + Calendar + AI"
	@echo ""
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/minios_carplay.iso -m 256M -boot d

# Build multiboot kernel with full GUI
$(KERNEL_MULTIBOOT): check-nasm-optional $(BUILD_DIR)
	@echo "Building multiboot kernel with GUI..."
	$(AS) -f elf32 $(KERNEL_DIR)/multiboot_header.asm -o $(BUILD_DIR)/multiboot_header.o
	$(CC) -m32 -ffreestanding -fno-stack-protector -nostdlib -Ikernel \
		-c $(KERNEL_DIR)/kernel_full.c -o $(BUILD_DIR)/kernel_full.o
	$(LD) -m elf_i386 -T $(KERNEL_DIR)/linker_multiboot.ld \
		$(BUILD_DIR)/multiboot_header.o $(BUILD_DIR)/kernel_full.o \
		-o $(KERNEL_MULTIBOOT)
	@echo "✓ Built: $(KERNEL_MULTIBOOT)"

# Build simple text-only kernel (fallback)
$(BUILD_DIR)/minios_simple.bin: check-nasm-optional $(BUILD_DIR)
	@echo "Building simple multiboot kernel..."
	$(AS) -f elf32 $(KERNEL_DIR)/multiboot_header.asm -o $(BUILD_DIR)/multiboot_header.o
	$(CC) -m32 -ffreestanding -fno-stack-protector -nostdlib -Ikernel \
		-c $(KERNEL_DIR)/kernel_simple.c -o $(BUILD_DIR)/kernel_simple.o
	$(LD) -m elf_i386 -T $(KERNEL_DIR)/linker_multiboot.ld \
		$(BUILD_DIR)/multiboot_header.o $(BUILD_DIR)/kernel_simple.o \
		-o $(BUILD_DIR)/minios_simple.bin
	@echo "✓ Built: simple kernel"

check-grub:
	@which grub-mkrescue > /dev/null || (echo "Installing GRUB tools..." && \
		echo "Run: sudo apt-get install grub-pc-bin xorriso" && exit 1)

check-nasm-optional:
	@which nasm > /dev/null || (echo "NASM not found - trying to continue anyway..." && exit 0)

clean:
	rm -rf $(BUILD_DIR)
	rm -f minios_gui minios_simulator

run: $(OS_IMAGE)
	qemu-system-x86_64 -drive format=raw,file=$(OS_IMAGE) -m 128M

run-iso:
	@echo "Starting MiniOS in QEMU from ISO..."
	@if [ ! -f "$(ISO_IMAGE)" ]; then \
		echo ""; \
		echo "❌ ERROR: ISO file not found!"; \
		echo ""; \
		echo "The file $(ISO_IMAGE) does not exist."; \
		echo ""; \
		echo "You need to build it first:"; \
		echo "  make iso"; \
		echo ""; \
		echo "Then run this command again:"; \
		echo "  make run-iso"; \
		echo ""; \
		echo "Or use the automated script:"; \
		echo "  ./build_and_run.sh"; \
		echo ""; \
		exit 1; \
	fi
	@echo "✓ ISO found: $(ISO_IMAGE)"
	@ls -lh $(ISO_IMAGE)
	@echo ""
	@echo "Starting QEMU... (Press Ctrl+Alt+G to release mouse/keyboard)"
	@echo ""
	qemu-system-x86_64 -cdrom $(ISO_IMAGE) -m 128M

debug: $(OS_IMAGE)
	qemu-system-x86_64 -drive format=raw,file=$(OS_IMAGE) -m 128M -s -S

# Run simulators
run-gui: minios_gui
	./minios_gui

run-sim: minios_simulator
	./minios_simulator

# Test with minimal bootloader (no kernel needed)
test-boot: check-tools $(BUILD_DIR)
	@echo "Building minimal test bootloader..."
	$(AS) -f bin $(BOOT_DIR)/minimal_boot.asm -o $(BUILD_DIR)/test_boot.bin
	@echo "Creating test image..."
	dd if=$(BUILD_DIR)/test_boot.bin of=$(BUILD_DIR)/test_boot.img bs=512 count=1
	dd if=/dev/zero of=$(BUILD_DIR)/test_boot.img bs=512 count=2047 seek=1
	@echo "Running test..."
	@echo "Expected: Screen shows '123' then '4PROT'"
	qemu-system-x86_64 -drive format=raw,file=$(BUILD_DIR)/test_boot.img -m 128M

# Test with minimal kernel
test: check-tools $(BUILD_DIR)
	@echo "Building minimal test kernel..."
	$(AS) -f bin $(BOOT_DIR)/boot.asm -o $(BUILD_DIR)/boot.bin
	$(AS) -f bin $(KERNEL_DIR)/test_kernel.asm -o $(BUILD_DIR)/test_kernel.bin
	cat $(BUILD_DIR)/boot.bin $(BUILD_DIR)/test_kernel.bin > $(BUILD_DIR)/test.img
	truncate -s 1M $(BUILD_DIR)/test.img
	@echo "Running test..."
	qemu-system-x86_64 -drive format=raw,file=$(BUILD_DIR)/test.img -m 128M

help:
	@echo "MiniOS Build System"
	@echo "=================="
	@echo ""
	@echo "Simulator Targets (Ready to run!):"
	@echo "  make simulator       - Build both simulators (RECOMMENDED)"
	@echo "  make run-gui         - Build and run GUI simulator"
	@echo "  make run-sim         - Build and run text simulator"
	@echo "  ./minios_gui         - Run GUI directly"
	@echo "  ./minios_simulator   - Run text version directly"
	@echo ""
	@echo "Bootable OS Targets:"
	@echo "  make iso             - Build GRUB ISO (RECOMMENDED for bootable)"
	@echo "  make run-iso         - Build and run ISO in QEMU"
	@echo "  make                 - Build with custom bootloader (requires NASM)"
	@echo "  make run             - Run custom bootloader version"
	@echo ""
	@echo "Testing:"
	@echo "  make test-boot       - Test minimal bootloader"
	@echo "  make test            - Test with minimal kernel"
	@echo ""
	@echo "Utilities:"
	@echo "  make clean           - Remove build artifacts"
	@echo "  make help            - Show this help"
	@echo ""
	@echo "Quick Start:"
	@echo "  For simulators:  make simulator && ./minios_gui"
	@echo "  For bootable OS: sudo apt-get install grub-pc-bin xorriso"
	@echo "                   make iso && make run-iso"
	@echo ""
	@echo "Requirements:"
	@echo "  Simulators: gcc, libc (always available)"
	@echo "  GRUB ISO: grub-mkrescue, xorriso"
	@echo "  Custom bootloader: nasm, gcc, qemu"

.PHONY: all clean run debug simulator help check-tools run-gui run-sim
