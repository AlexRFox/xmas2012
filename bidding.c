#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include <gtk/gtk.h>

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

void
usage (void)
{
	fprintf (stderr, "usage: bidding\n");
	exit (1);
}

pa_glib_mainloop *pa_loop;
pa_context *pa_ctx;
int pa_ready;

void
pa_state_cb (pa_context *ctx, void *userdata)
{
	switch (pa_context_get_state (ctx)) {
	case PA_CONTEXT_READY:
		printf ("pulseaudio ready\n");
		pa_ready = 1;
		break;
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		pa_ready = -1;
		break;
	default:
		break;
	}
}

void
setup_pulse_audio (char *name)
{
	pa_loop = pa_glib_mainloop_new (g_main_context_default ());
	pa_ctx = pa_context_new (pa_glib_mainloop_get_api (pa_loop),
				 name);
	pa_context_connect (pa_ctx, NULL, 0, NULL);
	pa_context_set_state_callback (pa_ctx, pa_state_cb, NULL);
}

GtkWidget *window;

static gboolean
draw_cb (GtkWidget *widget, cairo_t *cr, gpointer data)
{
	int width, height;
	gtk_window_get_size (GTK_WINDOW(widget), &width, &height);

	cairo_move_to (cr, 0, 0);
	cairo_line_to (cr, width, height);
	cairo_stroke (cr);

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
	}
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


	setup_pulse_audio (argv[0]);

	setup_gtk (argv[0], 640, 480);

	gtk_main ();

	return (0);
}
