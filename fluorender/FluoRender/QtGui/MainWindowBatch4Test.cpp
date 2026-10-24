// Testing aid for batch 4a (several render views, Components & Tracking):
// VVD_QT_TEST=b4_<kind> (+ VVD_QT_GRAB, VVD_QT_TEST_DIR), "[b4] PASS/FAIL"
// lines on stderr, captures in the test directory, quits. See docs/QT_PORT.md.
//
//   b4_views     volA.nrrd volB.nrrd mesh.obj: a second view (CreateView),
//                volB dragged onto it in the tree, different cameras, the
//                active view (click / tree selection) and the panes that
//                follow it, linked rotations (Settings link), a capture per
//                view (v0.tif, v1.tif), the Record/Export view combo, a
//                2-view project (p2.vrp), the tree's "Close"
//   b4_viewsrt   p2.vrp on the command line: captures r0.tif / r1.tif and
//                the project saved again (VVD_QT_SAVE)
//   b4_track     tk_T1.nrrd with VVD_QT_TIME_SEQ=1 (3 time points, .lbl per
//                time point, moving blobs whose ids change): Components &
//                Tracking: Generate, the generated links, Save As / Load,
//                selection tools, f / l keys, selection_tracking to the next
//                time points (mapped cells), Unlink / Link / Excl. Link /
//                Isolate, Convert to Rulers, UniIDs, Compnts / Links / Save As,
//                Assign / Replace / Combine ID, auto ID after a brush stroke

#include "MainWindow.h"

#include <QCoreApplication>
#include <QApplication>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QImage>
#include <QDockWidget>
#include <QTabWidget>
#include <QLineEdit>
#include <QTreeWidget>
#include <QMenu>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QListWidget>
#include <QPushButton>
#include <QUrl>
#include <QProcess>
#include <functional>
#include <memory>
#include <vector>
#include <set>
#include <map>

#include <glm/gtc/matrix_transform.hpp>

#include "RenderCanvas.h"
#include "ViewPanel.h"
#include "LegendMenu.h"
#include "ViewOverlays.h"
#include "TreePanel.h"
#include "ClipPanel.h"
#include "AdjustPanel.h"
#include "VolumePropPanel.h"
#include "MoviePanel.h"
#include "RulerTool.h"
#include "BrushTool.h"
#include "MeasureDialog.h"
#include "BrushToolDialog.h"
#include "TraceDialog.h"
#include "DataManager.h"
#include "Core/FileUtil.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/Texture.h"
#include "Formats/lbl_reader.h"

namespace
{
	// a chain of steps; a step calls next() when it is done
	struct Seq
	{
		std::vector<std::function<void(std::function<void()>)>> steps;
		size_t i = 0;
		void add(std::function<void(std::function<void()>)> f) { steps.push_back(std::move(f)); }
		void run(QObject* ctx, const std::shared_ptr<Seq>& self, int delay = 0)
		{
			QTimer::singleShot(delay, ctx, [ctx, self]() {
				if (self->i >= self->steps.size()) return;
				auto f = self->steps[self->i++];
				f([ctx, self]() { self->run(ctx, self, 50); });
			});
		}
	};
	long long CountVal(const unsigned int* p, long long n, unsigned int v) { long long c = 0; for (long long i = 0; i < n; i++) c += p[i] == v; return c; }
	long long CountNZ8(const unsigned char* p, long long n) { long long c = 0; for (long long i = 0; i < n; i++) c += p[i] != 0; return c; }
}

