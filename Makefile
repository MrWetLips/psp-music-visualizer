TARGET = retroviz
 
OBJS = src/main.o
 
# FIX: добавлен -G0 обязательно, убрали лишние флаги
CFLAGS = -O2 -G0 -Wall -ffast-math
 
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
 
ASFLAGS = $(CFLAGS)
 
LIBDIR =
 
LDFLAGS =
 
# FIX: правильный порядок библиотек для линковки
LIBS = -lpspaudiolib -lpspaudio -lpspgu -lpspdisplay -lpspctrl -lpsprtc -lpspkernel -lm
 
EXTRA_TARGETS = EBOOT.PBP
 
PSP_EBOOT_TITLE = RetroViz
 
PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
