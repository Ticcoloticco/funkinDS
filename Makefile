#---------------------------------------------------------------------------------
# Funkin DS - devkitARM makefile
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules

TARGET		:=	FunkinDS
BUILD		:=	build
SOURCES		:=	source
DATA		:=
INCLUDES	:=	include
MUSIC		:=	maxmod_data
NITRODATA	:=	nitrofiles

GAME_TITLE	:=	Friday Night Funkin DS
GAME_SUBTITLE1	:=	Unofficial fan port
GAME_SUBTITLE2	:=	Nintendo DS
GAME_ICON	:=	$(if $(wildcard $(TOPDIR)/build_tools/icon.bmp),$(TOPDIR)/build_tools/icon.bmp,$(TOPDIR)/icon.bmp)

ARCH		:=	-march=armv5te -mtune=arm946e-s -mthumb

CFLAGS		:=	-g -Wall -O2 -ffunction-sections -fdata-sections -ffast-math \
			$(ARCH)
CFLAGS		+=	$(INCLUDE) -DARM9 $(EXTRA_CFLAGS)
CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS		:=	-g $(ARCH)
LDFLAGS		=	-specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS		:=	-lfat -lmm9 -lnds9 -lm
LIBDIRS		:=	$(LIBNDS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export TOPDIR	:=	$(CURDIR)
export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

ifneq ($(strip $(NITRODATA)),)
	export NITRO_FILES	:=	$(CURDIR)/$(NITRODATA)
endif

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*))) soundbank.bin
export AUDIOFILES	:=	$(foreach dir,$(notdir $(wildcard $(MUSIC)/*.*)),$(CURDIR)/$(MUSIC)/$(dir))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES	:=	$(OFILES_BIN) $(OFILES_SOURCES)
export HFILES	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds

else

$(OUTPUT).nds	:	$(OUTPUT).elf
ifneq ($(strip $(NITRODATA)),)
$(OUTPUT).nds	:	$(shell find $(TOPDIR)/$(NITRODATA))
endif
$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SOURCES) : $(HFILES)

soundbank.bin soundbank.h : $(AUDIOFILES)
	@mmutil $^ -d -osoundbank.bin -hsoundbank.h

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

endif
