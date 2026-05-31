TARGET = retroviz
OBJS = src/main.o
 
CFLAGS = -O2 -G0 -Wall
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
 
LIBDIR =
LDFLAGS =
LIBS = -lm -lpspgu -lpspctrl -lpsprtc -lpsppower -lpspkernel
 
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = RetroViz
PSP_LARGE_MEMORY = 0
USE_PSPSDK_NET = 0
PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
