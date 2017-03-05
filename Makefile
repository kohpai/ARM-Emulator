LDFLAGS += $(shell pkg-config --libs glib-2.0) -lpthread -lm -lunicorn -lelf -g

emu_cloud: emu_cloud.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

check_elf: check_elf.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

arm_m0_uart_test: arm_m0_uart_test.c
	clang -target arm-none-eabi -mcpu=cortex-m0 \
		-mfloat-abi=soft -mthumb -static $^ 	\
		-Lthird_party/libaeabi-cortexm0/ 		\
		-laeabi-cortexm0 -nostdlib -lc -static -o $@
