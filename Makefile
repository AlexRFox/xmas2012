PKGS = gtk+-3.0 libpulse libpulse-mainloop-glib fftw3

CFLAGS = -g -Wall `pkg-config --cflags $(PKGS)`
LIBS = `pkg-config --libs $(PKGS)` -lm

all: pulsedevicelist pa-beep mkbeep bidding

pkgs:
	apt-get install libpulse-dev libfftw3-dev libsndfile1-dev


bidding: bidding.o
	$(CC) $(CFLAGS) -o bidding bidding.o $(LIBS)

pulsedevicelist: pulsedevicelist.o
	$(CC) $(CFLAGS) -o pulsedevicelist pulsedevicelist.o $(LIBS)

pa-beep: pa-beep.o
	$(CC) $(CFLAGS) -o pa-beep pa-beep.o $(LIBS)

mkbeep: mkbeep.o
	$(CC) $(CFLAGS) -o mkbeep mkbeep.o $(LIBS)

