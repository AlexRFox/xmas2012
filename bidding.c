#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <fftw3.h>
#include <sndfile.h>

#include <gtk/gtk.h>

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

void setup_signal_processing (void);
void process_utterance (void);

void write_u_dat (void);
void display_u_dat (void);

void write_fft (void);
void display_fft (void);



int vox_state;

struct utterance {
	int avail;
	int used;
	double *samps;
	double *ratios;
	double *env;
	double *denv;
	double *stats;
	double *slope;

	int spec_size;
	double spec_freq_incr;
	double *spec;

	int lookback;
	int trim_end;
};
struct utterance utt;

void print_stats (void);

int freeze;
int armed;

void do_arm (void);
FILE *f_raw;
FILE *f_hpf;
FILE *f_lpf;
FILE *f_energy;
FILE *f_fast;
FILE *f_slow;
FILE *f_noise;
FILE *f_ratio;
double armed_timestamp;

gboolean tick (gpointer data);

void trace_setup (double secs);
void draw_traces (cairo_t *cr, int width, int height);


int sample_rate;
double sample_period;
int samps_per_frame;
void process_data (int16_t const *samps, int nsamps);

double
get_secs (void)
{
	struct timeval tv;
	gettimeofday (&tv, NULL);
	return (tv.tv_sec + tv.tv_usec / 1e6);
}



void
usage (void)
{
	fprintf (stderr, "usage: bidding\n");
	exit (1);
}

pa_glib_mainloop *mainloop;
pa_context *ctx;
pa_stream *rec_stream;
pa_stream *play_stream;

void
rec_state_cb (pa_stream *s, void *userdata)
{
	printf ("rec_state_cb\n");
}

void
rec_data_cb (pa_stream *s, size_t arg1, void *userdata)
{
	const void *data;
	size_t len;
	
	if (pa_stream_peek (s, &data, &len) < 0) {
		printf ("pulseaudio input error\n");
		exit (1);
	}

	process_data ((int16_t const *)data, len / 2);

	pa_stream_drop (s);

	tick (NULL);
}

void
rec_underflow_cb (pa_stream *s, void *userdata)
{
	printf ("rec_underflow_cb\n");
}

void
rec_overflow_cb (pa_stream *s, void *userdata)
{
	printf ("rec_overflow_cb\n");
}

void
setup_record_stream (void)
{
	pa_sample_spec ss;
	pa_buffer_attr bufattr;

	memset (&ss, 0, sizeof ss);
	ss.rate = sample_rate;
	ss.channels = 1;
	ss.format = PA_SAMPLE_S16LE;
	if ((rec_stream = pa_stream_new (ctx, "record", &ss, NULL)) == NULL) {
		printf ("can't create record stream\n");
		exit (1);
	}

	pa_stream_set_state_callback (rec_stream, rec_state_cb, NULL);
	pa_stream_set_read_callback (rec_stream, rec_data_cb, NULL);
	pa_stream_set_underflow_callback (rec_stream, rec_underflow_cb, NULL);
	pa_stream_set_overflow_callback (rec_stream, rec_overflow_cb, NULL);

	memset (&bufattr, 0, sizeof bufattr);
	
	bufattr.maxlength = (uint32_t)-1;
	bufattr.fragsize = pa_usec_to_bytes(250 * 1000, &ss);

	pa_stream_connect_record (rec_stream, NULL, &bufattr,
				  PA_STREAM_INTERPOLATE_TIMING
				  | PA_STREAM_ADJUST_LATENCY
				  | PA_STREAM_AUTO_TIMING_UPDATE);
	
}
void
ctx_state_cb (pa_context *ctx, void *userdata)
{
	switch (pa_context_get_state (ctx)) {
	case PA_CONTEXT_READY:
		printf ("pulseaudio ready\n");
		setup_record_stream ();
		break;
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		printf ("pulseaudio error\n");
		exit (1);
		break;
	default:
		break;
	}
}

void
setup_pulse_audio (char *name)
{
	mainloop = pa_glib_mainloop_new (g_main_context_default ());
	ctx = pa_context_new (pa_glib_mainloop_get_api (mainloop), name);
	pa_context_connect (ctx, NULL, 0, NULL);
	pa_context_set_state_callback (ctx, ctx_state_cb, NULL);
}

int ready_buf_used;
int ready_buf_offset;
int16_t ready_buf[8000 * 10];

