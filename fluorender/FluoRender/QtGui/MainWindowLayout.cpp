// Main window size against the screen (MainWindow::FitToScreen) and its
// testing aid VVD_QT_TEST=layout (docs/QT_PORT.md "Window size").
#include "MainWindow.h"
#include "TestRun.h"
#include "QtPluginManager.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QLayout>
#include <QScreen>
#include <QTimer>
#include <QToolBar>
#include <QWindow>
#include <functional>

namespace
{
	// the screen the window is (mostly) on, else the primary one
	QScreen* ScreenFor(const QRect& r)
	{
		QScreen* best = nullptr;
		int area = 0;
		for (QScreen* s : QGuiApplication::screens())
		{
			const QRect i = s->availableGeometry().intersected(r);
			if (i.width() * i.height() > area) { area = i.width() * i.height(); best = s; }
		}
		return best ? best : QGuiApplication::primaryScreen();
	}

	// moves (and if needed shrinks) a top-level rect into the available area
	QRect FitRect(QRect r, const QRect& avail, const QSize& frame_extra)
	{
		const int maxw = avail.width() - frame_extra.width();
		const int maxh = avail.height() - frame_extra.height();
		if (r.width() > maxw) r.setWidth(maxw);
		if (r.height() > maxh) r.setHeight(maxh);
		const int top = frame_extra.height();   // title bar above the client rect
		if (r.right() > avail.right()) r.moveRight(avail.right());
		if (r.bottom() > avail.bottom()) r.moveBottom(avail.bottom());
		if (r.left() < avail.left()) r.moveLeft(avail.left());
		if (r.top() < avail.top() + top) r.moveTop(avail.top() + top);
		return r;
	}
}

void MainWindow::FitToScreen()
{
	if (isFullScreen()) return;
	QScreen* scr = ScreenFor(frameGeometry());
	if (!scr) return;
	const QRect avail = scr->availableGeometry();
	// frame (title bar) size; before the window is shown it is unknown:
	// macOS title bar ~28 px
	QSize extra = frameGeometry().size() - geometry().size();
	if (!isVisible() || extra.width() < 0 || extra.height() <= 0) extra = QSize(0, 28);
	if (isMaximized())
	{
		// a maximized window wider than the screen (minimum size larger than
		// the screen when it was saved): un-maximize to a fitting normal size
		if (frameGeometry().width() <= avail.width() + 1 && frameGeometry().height() <= avail.height() + 1)
			return;
		showNormal();
	}
	const QRect fit = FitRect(geometry(), avail, extra);
	if (fit != geometry())
		setGeometry(fit);

	// floating docks (outer and inner window, plugin panes) restored
	// off-screen: back onto the window's screen
	QList<QDockWidget*> docks = findChildren<QDockWidget*>();
	for (QDockWidget* d : docks)
	{
		if (!d->isFloating()) continue;
		const QRect g = d->frameGeometry();
		bool on = false;
		for (QScreen* s : QGuiApplication::screens())
		{
			const QRect i = s->availableGeometry().intersected(g);
			if (i.width() >= qMin(100, g.width()) && i.height() >= qMin(40, g.height())) { on = true; break; }
		}
		if (!on || g.width() > avail.width() || g.height() > avail.height())
			d->setGeometry(FitRect(d->geometry(), avail, QSize(0, 20)));
	}
}

// ---------------------------------------------------------------------------
// VVD_QT_TEST=layout: sizes of the main window and its panes against the
// screen, dock resizing, small window sizes; "[layout]" lines, pictures in
// VVD_QT_TEST_DIR (default: the folder of VVD_QT_GRAB). Quits when done.
// ---------------------------------------------------------------------------

namespace
{
	int MinW(QWidget* w)
	{
		// QLayout's effective minimum (qSmartMinSize)
		const QSize ms = w->minimumSize(), mh = w->minimumSizeHint();
		int x = ms.width() > 0 ? ms.width() : mh.width();
		if (w->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored && ms.width() <= 0) x = 0;
		return qMin(x, w->maximumWidth());
	}

	QString Desc(QWidget* w)
	{
		QString n = w->objectName();
		QString t;
		if (auto* d = qobject_cast<QDockWidget*>(w)) t = d->windowTitle();
		return QString("%1%2%3").arg(w->metaObject()->className()).arg(n.isEmpty() ? QString() : " #" + n)
			.arg(t.isEmpty() ? QString() : " '" + t + "'");
	}

