// Entry point of the Qt front end (target VVDViewerQt).

#include <QApplication>
#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QUrl>
#include <QSurfaceFormat>
#include <QTimer>
#include <QFileInfo>
#include <QPixmap>

#include "MainWindow.h"
#include "TestRun.h"
#include "RenderCanvas.h"
#include "ViewPanel.h"
#include "CaptureDialog.h"
#include "MoviePanel.h"
#include "RecorderPanel.h"
#include <QDir>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QRadioButton>
#include <QSpinBox>
#include <memory>
#include "QtUIBackend.h"
#include "Core/FileUtil.h"

int main(int argc, char* argv[])
{
#if defined(__APPLE__)
	// Qt 6.7+ backs Metal- and Vulkan-surface windows with its own QMetalLayer
	// (presentsWithTransaction + a display lock) unless this is set. That layer
	// is meant for Qt's own Metal renderer, which presents from the main thread
	// inside the display cycle. MoltenVK presents from a Metal completion
	// thread; with presentsWithTransaction on, that present blocks on the main
	// thread's CATransaction, the main thread (rendering inside the very same
	// transaction) blocks in nextDrawable waiting for a drawable the blocked
	// present would have released, and the app hangs at start-up. Ask Qt for
	// a plain CAMetalLayer instead. Must be set before the first window's
	// backing layer is created; Qt reads it once.
	if (!qEnvironmentVariableIsSet("QT_MTL_NO_TRANSACTION"))
		qputenv("QT_MTL_NO_TRANSACTION", "1");
#endif
	QApplication app(argc, argv);
	QCoreApplication::setOrganizationName("Janelia");
	QCoreApplication::setApplicationName("VVDViewerQt");
	QCoreApplication::setApplicationVersion(QString("%1.%2").arg(VERSION_MAJOR).arg(VERSION_MINOR));

	// Route engine messages and progress reports to Qt dialogs.
	QtUIBackend::Install();
	vvd::SetAppName(FLUORENDER_TITLE);

	// wx Main.cpp g_cmdLineDesc: -p/--plugin, -d/--desc (repeatable), -m/--macro,
	// input files (a "vvd:path1,path2" argument = the web-browser launch)
	QCommandLineParser parser;
	parser.setApplicationDescription("VVDViewer - Voxel Volume Data Viewer (Qt front end)");
	parser.addHelpOption();
	parser.addVersionOption();
	QCommandLineOption opt_plugin(QStringList() << "p" << "plugin", "Run a plugin: \"<name> <params>\".", "plugin");
	QCommandLineOption opt_desc(QStringList() << "d" << "desc", "Name prefix of the n-th input file (repeat per file).", "desc");
	QCommandLineOption opt_macro(QStringList() << "m" << "macro", "Comma-separated tasks: hideui, showui, basic, advanced, record,<file>.", "tasks");
	parser.addOption(opt_plugin);
	parser.addOption(opt_desc);
	parser.addOption(opt_macro);
	parser.addPositionalArgument("files", "Volume / mesh files or a .vrp project to open", "[files...]");
	parser.process(app);

	QStringList files;
	for (const QString& f : parser.positionalArguments())
	{
		if (f.startsWith("vvd:"))
		{
			// wx m_open_by_web_browser: "vvd:" + comma-separated paths
			if (f.length() >= 5)
				for (const QString& p : f.mid(4).split(',', Qt::SkipEmptyParts))
					files << p;
		}
		else
			files << f;
	}
	const QStringList descs = parser.values(opt_desc);
	QString plugin_name, plugin_params;
	if (parser.isSet(opt_plugin))
	{
		const QString val = parser.value(opt_plugin);
		plugin_name = val.section(' ', 0, 0);
		plugin_params = val.section(' ', 1);
		for (const QString& d : descs) plugin_params += " -d " + d;
	}

	MainWindow w;
	w.show();

	// macOS: files / vvd: URLs opened from Finder or the Dock arrive as
	// QFileOpenEvent (wx VRenderApp::MacOpenFiles -> StartupLoad). Events for
	// the command-line files themselves are ignored (Cocoa may report argv
	// documents as open requests too).
	struct FileOpenFilter : QObject
	{
		MainWindow* w;
		QStringList startup;
		bool eventFilter(QObject* o, QEvent* e) override
		{
			if (e->type() != QEvent::FileOpen)
				return QObject::eventFilter(o, e);
			auto* fe = static_cast<QFileOpenEvent*>(e);
			QStringList list;
			const QUrl url = fe->url();
			if (url.scheme() == "vvd")
			{
				const QString str = url.toString();
				if (str.length() >= 5)
					for (const QString& p : str.mid(4).split(',', Qt::SkipEmptyParts)) list << p;
			}
			else if (!fe->file().isEmpty())
				list << fe->file();
			const int i = list.size() == 1 ? startup.indexOf(list.first()) : -1;
			if (i >= 0) { startup.removeAt(i); return true; }
			if (qEnvironmentVariableIsSet("VVD_QT_LOAD_TIMING") || qEnvironmentVariableIsSet("VVD_QT_TEST"))
				fprintf(stderr, "[fileopen] %s\n", list.join(',').toLocal8Bit().constData());
			if (!list.isEmpty())
				QTimer::singleShot(0, w, [mw = w, list]() { mw->StartupLoad(list); });
			return true;
		}
	};
	auto* fof = new FileOpenFilter;
	fof->w = &w;
	for (const QString& f : files) fof->startup << QFileInfo(f).absoluteFilePath() << f;
	app.installEventFilter(fof);

	// wx OnInit: StartupLoad(m_files, 0, m_descs), RunPlugin, SetTasks
	if (!files.isEmpty()) w.StartupLoad(files, descs);
	// wx: RunPlugin(m_plugin_name, m_plugin_params, true)
	if (!plugin_name.isEmpty() && !w.RunPlugin(plugin_name, plugin_params, true))
		fprintf(stderr, "VVDViewer: no plugin called \"%s\" (-p %s %s)\n", plugin_name.toLocal8Bit().constData(),
			plugin_name.toLocal8Bit().constData(), plugin_params.toLocal8Bit().constData());
	if (parser.isSet(opt_macro))
		w.SetTasks(parser.value(opt_macro));

	// Debug aid: VVD_QT_GRAB=<file.png> saves the window layout (widgets only;
	// the Vulkan surface is not captured) a few seconds after start-up and quits.
	const QString grab = qEnvironmentVariable("VVD_QT_GRAB");
	if (!grab.isEmpty())
	{
		const QString test = qEnvironmentVariable("VVD_QT_TEST");
		if (!test.isEmpty())
			QTimer::singleShot(3000, &w, [&w, test]() { w.RunTestScript(test); });
		// paint / calc / seg (MainWindowPaintTest.cpp) save their own
		// captures and quit by themselves
		if (VVDQtPaintTestKind(test))
			;
		else
		QTimer::singleShot(test.isEmpty() ? 4000 : ((test == "dialogs" || test == "tree") ? 14000 : 7000), &w, [&w, grab]() {
			// VVD_QT_SAVE=<file.vrp>: also save the project (round-trip testing)
			const QString save = qEnvironmentVariable("VVD_QT_SAVE");
			if (!save.isEmpty()) w.SaveProject(save);
			w.grab().save(grab);
			// VVD_QT_GRAB_VIEW=<file.png>: also save the 3D view's own pixels
			const QString view = qEnvironmentVariable("VVD_QT_GRAB_VIEW");
			if (view.isEmpty()) { QCoreApplication::quit(); return; }
			QObject::connect(w.Canvas(), &RenderCanvas::frameCaptured, &w, [](const QString&, bool) { QCoreApplication::quit(); });
			w.Canvas()->CaptureToFile(view);
		});
	}

	// Testing aid: VVD_QT_CAPTURE=<file.tif> runs the render view's "Capture"
	// button without its dialog (CaptureDialog::Execute) after
	// VVD_QT_CAPTURE_DELAY ms (default 4000), then quits. Options:
	//   VVD_QT_CAPTURE_SIZE      WxH | <k>x (k times the view size) | 1:1 | disp (default: dialog default)
	//   VVD_QT_CAPTURE_COMPRESS  0/1 (default: the "/save cmp" setting; the setting is not changed)
	//   VVD_QT_CAPTURE_GROUP     1: "Capture each group separately"
	//   VVD_QT_CAPTURE_PROJECT   0/1: overrides the "Save project when capture" setting
	//   VVD_QT_GRAB_VIEW         also save the presented 3D view afterwards (PNG)
	const QString cap = qEnvironmentVariable("VVD_QT_CAPTURE");
	if (!cap.isEmpty())
	{
		bool dok = false;
		int delay = qEnvironmentVariableIntValue("VVD_QT_CAPTURE_DELAY", &dok);
		QTimer::singleShot(dok ? delay : 4000, &w, [&w, cap]() {
			RenderCanvas* canvas = w.Canvas();
			const QSize disp = canvas->GetDeviceSize();
			CaptureDialog::Options opt;
			opt.resx = canvas->GetCaptureResX() > 0 ? canvas->GetCaptureResX() : disp.width();
			opt.resy = canvas->GetCaptureResY() > 0 ? canvas->GetCaptureResY() : disp.height();
			const QString size = qEnvironmentVariable("VVD_QT_CAPTURE_SIZE").toLower();
			if (size == "1:1")
			{
				canvas->Get1x1DispSize(opt.resx, opt.resy);
				if (opt.resx <= 0) opt.resx = disp.width();
				if (opt.resy <= 0) opt.resy = disp.height();
			}
			else if (size == "disp") { opt.resx = disp.width(); opt.resy = disp.height(); }
			else if (size.endsWith('x')) { double k = size.chopped(1).toDouble(); opt.resx = int(disp.width() * k); opt.resy = int(disp.height() * k); }
			else if (size.contains('x')) { opt.resx = size.section('x', 0, 0).toInt(); opt.resy = size.section('x', 1, 1).toInt(); }
			opt.compress = qEnvironmentVariableIsSet("VVD_QT_CAPTURE_COMPRESS") ?
				qEnvironmentVariableIntValue("VVD_QT_CAPTURE_COMPRESS") != 0 : true;
			opt.group_each = qEnvironmentVariableIntValue("VVD_QT_CAPTURE_GROUP") != 0;
			opt.proj_save = qEnvironmentVariableIsSet("VVD_QT_CAPTURE_PROJECT") ?
				qEnvironmentVariableIntValue("VVD_QT_CAPTURE_PROJECT") != 0 : false;
			fprintf(stderr, "VVD_QT_CAPTURE: view %dx%d, capture %dx%d, compress %d, group %d, project %d -> %s\n",
				disp.width(), disp.height(), opt.resx, opt.resy, (int)opt.compress, (int)opt.group_each,
				(int)opt.proj_save, cap.toLocal8Bit().constData());
			auto finish = [&w]() {
				const QString view = qEnvironmentVariable("VVD_QT_GRAB_VIEW");
				if (view.isEmpty() || !qEnvironmentVariable("VVD_QT_GRAB").isEmpty()) { QCoreApplication::quit(); return; }
				QObject::connect(w.Canvas(), &RenderCanvas::frameCaptured, &w, [](const QString&, bool) { QCoreApplication::quit(); });
				w.Canvas()->CaptureToFile(view);
			};
			QObject::connect(canvas, &RenderCanvas::captureFinished, &w, [canvas, finish](const QString& f, bool ok) {
				fprintf(stderr, "VVD_QT_CAPTURE: wrote %s: %s\n", f.toLocal8Bit().constData(), ok ? "ok" : "FAILED");
				if (!canvas->IsCapturing()) QTimer::singleShot(500, finish);
			});
			QObject::connect(canvas, &RenderCanvas::groupCaptureFinished, &w, [finish]() {
				fprintf(stderr, "VVD_QT_CAPTURE: group capture finished\n");
				QTimer::singleShot(500, finish);
			});
			w.View()->Capture(cap, opt);
		});
	}

	// Testing aid: VVD_QT_MOVIE=<file.mp4|.mov|.tif> runs the Record/Export
	// "Save..." without its dialog (rewind + VMovieView::SaveMovie) after
	// VVD_QT_MOVIE_DELAY ms (default 5000), then quits when the movie is
	// written. Options (Basic tab unless VVD_QT_MOVIE_KEYS is given):
	//   VVD_QT_MOVIE_LEN    movie length in s (default 2)
	//   VVD_QT_MOVIE_FPS    frames per second (default 30)
	//   VVD_QT_MOVIE_AXIS   x | y | z (default y)
	//   VVD_QT_MOVIE_DEG    degrees (default 360)
	//   VVD_QT_MOVIE_STYLE  0 linear / 1 smooth
	//   VVD_QT_MOVIE_REWIND 1: "Rewind" check box
	//   VVD_QT_MOVIE_ROT    0: "Rotation" off
	//   VVD_QT_MOVIE_SEQ    1: "Time Sequence / Batch" on (4D series / folder batch)
	//   VVD_QT_MOVIE_CROP   1: "Enable cropping" (default frame), or "cx,cy,w,h"
	//   VVD_QT_MOVIE_KEYS   "rx,ry,rz,zoom;rx,ry,rz,zoom;..." keyframes added with
	//                       "Add" (view rotation + zoom set first); the movie is
	//                       then recorded from the Advanced tab
	//   VVD_QT_MOVIE_KEYDUR default duration (inbetweens) of the keys (default 30)
	//   VVD_QT_MOVIE_PRJ    <file.vrp>: save the project after the movie (round trip)
	//   VVD_QT_GRAB_VIEW    <file.png>: the presented view after the movie
	// VVD_QT_GRAB_MOVPANEL=<dir>: pictures of the four tabs of the pane
	// (movie_tab_<n>.png) and of the "Save..." dialog (movie_save_dialog.png),
	// then quit.
	const QString grab_mov = qEnvironmentVariable("VVD_QT_GRAB_MOVPANEL");
	if (!grab_mov.isEmpty())
	{
		QTimer::singleShot(3000, &w, [&w, grab_mov]() {
			MoviePanel* mp = w.Movie();
			QDir().mkpath(grab_mov);
			for (int i = 0; i < 4; ++i)
			{
				mp->SetMode(i);
				// Advanced tab: the in-place editors of the last key (a loaded project's keys)
				if (i == 1 && mp->GetRecorder()->GetKeyList()->topLevelItemCount() > 0)
					mp->GetRecorder()->GetKeyList()->SelectRow(mp->GetRecorder()->GetKeyList()->topLevelItemCount() - 1);
				QCoreApplication::processEvents();
				mp->grab().save(QDir(grab_mov).filePath(QString("movie_tab_%1.png").arg(i)));
			}
			mp->SetMode(0);
			qputenv("VVD_QT_GRAB_MOVDLG", QDir(grab_mov).filePath("movie_save_dialog.png").toLocal8Bit());
			mp->RunSaveDialog();
			w.grab().save(QDir(grab_mov).filePath("window.png"));
			QCoreApplication::quit();
		});
	}
	// Testing aid: VVD_QT_FILEDLG_RUN="step,step,..." runs file dialogs and
	// tree save actions (MainWindow::RunFileDialogTest) after
	// VVD_QT_FILEDLG_DELAY ms (default 3000), then quits. Files and options of
	// each dialog: VVD_QT_FILEDLG_FILE_<NAME>, VVD_QT_FILEDLG_OPTS_<NAME>;
	// pictures: VVD_QT_GRAB_FILEDLG=<dir> (FileDialogs.h).
	const QString fdlg_run = qEnvironmentVariable("VVD_QT_FILEDLG_RUN");
	if (!fdlg_run.isEmpty())
	{
		bool dok = false;
		int delay = qEnvironmentVariableIntValue("VVD_QT_FILEDLG_DELAY", &dok);
		QTimer::singleShot(dok ? delay : 3000, &w, [&w, fdlg_run]() {
			w.RunFileDialogTest(fdlg_run);
			QTimer::singleShot(500, []() { QCoreApplication::quit(); });
		});
	}
	const QString movie = qEnvironmentVariable("VVD_QT_MOVIE");
	if (!movie.isEmpty())
	{
		bool dok = false;
		int delay = qEnvironmentVariableIntValue("VVD_QT_MOVIE_DELAY", &dok);
		QTimer::singleShot(dok ? delay : 5000, &w, [&w, movie]() {
			MoviePanel* mp = w.Movie();
			RenderCanvas* canvas = w.Canvas();
			auto env = [](const char* k, const QString& def) {
				const QString v = qEnvironmentVariable(k);
				return v.isEmpty() ? def : v;
			};
			mp->m_fps_text->setText(env("VVD_QT_MOVIE_FPS", "30"));
			const QString axis = env("VVD_QT_MOVIE_AXIS", "y").toLower();
			(axis == "x" ? mp->m_x_rd : axis == "z" ? mp->m_z_rd : mp->m_y_rd)->setChecked(true);
			mp->m_degree_end->setText(env("VVD_QT_MOVIE_DEG", "360"));
			const int style = env("VVD_QT_MOVIE_STYLE", "0").toInt();
			mp->m_rot_int_cmb->setCurrentIndex(style);
			Q_EMIT mp->m_rot_int_cmb->activated(style);
			mp->m_rewind_chk->setChecked(env("VVD_QT_MOVIE_REWIND", "0") == "1");
			if (!qEnvironmentVariableIsSet("VVD_QT_MOVIE_KEEP"))   // VVD_QT_MOVIE_KEEP: keep the pane's values (loaded project)
				mp->m_movie_time->setText(env("VVD_QT_MOVIE_LEN", "2"));
			// VVD_QT_MOVIE_ROT=0: "Rotation" off; VVD_QT_MOVIE_SEQ=1/0: "Time Sequence / Batch"
			if (qEnvironmentVariableIsSet("VVD_QT_MOVIE_ROT") && mp->m_rot_chk->isChecked() != (qEnvironmentVariableIntValue("VVD_QT_MOVIE_ROT") != 0))
				mp->m_rot_chk->click();
			if (qEnvironmentVariableIsSet("VVD_QT_MOVIE_SEQ") && mp->m_seq_chk->isChecked() != (qEnvironmentVariableIntValue("VVD_QT_MOVIE_SEQ") != 0))
				mp->m_seq_chk->click();
			if (qEnvironmentVariableIsSet("VVD_QT_MOVIE_LEN") && mp->m_seq_chk->isChecked())
				mp->m_movie_time->setText(qEnvironmentVariable("VVD_QT_MOVIE_LEN"));
			fprintf(stderr, "VVD_QT_MOVIE: rotation %d, sequence %d (start %s, end %s, current %s)\n",
				(int)mp->m_rot_chk->isChecked(), (int)mp->m_seq_chk->isChecked(),
				mp->m_time_start_text->text().toLocal8Bit().constData(), mp->m_time_end_text->text().toLocal8Bit().constData(),
				mp->m_time_current_text->text().toLocal8Bit().constData());
			const QString crop = qEnvironmentVariable("VVD_QT_MOVIE_CROP");
			if (!crop.isEmpty() && crop != "0")
			{
				mp->SetMode(MoviePanel::MODE_CROPPING);
				mp->m_frame_chk->click();
				const QStringList c = crop.split(',');
				if (c.size() == 4)
				{
					mp->m_width->setValue(c[2].toInt());
					mp->m_height->setValue(c[3].toInt());
					mp->m_center_x->setValue(c[0].toInt());
					mp->m_center_y->setValue(c[1].toInt());
				}
				mp->SetMode(MoviePanel::MODE_BASIC);
			}
			const QString keys = qEnvironmentVariable("VVD_QT_MOVIE_KEYS");
			if (!keys.isEmpty())
			{
				RecorderPanel* rec = mp->GetRecorder();
				rec->DurationText()->setText(env("VVD_QT_MOVIE_KEYDUR", "30"));
				for (const QString& k : keys.split(';', Qt::SkipEmptyParts))
				{
					const QStringList p = k.split(',');
					if (p.size() < 3) continue;
					canvas->SetRotations(p[0].toDouble(), p[1].toDouble(), p[2].toDouble());
					if (p.size() > 3) canvas->SetScaleFactor(p[3].toDouble());
					rec->AddKey();
				}
				mp->SetMode(MoviePanel::MODE_ADVANCED);
			}
			// VVD_QT_MOVIE_MODE=<tab>: e.g. 1 = Advanced (keys of a loaded project)
			if (qEnvironmentVariableIsSet("VVD_QT_MOVIE_MODE"))
				mp->SetMode(qEnvironmentVariableIntValue("VVD_QT_MOVIE_MODE"));
			const QSize disp = canvas->GetDeviceSize();
			int fx, fy, fw, fh;
			canvas->GetFrame(fx, fy, fw, fh);
			fprintf(stderr, "VVD_QT_MOVIE: view %dx%d, fps %ld, length %s s, page %d, crop %d (%d,%d %dx%d) -> %s\n",
				disp.width(), disp.height(), mp->GetFPS(), mp->m_movie_time->text().toLocal8Bit().constData(),
				mp->GetMode(), (int)canvas->GetFrameEnabled(), fx, fy, fw, fh, movie.toLocal8Bit().constData());
			auto t0 = std::make_shared<QElapsedTimer>();
			t0->start();
			QObject::connect(mp, &MoviePanel::movieSaved, &w, [&w, t0](const QString& f, bool ok) {
				fprintf(stderr, "VVD_QT_MOVIE: wrote %s: %s (%.1f s)\n", f.toLocal8Bit().constData(),
					ok ? "ok" : "FAILED", t0->elapsed() / 1000.0);
				const QString prj = qEnvironmentVariable("VVD_QT_MOVIE_PRJ");
				if (!prj.isEmpty()) w.SaveProject(prj);
				// VVD_QT_GRAB_VIEW: also save the presented view (frame border included)
				const QString view = qEnvironmentVariable("VVD_QT_GRAB_VIEW");
				if (!view.isEmpty())
				{
					QObject::connect(w.Canvas(), &RenderCanvas::frameCaptured, &w, [](const QString&, bool) { QCoreApplication::quit(); });
					w.Canvas()->CaptureToFile(view);
					return;
				}
				QTimer::singleShot(300, []() { QCoreApplication::quit(); });
			});
			mp->Rewind();
			mp->SaveMovie(movie);
		});
	}

	return app.exec();
}
