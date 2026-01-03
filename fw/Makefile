# make RELEASE=1

##### Project #####

PROJECT			?= app
BUILD_DIR		= build

##### Options #####

# Enable printf float %f support, y:yes, n:no
ENABLE_PRINTF_FLOAT	?= n

##### Toolchains #######

ARM_TOOCHAIN	?= ~/tools/xpack-arm-none-eabi-gcc-14.2.1-1.1/bin
PYOCD_EXE		?= pyocd
PYOCD_DEVICE	?= stm32g030f6px

##### Paths ############

# C source folders
CDIRS		:= src 
# Single C source files
CFILES		:= 

CDIRS     += base/STM32CubeG0/Drivers/STM32G0xx_HAL_Driver/Src
INCLUDES  += base/STM32CubeG0/Drivers/STM32G0xx_HAL_Driver/Inc

# ASM source folders
ADIRS		:= src
# Single ASM source files
AFILES		:= 

# Include paths
INCLUDES	:= \
  base/STM32CubeG0/Drivers/STM32G0xx_HAL_Driver/Inc \
  base/STM32CubeG0/Drivers/CMSIS/Device/ST/STM32G0xx/Include \
  base/STM32CubeG0/Drivers/CMSIS/Include \
  ./src

##### Library Paths ############

LDSCRIPT		= base/stm32g030.ld


CFILES	+= base/STM32CubeG0/Drivers/CMSIS/Device/ST/STM32G0xx/Source/Templates/system_stm32g0xx.c
AFILES	+= base/STM32CubeG0/Drivers/CMSIS/Device/ST/STM32G0xx/Source/Templates/gcc/startup_stm32g030xx.s

# 'make V=1' will show all compiler calls.
V		?= 0
ifeq ($(V),0)
Q		:= @
NULL	:= 2>/dev/null
endif

PREFIX		?= $(ARM_TOOCHAIN)/arm-none-eabi-
CC			= $(PREFIX)gcc
AS			= $(PREFIX)as
LD			= $(PREFIX)ld
OBJCOPY		= $(PREFIX)objcopy
TOP			= .
BDIR		?= $(TOP)/$(BUILD_DIR)

CSOURCES 	:= $(foreach dir, $(CDIRS), $(shell find $(TOP)/$(dir) -maxdepth 1 -name '*.c'))
CSOURCES	:= $(filter-out %_template.c, $(CSOURCES))
CSOURCES 	+= $(addprefix $(TOP)/, $(CFILES))

ASOURCES := $(foreach dir, $(ADIRS), $(shell find $(TOP)/$(dir) -maxdepth 1 -name '*.s'))
ASOURCES += $(addprefix $(TOP)/, $(AFILES))

OBJS = $(CSOURCES:$(TOP)/%.c=$(BDIR)/%.o)
OBJS += $(ASOURCES:$(TOP)/%.s=$(BDIR)/%.o)
# d files for detecting h file changes
DEPS = $(CSOURCES:$(TOP)/%.c=$(BDIR)/%.d)

# Arch and target specified flags
ARCH_FLAGS	:= -mthumb -mcpu=cortex-m0plus -DSTM32G0 -DSTM32G030xx
# Debug options, -gdwarf-2 for debug, -g0 for release 
# https://gcc.gnu.org/onlinedocs/gcc-12.2.0/gcc/Debugging-Options.html
DEBUG_FLAGS ?= -gdwarf-3 -fstack-usage

OPT			?= -Og

ifeq ($(RELEASE),1)
OPT = -O2 -DRELEASE
endif

CFLAGS  ?= $(ARCH_FLAGS) $(DEBUG_FLAGS) $(OPT) \
  -std=c99 -Wall -ffunction-sections -fdata-sections \
  $(addprefix -I $(TOP)/, $(INCLUDES))
ASFLAGS ?= $(ARCH_FLAGS) $(DEBUG_FLAGS) $(OPT) -Wa,--warn
LDFLAGS ?= $(ARCH_FLAGS) -specs=nano.specs -static -lc -lm \
  -Wl,-Map=$(BDIR)/$(PROJECT).map \
  -Wl,--gc-sections \
  -Wl,--print-memory-usage

IS_GCC_ABOVE_12 := $(shell expr "$(shell $(CC) -dumpversion)" ">=" "12")
ifeq "$(IS_GCC_ABOVE_12)" "1"
    LDFLAGS += -Wl,--no-warn-rwx-segments
endif

ifeq ($(ENABLE_PRINTF_FLOAT),y)
LDFLAGS	+= -u _printf_float
endif


.PHONY: all clean flash echo debug debug-server

all: $(BDIR)/$(PROJECT).elf $(BDIR)/$(PROJECT).bin

echo:
	$(info AFILES:    $(AFILES))
	$(info ASOURCES:  $(ASOURCES))
	$(info CSOURCES:  $(CSOURCES))
	$(info OBJS:      $(OBJS))
	$(info CFLAGS:    $(CFLAGS))

# include d files without non-exist warning
-include $(DEPS)

$(BDIR)/%.o: %.c
	@printf "  CC\t$<\n"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(INCFLAGS) -MT $@ -o $@ -c $< -MD -MF $(BDIR)/$*.d -MP

$(BDIR)/%.o: %.s
	@printf "  AS\t$<\n"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(ASFLAGS) -o $@ -c $<

$(BDIR)/$(PROJECT).elf: $(OBJS) $(TOP)/$(LDSCRIPT)
	@printf "  LD\t$(LDSCRIPT) -> $@\n"
	$(Q)$(CC) $(LDFLAGS) -T$(TOP)/$(LDSCRIPT) $(OBJS) -o $@

%.bin: %.elf
	@printf "  OBJCP\t$@\n"
	$(Q)$(OBJCOPY) -I elf32-littlearm -O binary $< $@

clean:
	rm -rf $(BDIR)/*

flash: all
	$(PYOCD_EXE) cmd -v -c 'erase; load $(BDIR)/$(PROJECT).bin; reset' -t $(PYOCD_DEVICE) --config $(TOP)/base/pyocd.yml

debug: all
	# make debug c=1
	@bash base/debug.sh $(BDIR)/$(PROJECT).elf c=$(c)

debug-server:
	$(PYOCD_EXE) gdb --elf $(BDIR)/$(PROJECT).elf -t $(PYOCD_DEVICE) --config $(TOP)/base/pyocd.yml --persist