void
read_ready_sound (void)
{
	SNDFILE *sf;
	SF_INFO sfinfo;

	memset (&sfinfo, 0, sizeof sfinfo);
	if ((sf = sf_open ("ready.wav", SFM_READ, &sfinfo)) == NULL) {
		fprintf (stderr, "can't open ready.wav");
		exit (1);
	}

	if (sfinfo.samplerate != sample_rate) {
		fprintf (stderr, "ready.wav: bad sample rate (want %d)\n",
			 sample_rate);
		exit (1);
	}

	if ((sfinfo.format & SF_FORMAT_WAV) == 0) {
		fprintf (stderr, "ready.wav: not wav format\n");
		exit (1);
	}

	if ((sfinfo.format & SF_FORMAT_PCM_16) == 0) {
		fprintf (stderr, "ready.wav: not pcm16 format\n");
		exit (1);
	}

	ready_buf_used = sf_read_short (sf,
					ready_buf,
					sizeof ready_buf / sizeof ready_buf[0]);
	if (ready_buf_used <= 0) {
		fprintf (stderr, "ready.wav: can't read data\n");
		exit (1);
	}

	printf ("ready size = %d\n", ready_buf_used);
}

void
play_done_cb (pa_stream *s, int success, void *userdata)
{
	printf ("play done %d\n", success);
}

void
play_cb (pa_stream *s, size_t length, void *userdata)
{
	int thistime;

	thistime = length / 2;
	if (ready_buf_offset + thistime > ready_buf_used)
		thistime = ready_buf_used - ready_buf_offset;
	if (thistime == 0) {
		printf ("play_cb: nothing left\n");
		return;
	}

	pa_stream_write (play_stream, &ready_buf[ready_buf_offset], 
			 thistime * 2, NULL, 0LL, PA_SEEK_RELATIVE);
	ready_buf_offset += thistime;

	if (ready_buf_offset >= ready_buf_used) {
		pa_stream_drain (play_stream, play_done_cb, NULL);
	}
}

void
play_ready (void)
{
	pa_sample_spec ss;

	memset (&ss, 0, sizeof ss);
	ss.rate = sample_rate;
	ss.channels = 1;
	ss.format = PA_SAMPLE_S16LE;
	if ((play_stream = pa_stream_new (ctx, "play ready", &ss, NULL))
	    == NULL) {
		fprintf (stderr, "can't make play_stream\n");
		exit (1);
	}

	ready_buf_offset = 0;
	pa_stream_set_write_callback (play_stream, play_cb, NULL);
	if (pa_stream_connect_playback (play_stream,
					NULL, NULL, 0, NULL, NULL) < 0) {
		fprintf (stderr, "can't connect play_stream\n");
		exit (1);
	}
}

GtkWidget *window;

static gboolean
draw_cb (GtkWidget *widget, cairo_t *cr, gpointer data)
{
	int width, height;

	gtk_window_get_size (GTK_WINDOW(widget), &width, &height);

	draw_traces (cr, width, height);

	gdk_display_sync (gtk_widget_get_display (window));

	return (TRUE);
}

gboolean
key_press_event (GtkWidget *widget, GdkEventKey *ev, gpointer data)
{
	switch (ev->keyval) {
	case 'q': /* covers q or ALT-q */
	case 'c': /* covers CTL-c */
	case 'w': /* covers CTL-w */
	case GDK_KEY_Escape:
		gtk_main_quit ();
		break;
	case 'f':
		freeze ^= 1;
		break;
	case ' ':
		do_arm ();
		break;
	case '/':
		print_stats ();
		break;
	}
	return (TRUE);
}

gboolean
tick (gpointer data)
{
	gtk_widget_queue_draw (window);
	return (TRUE);
}

void
setup_gtk (char *title, int width, int height)
{
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (window), title);

	g_signal_connect (G_OBJECT (window), "destroy",
			  G_CALLBACK (gtk_main_quit), NULL);

	g_signal_connect (G_OBJECT (window), "draw",
			  G_CALLBACK (draw_cb), NULL);

	g_signal_connect (G_OBJECT (window), "key_press_event",
			  G_CALLBACK (key_press_event), NULL);

	gtk_window_set_default_size (GTK_WINDOW (window), width, height);

//	g_timeout_add (10, tick, NULL);

	gtk_widget_show_all (window);
}

