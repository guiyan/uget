/* Repro harness: delete download records, then click sidebar (state/category) rows.
 * Mirrors the GUI path exactly:
 *  - row click            -> gtk_single_selection_set_selected (state/category)
 *  - download row click   -> selection changed on download.selection
 *  - delete key/toolbar   -> ugtk_app_delete_download()
 */
#include <UgtkApp.h>
#include <gtk/gtk.h>
#include <stdio.h>

static int step = 0;
static int loop = 0;
static UgtkApp* gapp;

static void
do_clicks (void)
{
    int ncat, nstate;

    ncat   = g_list_model_get_n_items (G_LIST_MODEL (gapp->traveler.category.model));
    nstate = g_list_model_get_n_items (G_LIST_MODEL (gapp->traveler.state.model));

    printf ("[step %d] ncat=%d nstate=%d selected dl=%u\n", loop, ncat, nstate,
            gtk_selection_model_get_selection (GTK_SELECTION_MODEL (gapp->traveler.download.selection)) ?
            gtk_bitset_get_size (gtk_selection_model_get_selection (GTK_SELECTION_MODEL (gapp->traveler.download.selection))) : 0);
    fflush (stdout);

    /* toggle through category rows and state rows (mimic user clicks) */
    gtk_single_selection_set_selected (gapp->traveler.category.selection, loc_scroll_dummy (loop) );
    gtk_single_selection_set_selected (gapp->traveler.state.selection, dummy_state (loop));
    /* click a download row too */
    gtk_single_selection_set_selected (gapp->traveler.category.selection, loc_scroll_dummy2 (loop));
}

int main (int argc, char* argv[])
{
	UgtkApp* app;

	gtk_init ();
	app = g_new0 (UgtkApp, 1);
	gapp = app;
	ugtk_app_init (app, NULL);

	printf ("app ready; categories=%u\n", g_list_model_get_n_items (G_LIST_MODEL (app->traveler.category.model)));

	/* add 3 download records non-interactively */
	uget_app_add_download_uri ((UgetApp*) app, "http://example.org/a.bin", NULL, TRUE);
	uget_app_add_download_uri ((UgetApp*) app, "http://example.org/b.bin", NULL, TRUE);
	uget_app_add_download_uri ((UgetApp*) app, "http://example.org/c.bin", NULL, TRUE);
	printf ("added 3 downloads; download model n=%u\n",
			g_list_model_get_n_items (G_LIST_MODEL (app->traveler.download.model)));

	gtk_main ();
	ugtk_app_final (app);
	return 0;
}
