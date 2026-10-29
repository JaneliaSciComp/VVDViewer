#include "MainWindow.h"
#include "TestRun.h"

#include <QApplication>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include "PaneScrollArea.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QColorDialog>
#include <QStatusBar>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QSettings>
#include <QFileInfo>
#include <QStyle>
#include <QWindow>
#include <QLabel>
#include <QVBoxLayout>
#include <cmath>
#include <QToolButton>
#include <QMenu>
#include <QProcess>
#include <QPixmap>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <memory>
#include <filesystem>

#include "RenderCanvas.h"
#include "TreePanel.h"
#include "VolumePropPanel.h"
#include "ViewPanel.h"
#include <QTimer>
#include <QPointer>
#include <QDir>
#include <QRegularExpression>
#include <QDirIterator>
#include <QCursor>
#include <QDateTime>
#include "AdjustPanel.h"
#include "ClipPanel.h"
#include "MoviePanel.h"
#include "RecorderPanel.h"
#include "Animator/Interpolator.h"
#include "InfoDialog.h"
#include "SettingsDialog.h"
#include "MeshPropPanel.h"
#include "AnnotationPropPanel.h"
#include "MeshIO.h"
#include "ProjectIO.h"
#include "VolumePrefetcher.h"
#include "RulerTool.h"
#include "ViewOverlays.h"
#include "BrushTool.h"
#include "BrushToolDialog.h"
#include "MeasureDialog.h"
#include "TraceDialog.h"
#include "LegendMenu.h"
#include "QtPluginManager.h"
#include <QListWidget>
#include <QScrollBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include "NoiseReductionDialog.h"
#include "CountingDialog.h"
#include "ColocalizationDialog.h"
#include "ConvertDialog.h"
#include "img/icons.h"
#include "DataManager.h"
#include "Formats/brkxml_reader.h"
#include "Core/FileUtil.h"
#include "FLIVR/Color.h"
#include "FLIVR/TextureBrick.h"
#include <curl/curl.h>

// Process-wide curl handles used by the brick loader (remote volumes). The wx
// frame defines and initialises these in VRenderFrame.cpp; the Qt front end
// owns them here.
CURLM* _g_curlm = nullptr;
CURL* _g_curl = nullptr;

namespace
{
	// same suffix -> loader type table as VRenderFrame::LoadVolumes
	int LoadTypeForSuffix(const QString& suffix_in)
	{
		QString s = suffix_in.toLower();
		if (s == ".nrrd") return LOAD_TYPE_NRRD;
		if (s == ".tif" || s == ".tiff" || s == ".zip") return LOAD_TYPE_TIFF;
		if (s == ".oib") return LOAD_TYPE_OIB;
		if (s == ".oif") return LOAD_TYPE_OIF;
		if (s == ".lsm") return LOAD_TYPE_LSM;
		if (s == ".czi") return LOAD_TYPE_CZI;
		if (s == ".vvd" || s == ".n5" || s == ".json" || s == ".n5fs_ch" || s == ".xml" || s == ".zarr" ||
			s == ".zattrs" || s == ".zfs_ch") return LOAD_TYPE_BRKXML;
		if (s == ".h5j") return LOAD_TYPE_H5J;
		if (s == ".v3dpbd") return LOAD_TYPE_V3DPBD;
		if (s == ".idi") return LOAD_TYPE_IDI;
#ifndef _DARWIN
		if (s == ".nd2") return LOAD_TYPE_ND2;
#endif
		return 0;
	}

	const char* kVolumeFilter =
		"All supported volumes (*.tif *.tiff *.nrrd *.oib *.oif *.lsm *.czi *.h5j *.v3dpbd *.vvd *.n5 *.zarr *.zgroup *.zattrs *.json *.xml *.idi *.zip *.nd2);;"
		"TIFF (*.tif *.tiff *.zip);;NRRD (*.nrrd);;Olympus (*.oib *.oif);;Zeiss (*.lsm *.czi);;"
		"Janelia H5J (*.h5j);;Vaa3D (*.v3dpbd);;Bricked (*.vvd *.n5 *.zarr *.json *.xml);;Zarr files (*.zarr *.zgroup *.zattrs);;N5 files (*.n5 *.json);;All files (*)";
}

