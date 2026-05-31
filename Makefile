
TARGET = retroviz
OBJS = src/main.o
 
CFLAGS = -O2 -G0 -Wall -fomit-frame-pointer
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
 
LIBDIR =
LDFLAGS =
LIBS = -lm -lpspgu -lpspgum -lpspaudiolib -lpspaudio -lpspctrl -lpspkernel -lpsprtc
 
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = RetroViz - PSP Visualizer
PSP_EBOOT_SFO = PARAM.SFO
 
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
