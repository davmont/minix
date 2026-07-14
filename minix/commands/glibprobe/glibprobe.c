/*
 * glibprobe -- does GLib itself work on MINIX, with no Qt anywhere near it?
 *
 * GLib is the heaviest dependency the LXQt stack pulls in (libqtxdg's mime-apps
 * backend is GIO), and it is the newest thing in the link.  When lxqtprobe
 * crashed before printing anything, the question became: is it GLib, or is it
 * the LXQt libraries on top?  This answers the first half on its own.
 *
 * It exercises what libqtxdg actually uses: the base-directory lookups, the
 * type/object system, a main context, and GDesktopAppInfo -- the GIO piece that
 * walks the desktop-file databases.
 */
#include <gio/gdesktopappinfo.h>
#include <glib.h>
#include <glib-object.h>
#include <stdio.h>

int
main(void)
{
	GMainContext *ctx;
	GList *apps;
	gchar *s;

	printf("glibprobe: GLib %d.%d.%d on MINIX\n\n",
	    glib_major_version, glib_minor_version, glib_micro_version);

	printf("  g_get_user_data_dir   -> %s\n", g_get_user_data_dir());
	printf("  g_get_user_config_dir -> %s\n", g_get_user_config_dir());

	s = g_strdup_printf("%s-%d", "glib", 42);
	printf("  g_strdup_printf       -> %s\n", s);
	g_free(s);

	/* GObject: the type system has to initialise, which is where a broken
	 * libffi or TLS setup would show up. */
	g_type_ensure(G_TYPE_OBJECT);
	printf("  GObject type system   -> ok (%s)\n", g_type_name(G_TYPE_OBJECT));

	/* A main context: GLib's poll(2)-based loop.  MINIX has neither inotify
	 * nor kqueue, so GLib falls back to polling -- exercise that it runs. */
	ctx = g_main_context_new();
	g_main_context_iteration(ctx, FALSE);
	g_main_context_unref(ctx);
	printf("  GMainContext iterate  -> ok\n");

	/* GIO / GDesktopAppInfo: what libqtxdg's XdgMimeApps is built on.  An
	 * empty list is a fine answer on an image with no .desktop files; not
	 * crashing is the point. */
	apps = g_app_info_get_all();
	printf("  g_app_info_get_all    -> ok (%u apps)\n",
	    g_list_length(apps));
	g_list_free_full(apps, g_object_unref);

	printf("\nglibprobe: ALL PASS\n\n");
	return 0;
}
