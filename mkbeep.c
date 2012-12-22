#include <stdio.h>
#include <math.h>

int
main (int argc, char **argv)
{
	FILE *outf;
	double sample_rate, freq1, freq2, amp, t;
	int val;

	outf = fopen ("beep.raw", "w");
	sample_rate = 44100.0;
	freq1 = 440;
	freq2 = freq1 * 3 / 2;
	amp = 10000;

	for (t = 0; t < 1; t += 1/sample_rate) {
		val = 0;

		val += amp * sin (t * freq1 * 2 * M_PI);
		val += amp * sin (t * freq2 * 2 * M_PI);
		putc (val, outf);
		putc (val >> 8, outf);
	}

	return (0);
}

	

	
