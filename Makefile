ifeq (/bin/bash), $(wildcard /bin/bash))
	SHELL=/bin/bash
endif

MAKE            = make
RM              = rm -rf
AS              = $(CROSS_COMPILE)as
LD              = $(CROSS_COMPILE)ld
CC              = $(CROSS_COMPILE)gcc
CPP             = $(CC) -E
AR              = $(CROSS_COMPILE)ar
ARFLAGS         = -rcsu
NM              = $(CROSS_COMPILE)nm
STRIP           = $(CROSS_COMPILE)strip
OBJCOPY         = $(CROSS_COMPILE)objcopy
OBJDUMP         = $(CROSS_COMPILE)objdump
WORKSPACE 	:= $(shell pwd)

SRC_PATH 	:= $(WORKSPACE)/src 

INC_PATH 	:= $(WORKSPACE)/include

CFLAGS  += -Wall -lpthread $(foreach inc, $(INC_PATH), -I$(inc)) 


ifeq ($(strip $(PLATFORM)),)
$(error PLATFORM IS NULL!!!)
endif

SHARE_CFLAGS +=-Wall -fpic -shared -lpthread -lrt $(foreach inc, $(INC_PATH), -I$(inc))


OUT_PATH = $(WORKSPACE)/release
AUTO_SUBVERSION_FILE_NAME := $(INC_PATH)/auto_modsubver.h


include $(WORKSPACE)/autosubver.mk
check-function = \
	$(shell mkdir -p $(OUT_PATH)/{lib,exe}/$(PLATFORM)/) \

SRCS    :=      $(foreach cf, $(SRC_PATH), $(wildcard $(cf)/*.c))
OBJS    :=      $(SRCS:.c=.o)

MBNET_STATIC = libmbnet.a
MBNET_SHARE  = libmbnet.so
MBNET_BIN   = mbnet_demo
MBNET_DEBUG   = mbnet_debug

BIN_CFLAGS +=  -lpthread  -I$(WORKSPACE)/include -L$(OUT_PATH)/lib/$(PLATFORM)/ -lmbnet  

all:$(MBNET_STATIC) $(MBNET_SHARE) $(MBNET_BIN) $(MBNET_DEBUG)
static:$(MBNET_STATIC)
share :$(MBNET_SHARE)
bin :$(MBNET_BIN)
debug :$(MBNET_DEBUG)


$(MBNET_STATIC):VER $(OBJS)
	$(AR) $(ARFLAGS) $(OUT_PATH)/lib/$(PLATFORM)/$(MBNET_STATIC) $(OBJS)

$(MBNET_SHARE):$(SRCS) VER
	$(CC) $(SHARE_CFLAGS) $(SRCS) -o $(OUT_PATH)/lib/$(PLATFORM)/$@
	$(STRIP) $(OUT_PATH)/lib/$(PLATFORM)/$@

$(MBNET_BIN):$(MBNET_SHARE)
	$(CC)  $(BIN_CFLAGS) $(WORKSPACE)/src/demo.c -o $(OUT_PATH)/exe/$(PLATFORM)/$@

$(MBNET_DEBUG):
	$(CC)  $(WORKSPACE)/debug/mbnet_debug.c -o $(OUT_PATH)/exe/$(PLATFORM)/$@

VER:
#	$(call check-function)
	$(call autogenerate_modsubversion, $(AUTO_SUBVERSION_FILE_NAME)) 
clean:
	-$(RM) $(WORKSPACE)/src/*.o  $(INC_PATH)/auto_modsubver.h $(WORKSPACE)/release

.PHONY: clean VER 

