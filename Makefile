TARGET = retroviz
OBJS = src/main.o

PSPSDK=$(shell psp-config --pspsdk-path)
PSPDEV=$(shell psp-config --pspdev-path)

CC = psp-gcc
CFLAGS = -O2 -G0 -Wall -D_PSP_FW_VERSION=600 \
         -I$(PSPSDK)/include \
         -I$(PSPDEV)/psp/include

LDFLAGS = -L$(PSPSDK)/lib \
          -L$(PSPDEV)/psp/lib \
          -Wl,-zmax-page-size=128

LIBS = -lm -lpspgu -lpspctrl -lpsprtc -lpsppower -lpspdisplay -lpspkernel

all: $(TARGET).elf EBOOT.PBP

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

EBOOT.PBP: $(TARGET).elf
	mksfoex -d MEMSIZE=0 'RetroViz' PARAM.SFO
	psp-strip $(TARGET).elf -o $(TARGET)_strip.elf
	pack-pbp EBOOT.PBP PARAM.SFO NULL NULL NULL NULL NULL $(TARGET)_strip.elf NULL
	rm -f $(TARGET)_strip.elf

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET)_strip.elf EBOOT.PBP PARAM.SFO
