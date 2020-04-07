PWD := $(shell pwd)
COMPONENT_DIR := $(abspath $(PWD)/..)
CSS := $(abspath $(PWD)/../../../../../)
SCRIPTS := $(PWD)/scripts

VER_CHECKER := $(SCRIPTS)/ver_checker/ver_checker.py
REV_CHECKER := $(SCRIPTS)/rev_checker.sh
HEX2BIN     := $(SCRIPTS)/cdn_hex2bin.sh

####################################
# Part connected with srcgen target #
####################################

# Path to files in XDP work
TGTDIR_ABS  := $(abspath $(SRCGEN_DIR))
TGTDIR_CORE := $(TGTDIR_ABS)/core_driver
TGTDIR_SRC  := $(TGTDIR_CORE)/src
TGTDIR_INC  := $(TGTDIR_CORE)/include
TGTDIR_CMN  := $(TGTDIR_CORE)/common
TGTDIR_ASM  := $(TGTDIR_ABS)/asm
TGTDIR_DOC  := $(TGTDIR_ABS)/doc

# Paths to needed files
SRC_DIR := $(COMPONENT_DIR)/src
INC_DIR := $(COMPONENT_DIR)/inc
ASM_DIR := $(COMPONENT_DIR)/asm
CPS_DIR := $(CSS)/firmware/include
DOC_DIR := $(COMPONENT_DIR)/doc

# List of files needed to compile binary
CPS_FILES := cdn_errno.h \
			 cdn_assert.h \
			 cdn_inttypes.h \
			 cdn_log.h \
			 cdn_stdint.h \
			 cdn_stdtypes.h