void MainWindow::RunBatch4Test(const QString& kind)
{
	const QString dir = qEnvironmentVariableIsSet("VVD_QT_TEST_DIR") ? qEnvironmentVariable("VVD_QT_TEST_DIR") :
		QFileInfo(qEnvironmentVariable("VVD_QT_GRAB")).absolutePath();
	QDir().mkpath(dir);
	auto fails = std::make_shared<int>(0);
	auto log = [](const QString& s) { fprintf(stderr, "[b4] %s\n", s.toLocal8Bit().constData()); };
	auto check = [fails, log](bool ok, const QString& what) {
		if (!ok) (*fails)++;
		log(QString(ok ? "PASS " : "FAIL ") + what);
	};
	auto seq = std::make_shared<Seq>();
	auto wait = [seq](int ms) { seq->add([ms](std::function<void()> next) { QTimer::singleShot(ms, next); }); };
	auto step = [seq](std::function<void()> f) { seq->add([f](std::function<void()> next) { f(); next(); }); };
	// capture of view i (the view's Capture button without its dialog)
	auto capture = [this, seq, log](int vi, const QString& file) {
		seq->add([this, vi, file, log](std::function<void()> next) {
			RenderCanvas* c = GetViewCanvas(vi);
			ViewPanel* vp = GetViewPanel(vi);
			if (!c || !vp) { log("no view for capture"); next(); return; }
			auto conn = std::make_shared<QMetaObject::Connection>();
			*conn = connect(c, &RenderCanvas::captureFinished, this, [conn, file, next, log](const QString& f, bool ok) {
				QObject::disconnect(*conn);
				log(QString("capture %1: %2").arg(f, ok ? "ok" : "FAILED"));
				QTimer::singleShot(300, next);
			});
			CaptureDialog::Options opt;
			const QSize s = c->GetDeviceSize();
			opt.resx = s.width(); opt.resy = s.height();
			opt.compress = false;
			vp->Capture(file, opt);
		});
	};
	auto finish = [seq, fails, log, this, dir]() {
		seq->add([fails, log, this, dir](std::function<void()>) {
			grab().save(dir + "/window_end.png");
			log(QString("done: %1 failure(s)").arg(*fails));
			QCoreApplication::quit();
		});
	};
	auto rot_eq = [](RenderCanvas* c, double x, double y, double z) {
		double rx, ry, rz; c->GetRotations(rx, ry, rz);
		return std::fabs(rx - x) < 1e-6 && std::fabs(ry - y) < 1e-6 && std::fabs(rz - z) < 1e-6;
	};

	if (kind == "b4_views")
	{
		VolumeData* volB = m_data_mgr->GetVolumeData(VVDString(L"volB.nrrd"));
		VolumeData* volA = m_data_mgr->GetVolumeData(VVDString(L"volA.nrrd"));
		step([=]() {
			check(GetViewNum() == 1 && volA && volB, "start: one view, volA + volB loaded");
			const QString name = CreateView();
			check(name == "Render View:2" && GetViewNum() == 2, "CreateView -> " + name);
			check(m_tree->FindItem("Render View:2", TreePanel::TypeView) != nullptr, "tree shows Render View:2");
			check(m_movie->ViewCount() == 2, "Record/Export view combo has 2 views");
		});
		wait(1500);
		step([=]() {
			check(GetViewCanvas(1)->IsVulkanReady() && GetViewCanvas(1)->IsSecondarySurface() &&
				GetViewCanvas(1)->GetVulkan() == GetViewCanvas(0)->GetVulkan(), "view 2 shares view 1's Vulkan context (own surface)");
		});
		step([=]() {
			// wx tree drag: within a view only; Qt: a volume dropped on another view moves there
			QTreeWidgetItem* src = m_tree->FindItem("volB.nrrd", TreePanel::TypeVolume);
			QTreeWidgetItem* dst = m_tree->FindItem("Render View:2", TreePanel::TypeView);
			check(src && dst && m_tree->SimulateDrag(src, dst, false), "drag volB onto Render View:2");
		});
		wait(800);
		step([=]() {
			const auto& v0 = GetViewCanvas(0)->Volumes();
			const auto& v1 = GetViewCanvas(1)->Volumes();
			check(std::find(v1.begin(), v1.end(), volB) != v1.end() && std::find(v0.begin(), v0.end(), volB) == v0.end(),
				QString("volB moved to view 2 (view 1: %1 volumes, view 2: %2)").arg(v0.size()).arg(v1.size()));
			check(GetViewCanvas(1)->GetLayerNum() >= 1 && GetViewCanvas(1)->GetGroupNum() == 1, "view 2: volB in a group of its own (OrganizeLayers)");
			// in-view drag (wx MoveLayertoView: out of the group to the view level)
			QTreeWidgetItem* srcA = m_tree->FindItem("volA.nrrd", TreePanel::TypeVolume);
			check(srcA && srcA->parent() && TreePanel::ItemType(srcA->parent()) == TreePanel::TypeGroup, "volA in a group of view 1");
			// linked rotations (Settings "Link all rendering views' rotations")
			GetViewCanvas(0)->LinkAllRotationsFromSettings(true);
			GetViewCanvas(0)->SetRotations(10, 20, 30);
			check(rot_eq(GetViewCanvas(1), 10, 20, 30), "linked: view 2 follows view 1's rotation (10, 20, 30)");
			GetViewCanvas(1)->SetRotations(40, 50, 60);
			check(rot_eq(GetViewCanvas(0), 40, 50, 60), "linked: view 1 follows view 2's rotation (40, 50, 60)");
			GetViewCanvas(0)->LinkAllRotationsFromSettings(false);
			GetViewCanvas(1)->SetLinkedRot(false);
			GetViewCanvas(1)->SetRotations(0, 90, 0);
			check(rot_eq(GetViewCanvas(0), 40, 50, 60), "unlinked: view 1 keeps its rotation");
			// different cameras
			GetViewCanvas(0)->SetRotations(30, 40, 0);
			GetViewCanvas(1)->SetRotations(0, 90, 0);
			GetViewCanvas(1)->SetScaleFactor(1.6);
			GetViewCanvas(1)->SetBackgroundColor(FLIVR::Color(0.1, 0.1, 0.25));
			check(!rot_eq(GetViewCanvas(1), 30, 40, 0), "views have different cameras");
			// active view: a click in view 2
			RenderCanvas* c1 = GetViewCanvas(1);
			const QPoint p(20, 20);
			c1->SimulateMouse(QEvent::MouseButtonPress, p, Qt::RightButton, Qt::RightButton);
			c1->SimulateMouse(QEvent::MouseButtonRelease, p, Qt::RightButton, Qt::NoButton);
			if (m_view_ctx_menu) m_view_ctx_menu->hide();
			check(ActiveView() == 1 && Canvas() == c1, "click in view 2: it is the active view");
			check(m_clip->GetRenderCanvas() == c1 && m_adjust->GetRenderCanvas() == c1, "Clipping Planes / Output Adjustments follow view 2");
			check(m_brush_dlg->GetTool() == GetViewBrushTool(1), "brush dialog follows view 2's brush");
			check(m_trace_dlg->GetView() == c1, "Components & Tracking follows view 2");
			EnsureMeasureDialog();
			check(m_measure_dlg->GetTool() == GetViewRulerTool(1), "Measurement follows view 2's rulers");
			check(m_sel != &m_selector, "view 2 has its own selector");
			// tree selection of an item of view 1
			m_tree->SelectVolume(volA);
			check(ActiveView() == 0 && m_prop->GetVolume() == volA, "tree selection of volA: view 1 active, Properties show volA");
			m_tree->SelectVolume(volB);
			check(ActiveView() == 1 && m_prop->GetVolume() == volB, "tree selection of volB: view 2 active");
			// a ruler in view 2 only
			Ruler* r = new Ruler();
			r->SetRulerType(0);
			r->AddPoint(FLIVR::Point(1, 2, 3));
			r->AddPoint(FLIVR::Point(10, 20, 30));
			r->SetName(VVDString(L"view2ruler"));
			GetViewRulerTool(1)->GetRulerList()->push_back(r);
			GetViewRulerTool(1)->RulersModified();
			check(GetViewRulerTool(0)->GetRulerList()->empty(), "view 1 has no rulers, view 2 has one");
			// Record/Export: the capture view
			m_movie->SetView(1);
			check(m_movie->GetRenderCanvas() == c1, "Record/Export records view 2 when selected");
			m_movie->SetView(0);
			check(m_movie->GetRenderCanvas() == GetViewCanvas(0), "Record/Export back on view 1");
			grab().save(dir + "/window_2views.png");
		});
		wait(1500);
		capture(0, dir + "/v0.tif");
		capture(1, dir + "/v1.tif");
		step([=]() {
			check(SaveProject(dir + "/p2.vrp"), "2-view project saved");
			vvd::IniFile ini(VVDString((dir + "/p2.vrp").toStdWString()));
			check(ini.ReadLong(L"/views/num") == 2, "/views/num = 2");
			VVDString n;
			ini.Read(L"/views/1/layers/0/name", n);
			check(QString::fromStdWString(n).startsWith("Group") || n == L"volB.nrrd", "/views/1/layers/0 = " + QString::fromStdWString(n));
			check(ini.Has(L"/views/1/properties/rotation") && ini.Has(L"/views/1/rulers/0/name"), "/views/1/properties and /views/1/rulers written");
		});
		step([=]() {
			// wx DataTreeCtrl::OnCloseView: "Close" on Render View:2 (none on Render View:1)
			QTreeWidgetItem* it0 = m_tree->FindItem("Render View:1", TreePanel::TypeView);
			std::unique_ptr<QMenu> m0(m_tree->BuildContextMenu(it0));
			bool has_close0 = false;
			for (QAction* a : m0->actions()) if (a->text() == "Close") has_close0 = true;
			check(!has_close0, "no Close entry for Render View:1");
			QTreeWidgetItem* it = m_tree->FindItem("Render View:2", TreePanel::TypeView);
			m_tree->Tree()->setCurrentItem(it);
			std::unique_ptr<QMenu> m(m_tree->BuildContextMenu(it));
			QAction* close = nullptr;
			for (QAction* a : m->actions()) if (a->text() == "Close") close = a;
			check(close != nullptr, "Close entry for Render View:2");
			if (close) close->trigger();
		});
		wait(800);
		step([=]() {
			check(GetViewNum() == 1 && m_movie->ViewCount() == 1 && !m_tree->FindItem("Render View:2", TreePanel::TypeView),
				"view 2 closed: one view, movie list and tree updated");
			check(m_data_mgr->GetVolumeData(VVDString(L"volB.nrrd")) != nullptr, "volB stays in the data manager (wx)");
			check(ActiveView() == 0 && Canvas() == GetViewCanvas(0), "view 1 active again");
		});
		finish();
	}
	else if (kind == "b4_viewsrt")
	{
		wait(1500);
		step([=]() {
			check(GetViewNum() == 2, QString("project opened with %1 views").arg(GetViewNum()));
			if (GetViewNum() < 2) return;
			check(GetViewCanvas(1)->GetViewName() == "Render View:2", "second view named Render View:2");
			check(GetViewRulerTool(1)->GetRulerList()->size() == 1 && GetViewRulerTool(0)->GetRulerList()->empty(), "rulers back in their views");
			double rx, ry, rz; GetViewCanvas(1)->GetRotations(rx, ry, rz);
			log(QString("view 2 rotation %1 %2 %3, zoom %4").arg(rx).arg(ry).arg(rz).arg(GetViewCanvas(1)->GetScaleFactor()));
			grab().save(dir + "/window_reopen.png");
		});
		wait(1500);
		capture(0, dir + "/r0.tif");
		capture(1, dir + "/r1.tif");
		step([=]() {
			const QString f = qEnvironmentVariable("VVD_QT_SAVE");
			if (!f.isEmpty()) check(SaveProject(f), "saved again: " + f);
		});
		finish();
	}
	else if (kind == "b4_track")
	{
		struct St { VolumeData* vd = nullptr; long long n = 0; int nx = 0, ny = 0, nz = 0; size_t rulers0 = 0; };
		auto st = std::make_shared<St>();
		auto label = [st]() -> unsigned int* {
			FLIVR::Texture* tex = st->vd ? st->vd->GetTexture() : nullptr;
			auto l = tex ? tex->get_nrrd(tex->nlabel()) : nullptr;
			return (l && l->getNrrd()) ? (unsigned int*)l->getNrrd()->data : nullptr;
		};
		auto mask = [st]() -> unsigned char* {
			auto m = st->vd ? st->vd->GetMask(true) : nullptr;
			return (m && m->getNrrd()) ? (unsigned char*)m->getNrrd()->data : nullptr;
		};
		// mask voxels whose label is v (all set voxels when v == 0)
		auto mask_of = [st, label, mask](unsigned int v) {
			unsigned int* l = label(); unsigned char* m = mask();
			if (!l || !m) return -1LL;
			long long c = 0;
			for (long long i = 0; i < st->n; i++) if (m[i] && (v == 0 || l[i] == v)) c++;
			return c;
		};
		auto ids_curr = [this]() {
			QStringList s;
			for (int i = 0; i < m_trace_dlg->ListCurr()->ItemCount(); i++) s << m_trace_dlg->ListCurr()->GetText(i, 1);
			return s.join(",");
		};
		auto ids_prev = [this]() {
			QStringList s;
			for (int i = 0; i < m_trace_dlg->ListPrev()->ItemCount(); i++) s << m_trace_dlg->ListPrev()->GetText(i, 1);
			return s.join(",");
		};
		auto mapped = [this](unsigned int id, size_t f1, size_t f2) -> unsigned int {
			TraceGroup* tg = m_canvas->GetTraceGroup();
			if (!tg) return 0;
			FL::TrackMapProcessor p;
			unsigned int out = 0;
			if (!p.GetMappedID(tg->GetTrackMap(), id, out, f1, f2)) return 0;
			return out;
		};
		// wx 4D script task selection_tracking at the new time point
		auto track_to_next = [this]() {
			m_trace_dlg->OnCellNext();   // wx TraceDlg "Forward >" = VMovieView::UpFrame
			vvd::IniFile ini;
			ini.Write(L"/tasks/task0/size_limit", 0L);
			m_script_vol = m_canvas->GetCurrentVolume();
			RunSelectionTracking(ini, VVDString(L"/tasks/task0/"));
			m_script_vol = nullptr;
			if (m_canvas->GetCurrentVolume() && m_canvas->GetCurrentVolume()->GetVR())
				m_canvas->GetCurrentVolume()->GetVR()->clear_tex_current();
			m_canvas->RefreshView();
		};
		auto key = [this](int k) {
			// wx OnIdle ignores the keys while a text control has the focus
			if (QWidget* f = QApplication::focusWidget()) f->clearFocus();
			QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier, QKeySequence(k).toString().toLower());
			QKeyEvent release(QEvent::KeyRelease, k, Qt::NoModifier, QKeySequence(k).toString().toLower());
			QApplication::sendEvent(m_canvas, &press);
			QApplication::sendEvent(m_canvas, &release);
		};
		step([=]() {
			st->vd = m_canvas->GetCurrentVolume();
			if (st->vd) { st->vd->GetResolution(st->nx, st->ny, st->nz); st->n = (long long)st->nx * st->ny * st->nz; }
			check(st->vd && st->vd->GetReader() && st->vd->GetReader()->GetTimeNum() == 3, "tk_T1.nrrd opened as a 3 time point series");
			check(label() != nullptr, "its label (tk_T1.lbl) loaded with it");
			check(m_trace_dlg && !m_trace_dlg->isVisible(), "Components & Tracking exists hidden (wx: no menu entry shows it)");
			ShowTraceDlg();
			check(m_trace_dlg->isVisible() && m_trace_dlg->TrackMapText() == "No Track map", "ShowTraceDlg: shown, \"No Track map\"");
		});
		wait(500);
		step([=]() {
			m_trace_dlg->grab().save(dir + "/trace_start.png");
			m_trace_dlg->OnGenMapBtn();
			TraceGroup* tg = m_canvas->GetTraceGroup();
			check(tg && tg->GetTrackMap().GetFrameNum() == 3, QString("Generate: track map with %1 frames").arg(tg ? (int)tg->GetTrackMap().GetFrameNum() : -1));
			check(m_trace_dlg->StatText().contains("Time point 2 initialized.") && m_trace_dlg->StatText().contains("Wall clock time"), "Generate output text");
			check(mapped(1, 0, 1) == 11 && mapped(2, 0, 1) == 13 && mapped(3, 0, 1) == 12, QString("links T1->T2: 1->%1 2->%2 3->%3 (expected 11 13 12)").arg(mapped(1, 0, 1)).arg(mapped(2, 0, 1)).arg(mapped(3, 0, 1)));
			check(mapped(11, 1, 2) == 21 && mapped(13, 1, 2) == 22 && mapped(12, 1, 2) == 23, QString("links T2->T3: 11->%1 13->%2 12->%3 (expected 21 22 23)").arg(mapped(11, 1, 2)).arg(mapped(13, 1, 2)).arg(mapped(12, 1, 2)));
			check(m_trace_dlg->TrackMapText() == "Track map created but not saved", "track map text after Generate");
			m_trace_dlg->grab().save(dir + "/trace_genmap.png");
			// Refine T / Refine All keep the links of this clean series
			m_trace_dlg->OnRefineAllBtn();
			check(mapped(1, 0, 1) == 11 && mapped(11, 1, 2) == 21, "Refine All: links kept");
			// Save As / Load
			const QString tf = dir + "/t.track";
			m_trace_dlg->OnSaveasTrace(tf);
			check(QFileInfo(tf).size() > 0 && m_trace_dlg->TrackMapText() == tf, "Save As " + tf);
			m_trace_dlg->OnLoadTrace(tf);
			tg = m_canvas->GetTraceGroup();
			check(tg && tg->GetTrackMap().GetFrameNum() == 3 && mapped(2, 0, 1) == 13, "Load: the saved map (3 frames, links)");
			m_trace_dlg->OnSaveTrace();   // existing path: saved in place
			// UniIDs (consistent ids across the time points)
			const QString ud = dir + "/uniids";
			QDir().mkpath(ud);
			m_trace_dlg->OnConvertConsistent(ud);
			bool uni_ok = m_trace_dlg->StatText().contains("All done.");
			std::vector<std::set<unsigned int>> per_blob(3);
			for (int t = 0; t < 3; t++)
			{
				LBLReader r;
				std::wstring f = (ud + QString("/tk_T%1.lbl").arg(t + 1)).toStdWString();
				r.SetFile(f);
				BaseReader* br = &r;
				auto n = br->Convert(t, 0, true);
				if (!n || !n->getNrrd()) { uni_ok = false; continue; }
				const unsigned int* d = (const unsigned int*)n->getNrrd()->data;
				// blob centres: (14+3t,14,10), (48-3t,16,10), (32,28+3t,10)
				const int cs[3][2] = { { 14 + 3 * t, 14 }, { 48 - 3 * t, 16 }, { 32, 28 + 3 * t } };
				for (int b = 0; b < 3; b++)
					per_blob[b].insert(d[(long long)st->nx * st->ny * 10 + (long long)st->nx * cs[b][1] + cs[b][0]]);
			}
			for (int b = 0; b < 3; b++) uni_ok = uni_ok && per_blob[b].size() == 1;
			check(uni_ok, QString("UniIDs: each blob keeps one id in all 3 written .lbl (blob ids %1 %2 %3)")
				.arg(*per_blob[0].begin()).arg(*per_blob[1].begin()).arg(*per_blob[2].begin()));
		});
		step([=]() {
			// Selection: ID text + Append (wx OnCompFull with a non-empty ID)
			m_trace_dlg->CompIDText()->setText("1");
			m_trace_dlg->OnCompFull();
			check(mask_of(1) == 895 && mask_of(0) == 895, QString("ID 1 + FullCompt/Append: mask = label 1 (%1 voxels)").arg(mask_of(0)));
			check(ids_curr() == "1" && m_trace_dlg->CurTime() == 0, "ID list (current T 0): " + ids_curr());
			// Replace with ID 2
			m_trace_dlg->CompIDText()->setText("2");
			m_trace_dlg->OnCompExclusive();
			check(mask_of(2) == 895 && mask_of(0) == 895, "Replace: mask = label 2 only");
			// Clear
			m_trace_dlg->OnCompClear();
			check(mask_of(0) == 0 && ids_curr().isEmpty(), "Clear: empty mask and list");
			// f key = CellFull: a few voxels of label 3 become the whole component (> 20 voxels)
			unsigned char* m = mask(); unsigned int* l = label();
			int set = 0;
			for (long long i = 0; i < st->n && set < 30; i++) if (l[i] == 3) { m[i] = 255; set++; }
			st->vd->GetVR()->clear_tex_current();
			m_trace_dlg->CompIDText()->clear();
			key(Qt::Key_F);
			check(mask_of(3) == 895 && mask_of(0) == 895, QString("f key (CellFull): 30 voxels -> label 3 component (%1)").arg(mask_of(0)));
			// size filter: 2000 > 895 -> CellFull deselects it
			m_trace_dlg->CellSizeText()->setText("2000");
			m_trace_dlg->CellFull();
			check(mask_of(0) == 0, "CellFull with component size 2000: deselected");
			m_trace_dlg->CellSizeText()->setText("20");
			// selection at T 0 for tracking: blob 1
			m_trace_dlg->CompIDText()->setText("1");
			m_trace_dlg->OnCompAppend(false);
			check(ids_curr() == "1", "T 0 selection: ID 1");
			m_trace_dlg->grab().save(dir + "/trace_sel.png");
		});
		step([=]() {
			track_to_next();
			check(m_canvas->GetTSeqCurNum() == 1, QString("Forward >: time point %1").arg(m_canvas->GetTSeqCurNum()));
			check(ids_curr() == "11" && ids_prev() == "1" && m_trace_dlg->CurTime() == 1 && m_trace_dlg->PrvTime() == 0,
				QString("selection_tracking: mapped cell 11 at T 1, previous 1 (lists %1 / %2, T %3 / %4)").arg(ids_curr(), ids_prev()).arg(m_trace_dlg->CurTime()).arg(m_trace_dlg->PrvTime()));
			check(mask_of(11) == 895 && mask_of(0) == 895, "mask at T 1 = label 11 (blob 1)");
			// Linkage
			m_trace_dlg->OnCellUnlink();
			check(mapped(1, 0, 1) == 0, QString("Unlink IDs: 1 (T 0) no longer linked (%1)").arg(mapped(1, 0, 1)));
			m_trace_dlg->OnCellLink();
			check(mapped(1, 0, 1) == 11, "Link IDs: 1 -> 11 again");
			m_trace_dlg->OnCellUnlink();
			key(Qt::Key_L);
			check(mapped(1, 0, 1) == 11, "l key (CellLink): 1 -> 11 again");
			m_trace_dlg->OnCellExclusiveLink();
			check(mapped(1, 0, 1) == 11 && mapped(11, 1, 0) == 1, "Excl. Link: 1 <-> 11");
			m_trace_dlg->OnCellIsolate();
			check(mapped(11, 1, 0) == 0 && mapped(11, 1, 2) == 0, "Isolate: 11 has no links");
			m_trace_dlg->OnCellLink();
			check(mapped(1, 0, 1) == 11, "Link IDs after Isolate: 1 -> 11");
			// the T1 -> T2 link was removed by Isolate; link 11 to the next time point again later
			m_trace_dlg->grab().save(dir + "/trace_link.png");
		});
		step([=]() {
			// T 2: select all cells, link the isolated 11 again, then rulers along the tracks
			track_to_next();
			check(m_canvas->GetTSeqCurNum() == 2, "time point 2");
			log("lists at T 2 after selection_tracking: " + ids_curr() + " / " + ids_prev());
			m_trace_dlg->CompIDText()->setText("21");
			m_trace_dlg->OnCompExclusive();   // curr {21}; prev stays (time unchanged)
			m_trace_dlg->ListPrev()->DeleteAllItems();
			m_trace_dlg->ListPrev()->Append("x", 11, Qt::white, 895, 17, 14, 10);
			m_trace_dlg->CellLink(false);
			check(mapped(11, 1, 2) == 21, "link 11 (T 1) -> 21 (T 2) through the previous list");
			m_trace_dlg->CompIDText()->setText("all");
			m_trace_dlg->OnCompAppend(false);
			check(mask_of(0) == 3 * 895, "Append \"all\": every labelled voxel");
			st->rulers0 = m_ruler_tool->GetRulerList()->size();
			m_trace_dlg->OnConvertToRulers();
			const auto* rl = m_ruler_tool->GetRulerList();
			const size_t added = rl->size() - st->rulers0;
			QStringList pts;
			for (size_t i = st->rulers0; i < rl->size(); i++)
			{
				Ruler* r = (*rl)[i];
				QStringList p;
				for (int k = 0; k < r->GetNumPoint(); k++) p << QString("(%1,%2,%3)").arg(r->GetPoint(k)->x(), 0, 'f', 1).arg(r->GetPoint(k)->y(), 0, 'f', 1).arg(r->GetPoint(k)->z(), 0, 'f', 1);
				pts << p.join("-");
			}
			// TrackMapProcessor::GetMappedRulers (shared Tracking/TrackMap.cpp) has
			// its whole body commented out, so wx's Convert to Rulers adds nothing
			check(added == 0, QString("Convert to Rulers: %1 rulers (as wx: GetMappedRulers is commented out in the engine) %2").arg(added).arg(pts.join("  ")));
			EnsureMeasureDialog()->show();
			m_measure_dlg->GetSettings();
			m_measure_dlg->grab().save(dir + "/measure_rulers.png");
			m_measure_dlg->hide();
			// Analysis
			m_trace_dlg->OnAnalyzeComp();
			check(m_trace_dlg->StatText().contains("A total of 3 component(s) selected"), "Compnts: 3 components");
			log("Compnts output:\n" + m_trace_dlg->StatText());
			m_trace_dlg->OnAnalyzeLink();
			check(m_trace_dlg->StatText().startsWith("Time point number: 3"), "Links: 3 time points");
			log("Links output:\n" + m_trace_dlg->StatText());
			const QString rf = dir + "/links.txt";
			m_trace_dlg->OnSaveResult(rf);
			QFile f(rf);
			check(f.open(QIODevice::ReadOnly) && QString(f.readAll()).contains("In Orphan"), "Save As: " + rf);
			m_trace_dlg->grab().save(dir + "/trace_analysis.png");
		});
		step([=]() {
			// Modify: Assign ID / Replace ID / Combine (labels written to the .lbl of the time point)
			m_trace_dlg->CompIDText()->setText("21");
			m_trace_dlg->OnCompExclusive();
			m_trace_dlg->CellNewIDText()->setText("50");
			m_trace_dlg->OnCellNewID();
			unsigned int* l = label();
			check(CountVal(l, st->n, 50) == 895 && CountVal(l, st->n, 21) == 0, "Assign ID 50: label 21 -> 50");
			LBLReader r;
			std::wstring lf = QString(QFileInfo(QString::fromStdWString(st->vd->GetReader()->GetCurName(2, 0))).absolutePath() + "/tk_T3.lbl").toStdWString();
			r.SetFile(lf);
			BaseReader* br = &r;
			auto n = br->Convert(2, 0, true);
			check(n && n->getNrrd() && CountVal((const unsigned int*)n->getNrrd()->data, st->n, 50) == 895, "tk_T3.lbl written with ID 50 (wx MSKWriter)");
			check(ids_curr() == "50", "ID list: 50");
			m_trace_dlg->CellNewIDText()->setText("60");
			m_trace_dlg->OnCellReplaceID();
			check(CountVal(label(), st->n, 60) == 895 && CountVal(label(), st->n, 50) == 0, "Replace ID 60: 50 -> 60");
			m_trace_dlg->CompIDText()->setText("all");
			m_trace_dlg->OnCompAppend(false);
			m_trace_dlg->OnCellCombineID();
			std::set<unsigned int> ids;
			for (long long i = 0; i < st->n; i++) if (label()[i]) ids.insert(label()[i]);
			check(ids.size() == 1 && CountVal(label(), st->n, *ids.begin()) == 3 * 895, QString("Combine: one id (%1) for the 3 components").arg(ids.empty() ? 0 : *ids.begin()));
			m_trace_dlg->OnCellDivideID();   // graph only (wx)
			// auto ID after a brush stroke (wx Segment -> CellNewID(true))
			m_trace_dlg->SetAutoID(!qEnvironmentVariableIsSet("VVD_QT_B4_NOAUTO"));
			check(m_trace_dlg->GetAutoID() && m_trace_dlg->StatText().contains("Auto ID assignment enabled"), "Auto Assign ID on");
			m_trace_dlg->OnCompClear();
			m_trace_dlg->CellNewIDText()->setText("77");
			// auto ID labels unlabelled painted voxels (labelled ones of another
			// id are dropped from the selection, wx): start from an empty label
			std::fill(label(), label() + st->n, 0u);
			st->vd->GetVR()->clear_tex_current();
		});
		wait(800);
		step([=]() {
			// a Highlight stroke across the view centre (the volume is centred)
			m_brush_tool->SelectBrushMode(2);
			m_canvas->RefreshView();
		});
		wait(800);
		seq->add([this, dir](std::function<void()> next) {
			auto conn = std::make_shared<QMetaObject::Connection>();
			*conn = connect(m_canvas, &RenderCanvas::frameCaptured, this, [conn, next](const QString&, bool) {
				QObject::disconnect(*conn);
				QTimer::singleShot(200, next);
			});
			m_canvas->CaptureToFile(dir + "/track_view_t3.png");
		});
		step([=]() {
			RenderCanvas* cv = m_canvas;
			const QSize s = cv->size();
			// across the brightest row of the presented view (the blobs)
			QImage img(dir + "/track_view_t3.png");
			int best_y = s.height() / 2, best = -1, x0 = 0, x1 = 0;
			for (int y = 0; y < img.height(); y++)
			{
				int n = 0, lo = img.width(), hi = -1;
				for (int x = 0; x < img.width(); x++)
					if (qGray(img.pixel(x, y)) > 60) { n++; lo = std::min(lo, x); hi = std::max(hi, x); }
				if (n > best) { best = n; best_y = y; x0 = lo; x1 = hi; }
			}
			const double sx = img.width() > 0 ? double(s.width()) / img.width() : 1.0;
			const double sy = img.height() > 0 ? double(s.height()) / img.height() : 1.0;
			const QPoint a(int(x0 * sx), int(best_y * sy)), b(int(x1 * sx), int(best_y * sy));
			log(QString("stroke %1,%2 -> %3,%4 in %5x%6, brush int mode %7").arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y()).arg(s.width()).arg(s.height()).arg(m_brush_tool->GetIntMode()));
			cv->SimulateMouse(QEvent::MouseMove, a, Qt::NoButton, Qt::NoButton);
			cv->SimulateMouse(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
			for (int i = 1; i <= 24; i++)
				cv->SimulateMouse(QEvent::MouseMove, a + (b - a) * i / 24, Qt::NoButton, Qt::LeftButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
		});
		wait(1500);
		step([=]() { m_brush_tool->SelectBrushMode(0); });
		step([=]() {
			const long long sel = mask_of(0);
			const long long n77 = CountVal(label(), st->n, 77);
			check(sel > 0 && n77 > 0, QString("brush stroke with Auto ID: %1 voxels selected, %2 labelled 77").arg(sel).arg(n77));
			m_trace_dlg->SetAutoID(false);
			m_trace_dlg->grab().save(dir + "/trace_end.png");
			grab().save(dir + "/window_track.png");
		});
		finish();
	}
	else if (kind == "legend2")
	{
		// volA.nrrd on the command line; volB.nrrd, b1.nrrd, b2.nrrd next to it
		VolumeData* volA0 = m_canvas->Volumes().empty() ? nullptr : m_canvas->Volumes().front();
		const QString ddir = volA0 ? QFileInfo(volA0->GetPath().ToQt()).absolutePath() : QString();
		auto legends = [](RenderCanvas* c) {
			QString s;
			for (VolumeData* vd : c->PopVolumeList()) s += vd->GetLegend() ? '1' : '0';
			s += '|';
			for (MeshData* md : c->GetPopMeshList()) s += md->GetLegend() ? '1' : '0';
			return s;
		};
		auto menu_of = [this](int vi) { ViewPanel* vp = GetViewPanel(vi); return vp ? vp->GetLegendMenu() : nullptr; };
		// rows of the open menu: count, every row inside the list viewport and
		// the list inside the menu (not clipped by a stale menu size)
		auto rows_ok = [=](int vi, int expect, const QString& what) {
			LegendMenu* menu = menu_of(vi);
			QListWidget* l = menu ? menu->List() : nullptr;
			if (!l) { check(false, what + ": no menu"); return; }
			bool inside = true;
			const int rh = l->count() ? l->sizeHintForRow(0) : 0;
			for (int i = 0; i < l->count(); ++i)
				if (!l->viewport()->rect().contains(l->visualItemRect(l->item(i)))) inside = false;
			const int list_bottom = l->mapTo(menu, QPoint(0, l->height())).y();
			QStringList names;
			for (int i = 0; i < l->count(); ++i) names << l->item(i)->text();
			const QString f = dir + QString("/menu_%1.png").arg(QString(what).replace(' ', '_').replace('/', '_'));
			menu->grab().save(f);
			check(menu->isVisible() && l->count() == expect && inside && l->isVisible() && list_bottom <= menu->height() && l->height() >= expect * rh,
				QString("%1: menu open %2, %3 rows (expect %4) [%5], rows inside list %6, list %7x%8 (row %9) bottom %10 / menu height %11 -> %12")
				.arg(what).arg(menu->isVisible()).arg(l->count()).arg(expect).arg(names.join(", ")).arg(inside)
				.arg(l->width()).arg(l->height()).arg(rh).arg(list_bottom).arg(menu->height()).arg(f));
		};
		auto open_menu = [=](int vi) {
			seq->add([=](std::function<void()> next) {
				ViewPanel* vp = GetViewPanel(vi);
				next();                          // the following steps run inside the menu's loop
				if (vp) vp->LegendButton()->showMenu();   // modal until hide()
			});
			wait(600);
		};
		auto close_menu = [=](int vi) { step([=]() { if (LegendMenu* m = menu_of(vi)) m->hide(); }); wait(300); };
		auto click_row = [=](int vi, int row) {
			QListWidget* l = menu_of(vi)->List();
			if (row >= l->count()) return;
			const QPoint p = l->visualItemRect(l->item(row)).center();
			QMouseEvent press(QEvent::MouseButtonPress, p, l->viewport()->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
			QMouseEvent rel(QEvent::MouseButtonRelease, p, l->viewport()->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(l->viewport(), &press);
			QApplication::sendEvent(l->viewport(), &rel);
		};
		auto click_action = [=](int vi, QAction* a) {
			LegendMenu* menu = menu_of(vi);
			const QPoint p = menu->actionGeometry(a).center();
			QMouseEvent press(QEvent::MouseButtonPress, p, menu->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
			QMouseEvent rel(QEvent::MouseButtonRelease, p, menu->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(menu, &press);
			QApplication::sendEvent(menu, &rel);
		};
		// pixels that differ between two captures (whole image, bottom band
		// where the legend is drawn)
		auto diff = [](const QString& a, const QString& b, long long* band) {
			QImage A(a), B(b);
			*band = -1;
			if (A.isNull() || B.isNull() || A.size() != B.size()) return -1LL;
			long long n = 0, nb = 0;
			const int y0 = A.height() * 3 / 4;
			for (int y = 0; y < A.height(); ++y)
				for (int x = 0; x < A.width(); ++x)
					if (A.pixel(x, y) != B.pixel(x, y)) { ++n; if (y >= y0) ++nb; }
			*band = nb;
			return n;
		};
		auto base_file = [dir](int vi) { return dir + QString("/view%1_base.png").arg(vi); };
		// the canvas as on the screen (macOS screencapture: what the user sees,
		// not the swapchain image), in device pixels
		auto screen_grab = [this](int vi, const QString& file) {
			RenderCanvas* c = GetViewCanvas(vi);
			const QPoint g = c->mapToGlobal(QPoint(0, 0));
			QFile::remove(file);
			QProcess::execute("screencapture", { "-x", "-o", QString("-R%1,%2,%3,%4").arg(g.x()).arg(g.y()).arg(c->width()).arg(c->height()), file });
		};
		auto sbase_file = [dir](int vi) { return dir + QString("/screen%1_base.png").arg(vi); };
		// the action alone (no other refresh): a passive capture of the next
		// presented frame of view vi, compared with the view's base picture
		auto act = [=](int vi, const QString& name, std::function<void()> action, std::function<bool(long long, long long)> expect, const QString& what) {
			seq->add([=](std::function<void()> next) {
				RenderCanvas* c = GetViewCanvas(vi);
				const QString file = dir + "/" + name + ".png";
				QFile::remove(file);
				const long long f0 = c->PresentedFrames();
				// the other view must not be the one redrawn
				RenderCanvas* other = GetViewNum() > 1 ? GetViewCanvas(1 - vi) : nullptr;
				const long long fo = other ? other->PresentedFrames() : 0;
				auto done = std::make_shared<bool>(false);
				auto conn = std::make_shared<QMetaObject::Connection>();
				auto finish_one = [=](bool got) {
					if (*done) return;
					*done = true;
					QObject::disconnect(*conn);
					if (!got) c->CaptureNextFrame(QString());
					long long band = -1;
					const long long d = got ? diff(base_file(vi), file, &band) : -1;
					check(got && expect(d, band), QString("%1: frame presented after the action alone %2 (frames %3 -> %4), view %5 legends %6, "
						"pixels differing from base %7 (bottom band %8)%9 -> %10")
						.arg(what).arg(got).arg(f0).arg(c->PresentedFrames()).arg(vi + 1).arg(legends(c)).arg(d).arg(band)
						.arg(other ? QString(", other view frames %1 -> %2").arg(fo).arg(other->PresentedFrames()) : QString())
						.arg(file));
					// and on the screen, 300 ms later
					QTimer::singleShot(300, this, [=]() {
						const QString sf = dir + "/" + name + "_screen.png";
						screen_grab(vi, sf);
						long long sband = -1;
						const long long sd = diff(sbase_file(vi), sf, &sband);
						check(sd >= 0 && expect(sband, sband), QString("%1 on the screen: pixels differing from the screen base %2 (bottom band %3) -> %4")
							.arg(what).arg(sd).arg(sband).arg(sf));
						QTimer::singleShot(200, next);
					});
				};
				*conn = connect(c, &RenderCanvas::frameCaptured, this, [=](const QString&, bool ok) { finish_one(ok); });
				c->CaptureNextFrame(file);
				action();
				QTimer::singleShot(2500, this, [=]() { finish_one(false); });
			});
		};
		auto base = [=](int vi) {
			seq->add([=](std::function<void()> next) {
				RenderCanvas* c = GetViewCanvas(vi);
				auto conn = std::make_shared<QMetaObject::Connection>();
				*conn = connect(c, &RenderCanvas::frameCaptured, this, [=](const QString& f, bool ok) {
					QObject::disconnect(*conn);
					log(QString("base picture of view %1 (legends %2): %3 %4").arg(vi + 1).arg(legends(c)).arg(f).arg(ok ? "ok" : "FAILED"));
					QTimer::singleShot(300, this, [=]() { screen_grab(vi, sbase_file(vi)); next(); });
				});
				c->CaptureToFile(base_file(vi));
			});
		};
		auto changed = [](long long d, long long band) { return d > 0 && band > 0; };
		auto same = [](long long d, long long) { return d == 0; };

		step([=]() {
			check(volA0 != nullptr && !ddir.isEmpty(), "start: volA loaded from " + ddir);
			for (VolumeData* vd : m_canvas->Volumes())
				check(!vd->GetLegend(), QString("new volume %1: legend flag off (wx VolumeData ctor m_legend = false)").arg(vd->GetName().ToQt()));
			GetViewOverlays(0)->SetDrawLegend(true);
			GetViewPanel(0)->SyncOverlayChecks();
			raise();
			activateWindow();
		});
		wait(800);
		base(0);
		open_menu(0);
		step([=]() { rows_ok(0, 1, "volA open menu"); });
		close_menu(0);
		// (b) a second volume while running (Open Volume path)
		step([=]() { LoadVolumes({ ddir + "/volB.nrrd" }); });
		wait(1500);
		base(0);
		open_menu(0);
		step([=]() { rows_ok(0, 2, "volB loaded reopen menu"); });
		// (a) clicks with the menu open
		act(0, "click_row0_on", [=]() { click_row(0, 0); }, changed, "click row 0 (legend on)");
		act(0, "click_row0_off", [=]() { click_row(0, 0); }, same, "click row 0 again (legend off)");
		act(0, "click_row1_on", [=]() { click_row(0, 1); }, changed, "click row 1 (legend on)");
		act(0, "showall", [=]() { click_action(0, menu_of(0)->ShowAllAction()); }, changed, "Show All");
		act(0, "hideall", [=]() { click_action(0, menu_of(0)->HideAllAction()); }, same, "Hide All");
		step([=]() { check(menu_of(0)->isVisible(), "menu still open after the clicks"); });
		// a volume loaded while the menu is open (Qt: the open menu follows)
		step([=]() { LoadVolumes({ ddir + "/b1.nrrd" }); });
		wait(1500);
		step([=]() { rows_ok(0, 3, "b1 loaded while open"); });
		close_menu(0);
		// drag & drop onto the view
		step([=]() { check(m_canvas->SimulateDrop({ QUrl::fromLocalFile(ddir + "/b2.nrrd") }), "drop b2.nrrd on the view"); });
		wait(2000);
		open_menu(0);
		step([=]() { rows_ok(0, 4, "b2 dropped reopen menu"); });
		act(0, "showall4", [=]() { click_action(0, menu_of(0)->ShowAllAction()); }, changed, "Show All (4 volumes)");
		close_menu(0);
		// a second view; its own volume; its menu / canvas
		step([=]() {
			const QString name = CreateView();
			check(GetViewNum() == 2, "CreateView -> " + name);
		});
		wait(1500);
		step([=]() {
			LoadVolumes({ ddir + "/volA.nrrd" }, {}, GetViewCanvas(1));
			GetViewOverlays(1)->SetDrawLegend(true);
			GetViewPanel(1)->SyncOverlayChecks();
		});
		wait(1500);
		base(1);
		open_menu(1);
		step([=]() { rows_ok(1, 1, "view 2 menu"); });
		act(1, "view2_click_row0_on", [=]() { click_row(1, 0); }, changed, "view 2: click row 0 (legend on)");
		step([=]() {
			check(legends(GetViewCanvas(0)) == "1111|", "view 1 legends unchanged by view 2's menu: " + legends(GetViewCanvas(0)));
		});
		act(1, "view2_hideall", [=]() { click_action(1, menu_of(1)->HideAllAction()); }, same, "view 2: Hide All");
		close_menu(1);
		finish();
	}
	else
	{
		log("unknown test " + kind);
		QCoreApplication::quit();
		return;
	}
	seq->run(this, seq, 0);
}