double system_start_secs;

char *inname;

void
process_file (char *inname)
{
	FILE *inf;
	int idx;
	double val;

	inf = fopen ("u.dat", "r");
	for (idx = 0; idx < utt.avail; idx++) {
		if (fscanf (inf, "%*f %lf\n", &val) != 1)
			break;
		utt.samps[idx] = val;
	}
	fclose (inf);

	utt.used = idx;

	process_utterance ();
	exit (0);
}

int
main (int argc, char **argv)
{
	int c;

	gtk_init (&argc, &argv);

	while ((c = getopt (argc, argv, "")) != EOF) {
		switch (c) {
		default:
			usage ();
		}
	}

	if (optind < argc)
		inname = argv[optind++];

	if (optind != argc)
		usage ();

	/* 8000 samps per second; 20 msec frames means 160 samps/frame */
	sample_rate = 8000;
	sample_period = 1.0 / sample_rate;

	utt.avail = sample_rate * 10;
	utt.samps = calloc (utt.avail, sizeof *utt.samps);
	utt.ratios = calloc (utt.avail, sizeof *utt.ratios);
	utt.env = calloc (utt.avail, sizeof *utt.env);
	utt.denv = calloc (utt.avail, sizeof *utt.denv);
	utt.stats = calloc (utt.avail, sizeof *utt.stats);
	utt.slope = calloc (utt.avail, sizeof *utt.slope);

	if (inname) {
		process_file (inname);
		exit (0);
	}

	system_start_secs = get_secs ();

	trace_setup (4);
	setup_signal_processing ();

	setup_pulse_audio (argv[0]);

	read_ready_sound ();

	setup_gtk (argv[0], 1000, 300);

	gtk_main ();

	return (0);
}

struct trace {
	struct trace *next;
	char *name;
	double *buf;
};

struct trace *traces, **traces_tailp = &traces;
int ntraces;

int trace_nsamps;
int trace_off;

struct trace *make_trace (char *name);

double raw_secs;
double *raw_samps;
double *raw_ratios;
int raw_nsamps;
int raw_offset;

double *raw_disp;
int *raw_disp_states;
int raw_disp_width;
int raw_disp_off;
int raw_disp_thresh;
double raw_disp_acc;
int raw_disp_count;

void
set_raw_disp_width (int width)
{
	free (raw_disp);
	free (raw_disp_states);
	raw_disp_width = width;
	raw_disp = calloc (raw_disp_width, sizeof *raw_disp);
	raw_disp_states = calloc (raw_disp_width, sizeof *raw_disp_states);

	raw_disp_thresh = raw_nsamps / raw_disp_width;
	if (raw_disp_thresh < 1)
		raw_disp_thresh = 1;

	raw_disp_off = 0;
	raw_disp_acc = 0;
	raw_disp_count = 0;
}

void
trace_setup (double secs)
{
	raw_secs = secs;

	raw_nsamps = raw_secs * sample_rate;
	raw_samps = calloc (raw_nsamps, sizeof *raw_samps);
	raw_ratios = calloc (raw_nsamps, sizeof *raw_ratios);

	set_raw_disp_width (100);

	trace_nsamps = secs * sample_rate;
}

void
unarm (void) 
{
	if (! armed)
		return;

	fclose (f_raw);
	fclose (f_hpf);
	fclose (f_lpf);
	fclose (f_energy);
	fclose (f_fast);
	fclose (f_slow);
	fclose (f_noise);
	fclose (f_ratio);
	armed = 0;
}

void
do_arm (void)
{
	unarm ();

	armed_timestamp = 0;
	f_raw = fopen ("raw.dat", "w");
	f_hpf = fopen ("hpf.dat", "w");
	f_lpf = fopen ("lpf.dat", "w");
	f_energy = fopen ("energy.dat", "w");
	f_fast = fopen ("fast.dat", "w");
	f_slow = fopen ("slow.dat", "w");
	f_noise = fopen ("noise.dat", "w");
	f_ratio = fopen ("ratio.dat", "w");

	armed = 1;
}

double avgval, avg_energy_fast;
double ratio;
double noise;

double vox_too_quiet_timestamp;
double vox_too_loud_timestamp;
double vox_start, vox_end;

double vox_start_debounce = .100;

