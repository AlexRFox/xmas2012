CFLAGS = -g -Wall

all: pulsedevicelist pa-beep

pulsedevicelist: pulsedevicelist.o
	$(CC) $(CFLAGS) -o pulsedevicelist pulsedevicelist.o -lpulse

pa-beep: pa-beep.o
	$(CC) $(CFLAGS) -o pa-beep pa-beep.o -lpulse
