#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include <gtk/gtk.h>

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

int vox_state;

int utterance_avail;
int utterance_used;
double *utterance;

void print_stats (void);

int freeze;
int armed;

void do_arm (void);
FILE *f_raw;
FILE *f_avg;
FILE *f_energy;
FILE *f_fast;
FILE *f_slow;
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

	if (optind != argc)
		usage ();

	/* 8000 samps per second; 20 msec frames means 160 samps/frame */
	sample_rate = 8000;
	sample_period = 1.0 / sample_rate;

	utterance_avail = sample_rate * 10;
	utterance = calloc (utterance_avail, sizeof *utterance);

	system_start_secs = get_secs ();

	trace_setup (4);

	setup_pulse_audio (argv[0]);

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
	fclose (f_avg);
	fclose (f_energy);
	fclose (f_fast);
	fclose (f_slow);
	armed = 0;
}

void
do_arm (void)
{
	unarm ();

	armed_timestamp = 0;
	f_raw = fopen ("raw.dat", "w");
	f_avg = fopen ("avg.dat", "w");
	f_energy = fopen ("energy.dat", "w");
	f_fast = fopen ("fast.dat", "w");
	f_slow = fopen ("slow.dat", "w");

	armed = 1;
}

double avgval, avg_energy_fast, avg_energy_slow;
double ratio;

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

	debounce_samps = sample_rate * vox_start_debounce;

	offset = (raw_offset + raw_nsamps - debounce_samps * 2)
		% raw_nsamps;
	stop_offset = (raw_offset + 1) % raw_nsamps;

	while (offset != stop_offset) {
		if (raw_ratios[offset] > 1.1)
			break;
		offset = (offset + 1) % raw_nsamps;
	}

	utterance_used = 0;
	while (offset != stop_offset && utterance_used < utterance_avail) {
		utterance[utterance_used++] = raw_samps[offset];
		offset = (offset + 1) % raw_nsamps;
	}
}

void
utterance_finish (void)
{
	FILE *outf;
	int idx;
	outf = fopen ("u.dat", "w");
	for (idx = 0; idx < utterance_used; idx++) {
		fprintf (outf, "%.6f %g\n", (double)idx / sample_rate,
			 utterance[idx]);
	}
	fclose (outf);
	printf ("written\n");
}

void
process_data (int16_t const *samps, int nsamps)
{
	int i;
	double rawval;
	double factor, time_constant;
	double zval, zval2;
	double now;

	if (freeze)
		return;

	for (i = 0; i < nsamps; i++) {
		raw_offset = (raw_offset + 1) % raw_nsamps;

		rawval = samps[i] / 32768.0;

		raw_samps[raw_offset] = rawval;
		if (vox_state && utterance_used < utterance_avail)
			utterance[utterance_used++] = rawval;

		raw_disp_acc += log (rawval * rawval);
		raw_disp_count++;
		if (raw_disp_count >= raw_disp_thresh) {
			raw_disp[raw_disp_off] = raw_disp_acc / raw_disp_count;
			raw_disp_states[raw_disp_off] = vox_state;
			raw_disp_off = (raw_disp_off + 1) % raw_disp_width;
			raw_disp_acc = 0;
			raw_disp_count = 0;
		}

		time_constant = 10;
		factor = time_constant / sample_rate;
		avgval = avgval * (1 - factor) + rawval * factor;

		zval = rawval - avgval;
		zval2 = zval * zval;

		time_constant = .25;
		factor = 2 * M_PI * 1.0/time_constant * sample_period;
		avg_energy_fast = avg_energy_fast*(1-factor) + zval2*factor;

		time_constant = 10;
		factor = 2 * M_PI * 1.0/time_constant * sample_period;
		if (avg_energy_slow == -1)
			avg_energy_slow = zval2;
		avg_energy_slow = avg_energy_slow*(1-factor) + zval2*factor;

		if (get_secs () - system_start_secs < .5)
			avg_energy_slow = avg_energy_fast * 10;

		ratio = sqrt (avg_energy_fast) / sqrt (avg_energy_slow);
		if (isnan (ratio))
		    ratio = 0;

		raw_ratios[raw_offset] = ratio;

		now = get_secs ();

		if (vox_state == 0) {
			if (ratio < 1.2) {
				vox_too_quiet_timestamp = now;
			} else if (now - vox_too_quiet_timestamp
				   > vox_start_debounce) {
				vox_state = 1;
				vox_start = now;
				vox_too_loud_timestamp = now;

				utterance_start ();
			}
		} else {
			if (ratio > 1.1) {
				vox_too_loud_timestamp = now;
			} else if (now - vox_too_loud_timestamp > .500) {
				vox_state = 0;
				vox_end = now;
				vox_too_quiet_timestamp = now;

				utterance_finish ();
			}
		}


		if (armed) {
			fprintf (f_raw, "%.6f %g\n",
				 armed_timestamp, rawval);
			fprintf (f_avg, "%.6f %g\n",
				 armed_timestamp, avgval);
			fprintf (f_energy, "%.6f, %g\n",
				 armed_timestamp, ratio);
			fprintf (f_fast, "%.6f, %g\n",
				 armed_timestamp, avg_energy_fast);
			fprintf (f_slow, "%.6f, %g\n",
				 armed_timestamp, avg_energy_slow);
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
		if (0) {
			y = 10 * raw_disp[(x + raw_disp_off) % raw_disp_width];
		} else {
			y = 10 * raw_disp[x];
		}

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
	printf ("avg_energy_slow %g\n", avg_energy_slow);
	printf ("ratio %g\n", ratio);
	printf ("\n");
}