GEN_SRC    := $(subst $(SRC_DIR), $(TGTDIR_SRC), $(wildcard $(SRC_DIR)/*.c))
GEN_INC    := $(subst $(INC_DIR), $(TGTDIR_INC), $(wildcard $(INC_DIR)/*.h))
GEN_ASM    := $(subst $(ASM_DIR), $(TGTDIR_ASM), $(wildcard $(ASM_DIR)/*.S))
GEN_CPS    := $(addprefix $(TGTDIR_CMN)/, $(CPS_FILES))
GEN_README := $(TGTDIR_DOC)/README.txt
GEN_RNOTES := $(TGTDIR_DOC)/release_notes.txt

GEN_DIR := $(TGTDIR_SRC) $(TGTDIR_INC) $(TGTDIR_CMN) $(TGTDIR_ASM) $(TGTDIR_DOC)

SRCGEN_DEPS := $(GEN_SRC) $(GEN_INC) $(GEN_CPS) $(GEN_ASM)

ifeq ($(GEN_DOC),1)
SRCGEN_DEPS += $(GEN_README) \
			   $(GEN_RNOTES)
endif

###################################
# Part connected with bingen part #
###################################

include build.mk

XT_XCC  := xt-xcc
XT_XCPP := xt-xc++
XT_SIZE := xt-size
XT_DUMP := xt-dumpelf

XT_CFLAGS := -c -Os -O2 -Wall -Wextra -Werror -fmessage-length=0 -ipa \
             --xtensa-system=$(XTENSA_SYSTEM) --xtensa-core=$(XTENSA_CORE) \
			 --xtensa-params= -Wno-unused -std=gnu99 -Wl,--defsym

XT_LINKER := $(abspath $(XTENSA_SYSTEM)/../xtensa-elf/lib/min-rt-local)

XT_DUMP_FLAG = $(shell [[ $(*) =~ .*_xt ]] && echo '--xtsc')

BIN_INCS := $(addprefix -I, $(TGTDIR_INC) $(TGTDIR_CMN))

BINARY_NAME ?= mhdp8546
BINDIR_ABS := $(abspath $(BINGEN_DIR))

CDEFS = $(addprefix -D, $(EXTRA_CDEFS) \
						FW_VERSION=$(FW_VER) \
						REVISION_NUM=$(REV_NUM))

GEN_OBJ  := $(sort $(subst $(TGTDIR_SRC), $(BINDIR_ABS), $(GEN_SRC:.c=.o)))

EXEC_CFLAGS = -Wl,$(GEN_OBJ) -o $(@) -mlsp=$(XT_LINKER) -Wl,-Map -Wl,$(@).map \
  -O2 -Os -Wall -fmessage-length=0 -ipa $(GEN_ASM) \
  --xtensa-system=$(XTENSA_SYSTEM) --xtensa-core=$(XTENSA_CORE) --xtensa-params=

GEN_EXEC := $(BINGEN_DIR)/$(BINARY_NAME)

GEN_DATA := $(addprefix $(BINGEN_DIR)/,$(addsuffix .data, dram0 dram0_xt iram0 iram0_xt))

GEN_BIN := $(BINGEN_DIR)/$(BINARY_NAME).bin

BINGEN_DEPS := binerr rmbin verchk revchk $(GEN_BIN)

.PHONY: srcgen bingen verchk revchk %.data rmbin binerr

ifndef BINGEN_DIR
binerr:
	$(error "Please set directory where binary should be compiled!")
else
binerr:
	
endif

srcgen: $(SRCGEN_DEPS)

bingen: $(BINGEN_DEPS)

revchk:
	$(call CHECK_REVISION)

verchk:
	$(call CHECK_VERSION)

rmbin:
	@rm -rf $(BINGEN_DIR)/$(BINARY_NAME)

$(GEN_BIN): $(GEN_DATA)
	$(call CONVERT_HEX2BIN,iram0)
	$(call CONVERT_HEX2BIN,dram0)

$(BINGEN_DIR)/%.data: $(GEN_EXEC)
	$(eval DATA_UC := $(shell echo $(*) | tr '[a-z]' '[A-Z]'))
	$(XT_DUMP) --base=$(MEMORY_BASE) --width $(MEMORY_WIDTH) --full \
	  --size $(MEMORY_SIZE_KB) $(XT_DUMP_FLAG) $(^) > $(@)

$(BINDIR_ABS)/%.o: $(TGTDIR_SRC)/%.c
	$(XT_XCC) $(XT_CFLAGS) $(CDEFS) $(BIN_INCS) $(^) -o $(@)

$(GEN_EXEC): $(GEN_OBJ)
	$(XT_XCPP) $(EXEC_CFLAGS)
	$(XT_SIZE) -C $(@)

$(TGTDIR_SRC)/%: $(SRC_DIR)/% | $(TGTDIR_SRC)
	$(COPY_FILE)

$(TGTDIR_INC)/%: $(INC_DIR)/% | $(TGTDIR_INC)
	$(COPY_FILE)

$(TGTDIR_CMN)/%: $(CPS_DIR)/% | $(TGTDIR_CMN)
	$(COPY_FILE)

$(TGTDIR_ASM)/%: $(ASM_DIR)/% | $(TGTDIR_ASM)
	$(COPY_FILE)

$(TGTDIR_DOC)/%: $(DOC_DIR)/% | $(TGTDIR_DOC)
	$(COPY_FILE)

# Create directories in XDP
$(GEN_DIR):
	mkdir -p $(@)

# Used defines
define COPY_FILE
	@cp -v $(<) $(@)
endef

define CHECK_VERSION
    $(eval SPACE := " ")
    $(eval RESULT := $(shell python3 $(VER_CHECKER) \
      --version=$(FIRMWARE_VER) --mask 10.100.100 2>&1; echo $(SPACE)$$?))
	$(eval ERR_CODE := $(lastword $(RESULT)))
	$(eval FW_VER ?= $(firstword $(RESULT)))
endef

define CHECK_REVISION
	$(eval REV_NUM ?= $(shell bash $(REV_CHECKER) $(CSS)))
endef

define CONVERT_HEX2BIN
	bash $(HEX2BIN) $(BINGEN_DIR)/$(1).data $(BINGEN_DIR)/$(1).bin
	cat $(BINGEN_DIR)/$(1).bin >> $(@)
endef