MainWindow::MainWindow(QWidget* parent)
	: QMainWindow(parent)
{
	setWindowTitle(QString("%1 %2.%3 (Qt)").arg(FLUORENDER_TITLE).arg(VERSION_MAJOR).arg(VERSION_MINOR));
	setAcceptDrops(true);
	resize(1400, 900);

	if (!_g_curlm)
	{
		curl_global_init(CURL_GLOBAL_ALL);
		_g_curlm = curl_multi_init();
		_g_curl = curl_easy_init();
		FLIVR::TextureBrick::setCURL(_g_curl);
		FLIVR::TextureBrick::setCURL_Multi(_g_curlm);
	}

	m_data_mgr = new DataManager();
	m_selector.SetDataManager(m_data_mgr);
	m_settings = new SettingsDialog(this);
	m_settings->SetDataManager(m_data_mgr);
	m_settings->ApplyToEngine();

	// inner window: Output Adjustments | render views | Clipping Planes. The
	// views live in m_view_area: view 0 ("Render View:1") is its centre,
	// further views are docked to its right (wxAUI Layer(0).Centre().Right()
	// panes, MainWindowViews.cpp)
	m_inner = new QMainWindow(this);
	m_inner->setWindowFlags(Qt::Widget);
	m_inner->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);
	m_view_area = new QMainWindow(m_inner);
	m_view_area->setObjectName("RenderViewArea");
	m_view_area->setWindowFlags(Qt::Widget);
	m_view_area->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);
	{
		ViewSlot* v0 = NewViewSlot(1);
		m_view = v0->view;
		m_canvas = v0->canvas;
		m_view_area->setCentralWidget(v0->frame);
	}
	setCentralWidget(m_inner);

	BuildDocks();
	BuildMenusAndToolbar();
	// view 0's tools, overlays and connections (brush first: the first
	// active tool receives the mouse, wx modes 7/8)
	WireView(*m_views[0]);
	SetActiveView(0);
	// wx VRenderFrame creates the BrushToolDlg at start-up (hidden): its
	// defaults (default_brush_settings.dft: component analysis / noise
	// reduction values) feed the other analysis dialogs, and it saves them
	// on exit even when it was never shown
	m_brush_dlg = new BrushToolDialog(this, m_brush_tool, this);
	// wx VRenderFrame: m_trace_dlg = new TraceDlg (a hidden floating pane)
	m_trace_dlg = new TraceDialog(this);
	MakeFloatingTool(m_trace_dlg);
	{
		TraceDialog::Hooks th;
		th.ruler_tool = [this](RenderCanvas* c) { const int i = ViewIndex(c); return GetViewRulerTool(i >= 0 ? i : m_active_view); };
		th.measure_settings = [this]() { if (m_measure_dlg) m_measure_dlg->GetSettings(); };
		th.brush_clear = [this]() { if (m_brush_tool) m_brush_tool->BrushClear(); };
		th.frame_step = [this](int dir) { if (dir > 0) m_movie->UpFrame(); else m_movie->DownFrame(); };
		m_trace_dlg->SetHooks(th);
		m_trace_dlg->GetSettings(m_canvas);
		m_trace_dlg->hide();
	}
	m_prop->SetOverlays(m_overlays);
	{
		// Record/Export "Save..." (wx VMovieView::OnRun / SaveMovie)
		MoviePanel::Hooks mhooks;
		mhooks.get_proj_save = [this]() { return m_settings->GetProjSave(); };
		mhooks.get_compress = [this]() { return m_settings->GetSaveCompress(); };
		mhooks.set_compress = [this](bool b) { m_settings->SetSaveCompress(b); };
		mhooks.get_embed = [this]() { return m_vrp_embed; };
		mhooks.set_embed = [this](bool b) { m_vrp_embed = b; };
		mhooks.save_project = [this](const QString& f) { return SaveProject(f); };
		// wx VMovieView::OnPrev: SettingDlg GetRunScript() && wxFileExists(GetScriptFile())
		mhooks.script_enabled = [this]() {
			return m_settings->GetRunScript() && !m_settings->GetScriptFile().isEmpty() &&
				QFileInfo::exists(m_settings->GetScriptFile());
		};
		mhooks.run_4d_script = [this]() { Run4DScript(); };
		// wx VMovieView: vr_frame->GetView(m_views_cmb->GetValue())
		mhooks.find_view = [this](const QString& name, RenderCanvas*& c, ViewOverlays*& ov) {
			for (const auto& v : m_views)
				if (v->name == name) { c = v->canvas; ov = v->overlays; return true; }
			return false;
		};
		m_movie->SetHooks(mhooks);
		m_movie->SetOverlays(m_overlays);
	}
	m_clip->SetRenderCanvas(m_canvas);
	// wx VPropView: m_frame->GetDataManager() (volume defaults), m_vrv
	m_prop->SetDataManager(m_data_mgr);
	m_prop->SetRenderCanvas(m_canvas);
	connect(m_prop, &VolumePropPanel::colormapModeChanged, m_adjust, &AdjustPanel::UpdateSync);
	// the tree's brush buttons act on the active view's brush (wx: the
	// tree panel's m_vrv)
	connect(m_tree, &TreePanel::brushModeRequested, this, [this](int m) { m_brush_tool->SelectBrushMode(m); });
	connect(m_tree, &TreePanel::brushClearRequested, this, [this]() { m_brush_tool->BrushClear(); });
	connect(m_tree, &TreePanel::brushEraseRequested, this, [this]() { m_brush_tool->BrushErase(); });
	connect(m_tree, &TreePanel::brushExtractRequested, this, [this]() { m_brush_tool->BrushCreate(); });
	connect(m_prop, &VolumePropPanel::roiNameEntered, this, &MainWindow::SaveROIName);
	connect(m_prop, &VolumePropPanel::treeRefreshRequested, this, [this]() {
		VolumeData* vd = m_prop->GetVolume();
		RefreshTree();
		if (vd) m_tree->SelectVolume(vd);
	});
	ApplyRulerSettings();
	connect(m_settings, &SettingsDialog::settingsChanged, this, &MainWindow::ApplyRulerSettings);
	// wx VRenderFrame ctor: the Settings test modes (speed / wireframe on the
	// first view, m_vol_test_wiref in SettingsDialog::SetDataManager; the
	// Tester dialog -- wx TesterDlg, whose handlers are all commented out:
	// a "Click Me" button that does nothing -- shown for the parameter test)
	if (m_settings->GetTestMode(1))
		m_canvas->SetTestSpeed(true);
	if (m_settings->GetTestMode(3))
		m_canvas->SetTestWiref(true);
	{
		m_tester = new QDialog(this, Qt::Dialog | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
		m_tester->setObjectName("TesterDlg");
		m_tester->setWindowTitle(tr("Tester"));
		m_tester->resize(600, 600);
		auto* sizer_v = new QVBoxLayout(m_tester);
		auto* b1 = new QPushButton(tr("Click Me"), m_tester);
		b1->setObjectName("B1Btn");
		sizer_v->addWidget(b1, 0, Qt::AlignLeft | Qt::AlignTop);
		sizer_v->addStretch(1);
		connect(b1, &QPushButton::clicked, this, []() {});   // wx TesterDlg::OnB1: commented out
		if (m_settings->GetTestMode(2))
			m_tester->show();
		else
			m_tester->hide();
	}
	connect(m_settings, &SettingsDialog::fontChanged, this, [this](const QString& path, int size) {
		for (const auto& v : m_views) v->ruler->SetFont(path, size); });
	connect(m_settings, &SettingsDialog::textSizeChanged, this, [this](int size) {
		for (const auto& v : m_views) v->ruler->SetTextSize(size); });
	statusBar()->showMessage(tr("VVDViewer started normally."));

	// view-side render settings (wx VRenderFrame ctor / SettingDlg handlers:
	// SetPeelingLayers, SetBlendSlices, SetAdaptive, SetAdaptiveRes,
	// SetUseFogMesh): every view (ApplyViewSettings)
	connect(m_settings, &SettingsDialog::settingsChanged, this, [this]() {
		for (const auto& v : m_views) ApplyViewSettings(v->canvas); });
	// SettingDlg::OnRotLink -> view(0)->OnRotLink(checked)
	connect(m_settings, &SettingsDialog::rotationLinkChanged, this, [this](bool on) {
		m_views[0]->canvas->LinkAllRotationsFromSettings(on); });

	// wx VRenderFrame ctor: m_time_id = m_setting_dlg->GetTimeId()
	m_open_opts.time_id = m_settings->GetTimeId();
	// testing: VVD_QT_TIME_SEQ=1 selects the Open Volume dialog option "Read a
	// sequence as time series" (VRenderFrame::m_timeSequence) for this session
	if (qEnvironmentVariableIntValue("VVD_QT_TIME_SEQ") != 0)
	{
		m_open_opts.time_seq = true;
		m_open_opts.slice_seq = false;
	}
	// testing: VVD_QT_VOL_METHOD=1|2|3 (Layered / Depth / Composite),
	// VVD_QT_FOG=<intensity> (depth attenuation on with that intensity)
	{
		bool ok = false;
		int vm = qEnvironmentVariableIntValue("VVD_QT_VOL_METHOD", &ok);
		if (ok) m_canvas->SetVolMethod(vm);
		const QString fog = qEnvironmentVariable("VVD_QT_FOG");
		if (!fog.isEmpty()) { m_canvas->SetFog(true); m_canvas->SetFogIntensity(fog.toDouble()); }
		// VVD_QT_ROT="rx,ry,rz": view rotation applied 2.5 s after start-up
		const QStringList rot = qEnvironmentVariable("VVD_QT_ROT").split(',');
		if (rot.size() == 3)
			QTimer::singleShot(2500, this, [this, rot]() {
				m_canvas->SetRotations(rot[0].toDouble(), rot[1].toDouble(), rot[2].toDouble()); });
		// VVD_QT_SPIN="deg,steps,delay_ms": after delay_ms, rotate the view
		// about y by deg every 16 ms for steps steps, then print the frame rate
		// (frames presented / wall time) on stderr
		const QStringList spin = qEnvironmentVariable("VVD_QT_SPIN").split(',');
		if (spin.size() == 3)
			QTimer::singleShot(spin[2].toInt(), this, [this, spin]() {
				auto* t = new QTimer(this);
				auto step = std::make_shared<int>(0);
				auto t0 = std::make_shared<qint64>(QDateTime::currentMSecsSinceEpoch());
				auto f0 = std::make_shared<long long>(m_canvas->PresentedFrames());
				const double deg = spin[0].toDouble();
				const int steps = spin[1].toInt();
				connect(t, &QTimer::timeout, this, [this, t, step, t0, f0, deg, steps]() {
					double rx, ry, rz;
					m_canvas->GetRotations(rx, ry, rz);
					m_canvas->SetRotations(rx, ry + deg, rz);
					if (++(*step) >= steps)
					{
						t->stop();
						const double sec = (QDateTime::currentMSecsSinceEpoch() - *t0) / 1000.0;
						const long long fr = m_canvas->PresentedFrames() - *f0;
						fprintf(stderr, "[spin] %d steps, %lld frames in %.2f s: %.1f fps\n", steps, fr, sec, fr / sec);
						t->deleteLater();
					}
				});
				t->start(16);
			});
		// VVD_QT_VOLPROPS="key=value,...": volume properties applied to every
		// volume 2.4 s after start-up (render-state coverage tests). Keys:
		// invert, mip (render mode 1), shading, solid (alpha off), colormap
		// (colormap mode), gamma, offset, lo, hi, boundary, alpha, lum
		const QStringList vprops = qEnvironmentVariable("VVD_QT_VOLPROPS").split(',', Qt::SkipEmptyParts);
		if (!vprops.isEmpty())
			QTimer::singleShot(2400, this, [this, vprops]() {
				for (VolumeData* vd : m_canvas->Volumes())
				{
					for (const QString& kv : vprops)
					{
						const QStringList p = kv.split('=');
						if (p.size() != 2) continue;
						const QString k = p[0].trimmed();
						const double v = p[1].toDouble();
						if (k == "invert") vd->SetInvert(v != 0.0);
						else if (k == "mip") vd->SetMode(v != 0.0 ? 1 : 0);
						else if (k == "shading") vd->SetShading(v != 0.0);
						else if (k == "solid") vd->SetEnableAlpha(v == 0.0);
						else if (k == "colormap") vd->SetColormapMode((int)v);
						else if (k == "gamma") vd->Set3DGamma(v);
						else if (k == "offset") vd->SetOffset(v);
						else if (k == "lo") vd->SetLeftThresh(v);
						else if (k == "hi") vd->SetRightThresh(v);
						else if (k == "boundary") vd->SetBoundary(v);
						else if (k == "alpha") vd->SetAlpha(v);
						else if (k == "lum") vd->SetLuminance(v);
					}
				}
				m_canvas->RefreshView(); });
		// VVD_QT_MESH_ALPHA=<a>: mesh transparency (exercises depth peeling layers)
		bool aok = false;
		const double malpha = qEnvironmentVariable("VVD_QT_MESH_ALPHA").toDouble(&aok);
		if (aok)
			QTimer::singleShot(2500, this, [this, malpha]() {
				for (MeshData* md : m_canvas->Meshes()) { double a = malpha; md->SetFloat(a, MESH_FLOAT_ALPHA); }
				m_canvas->RefreshView(); });
	}

	// VVD_QT_OVERLAYS=1: camera centre, info text and scale bar on (testing)
	if (qEnvironmentVariableIsSet("VVD_QT_OVERLAYS"))
	{
		m_overlays->SetDrawCamCtr(true);
		m_overlays->SetDrawInfo(true);
		m_overlays->EnableScaleBar();
		m_overlays->EnableSBText();
		m_overlays->SetDrawLegend(true);
		m_view->SyncOverlayChecks();
		QTimer::singleShot(2000, this, [this]() {
			for (VolumeData* vd : m_canvas->Volumes()) vd->SetLegend(true);
			for (MeshData* md : m_canvas->Meshes()) md->SetLegend(true);
			m_canvas->RefreshView(); });
	}

	// VVD_QT_GRAB_LEGEND=<dir>: the Legend button's drop-down menu (testing).
	// After VVD_QT_GRAB_LEGEND_DELAY ms (default 5000) the menu is opened,
	// rows 0 and 1 are clicked, Hide All / Show All are triggered with mouse
	// releases on the menu, the list is scrolled to its end; pictures
	// legend_menu_<step>.png, "[legend]" lines with PASS/FAIL on stderr, the
	// view as legend_view.png, then quit.
	{
		const QString ldir = qEnvironmentVariable("VVD_QT_GRAB_LEGEND");
		if (!ldir.isEmpty())
		{
			bool dok = false;
			const int ldelay = qEnvironmentVariableIntValue("VVD_QT_GRAB_LEGEND_DELAY", &dok);
			QTimer::singleShot(dok ? ldelay : 5000, this, [this, ldir]() {
				LegendMenu* menu = m_view ? m_view->GetLegendMenu() : nullptr;
				QPushButton* btn = m_view ? m_view->LegendButton() : nullptr;
				if (!menu || !btn) { fprintf(stderr, "[legend] FAIL: no legend menu\n"); QCoreApplication::quit(); return; }
				auto legends = [this]() {
					QString s;
					for (VolumeData* vd : m_canvas->PopVolumeList()) s += vd->GetLegend() ? '1' : '0';
					s += '|';
					for (MeshData* md : m_canvas->GetPopMeshList()) s += md->GetLegend() ? '1' : '0';
					return s;
				};
				auto grab = [menu, ldir](const char* step) {
					const QString f = ldir + QString("/legend_menu_%1.png").arg(step);
					menu->grab().save(f);
					fprintf(stderr, "[legend] %s: menu visible %d, %d rows, list %dx%d, menu %dx%d -> %s\n", step,
						(int)menu->isVisible(), menu->List()->count(), menu->List()->width(), menu->List()->height(),
						menu->width(), menu->height(), f.toLocal8Bit().constData());
				};
				auto check = [menu, legends](const char* what, bool ok) {
					const QString l = legends();
					fprintf(stderr, "[legend] %s %s (menu open %d, legends %s)\n", ok ? "PASS" : "FAIL", what,
						(int)menu->isVisible(), (l.size() > 80 ? l.left(40) + "..." + l.right(40) : l).toLocal8Bit().constData());
				};
				auto click_row = [menu](int row) {
					QListWidget* l = menu->List();
					if (row >= l->count()) return;
					const QPoint p = l->visualItemRect(l->item(row)).center();
					QMouseEvent press(QEvent::MouseButtonPress, p, l->viewport()->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
					QMouseEvent rel(QEvent::MouseButtonRelease, p, l->viewport()->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
					QApplication::sendEvent(l->viewport(), &press);
					QApplication::sendEvent(l->viewport(), &rel);
				};
				auto click_action = [menu](QAction* a) {
					const QPoint p = menu->actionGeometry(a).center();
					QMouseEvent press(QEvent::MouseButtonPress, p, menu->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
					QMouseEvent rel(QEvent::MouseButtonRelease, p, menu->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
					QApplication::sendEvent(menu, &press);
					QApplication::sendEvent(menu, &rel);
				};
				const QString before = legends();
				QTimer::singleShot(800, this, [=]() { grab("open"); check("menu opened", menu->isVisible() && menu->List()->count() > 0); });
				QTimer::singleShot(1100, this, [=]() {
					click_row(0);
					click_row(1);
					QString exp = before;
					for (int i : { 0, 1 }) if (i < exp.size() && exp[i] != '|') exp[i] = exp[i] == '1' ? '0' : '1';
					check("rows 0 and 1 toggled, menu still open", menu->isVisible() && legends() == exp);
				});
				QTimer::singleShot(1500, this, [=]() { grab("toggled"); });
				QTimer::singleShot(1800, this, [=]() {
					click_action(menu->HideAllAction());
					check("Hide All, menu still open", menu->isVisible() && !legends().contains('1'));
				});
				QTimer::singleShot(2200, this, [=]() { grab("hideall"); });
				QTimer::singleShot(2500, this, [=]() {
					click_action(menu->ShowAllAction());
					check("Show All, menu still open", menu->isVisible() && !legends().contains('0'));
					click_row(1);
				});
				QTimer::singleShot(2900, this, [=]() {
					QListWidget* l = menu->List();
					l->scrollToItem(l->item(l->count() - 1));
					grab("scrolled");
					fprintf(stderr, "[legend] scroll bar %d/%d\n", l->verticalScrollBar()->value(), l->verticalScrollBar()->maximum());
				});
				QTimer::singleShot(3300, this, [=]() {
					menu->List()->scrollToTop();
					grab("final");
					menu->hide();
					check("menu closed by hide()", !menu->isVisible());
					connect(m_canvas, &RenderCanvas::frameCaptured, this, [](const QString&, bool) { QCoreApplication::quit(); });
					m_canvas->CaptureToFile(ldir + "/legend_view.png");
				});
				btn->showMenu();   // modal (QMenu::exec) until hide()
			});
		}
	}

	QSettings settings("Janelia", "VVDViewerQt");
	{
		QByteArray geom = settings.value("geometry").toByteArray();
		QByteArray st = settings.value("windowState2").toByteArray();
		QByteArray ist = settings.value("innerState2").toByteArray();
		// Testing aid: VVD_QT_STATE_HEX=<file> (lines "key hexbytes"; an empty
		// file = no saved state) replaces the saved window state for the run
		const QString sh = qEnvironmentVariable("VVD_QT_STATE_HEX");
		if (!sh.isEmpty())
		{
			geom.clear(); st.clear(); ist.clear();
			QFile f(sh);
			if (f.open(QIODevice::ReadOnly | QIODevice::Text))
				for (const QByteArray& line : f.readAll().split('\n'))
				{
					const QList<QByteArray> kv = line.trimmed().split(' ');
					if (kv.size() != 2) continue;
					const QByteArray v = QByteArray::fromHex(kv[1]);
					if (kv[0] == "geometry") geom = v;
					else if (kv[0] == "windowState2") st = v;
					else if (kv[0] == "innerState2") ist = v;
				}
		}
		restoreGeometry(geom);
		const bool restored = restoreState(st);
		m_inner->restoreState(ist);
		// no saved state: the left column's start-up width once shown (the
		// resizeDocks of BuildDocks, before the window has a size, is not
		// kept). Record/Export's width, which used to be the column's
		// minimum, at least 320
		if (!restored)
			QTimer::singleShot(0, this, [this]() {
				if (!m_tree_dock || !m_movie) return;
				resizeDocks({ m_tree_dock }, { qMax(320, m_movie->sizeHint().width()) }, Qt::Horizontal);
			});
	}
	// a restored geometry larger than (or off) the screen, floating docks
	// off-screen: back into the screen's available area (again once shown,
	// when the frame size is known; MainWindowLayout.cpp)
	FitToScreen();
	QTimer::singleShot(0, this, [this]() { FitToScreen(); });
	// Properties and Record/Export fitted to their content once the window size has settled
	// (MainWindowLayout.cpp: a restored state is re-applied by Qt while a
	// maximized window is still growing), and again on another screen
	m_prop_dock->installEventFilter(this);
	m_movie_dock->installEventFilter(this);
	m_movie->installEventFilter(this);   // its size need changes with the tab
	ScheduleDockFit();
	QTimer::singleShot(0, this, [this]() {
		if (QWindow* wh = windowHandle())
			connect(wh, &QWindow::screenChanged, this, [this]() { ScheduleDockFit(); });
	});
	m_last_dir = settings.value("lastDir").toString();

	// wx VRenderFrame ctor: the plugin manager, the Plugins menu, and at its
	// end m_plugin_manager->InitPlugins() (MainWindowPlugins.cpp)
	if (!qEnvironmentVariableIsSet("VVD_QT_NO_PLUGINS"))   // debugging aid
		InitPluginSystem();
}

MainWindow::~MainWindow()
{
	// wx ~VRenderFrame: FinalizePligins first; the plugin panes (whose
	// destructors save the plugins' default settings, as wx) before the
	// plugins they point at
	FinalizePluginSystem();
	for (auto& kv : m_plugin_docks)
		delete kv.second;
	m_plugin_docks.clear();
	delete m_plugin_manager;
	m_plugin_manager = nullptr;
	delete m_trace_dlg;
	m_trace_dlg = nullptr;
	delete m_brush_dlg;
	m_brush_dlg = nullptr;
	// further views first (their surfaces go back to view 0's context)
	while (m_views.size() > 1)
	{
		ViewSlot& v = *m_views.back();
		v.canvas->RemoveTool(v.brush);
		v.brush->ReleaseResources();
		delete v.brush;
		delete v.overlays;
		delete v.ruler;
		v.canvas->ClearMeshes();
		v.canvas->ClearVolumes();
		delete v.frame;
		if (v.dock) delete v.dock;
		m_views.pop_back();
	}
	m_canvas = m_views[0]->canvas;
	m_brush_tool = m_views[0]->brush;
	m_overlays = m_views[0]->overlays;
	m_ruler_tool = m_views[0]->ruler;
	if (m_brush_tool) { m_canvas->RemoveTool(m_brush_tool); m_brush_tool->ReleaseResources(); delete m_brush_tool; m_brush_tool = nullptr; }
	delete m_overlays;     // uses the ruler tool's text renderer
	m_overlays = nullptr;
	delete m_ruler_tool;   // owns rulers / text renderer (GPU resources)
	m_ruler_tool = nullptr;
	m_canvas->ClearVolumes();
	delete m_data_mgr;
	// wx ~VRenderFrame: curl handles, then the cached remote-brick files
	if (_g_curl) { curl_easy_cleanup(_g_curl); _g_curl = nullptr; }
	if (_g_curlm) { curl_multi_cleanup(_g_curlm); _g_curlm = nullptr; }
	curl_global_cleanup();
	FLIVR::TextureBrick::delete_all_cache_files();
}

// wx VRenderFrame::StartupLoad
void MainWindow::StartupLoad(const QStringList& files, const QStringList& descs)
{
	if (files.isEmpty()) return;
	auto suffix_of = [](const QString& f) { const int d = f.lastIndexOf('.'); return (d >= 0 ? f.mid(d) : f).toLower(); };
	if (suffix_of(files.first()) == ".vrp")
	{
		OpenProject(files.first());
		return;
	}
	QStringList vol_files, msh_files, vol_descs, msh_descs;
	for (int i = 0; i < files.size(); i++)
	{
		const QString s = suffix_of(files[i]);
		if (s == ".nrrd" || s == ".tif" || s == ".tiff" || s == ".oib" || s == ".oif" || s == ".lsm" ||
			s == ".czi" || s == ".xml" || s == ".vvd" || s == ".n5" || s == ".zarr" ||
#ifndef _DARWIN
			s == ".nd2" ||
#endif
			s == ".json" || s == ".h5j" || s == ".v3dpbd" || s == ".zip" || s == ".idi" ||
			// Zarr / N5 entry points LoadVolumes accepts (Open Volume, drag & drop)
			s == ".zgroup" || s == ".zattrs" || s == ".zfs_ch" || s == ".n5fs_ch")
		{
			vol_files << files[i];
			if (i < descs.size()) vol_descs << descs[i];
		}
		else if (s == ".obj" || s == ".swc" || s == ".ply" || s == ".csv" || s == ".nml")
		{
			msh_files << files[i];
			if (i < descs.size()) msh_descs << descs[i];
		}
	}
	if (!vol_files.isEmpty()) LoadVolumes(vol_files, vol_descs);
	if (!msh_files.isEmpty()) LoadMeshFiles(msh_files, QString(), msh_descs);
}

// wx VRenderFrame::SetTasks
void MainWindow::SetTasks(const QString& comma_separated_tasks)
{
	m_tasks = comma_separated_tasks.split(',', Qt::SkipEmptyParts);
	if (!m_tasks.isEmpty())
		m_run_tasks = true;
	if (!m_task_timer)
	{
		m_task_timer = new QTimer(this);
		connect(m_task_timer, &QTimer::timeout, this, &MainWindow::OnTaskTimer);
		m_task_timer->start(100);   // wx m_timer->Start(100)
	}
}

// wx VRenderFrame::OnTimer
void MainWindow::OnTaskTimer()
{
	if (!m_run_tasks)
		return;
	if (m_tasks.isEmpty())
	{
		m_run_tasks = false;
		close();   // wx Close(true)
		return;
	}
	const QString task = m_tasks.first();
	fprintf(stderr, "%s\n", task.toLocal8Bit().constData());   // wx cout << m_tasks[0]
	if (task == "hideui")
	{
		if (m_ui_state) ToggleAllTools();
		m_tasks.removeFirst();
	}
	else if (task == "showui")
	{
		if (!m_ui_state) ToggleAllTools();
		m_tasks.removeFirst();
	}
	else if (task == "basic" || task == "advanced")
	{
		const int mode = task == "basic" ? 0 : 1;   // VMovieView::MODE_BASIC / MODE_ADVANCED
		if (!m_movie)
			m_tasks.removeFirst();
		else if (!m_waiting_for_task)
		{
			m_movie->SetMode(mode);
			m_waiting_for_task = true;
		}
		else if (m_movie->GetMode() == mode)
		{
			m_tasks.removeFirst();
			m_waiting_for_task = false;
		}
	}
	else if (task == "record")
	{
		if (m_tasks.size() >= 2)
		{
			if (!m_movie)
			{
				m_tasks.removeFirst();
				m_tasks.removeFirst();
			}
			else if (!m_waiting_for_task)
			{
				// wxFileName::ReplaceHomeDir + MakeAbsolute
				QString f = m_tasks[1];
				if (f == "~" || f.startsWith("~/")) f = QDir::homePath() + f.mid(1);
				m_movie->SaveMovie(QFileInfo(f).absoluteFilePath());
				m_waiting_for_task = true;
			}
			else if (!m_movie->IsRecording())
			{
				m_tasks.removeFirst();
				m_tasks.removeFirst();
				m_waiting_for_task = false;
			}
		}
		else
			m_tasks.removeFirst();
	}
	else
		m_tasks.removeFirst();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
	// scripted test runs (VVD_QT_CAPTURE / _GRAB / _MOVIE / _FILEDLG_RUN; Qt 6
	// closes the windows on QCoreApplication::quit) leave the user's settings
	// file and window state alone
	const bool test_run = VVDQtScriptedRun();
	if (!test_run)
	{
		QSettings settings("Janelia", "VVDViewerQt");
		settings.setValue("geometry", saveGeometry());
		settings.setValue("windowState2", saveState());
		settings.setValue("innerState2", m_inner->saveState());
		settings.setValue("lastDir", m_last_dir);
		m_settings->SaveSettings();
	}
	QMainWindow::closeEvent(e);
	if (e->isAccepted())
	{
		for (const auto& v : m_views)
		{
			v->brush->ReleaseResources();
			v->canvas->Shutdown();
		}
	}
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

namespace
{
	// scroll: the directions in which the dock may be made smaller than its
	// panel (scroll bars then; PaneScrollArea.h), down to min
	QDockWidget* MakeDock(QMainWindow* owner, const QString& title, const QString& name, QWidget* w,
		Qt::Orientations scroll = {}, QSize min = QSize())
	{
		auto* dock = new QDockWidget(title, owner);
		dock->setObjectName(name);
		dock->setWidget(scroll ? new PaneScrollArea(w, scroll, min) : w);
		dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
		return dock;
	}

	QIcon IconFromPng(const unsigned char* data, int len)
	{
		QPixmap pm;
		pm.loadFromData(data, (uint)len, "PNG");
		return QIcon(pm);
	}
#define VVD_ICON(name) IconFromPng(icons::name##_png, (int)sizeof(icons::name##_png))
}

void MainWindow::BuildDocks()
{
	// wxAUI caption look: small bold title on a light bar
	setStyleSheet(
		"QDockWidget { font-size: 11px; }"
		"QDockWidget::title { background: #e4e4e4; padding: 2px 4px; border-bottom: 1px solid #c8c8c8; }");
	m_inner->setStyleSheet(styleSheet());

	// left column, full height (wx layer 3): Workspace over Record/Export
	setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
	setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
	setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
	setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

	m_tree = new TreePanel(this);
	m_tree->SetDataManager(m_data_mgr);
	m_tree->setMinimumWidth(250);
	m_tree_dock = MakeDock(this, tr("Workspace"), "WorkspaceDock", m_tree);
	addDockWidget(Qt::LeftDockWidgetArea, m_tree_dock);

	m_interpolator = std::make_unique<Interpolator>();
	m_movie = new MoviePanel(this);
	m_movie->SetRenderCanvas(m_canvas);
	m_movie->SetInterpolator(m_interpolator.get());
	m_movie->AddView(m_views[0]->name);
	m_movie->SetView(0);
	m_movie_dock = MakeDock(this, tr("Record/Export"), "MovieDock", m_movie, Qt::Horizontal, QSize(150, 0));
	addDockWidget(Qt::LeftDockWidgetArea, m_movie_dock);
	splitDockWidget(m_tree_dock, m_movie_dock, Qt::Vertical);
	resizeDocks({ m_tree_dock, m_movie_dock }, { 700, 330 }, Qt::Vertical);
	resizeDocks({ m_tree_dock }, { 320 }, Qt::Horizontal);

	// bottom (wx layer 2): Properties, spanning under adjust/view/clip
	m_prop = new VolumePropPanel(this);
	m_mesh_prop = new MeshPropPanel(this);
	m_prop_stack = new QStackedWidget(this);
	m_prop_stack->addWidget(m_prop);
	m_prop_stack->addWidget(m_mesh_prop);
	m_mesh_manip = new MeshManipPanel(this);
	m_prop_stack->addWidget(m_mesh_manip);
	m_ann_prop = new AnnotationPropPanel(this);
	m_prop_stack->addWidget(m_ann_prop);
	// wx: nothing shown for views, groups, ... (all property panes hidden)
	m_prop_empty = new QWidget(this);
	m_prop_stack->addWidget(m_prop_empty);
	// the volume page's long rows (Max Value ... Depth) are ~1700 px wide:
	// scroll horizontally below that instead of widening the window
	m_prop_dock = MakeDock(this, tr("Properties"), "PropertiesDock", m_prop_stack, Qt::Horizontal, QSize(200, 0));
	addDockWidget(Qt::BottomDockWidgetArea, m_prop_dock);

	// inner window (wx layer 1 + centre): the render views' area
	m_view_dock = nullptr;
	m_inner->setCentralWidget(m_view_area);

	m_adjust = new AdjustPanel(m_inner);
	m_adjust->SetRenderCanvas(m_canvas);
	m_adjust_dock = MakeDock(m_inner, tr("Output Adjustments"), "AdjustDock", m_adjust, Qt::Vertical, QSize(0, 120));
	m_inner->addDockWidget(Qt::LeftDockWidgetArea, m_adjust_dock);
	m_inner->resizeDocks({ m_adjust_dock }, { 125 }, Qt::Horizontal);

	m_clip = new ClipPanel(m_inner);
	m_clip_dock = MakeDock(m_inner, tr("Clipping Planes"), "ClipDock", m_clip, Qt::Vertical, QSize(0, 120));
	m_inner->addDockWidget(Qt::RightDockWidgetArea, m_clip_dock);
	m_inner->resizeDocks({ m_clip_dock }, { 160 }, Qt::Horizontal);

	// workspace tree (wx DataTreeCtrl -> VRenderFrame::OnSelection etc.)
	m_tree->SetCanvases({ m_canvas });
	m_tree->SetCompressionGetter([this]() { return m_settings->GetSaveCompress(); });
	// wx MPropView::GetSyncGroup / GetSyncName for SWC segment toggles
	m_tree->SetMeshSyncGetter([this](bool& group, bool& name) {
		group = m_mesh_prop && m_mesh_prop->GetSyncGroup();
		name = m_mesh_prop && m_mesh_prop->GetSyncName();
	});
	// wx GetPropView()->UpdateUIsROI() / GetMeasureDlg()->UpdateList()
	connect(m_tree, &TreePanel::roiDisplayChanged, this, [this]() { if (m_prop) m_prop->GetSettings(); });
	connect(m_tree, &TreePanel::annotationsChanged, this, [this]() { if (m_measure_dlg) m_measure_dlg->UpdateList(); });
	connect(m_tree, &TreePanel::selectionChanged, this, &MainWindow::OnTreeSelection);
	connect(m_tree, &TreePanel::volumeRemoveRequested, this, &MainWindow::OnVolumeRemove);
	connect(m_tree, &TreePanel::meshRemoveRequested, this, &MainWindow::OnMeshRemove);
	connect(m_tree, &TreePanel::groupRemoved, this, [this](const std::vector<VolumeData*>& vols, const std::vector<MeshData*>& meshes) {
		for (VolumeData* vd : vols) ForgetVolume(vd);
		for (MeshData* md : meshes) ForgetMesh(md);
		m_canvas->RefreshView();
	});
	connect(m_tree, &TreePanel::annotationsRemoveRequested, this, [this](Annotations* ann) {
		if (!ann) return;
		if (m_ann_prop->GetAnnotations() == ann)
		{
			m_ann_prop->SetAnnotations(nullptr);
			m_prop_stack->setCurrentWidget(m_prop_empty);
			m_prop_dock->setWindowTitle(tr("Properties"));
		}
		// every view, not only the active one (m_canvas): the tree item may
		// belong to another view, which kept a dangling pointer after the
		// delete below (wx removes it from the item's own view)
		for (const auto& v : m_views)
		{
			v->canvas->RemoveAnnotations(ann);
			// the analysis that made it keeps a pointer for Show Annotations
			if (v->sel) v->sel->ForgetAnnotations(ann);
		}
		m_selector.ForgetAnnotations(ann);
		m_data_mgr->RemoveAnnotations(ann->GetName());
	});
	// wx RefreshVRenderViews: every view
	connect(m_tree, &TreePanel::refreshRequested, this, [this]() {
		for (const auto& v : m_views) { v->canvas->RefreshView(); v->view->ReloadLegend(); } });
	connect(m_tree, &TreePanel::toolRequested, this, &MainWindow::OnTreeTool);
	// wx DataTreeCtrl::OnCloseView -> DeleteVRenderView
	connect(m_tree, &TreePanel::closeViewRequested, this, [this](RenderCanvas* c) {
		const int i = ViewIndex(c);
		if (i > 0) DeleteView(i); });
	connect(m_tree, &TreePanel::manipulateRequested, this, &MainWindow::ShowMeshManip);
	connect(m_mesh_prop, &MeshPropPanel::meshChanged, this, [this]() { m_canvas->RefreshView(); });
	// wx MManipulator::UpdateData -> RefreshVRenderViews
	connect(m_mesh_manip, &MeshManipPanel::meshChanged, this, [this]() { m_canvas->RefreshView(); });
	// wx APropView: RefreshVRenderViews(tree)
	connect(m_ann_prop, &AnnotationPropPanel::annotationsChanged, this, [this](bool tree) {
		if (tree) m_tree->UpdateTreeColors();
		m_canvas->RefreshView();
	});
	// wx APropView::OnThresholdText: MeasureDlg::UpdateList(); GetCount(m_ann)
	m_ann_prop->SetCountProvider([this](Annotations* ann) -> long {
		MeasureDialog* dlg = EnsureMeasureDialog();
		dlg->UpdateList();
		return dlg->GetCount(ann);
	});
	connect(m_mesh_prop, &MeshPropPanel::meshBoundsChanged, this, [this]() { m_canvas->RecomputeBounds(); });
	connect(m_prop, &VolumePropPanel::volumeChanged, this, [this]() { m_canvas->RefreshView(); });
	connect(m_prop, &VolumePropPanel::colorChanged, this, [this]() {
		m_tree->UpdateTreeColors();
		m_adjust->UpdateSync();
	});
	connect(m_clip, &ClipPanel::clipChanged, this, [this]() { m_canvas->RefreshView(); });
	connect(m_adjust, &AdjustPanel::adjustmentChanged, this, [this]() { m_canvas->RefreshView(); });
}

// wx VRenderFrame constructor: the main toolbar (m_main_tb) and the menu bar
// (m_top_menu: File, Tools, Windows -- the wx frame has no other menus)
void MainWindow::BuildMenusAndToolbar()
{
	BuildUIMenu();   // wx m_tb_menu_ui (MainWindowUI.cpp)

	// ---- File (wx m_top_file)
	QMenu* file = menuBar()->addMenu(tr("&File"));
	QAction* open_vol = file->addAction(VVD_ICON(icon_open_volume_mini), tr("Open &Volume"), this, &MainWindow::OnOpenVolume);
	QAction* open_mesh_m = file->addAction(VVD_ICON(icon_open_mesh_mini), tr("Open &Mesh"), this, &MainWindow::OnOpenMesh);
	QAction* open_prj = file->addAction(VVD_ICON(icon_open_project_mini), tr("Open &Project"), this, &MainWindow::OnOpenProject);
	QAction* save_prj = file->addAction(VVD_ICON(icon_save_project_mini), tr("&Save Project"), this, &MainWindow::OnSaveProject);
	file->addAction(tr("Open &URL"), this, &MainWindow::OnOpenURL);
	file->addSeparator();
	QAction* quit = file->addAction(tr("&Quit"), this, &QWidget::close, QKeySequence::Quit);
	quit->setMenuRole(QAction::QuitRole);   // wxID_EXIT: the application menu on macOS
	QAction* about = file->addAction(tr("&About VVDViewer"), this, &MainWindow::OnAbout);
	about->setMenuRole(QAction::AboutRole); // wx: the application menu's About on macOS
	(void)open_mesh_m;

	// ---- Tools (wx m_top_tools)
	// the analysis entries (BuildAnalysisMenu) for the toolbar's Analyze
	// drop-down (wx m_tb_menu_edit)
	auto* analyze_menu = new QMenu(this);
	BuildAnalysisMenu(analyze_menu);
	QMenu* tools = menuBar()->addMenu(tr("&Tools"));
	{
		const QList<QAction*> acts = analyze_menu->actions();
		// look the drop-down entries up by their text (indexes shift when entries are added)
		auto entry = [&](const QString& src_text, const QString& text, const QIcon& icon = QIcon()) {
			for (QAction* src : acts)
				if (src->text() == src_text)
				{
					QAction* a = tools->addAction(icon, text);
					connect(a, &QAction::triggered, src, &QAction::trigger);
					return;
				}
		};
		entry(tr("Analyze..."), tr("&Analysis Tools..."), VVD_ICON(icon_edit_mini));
		entry(tr("Measurement..."), tr("&Measurement Tools..."));
		entry(tr("Components && Tracking..."), tr("Components && &Tracking..."));   // commented out in wx
		entry(tr("Noise Reduction..."), tr("&Noise Reduction..."));
		entry(tr("Counting and Volume..."), tr("&Counting and Volume..."));
		entry(tr("Colocalization Analysis..."), tr("Colocalization &Analysis..."));
		entry(tr("Convert..."), tr("Con&vert..."));
	}
	tools->addSeparator();
	QAction* settings_m = tools->addAction(VVD_ICON(icon_settings_mini), tr("&Settings..."), this, [this]() {
		m_settings->UpdateUI();
		MakeFloatingTool(m_settings);
		m_settings->show();
		m_settings->raise();
		m_settings->activateWindow();
	});
	settings_m->setMenuRole(QAction::NoRole);   // wx ID_Settings stays in Tools on macOS

	// ---- main toolbar: the wx bitmaps already contain the captions, so icon only
	QToolBar* tb = addToolBar(tr("Toolbar"));
	m_main_tb = tb;
	tb->setObjectName("MainToolBar");
	tb->setMovable(false);
	tb->setFloatable(false);
	tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
	tb->setIconSize(QSize(90, 42));
	tb->setStyleSheet("QToolBar { spacing: 2px; padding: 2px; } QToolButton { padding: 1px; }");
	QAction* open_vol_tb = tb->addAction(VVD_ICON(icon_open_volume), tr("Open Volume"), this, &MainWindow::OnOpenVolume);
	open_vol_tb->setToolTip(tr("Open single or multiple volume data file(s)"));
	QAction* open_prj_tb = tb->addAction(VVD_ICON(icon_open_project), tr("Open Project"), this, &MainWindow::OnOpenProject);
	open_prj_tb->setToolTip(tr("Open a saved project"));
	QAction* save_prj_tb = tb->addAction(VVD_ICON(icon_save_project), tr("Save Project"), this, &MainWindow::OnSaveProject);
	save_prj_tb->setToolTip(tr("Save current work as a project"));
	(void)open_vol; (void)open_prj; (void)save_prj;
	tb->addSeparator();
	QAction* new_vvd = tb->addAction(VVD_ICON(icon_open_new_vvd), tr("Open New VVDViewer"), this, &MainWindow::OnNewViewer);
	new_vvd->setToolTip(tr("Open a new VVDViewer"));
	// wx ID_ShowHideUI: the button toggles all panes (ToggleAllTools), the
	// arrow opens m_tb_menu_ui (wxITEM_DROPDOWN + SetDropdownMenu)
#ifdef __APPLE__
	// wx on macOS (#ifdef _DARWIN): a plain button (wxITEM_NORMAL) whose
	// click pops m_tb_menu_ui up (OnShowHideUI -> PopupMenu)
	QAction* show_hide = tb->addAction(VVD_ICON(icon_show_hide_ui), tr("Show/Hide UI"));
	show_hide->setToolTip(tr("Show or hide control panels"));
	connect(show_hide, &QAction::triggered, this, [this, tb, show_hide]() {
		QWidget* btn = tb->widgetForAction(show_hide);
		QPoint at = btn ? btn->mapToGlobal(QPoint(0, btn->height())) : QCursor::pos();
		m_tb_menu_ui->popup(at);
	});
	if (QWidget* btn = tb->widgetForAction(show_hide))
		btn->setObjectName("ShowHideUIButton");
#else
	QAction* show_hide = tb->addAction(VVD_ICON(icon_show_hide_ui), tr("Show/Hide UI"), this, &MainWindow::OnShowHideUI);
	show_hide->setToolTip(tr("Show or hide all control panels"));
	show_hide->setMenu(m_tb_menu_ui);
	if (auto* btn = qobject_cast<QToolButton*>(tb->widgetForAction(show_hide)))
	{
		btn->setObjectName("ShowHideUIButton");
		btn->setPopupMode(QToolButton::MenuButtonPopup);
	}
#endif
	tb->addSeparator();
	QAction* open_mesh = tb->addAction(VVD_ICON(icon_open_mesh), tr("Open Mesh"), this, &MainWindow::OnOpenMesh);
	open_mesh->setToolTip(tr("Open single or multiple mesh file(s)"));
	QAction* analyze = tb->addAction(VVD_ICON(icon_edit), tr("Analyze"));
	analyze->setMenu(analyze_menu);
	analyze->setToolTip(tr("Tools for analyzing selected channel"));
	// wx: the button opens the brush tool, its arrow the drop-down menu
	connect(analyze, &QAction::triggered, this, [analyze_menu]() {
		if (!analyze_menu->actions().isEmpty()) analyze_menu->actions().first()->trigger(); });
	if (auto* btn = qobject_cast<QToolButton*>(tb->widgetForAction(analyze)))
		btn->setPopupMode(QToolButton::MenuButtonPopup);
	tb->addSeparator();
	// wx ID_Plugins: a plain tool popping up m_tb_menu_plugin (wired in
	// InitPluginSystem, MainWindowPlugins.cpp)
	m_plugins_act = tb->addAction(VVD_ICON(plugins), tr("Plugins"));
	m_plugins_act->setEnabled(false);
	m_plugins_act->setToolTip(tr("Plugins"));
	tb->addSeparator();
	QAction* settings = tb->addAction(VVD_ICON(icon_settings), tr("Settings"), settings_m, &QAction::trigger);
	settings->setToolTip(tr("Settings of VVDViewer"));

	// ---- Windows (wx m_top_window)
	QMenu* win = menuBar()->addMenu(tr("&Windows"));
	m_ui_toolbar_act = win->addAction(tr("Show/Hide &Toolbar"));
	m_ui_toolbar_act->setCheckable(true);
	m_ui_toolbar_act->setChecked(true);
	connect(m_ui_toolbar_act, &QAction::triggered, this, [this]() {   // wx OnShowHideToolbar
		if (m_syncing_ui) return;
		m_main_tb->setVisible(m_main_tb->isHidden());
		SyncUIChecks();
	});
	win->addSeparator();
	win->addAction(VVD_ICON(icon_show_hide_ui_mini), tr("Show/Hide &UI"), this, &MainWindow::OnShowHideUI);
	// wx ID_UIListView: a check item without a handler
	QAction* datasets = win->addAction(tr("&Datasets"));
	datasets->setCheckable(true);
	datasets->setChecked(true);
	struct Item { int pane; const char* text; };
	const Item items[] = {
		{ PaneTree, "&Workspace" }, { PaneMeasure, "&Measurement" }, { PaneMovie, "&Recorder" },
		{ PaneAdjust, "&Output Adjustments" }, { PaneClip, "&Clipping Planes" }, { PaneProp, "&Properties" },
	};
	for (const Item& it : items)
	{
		QAction* a = win->addAction(tr(it.text));
		a->setCheckable(true);
		const int pane = it.pane;
		connect(a, &QAction::triggered, this, [this, pane]() { if (!m_syncing_ui) OnShowHideView(pane); });
		m_win_pane_act[pane] = a;
	}
	win->addSeparator();
	win->addAction(tr("&Open New VVDViewer"), this, &MainWindow::OnNewViewer);
#ifndef Q_OS_MACOS
	// wx ID_FullScreen (not in the macOS build)
	m_fullscreen_act = win->addAction(VVD_ICON(full_screen_menu), tr("&Full Screen"), this, [this]() {
		if (isFullScreen()) showNormal();
		else showFullScreen();
		SyncUIChecks();
	});
#endif
	connect(tb, &QToolBar::visibilityChanged, this, [this]() { QTimer::singleShot(0, this, [this]() { SyncUIChecks(); }); });
	SyncUIChecks();
}

void MainWindow::ShowTraceDlg()
{
	if (!m_trace_dlg) return;
	m_trace_dlg->GetSettings(m_canvas);
	m_trace_dlg->show();
	m_trace_dlg->raise();
}

void MainWindow::OnNewViewer()
{
	QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
}

// ---------------------------------------------------------------------------
// data
// ---------------------------------------------------------------------------

void MainWindow::AddVolumeToView(VolumeData* vd, RenderCanvas* canvas)
{
	if (!vd) return;
	// Default channel colours in opening order: red, green, blue, white, repeat.
	static const FLIVR::Color kOrder[] = {
		FLIVR::Color(1.0, 0.0, 0.0), FLIVR::Color(0.0, 1.0, 0.0),
		FLIVR::Color(0.0, 0.0, 1.0), FLIVR::Color(1.0, 1.0, 1.0) };
	size_t idx = canvas->Volumes().size() % 4;
	vd->SetColor(kOrder[idx], true);
	m_adjust->ApplyDefaults(vd);
	canvas->AddVolume(vd);
	// wx VRenderFrame::AddVolume: a 4D series switches the Record/Export
	// pane to the time sequence
	if (vd->GetReader() && vd->GetReader()->GetTimeNum() > 1)
	{
		canvas->SetTSeqCurNum(vd->GetReader()->GetCurTime());
		m_movie->EnableTime();
		m_movie->DisableRot();
		m_movie->SetCurrentTime(canvas->GetTSeqCurNum());
	}
}

void MainWindow::RefreshTree()
{
	m_tree->UpdateTree();
	for (const auto& v : m_views) v->view->ReloadLegend();
}

void MainWindow::SetStatus(const QString& s)
{
	statusBar()->showMessage(s, 5000);
}

// wx VRenderFrame::LoadVolumes, first loop: a Zarr root (.zarr / .zgroup /
// .zattrs) or an N5 root (.n5 / .json) becomes the paths of its datasets
// (BRKXMLReader::GetZarrChannelPaths / GetN5ChannelPaths) with the ".zfs_ch" /
// ".n5fs_ch" channel suffix; several Zarr datasets and every N5 root go
// through the dataset selection dialog. meta: the metadata file per entry
// (the .zattrs / .json that was opened, else empty).
void MainWindow::ExpandZarrN5(const QStringList& files, QStringList& out, QStringList& meta)
{
	// the readers split on GETSLASH() (a backslash on Windows): native
	// separators in, and either separator accepted here (command line, vvd:)
	const wchar_t* slashes = L"/\\";
	auto relative = [](const std::wstring& p, const std::wstring& root) {
		try { return QString::fromStdWString(std::filesystem::relative(p, root).wstring()); }
		catch (...) { return QString::fromStdWString(p); }
	};
	for (const QString& f : files)
	{
		const int dot = f.lastIndexOf('.');
		const QString suffix = (dot >= 0 ? f.mid(dot) : f).toLower();
		const std::wstring fw = vvd::NativeSeparators(VVDString(f.toStdWString())).ToStdWstring();
		if (suffix == ".zarr" || suffix == ".zgroup" || suffix == ".zattrs")
		{
			std::vector<std::wstring> zpaths;
			BRKXMLReader::GetZarrChannelPaths(fw, zpaths);
			std::wstring root = fw;
			if (suffix == ".zgroup" || suffix == ".zattrs")
				root = fw.substr(0, fw.find_last_of(slashes));
			if (zpaths.empty())
				zpaths.push_back(root);
			QStringList list;
			for (const std::wstring& p : zpaths) list << relative(p, root);
			if (zpaths.size() >= 2)
			{
				std::vector<bool> sel;
				if (FileDialogs::SelectDatasets(this, f, list, sel))
					for (size_t i = 0; i < zpaths.size(); i++)
						if (sel[i])
						{
							out << QString::fromStdWString(zpaths[i] + L".zfs_ch");
							meta << (suffix == ".zattrs" ? f : QString());
						}
			}
			else
			{
				const QString z0 = QString::fromStdWString(zpaths[0]);
				const int d2 = z0.lastIndexOf('.');
				const QString suffix2 = (d2 >= 0 ? z0.mid(d2) : z0).toLower();
				out << (suffix2 == ".zarr" ? z0 : z0 + ".zfs_ch");
				meta << QString();
			}
		}
		else if (suffix == ".n5" || suffix == ".json")
		{
			std::vector<std::wstring> n5paths;
			BRKXMLReader::GetN5ChannelPaths(fw, n5paths);
			std::wstring root = fw;
			if (suffix == ".json")
				root = fw.substr(0, fw.find_last_of(slashes));
			if (n5paths.empty())
				n5paths.push_back(root);
			QStringList list;
			for (const std::wstring& p : n5paths) list << relative(p, root);
			std::vector<bool> sel;
			if (FileDialogs::SelectDatasets(this, f, list, sel))
				for (size_t i = 0; i < n5paths.size(); i++)
					if (sel[i])
					{
						out << QString::fromStdWString(n5paths[i] + L".n5fs_ch");
						meta << (suffix == ".json" ? f : QString());
					}
		}
		else
		{
			out << f;
			meta << QString();
		}
	}
}

void MainWindow::LoadVolumes(const QStringList& files_in, const QStringList& descs, RenderCanvas* target)
{	// wx LoadVolumes(files, view): the given view, else GetView(0)
	RenderCanvas* const canvas = target ? target : GetViewCanvas(0);

	if (files_in.isEmpty()) return;
	QStringList files, metadata;
	// a filesystem error in the Zarr / N5 scan must not leave the event loop
	// (an uncaught exception there ends the app)
	try { ExpandZarrN5(files_in, files, metadata); }
	catch (const std::exception& e)
	{
		QMessageBox::warning(this, tr("Open Volume"), tr("Cannot open the dataset:\n%1").arg(QString::fromLocal8Bit(e.what())));
		return;
	}
	if (files.isEmpty()) return;
	// The volume renderer is created on load and needs the Vulkan context.
	if (!canvas->EnsureVulkan())
	{
		QMessageBox::critical(this, tr("Vulkan"), tr("Vulkan could not be initialised; cannot load volumes."));
		return;
	}
	// no frames of the half-loaded scene while the progress dialog spins
	// the event loop (RenderCanvas::SuspendRendering)
	struct SuspendGuard { RenderCanvas* c; ~SuspendGuard() { c->ResumeRendering(); } } suspend{ canvas };
	canvas->SuspendRendering();

	QElapsedTimer timer;
	timer.start();
	// read + decode all files in parallel; finish them below in file order
	// (VolumePrefetcher: wx's ProjectDataLoader in prefetch mode)
	// wx VRenderFrame::LoadVolumes: the Open Volume dialog options for every load
	m_data_mgr->SetSliceSequence(m_open_opts.slice_seq);
	m_data_mgr->SetTimeSequence(m_open_opts.time_seq);
	m_data_mgr->SetTimeId(VVDString(m_open_opts.time_id.toStdWString()));
	m_settings->SetTimeId(m_open_opts.time_id);
	// wx: m_data_mgr.SetLoadMask(m_load_mask), m_load_mask = true: the
	// companion <name>.msk / <name>.lbl of a volume file are loaded with it
	m_data_mgr->SetLoadMask(true);

	std::vector<int> types;
	std::vector<bool> prefetched;
	std::vector<VolumePrefetcher::Request> requests;
	for (int fi = 0; fi < files.size(); ++fi)
	{
		const QString& f = files[fi];
		int type = LoadTypeForSuffix("." + QFileInfo(f).suffix());
		types.push_back(type);
		// a metadata file is applied by the reader LoadVolumeData creates
		// (a memory budget, wx StartupLoad(files, datasize), loads directly)
		prefetched.push_back(type != 0 && metadata[fi].isEmpty() && m_load_datasize == 0);
		if (!prefetched.back()) continue;
		VolumePrefetcher::Request r;
		r.filename = VVDString(f.toStdWString());
		r.type = type;
		r.slice_seq = m_data_mgr->GetSliceSequence();
		r.time_seq = m_data_mgr->GetTimeSequence();
		r.time_id = m_data_mgr->GetTimeId();
		requests.push_back(r);
	}
	std::unique_ptr<QProgressDialog> prg;
	if (requests.size() > 1)
	{
		prg = std::make_unique<QProgressDialog>(tr("Opening volumes. Please wait."), QString(), 0, (int)requests.size(), this);
		prg->setWindowTitle(tr("Open Volume"));
		prg->setWindowModality(Qt::ApplicationModal);
		prg->setCancelButton(nullptr);
		prg->setMinimumDuration(300);
		prg->setAutoClose(false);
		prg->setAutoReset(false);
		prg->setValue(0);
	}
	VolumePrefetcher prefetcher(m_data_mgr, requests);
	int finished = 0;
	QElapsedTimer idle_timer;
	idle_timer.start();
	auto idle = [&]() {
		if (idle_timer.elapsed() < 50) return;
		idle_timer.restart();
		if (prg) prg->setValue(std::min((prefetcher.Decoded() + finished) / 2, (int)requests.size()));
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	};

	VolumeData* last = nullptr;
	size_t req_idx = 0;
	for (int fidx = 0; fidx < files.size(); ++fidx)
	{
		const QString& f = files[fidx];
		QFileInfo fi(f);
		m_last_dir = fi.absolutePath();
		int type = types[fidx];
		if (type == 0)
		{
			QMessageBox::warning(this, tr("Open Volume"), tr("Unsupported file type: %1").arg(fi.fileName()));
			continue;
		}
		std::unique_ptr<VolumePrefetch> pre;
		if (prefetched[fidx]) pre = prefetcher.Wait(req_idx++, idle);
		m_data_mgr->SetCompression(m_settings->GetRealtimeCompress());
		m_data_mgr->SetSkipBrick(m_settings->GetSkipBricks());
		VVDString path(f.toStdWString());
		// wx: prefix = descs[j] (command line -d/--desc), metadata file
		const VVDString prefix(fidx < descs.size() ? descs[fidx].toStdWString() : std::wstring());
		const VVDString meta(metadata[fidx].toStdWString());
		// the readers parse JSON / XML metadata while loading (N5 / Zarr / BDV):
		// an exception there must not leave the event loop (std::terminate)
		int ch_num = 0;
		QString load_error;
		try { ch_num = m_data_mgr->LoadVolumeData(path, type, -1, -1, m_load_datasize, prefix, meta, pre.get()); }
		catch (const std::exception& e) { load_error = QString::fromLocal8Bit(e.what()); ch_num = 0; }
		++finished;
		idle();
		if (ch_num <= 0)
		{
			if (!load_error.isEmpty())
				QMessageBox::warning(this, tr("Open Volume"), tr("Cannot open the dataset:\n%1\n%2").arg(fi.fileName(), load_error));
			else
				QMessageBox::warning(this, tr("Open Volume"), tr("Failed to load %1").arg(fi.fileName()));
			continue;
		}
		// channels are appended to the data manager in order; take the last
		// ch_num. Groups as in wx VRenderFrame::LoadVolumes: a multi-channel
		// file (and every .idi) gets a group of its own, hidden for every
		// file but the first; single channels go to the current group.
		int total = m_data_mgr->GetVolumeNum();
		const QString suffix = "." + fi.suffix().toLower();
		if (suffix == ".idi")
		{
			VVDString group_name = canvas->AddGroup();
			DataGroup* group = canvas->GetGroup(group_name);
			VolumeData* vd = m_data_mgr->GetVolumeData(total - 1);
			if (group && vd)
			{
				FLIVR::Color gamma(1.0, 1.0, 1.0);
				group->SetGammaAll(gamma);
				m_adjust->ApplyDefaults(vd);
				canvas->AddVolumeData(vd, group_name);
				last = vd;
			}
		}
		else if (ch_num > 1)
		{
			VVDString group_name = canvas->AddGroup();
			DataGroup* group = canvas->GetGroup(group_name);
			VolumeData* vd_first = nullptr;
			const bool h5j4 = suffix == ".h5j" && ch_num == 4;
			for (int i = ch_num; i > (h5j4 ? 1 : 0); --i)
			{
				VolumeData* vd = m_data_mgr->GetVolumeData(total - i);
				if (!vd) continue;
				m_adjust->ApplyDefaults(vd);
				canvas->AddVolumeData(vd, group_name);
				const VVDString vol_name = vd->GetName();
				if (vol_name.Find(L"_1ch") != -1 && (i == 1 || i == 2))
					vd->SetDisp(false);
				if (vol_name.Find(L"_2ch") != -1 && i == 1)
					vd->SetDisp(false);
				if (i == ch_num) vd_first = vd;
			}
			DataGroup* ref_group = nullptr;
			if (h5j4)
			{
				//reference
				VVDString ref_group_name = canvas->AddGroup();
				ref_group = canvas->GetGroup(ref_group_name);
				if (VolumeData* rvd = m_data_mgr->GetVolumeData(total - 1))
				{
					m_adjust->ApplyDefaults(rvd);
					canvas->AddVolumeData(rvd, ref_group_name);
				}
			}
			if (fidx > 0)
			{
				if (group) group->SetDisp(false);
				if (ref_group) ref_group->SetDisp(false);
			}
			if (vd_first) last = vd_first;
		}
		else
		{
			VolumeData* vd = m_data_mgr->GetVolumeData(total - 1);
			if (vd)
			{
				AddVolumeToView(vd, canvas);
				last = vd;
			}
		}
	}
	prg.reset();
	if (qEnvironmentVariableIsSet("VVD_QT_LOAD_TIMING"))
		fprintf(stderr, "[load] %lld volume file(s) in %lld ms (%d threads)\n", (long long)files.size(), (long long)timer.elapsed(), prefetcher.Threads());
	// wx: UpdateTree(vd_sel->GetName(), 2)
	if (last)
	{
		canvas->SetCurrentVolume(last);
		m_tree->UpdateTree(last->GetName().ToQt(), TreePanel::TypeVolume);
		if (m_tree->CurrentVolume() != last) m_tree->SelectVolume(last);
		SetStatus(tr("Loaded %1").arg(last->GetName().ToQt()));
	}
	else
		RefreshTree();
	// an open Legend drop-down lists the new volumes (RefreshTree does it
	// on the other path; wx reloads its LegendPanel only when it is shown)
	if (last)
		for (const auto& v : m_views) v->view->ReloadLegend();
	canvas->RefreshView();
}

void MainWindow::ApplyRulerSettings()
{
	for (const auto& v : m_views)
	{
		RulerTool* r = v->ruler;
		if (!r) continue;
		r->SetPointVolumeMode(m_settings->GetPointVolumeMode());
		r->SetRulerUseTransf(m_settings->GetRulerUseTransf());
		r->SetRulerTimeDep(m_settings->GetRulerTimeDep());
		r->SetRulerAutoSelect(m_settings->GetRulerAutoSelect());
		r->SetTextColorMode(m_settings->GetTextColor());
		r->SetFont(m_settings->GetFontPath(), m_settings->GetTextSize());
	}
}

void MainWindow::ClearScene()
{
	m_ruler_tool->ClearRulers();
	m_ruler_tool->RulersModified();
	if (m_info_dlg) m_info_dlg->SetData(nullptr);
	m_sel->SetVolume(nullptr);
	m_calc->SetVolumeA(nullptr);
	m_calc->SetVolumeB(nullptr);
	m_calc->SetVolumeC(nullptr);
	m_prop->SetVolume(nullptr);
	m_mesh_prop->SetMeshData(nullptr);
	m_mesh_manip->SetMeshData(nullptr);
	m_ann_prop->SetAnnotations(nullptr);
	m_prop_stack->setCurrentWidget(m_prop_empty);
	// ClearData, not SetVolume(nullptr): that is a no-op while a mesh is the
	// clip target, and the pane then kept the freed mesh
	m_clip->ClearData();
	m_view->SetCurrentVolume(nullptr);
	m_canvas->SetCurrentMesh(nullptr);
	m_canvas->ClearMeshes();     // waits for the GPU
	m_canvas->ClearVolumes();
	// ClearAll frees every annotation: the analyses must not keep theirs
	m_selector.ForgetAnnotations();
	for (const auto& v : m_views)
		if (v->sel) v->sel->ForgetAnnotations();
	m_data_mgr->ClearAll();
	m_prj_passthrough.clear();
	RefreshTree();
}

// wx VRenderFrame::SaveProject, /views/0/properties (+ /memory settings):
// every value the wx frame writes, read from the canvas / overlays.
void MainWindow::CaptureProjectView(ProjectIO::ProjectView& v, int view)
{
	RenderCanvas* c = GetViewCanvas(view);
	ViewOverlays* overlays = GetViewOverlays(view);
	v.draw_all = c->GetDraw();
	v.persp = c->GetPerspective();
	v.free = c->GetFree();
	v.aov = c->GetAovValue();
	v.min_dpi = c->GetMinPPI();
	v.res_mode = c->GetResMode();
	v.near_clip = c->GetNearClip();
	v.far_clip = c->GetFarClip();
	v.bg_color = c->GetBackgroundColor();
	v.draw_type = c->GetDrawType();
	v.vol_method = c->GetVolMethod();
	v.peel_layers = c->GetPeelingLayers();
	v.fog = c->GetFog();
	v.fog_intensity = c->GetFogIntensity();
	if (overlays)
	{
		v.draw_camctr = overlays->GetDrawCamCtr();
		v.draw_fps = overlays->GetDrawInfo();
		v.draw_legend = overlays->GetDrawLegend();
	}
	//camera (wx VRenderView::GetRotations: x = 360 - m_rotx)
	c->GetTranslations(v.transx, v.transy, v.transz);
	double rx, ry, rz;
	c->GetRotations(rx, ry, rz);
	v.rotx = 360.0 - rx; v.roty = ry; v.rotz = rz;
	c->GetCenters(v.ctrx, v.ctry, v.ctrz);
	v.has_camera_frame = true;
	v.center_eye_dist = c->GetCenterEyeDist();
	v.radius = c->GetRadius();
	v.init_dist = c->GetInitDist();
	v.scale_factor = c->GetScaleFactor();
	//object
	c->GetObjCenter(v.obj_ctrx, v.obj_ctry, v.obj_ctrz);
	c->GetObjTrans(v.obj_transx, v.obj_transy, v.obj_transz);
	c->GetObjRot(v.obj_rotx, v.obj_roty, v.obj_rotz);
	//scale bar (wx writes sb_length before GetScaleBarFixed updates it)
	if (overlays)
	{
		v.disp_scale_bar = overlays->GetScaleBar();
		v.disp_scale_bar_text = overlays->GetSBTextEnabled();
		v.sb_length = overlays->GetScaleBarLen();
		v.sb_text = overlays->GetSBText();
		v.sb_num = overlays->GetSbNumText();
		v.sb_unit = overlays->GetSbUnitSel();
		v.sb_digit = overlays->GetScaleBarDigit();
		bool fixed; double len; int uid;
		overlays->GetScaleBarFixed(fixed, len, uid);
		v.sb_fixed = fixed;
	}
	//2d adjustment
	v.gamma = c->GetGamma();
	v.brightness = c->GetBrightness();
	v.hdr = c->GetHdr();
	v.levels = c->GetLevels();
	v.sync_r = c->GetSyncR();
	v.sync_g = c->GetSyncG();
	v.sync_b = c->GetSyncB();
	v.easy_2d_adjustment = c->GetEasy2DAdjustMode();
	//4d sequence
	int st = 0, ed = 0, cur = 0;
	c->Get4DSeqFrames(st, ed, cur);
	v.st_time = st; v.ed_time = ed; v.cur_time = cur;
	//clipping (VRenderView values: GetClippingPlaneRotations)
	ProjectIO::ProjectClipping& cl = v.clipping;
	cl.valid = true;
	c->GetClippingPlaneRotations(cl.rot_cl[0], cl.rot_cl[1], cl.rot_cl[2]);
	cl.sync_clipping_planes = c->GetSyncClippingPlanes();
	c->GetClipDistance(cl.clip_dist[0], cl.clip_dist[1], cl.clip_dist[2]);
	cl.link[0] = c->GetClippingLinkX();
	cl.link[1] = c->GetClippingLinkY();
	cl.link[2] = c->GetClippingLinkZ();
	for (int i = 0; i < 6; ++i) cl.linked_param[i] = c->GetLinkedParam(i);
	FLIVR::Quaternion q_cl_zero, q_cl_fix, q_fix;
	FLIVR::Vector trans_fix;
	c->GetClippingFixParams(cl.clip_mode, q_cl_zero, q_cl_fix, q_fix,
		cl.rot_cl_fix[0], cl.rot_cl_fix[1], cl.rot_cl_fix[2],
		cl.rot_fix[0], cl.rot_fix[1], cl.rot_fix[2], trans_fix);
	auto q4 = [](const FLIVR::Quaternion& q, double* o) { o[0] = q.x; o[1] = q.y; o[2] = q.z; o[3] = q.w; };
	q4(q_cl_zero, cl.q_cl_zero);
	q4(q_cl_fix, cl.q_cl_fix);
	q4(q_fix, cl.q_fix);
	cl.trans_fix[0] = trans_fix.x(); cl.trans_fix[1] = trans_fix.y(); cl.trans_fix[2] = trans_fix.z();
	cl.has_fix = cl.has_mode = cl.has_rot = cl.has_sync = cl.has_dist = true;
	//clip panel
	v.plane_mode = m_clip ? m_clip->GetPlaneMode() : 0;
	//streaming settings
	v.memory.valid = true;
	v.memory.mem_swap = m_settings->GetMemSwap();
	v.memory.graphics_mem = m_settings->GetGraphicsMem();
	v.memory.large_data_size = m_settings->GetLargeDataSize();
	v.memory.force_brick_size = m_settings->GetForceBrickSize();
	v.memory.up_time = m_settings->GetResponseTime();
	v.memory.update_order = m_settings->GetUpdateOrder();
}

// wx VRenderFrame::OpenProject, /views/0/properties, in the wx order. v was
// pre-filled with CaptureProjectView, so keys missing from the file keep the
// current state (the wx "if (fconfig.Read(...))" rule).
void MainWindow::ApplyProjectView(const ProjectIO::ProjectView& v, int view)
{
	RenderCanvas* c = GetViewCanvas(view);
	ViewOverlays* overlays = GetViewOverlays(view);
	ViewPanel* vpanel = GetViewPanel(view);
	c->SetDraw(v.draw_all);
	c->SetPersp(v.persp);
	c->SetFree(v.free);
	c->SetAovValue(v.aov);
	c->SetMinPPI(v.min_dpi);
	c->SetResMode(v.res_mode);
	c->SetNearClip(v.near_clip);
	c->SetFarClip(v.far_clip);
	c->SetBackgroundColor(v.bg_color);
	c->SetVolMethod(v.vol_method);
	c->SetPeelingLayers(v.peel_layers);
	c->SetFog(v.fog);
	c->SetFogIntensity(v.fog_intensity);
	if (overlays)
	{
		overlays->SetDrawCamCtr(v.draw_camctr);
		overlays->SetDrawInfo(v.draw_fps);
		overlays->SetDrawLegend(v.draw_legend);   // legends of the data: ProjectIO
	}
	if (vpanel) vpanel->SyncOverlayChecks();
	//camera (wx VRenderView::SetRotations: m_rotx = 360 - x)
	c->SetTranslations(v.transx, v.transy, v.transz);
	c->SetRotations(360.0 - v.rotx, v.roty, v.rotz);
	c->SetCenters(v.ctrx, v.ctry, v.ctrz);
	c->SetCenterEyeDist(v.center_eye_dist);
	c->SetRadius(v.radius);
	c->SetInitDist(v.init_dist);
	c->SetScaleFactor(v.scale_factor);
	//object
	c->SetObjCenters(v.obj_ctrx, v.obj_ctry, v.obj_ctrz);
	c->SetObjTrans(v.obj_transx, v.obj_transy, v.obj_transz);
	c->SetObjRot(v.obj_rotx, v.obj_roty, v.obj_rotz);
	//scale bar
	if (overlays)
	{
		if (v.disp_scale_bar) overlays->EnableScaleBar(); else overlays->DisableScaleBar();
		if (v.disp_scale_bar_text) overlays->EnableSBText(); else overlays->DisableSBText();
		overlays->SetScaleBarLen(v.sb_length);
		overlays->SetSBText(v.sb_text);
		overlays->SetSbNumText(v.sb_num);
		overlays->SetSbUnitSel(v.sb_unit);
		overlays->SetScaleBarDigit(v.sb_digit);
		overlays->FixScaleBarLen(v.sb_fixed);
		if (m_prop && c == m_canvas) m_prop->GetScaleBarSettings();
	}
	//2d adjustment
	c->SetGamma(v.gamma);
	c->SetBrightness(v.brightness);
	c->SetHdr(v.hdr);
	c->SetLevels(v.levels);
	c->SetSyncR(v.sync_r);
	c->SetSyncG(v.sync_g);
	c->SetSyncB(v.sync_b);
	c->SetEasy2DAdjustMode(v.easy_2d_adjustment);
	//clipping plane rotations
	const ProjectIO::ProjectClipping& cl = v.clipping;
	if (cl.has_mode)
		c->SetClipMode(cl.clip_mode);
	if (cl.has_rot)
		c->SetClippingPlaneRotations(cl.rot_cl[0], cl.rot_cl[1], cl.rot_cl[2]);
	if (cl.has_sync)
		c->SetSyncClippingPlanes(cl.sync_clipping_planes);
	else
		c->SyncClippingPlaneRotations(true);   //for old versions
	if (cl.has_dist)
		c->SetClipDistance(cl.clip_dist[0], cl.clip_dist[1], cl.clip_dist[2]);
	c->SetClippingLinkX(cl.link[0]);
	c->SetClippingLinkY(cl.link[1]);
	c->SetClippingLinkZ(cl.link[2]);
	for (int i = 0; i < 6; ++i) c->SetLinkedParam(i, cl.linked_param[i]);
	if (cl.has_fix)
	{
		auto q4 = [](const double* o) { return FLIVR::Quaternion(o[0], o[1], o[2], o[3]); };
		c->SetClippingFixParams(cl.clip_mode, q4(cl.q_cl_zero), q4(cl.q_cl_fix), q4(cl.q_fix),
			cl.rot_cl_fix[0], cl.rot_cl_fix[1], cl.rot_cl_fix[2],
			cl.rot_fix[0], cl.rot_fix[1], cl.rot_fix[2],
			FLIVR::Vector(cl.trans_fix[0], cl.trans_fix[1], cl.trans_fix[2]));
	}
	c->RefreshView();
}

// VRenderFrame::OpenProject, through ProjectIO.
bool MainWindow::OpenProject(const QString& path)
{
	VVDString prj(path.toStdWString());
	if (!vvd::FileExists(prj))
		return false;
	// back to one view (the project brings its own further views)
	ClearExtraViews();
	SetActiveView(0);
	if (!m_canvas->EnsureVulkan())
		return false;

	const bool timing = qEnvironmentVariableIsSet("VVD_QT_LOAD_TIMING");
	// no frames until the project is set up (see LoadVolumes)
	struct SuspendGuard { RenderCanvas* c; ~SuspendGuard() { c->ResumeRendering(); } } suspend{ m_canvas };
	m_canvas->SuspendRendering();
	QElapsedTimer timer;
	timer.start();
	auto lap = [&](const char* what) {
		if (timing) fprintf(stderr, "[load] %-28s %8lld ms\n", what, (long long)timer.restart());
	};
	ClearScene();
	lap("ClearScene");
	m_data_mgr->SetCompression(m_settings->GetRealtimeCompress());
	m_data_mgr->SetSkipBrick(m_settings->GetSkipBricks());

	ProjectIO::ProjectView view;
	view.rulers = m_ruler_tool->GetRulerList();
	view.interpolator = m_interpolator.get();
	// keys missing from the file keep the current view state
	CaptureProjectView(view, 0);

	std::vector<VolumeData*> vols;
	std::vector<MeshData*> meshes;
	QString error;
	QApplication::setOverrideCursor(Qt::WaitCursor);
	bool ok = ProjectIO::LoadProject(path, m_data_mgr, vols, meshes, view, &error, this);
	QApplication::restoreOverrideCursor();
	lap("ProjectIO::LoadProject");
	if (!ok)
	{
		QMessageBox::warning(this, tr("Open Project"), error.isEmpty() ? tr("Could not open %1").arg(path) : error);
		return true;   // already reported
	}
	// wx OpenProject: version check (shown before loading in wx)
	if (view.newer_version)
	{
		const QString msg = tr("The project file is saved by a newer version of VVDViewer.\n"
			"Please check update and download the new version.");
		if (VVDQtScriptedRun()) fprintf(stderr, "[project] %s\n", msg.toLocal8Bit().constData());
		else QMessageBox::information(this, tr("Open Project"), msg);
	}
	m_prj_passthrough = view.passthrough;
	ApplyProjectBrushSettings();   // wx OpenProject: view painting parameters, /brush_diag
	ApplyProjectUILayout();        // wx OpenProject: /ui_layout (pane visibility)
	m_ruler_tool->RulersModified();

	// the view's layer tree as wx VRenderFrame::OpenProject builds it
	// (the project's own colours are kept)
	BuildLayersFromProject(view, vols, meshes, m_canvas, true);
	lap("canvas layers");

	// wx VRenderFrame::OpenProject /views/0/properties (camera, object,
	// scale bar, 2D adjustment, clipping, ...)
	ApplyProjectView(view, 0);
	lap("canvas view settings");

	// wx OpenProject: /views/<i> for i > 0, each in a new view (CreateView):
	// layers, properties, painting parameters, rulers
	for (int vi = 0; vi < (int)view.extra_views.size(); ++vi)
	{
		ProjectIO::ProjectView& ev = view.extra_views[vi];
		CreateView();
		const int idx = (int)m_views.size() - 1;
		ViewSlot& slot = *m_views[idx];
		slot.canvas->EnsureVulkan();
		BuildLayersFromProject(ev, vols, meshes, slot.canvas, false);
		ApplyProjectView(ev, idx);
		slot.prj_props = ev.passthrough;
		ApplyProjectBrushSettings(idx);
		if (std::vector<Ruler*>* rl = slot.ruler->GetRulerList())
		{
			for (Ruler* r : *rl) delete r;
			rl->clear();
			for (Ruler* r : ev.owned_rulers) rl->push_back(r);
			ev.owned_rulers.clear();
		}
		slot.ruler->RulersModified();
	}
	if (!view.extra_views.empty())
		lap("further views");

	// wx: UpdateTree(<selected volume / mesh>), then ImportExpState. A
	// saved selection of another type (view, group, ...) selects nothing.
	VolumeData* sel = view.selected_volume ? view.selected_volume :
		((vols.empty() || view.has_selection) ? nullptr : vols.back());
	if (view.selected_mesh && !view.selected_volume)
		m_tree->UpdateTree(view.selected_mesh->GetName().ToQt(), TreePanel::TypeMesh);
	else if (sel)
	{
		RenderCanvas* sel_canvas = m_canvas;
		for (const auto& sv : m_views)
		{
			const auto& vl = sv->canvas->Volumes();
			if (std::find(vl.begin(), vl.end(), sel) != vl.end()) { sel_canvas = sv->canvas; break; }
		}
		sel_canvas->SetCurrentVolume(sel);
		m_tree->UpdateTree(sel->GetName().ToQt(), TreePanel::TypeVolume);
	}
	else
		m_tree->UpdateTree();
	if (!view.tree_exp_states.isEmpty())
		m_tree->ImportExpState(view.tree_exp_states);
	// wx: m_cur_sel_* = the saved values
	if (view.has_selection)
	{
		m_cur_sel_type = view.cur_sel_type;
		m_cur_sel_vol = view.cur_sel_vol;
		m_cur_sel_mesh = view.cur_sel_mesh;
	}
	// wx OpenProject /prop_panel: plane_mode, then the legacy "chann_link"
	// keys of old projects
	m_clip->SetRenderCanvas(m_canvas);
	if (view.has_plane_mode)
		m_clip->SetPlaneMode(view.plane_mode);
	if (view.has_chann_link)
	{
		if (view.has_linked_legacy)
			for (int k = 0; k < 6; ++k) m_canvas->SetLinkedParam(k, view.linked_legacy[k]);
		else if (!view.chann_link)
			for (int k = 0; k < 6; ++k) m_canvas->SetLinkedParam(k, 0.0);   //old version
		else
		{
			std::vector<FLIVR::Plane*>* planes = nullptr;
			if (view.cur_sel_type == 2 && view.selected_volume && view.selected_volume->GetVR())
				planes = view.selected_volume->GetVR()->get_planes();
			else if (view.cur_sel_type == 3 && view.selected_mesh && view.selected_mesh->GetMR())
				planes = view.selected_mesh->GetMR()->get_planes();
			if (planes && planes->size() == 6)
				for (int k = 0; k < 6; ++k) m_canvas->SetLinkedParam(k, (*planes)[k]->GetParam());
		}
		m_canvas->SetSyncClippingPlanes(view.chann_link);
		if (view.chann_link)
			m_canvas->CalcAndSetCombinedClippingPlanes();
		if (view.has_axis_link[0] >= 0) m_canvas->SetClippingLinkX(view.has_axis_link[0] != 0);
		if (view.has_axis_link[1] >= 0) m_canvas->SetClippingLinkY(view.has_axis_link[1] != 0);
		if (view.has_axis_link[2] >= 0) m_canvas->SetClippingLinkZ(view.has_axis_link[2] != 0);
	}
	m_clip->GetSettings();
	// wx OpenProject end: with "Sync All Chan." the combined planes and the
	// clipping rotations are re-applied
	if (m_canvas->GetSyncClippingPlanes())
	{
		m_canvas->CalcAndSetCombinedClippingPlanes();
		double crx, cry, crz;
		m_canvas->GetClippingPlaneRotations(crx, cry, crz);
		m_canvas->SetClippingPlaneRotations(crx, cry, crz);
	}
	if (m_view) m_view->ReloadLegend();
	lap("UpdateTree / select");
	// wx OpenProject: /interpolator -> RecorderDlg::UpdateList, /movie_panel
	if (view.interpolator_loaded)
		m_movie->GetRecorder()->UpdateList();
	m_movie->LoadProjectSettings(view.movie);
	m_canvas->RefreshView();
	lap("RefreshView");
	if (!error.isEmpty())
		QMessageBox::warning(this, tr("Open Project"), error);
	// wx OpenProject: /ui_layout/<plugin> (pane, floating, .set file)
	ApplyProjectPluginLayout();
	SetStatus(tr("Opened project %1 (%2 volumes, %3 meshes)").arg(QFileInfo(path).fileName()).arg(vols.size()).arg(meshes.size()));
	return true;
}

// wx VRenderFrame::OpenProject, /views/0/layers: volumes / meshes by name,
// groups created with their saved id, display, 2D adjustment, sync flags and
// clipping, members added through AddVolumeData(vd, group) (which hands
// them the group's 2D adjustment, as in wx). Files without a layer tree:
// every loaded item, ungrouped volumes into one group (as before).
void MainWindow::BuildLayersFromProject(const ProjectIO::ProjectView& view,
	const std::vector<VolumeData*>& vols, const std::vector<MeshData*>& meshes,
	RenderCanvas* canvas, bool reset_ids)
{
	// wx OpenProject: the group ids are reset once, before the views
	if (reset_ids)
	{
		DataGroup::ResetID();
		MeshGroup::ResetID();
	}
	if (view.layers.empty())
	{
		for (VolumeData* vd : vols) if (vd) canvas->AddVolume(vd);
		for (MeshData* md : meshes) if (md) canvas->AddMesh(md);
		return;
	}
	for (const ProjectIO::ProjectLayer& l : view.layers)
	{
		const VVDString name(l.name.toStdWString());
		switch (l.type)
		{
		case ProjectIO::ProjectLayer::Volume:
			if (VolumeData* vd = m_data_mgr->GetVolumeData(name)) canvas->AddVolumeData(vd);
			break;
		case ProjectIO::ProjectLayer::Mesh:
			if (MeshData* md = m_data_mgr->GetMeshData(name)) canvas->AddMeshData(md);
			break;
		case ProjectIO::ProjectLayer::Annotations:
			if (Annotations* ann = m_data_mgr->GetAnnotations(name)) canvas->AddAnnotations(ann);
			break;
		case ProjectIO::ProjectLayer::VolumeGroup:
		{
			if (l.id) DataGroup::SetID(l.id - 1);
			VVDString gname = canvas->AddGroup(name);
			DataGroup* group = canvas->GetGroup(gname);
			if (!group) break;
			group->SetDisp(l.display);
			group->SetGamma(l.gamma);
			group->SetBrightness(l.brightness);
			group->SetHdr(l.hdr);
			group->SetLevels(l.levels);
			group->SetSyncR(l.sync_r);
			group->SetSyncG(l.sync_g);
			group->SetSyncB(l.sync_b);
			group->SetVolumeSyncProp(l.sync_vp);
			group->SetVolumeSyncSpc(l.sync_vspc);
			for (const QString& m : l.members)
				if (VolumeData* vd = m_data_mgr->GetVolumeData(VVDString(m.toStdWString())))
					canvas->AddVolumeData(vd, group->GetName());
			ProjectIO::ApplyClipping(l.clipping, group);
		}
		break;
		case ProjectIO::ProjectLayer::MeshGroup:
		{
			if (l.id) MeshGroup::SetID(l.id);
			VVDString gname = canvas->AddMGroup(name);
			MeshGroup* group = canvas->GetMGroup(gname);
			if (!group) break;
			group->SetDisp(l.display);
			group->SetMeshSyncProp(l.sync_mp);
			for (int k = 0; k < (int)l.members.size(); k++)
				if (MeshData* md = m_data_mgr->GetMeshData(VVDString(l.members[k].toStdWString())))
					group->InsertMeshData(k - 1, md);
			canvas->LayersModified();
			ProjectIO::ApplyClipping(l.clipping, group);
		}
		break;
		}
	}
	canvas->SetDraw(view.draw_all);
}

// wx SaveProject /views/<i>/layers: the view's layer list
void MainWindow::CaptureProjectLayers(RenderCanvas* c, std::vector<ProjectIO::ProjectLayer>& layers)
{
	for (int j = 0; j < c->GetLayerNum(); j++)
	{
		TreeLayer* layer = c->GetLayer(j);
		if (!layer) continue;
		ProjectIO::ProjectLayer l;
		l.type = layer->IsA();
		l.name = QString::fromStdWString(layer->GetName());
		if (l.type == ProjectIO::ProjectLayer::VolumeGroup)
		{
			DataGroup* group = (DataGroup*)layer;
			l.id = DataGroup::GetID();
			l.display = group->GetDisp();
			l.gamma = group->GetGamma();
			l.brightness = group->GetBrightness();
			l.hdr = group->GetHdr();
			l.levels = group->GetLevels();
			l.sync_r = group->GetSyncR();
			l.sync_g = group->GetSyncG();
			l.sync_b = group->GetSyncB();
			l.sync_vp = group->GetVolumeSyncProp();
			l.sync_vspc = group->GetVolumeSyncSpc();
			for (int k = 0; k < group->GetVolumeNum(); k++)
				if (VolumeData* vd = group->GetVolumeData(k)) l.members.push_back(vd->GetName().ToQt());
			ProjectIO::CaptureClipping(group, l.clipping);
		}
		else if (l.type == ProjectIO::ProjectLayer::MeshGroup)
		{
			MeshGroup* group = (MeshGroup*)layer;
			l.id = MeshGroup::GetID();
			l.display = group->GetDisp();
			l.sync_mp = group->GetMeshSyncProp();
			for (int k = 0; k < group->GetMeshNum(); k++)
				if (MeshData* md = group->GetMeshData(k)) l.members.push_back(md->GetName().ToQt());
			ProjectIO::CaptureClipping(group, l.clipping);
		}
		layers.push_back(l);
	}

}

void MainWindow::OnSaveProject()
{
	// wx OnSaveProject + CreateExtraControlProjectSave (embed, compression)
	QString f = FileDialogs::SaveProject(this, m_last_dir, m_vrp_embed,
		[this]() { return m_settings->GetSaveCompress(); },
		[this](bool b) { m_settings->SetSaveCompress(b); });
	if (f.isEmpty()) return;
	if (!f.endsWith(".vrp", Qt::CaseInsensitive)) f += ".vrp";
	m_last_dir = QFileInfo(f).absolutePath();
	SaveProject(f);
}

bool MainWindow::SaveProject(const QString& f)
{

	ProjectIO::ProjectView view;
	CaptureProjectView(view, 0);
	view.raw_selection = true;   // wx m_cur_sel_type / _vol / _mesh
	view.cur_sel_type = m_cur_sel_type;
	view.cur_sel_vol = m_cur_sel_vol;
	view.cur_sel_mesh = m_cur_sel_mesh;
	view.selected_volume = m_canvas->GetCurrentMesh() ? nullptr : m_canvas->GetCurrentVolume();
	view.selected_mesh = m_canvas->GetCurrentMesh();
	StoreProjectBrushSettings(0);  // wx SaveProject: view painting parameters, /brush_diag
	StoreProjectUILayout();        // wx SaveProject: /ui_layout
	StoreProjectPluginLayout(f);   // wx SaveProject: /ui_layout/<plugin>, plugin .set files
	view.passthrough = m_prj_passthrough;
	view.rulers = m_views[0]->ruler->GetRulerList();
	view.interpolator = m_interpolator.get();
	m_movie->SaveProjectSettings(view.movie);
	view.draw_all = GetViewCanvas(0)->GetDraw();
	view.tree_exp_states = m_tree->ExportExpState();
	// the layer tree (wx SaveProject /views/0/layers)
	CaptureProjectLayers(GetViewCanvas(0), view.layers);
	// wx SaveProject: /views/<i> of the further views
	for (int vi = 1; vi < (int)m_views.size(); ++vi)
	{
		ViewSlot& slot = *m_views[vi];
		ProjectIO::ProjectView ev;
		CaptureProjectView(ev, vi);
		ev.memory.valid = false;
		StoreProjectBrushSettings(vi);
		ev.passthrough = slot.prj_props;
		ev.rulers = slot.ruler->GetRulerList();
		ev.draw_all = slot.canvas->GetDraw();
		CaptureProjectLayers(slot.canvas, ev.layers);
		view.extra_views.push_back(std::move(ev));
	}
	ProjectIO::SaveOptions opts;
	opts.compress = m_settings->GetSaveCompress();
	opts.embed = m_vrp_embed;   // wx VRenderFrame::m_vrp_embed (capture dialog)
	QString error;
	QApplication::setOverrideCursor(Qt::WaitCursor);
	// wx SaveProject: /data/volume and /data/mesh in DataManager order
	std::vector<VolumeData*> dm_vols;
	for (int i = 0; i < m_data_mgr->GetVolumeNum(); i++) dm_vols.push_back(m_data_mgr->GetVolumeData(i));
	std::vector<MeshData*> dm_meshes;
	for (int i = 0; i < m_data_mgr->GetMeshNum(); i++) dm_meshes.push_back(m_data_mgr->GetMeshData(i));
	bool ok = ProjectIO::SaveProject(f, m_data_mgr, dm_vols, dm_meshes, view, &error, opts);
	QApplication::restoreOverrideCursor();
	if (!ok)
		QMessageBox::warning(this, tr("Save Project"), error.isEmpty() ? tr("Could not save %1").arg(f) : error);
	else
		SetStatus(tr("Saved project %1").arg(QFileInfo(f).fileName()));
	return ok;
}

void MainWindow::OnOpenMesh()
{
	QStringList files = FileDialogs::OpenFiles(this, "OPENMESH", tr("Choose the mesh data file"), m_last_dir, MeshIO::FileDialogFilter());
	LoadMeshFiles(files);
}

// wx VRenderFrame::LoadMeshes(files, vrv, descs, group_name): several mesh
// files (csv not counted) or a group name -> one new mesh group (named
// group_name when given) that receives the meshes of the .obj / .swc / .ply
// files; a .csv is added on its own (ConvertCSV2SWC), an .nml gets a group of
// its own named after the file (ConvertNML2SWC).
void MainWindow::LoadMeshFiles(const QStringList& files, const QString& group_name, const QStringList& descs,
	RenderCanvas* target)
{	// wx LoadMeshes(files, vrv): the given view, else GetView(0)
	RenderCanvas* const canvas = target ? target : GetViewCanvas(0);

	if (files.isEmpty()) return;
	if (!canvas->EnsureVulkan())
	{
		QMessageBox::critical(this, tr("Vulkan"), tr("Vulkan could not be initialised; cannot load meshes."));
		return;
	}
	m_last_dir = QFileInfo(files.last()).absolutePath();
	auto suffix_of = [](const QString& f) { const int d = f.lastIndexOf('.'); return (d >= 0 ? f.mid(d) : QString()).toLower(); };
	int count = 0;
	for (const QString& f : files)
		if (suffix_of(f) != ".csv") count++;
	MeshGroup* group = nullptr;
	if (count > 1 || !group_name.isEmpty())
	{
		group = canvas->GetMGroup(canvas->AddMGroup());
		if (group && !group_name.isEmpty())
			group->SetName(VVDString(group_name.toStdWString()));
	}
	MeshData* md_sel = nullptr;
	QStringList errors;
	for (int i = 0; i < files.size(); i++)
	{
		const QString& f = files[i];
		const QString suffix = suffix_of(f);
		QList<MeshData*> got;
		MeshIO::LoadMeshes(*m_data_mgr, QStringList(f), &got,
			QStringList(i < descs.size() ? descs[i] : QString()), &errors);
		if (got.isEmpty()) continue;
		if (suffix == ".nml")
		{
			MeshGroup* ng = canvas->GetMGroup(canvas->AddMGroup());
			if (ng) ng->SetName(VVDString(QFileInfo(f).fileName().toStdWString()));
			for (MeshData* md : got)
				if (ng) ng->InsertMeshData(ng->GetMeshNum() - 1, md);
				else canvas->AddMeshData(md);
			canvas->LayersModified();
		}
		else if (suffix == ".csv" || !group)
			for (MeshData* md : got) canvas->AddMeshData(md);
		else
		{
			for (MeshData* md : got) group->InsertMeshData(group->GetMeshNum() - 1, md);
			canvas->LayersModified();   // wx SetMeshPopDirty + InitView(INIT_BOUNDS | INIT_CENTER)
		}
		md_sel = got.last();
	}
	RefreshTree();
	if (md_sel)
	{
		m_tree->SelectMesh(md_sel);
		OnMeshSelected(md_sel);
	}
	canvas->RefreshView();
	if (!errors.isEmpty())
		QMessageBox::warning(this, tr("Open Mesh"), errors.join('\n'));
}

void MainWindow::OnMeshSelected(MeshData* md)
{
	OnTreeSelection(3, nullptr, nullptr, md);
}

void MainWindow::ForgetMesh(MeshData* md)
{
	if (!md) return;
	if (m_mesh_prop->GetMeshData() == md)
	{
		m_mesh_prop->SetMeshData(nullptr);
		m_prop_stack->setCurrentWidget(m_prop_empty);
	}
	if (m_mesh_manip->GetMeshData() == md)
	{
		m_mesh_manip->SetMeshData(nullptr);
		if (m_prop_stack->currentWidget() == m_mesh_manip)
			m_prop_stack->setCurrentWidget(m_prop_empty);
	}
	// as ForgetVolume: the Clipping Planes pane must not keep the mesh (its
	// sliders, Link, S/W, Align, Fix and Reset write into it)
	if (m_clip->GetMeshData() == md)
		m_clip->SetMeshData(nullptr);
	for (const auto& v : m_views)
		v->canvas->RemoveMesh(md);               // waits for the GPU; before the delete
	m_data_mgr->RemoveMeshData(md->GetName());   // deletes md
}

void MainWindow::OnMeshRemove(MeshData* md)
{
	if (!md) return;
	ForgetMesh(md);
	RefreshTree();
	m_canvas->RefreshView();
}

// ---------------------------------------------------------------------------
// slots
// ---------------------------------------------------------------------------

void MainWindow::OnOpenVolume()
{
	// wx OnOpenVolume + CreateExtraControlVolume
	FileDialogs::OpenVolumeHooks hooks;
	hooks.get_compress = [this]() { return m_settings->GetRealtimeCompress(); };
	hooks.set_compress = [this](bool b) { m_settings->SetRealtimeCompress(b); };
	hooks.get_skip_brick = [this]() { return m_settings->GetSkipBricks(); };
	hooks.set_skip_brick = [this](bool b) { m_settings->SetSkipBricks(b); };
	hooks.update_settings_ui = [this]() { m_settings->UpdateUI(); };
	QStringList files = FileDialogs::OpenVolume(this, m_last_dir, tr(kVolumeFilter), m_open_opts, hooks);
	LoadVolumes(files);
}

void MainWindow::OnOpenProject()
{
	QString f = FileDialogs::OpenFile(this, "OPENPROJECT", tr("Choose Project File"), m_last_dir, tr("VVDViewer project (*.vrp);;All files (*)"));
	if (f.isEmpty()) return;
	m_last_dir = QFileInfo(f).absolutePath();
	if (!OpenProject(f))
		QMessageBox::warning(this, tr("Open Project"), tr("Could not open %1").arg(f));
}

void MainWindow::OnResetView() { m_canvas->ResetView(); }
void MainWindow::OnTogglePerspective(bool on) { m_canvas->SetPerspective(on); }

void MainWindow::OnBackgroundColor()
{
	FLIVR::Color c = m_canvas->GetBackgroundColor();
	QColor chosen = QColorDialog::getColor(QColor::fromRgbF(c.r(), c.g(), c.b()), this, tr("Background color"));
	if (chosen.isValid())
		m_canvas->SetBackgroundColor(FLIVR::Color(chosen.redF(), chosen.greenF(), chosen.blueF()));
}

void MainWindow::OnVolumeSelected(VolumeData* vd)
{
	OnTreeSelection(2, vd ? m_canvas->GetParentGroup(vd) : nullptr, vd, nullptr);
}

// wx DataTreeCtrl::UpdateSelection + VRenderFrame::OnSelection(type, vrv,
// group, vd, md): the Output Adjustments / Clipping Planes targets and the
// Properties pane follow the tree selection
void MainWindow::OnTreeSelection(int type, DataGroup* group, VolumeData* vd, MeshData* md)
{
	// wx OnSelection(type, vrv, ...): the panes follow the item's view
	if (type >= 1 && type <= 5)
		ActivateCanvas(m_tree->CurCanvas());
	// UpdateSelection: volume A cleared, set again for a volume
	m_calc->SetVolumeA(nullptr);
	if (type == 2)
	{
		m_adjust->SetGroupLink(group);
		m_calc->SetVolumeA(vd);
		m_sel->SetVolume(vd);
		if (m_brush_dlg) m_brush_dlg->GetSettings();
		if (m_measure_dlg && m_measure_dlg->isVisible()) m_measure_dlg->GetSettings();
		for (QDialog* d : { (QDialog*)m_nr_dlg, (QDialog*)m_count_dlg, (QDialog*)m_convert_dlg })
			if (d && d->isVisible()) QMetaObject::invokeMethod(d, "GetSettings");
		if (m_coloc_dlg && m_coloc_dlg->isVisible()) m_coloc_dlg->GetSettings();
		if (m_info_dlg) m_info_dlg->SetData(vd);
		// wx UpdateTree / OnSelChanged: GetTraceDlg()->GetSettings(vrv)
		if (m_trace_dlg) m_trace_dlg->GetSettings(m_canvas);
	}

	// OnSelection: the view (vrv) is null for the root / mesh group
	// (OnSelection(0)) and for a cleared selection
	const bool has_view = type >= 1 && type <= 5;
	if (has_view)
		m_adjust->SetRenderCanvas(m_canvas);
	if (!has_view || vd)
		m_adjust->SetVolume(vd);

	Annotations* ann = type == 4 ? m_tree->SelectedAnnotations() : nullptr;
	if (type == 2 && vd)
		m_clip->SetVolume(vd);
	else if (type == 3 && md)
		m_clip->SetMeshData(md);
	else if (type == 4 && ann)
	{
		// wx: the clipping view follows the annotation's volume / mesh
		VolumeData* vd_ann = ann->GetVolume();
		if (vd_ann) m_clip->SetVolume(vd_ann);
		MeshData* md_ann = ann->GetMesh();
		if (md_ann) m_clip->SetMeshData(md_ann);
		if (!vd_ann && !md_ann)
			m_clip->ClearData();
	}

	// the previously selected mesh loses its bounding box
	m_canvas->SetCurrentMesh(type == 3 ? md : nullptr);

	// wx OnSelection: m_cur_sel_type, m_cur_sel_vol (a shown volume),
	// m_cur_sel_mesh (DataManager indices; saved in /prop_panel)
	m_cur_sel_type = type;
	if (type == 2 && vd && vd->GetDisp())
		m_cur_sel_vol = m_data_mgr->GetVolumeIndex(vd->GetName());
	if (type == 3 && md)
		m_cur_sel_mesh = m_data_mgr->GetMeshIndex(md->GetName());

	auto hide_props = [this]() {
		m_prop_stack->setCurrentWidget(m_prop_empty);
		m_prop_dock->setWindowTitle(tr("Properties"));
	};
	switch (type)
	{
	case 2:  //volume
		if (vd && vd->GetDisp())
		{
			m_view->SetCurrentVolume(vd);   // wx: SetCurrentVolume on every view
			m_prop->SetGroupVolumes({});
			m_prop->SetGroup(group);
			m_prop->SetVolume(vd);
			m_prop_stack->setCurrentWidget(m_prop);
			m_prop_dock->setWindowTitle(tr("Properties - %1").arg(vd->GetName().ToQt()));
		}
		else
			hide_props();
		break;
	case 3:  //mesh
		if (md)
		{
			m_mesh_prop->SetMeshData(md, m_canvas);
			m_prop_stack->setCurrentWidget(m_mesh_prop);
			m_prop_dock->setWindowTitle(tr("Properties - %1").arg(md->GetName().ToQt()));
		}
		else
			hide_props();
		break;
	case 4:  //annotations
		if (ann)
		{
			m_ann_prop->SetAnnotations(ann);
			m_prop_stack->setCurrentWidget(m_ann_prop);
			m_prop_dock->setWindowTitle(tr("Properties - %1").arg(ann->GetName().ToQt()));
		}
		else
			hide_props();
		break;
	case 5:  //group
		m_adjust->SetGroup(group);
		hide_props();
		break;
	default:  // root, view, mesh group, none
		hide_props();
		break;
	}

	if (has_view && m_canvas->GetVolMethod() == RenderCanvas::VolMethodMulti)
		m_adjust->SetRenderCanvas(m_canvas);
}

// wx VRenderFrame::OnSelection(6, 0, 0, 0, md) (DataTreeCtrl::OnManipulateData)
void MainWindow::ShowMeshManip(MeshData* md)
{
	// no view: Output Adjustments lose their volume (wx SetVolumeData(0))
	m_adjust->SetVolume(nullptr);
	m_cur_sel_type = 6;
	// the previously selected mesh loses its bounding box
	m_canvas->SetCurrentMesh(nullptr);
	if (md)
	{
		m_mesh_manip->SetMeshData(md);
		m_mesh_manip->GetData();
		m_prop_stack->setCurrentWidget(m_mesh_manip);
		m_prop_dock->setWindowTitle(tr("Manipulations - %1").arg(md->GetName().ToQt()));
	}
	m_canvas->RefreshView();
}

void MainWindow::OnTreeTool(int which)
{
	// wx DataTreeCtrl::OnEdit / OnInfo / OnNoiseCancelling / OnCounting /
	// OnColocalization / OnConvert -> the Analyze menu entries
	static const char* kNames[] = { "Analyze...", "Infomation...", "Noise Reduction...",
		"Counting and Volume...", "Colocalization Analysis...", "Convert...", "Components && Tracking..." };
	if (which < 0 || which > 6 || !m_analysis_menu) return;
	for (QAction* a : m_analysis_menu->actions())
		if (a->text() == tr(kNames[which])) { a->trigger(); return; }
}

void MainWindow::ForgetVolume(VolumeData* vd)
{
	if (!vd) return;
	if (m_info_dlg) m_info_dlg->SetData(nullptr);
	if (m_adjust->GetVolume() == vd) m_adjust->SetVolume(nullptr);
	for (const auto& v : m_views)
	{
		if (v->sel->GetVolume() == vd) v->sel->SetVolume(nullptr);
		if (v->calc->GetVolumeA() == vd) v->calc->SetVolumeA(nullptr);
		if (v->calc->GetVolumeB() == vd) v->calc->SetVolumeB(nullptr);
		if (v->calc->GetVolumeC() == vd) v->calc->SetVolumeC(nullptr);
		v->canvas->RemoveVolume(vd);
	}
	if (m_trace_dlg) m_trace_dlg->VolumeRemoved(vd);
	if (m_prop->GetVolume() == vd) { m_prop->SetVolume(nullptr); m_view->SetCurrentVolume(nullptr); }
	if (m_clip->GetVolume() == vd) m_clip->SetVolume(nullptr);
	m_data_mgr->RemoveVolumeData(vd->GetName());
}

void MainWindow::OnVolumeRemove(VolumeData* vd)
{
	if (!vd) return;
	ForgetVolume(vd);
	RefreshTree();
	m_canvas->RefreshView();
}

void MainWindow::OnAbout()
{
	QMessageBox::about(this, tr("About VVDViewer"),
		tr("<b>%1 %2.%3</b> (Qt front end, phase 1)<br>Voxel Volume Data Viewer<br>"
		   "Janelia Research Campus / Scientific Computing<br><br>"
		   "Rendering: Vulkan (FLIVR engine, shared with the wxWidgets build).")
		.arg(FLUORENDER_TITLE).arg(VERSION_MAJOR).arg(VERSION_MINOR));
}

// ---------------------------------------------------------------------------
// drag & drop
// ---------------------------------------------------------------------------

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
	if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
	e->acceptProposedAction();
	OpenDroppedUrls(e->mimeData()->urls());
}

namespace
{
	// DragDrop.cpp: the suffixes DnDFile::OnDropFiles loads as volumes / meshes
	bool DropVolumeSuffix(const QString& s)
	{
		return s == ".nrrd" || s == ".tif" || s == ".tiff" || s == ".oib" || s == ".oif" || s == ".lsm" ||
			s == ".czi" || s == ".xml" || s == ".vvd" || s == ".h5j" ||
#ifndef _DARWIN
			s == ".nd2" ||
#endif
			s == ".v3dpbd" || s == ".zip" || s == ".idi" || s == ".zarr" || s == ".zgroup" || s == ".zfs_ch" ||
			s == ".n5" || s == ".json" || s == ".n5fs_ch";
	}
	bool DropMeshSuffix(const QString& s)
	{
		return s == ".obj" || s == ".swc" || s == ".ply" || s == ".csv" || s == ".nml";
	}
	// wxString::Mid(Find('.', true)).MakeLower() (the whole name when there is no dot)
	QString DropSuffix(const QString& f)
	{
		const int d = f.lastIndexOf('.');
		return (d >= 0 ? f.mid(d) : f).toLower();
	}
}

// wx DnDFile::OnDropFiles (DragDrop.cpp). Dropped on the window or on the
// render view (on_view: the wx DnDFile of a VRenderView). A .vrp as the first
// file opens that project only, also when dropped on a render view (wx
// refuses a .vrp dropped on a view with "For project files, drag and drop to
// regions other than render views."; the port opens it as a drop on the
// frame does); a .vrp that is not the first file is ignored.
// Files are sorted into volumes and meshes by suffix (others are ignored); a
// folder is expanded: its files by suffix, or, when it has an "s<N>"
// subfolder with attributes.json, the folder itself as an N5 dataset
// (<folder>.n5fs_ch); the meshes of a folder go into a mesh group named after
// the folder. Each batch is sorted. Loading runs after the drop returns so
// the system drag session finishes first (a long load inside the drop handler
// stalls Finder's drag on macOS).
void MainWindow::OpenDroppedUrls(const QList<QUrl>& urls, bool on_view, RenderCanvas* target)
{
	QStringList filenames;
	for (const QUrl& u : urls)
	{
		QString p = u.toLocalFile();
		if (p.isEmpty()) continue;
		while (p.size() > 1 && p.endsWith('/')) p.chop(1);   // folders arrive with a trailing slash
		filenames << p;
	}
	if (filenames.isEmpty()) return;
	if (DropSuffix(filenames.first()) == ".vrp")
	{
		const QString prj = filenames.first();
		QTimer::singleShot(0, this, [this, prj]() { OpenProject(prj); });
		return;
	}
	// the load steps in wx order: (volumes, meshes, mesh group name)
	struct Batch { QStringList volumes, meshes; QString group; };
	std::vector<Batch> steps;
	QStringList volumes, meshes;
	auto flush = [&](const QString& group) {
		if (volumes.isEmpty() && meshes.isEmpty()) return;
		volumes.sort();
		meshes.sort();
		steps.push_back({ volumes, meshes, group });
		volumes.clear();
		meshes.clear();
	};
	for (const QString& filename : filenames)
	{
		const QString suffix = DropSuffix(filename);
		const QFileInfo fi(filename);
		if (suffix == ".vrp")
		{
			// wx: remembered, never opened (only a first .vrp is)
		}
		else if (DropVolumeSuffix(suffix))
			volumes << filename;
		else if (DropMeshSuffix(suffix))
			meshes << filename;
		else if (fi.isDir())
		{
			flush(QString());
			QStringList flatfns;
			static const QRegularExpression scdir("^s[0-9]+$");
			QDirIterator it(filename, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
			while (it.hasNext())
			{
				const QString full = it.next();
				const QFileInfo e = it.fileInfo();
				if (e.isDir())
				{
					if (scdir.match(e.fileName()).hasMatch() && QFileInfo::exists(full + "/attributes.json"))
					{
						flatfns << filename + ".n5fs_ch";
						break;
					}
				}
				else if (e.isFile())
					flatfns << full;
			}
			if (flatfns.isEmpty())
				continue;
			for (const QString& fn : flatfns)
			{
				const QString s2 = DropSuffix(fn);
				if (DropVolumeSuffix(s2)) volumes << fn;
				else if (DropMeshSuffix(s2)) meshes << fn;
			}
			// wxFileName(folder).GetName()
			flush(fi.completeBaseName());
		}
	}
	flush(QString());
	// wx DnDFile(frame, vrv): a drop on a view loads into it, elsewhere into view 0
	QPointer<RenderCanvas> tgt(on_view ? target : nullptr);
	QTimer::singleShot(0, this, [this, steps, tgt]() {
		RenderCanvas* t = tgt.data();
		for (const Batch& b : steps)
		{
			if (!b.volumes.isEmpty()) LoadVolumes(b.volumes, QStringList(), t);
			if (!b.meshes.isEmpty()) LoadMeshFiles(b.meshes, b.group, QStringList(), t);
		}
	});
}
