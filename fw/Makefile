# make OPT=-O3

##### Project #####

PROJECT			?= app
# The path for generated files
BUILD_DIR		= build

##### Options #####

# Use LL library instead of HAL, y:yes, n:no
USE_LL_LIB ?= n
# Enable printf float %f support, y:yes, n:no
ENABLE_PRINTF_FLOAT	?= n
# Programmer, jlink or pyocd
FLASH_PROGRM	?= pyocd

##### Toolchains #######

ARM_TOOCHAIN	?= ~/tools/xpack-arm-none-eabi-gcc-14.2.1-1.1/bin

# path to JLinkExe
JLINKEXE		?= /opt/SEGGER/JLink/JLinkExe
# path to PyOCD
PYOCD_EXE		?= pyocd
PYOCD_DEVICE	?= stm32g030f6px

##### Paths ############

# C and CPP source folders
CDIRS		:= src 
# Single C and CPP source files
CFILES		:= 
CPPFILES	:= 

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
XX			= $(PREFIX)g++
AS			= $(PREFIX)as
LD			= $(PREFIX)ld
OBJCOPY		= $(PREFIX)objcopy
# `$(shell pwd)` or `.`, both works
TOP			= .
BDIR		?= $(TOP)/$(BUILD_DIR)

# For each direcotry, add it to csources
CSOURCES 	:= $(foreach dir, $(CDIRS), $(shell find $(TOP)/$(dir) -maxdepth 1 -name '*.c'))
CSOURCES	:= $(filter-out %_template.c, $(CSOURCES))
# Add single c source files to csources
CSOURCES 	+= $(addprefix $(TOP)/, $(CFILES))
# C++ files
CPPSOURCES	:= $(foreach dir, $(CDIRS), $(shell find $(TOP)/$(dir) -maxdepth 1 -name '*.cpp'))
CPPSOURCES 	+= $(addprefix $(TOP)/, $(CPPFILES))

# Then assembly source folders and files
ASOURCES := $(foreach dir, $(ADIRS), $(shell find $(TOP)/$(dir) -maxdepth 1 -name '*.s'))
ASOURCES += $(addprefix $(TOP)/, $(AFILES))

# Fill object files with c and asm files (keep source directory structure)
OBJS = $(CSOURCES:$(TOP)/%.c=$(BDIR)/%.o)
OBJS += $(CPPSOURCES:$(TOP)/%.cpp=$(BDIR)/%.o)
OBJS += $(ASOURCES:$(TOP)/%.s=$(BDIR)/%.o)
# d files for detecting h file changes
DEPS=$(CSOURCES:$(TOP)/%.c=$(BDIR)/%.d)

# Arch and target specified flags
ARCH_FLAGS	:= -mthumb -mcpu=cortex-m0plus -DSTM32G0 -DSTM32G030xx
# Debug options, -gdwarf-2 for debug, -g0 for release 
# https://gcc.gnu.org/onlinedocs/gcc-12.2.0/gcc/Debugging-Options.html
#  -g: system’s native format, -g0:off, -g/g1,-g2,-g3 -> more verbosely
#  -ggdb: for gdb, -ggdb0:off, -ggdb/ggdb1,-ggdb2,-ggdb3 -> more verbosely
#  -gdwarf: in DWARF format, -gdwarf-2,-gdwarf-3,-gdwarf-4,-gdwarf-5
DEBUG_FLAGS ?= -gdwarf-3 -fstack-usage

OPT			?= -Og

ifeq ($(RELEASE),1)
OPT = -O2 -DRELEASE
endif

# C flags
TGT_CFLAGS		?= $(ARCH_FLAGS) $(DEBUG_FLAGS) $(OPT) -std=c99 -Wall -ffunction-sections -fdata-sections
# C++ flags
TGT_CPPFLAGS	?= $(ARCH_FLAGS) $(DEBUG_FLAGS) $(OPT) -std=c++11 -Wall -ffunction-sections -fdata-sections
# ASM flags
TGT_ASFLAGS		?= $(ARCH_FLAGS) $(DEBUG_FLAGS) $(OPT) -Wa,--warn
# LD flags
TGT_LDFLAGS		?= $(ARCH_FLAGS) -specs=nano.specs -static -lc -lm \
				-Wl,-Map=$(BDIR)/$(PROJECT).map \
				-Wl,--gc-sections \
				-Wl,--print-memory-usage

GCC_VERSION := $(shell $(CC) -dumpversion)
IS_GCC_ABOVE_12 := $(shell expr "$(GCC_VERSION)" ">=" "12")
ifeq "$(IS_GCC_ABOVE_12)" "1"
    TGT_LDFLAGS += -Wl,--no-warn-rwx-segments
endif

ifeq ($(ENABLE_PRINTF_FLOAT),y)
TGT_LDFLAGS	+= -u _printf_float
endif

# include paths
TGT_INCFLAGS := $(addprefix -I $(TOP)/, $(INCLUDES))


.PHONY: all clean flash echo debug

all: $(BDIR)/$(PROJECT).elf $(BDIR)/$(PROJECT).bin $(BDIR)/$(PROJECT).hex

# for debug
echo:
	$(info 1. $(AFILES))
	$(info 2. $(ASOURCES))
	$(info 3. $(CSOURCES))
	$(info 4. $(OBJS))
	$(info 5. $(TGT_INCFLAGS))

# include d files without non-exist warning
-include $(DEPS)

$(BDIR)/%.o: %.c
	@printf "  CC\t$<\n"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(TGT_CFLAGS) $(TGT_INCFLAGS) -MT $@ -o $@ -c $< -MD -MF $(BDIR)/$*.d -MP

$(BDIR)/%.o: %.cpp
	@printf "  XX\t$<\n"
	@mkdir -p $(dir $@)
	$(Q)$(XX) $(TGT_CPPFLAGS) $(TGT_INCFLAGS) -MT $@ -o $@ -c $< -MD -MF $(BDIR)/$*.d -MP

# Compile asm to obj
$(BDIR)/%.o: %.s
	@printf "  AS\t$<\n"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(TGT_ASFLAGS) -o $@ -c $<

# Link object files to elf
$(BDIR)/$(PROJECT).elf: $(OBJS) $(TOP)/$(LDSCRIPT)
	@printf "  LD\t$(LDSCRIPT) -> $@\n"
	$(Q)$(CC) $(TGT_LDFLAGS) -T$(TOP)/$(LDSCRIPT) $(OBJS) -o $@

# Convert elf to bin
%.bin: %.elf
	@printf "  OBJCP BIN\t$@\n"
	$(Q)$(OBJCOPY) -I elf32-littlearm -O binary  $< $@

# Convert elf to hex
%.hex: %.elf
	@printf "  OBJCP HEX\t$@\n"
	$(Q)$(OBJCOPY) -I elf32-littlearm -O ihex  $< $@

clean:
	rm -rf $(BDIR)/*

flash: all
	$(PYOCD_EXE) cmd -v -c 'erase; load $(BDIR)/$(PROJECT).hex; reset' -t $(PYOCD_DEVICE) --config $(TOP)/base/pyocd.yml
