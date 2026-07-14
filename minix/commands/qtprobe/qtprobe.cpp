/*	qtprobe - does Qt actually work on MINIX?
 *
 * The point of the whole Qt port: a real Qt Widgets application, rendering with
 * Qt's raster engine into a wl_shm buffer, on wlcompd.  No GPU is involved
 * anywhere -- which is exactly why this path was chosen, since MINIX has no
 * DRM, no Mesa and no EGL.
 *
 * It is written as a probe rather than a demo because "Qt started" proves very
 * little.  Each of the things a toolkit stands on is checked separately, since
 * each fails in a different place:
 *
 *   QCoreApplication	the event loop -- on MINIX that means poll(2), since
 *			there is no epoll and no kqueue
 *   QString/QVariant	the value types, and with them the whole PCRE/Unicode tier
 *   QObject		signals and slots, i.e. the meta-object system, which
 *			leans on the RTTI cxxprobe proved works here
 *   QImage/QPainter	the raster paint engine -- the renderer that lets Qt
 *			draw without a GPU at all
 *   QGuiApplication	the QPA platform plugin actually loading and connecting
 *			to the compositor
 *
 * With -platform wayland it connects to wlcompd and shows a window.  With
 * -platform offscreen it runs headless, which is what makes it usable as an
 * automated check.
 *
 * Exits 0 only if every check passes.
 */

#include <QtCore/QCoreApplication>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtCore/QTimer>

#include <QtGui/QGuiApplication>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QPainter>

#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include <cstdio>

/* Static Qt: the platform plugin has to be linked in and registered by hand. */
#include <QtPlugin>
#if defined(QTPROBE_WAYLAND)
Q_IMPORT_PLUGIN(QWaylandIntegrationPlugin)
#endif
Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)
Q_IMPORT_PLUGIN(QMinimalIntegrationPlugin)

static int failures;

static void
check(const char *what, bool ok, const QString &detail = QString())
{
    if (ok) {
        std::printf("%-46s -> OK\n", what);
    } else {
        std::printf("%-46s -> FAIL (%s)\n", what,
                    detail.toLocal8Bit().constData());
        failures++;
    }
    std::fflush(stdout);
}

/* A QObject with a signal and a slot: the meta-object system in miniature. */
class Pinger : public QObject
{
    Q_OBJECT
public:
    int pongs = 0;
signals:
    void ping(int value);
public slots:
    void onPing(int value) { pongs += value; }
};

int
main(int argc, char **argv)
{
    std::printf("qtprobe: Qt %s on MINIX\n\n", qVersion());

    QApplication app(argc, argv);

    check("QApplication constructed", true);
    check("Qt version is the one we built",
          QString(qVersion()).startsWith(QLatin1String("6.")), qVersion());

    /* The QPA plugin that actually got loaded. */
    const QString platform = QGuiApplication::platformName();
    std::printf("%-46s -> %s\n", "  QPA platform in use",
                platform.toLocal8Bit().constData());
    check("a QPA platform plugin loaded", !platform.isEmpty(),
          QStringLiteral("no platform"));

    /* 1. Value types: QString and QVariant, and the Unicode tier behind them. */
    {
        QString s = QStringLiteral("MINIX");
        s += QLatin1String(" + Qt");
        QVariant v = s;

        check("QString: build and compare",
              s == QLatin1String("MINIX + Qt"), s);
        check("QVariant: round-trips a QString",
              v.toString() == s, v.toString());

        QStringList parts = s.split(QLatin1Char(' '));
        check("QString::split", parts.size() == 3,
              QString::number(parts.size()));
    }

    /* 2. The meta-object system: signals and slots, i.e. moc plus RTTI. */
    {
        Pinger p;
        QObject::connect(&p, &Pinger::ping, &p, &Pinger::onPing);
        emit p.ping(21);
        emit p.ping(21);

        check("QObject: signal reached its slot", p.pongs == 42,
              QString::number(p.pongs));
        check("QObject: metaObject knows the class name",
              QLatin1String(p.metaObject()->className()) ==
                  QLatin1String("Pinger"),
              QLatin1String(p.metaObject()->className()));
    }

    /*
     * 3. The raster paint engine.  This is the load-bearing one: it is what
     * lets Qt render with no GPU, and so what makes Qt possible on MINIX at
     * all.  Paint into a QImage and read the pixels back.
     */
    {
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(Qt::black);

        QPainter painter(&img);
        check("QPainter: opened on a QImage", painter.isActive());

        painter.fillRect(0, 0, 32, 48, QColor(255, 102, 0));   /* orange */
        painter.fillRect(32, 0, 32, 48, QColor(34, 68, 102));  /* blue   */
        painter.end();

        const QRgb left = img.pixel(8, 24);
        const QRgb right = img.pixel(40, 24);

        check("raster engine: left half painted orange",
              qRed(left) == 255 && qGreen(left) == 102 && qBlue(left) == 0,
              QString::asprintf("rgb(%d,%d,%d)", qRed(left), qGreen(left),
                                qBlue(left)));
        check("raster engine: right half painted blue",
              qRed(right) == 34 && qGreen(right) == 68 && qBlue(right) == 102,
              QString::asprintf("rgb(%d,%d,%d)", qRed(right), qGreen(right),
                                qBlue(right)));
    }

    /* 4. Widgets: a real window, laid out.  On wayland this reaches wlcompd. */
    {
        QWidget window;
        window.setWindowTitle(QStringLiteral("qtprobe"));
        window.resize(320, 200);

        auto *layout = new QVBoxLayout(&window);
        auto *label = new QLabel(QStringLiteral("Qt on MINIX"), &window);
        layout->addWidget(label);

        window.show();

        check("QWidget: window created and shown",
              window.isVisible() && window.width() == 320,
              QString::number(window.width()));
        check("QLabel: text set",
              label->text() == QLatin1String("Qt on MINIX"), label->text());

        /*
         * 5. The event loop.  On MINIX this is poll(2) underneath -- there is
         * no epoll and no kqueue -- so a timer firing and quit() being honoured
         * is a real test of the dispatcher, not a formality.
         */
        bool timer_fired = false;
        QTimer::singleShot(150, [&] {
            timer_fired = true;
            QCoreApplication::quit();
        });

        const int rc = app.exec();

        check("event loop: ran and exited cleanly", rc == 0,
              QString::number(rc));
        check("event loop: the timer fired (poll(2) dispatcher)", timer_fired);
    }

    std::printf("\nqtprobe: %s\n",
                failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}

#include "qtprobe.moc"
