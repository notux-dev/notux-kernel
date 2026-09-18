ZIG := zig
BUILD_DIR := build
USER_DIR := src/user

USER_ELF := $(BUILD_DIR)/user/hello.elf

.PHONY: all clean

all: $(USER_ELF)

$(USER_ELF): $(USER_DIR)/hello/main.zig
	@mkdir -p $(dir $@)
	$(ZIG) build-exe $(USER_DIR)/hello/main.zig \
		-target x86_64-freestanding-none \
		-mcpu=x86_64-sse-sse2-mmx \
		-O ReleaseSmall \
		-fno-entry \
		-fno-compiler-rt \
		-femit-bin=$@
		
clean:
	rm -rf $(BUILD_DIR)/user