void
utterance_start (void)
{
	int debounce_samps;
	int offset;
	int stop_offset;
	double noise_amp;

	noise_amp = sqrt (noise);
	debounce_samps = sample_rate * vox_start_debounce;

	offset = (raw_offset + raw_nsamps - debounce_samps * 2)
		% raw_nsamps;
	stop_offset = (raw_offset + 1) % raw_nsamps;

	while (offset != stop_offset) {
		if (fabs (raw_samps[offset]) > 4 * noise_amp)
			break;
		offset = (offset + 1) % raw_nsamps;
	}

	offset = (offset + raw_nsamps - (int)(sample_rate * .25)) % raw_nsamps;

	utt.lookback = (raw_offset - offset + raw_nsamps) % raw_nsamps;

	utt.used = 0;
	while (offset != stop_offset && utt.used < utt.avail) {
		utt.samps[utt.used] = raw_samps[offset];
		utt.ratios[utt.used] = raw_ratios[offset];
		utt.used++;
		offset = (offset + 1) % raw_nsamps;
	}
}

void
utterance_finish (void)
{
	int idx;
	double acc, avg;
	int nsamps;
	double factor;
	double minval, maxval;
	double noise_amp;

	nsamps = .040 * sample_rate;

	if (utt.used < 10 * nsamps)
		return;

	/* subtract dc offset */
	acc = 0;
	for (idx = 0; idx < utt.used; idx++)
		acc += utt.samps[idx];
	avg = acc / utt.used;
	for (idx = 0; idx < utt.used; idx++)
		utt.samps[idx] -= avg;

	/* trim quiet part at end */
	noise_amp = sqrt (noise);
	idx = utt.used;
	while (idx > 0 && fabs (utt.samps[idx]) < 8 * noise_amp)
		idx--;
	utt.trim_end = utt.used - idx;
	utt.used = idx;

	/* scale to -1..1 */
	minval = utt.samps[0];
	maxval = utt.samps[0];
	if (minval > 0)
		minval = 0;
	if (maxval < 0)
		maxval = 0;
	for (idx = 0; idx < utt.used; idx++) {
		if (utt.samps[idx] < minval)
			minval = utt.samps[idx];
		if (utt.samps[idx] > maxval)
			maxval = utt.samps[idx];
	}
	factor = maxval;
	if (-minval > factor)
		factor = -minval;
	for (idx = 0; idx < utt.used; idx++)
		utt.samps[idx] /= factor;

	if (utt.used < 4 * nsamps) {
		printf ("utt lookback %d; trim end %d; all gone\n",
			utt.lookback, utt.trim_end);
		return;
	}

	/* ramp the start and finish to 0 over a 40 msec window */
	for (idx = 0; idx < nsamps; idx++) {
		factor = (double)idx / nsamps;
		utt.samps[idx] *= factor;
		utt.samps[utt.used - idx - 1] *= factor;
	}

	process_utterance ();

//	display_fft ();
	display_u_dat ();
}


void
write_u_dat (void)
{
	FILE *outf;
	int idx;

	outf = fopen ("u.dat", "w");
	for (idx = 0; idx < utt.used; idx++) {
		fprintf (outf, "%.6f %g\n", (double)idx / sample_rate,
			 utt.samps[idx]);
	}
	fclose (outf);

	outf = fopen ("env.dat", "w");
	for (idx = 0; idx < utt.used; idx++) {
		fprintf (outf, "%.6f %g\n", (double)idx / sample_rate,
			 utt.env[idx]);
	}
	fclose (outf);

	outf = fopen ("denv.dat", "w");
	for (idx = 0; idx < utt.used; idx++) {
		fprintf (outf, "%.6f %g\n", (double)idx / sample_rate,
			 utt.denv[idx]);
	}
	fclose (outf);

	outf = fopen ("slope.dat", "w");
	for (idx = 0; idx < utt.used; idx++) {
		fprintf (outf, "%.6f %g\n", (double)idx / sample_rate,
			 utt.slope[idx]);
	}
	fclose (outf);

	outf = fopen ("uratio.dat", "w");
	for (idx = 0; idx < utt.used; idx++) {
		fprintf (outf, "%.6f %g\n", (double)idx / sample_rate,
			 utt.ratios[idx]);
	}
	fclose (outf);
}

