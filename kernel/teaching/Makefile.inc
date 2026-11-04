# Separate build tree: original kernel/research/demo targets stay intact.
TEACHING_CC ?= gcc
TEACHING_QEMU ?= qemu-system-i386
TEACHING_DIR := build/teaching
TEACHING_CFLAGS := -std=c11 -m32 -ffreestanding -fno-builtin -fno-pie -fno-pic \
 -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables \
 -msoft-float -mno-sse -mno-mmx -mpreferred-stack-boundary=2 -mincoming-stack-boundary=2 \
 -O2 -Wall -Wextra -Werror -Iinclude -Ikernel/teaching -MMD -MP
TEACHING_C := kernel/teaching/kernel.c kernel/teaching/console.c \
 kernel/teaching/scheduler.c kernel/teaching/string.c src/shared/shell.c \
 src/shared/mock_provider.c src/shared/suggestion_policy.c src/shared/task_policy.c \
 src/shared/region_bounds.c src/platform/kernel/runtime.c
TEACHING_OBJECTS := $(patsubst %.c,$(TEACHING_DIR)/%.o,$(TEACHING_C)) \
 $(TEACHING_DIR)/boot.o $(TEACHING_DIR)/switch.o

$(TEACHING_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(TEACHING_CC) $(TEACHING_CFLAGS) -c $< -o $@
$(TEACHING_DIR)/boot.o: kernel/teaching/boot.asm
	mkdir -p $(dir $@)
	nasm -f elf32 $< -o $@
$(TEACHING_DIR)/switch.o: kernel/teaching/scheduler.asm
	mkdir -p $(dir $@)
	nasm -f elf32 $< -o $@
$(TEACHING_DIR)/minios.elf: $(TEACHING_OBJECTS) kernel/linker_multiboot.ld
	$(TEACHING_CC) -m32 -nostdlib -static -no-pie -Wl,--build-id=none,-z,noexecstack \
	 -Wl,-T,kernel/linker_multiboot.ld $(TEACHING_OBJECTS) -lgcc -o $@
	grub-file --is-x86-multiboot $@
$(TEACHING_DIR)/minios.iso: $(TEACHING_DIR)/minios.elf kernel/teaching/grub.cfg
	mkdir -p $(TEACHING_DIR)/iso/boot/grub
	cp $< $(TEACHING_DIR)/iso/boot/minios.elf
	cp kernel/teaching/grub.cfg $(TEACHING_DIR)/iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(TEACHING_DIR)/iso

teaching-kernel: $(TEACHING_DIR)/minios.elf
teaching-iso: $(TEACHING_DIR)/minios.iso
run-teaching: teaching-iso
	$(TEACHING_QEMU) -accel tcg -m 64M -cdrom $(TEACHING_DIR)/minios.iso \
	 -display none -serial mon:stdio -no-reboot

$(TEACHING_DIR)/task-policy-test: tests/unit/task_policy_test.c src/shared/task_policy.c include/minios/task_policy.h
	mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) tests/unit/task_policy_test.c src/shared/task_policy.c -o $@
$(TEACHING_DIR)/region-test: tests/unit/region_bounds_test.c src/shared/region_bounds.c include/minios/runtime.h
	mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) tests/unit/region_bounds_test.c src/shared/region_bounds.c -o $@
test-person-a: $(TEACHING_DIR)/task-policy-test $(TEACHING_DIR)/region-test
	./$(TEACHING_DIR)/task-policy-test
	./$(TEACHING_DIR)/region-test
test-qemu: teaching-iso
	python3 tests/qemu/shell_smoke.py $(TEACHING_DIR)/minios.iso
test-teaching: test-person-b test-person-a test-qemu

-include $(patsubst %.o,%.d,$(filter %.o,$(TEACHING_OBJECTS)))
.PHONY: teaching-kernel teaching-iso run-teaching test-person-a test-qemu test-teaching
