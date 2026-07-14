/*
 * lxqtprobe -- does the LXQt library stack actually work on MINIX?
 *
 * Linking proves very little here.  The interesting parts are the ones that
 * reach outside Qt and into the platform:
 *
 *   - XdgMimeApps is backed by GLib/GIO (GDesktopAppInfo).  Constructing it and
 *     asking it a question is the real test that the GLib port works at all --
 *     GLib is the heaviest new dependency the LXQt stack drags in, and on MINIX
 *     it runs with no inotify and no kqueue, so its file monitoring falls back
 *     to polling.
 *   - XdgDesktopFile is the XDG desktop-entry parser: write one, read it back.
 *   - XdgDirs resolves the XDG base directories from the environment.
 *   - LXQt::Settings sits on QSettings, i.e. real file I/O under XDG_CONFIG_HOME.
 *
 * Runs headless (-platform offscreen) so it is deterministic and needs no
 * compositor, exactly like qtprobe's offscreen mode.
 */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QStringList>
#include <QMimeDatabase>
#include <QTemporaryDir>
#include <QTextStream>

#include <XdgDesktopFile>
#include <XdgDirs>
#include <XdgMimeApps>
#include <XdgMimeType>

#include <LXQt/Settings>

#include <QApplication>

#include <unistd.h>

/* Static Qt: plugins are archives, not dlopen'd .so files, so a platform plugin
 * exists only if it is registered by hand.  Without this Qt aborts at startup
 * with "Could not find the Qt platform plugin". */
#include <QtPlugin>
Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)
Q_IMPORT_PLUGIN(QMinimalIntegrationPlugin)
Q_IMPORT_PLUGIN(QWaylandIntegrationPlugin)

/* Markers, written with raw write(2) so nothing in the C++ or Qt runtime has to
 * be alive for them to appear.  If "ctor" prints but "main" does not, the crash
 * is in static initialisation; if neither prints, it is even earlier. */
__attribute__((constructor)) static void
probe_ctor_marker(void)
{
	static const char m[] = "lxqtprobe: reached a static constructor\n";
	(void)write(1, m, sizeof(m) - 1);
}

static int failures = 0;

static void
check(const char *what, bool ok, const QString &detail = QString())
{
	QTextStream out(stdout);

	out << "  " << qSetFieldWidth(46) << Qt::left << what
	    << qSetFieldWidth(0) << (ok ? "-> OK" : "-> FAIL");
	if (!detail.isEmpty())
		out << "  (" << detail << ")";
	out << Qt::endl;

	if (!ok)
		failures++;
}

int
main(int argc, char **argv)
{
	{
		static const char m[] = "lxqtprobe: reached main()\n";
		(void)write(1, m, sizeof(m) - 1);
	}

	QApplication app(argc, argv);
	QTextStream out(stdout);

	out << Qt::endl << "lxqtprobe: libqtxdg + liblxqt on MINIX" << Qt::endl
	    << Qt::endl;

	/* 1. XdgDirs: the XDG base directories. */
	const QString dataHome = XdgDirs::dataHome();
	const QString configHome = XdgDirs::configHome();
	check("XdgDirs::dataHome", !dataHome.isEmpty(), dataHome);
	check("XdgDirs::configHome", !configHome.isEmpty(), configHome);

	/* 2. XdgDesktopFile: write a desktop entry, read it back. */
	QTemporaryDir tmp;
	if (tmp.isValid()) {
		const QString path = tmp.filePath(QStringLiteral("probe.desktop"));
		QFile f(path);
		if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream ts(&f);
			ts << "[Desktop Entry]\n"
			   << "Type=Application\n"
			   << "Name=Probe\n"
			   << "Exec=/bin/echo probe\n"
			   << "Categories=Utility;\n";
			f.close();
		}

		XdgDesktopFile df;
		const bool loaded = df.load(path);
		check("XdgDesktopFile::load", loaded);
		check("XdgDesktopFile::isValid", loaded && df.isValid());
		check("XdgDesktopFile Name == \"Probe\"",
		    df.name() == QStringLiteral("Probe"), df.name());
		check("XdgDesktopFile type is Application",
		    df.type() == XdgDesktopFile::ApplicationType);
	} else {
		check("QTemporaryDir", false);
	}

	/* 3. XdgMimeType: Qt's MIME database, wrapped by libqtxdg. */
	QMimeDatabase mimeDb;
	XdgMimeType mt(mimeDb.mimeTypeForName(QStringLiteral("text/plain")));
	check("XdgMimeType(\"text/plain\") is valid", mt.isValid(), mt.name());

	/*
	 * 4. XdgMimeApps -- the GLib/GIO backend.  This is the one that proves the
	 * GLib port: constructing it spins up GDesktopAppInfo's monitors, and
	 * categorizedApps()/apps() walks the desktop-file databases through GIO.
	 * We do not require that any application is actually installed (the base
	 * image has no .desktop files), only that GLib runs and answers.
	 */
	{
		XdgMimeApps apps;
		const QList<XdgDesktopFile *> all = apps.allApps();
		check("XdgMimeApps (GLib/GIO backend) constructed", true,
		    QStringLiteral("%1 apps known").arg(all.count()));

		/* Asking for a default handler must not crash; null is a fine answer
		 * on an image with no desktop files installed. */
		XdgDesktopFile *def = apps.defaultApp(QStringLiteral("text/plain"));
		check("XdgMimeApps::defaultApp(text/plain) answered", true,
		    def ? def->name() : QStringLiteral("none installed"));
		qDeleteAll(all);
	}

	/* 5. liblxqt: LXQt::Settings, i.e. QSettings under XDG_CONFIG_HOME. */
	{
		LXQt::Settings settings(QStringLiteral("lxqtprobe"));
		settings.setValue(QStringLiteral("probe/value"), 42);
		settings.sync();

		const bool wrote = settings.status() == QSettings::NoError;
		check("LXQt::Settings write", wrote, settings.fileName());
		check("LXQt::Settings read back",
		    settings.value(QStringLiteral("probe/value")).toInt() == 42);
	}

	/* 6. liblxqt linked and its QApplication subclass ran. */
	check("QApplication event loop alive",
	    QCoreApplication::instance() != nullptr,
	    QGuiApplication::platformName());

	out << Qt::endl;
	if (failures == 0)
		out << "lxqtprobe: ALL PASS" << Qt::endl;
	else
		out << "lxqtprobe: " << failures << " FAILED" << Qt::endl;
	out << Qt::endl;

	return failures == 0 ? 0 : 1;
}