void
display_u_dat (void)
{
	static FILE *gp;

	if (gp == NULL) {
		gp = popen ("gnuplot", "w");
		fprintf (gp, "set style data lines\n");
		fprintf (gp, "set xrange [0:.4]\n");
		fprintf (gp, "set yrange [-1:1]\n");
		fprintf (gp, "set y2tics\n");
		fprintf (gp, "set y2range [-5:5]\n");
	}

	fprintf (gp, "plot \"u.dat\", \"env.dat\","
		 "  \"slope.dat\" axes x1y2\n");
	fflush (gp);
}

void
do_fft (void)
{
	double *in;
	fftw_complex *out;
	fftw_plan plan;
	int idx;
	double freq;

	in = fftw_alloc_real (utt.used);
	out = fftw_alloc_complex (utt.used);
	plan = fftw_plan_dft_r2c_1d (utt.used, in, out, FFTW_ESTIMATE);
	
	for (idx = 0; idx < utt.used; idx++)
		in[idx] = utt.samps[idx];

	fftw_execute (plan);

	utt.spec_size = utt.used / 2;
	free (utt.spec);
	utt.spec = calloc (utt.spec_size, sizeof *utt.spec);
	utt.spec_freq_incr = (double)sample_rate / utt.used;

	for (idx = 0; idx < utt.spec_size; idx++) {
		freq = idx * utt.spec_freq_incr;
		if (300 < freq && freq < 3000) {
			utt.spec[idx] = hypot (out[idx][0], out[idx][1]);
		} else {
			utt.spec[idx] = 0;
		}
	}

	fftw_destroy_plan (plan);
	fftw_free (in);
	fftw_free (out);
}

void
write_fft (void)
{
	FILE *outf;
	double freq;
	int idx;

	outf = fopen ("spec.dat", "w");

	for (idx = 0; idx < utt.spec_size; idx++) {
		freq = idx * utt.spec_freq_incr;
		fprintf (outf, "%g %g\n", freq, utt.spec[idx]);
	}

	fclose (outf);
}

void
display_fft (void)
{
	static FILE *gp;

	if (gp == NULL) {
		gp = popen ("gnuplot", "w");
		fprintf (gp, "set style data lines\n");
		fprintf (gp, "set xrange [0:3000]\n");
		fprintf (gp, "set yrange [0:200]\n");
	}
	fprintf (gp, "plot \"spec.dat\"\n");
	fflush (gp);
}

int
double_cmp (void const *raw1, void const *raw2)
{
	double arg1 = *(double const *)raw1;
	double arg2 = *(double const *)raw2;
	if (arg1 < arg2)
		return (-1);
	else if (arg1 > arg2)
		return (1);
	return (0);
}

void
find_next (int *off, int val, double *start, double *end, double *dur)
{
	int idx;

	idx = *off;
	while (idx < utt.used && utt.slope[idx] != val)
		idx++;
	*start = idx * sample_period;

	while (idx < utt.used && utt.slope[idx] == val)
		idx++;
	*end = idx * sample_period;

	*dur = *end - *start;
	*off = idx;
}

