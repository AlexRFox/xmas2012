#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include <gtk/gtk.h>

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

int freeze;

gboolean tick (gpointer data);

void trace_setup (double secs);
void draw_traces (cairo_t *cr, int width, int height);


int sample_rate;
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
	case ' ':
		freeze ^= 1;
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
int raw_nsamps;
int raw_offset;

double *raw_disp;
int raw_disp_width;
int raw_disp_off;
int raw_disp_thresh;
double raw_disp_acc;
int raw_disp_count;

void
set_raw_disp_width (int width)
{
	free (raw_disp);
	raw_disp_width = width;
	raw_disp = calloc (raw_disp_width, sizeof *raw_disp);

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

	set_raw_disp_width (100);

	trace_nsamps = secs * sample_rate;
}

void
process_data (int16_t const *samps, int nsamps)
{
	int i;
	double val;
	static double avgval;
	double factor, time_constant;

	if (freeze)
		return;

	for (i = 0; i < nsamps; i++) {
		val = samps[i] / 32768.0;

		time_constant = 1;
		factor = time_constant / sample_rate;
		avgval = avgval * (1 - factor) + val * factor;

		val -= avgval;

		raw_samps[raw_offset] = val;
		raw_offset = (raw_offset + 1) % raw_nsamps;

		raw_disp_acc += log (val * val);
		raw_disp_count++;
		if (raw_disp_count >= raw_disp_thresh) {
			raw_disp[raw_disp_off] = raw_disp_acc / raw_disp_count;
			raw_disp_off = (raw_disp_off + 1) % raw_disp_width;
			raw_disp_acc = 0;
			raw_disp_count = 0;
		}
	}
}

void
draw_traces (cairo_t *cr, int width, int height)
{
	int center_y;
	int x, y;

	if (raw_disp_width != width)
		set_raw_disp_width (width);

	center_y = height / 2;

	cairo_move_to (cr, 0, center_y);

	for (x = 0; x < width; x++) {
		y = 10 * raw_disp[(x + raw_disp_off) % raw_disp_width];

		cairo_line_to (cr, x, height - (y + center_y));
	}

	cairo_stroke (cr);
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

