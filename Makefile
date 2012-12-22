CFLAGS = -g -Wall

all: pulsedevicelist pa-beep mkbeep

pulsedevicelist: pulsedevicelist.o
	$(CC) $(CFLAGS) -o pulsedevicelist pulsedevicelist.o -lpulse

pa-beep: pa-beep.o
	$(CC) $(CFLAGS) -o pa-beep pa-beep.o -lpulse

mkbeep: mkbeep.o
	$(CC) $(CFLAGS) -o mkbeep mkbeep.o -lm