void
do_envelope (void)
{
	double time_constant, factor, env;
	int idx;
	double val, val2;
	double last;
	double inst_vel;
	double denv;
	double q1, q3;
	double zthresh;
	double p1_start, p1_end, p1_dur;
	double p2_start, p2_end, p2_dur;
	double p3_start, p3_end, p3_dur;
	double p4_start, p4_end, p4_dur;
	double p13_dist, total;
	int p1_ok, p3_ok, p13_ok, total_ok, got_bidding;

	time_constant = .1;
	factor = 2 * M_PI * 1.0/time_constant * sample_period;
	env = 0;
	for (idx = 0; idx < utt.used; idx++) {
		val = utt.samps[idx];
		val2 = val * val;
		env = env * (1 - factor) + val2 * factor;
		utt.env[idx] = sqrt (env);
	}

	last = utt.env[0];
	denv = 0;
	for (idx = 0; idx < utt.used; idx++) {
		inst_vel = (utt.env[idx] - last) / sample_period;
		last = utt.env[idx];

		time_constant = .1;
		factor = 2 * M_PI * 1.0/time_constant * sample_period;
		denv = denv * (1 - factor) + inst_vel * factor;
		utt.denv[idx] = denv;
	}

	for (idx = 0; idx < utt.used; idx++)
		utt.stats[idx] = utt.denv[idx];
	qsort (utt.stats, utt.used, sizeof *utt.stats, double_cmp);
	
	q1 = utt.stats[utt.used / 4];
	q3 = utt.stats[utt.used * 3 / 4];

	
	zthresh = fabs ((q3 - q1) / 10);

	val = 0;
	for (idx = 0; idx < utt.used; idx++) {
		if (val == 0) {
			if (utt.denv[idx] > q3)
				val = 1;
			if (utt.denv[idx] < q1)
				val = -1;
		} else if (val == 1) {
			if (utt.denv[idx] < zthresh)
				val = 0;
		} else if (val == -1) {
			if (utt.denv[idx] > -zthresh)
				val = 0;
		}
		utt.slope[idx] = val;
	}

	idx = 0;
	find_next (&idx, 1, &p1_start, &p1_end, &p1_dur);
	find_next (&idx, -1, &p2_start, &p2_end, &p2_dur);
	find_next (&idx, 1, &p3_start, &p3_end, &p3_dur);
	find_next (&idx, -1, &p4_start, &p4_end, &p4_dur);

	p13_dist = p3_start - p1_start;
	total = p4_end - p1_start;

	p1_ok = 0;
	p3_ok = 0;
	p13_ok = 0;
	total_ok = 0;

	if (.025 <= p1_dur && p1_dur <= .100)
		p1_ok = 1;
	if (.010 <= p3_dur && p3_dur <= .100)
		p3_ok = 1;
	if (.050 <= p13_dist && p13_dist <= .300)
		p13_ok = 1;
	if (.150 <= total && total <= .300)
		total_ok = 1;

	got_bidding = 0;
	if (p1_ok && p3_ok && p13_ok && total_ok)
		got_bidding = 1;

	printf ("env: %5.3f%s %5.3f%s %5.3f%s %5.3f%s %s\n",
		p1_dur, p1_ok ? "+" : " ",
		p3_dur, p3_ok ? "+" : " ",
		p13_dist, p13_ok ? "+" : " ",
		total, total_ok ? "+" : " ",
		got_bidding ? "got bidding" : "");

	if (got_bidding)
		play_ready ();
}

void
write_wav (void)
{
	SNDFILE *sf;
	SF_INFO sfinfo;
	const int sbuf_size = 1000;
	int16_t sbuf[sbuf_size];
	int off, thistime, idx;

	memset (&sfinfo, 0, sizeof sfinfo);
	sfinfo.samplerate = sample_rate;
	sfinfo.channels = 1;
	sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

	if ((sf = sf_open ("utt.wav", SFM_WRITE, &sfinfo)) == NULL) {
		fprintf (stderr, "can't create utt.wav\n");
		exit (1);
	}

	off = 0;
	while (off < utt.used) {
		thistime = utt.used - off;
		if (thistime > sbuf_size)
			thistime = sbuf_size;
		for (idx = 0; idx < thistime; idx++)
			sbuf[idx] = 20000 * utt.samps[off + idx];
		sf_write_short (sf, sbuf, thistime);

		off += thistime;
	}

	sf_close (sf);
}

void
process_utterance (void)
{
	write_wav ();

	do_fft ();
	do_envelope ();

	write_u_dat ();
	write_fft ();
}


double hpfval, lpfval;

void
setup_signal_processing (void)
{
	noise = 1;
}

