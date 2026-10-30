#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPRO)/libogc2/gamecube_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
TARGET		:=	swiss
BUILD		:=	build
SOURCES		:=  source \
              source/aram \
			  source/gui \
			  source/config \
              source/devices \
              source/devices/system \
			  source/devices/aram \
              source/devices/dvd \
              source/devices/fat \
			  source/devices/flippydrive \
			  source/devices/fsp \
			  source/devices/ftp \
			  source/devices/gcloader \
              source/devices/kunaigc \
              source/devices/memcard \
              source/devices/qoob \
			  source/devices/smb \
			  source/devices/usbgecko \
              source/devices/wode \
			  source/devices/wiikeyfusion \
              source/cheats \
              source/patches \
			  source/patches/dvd \
			  source/patches/gx \
			  source/patches/mtx \
			  source/patches/os \
			  source/patches/vi \
              source/images \
			  source/images/buttons \
			  source/fatfs \
			  source/littlefs
DATA		:=	data  
TEXTURES	:=	source/images \
              source/images/buttons
INCLUDES	:=	include $(SOURCES) .. aesnd/libogc2/include

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------

# UI_PERF=1 builds the frame-time and GPU-counter overlay (gui/ui_perf.h).
UI_PERF		?= 0
# Application C is optimized for size without changing floating-point or aliasing
# policy. It needs no unwind tables; crash reports walk the stack back chain.
# Prebuilt dependencies retain their own exception metadata and compiler flags.
# Source paths are relative so the DOL is the same wherever it is built.
CFLAGS		= -g -Os -Wall -Wno-format-truncation -Wno-scalar-storage-order -fno-strict-aliasing -fno-asynchronous-unwind-tables -fno-unwind-tables -ffile-prefix-map=$(abspath $(CURDIR)/..)/= -D_GNU_SOURCE $(MACHDEP) $(INCLUDE) $(shell $(PREFIX)pkg-config --cflags libdeflate iconv mad uchardet libxxhash zlib) -D'GIT_COMMIT="$(shell git rev-parse HEAD | cut -c1-7)"' -D'GIT_REVISION="$(shell git rev-list HEAD | wc -l)"' -DLFS_MULTIVERSION -DUI_PERF_CAPTURE=$(UI_PERF)
CXXFLAGS	= $(CFLAGS)

ASFLAGS		= $(MACHDEP) -mregnames -D_LANGUAGE_ASSEMBLY $(INCLUDE)
LDFLAGS		= $(CFLAGS) -Wl,-Map,$(notdir $@).map -T ../../linker-script/ogc.ld -T ../../reservedarea.ld

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project
#---------------------------------------------------------------------------------
LIBS	:=	-ldvm -ltinysmb -lbba -ldb -logc -lm -lpsoarchive $(shell $(PREFIX)pkg-config --libs libdeflate iconv mad uchardet libxxhash zlib)

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=	$(abspath ../../.build-deps/libiconv-lite) $(PORTLIBS)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
					$(foreach dir,$(TEXTURES),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# automatically build a list of object files for our project
#---------------------------------------------------------------------------------
# Each directory's files in byte order: wildcard follows the locale, and the
# link order decides the DOL's bytes.
CFILES		:=	$(foreach dir,$(SOURCES),$(sort $(notdir $(wildcard $(dir)/*.c))))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(sort $(notdir $(wildcard $(dir)/*.cpp))))
sFILES		:=	$(foreach dir,$(SOURCES),$(sort $(notdir $(wildcard $(dir)/*.s))))
SFILES		:=	$(foreach dir,$(SOURCES),$(sort $(notdir $(wildcard $(dir)/*.S))))
BINFILES	:=	$(foreach dir,$(DATA),$(sort $(notdir $(wildcard $(dir)/*.*))))
SCFFILES	:=	$(foreach dir,$(TEXTURES),$(sort $(notdir $(wildcard $(dir)/*.scf))))
TPLFILES	:=	$(SCFFILES:.scf=.tpl)

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) $(addsuffix .o,$(TPLFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(sFILES:.s=.o) $(SFILES:.S=.o)
# libogc2's AESND, from aesnd/ with Indigo's patches rather than -laesnd
export OFILES_AESND := aesndlib.o aesndmp3player.o
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES) $(OFILES_AESND)

export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES))) $(addsuffix .h,$(subst .,_,$(TPLFILES))) tags.h

#---------------------------------------------------------------------------------
# build a list of include paths
#---------------------------------------------------------------------------------
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I$(LIBOGC_INC)

#---------------------------------------------------------------------------------
# build a list of library paths
#---------------------------------------------------------------------------------
export LIBPATHS	:=	-L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export OUTPUT	:=	$(CURDIR)/$(TARGET)
.PHONY: $(BUILD) clean

#---------------------------------------------------------------------------------
$(BUILD):
	@echo Current Directory $(CURDIR)
	@echo GIT Revision $(shell git rev-parse --short HEAD)
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol

#---------------------------------------------------------------------------------
run:
	wiiload $(TARGET).dol

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

# Keep the real-time audio mixer and decoder interface at their previous policy.
# Their source and patches are unchanged by this size optimization.
$(OFILES_AESND): CFLAGS += -O2

$(OFILES_SOURCES) : $(HFILES)

#---------------------------------------------------------------------------------
# This rule links in binary data with the .tpl extension
#---------------------------------------------------------------------------------
%.tpl.o %_tpl.h : %.tpl
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

#---------------------------------------------------------------------------------
tags.h :
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $@)
	$(SILENTCMD): > $@  # the tags load_auto_dol would skip: it never runs in Indigo

#---------------------------------------------------------------------------------
# libogc2's AESND: the copy in aesnd/libogc2, with the patches beside it
# applied in order (aesnd/README.md), and its DSP code assembled from source
#---------------------------------------------------------------------------------
AESND	:=	$(CURDIR)/../aesnd

aesndlib.c : $(AESND)/libogc2/libaesnd/aesndlib.c $(sort $(wildcard $(AESND)/*.patch))
	$(SILENTMSG) $(notdir $@)
	$(SILENTCMD)cp $< $@ && for p in $(filter %.patch,$^); do patch -s -F0 $@ $$p || exit 1; done

aesndmp3player.c : $(AESND)/libogc2/libaesnd/aesndmp3player.c
	$(SILENTCMD)cp $< $@

aesnddspmixer.h : $(AESND)/libogc2/libaesnd/dspcode/dspmixer.s
	$(SILENTMSG) $(notdir $@)
	$(SILENTCMD)$(DEVKITPRO)/tools/bin/gcdsptool -c $< -o $@ > /dev/null

aesndlib.o : aesnddspmixer.h

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
