# ── SCO Firmware Makefile ──────────────────────────────────────────────────
# STM32G474VET6 — arm-none-eabi-gcc toolchain
#
# Prerequisites:
#   - ARM GCC toolchain: https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain
#   - STM32CubeG4 HAL/LL drivers (place in Drivers/)
#   - STM32 USB Device Library (place in Middlewares/)
#
# Usage:
#   make              — build firmware
#   make flash        — flash via ST-Link (requires st-flash or openocd)
#   make clean        — remove build artifacts

# ── Toolchain ─────────────────────────────────────────────────────────────
CROSS   ?= arm-none-eabi-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
SIZE    = $(CROSS)size
GDB     = $(CROSS)gdb

# ── MCU ────────────────────────────────────────────────────────────────────
MCU      = STM32G474VETx
CPU      = cortex-m4
FPU      = fpv4-sp-d16
FLOAT_ABI = hard

# ── Directories ────────────────────────────────────────────────────────────
BUILD_DIR = build
CORE_SRC  = Core/Src
CORE_INC  = Core/Inc
DRV_SRC   = Drivers/STM32G4xx_HAL_Driver/Src
DRV_INC   = Drivers/STM32G4xx_HAL_Driver/Inc
CMSIS_INC = Drivers/CMSIS/Device/ST/STM32G4xx/Include
CMSIS_CORE_INC = Drivers/CMSIS/Include
USB_SRC   = Middlewares/ST/STM32_USB_Device_Library/Core/Src
USB_INC   = Middlewares/ST/STM32_USB_Device_Library/Core/Inc
USB_CDC_SRC = Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src
USB_CDC_INC = Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc

# ── Sources ────────────────────────────────────────────────────────────────
C_SOURCES = \
    $(CORE_SRC)/main.c \
    $(CORE_SRC)/dds.c \
    $(CORE_SRC)/scope.c \
    $(CORE_SRC)/protocol.c \
    $(CORE_SRC)/usbd_cdc_if.c \
    $(CORE_SRC)/stm32g4xx_it.c \
    $(CORE_SRC)/stm32g4xx_hal_msp.c \
    $(CORE_SRC)/usb_device.c \
    $(DRV_SRC)/stm32g4xx_hal.c \
    $(DRV_SRC)/stm32g4xx_hal_rcc.c \
    $(DRV_SRC)/stm32g4xx_hal_gpio.c \
    $(DRV_SRC)/stm32g4xx_hal_dma.c \
    $(DRV_SRC)/stm32g4xx_hal_dma_ex.c \
    $(DRV_SRC)/stm32g4xx_hal_dac.c \
    $(DRV_SRC)/stm32g4xx_hal_dac_ex.c \
    $(DRV_SRC)/stm32g4xx_hal_adc.c \
    $(DRV_SRC)/stm32g4xx_hal_adc_ex.c \
    $(DRV_SRC)/stm32g4xx_hal_tim.c \
    $(DRV_SRC)/stm32g4xx_hal_tim_ex.c \
    $(DRV_SRC)/stm32g4xx_hal_cortex.c \
    $(DRV_SRC)/stm32g4xx_hal_pwr.c \
    $(DRV_SRC)/stm32g4xx_hal_pwr_ex.c \
    $(DRV_SRC)/stm32g4xx_hal_flash.c \
    $(DRV_SRC)/stm32g4xx_hal_flash_ex.c \
    $(DRV_SRC)/stm32g4xx_hal_pcd.c \
    $(DRV_SRC)/stm32g4xx_hal_pcd_ex.c \
    $(USB_SRC)/usbd_core.c \
    $(USB_SRC)/usbd_ctlreq.c \
    $(USB_SRC)/usbd_ioreq.c \
    $(USB_CDC_SRC)/usbd_cdc.c \

ASM_SOURCES = \
    startup_stm32g474xx.s

# ── Includes ───────────────────────────────────────────────────────────────
INCLUDES = \
    -I$(CORE_INC) \
    -I$(DRV_INC) \
    -I$(CMSIS_INC) \
    -I$(CMSIS_CORE_INC) \
    -I$(USB_INC) \
    -I$(USB_CDC_INC)

# ── Flags ──────────────────────────────────────────────────────────────────
CFLAGS  = -mcpu=$(CPU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FLOAT_ABI)
CFLAGS += -D$(MCU)
CFLAGS += -DUSE_HAL_DRIVER
CFLAGS += $(INCLUDES)
CFLAGS += -Og -g3
CFLAGS += -Wall -Wextra -Wshadow -Wdouble-promotion
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -std=gnu11

LDFLAGS  = -mcpu=$(CPU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FLOAT_ABI)
LDFLAGS += --specs=nosys.specs --specs=nano.specs
LDFLAGS += -T STM32G474VETx_FLASH.ld
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/firmware.map

# ── Targets ────────────────────────────────────────────────────────────────

OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

TARGET = $(BUILD_DIR)/sco_firmware

.PHONY: all clean flash

all: $(BUILD_DIR) $(TARGET).elf $(TARGET).bin $(TARGET).hex
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo " BUILD SUCCESSFUL"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	$(SIZE) $(TARGET).elf

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET).elf: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Linked: $@"

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC: $<"

$(BUILD_DIR)/%.o: %.s
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "AS: $<"

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@echo "BIN: $@"

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@
	@echo "HEX: $@"

flash: $(TARGET).bin
	@echo "Flashing via ST-Link..."
	st-flash write $(TARGET).bin 0x08000000
	@echo "Flash complete."

# Alternative: flash with OpenOCD
flash-ocd: $(TARGET).elf
	openocd -f interface/stlink.cfg -f target/stm32g4x.cfg \
	        -c "program $(TARGET).elf verify reset exit"

clean:
	rm -rf $(BUILD_DIR)
	@echo "Clean complete."