void
process_data (int16_t const *samps, int nsamps)
{
	int i;
	double rawval;
	double factor, time_constant;
	double zval, zval2;
	double now;
	double cutoff_freq;

	if (freeze)
		return;

	for (i = 0; i < nsamps; i++) {
		raw_offset = (raw_offset + 1) % raw_nsamps;

		rawval = samps[i] / 32768.0;

		raw_samps[raw_offset] = rawval;

		raw_disp_acc += fabs (rawval);
		raw_disp_count++;
		if (raw_disp_count >= raw_disp_thresh) {
			raw_disp[raw_disp_off] = raw_disp_acc / raw_disp_count;
			raw_disp_states[raw_disp_off] = vox_state;
			raw_disp_off = (raw_disp_off + 1) % raw_disp_width;
			raw_disp_acc = 0;
			raw_disp_count = 0;
		}

		cutoff_freq = 1;
		factor = 2 * M_PI * cutoff_freq * sample_period;
		lpfval = lpfval * (1-factor) + rawval * factor;

		zval = rawval - lpfval;
		zval2 = zval * zval;

		time_constant = .25;
		factor = 2 * M_PI * 1.0/time_constant * sample_period;
		avg_energy_fast = avg_energy_fast*(1-factor) + zval2*factor;

		if (avg_energy_fast < noise) {
			noise = avg_energy_fast;
		} else {
			time_constant = 20;
			factor = 2 * M_PI * 1.0/time_constant * sample_period;
			noise = noise * (1-factor) + avg_energy_fast * factor;
		}

		if (noise < 5.0 / 32768)
			noise = 5.0 / 32768;

		ratio = avg_energy_fast / noise;

		raw_ratios[raw_offset] = ratio;

		if (vox_state && utt.used < utt.avail) {
			utt.samps[utt.used] = rawval;
			utt.ratios[utt.used] = ratio;
			utt.used++;
		}

		now = get_secs ();

		if (vox_state == 0) {
			if (ratio > 4) {
				if (now - vox_too_quiet_timestamp
				    > vox_start_debounce) {
					vox_state = 1;
					vox_start = now;
					vox_too_loud_timestamp = now;
					
					utterance_start ();
				}
			} else {
				vox_too_quiet_timestamp = now;
			}

		} else {
			if (ratio < 3) {
				if (now - vox_too_loud_timestamp > .500) {
					vox_state = 0;
					vox_end = now;
					vox_too_quiet_timestamp = now;
					
					utterance_finish ();
				}
			} else {
				vox_too_loud_timestamp = now;
			}
		}


		if (armed) {
			fprintf (f_raw, "%.6f %g\n",
				 armed_timestamp, rawval);
			fprintf (f_hpf, "%.6f %g\n",
				 armed_timestamp, hpfval);
			fprintf (f_lpf, "%.6f %g\n",
				 armed_timestamp, lpfval);
			fprintf (f_energy, "%.6f, %g\n",
				 armed_timestamp, ratio);
			fprintf (f_fast, "%.6f, %g\n",
				 armed_timestamp, avg_energy_fast);
			fprintf (f_noise, "%.6f, %g\n",
				 armed_timestamp, noise);
			fprintf (f_ratio, "%.6f, %g\n",
				 armed_timestamp, ratio);
			armed_timestamp += 1.0 / sample_rate;

			if (armed_timestamp > 2) 
				unarm ();
		}
	}
}

void
set_color (cairo_t *cr, int color)
{
	int r, g, b;
	r = (color >> 16) & 0xff;
	g = (color >> 8) & 0xff;
	b = color & 0xff;
	cairo_set_source_rgb (cr, r / 255.0, g / 255.0, b / 255.0);
}

void
draw_traces (cairo_t *cr, int width, int height)
{
	int center_y;
	int x, y;

	if (raw_disp_width != width)
		set_raw_disp_width (width);

	center_y = height / 2;

	for (x = 0; x < width; x++) {
		if (raw_disp_states[x])
			set_color (cr, 0x88ff88);
		else
			set_color (cr, 0xffffff);
			
		cairo_move_to (cr, x, 0);
		cairo_line_to (cr, x, height);
		cairo_stroke (cr);
	}

	set_color (cr, 0x8888ff);
	cairo_move_to (cr, 0, center_y);
	cairo_line_to (cr, width, center_y);
	cairo_stroke (cr);


	cairo_set_source_rgb (cr, 0, 0, 0);
	cairo_move_to (cr, 0, center_y);

	for (x = 0; x < width; x++) {
		y = 1000 * raw_disp[x];

		cairo_line_to (cr, x, height - (y + center_y));
	}

	cairo_stroke (cr);

	if (armed) {
		cairo_set_source_rgb (cr, 0, 1, 0);
		cairo_arc (cr, 20, 20, 5, 0, 2 * M_PI);
		cairo_fill (cr);
	}
}

struct trace *
make_trace (char *name)
{
	struct trace *tp;

	tp = calloc (1, sizeof *tp);
	tp->name = strdup (name);
	tp->buf = calloc (trace_nsamps, sizeof *tp->buf);

	*traces_tailp = tp;
	traces_tailp = &tp->next;
	ntraces++;

	return (tp);
}

void
print_stats (void)
{
	printf ("avgval %g\n", avgval);
	printf ("avg_energy_fast %g\n", avg_energy_fast);
	printf ("ratio %g\n", ratio);
	printf ("\n");
}