	// widgets under w whose minimum width is >= thr (the widest chain)
	void DumpWide(QWidget* w, int thr, int depth, int maxdepth)
	{
		if (depth > maxdepth) return;
		for (QObject* o : w->children())
		{
			auto* c = qobject_cast<QWidget*>(o);
			if (!c || c->isWindow() || c->isHidden()) continue;
			const int mw = MinW(c);
			if (mw < thr) continue;
			fprintf(stderr, "[layout]   %*s%s min %d (minSize %d hint %d) w %d\n", depth * 2, "",
				Desc(c).toLocal8Bit().constData(), mw, c->minimumWidth(), c->minimumSizeHint().width(), c->width());
			DumpWide(c, thr, depth + 1, maxdepth);
		}
	}
}

void MainWindow::RunLayoutTest(const QString& kind)
{
	// VVD_QT_LAYOUT_SIZE=WxH: the window's size first (pictures at a given
	// size, e.g. to compare builds)
	const QString fs = qEnvironmentVariable("VVD_QT_LAYOUT_SIZE");
	if (!fs.isEmpty() && kind != "layout_sized")
	{
		showNormal();
		resize(fs.section('x', 0, 0).toInt(), fs.section('x', 1, 1).toInt());
		QTimer::singleShot(1000, this, [this]() { RunLayoutTest("layout_sized"); });
		return;
	}
	QString dir = qEnvironmentVariable("VVD_QT_TEST_DIR");
	if (dir.isEmpty()) dir = QFileInfo(qEnvironmentVariable("VVD_QT_GRAB")).absolutePath();
	auto report = [this](const char* tag) {
		QScreen* s = screen();
		const QRect av = s ? s->availableGeometry() : QRect();
		fprintf(stderr, "[layout] == %s\n", tag);
		fprintf(stderr, "[layout] screen %d,%d %dx%d available %d,%d %dx%d dpr %.1f\n",
			s ? s->geometry().x() : 0, s ? s->geometry().y() : 0, s ? s->geometry().width() : 0, s ? s->geometry().height() : 0,
			av.x(), av.y(), av.width(), av.height(), s ? s->devicePixelRatio() : 0.0);
		const QRect g = geometry(), f = frameGeometry();
		fprintf(stderr, "[layout] window geometry %d,%d %dx%d frame %d,%d %dx%d maximized %d fits %d\n",
			g.x(), g.y(), g.width(), g.height(), f.x(), f.y(), f.width(), f.height(), isMaximized() ? 1 : 0,
			av.contains(f) ? 1 : 0);
		fprintf(stderr, "[layout] window minimumSizeHint %dx%d minimumSize %dx%d inner minHint %dx%d viewarea minHint %dx%d\n",
			minimumSizeHint().width(), minimumSizeHint().height(), minimumSize().width(), minimumSize().height(),
			m_inner->minimumSizeHint().width(), m_inner->minimumSizeHint().height(),
			m_view_area->minimumSizeHint().width(), m_view_area->minimumSizeHint().height());
		for (QToolBar* tb : findChildren<QToolBar*>())
			if (!tb->isHidden())
				fprintf(stderr, "[layout] toolbar %s min %dx%d hint %dx%d w %d\n", Desc(tb).toLocal8Bit().constData(),
					tb->minimumSizeHint().width(), tb->minimumSizeHint().height(), tb->sizeHint().width(), tb->sizeHint().height(), tb->width());
		for (QDockWidget* d : findChildren<QDockWidget*>())
		{
			QWidget* c = d->widget();
			fprintf(stderr, "[layout] dock %s owner %s %s%s geom %d,%d %dx%d dock min %dx%d widget min %d (minSize %d hint %dx%d)\n",
				Desc(d).toLocal8Bit().constData(), d->parentWidget() ? Desc(d->parentWidget()).toLocal8Bit().constData() : "-",
				d->isHidden() ? "hidden" : "shown", d->isFloating() ? " floating" : "",
				d->x(), d->y(), d->width(), d->height(), d->minimumSizeHint().width(), d->minimumSizeHint().height(),
				c ? MinW(c) : -1, c ? c->minimumWidth() : -1, c ? c->minimumSizeHint().width() : -1, c ? c->minimumSizeHint().height() : -1);
		}
		fprintf(stderr, "[layout] view 0 frame min %d hint %dx%d\n", MinW(m_views[0]->frame),
			m_views[0]->frame->minimumSizeHint().width(), m_views[0]->frame->minimumSizeHint().height());
		DumpWide(this, 300, 0, 9);
	};
	auto grab = [this, dir](const QString& n) { this->grab().save(dir + "/layout_" + n + ".png"); };
	auto at = [this](int ms, std::function<void()> f) { QTimer::singleShot(ms, this, f); };

	report("start-up");
	grab("startup");
	int t = 300;
	// dock separators: shrink each dock to a small size
	at(t, [=]() {
		resizeDocks({ m_tree_dock }, { 120 }, Qt::Horizontal);
		resizeDocks({ m_prop_dock }, { 60 }, Qt::Vertical);
		m_inner->resizeDocks({ m_adjust_dock }, { 40 }, Qt::Horizontal);
		m_inner->resizeDocks({ m_clip_dock }, { 40 }, Qt::Horizontal);
	});
	at(t += 500, [=]() {
		fprintf(stderr, "[layout] resizeDocks small: Workspace w %d (min %d) Properties h %d (min %d) Adjust w %d (min %d) Clip w %d (min %d)\n",
			m_tree_dock->width(), m_tree_dock->minimumSizeHint().width(), m_prop_dock->height(), m_prop_dock->minimumSizeHint().height(),
			m_adjust_dock->width(), m_adjust_dock->minimumSizeHint().width(), m_clip_dock->width(), m_clip_dock->minimumSizeHint().width());
		grab("docks_small");
		resizeDocks({ m_tree_dock }, { 320 }, Qt::Horizontal);
		resizeDocks({ m_prop_dock }, { 200 }, Qt::Vertical);
		m_inner->resizeDocks({ m_adjust_dock }, { 125 }, Qt::Horizontal);
		m_inner->resizeDocks({ m_clip_dock }, { 160 }, Qt::Horizontal);
	});
	for (QSize sz : { QSize(1280, 800), QSize(1024, 768) })
	{
		at(t += 600, [=]() { showNormal(); resize(sz.width(), sz.height() - 28); });
		at(t += 900, [=]() {
			const QString tag = QString("%1x%2").arg(sz.width()).arg(sz.height());
			report(tag.toLocal8Bit().constData());
			fprintf(stderr, "[layout] %s request %dx%d -> window %dx%d %s; render view %dx%d\n", tag.toLocal8Bit().constData(),
				sz.width(), sz.height() - 28, width(), height(), width() <= sz.width() ? "PASS" : "FAIL",
				m_views[0]->frame->width(), m_views[0]->frame->height());
			grab(tag);
		});
	}
	// the plugin panes (wx: Left().Layer(3))
	if (m_plugin_manager && !qEnvironmentVariableIsSet("VVD_QT_LAYOUT_NO_PLUGINS"))
	{
		at(t += 300, [=]() {
			for (const QString& n : m_plugin_list)
				if (m_plugin_manager->GetGuiPlugin(n))
					ToggleVisibilityPluginWindow(n, true);
		});
		at(t += 1200, [=]() {
			report("plugins shown, 1024x768");
			fprintf(stderr, "[layout] plugins shown: window %dx%d %s\n", width(), height(), width() <= 1024 ? "PASS" : "FAIL");
			grab("plugins_1024");
		});
	}
	// a floating dock off-screen is moved back by FitToScreen
	at(t += 300, [=]() {
		m_clip_dock->setFloating(true);
		m_clip_dock->move(6000, 4000);
	});
	at(t += 500, [=]() {
		const QRect before = m_clip_dock->frameGeometry();
		FitToScreen();
		const QRect g = m_clip_dock->frameGeometry();
		const QRect av = screen() ? screen()->availableGeometry() : QRect();
		fprintf(stderr, "[layout] floating dock off-screen %d,%d -> %d,%d %dx%d %s\n", before.x(), before.y(),
			g.x(), g.y(), g.width(), g.height(), av.contains(g) ? "PASS" : "FAIL");
		m_clip_dock->setFloating(false);
	});
	at(t += 300, [=]() { QCoreApplication::quit(); });
}
