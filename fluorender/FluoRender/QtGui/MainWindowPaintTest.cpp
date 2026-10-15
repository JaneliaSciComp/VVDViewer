// Testing aid: scripted paint / selection / segmentation / view-menu / UI runs
// (VVD_QT_GRAB=<file> VVD_QT_TEST=paint|calc|seg|index|nr|hide|ctx|ui|view3d|na, see
// QtMain.cpp and the per-kind comments below).
//
//   paint  brush strokes on the current volume (Select, Diffuse, Unselect,
//          Solid), undo / redo, V-hold, Enhance selection, Mask Intensity,
//          mask hide modes, all three blend modes, Extract, Erase, Reset
//          All, component analysis + exports, noise analysis / removal.
//          Mask voxel counts are read back from the GPU after every step;
//          Extract / Erase results are checked voxel by voxel against
//          data * mask / 255 and data * (1 - mask / 255).
//   calc   the Calculations operators (VolumeCalculator types 1-4, 9) on the
//          first two volumes, every voxel checked against the shader formula.
//   seg    paints a selection, then presses the buttons of the Noise
//          Reduction, Counting and Volume, Colocalization and Convert dialogs
//          and prints the dialogs' result fields and the created data.
//
// Output: "[paint] ..." lines on stderr; captures (PNG, the presented 3D view)
// in VVD_QT_TEST_DIR (default: the directory of VVD_QT_GRAB).
// VVD_QT_PAINT_UNDO=<n>: paint history depth for the run (default 5;
// Settings > paint history is 0 by default, which disables undo).

#include "MainWindow.h"

#include <QTimer>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QDockWidget>
#include <QAction>
#include <QImage>
#include <QFileInfo>
#include <QSlider>
#include <QComboBox>
#include <QFile>
#include <QDir>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QDialog>
#include <QCoreApplication>
#include <QElapsedTimer>

#include <functional>
#include <algorithm>
#include <memory>
#include <deque>
#include <cmath>
#include <cstdio>

#include <QTreeWidget>
#include <unordered_set>
#include "VolumePropPanel.h"
#include "ClipPanel.h"
#include "ViewPanel.h"
#include "SettingsDialog.h"
#include <QApplication>
#include <QMessageBox>
#include <QKeyEvent>
#include <set>
#include "Core/FileUtil.h"
#include "RenderCanvas.h"
#include "TreePanel.h"
#include "BrushTool.h"
#include "RulerTool.h"
#include "BrushToolDialog.h"
#include "NoiseReductionDialog.h"
#include "CountingDialog.h"
#include "ColocalizationDialog.h"
#include "ConvertDialog.h"
#include "MeasureDialog.h"
#include "RigidCellsReportDialog.h"
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCryptographicHash>
#include "DataManager.h"
#include "FLIVR/Texture.h"
#include "FLIVR/VolumeRenderer.h"

namespace
{
	struct VoxStats
	{
		long long total = 0;
		long long nonzero = 0;
	};

	long long VoxelCount(VolumeData* vd)
	{
		int nx = 0, ny = 0, nz = 0;
		vd->GetResolution(nx, ny, nz);
		return (long long)nx * ny * nz;
	}

	// value of voxel i of a uchar / ushort / float nrrd (raw units)
	inline double Val(const Nrrd* n, long long i)
	{
		switch (n->type)
		{
		case nrrdTypeUChar: return ((const unsigned char*)n->data)[i];
		case nrrdTypeUShort: return ((const unsigned short*)n->data)[i];
		case nrrdTypeFloat: return ((const float*)n->data)[i];
		default: return 0.0;
		}
	}

	Nrrd* DataNrrd(VolumeData* vd)
	{
		if (!vd) return nullptr;
		std::shared_ptr<VL_Nrrd> v = vd->GetVolume(false);
		return v ? v->getNrrd() : nullptr;
	}

	Nrrd* MaskNrrd(VolumeData* vd, bool readback = true)
	{
		if (!vd) return nullptr;
		std::shared_ptr<VL_Nrrd> m = vd->GetMask(readback);
		return m ? m->getNrrd() : nullptr;
	}

	long long CountNonzero(const Nrrd* n, long long count)
	{
		if (!n || !n->data) return -1;
		long long c = 0;
		if (n->type == nrrdTypeUChar)
		{
			const unsigned char* p = (const unsigned char*)n->data;
			for (long long i = 0; i < count; i++) c += p[i] != 0;
		}
		else
			for (long long i = 0; i < count; i++) c += Val(n, i) != 0.0;
		return c;
	}
}

void MainWindow::RunPaintTest(const QString& kind)
{
	struct Step
	{
		QString name;
		std::function<void()> fn;
		bool settle = true;      // wait for a finished frame afterwards
	};
	struct State
	{
		std::deque<Step> steps;
		QString dir;
		QRect obj;               // object box in widget coordinates
		long long frames0 = 0;
		QElapsedTimer settle;
		bool waiting_capture = false;
		bool settling = false;
		int fails = 0;
		std::map<QString, long long> counts;
		VolumeData* src = nullptr;
	};
	auto st = std::make_shared<State>();
	st->dir = qEnvironmentVariable("VVD_QT_TEST_DIR");
	if (st->dir.isEmpty())
		st->dir = QFileInfo(qEnvironmentVariable("VVD_QT_GRAB")).absolutePath();
	QDir().mkpath(st->dir);
	RenderCanvas* cv = m_canvas;

	auto log = [](const char* fmt, auto... args) {
		fprintf(stderr, "[paint] ");
		fprintf(stderr, fmt, args...);
		fprintf(stderr, "\n");
		fflush(stderr);
	};
	auto check = [st, log](bool ok, const QString& what) {
		if (!ok) st->fails++;
		log("%s: %s", ok ? "PASS" : "FAIL", what.toLocal8Bit().constData());
	};
	auto add = [st](const QString& name, std::function<void()> fn, bool settle = true) {
		st->steps.push_back({ name, std::move(fn), settle });
	};
	auto cur = [this]() { return CurrentVolume(); };
	auto mask_count = [cur](VolumeData* vd = nullptr) -> long long {
		if (!vd) vd = cur();
		if (!vd) return -1;
		Nrrd* m = MaskNrrd(vd);
		return m ? CountNonzero(m, VoxelCount(vd)) : 0;
	};
	auto capture = [st, cv, add](const QString& name) {
		add("capture " + name, [st, cv, name]() {
			st->waiting_capture = true;
			cv->CaptureToFile(st->dir + "/" + name + ".png");
		}, false);
	};
	// a stroke from a to b (widget coordinates) with brush mode m
	auto stroke = [this, cv](int mode, QPoint a, QPoint b) {
		m_brush_tool->SelectBrushMode(mode);
		cv->SimulateMouse(QEvent::MouseMove, a, Qt::NoButton, Qt::NoButton);
		cv->SimulateMouse(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
		const int n = 24;
		for (int i = 1; i <= n; i++)
		{
			QPoint p = a + (b - a) * i / n;
			cv->SimulateMouse(QEvent::MouseMove, p, Qt::NoButton, Qt::LeftButton);
		}
		cv->SimulateMouse(QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
		m_brush_tool->SelectBrushMode(0);   // leave the paint mode: no brush cursor in the captures
	};
	auto obj_pt = [st](double fx, double fy) {
		return QPoint(st->obj.left() + int(fx * st->obj.width()), st->obj.top() + int(fy * st->obj.height()));
	};
	// object box from a capture (pixels brighter than the background)
	auto find_object = [st, cv, log](const QString& file) {
		QImage img(file);
		if (img.isNull()) { log("no image %s", file.toLocal8Bit().constData()); return; }
		int x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
		for (int y = 0; y < img.height(); y++)
		{
			const QRgb* line = (const QRgb*)img.constScanLine(y);
			for (int x = 0; x < img.width(); x++)
			{
				QRgb c = line[x];
				if (qRed(c) + qGreen(c) + qBlue(c) > 120)
				{
					x0 = std::min(x0, x); x1 = std::max(x1, x);
					y0 = std::min(y0, y); y1 = std::max(y1, y);
				}
			}
		}
		if (x1 < 0) { x0 = img.width() / 4; x1 = img.width() * 3 / 4; y0 = img.height() / 4; y1 = img.height() * 3 / 4; }
		const double sx = double(cv->width()) / img.width(), sy = double(cv->height()) / img.height();
		st->obj = QRect(QPoint(int(x0 * sx), int(y0 * sy)), QPoint(int(x1 * sx), int(y1 * sy)));
		log("object box in view (widget px): %d,%d %dx%d (image %dx%d, view %dx%d)", st->obj.x(), st->obj.y(),
			st->obj.width(), st->obj.height(), img.width(), img.height(), cv->width(), cv->height());
	};
	auto show_only = [this](VolumeData* keep) {
		for (VolumeData* v : m_canvas->Volumes()) v->SetDisp(v == keep);
		if (keep)
		{
			m_canvas->SetCurrentVolume(keep);
			RefreshTree();
			m_tree->SelectVolume(keep);
		}
		m_canvas->RefreshView();
	};
	auto find_vol = [this](const std::wstring& suffix) -> VolumeData* {
		for (VolumeData* v : m_canvas->Volumes())
		{
			std::wstring n = v->GetName();
			if (n.size() >= suffix.size() && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0)
				return v;
		}
		return nullptr;
	};
	// result of the mask calculations: res == data * w(mask) (+-1)
	auto check_apply = [log, check](const char* what, VolumeData* src, VolumeData* res, bool inverse) {
		Nrrd* d = DataNrrd(src);
		Nrrd* m = MaskNrrd(src, false);
		Nrrd* r = DataNrrd(res);
		if (!d || !m || !r) { check(false, QString("%1: missing data (src %2 mask %3 result %4)").arg(what).arg(!!d).arg(!!m).arg(!!r)); return; }
		const long long n = VoxelCount(src);
		const double scale = d->type == r->type ? 1.0 : 1.0;
		long long nz_res = 0, expect_nz = 0, bad = 0, msk_nz = 0, msk_full = 0, msk_part = 0;
		for (long long i = 0; i < n; i++)
		{
			const double mv = ((const unsigned char*)m->data)[i];
			const double w = inverse ? 1.0 - mv / 255.0 : mv / 255.0;
			const double dv = Val(d, i);
			const double rv = Val(r, i);
			const double ev = dv * w * scale;
			if (rv != 0.0) nz_res++;
			if (ev >= 0.5) expect_nz++;
			if (std::fabs(rv - ev) > 1.0) bad++;
			if (mv > 0) { msk_nz++; if (mv >= 255) msk_full++; else msk_part++; }
		}
		log("%s: result %ls nonzero %lld, expected nonzero %lld, |result - data*w| > 1 at %lld voxels; mask voxels %lld (255: %lld, partial: %lld)",
			what, res->GetName().c_str(), nz_res, expect_nz, bad, msk_nz, msk_full, msk_part);
		check(bad == 0, QString("%1 voxels == data * %2").arg(what).arg(inverse ? "(1 - mask)" : "mask"));
	};
	auto data_nonzero = [](VolumeData* vd) { Nrrd* d = DataNrrd(vd); return d ? CountNonzero(d, VoxelCount(vd)) : -1; };

	const int undo_depth = qEnvironmentVariableIsSet("VVD_QT_PAINT_UNDO") ? qEnvironmentVariableIntValue("VVD_QT_PAINT_UNDO") : 5;

	// ------------------------------------------------------------------ steps
	add("start", [this, st, log, undo_depth]() {
		st->src = CurrentVolume();
		if (!st->src && !m_canvas->Volumes().empty()) st->src = m_canvas->Volumes()[0];
		if (st->src) { m_canvas->SetCurrentVolume(st->src); m_tree->SelectVolume(st->src); }
		log("history depth: setting %d, run %d", (int)FLIVR::Texture::mask_undo_num_, undo_depth);
		FLIVR::Texture::mask_undo_num_ = undo_depth;
		if (st->src)
		{
			int nx, ny, nz; st->src->GetResolution(nx, ny, nz);
			log("volume %ls %dx%dx%d, %d bit, max %.0f, mem swap %d, vol method %d", st->src->GetName().c_str(), nx, ny, nz,
				DataNrrd(st->src) && DataNrrd(st->src)->type == nrrdTypeUShort ? 16 : 8, st->src->GetMaxValue(),
				(int)FLIVR::TextureRenderer::get_mem_swap(), m_canvas->GetVolMethod());
		}
		log("brush: size %.0f/%.0f, iterations %d, threshold %.3f, mode %d", m_brush_tool->GetBrushSize1(), m_brush_tool->GetBrushSize2(),
			m_brush_tool->GetBrushIteration(), m_brush_tool->GetBrushSclTranslate(), m_brush_tool->GetPaintMode());
	});

	if (kind == "paint")
	{
		capture("p00_before");
		// The strokes go to the current volume (st->src; wx: the tree
		// selection, the last loaded volume). With several volumes shown the
		// object box is taken from st->src alone (the others hidden for one
		// capture), and when the default stroke line (0.25 .. 0.6 of the box
		// at mid height) crosses none of st->src's pixels (e.g. a ring), the
		// box is moved onto the longest run of st->src pixels in its rows.
		auto hidden = std::make_shared<std::vector<VolumeData*>>();
		add("solo", [this, st, hidden]() {
			for (VolumeData* v : m_canvas->PopVolumeList())
				if (v != st->src && v->GetDisp()) { v->SetDisp(false); hidden->push_back(v); }
			m_canvas->RefreshView();
		});
		capture("p00_src");
		add("locate", [this, st, find_object, hidden, log]() {
			const QString f = st->dir + "/p00_src.png";
			find_object(f);
			QImage img(f);
			if (!hidden->empty() && !img.isNull())
			{
				const double sx = double(img.width()) / m_canvas->width(), sy = double(img.height()) / m_canvas->height();
				auto bright = [&](int x, int y) {
					x = int(x * sx); y = int(y * sy);
					if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) return false;
					QRgb c = img.pixel(x, y);
					return qRed(c) + qGreen(c) + qBlue(c) > 120;
				};
				const int y0 = st->obj.top() + st->obj.height() / 2;
				const int xa = st->obj.left() + int(0.25 * st->obj.width()), xb = st->obj.left() + int(0.6 * st->obj.width());
				int hits = 0;
				for (int x = xa; x <= xb; x++) hits += bright(x, y0);
				if (hits == 0)
				{
					int best_len = 0, best_y = y0, best_x0 = xa, best_x1 = xb;
					for (int y = st->obj.top(); y <= st->obj.bottom(); y++)
					{
						int run = 0, x0 = 0;
						for (int x = st->obj.left(); x <= st->obj.right() + 1; x++)
						{
							if (x <= st->obj.right() && bright(x, y)) { if (!run) x0 = x; run++; }
							else
							{
								if (run > best_len) { best_len = run; best_y = y; best_x0 = x0; best_x1 = x - 1; }
								run = 0;
							}
						}
					}
					// obj_pt(0.25, 0.5) -> 20 % into the run, obj_pt(0.6, 0.5) -> 80 %
					const double pa = best_x0 + 0.2 * (best_x1 - best_x0), pb = best_x0 + 0.8 * (best_x1 - best_x0);
					const double w = (pb - pa) / 0.35;
					const int left = int(pa - 0.25 * w);
					const int h = st->obj.height();
					st->obj = QRect(left, best_y - h / 2, int(w), h);
					log("default stroke line misses %ls: box moved onto its longest run (row %d, x %d..%d)",
						st->src->GetName().c_str(), best_y, best_x0, best_x1);
				}
			}
			for (VolumeData* v : *hidden) v->SetDisp(true);
			hidden->clear();
			m_canvas->RefreshView();
		});
		add("select", [=]() {
			stroke(2, obj_pt(0.25, 0.5), obj_pt(0.6, 0.5));
			st->counts["select"] = mask_count();
			log("Select (highlight) stroke: mask voxels %lld", st->counts["select"]);
			check(st->counts["select"] > 0, "Select produced a selection");
		});
		capture("p01_select");
		add("diffuse", [=]() {
			stroke(4, obj_pt(0.45, 0.45), obj_pt(0.8, 0.55));
			st->counts["diffuse"] = mask_count();
			log("Diffuse stroke: mask voxels %lld", st->counts["diffuse"]);
			check(st->counts["diffuse"] >= st->counts["select"], "Diffuse kept / grew the selection");
		});
		capture("p02_diffuse");
		add("unselect", [=]() {
			stroke(3, obj_pt(0.35, 0.5), obj_pt(0.55, 0.5));
			st->counts["unselect"] = mask_count();
			log("Unselect stroke: mask voxels %lld", st->counts["unselect"]);
			check(st->counts["unselect"] < st->counts["diffuse"], "Unselect removed voxels");
		});
		capture("p03_unselect");
		add("undo/redo", [=]() {
			log("undo available %d redo %d", (int)m_brush_tool->CanUndo(), (int)m_brush_tool->CanRedo());
			m_brush_tool->BrushUndo();
			long long u1 = mask_count();
			m_brush_tool->BrushUndo();
			long long u2 = mask_count();
			m_brush_tool->BrushRedo();
			long long r1 = mask_count();
			m_brush_tool->BrushRedo();
			long long r2 = mask_count();
			log("undo -> %lld, undo -> %lld, redo -> %lld, redo -> %lld", u1, u2, r1, r2);
			check(u1 == st->counts["diffuse"], "undo restores the Diffuse mask");
			check(u2 == st->counts["select"], "second undo restores the Select mask");
			check(r1 == st->counts["diffuse"] && r2 == st->counts["unselect"], "redo replays Diffuse and Unselect");
			m_brush_tool->BrushUndo();
		});
		capture("p04_after_undo");
		add("redo", [=]() { m_brush_tool->BrushRedo(); log("redo: mask voxels %lld", mask_count()); });
		capture("p05_after_redo");
		add("keys", [=]() {
			// Ctrl+Z / Ctrl+Y on the view: exactly one undo / redo step each
			long long c0 = mask_count();
			cv->SimulateKey(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier, "z");
			cv->SimulateKey(QEvent::KeyRelease, Qt::Key_Z, Qt::ControlModifier, "z");
			long long c1 = mask_count();
			cv->SimulateKey(QEvent::KeyPress, Qt::Key_Y, Qt::ControlModifier, "y");
			cv->SimulateKey(QEvent::KeyRelease, Qt::Key_Y, Qt::ControlModifier, "y");
			long long c2 = mask_count();
			log("Ctrl+Z: %lld -> %lld, Ctrl+Y -> %lld", c0, c1, c2);
			check(c1 == st->counts["diffuse"] && c2 == c0, "Ctrl+Z / Ctrl+Y undo / redo one step");
			// wheel in paint mode: brush size (wx: +-wheel/100)
			m_brush_tool->SelectBrushMode(2);
			const double s0 = m_brush_tool->GetBrushSize1();
			cv->SimulateWheel(obj_pt(0.5, 0.5), 600);
			const double s1 = m_brush_tool->GetBrushSize1();
			cv->SimulateWheel(obj_pt(0.5, 0.5), -600);
			log("wheel +600 / -600 in paint mode: brush size %.1f -> %.1f -> %.1f", s0, s1, m_brush_tool->GetBrushSize1());
			check(std::fabs(s1 - s0 - 6.0) < 1e-6 && std::fabs(m_brush_tool->GetBrushSize1() - s0) < 1e-6, "wheel changes the brush size");
			m_brush_tool->SelectBrushMode(0);
			// V held (key press on the view) hides the selection
			cv->SimulateKey(QEvent::KeyPress, Qt::Key_V, Qt::NoModifier, "v");
		});
		capture("p06_vhold");
		add("V released", [=]() {
			cv->SimulateKey(QEvent::KeyRelease, Qt::Key_V, Qt::NoModifier, "v");
			check(cv->GetDrawMask(), "V released: selection drawn again");
			QImage a(st->dir + "/p00_before.png"), b(st->dir + "/p06_vhold.png");
			check(!a.isNull() && a == b, "V held: view identical to the unpainted view");
			m_canvas->SetEnhanceSelection(false);
		});
		capture("p07_no_enhance");
		add("mask intensity", [=]() {
			m_canvas->SetEnhanceSelection(true);
			st->counts["alpha_x1000"] = (long long)std::lround(st->src->GetMaskAlpha() * 1000);
			st->src->SetMaskAlpha(1.0);
			cv->RefreshView();
		});
		capture("p08_mask_intensity_1");
		add("mask intensity low", [=]() { st->src->SetMaskAlpha(0.1); cv->RefreshView(); });
		capture("p09_mask_intensity_0.1");
		add("hide outside", [=]() {
			st->src->SetMaskAlpha(st->counts["alpha_x1000"] / 1000.0);
			st->src->SetMaskHideMode(VOL_MASK_HIDE_OUTSIDE);
			cv->RefreshView();
		});
		capture("p10_hide_outside");
		add("hide inside", [=]() { st->src->SetMaskHideMode(VOL_MASK_HIDE_INSIDE); cv->RefreshView(); });
		capture("p11_hide_inside");
		add("layered", [=]() {
			st->src->SetMaskHideMode(VOL_MASK_HIDE_NONE);
			st->counts["method"] = m_canvas->GetVolMethod();
			m_canvas->SetVolMethod(RenderCanvas::VolMethodSeq);
		});
		capture("p12_layered");
		add("depth", [=]() { m_canvas->SetVolMethod(RenderCanvas::VolMethodMulti); });
		capture("p13_depth");
		add("composite", [=]() { m_canvas->SetVolMethod(RenderCanvas::VolMethodComp); });
		capture("p14_composite");
		add("solid", [=]() {
			m_canvas->SetVolMethod((int)st->counts["method"]);
			long long before = mask_count();
			stroke(8, obj_pt(0.3, 0.3), obj_pt(0.7, 0.7));
			long long after = mask_count();
			log("Solid stroke: mask voxels %lld -> %lld", before, after);
			check(after > before, "Solid brush added voxels");
			m_brush_tool->BrushUndo();
			log("undo Solid: mask voxels %lld", mask_count());
		});
		add("extract", [=]() {
			st->counts["mask_at_extract"] = mask_count();
			m_brush_tool->BrushCreate();
			VolumeData* r = find_vol(L"_EXTRACTED");
			check(r != nullptr, "Extract created <name>_EXTRACTED");
			if (r) check_apply("Extract", st->src, r, false);
			log("calculator A after Extract: %ls (source %ls)", m_calc->GetVolumeA() ? m_calc->GetVolumeA()->GetName().c_str() : L"-",
				st->src->GetName().c_str());
			if (r) show_only(r);
		});
		capture("p15_extracted");
		add("erase", [=]() {
			show_only(st->src);
			m_brush_tool->BrushErase();
			VolumeData* r = find_vol(L"_DELETED");
			check(r != nullptr, "Erase created <name>_DELETED");
			if (r)
			{
				check_apply("Erase", st->src, r, true);
				VolumeData* ex = find_vol(L"_EXTRACTED");
				long long a = data_nonzero(st->src), e = ex ? data_nonzero(ex) : -1, d = data_nonzero(r);
				log("nonzero voxels: original %lld, extracted %lld, erased %lld (extracted + erased = %lld)", a, e, d, e + d);
				show_only(r);
			}
		});
		capture("p16_erased");
		add("reset all", [=]() {
			show_only(st->src);
			m_brush_tool->BrushClear();
			long long c = mask_count();
			log("Reset All: mask voxels %lld", c);
			check(c == 0, "Reset All clears the mask");
		});
		capture("p17_reset_all");
		add("select for analysis", [=]() {
			stroke(2, obj_pt(0.1, 0.5), obj_pt(0.9, 0.5));
			stroke(2, obj_pt(0.5, 0.2), obj_pt(0.5, 0.8));
			log("selection for component analysis: mask voxels %lld", mask_count());
		});
		capture("p18_selection_for_analysis");
		add("component analysis", [=]() {
			m_sel->SetVolume(st->src);
			for (int select = 1; select >= 0; select--)
			{
				int comps = CompAnalysis(0.0, -1.0, 0.197255, select != 0, true);
				log("CompAnalysis(min 0, max ignored, thresh 0.197, selected only %d): %d components, %d voxels", select, comps, m_sel->GetVolumeNum());
				check(comps > 0, QString("component analysis finds components (selected only %1)").arg(select));
			}
			int comps = CompAnalysis(100.0, -1.0, 0.197255, true, true);
			log("CompAnalysis(min 100, selected only): %d components, %d voxels", comps, m_sel->GetVolumeNum());
			CompExport(0, true);
			int comp_vols = 0;
			for (VolumeData* v : m_canvas->Volumes()) if (v->GetName().find(L"_COMP") != std::wstring::npos) comp_vols++;
			log("Multi-Channels export: %d _COMP volumes", comp_vols);
			check(comp_vols > 0, "Multi-Channels export creates volumes");
			const int before = (int)m_canvas->Volumes().size();
			CompExport(1, true);
			log("Random Colors export: %d new volumes", (int)m_canvas->Volumes().size() - before);
			ShowAnnotations();
			log("annotations: %d in the data manager", m_data_mgr->GetAnnotationNum());
			// project with the annotations (wx SaveProject /data/annotations: a .txt
			// is written to <project>_files for annotations without a file)
			{
				const QString prj = st->dir + "/p19_annotations.vrp";
				SaveProject(prj);
				vvd::IniFile ini;
				long n = 0;
				if (ini.Load(VVDString(prj.toStdWString()))) ini.Read(VVDString(L"/data/annotations/num"), n);
				VVDString apath;
				ini.Read(VVDString(L"/data/annotations/0/path"), apath);
				log("project %s: %ld annotations, first at %s", prj.toLocal8Bit().constData(), n, apath.ToQt().toLocal8Bit().constData());
				check(n == m_data_mgr->GetAnnotationNum() && n > 0 && vvd::FileExists(apath),
					"annotations saved in the project with their .txt file");
			}
			show_only(st->src);   // the source with the annotation labels of the last analysis
		});
		capture("p19_annotations");
		add("noise", [=]() {
			show_only(st->src);
			m_sel->SetVolume(st->src);
			int comps = NoiseAnalysis(0.0, 400.0, 0.0196078);
			log("NoiseAnalysis(max 400, thresh 0.0196): %d components, %d voxels", comps, m_sel->GetVolumeNum());
			NoiseRemoval(400, 0.0196078);
			VolumeData* r = find_vol(L"_NR");
			check(r != nullptr, "Noise removal created <name>_NR");
			if (r)
			{
				DataGroup* g = m_canvas->GetParentGroup(r);
				log("_NR volume: nonzero %lld of %lld in the source; group %ls (%d volumes)", data_nonzero(r), data_nonzero(st->src),
					g ? g->GetName().c_str() : L"-", g ? g->GetVolumeNum() : 0);
				show_only(r);
			}
		});
		capture("p20_noise_removed");
	}
	else if (kind == "calc")
	{
		add("calc", [=]() {
			const std::vector<VolumeData*> vols = m_canvas->Volumes();
			if (vols.size() < 2) { check(false, "calc needs two volumes"); return; }
			VolumeData* a = vols[0];
			VolumeData* b = vols[1];
			Nrrd* na = DataNrrd(a);
			Nrrd* nb = DataNrrd(b);
			const long long n = VoxelCount(a);
			if (!na || !nb || VoxelCount(b) != n) { check(false, "calc: same-size volumes with data"); return; }
			const double mx = na->type == nrrdTypeUShort ? 65535.0 : 255.0;
			log("A %ls nonzero %lld, B %ls nonzero %lld", a->GetName().c_str(), CountNonzero(na, n), b->GetName().c_str(), CountNonzero(nb, n));
			const char* names[] = { "", "Subtract", "Add (max)", "Divide", "Colocalize (min)" };
			for (int type = 1; type <= 4; type++)
			{
				for (VolumeData* v : vols) v->SetDisp(true);
				m_calc->SetVolumeA(a);
				m_calc->SetVolumeB(b);
				const int nvol = (int)m_canvas->Volumes().size();
				Calculate(type);
				VolumeData* r = (int)m_canvas->Volumes().size() > nvol ? m_canvas->Volumes().back() : nullptr;
				if (!r) { check(false, QString("%1 produced a volume").arg(names[type])); continue; }
				Nrrd* nr = DataNrrd(r);
				long long bad = 0, nz = 0;
				double worst = 0.0;
				for (long long i = 0; i < n && nr; i++)
				{
					const double x = Val(na, i) / mx, y = Val(nb, i) / mx;
					double e = 0.0;
					switch (type)
					{
					case 1: e = std::min(std::max(x - y, 0.0), 1.0); break;
					case 2: e = std::max(x, y); break;
					case 3: e = (x > 1e-5 && y > 1e-5) ? std::min(std::max(x / y, 0.0), 1.0) : 0.0; break;
					case 4: e = std::min(x, y); break;
					}
					const double rv = Val(nr, i) / (nr->type == nrrdTypeUShort ? 65535.0 : (nr->type == nrrdTypeFloat ? 1.0 : 255.0));
					const double d = std::fabs(rv - e) * mx;
					if (d > 1.0) bad++;
					worst = std::max(worst, d);
					if (Val(nr, i) != 0.0) nz++;
				}
				log("%s: %ls nonzero %lld, voxels off by > 1: %lld (max %.1f); A shown %d, B shown %d; calculator A afterwards %ls",
					names[type], r->GetName().c_str(), nz, bad, worst, (int)a->GetDisp(), (int)b->GetDisp(),
					m_calc->GetVolumeA() ? m_calc->GetVolumeA()->GetName().c_str() : L"-");
				check(bad == 0, QString("%1 matches the formula on every voxel").arg(names[type]));
			}
			for (VolumeData* v : m_canvas->Volumes()) v->SetDisp(v == a);
			m_calc->SetVolumeA(a);
			const int nvol = (int)m_canvas->Volumes().size();
			Calculate(9);
			VolumeData* r = (int)m_canvas->Volumes().size() > nvol ? m_canvas->Volumes().back() : nullptr;
			if (r)
			{
				// wx VolumeCalculator::FillHoles (Calculate type 9): the result
				// is 255 where A > threshold * 255 (8-bit scale, the
				// calculator's threshold = the brush threshold) plus the
				// enclosed holes, 0 elsewhere -- soft edges of A at or below the
				// threshold are not part of it (the old check "every nonzero
				// voxel of A" failed on soft-edged data for that reason)
				Nrrd* nr = DataNrrd(r);
				const double thr = m_calc->GetThreshold();
				long long above = 0, kept = 0, soft = 0, nonbin = 0;
				for (long long i = 0; i < n && nr; i++)
				{
					double va = Val(na, i);
					if (na->type == nrrdTypeUShort) va = (double)(unsigned char)(va * a->GetScalarScale() / 257.0);
					const unsigned char a8 = (unsigned char)va;
					const double rv = Val(nr, i);
					if (rv != 0.0 && rv != 255.0) nonbin++;
					if (a8 > thr * 255) { above++; if (rv == 255.0) kept++; }
					else if (a8 > 0) soft++;
				}
				log("3D Fill Holes: %ls nonzero %lld (A nonzero %lld, A > threshold %.3f x 255: %lld, of them 255 in the result %lld; soft-edge voxels 0 < A <= threshold: %lld; non-binary result voxels %lld)",
					r->GetName().c_str(), data_nonzero(r), CountNonzero(na, n), thr, above, kept, soft, nonbin);
				check(nr && kept == above && nonbin == 0 && data_nonzero(r) >= above,
					"Fill Holes: every voxel of A above the threshold is 255 in the binary result");
			}
			else
				check(false, "Fill Holes produced a volume");
			for (VolumeData* v : m_canvas->Volumes()) v->SetDisp(v == a || v == b);
			m_canvas->RefreshView();
		});
		add("calc via dialog", [=]() {
			// the Analyze dialog's Calculations tab: Load A, Load B, Subtract
			const std::vector<VolumeData*> vols = m_canvas->Volumes();
			VolumeData* a = vols[0];
			VolumeData* b = vols[1];
			for (VolumeData* v : m_canvas->Volumes()) v->SetDisp(v == a || v == b);
			m_brush_dlg->show();
			QList<QPushButton*> loads, subs;
			for (QPushButton* btn : m_brush_dlg->findChildren<QPushButton*>())
			{
				if (btn->text() == "Load") loads << btn;
				if (btn->text() == "Subtract") subs << btn;
			}
			if (loads.size() < 2 || subs.isEmpty()) { check(false, "Calculations tab buttons"); return; }
			m_tree->SelectVolume(a);
			loads[0]->click();
			m_tree->SelectVolume(b);
			loads[1]->click();
			const std::vector<VolumeData*> before = m_canvas->Volumes();
			subs[0]->click();
			VolumeData* r = nullptr;
			for (VolumeData* v : m_canvas->Volumes())
				if (std::find(before.begin(), before.end(), v) == before.end()) r = v;
			Nrrd* na = DataNrrd(a);
			Nrrd* nb = DataNrrd(b);
			Nrrd* nr = DataNrrd(r);
			long long bad = 0;
			for (long long i = 0; na && nb && nr && i < VoxelCount(a); i++)
				if (std::fabs(Val(nr, i) - std::max(0.0, Val(na, i) - Val(nb, i))) > 1.0) bad++;
			log("dialog Subtract: %ls, nonzero %lld, voxels != max(A - B, 0): %lld", r ? r->GetName().c_str() : L"-", r ? data_nonzero(r) : -1, bad);
			check(r && nr && bad == 0, "Calculations tab Subtract == max(A - B, 0)");
			m_brush_dlg->hide();
		});
		capture("c00_after_calc");
	}
	else if (kind == "seg")
	{
		auto click = [log](QDialog* dlg, const QString& text) {
			if (!dlg) return;
			for (QPushButton* b : dlg->findChildren<QPushButton*>())
				if (b->text() == text) { b->click(); return; }
			log("no button '%s' in %s", text.toLocal8Bit().constData(), dlg->windowTitle().toLocal8Bit().constData());
		};
		auto dump = [log](QDialog* dlg) {
			if (!dlg) return;
			QStringList fields;
			for (QLineEdit* e : dlg->findChildren<QLineEdit*>())
				fields << (e->objectName().isEmpty() ? QString("?") : e->objectName()) + "=" + e->text();
			log("%s fields: %s", dlg->windowTitle().toLocal8Bit().constData(), fields.join(", ").toLocal8Bit().constData());
		};
		auto trigger = [this](const QString& entry) {
			for (QAction* a : m_analysis_menu->actions()) if (a->text().startsWith(entry)) { a->trigger(); return; }
		};
		capture("s00_before");
		add("locate", [st, find_object]() { find_object(st->dir + "/s00_before.png"); }, false);
		add("select", [=]() {
			stroke(2, obj_pt(0.1, 0.5), obj_pt(0.9, 0.5));
			log("selection: mask voxels %lld", mask_count());
		});
		add("noise reduction", [=]() {
			trigger(tr("Noise Reduction"));
			dump(m_nr_dlg);
			click(m_nr_dlg, "Preview");
			log("Noise Reduction Preview: mask voxels %lld (components of at most the voxel size)", mask_count());
		});
		capture("s01_nr_preview");
		add("noise reduction erase", [=]() {
			const int before = (int)m_canvas->Volumes().size();
			click(m_nr_dlg, "Erase");
			log("Noise Reduction Erase: %d new volumes (%ls)", (int)m_canvas->Volumes().size() - before,
				(int)m_canvas->Volumes().size() > before ? m_canvas->Volumes().back()->GetName().c_str() : L"-");
			if ((int)m_canvas->Volumes().size() > before)
				log("  nonzero %lld (source %lld)", data_nonzero(m_canvas->Volumes().back()), data_nonzero(st->src));
			if (m_nr_dlg) m_nr_dlg->hide();
			show_only(st->src);
			// a fresh brush selection for the next dialogs
			m_brush_tool->BrushClear();
			stroke(2, obj_pt(0.1, 0.5), obj_pt(0.9, 0.5));
			log("new selection: mask voxels %lld", mask_count());
		});
		add("counting", [=]() {
			trigger(tr("Counting and Volume"));
			if (m_count_dlg)
				for (QCheckBox* c : m_count_dlg->findChildren<QCheckBox*>())
					if (c->text() == "Selct. Only") c->setChecked(true);
			click(m_count_dlg, "Analyze");
			dump(m_count_dlg);
			const int before = (int)m_canvas->Volumes().size();
			click(m_count_dlg, "Multi-Channels");
			log("Counting Multi-Channels: %d new volumes", (int)m_canvas->Volumes().size() - before);
			click(m_count_dlg, "Show Annotations");
			log("annotations in the data manager: %d", m_data_mgr->GetAnnotationNum());
			if (m_count_dlg) m_count_dlg->hide();
			show_only(st->src);
		});
		add("colocalization", [=]() {
			const std::vector<VolumeData*> vols = m_canvas->Volumes();
			VolumeData* other = nullptr;
			for (VolumeData* v : vols) if (v != st->src && v->GetName().find(L"_") == std::wstring::npos) { other = v; break; }
			if (!other) for (VolumeData* v : vols) if (v != st->src) { other = v; break; }
			trigger(tr("Colocalization"));
			if (!m_coloc_dlg || !other) { log("colocalization: no second volume"); return; }
			for (VolumeData* v : m_canvas->Volumes()) v->SetDisp(v == st->src || v == other);
			m_tree->SelectVolume(st->src);
			QList<QPushButton*> loads;
			for (QPushButton* b : m_coloc_dlg->findChildren<QPushButton*>()) if (b->text() == tr("Load")) loads << b;
			if (loads.size() >= 2) loads[0]->click();
			m_tree->SelectVolume(other);
			if (loads.size() >= 2) loads[1]->click();
			dump(m_coloc_dlg);
			const std::vector<VolumeData*> before = m_canvas->Volumes();
			click(m_coloc_dlg, tr("Colocalization"));
			dump(m_coloc_dlg);
			{
				// expected: min(A * maskA, B * maskB) (CAL_INTERSECTION_WITH_MASK)
				Nrrd* da = DataNrrd(st->src); Nrrd* db = DataNrrd(other);
				Nrrd* ma = MaskNrrd(st->src); Nrrd* mb = MaskNrrd(other);
				long long n = VoxelCount(st->src), ea = 0, eb = 0, both = 0, raw = 0;
				for (long long i = 0; da && db && i < n; i++)
				{
					const bool a = Val(da, i) > 0 && (!ma || ((unsigned char*)ma->data)[i] > 0);
					const bool b = Val(db, i) > 0 && (!mb || ((unsigned char*)mb->data)[i] > 0);
					ea += a; eb += b; both += a && b; raw += Val(da, i) > 0 && Val(db, i) > 0;
				}
				log("  after the analysis: A in its mask %lld (mask %s), B in its mask %lld (mask %s), both %lld, raw overlap %lld",
					ea, ma ? "yes" : "no", eb, mb ? "yes" : "no", both, raw);
			}
			for (VolumeData* v : m_canvas->Volumes())
				if (std::find(before.begin(), before.end(), v) == before.end())
					log("Colocalization of %ls and %ls: new volume %ls, nonzero %lld (A %lld, B %lld)", st->src->GetName().c_str(),
						other->GetName().c_str(), v->GetName().c_str(), data_nonzero(v), data_nonzero(st->src), data_nonzero(other));
			m_coloc_dlg->hide();
			show_only(st->src);
		});
		add("convert", [=]() {
			trigger(tr("Convert"));
			if (!m_convert_dlg) return;
			const int meshes = (int)m_canvas->Meshes().size();
			for (QCheckBox* c : m_convert_dlg->findChildren<QCheckBox*>())
				if (c->text() == "Selected Only") c->setChecked(true);
			click(m_convert_dlg, "Convert");
			dump(m_convert_dlg);
			log("Convert (selected only): %d new meshes", (int)m_canvas->Meshes().size() - meshes);
			if ((int)m_canvas->Meshes().size() > meshes)
			{
				MeshData* md = m_canvas->Meshes().back();
				log("  mesh %ls: %d triangles", md->GetName().c_str(), md->GetMesh() ? (int)md->GetMesh()->numtriangles : -1);
			}
			m_convert_dlg->hide();
		});
		capture("s02_converted_mesh");
	}

	if (kind == "nr")
	{
		// noise removal, both wx entry points, results dumped as raw voxels
		// (nr_*.raw, x fastest) for an independent check (scratch nrcheck.py):
		//   Analyze dialog "Noise Removal": Analyze (NoiseAnalysis) + Remove
		//   (NoiseRemoval -> <name>_NR)
		//   Noise Reduction dialog: Preview (CompAnalysis(0, size, thresh,
		//   false, false, size + 1)) + Erase (Calculate(6) -> <name>_DELETED)
		// VVD_QT_NR_SIZE (default 400) voxels, VVD_QT_NR_THRESH raw value
		// (default 5)
		const double nr_size = qEnvironmentVariableIsSet("VVD_QT_NR_SIZE") ? qEnvironmentVariable("VVD_QT_NR_SIZE").toDouble() : 400.0;
		const double nr_raw = qEnvironmentVariableIsSet("VVD_QT_NR_THRESH") ? qEnvironmentVariable("VVD_QT_NR_THRESH").toDouble() : 5.0;
		auto dump = [st, log](VolumeData* vd, const QString& name, bool mask) {
			Nrrd* n = mask ? MaskNrrd(vd, false) : DataNrrd(vd);
			if (!n || !n->data) { log("dump %s: no data", name.toLocal8Bit().constData()); return; }
			const size_t bytes = (size_t)VoxelCount(vd) * (n->type == nrrdTypeUShort ? 2 : (n->type == nrrdTypeFloat ? 4 : 1));
			const QString file = st->dir + "/" + name + ".raw";
			FILE* f = fopen(file.toLocal8Bit().constData(), "wb");
			if (!f) return;
			fwrite(n->data, 1, bytes, f);
			fclose(f);
			log("dumped %s (%zu bytes, type %d)", file.toLocal8Bit().constData(), bytes, (int)n->type);
		};
		auto changed = [](VolumeData* a, VolumeData* b, long long& to_zero) {
			Nrrd* na = DataNrrd(a);
			Nrrd* nb = DataNrrd(b);
			to_zero = 0;
			if (!na || !nb) return -1LL;
			long long c = 0;
			for (long long i = 0; i < VoxelCount(a); i++)
			{
				const double x = Val(na, i), y = Val(nb, i);
				if (x != y) { c++; if (y == 0.0) to_zero++; }
			}
			return c;
		};
		add("analyze dialog: noise analysis + remove", [=]() {
			show_only(st->src);
			m_sel->SetVolume(st->src);
			const double thresh = nr_raw / st->src->GetMaxValue();
			log("size %.0f, threshold %.1f (%.6f of max %.0f), source nonzero %lld", nr_size, nr_raw, thresh, st->src->GetMaxValue(), data_nonzero(st->src));
			int comps = NoiseAnalysis(0.0, nr_size, thresh);
			log("NoiseAnalysis: %d components, %d voxels; mask voxels %lld", comps, m_sel->GetVolumeNum(), CountNonzero(MaskNrrd(st->src, false), VoxelCount(st->src)));
			dump(st->src, "nr_analysis_mask", true);
			NoiseRemoval(int(nr_size), thresh);
			VolumeData* r = find_vol(L"_NR");
			check(r != nullptr, "Remove created <name>_NR");
			if (!r) return;
			long long z = 0;
			const long long c = changed(st->src, r, z);
			log("_NR: nonzero %lld (source %lld, removed %lld); voxels changed %lld, of them set to 0: %lld",
				data_nonzero(r), data_nonzero(st->src), data_nonzero(st->src) - data_nonzero(r), c, z);
			dump(r, "nr_remove_result", false);
			show_only(r);
		});
		capture("n00_nr_removed");
		add("noise reduction dialog: preview + erase", [=]() {
			show_only(st->src);
			m_brush_tool->BrushClear();
			for (QAction* a : m_analysis_menu->actions()) if (a->text().startsWith(tr("Noise Reduction"))) { a->trigger(); break; }
			if (!m_nr_dlg) { check(false, "Noise Reduction dialog"); return; }
			QList<QLineEdit*> edits = m_nr_dlg->findChildren<QLineEdit*>();
			if (edits.size() >= 2)
			{
				edits[0]->setText(QString::asprintf("%.1f", nr_raw));
				edits[1]->setText(QString::number(int(nr_size)));
			}
			QStringList f; for (QLineEdit* e : edits) f << e->text();
			log("Noise Reduction fields: %s", f.join(", ").toLocal8Bit().constData());
			for (QPushButton* b : m_nr_dlg->findChildren<QPushButton*>()) if (b->text() == "Preview") { b->click(); break; }
			log("Preview: %d components, %d voxels; mask voxels %lld (GPU readback %lld)", m_sel->GetCompNum(), m_sel->GetVolumeNum(),
				CountNonzero(MaskNrrd(st->src, false), VoxelCount(st->src)), mask_count(st->src));
			dump(st->src, "nr_preview_mask", true);
			const std::vector<VolumeData*> before = m_canvas->Volumes();
			for (QPushButton* b : m_nr_dlg->findChildren<QPushButton*>()) if (b->text() == "Erase") { b->click(); break; }
			VolumeData* r = nullptr;
			for (VolumeData* v : m_canvas->Volumes())
				if (std::find(before.begin(), before.end(), v) == before.end()) r = v;
			check(r != nullptr, "Erase created a volume");
			if (!r) return;
			long long z = 0;
			const long long c = changed(st->src, r, z);
			log("%ls: nonzero %lld (source %lld, removed %lld); voxels changed %lld, of them set to 0: %lld", r->GetName().c_str(),
				data_nonzero(r), data_nonzero(st->src), data_nonzero(st->src) - data_nonzero(r), c, z);
			dump(r, "nr_erase_result", false);
			m_nr_dlg->hide();
			show_only(r);
		});
		capture("n01_nr_erased");
	}

	if (kind == "hide")
	{
		// Properties Mask: Hide Outside / Hide Inside against the Extract /
		// Erase result volumes rendered alone (same view).
		// VVD_QT_TEST_SHADING=0/1: shading off / on; VVD_QT_TEST_DIFFUSE=1: a Diffuse
		// stroke (partial mask values) instead of Select
		if (qEnvironmentVariableIsSet("VVD_QT_TEST_SHADING"))
			add("shading", [=]() {
				log("render mode %d, shading %d -> %d", st->src->GetMode(), (int)st->src->GetShading(), qEnvironmentVariableIntValue("VVD_QT_TEST_SHADING"));
				st->src->SetShading(qEnvironmentVariableIntValue("VVD_QT_TEST_SHADING") != 0);
				m_prop->GetSettings();
				cv->RefreshView();
			});
		capture("h00_before");
		add("locate", [st, find_object]() { find_object(st->dir + "/h00_before.png"); }, false);
		add("select", [=]() {
			const bool diffuse = qEnvironmentVariableIntValue("VVD_QT_TEST_DIFFUSE") != 0;
			stroke(diffuse ? 4 : 2, obj_pt(0.25, 0.5), obj_pt(0.6, 0.5));
			Nrrd* m = MaskNrrd(st->src);
			long long part = 0;
			for (long long i = 0; m && i < VoxelCount(st->src); i++)
				part += ((unsigned char*)m->data)[i] > 0 && ((unsigned char*)m->data)[i] < 255;
			log("%s stroke: mask voxels %lld (partial values %lld); shading %d", diffuse ? "Diffuse" : "Select", mask_count(), part, (int)st->src->GetShading());
		});
		capture("h01_select");
		add("hide inside", [=]() { st->src->SetMaskHideMode(VOL_MASK_HIDE_INSIDE); cv->RefreshView(); });
		capture("h02_hide_inside");
		add("hide outside", [=]() { st->src->SetMaskHideMode(VOL_MASK_HIDE_OUTSIDE); cv->RefreshView(); });
		capture("h03_hide_outside");
		add("erase", [=]() {
			st->src->SetMaskHideMode(VOL_MASK_HIDE_NONE);
			m_brush_tool->BrushErase();
			VolumeData* r = find_vol(L"_DELETED");
			check(r != nullptr, "Erase created <name>_DELETED");
			if (r) show_only(r);
		});
		capture("h04_erased");
		add("extract", [=]() {
			show_only(st->src);
			m_brush_tool->BrushCreate();
			VolumeData* r = find_vol(L"_EXTRACTED");
			check(r != nullptr, "Extract created <name>_EXTRACTED");
			if (r) show_only(r);
		});
		capture("h05_extracted");
		add("compare", [=]() {
			auto cmp = [&](const char* a, const char* b) {
				QImage ia(st->dir + "/" + a + ".png"), ib(st->dir + "/" + b + ".png");
				if (ia.isNull() || ib.isNull() || ia.size() != ib.size()) { log("compare %s %s: missing", a, b); return; }
				ia = ia.convertToFormat(QImage::Format_RGB32);
				ib = ib.convertToFormat(QImage::Format_RGB32);
				long long n = 0, d8 = 0, d32 = 0, bright_a = 0;
				double sum = 0;
				for (int y = 0; y < ia.height(); y++)
				{
					const QRgb* la = (const QRgb*)ia.constScanLine(y);
					const QRgb* lb = (const QRgb*)ib.constScanLine(y);
					for (int x = 0; x < ia.width(); x++)
					{
						const int d = std::max({ std::abs(qRed(la[x]) - qRed(lb[x])), std::abs(qGreen(la[x]) - qGreen(lb[x])), std::abs(qBlue(la[x]) - qBlue(lb[x])) });
						n++; sum += d; d8 += d > 8; d32 += d > 32;
						bright_a += (qRed(la[x]) + qGreen(la[x]) + qBlue(la[x]) > 60) && (qRed(lb[x]) + qGreen(lb[x]) + qBlue(lb[x]) < 20);
					}
				}
				log("compare %s vs %s: mean |d| %.3f, pixels |d| > 8: %lld, > 32: %lld, bright in %s but dark in %s: %lld", a, b, sum / n, d8, d32, a, b, bright_a);
			};
			cmp("h02_hide_inside", "h04_erased");
			cmp("h03_hide_outside", "h05_extracted");
		}, false);
	}

	if (kind == "ctx")
	{
		// the render view's right-click menu (wx OnContextMenu), PickMesh,
		// right click vs right drag, visibility undo / redo.
		// VVD_QT_CTX_INDEX=1: the first volume is an index (label) volume:
		// segments are selected by double clicks, joined and split.
		const bool index = qEnvironmentVariableIntValue("VVD_QT_CTX_INDEX") != 0;
		auto img_diff = [st](const QString& a, const QString& b) -> long long {
			QImage ia(st->dir + "/" + a + ".png"), ib(st->dir + "/" + b + ".png");
			if (ia.isNull() || ib.isNull() || ia.size() != ib.size()) return -1;
			ia = ia.convertToFormat(QImage::Format_RGB32);
			ib = ib.convertToFormat(QImage::Format_RGB32);
			long long n = 0;
			for (int y = 0; y < ia.height(); y++)
			{
				const QRgb* la = (const QRgb*)ia.constScanLine(y);
				const QRgb* lb = (const QRgb*)ib.constScanLine(y);
				for (int x = 0; x < ia.width(); x++) n += la[x] != lb[x];
			}
			return n;
		};
		auto right_click = [cv](QPoint p) {
			cv->SimulateMouse(QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
			cv->SimulateMouse(QEvent::MouseButtonPress, p, Qt::RightButton, Qt::RightButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, p, Qt::RightButton, Qt::NoButton);
		};
		auto menu_texts = [this]() {
			QStringList l;
			if (QMenu* m = ViewContextMenu())
				for (QAction* a : m->actions()) l << (a->isSeparator() ? QString("--") : a->text());
			return l.join(" | ");
		};
		// right click in the view, then the menu entry `text`
		auto menu_do = [=](const QString& text) {
			right_click(QPoint(cv->width() / 2, cv->height() / 2));
			QMenu* m = ViewContextMenu();
			if (!m || !m->isVisible()) { check(false, "right click opens the view menu for " + text); return false; }
			for (QAction* a : m->actions())
				if (a->text() == text) { a->trigger(); m->close(); return true; }
			check(false, "view menu has '" + text + "'");
			m->close();
			return false;
		};
		auto vols = [this]() { return m_canvas->Volumes(); };
		auto disp = [this]() {
			QStringList l;
			for (VolumeData* v : m_canvas->Volumes()) l << QString("%1=%2").arg(QString::fromStdWString(v->GetName())).arg((int)v->GetDisp());
			for (MeshData* m : m_canvas->Meshes()) l << QString("%1=%2").arg(QString::fromStdWString(m->GetName())).arg((int)m->GetDisp());
			return l.join(", ");
		};
		auto tree_sel = [this]() {
			QTreeWidgetItem* it = m_tree->Tree()->currentItem();
			return it ? QString("%1 (type %2)").arg(it->text(0)).arg(TreePanel::ItemType(it)) : QString("-");
		};
		if (index)
			add("index mode", [=]() {
				st->src->SetColormapMode(3);
				m_prop->GetSettings();
				RefreshTree();
				m_tree->SelectVolume(st->src);
			});
		capture("x00_all");
		add("locate", [st, find_object]() { find_object(st->dir + "/x00_all.png"); }, false);
		add("right click menu", [=]() {
			right_click(QPoint(cv->width() / 2, cv->height() / 2));
			QMenu* m = ViewContextMenu();
			check(m && m->isVisible(), "a right click opens the view's context menu");
			log("menu: %s", menu_texts().toLocal8Bit().constData());
			if (m)
			{
				m->grab().save(st->dir + "/x00_menu.png");
				m->close();
			}
			// wx RightUp: the menu in every mode but the ruler mode (int mode 5)
			m_brush_tool->SelectBrushMode(2);
			right_click(QPoint(cv->width() / 2, cv->height() / 2));
			m = ViewContextMenu();
			check(m && m->isVisible(), "a right click in paint mode opens the menu too");
			if (m) m->close();
			m_brush_tool->SelectBrushMode(0);
			m_ruler_tool->SetRulerType(0);
			m_ruler_tool->SetIntMode(5);
			right_click(QPoint(cv->width() / 2, cv->height() / 2));
			m = ViewContextMenu();
			check(!(m && m->isVisible()), "a right click in ruler mode finishes the ruler, no menu");
			if (m) m->close();
			m_ruler_tool->SetIntMode(1);
			log("rulers after the ruler-mode right click: %d", (int)m_ruler_tool->GetRulerList()->size());
		});
		if (!index)
		{
			add("mesh pick", [=]() {
				if (m_canvas->Meshes().empty()) { log("no mesh loaded"); return; }
				QPoint hit(-1, -1);
				int seg = -1;
				MeshData* md = nullptr;
				for (int y = 4; y < cv->height() && !md; y += 6)
					for (int x = 4; x < cv->width() && !md; x += 6)
						if ((md = m_canvas->PickMesh(QPoint(x, y), &seg))) hit = QPoint(x, y);
				log("PickMesh scan: %ls at %d,%d (segment %d)", md ? md->GetName().c_str() : L"-", hit.x(), hit.y(), seg);
				check(md != nullptr, "PickMesh finds the mesh in the view");
				if (!md) return;
				m_tree->SelectVolume(st->src);
				cv->SimulateMouse(QEvent::MouseMove, hit, Qt::NoButton, Qt::NoButton);
				cv->SimulateMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::LeftButton);
				cv->SimulateMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoButton);
				const int t = m_tree->GetCurrentSelType();
				log("click on the mesh: tree selection %s", tree_sel().toLocal8Bit().constData());
				check(t == TreePanel::TypeMesh || t == TreePanel::TypeMeshSeg || t == TreePanel::TypeMeshSegGroup,
					"a click on the mesh selects it (or its SWC segment) in the tree");
				const QPoint empty(3, 3);
				cv->SimulateMouse(QEvent::MouseMove, empty, Qt::NoButton, Qt::NoButton);
				cv->SimulateMouse(QEvent::MouseButtonPress, empty, Qt::LeftButton, Qt::LeftButton);
				cv->SimulateMouse(QEvent::MouseButtonRelease, empty, Qt::LeftButton, Qt::NoButton);
				log("click on empty space: tree selection %s", tree_sel().toLocal8Bit().constData());
				if (t == TreePanel::TypeMesh)
					check(m_tree->GetCurrentSelType() == TreePanel::TypeView, "a click on empty space with a mesh selected selects the view (wx Select(view, \"\"))");
			});
			add("hide selected", [=]() {
				m_tree->SelectVolume(st->src);
				log("selected %s; before: %s", tree_sel().toLocal8Bit().constData(), disp().toLocal8Bit().constData());
				menu_do("Hide selected");
				log("after Hide selected: %s; undo steps %d", disp().toLocal8Bit().constData(), m_tree->VisUndoCount());
				check(!st->src->GetDisp(), "Hide selected hides the selected volume");
			});
			capture("x01_hidden");
			add("undo", [=]() {
				menu_do("Undo visibility");
				log("after Undo visibility: %s; undo %d redo %d", disp().toLocal8Bit().constData(), m_tree->VisUndoCount(), m_tree->VisRedoCount());
				check(st->src->GetDisp(), "Undo visibility shows it again");
			});
			capture("x02_undo");
			add("redo", [=]() {
				const long long d01 = img_diff("x00_all", "x01_hidden"), d02 = img_diff("x00_all", "x02_undo");
				log("pixels differing: all vs hidden %lld, all vs undo %lld", d01, d02);
				check(d01 > 0 && d02 == 0, "the hidden volume is gone from the view, after the undo the view is identical to before");
				menu_do("Redo visibility");
				log("after Redo visibility: %s", disp().toLocal8Bit().constData());
				check(!st->src->GetDisp(), "Redo visibility hides it again");
			});
			capture("x03_redo");
			add("show only selected", [=]() {
				log("pixels differing: hidden vs redo %lld", img_diff("x01_hidden", "x03_redo"));
				menu_do("Show all");
				VolumeData* other = nullptr;
				for (VolumeData* v : vols()) if (v != st->src) { other = v; break; }
				if (!other) { log("one volume only"); return; }
				m_tree->SelectVolume(other);
				menu_do("Show only selected");
				bool ok = other->GetDisp();
				for (VolumeData* v : vols()) if (v != other) ok = ok && !v->GetDisp();
				for (MeshData* m : m_canvas->Meshes()) ok = ok && !m->GetDisp();
				log("Show only selected (%ls): %s", other->GetName().c_str(), disp().toLocal8Bit().constData());
				check(ok, "Show only selected hides every other volume and mesh");
			});
			capture("x04_only_selected");
			add("show all", [=]() {
				menu_do("Show all");
				bool ok = true;
				for (VolumeData* v : vols()) ok = ok && v->GetDisp();
				for (MeshData* m : m_canvas->Meshes()) ok = ok && m->GetDisp();
				log("Show all: %s", disp().toLocal8Bit().constData());
				check(ok, "Show all shows everything");
			});
			capture("x05_show_all");
			add("undo chain", [=]() {
				log("pixels differing: all vs show all %lld", img_diff("x00_all", "x05_show_all"));
				check(img_diff("x00_all", "x05_show_all") == 0, "after Show all the view is identical to the start");
				// three steps back: Show only selected -> Show all (redo) -> ...
				menu_do("Undo visibility");
				log("undo 1: %s", disp().toLocal8Bit().constData());
				menu_do("Undo visibility");
				log("undo 2: %s", disp().toLocal8Bit().constData());
				menu_do("Redo visibility");
				log("redo: %s", disp().toLocal8Bit().constData());
				menu_do("Show all");
			});
		}
		else
		{
			add("select two segments", [=]() {
				// double clicks (wx SelSegVolume(0)) on a grid until two
				// segments are selected
				st->src->ClearSelIDs();
				for (int y = st->obj.top(); y < st->obj.bottom() && st->src->GetSelIDs().size() < 2; y += 24)
					for (int x = st->obj.left(); x < st->obj.right() && st->src->GetSelIDs().size() < 2; x += 24)
					{
						const size_t before = st->src->GetSelIDs().size();
						const QPoint p(x, y);
						cv->SimulateMouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
						cv->SimulateMouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
						cv->SimulateMouse(QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton);
						cv->SimulateMouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
						if (st->src->GetSelIDs().size() < before) // toggled one off again
						{
							cv->SimulateMouse(QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton);
							cv->SimulateMouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
						}
					}
				QStringList l; for (int id : st->src->GetSelIDs()) l << QString::number(id);
				log("selected segments {%s}", l.join(",").toLocal8Bit().constData());
				check(st->src->GetSelIDs().size() == 2, "two segments selected");
				m_tree->SelectVolume(st->src);
			});
			capture("x01_two_selected");
			add("join", [=]() {
				std::vector<int> ids(st->src->GetSelIDs().begin(), st->src->GetSelIDs().end());
				menu_do("Join selected segments");
				QStringList l; for (int id : st->src->GetSelIDs()) l << QString::number(id);
				QStringList c; for (int id : ids) c << QString("%1->%2").arg(id).arg(st->src->IsROICombined(id));
				log("after Join: selected {%s}, combined %s", l.join(",").toLocal8Bit().constData(), c.join(" ").toLocal8Bit().constData());
				int combined = 0; for (int id : ids) combined += st->src->IsROICombined(id) >= 0;
				check(ids.size() == 2 && combined == 1 && st->src->GetSelIDs().size() == 1, "Join combines the two selected segments into one");
			});
			capture("x02_joined");
			add("split", [=]() {
				menu_do("Split selected segments");
				QStringList l; for (int id : st->src->GetSelIDs()) l << QString::number(id);
				log("after Split: selected {%s}", l.join(",").toLocal8Bit().constData());
				bool none = true; for (int id : st->src->GetSelIDs()) none = none && st->src->IsROICombined(id) < 0;
				check(st->src->GetSelIDs().size() == 2 && none, "Split restores the two segments");
			});
			capture("x03_split");
			add("only selected segments", [=]() {
				m_tree->SelectVolume(st->src);
				menu_do("Show only selected");
				log("Show only selected on the index volume: id display mode %d", st->src->GetIDColDispMode());
				check(st->src->GetIDColDispMode() == 2, "Show only selected shows the selected segments only (id display mode 2)");
			});
			capture("x04_only_selected_segments");
			add("undo segment display", [=]() {
				menu_do("Undo visibility");
				menu_do("Show all");
				log("Show all: id display mode %d, selected %d", st->src->GetIDColDispMode(), (int)st->src->GetSelIDs().size());
			});
			capture("x05_segments_show_all");
		}
		add("right drag", [=]() {
			if (QMenu* m = ViewContextMenu()) m->close();
			const double z0 = cv->GetScaleFactor();
			const QPoint a(cv->width() / 2, cv->height() / 2);
			cv->SimulateMouse(QEvent::MouseMove, a, Qt::NoButton, Qt::NoButton);
			cv->SimulateMouse(QEvent::MouseButtonPress, a, Qt::RightButton, Qt::RightButton);
			for (int i = 1; i <= 10; i++)
				cv->SimulateMouse(QEvent::MouseMove, a + QPoint(0, -8 * i), Qt::NoButton, Qt::RightButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, a + QPoint(0, -80), Qt::RightButton, Qt::NoButton);
			QMenu* m = ViewContextMenu();
			log("right drag: zoom %.3f -> %.3f, menu shown %d", z0, cv->GetScaleFactor(), m && m->isVisible() ? 1 : 0);
			check(cv->GetScaleFactor() != z0 && !(m && m->isVisible()), "a right drag zooms and opens no menu");
		});
		capture("x06_zoomed");
	}

	if (kind == "ui")
	{
		// Show/Hide UI (wx ToggleAllTools / m_tb_menu_ui / Windows menu)
		auto state = [this]() {
			const char* names[] = { "Workspace", "Measurement", "Record/Export", "Output Adjustments", "Clipping Planes", "Properties" };
			QStringList l;
			for (int i = 0; i < PaneCount; i++)
				l << QString("%1=%2(tb %3 win %4)").arg(names[i]).arg((int)IsPaneShown(i))
					.arg(m_ui_pane_act[i] ? (int)m_ui_pane_act[i]->isChecked() : -1)
					.arg(m_win_pane_act[i] ? (int)m_win_pane_act[i]->isChecked() : -1);
			return l.join(", ");
		};
		auto synced = [this]() {
			for (int i = 0; i < PaneCount; i++)
				if (m_ui_pane_act[i]->isChecked() != IsPaneShown(i) || m_win_pane_act[i]->isChecked() != IsPaneShown(i))
					return false;
			return true;
		};
		auto shown = [this]() { std::vector<bool> v; for (int i = 0; i < PaneCount; i++) v.push_back(IsPaneShown(i)); return v; };
		auto grab_win = [this, st](const QString& name) { grab().save(st->dir + "/" + name + ".png"); };
		auto button = [this]() { return findChild<QToolButton*>("ShowHideUIButton"); };
		auto st_shown = std::make_shared<std::vector<bool>>();
		add("menus", [=]() {
			for (QAction* m : menuBar()->actions())
			{
				QStringList items;
				if (m->menu())
					for (QAction* a : m->menu()->actions())
						items << (a->isSeparator() ? QString("--") : a->text() + (a->isCheckable() ? (a->isChecked() ? " [x]" : " [ ]") : QString()) +
							(a->menuRole() == QAction::QuitRole || a->menuRole() == QAction::AboutRole ? " (application menu)" : QString()));
				log("menu %s: %s", m->text().toLocal8Bit().constData(), items.join(" | ").toLocal8Bit().constData());
			}
			QStringList ui;
			for (QAction* a : m_tb_menu_ui->actions()) ui << a->text() + (a->isCheckable() ? (a->isChecked() ? " [x]" : " [ ]") : QString());
			log("Show/Hide UI drop-down: %s", ui.join(" | ").toLocal8Bit().constData());
			QToolButton* b = button();
#ifdef __APPLE__
			check(b && !b->defaultAction()->menu(), "Show/Hide UI is a plain button (wx macOS)");
#else
			check(b && b->popupMode() == QToolButton::MenuButtonPopup && b->defaultAction() && b->defaultAction()->menu() == m_tb_menu_ui,
				"Show/Hide UI is a button with a drop-down arrow");
#endif
			log("initial: %s", state().toLocal8Bit().constData());
			check(synced(), "check marks follow the panes");
			*st_shown = shown();
			grab_win("u00_start");
			m_tb_menu_ui->popup(mapToGlobal(QPoint(300, 60)));
		}, false);
		add("drop-down picture", [=]() {
			m_tb_menu_ui->grab().save(st->dir + "/u00_dropdown.png");
			m_tb_menu_ui->close();
		}, false);
#ifdef __APPLE__
		add("button pops the menu", [=]() {
			button()->click();
		}, false);
		add("menu shown", [=]() {
			check(m_tb_menu_ui->isVisible(), "clicking Show/Hide UI pops the panel menu (wx macOS PopupMenu)");
			m_tb_menu_ui->grab().save(st->dir + "/u00_popup.png");
			m_tb_menu_ui->close();
		}, false);
#endif
		auto hide_all = [=]() {
#ifdef __APPLE__
			for (QAction* a : m_tb_menu_ui->actions()) if (a->text() == tr("Toggle All Panels")) { a->trigger(); break; }
#else
			button()->click();
#endif
		};
		add("button hides all", [=]() {
			hide_all();
			log("after the button: %s", state().toLocal8Bit().constData());
			bool none = true; for (int i = 0; i < PaneCount; i++) none = none && !IsPaneShown(i);
			check(none && synced(), "Show/Hide UI hides every pane");
		}, false);
		add("grab hidden", [=]() { grab_win("u01_hidden"); }, false);
		add("button restores", [=]() {
			hide_all();
			log("after the button again: %s", state().toLocal8Bit().constData());
			check(shown() == *st_shown && synced(), "Show/Hide UI brings back exactly the panes that were shown");
		}, false);
		add("grab restored", [=]() { grab_win("u02_restored"); }, false);
		add("per-pane toggle + toggle all", [=]() {
			m_ui_pane_act[PaneClip]->trigger();
			log("drop-down Clipping Planes: %s", state().toLocal8Bit().constData());
			check(!IsPaneShown(PaneClip) && synced(), "the drop-down item hides Clipping Planes");
			std::vector<bool> expect = shown();
			for (QAction* a : m_tb_menu_ui->actions()) if (a->text() == tr("Toggle All Panels")) { a->trigger(); break; }
			bool none = true; for (int i = 0; i < PaneCount; i++) none = none && !IsPaneShown(i);
			check(none, "Toggle All Panels hides the rest");
			for (QAction* a : m_tb_menu_ui->actions()) if (a->text() == tr("Toggle All Panels")) { a->trigger(); break; }
			log("Toggle All Panels twice: %s", state().toLocal8Bit().constData());
			check(shown() == expect && synced(), "Toggle All Panels restores the panes without Clipping Planes");
			m_win_pane_act[PaneClip]->trigger();
			check(IsPaneShown(PaneClip) && synced(), "Windows > Clipping Planes shows it again");
		}, false);
		add("measurement", [=]() {
			m_win_pane_act[PaneMeasure]->trigger();
		}, false);
		add("measurement shown", [=]() {
			log("Windows > Measurement: %s", state().toLocal8Bit().constData());
			check(IsPaneShown(PaneMeasure) && synced(), "Measurement opens with its check marks set");
			if (m_measure_dlg) m_measure_dlg->hide();
		}, false);
		add("measurement closed", [=]() {
			check(!IsPaneShown(PaneMeasure) && synced(), "closing Measurement clears its check marks");
			m_tree_dock->close();   // the pane's title bar close button
		}, false);
		add("dock closed", [=]() {
			log("Workspace closed by its close button: %s", state().toLocal8Bit().constData());
			check(!IsPaneShown(PaneTree) && synced(), "closing a pane by its title bar clears its check marks (wx OnPaneClose)");
			m_win_pane_act[PaneTree]->trigger();
			m_ui_toolbar_act->trigger();
		}, false);
		add("toolbar hidden", [=]() {
			log("Show/Hide Toolbar: toolbar shown %d, check %d", (int)!m_main_tb->isHidden(), (int)m_ui_toolbar_act->isChecked());
			check(m_main_tb->isHidden() && !m_ui_toolbar_act->isChecked(), "Show/Hide Toolbar hides the toolbar");
			grab_win("u03_no_toolbar");
			m_ui_toolbar_act->trigger();
		}, false);
		add("toolbar back", [=]() {
			check(!m_main_tb->isHidden() && m_ui_toolbar_act->isChecked() && IsPaneShown(PaneTree), "the toolbar and the Workspace are back");
			// a project round trip keeps the pane layout (wx /ui_layout)
			m_ui_pane_act[PaneAdjust]->trigger();
			StoreProjectUILayout();
			QStringList l;
			for (const auto& kv : m_prj_passthrough) if (kv.first.startsWith("/ui_layout/")) l << kv.first.mid(11) + "=" + kv.second;
			log("project /ui_layout: %s", l.join(", ").toLocal8Bit().constData());
			m_ui_pane_act[PaneAdjust]->trigger();
			ApplyProjectUILayout();
			check(!IsPaneShown(PaneAdjust) && synced(), "/ui_layout of a project hides Output Adjustments again");
			m_ui_pane_act[PaneAdjust]->trigger();
			grab_win("u04_end");
		}, false);
	}

	if (kind == "diffuse")
	{
		// timing run (with VVD_PAINT_TIMING=1 for the per-stage [ptime] lines):
		// Select, Diffuse, Unselect strokes, VVD_QT_DIFFUSE_REPS times (Reset
		// All in between), mask counts after each stroke, captures of the
		// first round. VVD_QT_DIFFUSE_DRAG=1: the drag events are delivered
		// one per runner tick (50 ms) so frames are drawn while the button is
		// held, like a real drag; otherwise all events are sent at once.
		const int reps = std::max(1, qEnvironmentVariableIsSet("VVD_QT_DIFFUSE_REPS") ? qEnvironmentVariableIntValue("VVD_QT_DIFFUSE_REPS") : 1);
		const bool drag = qEnvironmentVariableIntValue("VVD_QT_DIFFUSE_DRAG") != 0;
		// VVD_QT_DIFFUSE_ITERS=<n>: brush iterations for the run (20 / 50 / 100 =
		// weak / normal / strong; default: the brush settings file)
		if (qEnvironmentVariableIsSet("VVD_QT_DIFFUSE_ITERS"))
		{
			const int it = qEnvironmentVariableIntValue("VVD_QT_DIFFUSE_ITERS");
			add("iterations", [=]() { m_brush_tool->SetBrushIteration(it); log("brush iterations set to %d", m_brush_tool->GetBrushIteration()); }, false);
		}
		// the clipping-plane frame follows the real pointer (shown while it is
		// over the Clipping Planes pane): keep it out of the captures / timings
		add("no clip frame", [cv]() { cv->SetDrawClip(false); cv->RefreshView(); });
		// VVD_QT_DIFFUSE_PERSP=1: perspective view; VVD_QT_DIFFUSE_ROT="rx,ry,rz" (degrees)
		if (qEnvironmentVariableIntValue("VVD_QT_DIFFUSE_PERSP") != 0 || qEnvironmentVariableIsSet("VVD_QT_DIFFUSE_ROT"))
		{
			const bool persp = qEnvironmentVariableIntValue("VVD_QT_DIFFUSE_PERSP") != 0;
			const QStringList r = qEnvironmentVariable("VVD_QT_DIFFUSE_ROT").split(',');
			add("view", [=]() {
				if (persp) cv->SetPerspective(true);
				if (r.size() == 3) cv->SetRotations(r[0].toDouble(), r[1].toDouble(), r[2].toDouble());
				log("view: perspective %d, rotation %s", (int)cv->GetPerspective(), qEnvironmentVariable("VVD_QT_DIFFUSE_ROT").toLocal8Bit().constData());
				cv->RefreshView();
			});
		}
		capture("d00_before");
		add("locate", [st, find_object]() { find_object(st->dir + "/d00_before.png"); }, false);
		auto timed_count = [=](const QString& what, int rep) {
			QElapsedTimer t; t.start();
			long long c = mask_count();
			log("rep %d %s: mask voxels %lld (readback + count %lld ms)", rep, what.toLocal8Bit().constData(), c, (long long)t.elapsed());
			st->counts[QString("%1_%2").arg(what).arg(rep)] = c;
		};
		auto stroke_steps = [=](const QString& what, int mode, double ax, double ay, double bx, double by, int rep) {
			auto t0 = std::make_shared<QElapsedTimer>();
			const int n = 24;
			if (!drag)
			{
				add(what, [=]() {
					cv->SetDrawClip(false);
					t0->start();
					stroke(mode, obj_pt(ax, ay), obj_pt(bx, by));
					log("rep %d %s stroke: events + Segment %lld ms", rep, what.toLocal8Bit().constData(), (long long)t0->elapsed());
				});
			}
			else
			{
				add(what + " press", [=]() {
					cv->SetDrawClip(false);
					m_brush_tool->SelectBrushMode(mode);
					const QPoint a = obj_pt(ax, ay);
					cv->SimulateMouse(QEvent::MouseMove, a, Qt::NoButton, Qt::NoButton);
					t0->start();
					cv->SimulateMouse(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
				}, false);
				for (int i = 1; i <= n; i++)
					add(what + " move", [=]() {
						const QPoint a = obj_pt(ax, ay), b = obj_pt(bx, by);
						cv->SimulateMouse(QEvent::MouseMove, a + (b - a) * i / n, Qt::NoButton, Qt::LeftButton);
					}, false);
				add(what + " release", [=]() {
					QElapsedTimer tr; tr.start();
					cv->SimulateMouse(QEvent::MouseButtonRelease, obj_pt(bx, by), Qt::LeftButton, Qt::NoButton);
					m_brush_tool->SelectBrushMode(0);
					log("rep %d %s stroke: release handler %lld ms, press to end of release %lld ms", rep, what.toLocal8Bit().constData(),
						(long long)tr.elapsed(), (long long)t0->elapsed());
				});
			}
			// the settle wait of the step above is the redraw after the stroke
			add(what + " count", [=]() { log("rep %d %s: settled %lld ms after the press", rep, what.toLocal8Bit().constData(), (long long)t0->elapsed()); timed_count(what, rep); }, false);
		};
		for (int r = 0; r < reps; r++)
		{
			if (r > 0)
				add("reset", [=]() { m_brush_tool->BrushClear(); log("rep %d reset: mask voxels %lld", r, mask_count()); });
			stroke_steps("select", 2, 0.25, 0.5, 0.6, 0.5, r);
			if (r == 0) capture("d01_select");
			stroke_steps("diffuse", 4, 0.45, 0.45, 0.8, 0.55, r);
			if (r == 0) capture("d02_diffuse");
			stroke_steps("unselect", 3, 0.35, 0.5, 0.55, 0.5, r);
			if (r == 0) capture("d03_unselect");
		}
		add("summary", [=]() {
			for (const char* w : { "select", "diffuse", "unselect" })
			{
				QStringList l;
				for (int r = 0; r < reps; r++) l << QString::number(st->counts[QString("%1_%2").arg(w).arg(r)]);
				log("counts %s: %s", w, l.join(" ").toLocal8Bit().constData());
			}
		}, false);
	}

	if (kind == "index")
	{
		// index (label) volume: ":Index" mode, double-click selects the
		// segment under the cursor, a click makes it the edited segment
		// (Properties "Segment:" row), the segment colour is changed
		add("index mode", [=]() {
			st->src->SetColormapMode(3);
			m_prop->GetSettings();
			RefreshTree();
			m_tree->SelectVolume(st->src);
		});
		capture("i00_index");
		add("locate", [st, find_object]() { find_object(st->dir + "/i00_index.png"); }, false);
		add("double click", [=]() {
			const QPoint p = obj_pt(0.5, 0.5);
			cv->SimulateMouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
			cv->SimulateMouse(QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
			std::unordered_set<int> ids = st->src->GetSelIDs();
			QStringList l; for (int id : ids) l << QString::number(id);
			log("double click at %d,%d: selected segment ids {%s}", p.x(), p.y(), l.join(",").toLocal8Bit().constData());
			check(!ids.empty(), "double click selects the segment under the cursor");
		});
		capture("i01_segment_selected");
		add("click", [=]() {
			const QPoint p = obj_pt(0.5, 0.5);
			cv->SimulateMouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
			const int edit = st->src->GetEditSelID();
			QLineEdit* name = m_prop->findChild<QLineEdit*>("ROINameText");
			log("click: edited segment %d; Properties segment row visible %d, name '%s' (placeholder '%s'); tree item '%s'", edit,
				name ? (int)name->isVisibleTo(m_prop) : -1, name ? name->text().toLocal8Bit().constData() : "-",
				name ? name->placeholderText().toLocal8Bit().constData() : "-",
				m_tree->Tree()->currentItem() ? m_tree->Tree()->currentItem()->text(0).toLocal8Bit().constData() : "-");
			check(edit > 0, "click makes the selected segment the edited one");
			if (edit > 0)
			{
				st->src->SetIDColor(0, 255, 0, true, edit);
				st->src->SetIDColDispMode(1);
				cv->RefreshView();
			}
		});
		capture("i02_segment_green_darkgray");
	}

	// Measurement list: the measurement dialog's rows on stderr + a picture
	auto dump_rulers = [=](const QString& file) {
		MeasureDialog* dlg = EnsureMeasureDialog();
		dlg->show();
		dlg->resize(1400, 420);   // wide enough for every column in the picture
		dlg->GetSettings();
		QTreeWidget* list = dlg->findChild<QTreeWidget*>();
		QStringList head;
		for (int c = 0; list && c < list->columnCount(); c++) head << list->headerItem()->text(c);
		log("measurement list columns: %s", head.join(" | ").toLocal8Bit().constData());
		for (int r = 0; list && r < list->topLevelItemCount(); r++)
		{
			QStringList cols;
			for (int c = 0; c < list->columnCount(); c++) cols << list->topLevelItem(r)->text(c);
			log("  row %d: %s", r, cols.join(" | ").toLocal8Bit().constData());
		}
		QCoreApplication::processEvents();
		dlg->grab().save(st->dir + "/" + file);
		log("saved %s", (st->dir + "/" + file).toLocal8Bit().constData());
	};
	auto list_cell = [this](int row, const QString& header) -> QString {
		QTreeWidget* list = m_measure_dlg ? m_measure_dlg->findChild<QTreeWidget*>() : nullptr;
		if (!list || row >= list->topLevelItemCount()) return QString("<no row>");
		for (int c = 0; c < list->columnCount(); c++)
			if (list->headerItem()->text(c) == header) return list->topLevelItem(row)->text(c);
		return QString("<no column>");
	};

	if (kind == "rulervol" || kind == "rulerauto")
	{
		// a 2-point ruler from a point on volume 0 (current volume) to a
		// point on volume 1 (made current before the second click), pointing
		// method Maximum Intensity; the list must name volume 0 at the start
		// and volume 1 at the end; text export + project (VVD_QT_SAVE or
		// <dir>/rulervol.vrp) for the reopen check (VVD_QT_TEST=rulerlist)
		auto pts = std::make_shared<std::pair<QPoint, QPoint>>();
		add("find points", [=]() {
			const std::vector<VolumeData*>& vols = m_canvas->Volumes();
			if (vols.size() < 2) { check(false, "two volumes loaded"); return; }
			VolumeData* a = vols[0];
			VolumeData* b = vols[1];
			// pixels where the max-intensity ray hits a but not b (and vice versa);
			// take the one nearest the middle of each set
			std::vector<QPoint> ha, hb;
			const int step = 6;
			for (int y = step; y < cv->height() - step; y += step)
				for (int x = step; x < cv->width() - step; x += step)
				{
					FLIVR::Point p;
					const bool ia = m_ruler_tool->GetPointVolume(p, x, y, a, 1, false) > 0.0;
					const bool ib = m_ruler_tool->GetPointVolume(p, x, y, b, 1, false) > 0.0;
					if (ia && !ib) ha.push_back(QPoint(x, y));
					if (ib && !ia) hb.push_back(QPoint(x, y));
				}
			auto pick = [](const std::vector<QPoint>& v) {
				if (v.empty()) return QPoint(-1, -1);
				double cx = 0, cy = 0;
				for (const QPoint& q : v) { cx += q.x(); cy += q.y(); }
				cx /= v.size(); cy /= v.size();
				QPoint best = v[0]; double bd = 1e30;
				for (const QPoint& q : v)
				{
					const double d = (q.x() - cx) * (q.x() - cx) + (q.y() - cy) * (q.y() - cy);
					if (d < bd) { bd = d; best = q; }
				}
				return best;
			};
			pts->first = pick(ha);
			pts->second = pick(hb);
			log("pixels hitting only %ls: %d, only %ls: %d; start %d,%d end %d,%d", a->GetName().c_str(), (int)ha.size(),
				b->GetName().c_str(), (int)hb.size(), pts->first.x(), pts->first.y(), pts->second.x(), pts->second.y());
			check(!ha.empty() && !hb.empty(), "found a pixel on each volume");
		}, false);
		if (kind == "rulerauto")
		{
			// Measurement "Auto-select": current volume = A, clicks on B
			// then A (2-point ruler, Maximum Intensity), the end point moved
			// back onto B in Edit mode, a View Plane locator on B; then
			// Auto-select off: a click on B with A current misses A and
			// lands on the view plane (no volume), A stays current
			// the point picked directly on v under q (Maximum Intensity)
			auto pick_on = [=](VolumeData* v, const QPoint& q) {
				FLIVR::Point p;
				m_ruler_tool->GetPointVolume(p, q.x(), q.y(), v, 1, false);
				return p;
			};
			auto click = [=](const QPoint& q) {
				cv->SimulateMouse(QEvent::MouseButtonPress, q, Qt::LeftButton, Qt::LeftButton);
				cv->SimulateMouse(QEvent::MouseButtonRelease, q, Qt::LeftButton, Qt::NoButton);
			};
			auto log_rulers = [=]() {
				std::vector<Ruler*>* rl = m_ruler_tool->GetRulerList();
				for (Ruler* r : *rl)
					for (int k = 0; k < r->GetNumPoint(); k++)
						log("  ruler %ls type %d point %d (%.3f, %.3f, %.3f) volume '%ls'", r->GetNameDisp().c_str(), r->GetRulerType(), k,
							r->GetPoint(k)->x(), r->GetPoint(k)->y(), r->GetPoint(k)->z(), r->GetPointVolName(k).c_str());
			};
			add("auto-select ruler", [=]() {
				const std::vector<VolumeData*>& vols = m_canvas->Volumes();
				if (vols.size() < 2 || pts->first.x() < 0 || pts->second.x() < 0) return;
				VolumeData* a = vols[0];
				VolumeData* b = vols[1];
				const QString na = a->GetName().ToQt(), nb = b->GetName().ToQt();
				MeasureDialog* dlg = EnsureMeasureDialog();
				dlg->show();
				QCheckBox* chk = dlg->findChild<QCheckBox*>("ruler_auto_select");
				check(chk && !chk->isChecked(), "Auto-select check box present, off by default");
				if (!chk) return;
				chk->setChecked(true);
				check(m_ruler_tool->GetRulerAutoSelect() && m_settings->GetRulerAutoSelect(), "check box -> ruler tool + settings");
				m_ruler_tool->ClearRulers();
				m_ruler_tool->RulersModified();
				m_ruler_tool->SetPointVolumeMode(1);
				m_ruler_tool->SetRulerType(0);
				m_ruler_tool->SetIntMode(5);
				m_canvas->SetCurrentVolume(a);
				m_tree->SelectVolume(a);
				check(m_tree->CurrentVolume() == a && m_canvas->GetCurrentVolume() == a, "current volume = " + na);
				click(pts->second);
				log("after click on B: current %ls, tree %ls", m_canvas->GetCurrentVolume() ? m_canvas->GetCurrentVolume()->GetName().c_str() : L"-",
					m_tree->CurrentVolume() ? m_tree->CurrentVolume()->GetName().c_str() : L"-");
				check(m_canvas->GetCurrentVolume() == b && m_tree->CurrentVolume() == b && m_prop->GetVolume() == b,
					"click on B: B current, selected in the tree, Properties follow");
				click(pts->first);
				check(m_canvas->GetCurrentVolume() == a && m_tree->CurrentVolume() == a && m_prop->GetVolume() == a,
					"click on A: A current, selected in the tree, Properties follow");
				std::vector<Ruler*>* rl = m_ruler_tool->GetRulerList();
				log("rulers: %d", (int)rl->size());
				log_rulers();
				dlg->UpdateList();
				check(rl->size() == 1 && (*rl)[0]->GetNumPoint() == 2, "one 2-point ruler");
				if (rl->size() != 1 || (*rl)[0]->GetNumPoint() != 2) return;
				check(list_cell(0, "Start Volume") == nb, "list: start volume = " + nb);
				check(list_cell(0, "End Volume") == na, "list: end volume = " + na);
				// the same points as picked on B / A directly (Maximum Intensity;
				// the pixels hit only B / only A)
				check(((*(*rl)[0]->GetPoint(0)) - pick_on(b, pts->second)).length() < 1e-9, "start point = Maximum Intensity point on B");
				check(((*(*rl)[0]->GetPoint(1)) - pick_on(a, pts->first)).length() < 1e-9, "end point = Maximum Intensity point on A");
				m_ruler_tool->SetIntMode(1);
				dump_rulers("a01_measure_list_auto.png");
			});
			capture("a02_view_auto");
			add("auto-select edit + view plane", [=]() {
				const std::vector<VolumeData*>& vols = m_canvas->Volumes();
				if (vols.size() < 2 || pts->first.x() < 0 || pts->second.x() < 0) return;
				VolumeData* a = vols[0];
				VolumeData* b = vols[1];
				const QString na = a->GetName().ToQt(), nb = b->GetName().ToQt();
				std::vector<Ruler*>* rl = m_ruler_tool->GetRulerList();
				if (rl->size() != 1) return;
				// Edit: drag the end point (on A) to B
				m_ruler_tool->SetIntMode(6);
				FLIVR::Point* ep = m_ruler_tool->GetEditingRulerPoint(pts->first.x(), pts->first.y());
				check(ep == (*rl)[0]->GetPoint(1), "Edit: the end point is under the A click");
				cv->SimulateMouse(QEvent::MouseButtonPress, pts->first, Qt::LeftButton, Qt::LeftButton);
				const QPoint mid((pts->first.x() + pts->second.x()) / 2, (pts->first.y() + pts->second.y()) / 2);
				cv->SimulateMouse(QEvent::MouseMove, mid, Qt::LeftButton, Qt::LeftButton);
				cv->SimulateMouse(QEvent::MouseMove, pts->second, Qt::LeftButton, Qt::LeftButton);
				cv->SimulateMouse(QEvent::MouseButtonRelease, pts->second, Qt::LeftButton, Qt::NoButton);
				m_ruler_tool->SetIntMode(1);
				log_rulers();
				EnsureMeasureDialog()->UpdateList();
				check(list_cell(0, "End Volume") == nb && ((*(*rl)[0]->GetPoint(1)) - pick_on(b, pts->second)).length() < 1e-9 &&
					m_tree->CurrentVolume() == b, "Edit drag onto B: end volume B, point = Maximum Intensity point on B, B selected");
				// View Plane: a locator clicked on A (B current)
				m_ruler_tool->SetPointVolumeMode(0);
				m_ruler_tool->SetRulerType(2);
				m_ruler_tool->SetIntMode(5);
				click(pts->first);
				m_ruler_tool->SetIntMode(1);
				log_rulers();
				EnsureMeasureDialog()->UpdateList();
				check(rl->size() == 2 && m_tree->CurrentVolume() == a && list_cell(1, "Start Volume") == na,
					"View Plane locator clicked on A: A selected, start volume A");
				FLIVR::Point pp;
				m_ruler_tool->GetPointPlane(pp, pts->first.x(), pts->first.y());
				check(rl->size() == 2 && ((*(*rl)[1]->GetPoint(0)) - pp).length() < 1e-9, "View Plane locator on the view plane");
				// empty space: no volume hit, current volume unchanged
				m_ruler_tool->SetIntMode(5);
				click(QPoint(3, 3));
				m_ruler_tool->SetIntMode(1);
				EnsureMeasureDialog()->UpdateList();
				check(rl->size() == 3 && list_cell(2, "Start Volume").isEmpty() && m_tree->CurrentVolume() == a,
					"empty space: view plane locator, no volume, A stays current");
				// paint ruler (mode 7, a 2+pt ruler painted with Shift held):
				// a stroke on B with A current paints B and adds its centre
				// with volume B
				{
					const size_t n0 = rl->size();
					m_ruler_tool->SetRulerType(1);
					m_ruler_tool->SetIntMode(5);
					m_brush_tool->SetRulerType(1);
					m_brush_tool->SetIntMode(7);
					log("paint ruler: brush mode %d, ruler mode %d", m_brush_tool->GetIntMode(), m_ruler_tool->GetIntMode());
					cv->SimulateMouse(QEvent::MouseButtonPress, pts->second, Qt::LeftButton, Qt::LeftButton);
					check(m_tree->CurrentVolume() == b && m_canvas->GetCurrentVolume() == b, "paint ruler press on B: B selected");
					cv->SimulateMouse(QEvent::MouseMove, pts->second + QPoint(1, 0), Qt::LeftButton, Qt::LeftButton);
					cv->SimulateMouse(QEvent::MouseButtonRelease, pts->second + QPoint(1, 0), Qt::LeftButton, Qt::NoButton);
					m_ruler_tool->FinishRuler();
					m_brush_tool->SetIntMode(1);
					m_ruler_tool->SetIntMode(1);
					log_rulers();
					if (rl->size() > n0)
						check(rl->back()->GetPointVolName(0).ToQt() == nb, "paint ruler point: volume B");
					else
						log("paint ruler: no point added (empty brush selection)");
					// back to A current for the next check
					m_tree->SelectVolume(a);
					m_ruler_tool->SetRulerType(2);
				}
				// Auto-select off: as before
				QCheckBox* chk = EnsureMeasureDialog()->findChild<QCheckBox*>("ruler_auto_select");
				if (chk) chk->setChecked(false);
				check(!m_ruler_tool->GetRulerAutoSelect() && !m_settings->GetRulerAutoSelect(), "Auto-select off");
				m_ruler_tool->SetPointVolumeMode(1);
				m_ruler_tool->SetIntMode(5);
				click(pts->second);
				m_ruler_tool->SetIntMode(1);
				log_rulers();
				EnsureMeasureDialog()->UpdateList();
				check(list_cell((int)rl->size() - 1, "Start Volume").isEmpty() && m_tree->CurrentVolume() == a &&
					m_canvas->GetCurrentVolume() == a, "off: click on B with A current -> view plane, no volume, A stays current");
				dump_rulers("a03_measure_list_auto_off.png");
			});
		}
		else
		add("ruler", [=]() {
			const std::vector<VolumeData*>& vols = m_canvas->Volumes();
			if (vols.size() < 2 || pts->first.x() < 0 || pts->second.x() < 0) return;
			VolumeData* a = vols[0];
			VolumeData* b = vols[1];
			m_ruler_tool->ClearRulers();
			m_ruler_tool->SetPointVolumeMode(1);
			m_ruler_tool->SetRulerType(0);
			m_ruler_tool->SetIntMode(5);
			m_canvas->SetCurrentVolume(a);
			m_tree->SelectVolume(a);
			cv->SimulateMouse(QEvent::MouseButtonPress, pts->first, Qt::LeftButton, Qt::LeftButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, pts->first, Qt::LeftButton, Qt::NoButton);
			m_canvas->SetCurrentVolume(b);
			m_tree->SelectVolume(b);
			cv->SimulateMouse(QEvent::MouseButtonPress, pts->second, Qt::LeftButton, Qt::LeftButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, pts->second, Qt::LeftButton, Qt::NoButton);
			// a third click on the view plane of empty space: a locator with
			// no volume (pointing method falls back to the view plane)
			m_ruler_tool->SetRulerType(2);
			cv->SimulateMouse(QEvent::MouseButtonPress, QPoint(3, 3), Qt::LeftButton, Qt::LeftButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, QPoint(3, 3), Qt::LeftButton, Qt::NoButton);
			m_ruler_tool->SetIntMode(1);
			std::vector<Ruler*>* rl = m_ruler_tool->GetRulerList();
			log("rulers: %d", (int)rl->size());
			for (Ruler* r : *rl)
				for (int k = 0; k < r->GetNumPoint(); k++)
					log("  ruler %ls type %d point %d (%.3f, %.3f, %.3f) volume '%ls'", r->GetNameDisp().c_str(), r->GetRulerType(), k,
						r->GetPoint(k)->x(), r->GetPoint(k)->y(), r->GetPoint(k)->z(), r->GetPointVolName(k).c_str());
			dump_rulers("m01_measure_list.png");
			const QString na = a->GetName().ToQt(), nb = b->GetName().ToQt();
			check(rl->size() == 2 && (*rl)[0]->GetNumPoint() == 2, "one 2-point ruler + one locator");
			check(list_cell(0, "Start Volume") == na, "list: start volume = " + na);
			check(list_cell(0, "End Volume") == nb, "list: end volume = " + nb);
			check(list_cell(1, "Start Volume").isEmpty() && list_cell(1, "End Volume").isEmpty(), "list: locator on the view plane has no volume");
		});
		if (kind == "rulervol")
		capture("m02_view");
		if (kind == "rulervol")
		add("export + save", [=]() {
			const QString txt = st->dir + "/rulers.txt";
			EnsureMeasureDialog()->Export(txt);
			QFile f(txt);
			if (f.open(QIODevice::ReadOnly)) log("text export %s:\n%s", txt.toLocal8Bit().constData(), f.readAll().constData());
			QString prj = qEnvironmentVariable("VVD_QT_SAVE");
			if (prj.isEmpty()) prj = st->dir + "/rulervol.vrp";
			check(SaveProject(prj), "project saved: " + prj);
		}, false);
	}

	if (kind == "rulerlist")
	{
		// the rulers of the opened project (command line .vrp)
		add("list", [=]() {
			std::vector<Ruler*>* rl = m_ruler_tool->GetRulerList();
			log("rulers: %d", (int)rl->size());
			for (Ruler* r : *rl)
				for (int k = 0; k < r->GetNumPoint(); k++)
					log("  ruler %ls type %d point %d volume '%ls'", r->GetNameDisp().c_str(), r->GetRulerType(), k, r->GetPointVolName(k).c_str());
			dump_rulers("m03_measure_list_reopened.png");
			const QString ea = qEnvironmentVariable("VVD_QT_EXPECT_START"), eb = qEnvironmentVariable("VVD_QT_EXPECT_END");
			if (!ea.isEmpty()) check(list_cell(0, "Start Volume") == ea, "reopened list: start volume = " + ea);
			if (!eb.isEmpty()) check(list_cell(0, "End Volume") == eb, "reopened list: end volume = " + eb);
		});
	}

	if (kind == "warp")
	{
		// Measurement "Apply Transform" through the dialog's controls:
		// VVD_QT_WARP_CSV (BigWarp landmarks, Import), VVD_QT_WARP_TTYPE
		// (Transform index), VVD_QT_WARP_LAMBDA (stiffness), VVD_QT_WARP_INTERP
		// (Interpolation index); the result's voxels -> VVD_QT_WARP_OUT (raw)
		add("warp", [=]() {
			MeasureDialog* dlg = EnsureMeasureDialog();
			dlg->show();
			VolumeData* src = CurrentVolume();
			if (!src && !m_canvas->Volumes().empty()) src = m_canvas->Volumes()[0];
			m_canvas->SetCurrentVolume(src);
			m_tree->SelectVolume(src);
			check(m_ruler_tool->ImportBigWarpCSV(qEnvironmentVariable("VVD_QT_WARP_CSV")), "landmarks imported");
			log("source %ls, rulers %d", src->GetName().c_str(), (int)m_ruler_tool->GetRulerList()->size());
			QComboBox* type = nullptr; QComboBox* interp = nullptr; QSlider* stiff = nullptr; QPushButton* apply = nullptr;
			for (QComboBox* c : dlg->findChildren<QComboBox*>())
			{
				if (c->findText("Thin Plate Spline") >= 0) type = c;
				if (c->findText("Nearest Neighbor") >= 0) interp = c;
			}
			stiff = dlg->findChild<QSlider*>();
			for (QPushButton* b : dlg->findChildren<QPushButton*>()) if (b->text() == "Apply Transform") apply = b;
			if (!type || !interp || !stiff || !apply) { check(false, "warp controls found"); return; }
			type->setCurrentIndex(qEnvironmentVariableIntValue("VVD_QT_WARP_TTYPE"));
			interp->setCurrentIndex(qEnvironmentVariableIntValue("VVD_QT_WARP_INTERP"));
			stiff->setValue(int(std::lround(qEnvironmentVariable("VVD_QT_WARP_LAMBDA").toDouble() * 1000.0)));
			log("transform '%s', interpolation '%s', stiffness %.3f (dialog %.3f)", type->currentText().toLocal8Bit().constData(),
				interp->currentText().toLocal8Bit().constData(), stiff->value() / 1000.0, dlg->GetWarpLambda());
			const size_t n0 = m_canvas->Volumes().size();
			apply->click();
			VolumeData* res = nullptr;
			for (VolumeData* v : m_canvas->Volumes())
				if (v != src && v->GetName().ToQt().startsWith(src->GetName().ToQt() + "_")) res = v;
			check(m_canvas->Volumes().size() == n0 + 1 && res, "result volume added");
			if (!res) return;
			Nrrd* d = DataNrrd(res);
			int rx, ry, rz; res->GetResolution(rx, ry, rz);
			const size_t bytes = (size_t)rx * ry * rz * res->GetTexture()->get_nrrd(0)->getBytesPerSample();
			const QString out = qEnvironmentVariable("VVD_QT_WARP_OUT");
			QFile f(out);
			if (d && f.open(QIODevice::WriteOnly)) { f.write((const char*)d->data, (qint64)bytes); f.close(); }
			log("result %ls %dx%dx%d max %g -> %s", res->GetName().c_str(), rx, ry, rz, res->GetMaxValue(), out.toLocal8Bit().constData());
		});
	}

	if (kind == "cells")
	{
		// Measurement "Apply Transform" with "Cells: shape preserving" through
		// the dialog's controls. VVD_QT_CELLS_CSV (BigWarp landmarks, Import);
		// VVD_QT_CELLS_RUNS = "local,objects,stiffness,device,loo,outprefix;..."
		// (local 0 Adaptive / 1 Translation / 2 Rigid / 3 Similarity, objects
		// 0 components / 1 per label, device 0 CPU / 1 first GPU); optional
		// VVD_QT_CELLS_PARAMS = "beta,sigma_min,neurite,samples,min_voxels,radius".
		// Per run: the result's voxels -> <outprefix>.raw (VVD_QT_CELLS_RAW=0:
		// md5 only), the tables -> <outprefix>_objects.csv / _landmarks.csv
		// (the results dialog's Save CSV); VVD_QT_CELLS_GRAB=<dir>: pictures
		// of the Measurement dialog and the results dialog.
		add("cells", [=]() {
			MeasureDialog* dlg = EnsureMeasureDialog();
			dlg->show();
			VolumeData* src = CurrentVolume();
			if (!src && !m_canvas->Volumes().empty()) src = m_canvas->Volumes()[0];
			m_canvas->SetCurrentVolume(src);
			m_tree->SelectVolume(src);
			check(m_ruler_tool->ImportBigWarpCSV(qEnvironmentVariable("VVD_QT_CELLS_CSV")), "landmarks imported");
			log("source %ls, rulers %d", src->GetName().c_str(), (int)m_ruler_tool->GetRulerList()->size());
			QComboBox* type = nullptr; QSlider* stiff = dlg->findChild<QSlider*>(); QPushButton* apply = nullptr;
			for (QComboBox* c : dlg->findChildren<QComboBox*>())
				if (c->findText("Thin Plate Spline") >= 0) type = c;
			for (QPushButton* b : dlg->findChildren<QPushButton*>()) if (b->text() == "Apply Transform") apply = b;
			auto* local = dlg->findChild<QComboBox*>("cell_local");
			auto* objects = dlg->findChild<QComboBox*>("cell_objects");
			auto* device = dlg->findChild<QComboBox*>("cell_device");
			auto* loo = dlg->findChild<QCheckBox*>("cell_loo");
			if (!type || !stiff || !apply || !local || !objects || !device || !loo) { check(false, "cell warp controls found"); return; }
			type->setCurrentIndex(MeasureDialog::WarpCells);
			QWidget* panel = dlg->findChild<QWidget*>("cell_panel");
			check(panel && !panel->isHidden(), "cell options shown for 'Cells: shape preserving'");
			const QStringList prm = qEnvironmentVariable("VVD_QT_CELLS_PARAMS").split(',', Qt::SkipEmptyParts);
			if (prm.size() == 6)
			{
				dlg->findChild<QDoubleSpinBox*>("cell_beta")->setValue(prm[0].toDouble());
				dlg->findChild<QDoubleSpinBox*>("cell_sigma_min")->setValue(prm[1].toDouble());
				dlg->findChild<QDoubleSpinBox*>("cell_neurite")->setValue(prm[2].toDouble());
				dlg->findChild<QSpinBox*>("cell_samples")->setValue(prm[3].toInt());
				dlg->findChild<QSpinBox*>("cell_min_voxels")->setValue(prm[4].toInt());
				dlg->findChild<QDoubleSpinBox*>("cell_radius")->setValue(prm[5].toDouble());
			}
			const QString grab = qEnvironmentVariable("VVD_QT_CELLS_GRAB");
			const bool raw = qEnvironmentVariable("VVD_QT_CELLS_RAW") != "0";
			const QStringList runs = qEnvironmentVariable("VVD_QT_CELLS_RUNS").split(';', Qt::SkipEmptyParts);
			int idx = 0;
			for (const QString& run : runs)
			{
				const QStringList f = run.split(',');
				if (f.size() < 6) { check(false, "run spec: " + run); continue; }
				local->setCurrentIndex(f[0].toInt());
				objects->setCurrentIndex(f[1].toInt());
				stiff->setValue(int(std::lround(f[2].toDouble() * 1000.0)));
				device->setCurrentIndex(std::min(f[3].toInt(), device->count() - 1));
				loo->setChecked(f[4].toInt() != 0);
				const QString out = f[5];
				const rigidcells::Params p = dlg->GetCellParams();
				log("run %d: local '%s', objects '%s', stiffness %.3f, device '%s', loo %d, beta %g, sigma_min %g, neurite %g, samples %d, min voxels %d, radius %g",
					idx, local->currentText().toLocal8Bit().constData(), objects->currentText().toLocal8Bit().constData(), p.stiffness,
					device->currentText().toLocal8Bit().constData(), (int)p.loo, p.beta, p.sigma_min, p.neurite, p.samples, p.min_voxels, p.radius);
				check(stiff->isEnabled(), "stiffness slider enabled");
				if (!grab.isEmpty() && idx == 0)
				{
					dlg->adjustSize();
					dlg->grab().save(grab + "/cells_measure_dialog.png");
					log("saved %s/cells_measure_dialog.png", grab.toLocal8Bit().constData());
				}
				const size_t n0 = m_canvas->Volumes().size();
				QElapsedTimer t; t.start();
				apply->click();
				const qint64 ms = t.elapsed();
				VolumeData* res = nullptr;
				for (VolumeData* v : m_canvas->Volumes())
					if (v != src && v->GetName().ToQt().startsWith(src->GetName().ToQt() + "_CELLS_")) res = v;
				check(m_canvas->Volumes().size() == n0 + 1 && res, "result volume added");
				if (!res) { idx++; continue; }
				Nrrd* d = DataNrrd(res);
				int rx, ry, rz; res->GetResolution(rx, ry, rz);
				const size_t bytes = (size_t)rx * ry * rz * res->GetTexture()->get_nrrd(0)->getBytesPerSample();
				QByteArray md5 = QCryptographicHash::hash(QByteArrayView((const char*)d->data, (qsizetype)bytes), QCryptographicHash::Md5).toHex();
				size_t nonzero = 0;
				for (size_t i = 0; i < bytes; ++i) if (((const unsigned char*)d->data)[i]) nonzero++;
				if (raw) { QFile fo(out + ".raw"); if (fo.open(QIODevice::WriteOnly)) { fo.write((const char*)d->data, (qint64)bytes); fo.close(); } }
				RigidCellsReportDialog* rd = dlg->GetCellReportDialog();
				check(rd && rd->isVisible(), "results dialog shown");
				if (rd)
				{
					check(rd->SaveObjectsCsv(out + "_objects.csv"), "objects table saved");
					if (rd->GetReport().has_landmarks) check(rd->SaveLandmarksCsv(out + "_landmarks.csv"), "landmarks table saved");
					for (const std::string& w : rd->GetReport().warnings) log("  %s", w.c_str());
					if (!grab.isEmpty() && idx == 0)
					{
						rd->grab().save(grab + "/cells_results_dialog.png");
						log("saved %s/cells_results_dialog.png", grab.toLocal8Bit().constData());
					}
				}
				log("RESULT %ls %dx%dx%d bits %d max %g md5 %s nonzero_bytes %zu time %lld ms objects %d",
					res->GetName().c_str(), rx, ry, rz, (int)res->GetTexture()->get_nrrd(0)->getBytesPerSample() * 8, res->GetMaxValue(),
					md5.constData(), nonzero, (long long)ms, rd ? (int)rd->GetReport().objects.size() : -1);
				idx++;
			}
		});
	}

	if (kind == "view3d")
	{
		// batch 2 of the wx parity work: the 3D view (see docs/QT_PORT.md).
		// Load volA.nrrd volB.nrrd mesh.obj (two volumes + a mesh).
		auto pixdiff = [](const QString& fa, const QString& fb) -> long long {
			QImage a(fa), b(fb);
			if (a.isNull() || b.isNull() || a.size() != b.size()) return -1;
			a = a.convertToFormat(QImage::Format_RGB32);
			b = b.convertToFormat(QImage::Format_RGB32);
			long long n = 0;
			for (int y = 0; y < a.height(); y++)
			{
				const QRgb* la = (const QRgb*)a.constScanLine(y);
				const QRgb* lb = (const QRgb*)b.constScanLine(y);
				for (int x = 0; x < a.width(); x++) n += la[x] != lb[x];
			}
			return n;
		};
		auto sendkey = [](QObject* target, QEvent::Type type, int key, Qt::KeyboardModifiers mods, const QString& text, bool rep = false) {
			QKeyEvent ev(type, key, mods, text, rep);
			QCoreApplication::sendEvent(target, &ev);
		};
		auto tap = [sendkey](QObject* target, int key, Qt::KeyboardModifiers mods, const QString& text) {
			sendkey(target, QEvent::KeyPress, key, mods, text);
			sendkey(target, QEvent::KeyRelease, key, mods, text);
		};
		auto zr = [this]() { int lo, hi; m_clip->GetClipRange(2, lo, hi); return QString("%1..%2").arg(lo).arg(hi); };
		auto rng = [this](int a) { int lo, hi; m_clip->GetClipRange(a, lo, hi); return QString("%1..%2").arg(lo).arg(hi); };
		auto vols = std::make_shared<std::vector<VolumeData*>>();
		auto cmp = [st, pixdiff, log, check](const QString& a, const QString& b, bool expect_diff, const QString& what) {
			const long long n = pixdiff(st->dir + "/" + a + ".png", st->dir + "/" + b + ".png");
			log("%s vs %s: %lld pixels differ", a.toLocal8Bit().constData(), b.toLocal8Bit().constData(), n);
			check(expect_diff ? n > 0 : n == 0, what);
		};

		add("setup", [=]() {
			vols->clear();
			for (VolumeData* v : m_canvas->Volumes()) vols->push_back(v);
			check(vols->size() >= 2, "two volumes loaded");
			if (vols->size() < 2) return;
			m_canvas->SetCurrentVolume((*vols)[0]);
			m_tree->SelectVolume((*vols)[0]);
			check(m_clip->GetVolume() == (*vols)[0], "clipping pane shows volume 0");
			log("volume 0 %ls, volume 1 %ls, meshes %d, mem swap %d", (*vols)[0]->GetName().c_str(), (*vols)[1]->GetName().c_str(),
				(int)m_canvas->Meshes().size(), (int)FLIVR::TextureRenderer::get_mem_swap());
		});

		// ---- 0: S / W move the linked clipping planes from any focus
		add("keys s/w", [=]() {
			QWidget* tree = m_tree->Tree();
			QCheckBox* chk = m_clip->findChild<QCheckBox*>();
			QLineEdit* edit = m_view->findChild<QLineEdit*>();
			for (int a = 0; a < 3; a++) m_clip->SetLink(a, false);
			m_clip->SetClipRange(2, 10, 20);
			check(zr() == "10..20", "z range set to 10..20 (" + zr() + ")");
			tap(tree, Qt::Key_S, Qt::NoModifier, "s");
			check(zr() == "10..20", "'s' with no axis linked: nothing moves (wx) -> " + zr());
			m_clip->SetLink(2, true);
			tap(tree, Qt::Key_S, Qt::NoModifier, "s");
			check(zr() == "20..30", "'s' (focus: tree) moves the linked z slab up by its width -> " + zr());
			tap(chk, Qt::Key_W, Qt::NoModifier, "w");
			check(zr() == "10..20", "'w' (focus: a check box of the clipping pane) moves it down -> " + zr());
			sendkey(tree, QEvent::KeyPress, Qt::Key_S, Qt::NoModifier, "s");
			sendkey(tree, QEvent::KeyPress, Qt::Key_S, Qt::NoModifier, "s", true);
			sendkey(tree, QEvent::KeyPress, Qt::Key_S, Qt::NoModifier, "s", true);
			sendkey(tree, QEvent::KeyRelease, Qt::Key_S, Qt::NoModifier, "s");
			check(zr() == "20..30", "'s' held (auto-repeat): one step per press -> " + zr());
			edit->setFocus();
			const QWidget* fw = QApplication::focusWidget();
			tap(edit, Qt::Key_S, Qt::NoModifier, "s");
			check(fw != edit || zr() == "20..30", QString("'s' typed into a text box: no move (focus widget is the box: %1) -> ").arg(fw == edit) + zr());
			tree->setFocus();
			cv->SimulateKey(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier, "w");
			cv->SimulateKey(QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier, "w");
			check(zr() == "10..20", "'w' on the canvas itself -> " + zr());
			for (int i = 0; i < 6; i++) tap(tree, Qt::Key_S, Qt::NoModifier, "s");
			check(zr() == "54..64", "'s' x6 stops at the top (x2 = res, slab kept) -> " + zr());
			for (int i = 0; i < 7; i++) tap(tree, Qt::Key_W, Qt::NoModifier, "w");
			check(zr() == "0..10", "'w' x7 stops at 0 -> " + zr());
			// ',' / '.' on the focused z slider (wx OnSliderKeyDown)
			QWidget* dual = nullptr;
			int nd = 0;
			for (QWidget* w : m_clip->findChildren<QWidget*>())
				if (QString(w->metaObject()->className()) == "DualSlider" && nd++ == 2) dual = w;
			if (dual)
			{
				tap(dual, Qt::Key_Period, Qt::NoModifier, ".");
				check(zr() == "10..20", "'.' on the z slider moves the slab up -> " + zr());
				tap(dual, Qt::Key_Comma, Qt::NoModifier, ",");
				check(zr() == "0..10", "',' on the z slider moves it down -> " + zr());
			}
			else
				check(false, "z slider found");
		});
		add("keys other", [=]() {
			QWidget* tree = m_tree->Tree();
			double x0, y0, z0, x1, y1, z1;
			m_canvas->GetObjTrans(x0, y0, z0);
			tap(tree, Qt::Key_Right, Qt::ControlModifier, "");
			m_canvas->GetObjTrans(x1, y1, z1);
			check(x1 != x0 || y1 != y0 || z1 != z0, QString::asprintf("Ctrl+Right (focus: tree) pans: obj trans %.1f,%.1f,%.1f -> %.1f,%.1f,%.1f", x0, y0, z0, x1, y1, z1));
			tap(tree, Qt::Key_Left, Qt::ControlModifier, "");
			m_canvas->GetObjTrans(x1, y1, z1);
			check(std::fabs(x1 - x0) + std::fabs(y1 - y0) + std::fabs(z1 - z0) < 1e-9, "Ctrl+Left pans back");
			sendkey(tree, QEvent::KeyPress, Qt::Key_V, Qt::NoModifier, "v");
			const bool hidden = !m_canvas->GetDrawMask();
			sendkey(tree, QEvent::KeyRelease, Qt::Key_V, Qt::NoModifier, "v");
			check(hidden && m_canvas->GetDrawMask(), "V held (focus: tree) hides the masks, release shows them");
			m_canvas->SetRotations(30.0, 0.0, 0.0);
			cv->SimulateKey(QEvent::KeyPress, Qt::Key_R, Qt::NoModifier, "r");
			cv->SimulateKey(QEvent::KeyRelease, Qt::Key_R, Qt::NoModifier, "r");
			double rx, ry, rz;
			m_canvas->GetRotations(rx, ry, rz);
			check(std::fabs(rx - 30.0) < 1e-6, QString::asprintf("'r' on the canvas does nothing (wx has no R key): rotation x %.1f", rx));
			m_canvas->SetRotations(0.0, 0.0, 0.0);
			// Alt+V / Ctrl+B: once per press
			auto nalt = std::make_shared<int>(0), nb = std::make_shared<int>(0);
			auto c1 = connect(m_canvas, &RenderCanvas::copyLevelRequested, this, [nalt]() { (*nalt)++; });
			auto c2 = connect(m_canvas, &RenderCanvas::warpRequested, this, [nb]() { (*nb)++; });
			sendkey(tree, QEvent::KeyPress, Qt::Key_V, Qt::AltModifier, "v");
			sendkey(tree, QEvent::KeyPress, Qt::Key_V, Qt::AltModifier, "v", true);
			sendkey(tree, QEvent::KeyRelease, Qt::Key_V, Qt::AltModifier, "v");
			sendkey(tree, QEvent::KeyPress, Qt::Key_V, Qt::AltModifier, "v");
			sendkey(tree, QEvent::KeyRelease, Qt::Key_V, Qt::AltModifier, "v");
			check(*nalt == 2, QString("Alt+V: copyLevelRequested once per press (%1 for 2 presses)").arg(*nalt));
			// Ctrl+B runs the Measurement dialog's Apply Transform; with 4
			// 2-point rulers (+5 voxels in x) it adds a warped volume
			VolumeData* a = (*vols)[0];
			m_canvas->SetCurrentVolume(a);
			m_ruler_tool->ClearRulers();
			const double pts[4][3] = { { 30, 40, 20 }, { 90, 50, 40 }, { 60, 100, 30 }, { 64, 64, 55 } };
			for (const auto& q : pts)
			{
				Ruler* r = new Ruler();
				r->SetRulerType(0);
				r->AddPoint(FLIVR::Point(q[0], q[1], q[2]));
				r->AddPoint(FLIVR::Point(q[0] + 5, q[1], q[2]));
				m_ruler_tool->GetRulerList()->push_back(r);
			}
			m_ruler_tool->RulersModified();
			const size_t n0 = m_canvas->Volumes().size();
			// a message box would block: close any modal dialog that opens
			QTimer* closer = new QTimer(this);
			connect(closer, &QTimer::timeout, this, [log]() {
				if (QWidget* m = QApplication::activeModalWidget())
				{
					if (QMessageBox* mb = qobject_cast<QMessageBox*>(m)) log("message box: %s", mb->text().toLocal8Bit().constData());
					m->close();
				}
			});
			closer->start(200);
			sendkey(tree, QEvent::KeyPress, Qt::Key_B, Qt::ControlModifier, "b");
			sendkey(tree, QEvent::KeyPress, Qt::Key_B, Qt::ControlModifier, "b", true);
			sendkey(tree, QEvent::KeyRelease, Qt::Key_B, Qt::ControlModifier, "b");
			closer->stop();
			closer->deleteLater();
			const size_t n1 = m_canvas->Volumes().size();
			log("Ctrl+B: warpRequested %d, volumes %d -> %d%s", *nb, (int)n0, (int)n1,
				n1 > n0 ? QString(" (" + m_canvas->Volumes().back()->GetName().ToQt() + ")").toLocal8Bit().constData() : "");
			check(*nb == 1 && n1 == n0 + 1, "Ctrl+B: one Apply Transform (warped volume added)");
			if (n1 > n0)
			{
				VolumeData* w = m_canvas->Volumes().back();
				m_canvas->RemoveVolume(w);
				m_data_mgr->RemoveVolumeData(w->GetName());
				RefreshTree();
			}
			m_ruler_tool->ClearRulers();
			disconnect(c1);
			disconnect(c2);
			m_canvas->SetCurrentVolume(a);
			m_tree->SelectVolume(a);
		});

		// ---- 11: right click on a clip slider = 1-voxel slab
		add("rclick", [=]() {
			for (int a = 0; a < 3; a++) m_clip->SetLink(a, false);
			m_clip->SetClipRange(2, 10, 64);
			m_clip->SetClipRange(1, 5, 100);
			m_clip->OnSliderRClick(2, 0);
			check(zr() == "10..11" && m_clip->GetLink(2), "right click on z1: z = 10..11, linked -> " + zr());
			check(rng(1) == "0..128" && !m_clip->GetLink(1) && rng(0) == "0..128" && !m_clip->GetLink(0), "x and y reset to full range, unlinked -> y " + rng(1));
			tap(m_tree->Tree(), Qt::Key_S, Qt::NoModifier, "s");
			check(zr() == "11..12", "'s' then steps the slab by one voxel -> " + zr());
			m_clip->OnSliderRClick(0, 1);
			check(rng(0) == "127..128" && m_clip->GetLink(0) && zr() == "0..64" && !m_clip->GetLink(2), "right click on x2: x = 127..128 linked, z reset -> x " + rng(0) + ", z " + zr());
			for (int a = 0; a < 3; a++) m_clip->SetLink(a, false);
			m_clip->SetClipRange(0, 0, 128);
			check(rng(0) == "0..128", "x back to full range");
		});

		// ---- 1: volume shadows in all blend modes, shadow direction
		const int methods[3] = { RenderCanvas::VolMethodSeq, RenderCanvas::VolMethodMulti, RenderCanvas::VolMethodComp };
		for (int mi = 0; mi < 3; mi++)
		{
			const int m = methods[mi];
			for (int mip = 0; mip < 2; mip++)
			{
				const QString tag = QString("sh_m%1%2").arg(m).arg(mip ? "_mip" : "");
				add("shadow off " + tag, [=]() {
					m_canvas->SetVolMethod(m);
					for (VolumeData* v : *vols) { v->SetShadow(false); v->SetMode(mip ? 1 : 0); }
					m_canvas->SetShadowDir(0.0, 0.0);
					m_canvas->RefreshView();
				});
				capture(tag + "_off");
				add("shadow on " + tag, [=]() {
					for (VolumeData* v : *vols) { v->SetShadow(true); v->SetShadowParams(0.8); }
					m_canvas->RefreshView();
				});
				capture(tag + "_on");
				add("compare " + tag, [=]() { cmp(tag + "_off", tag + "_on", true, "shadow visible: " + tag); }, false);
				if (mip == 0)
				{
					add("shadow dir " + tag, [=]() {
						const double r = -45.0 * 3.14159265358979323846 / 180.0;
						m_canvas->SetShadowDir(std::cos(r), std::sin(r));
					});
					capture(tag + "_dir");
					add("compare dir " + tag, [=]() { cmp(tag + "_on", tag + "_dir", true, "shadow direction -45 deg changes the shadow: " + tag); }, false);
				}
				add("shadow off again " + tag, [=]() {
					for (VolumeData* v : *vols) { v->SetShadow(false); v->SetMode(0); }
					m_canvas->SetShadowDir(0.0, 0.0);
					m_canvas->RefreshView();
				});
				if (mip == 0)
				{
					capture(tag + "_off2");
					add("compare off " + tag, [=]() { cmp(tag + "_off", tag + "_off2", false, "shadow off again: identical to before: " + tag); }, false);
				}
			}
		}
		// mesh shadow direction (DrawOLShadowsMesh)
		add("mesh shadow", [=]() {
			m_canvas->SetVolMethod(RenderCanvas::VolMethodSeq);
			for (MeshData* md : m_canvas->Meshes()) { md->SetShadow(true); md->SetShadowParams(0.8); }
			m_canvas->SetShadowDir(0.0, 0.0);
			m_canvas->RefreshView();
		});
		capture("msh_dir0");
		add("mesh shadow dir", [=]() {
			const double r = 135.0 * 3.14159265358979323846 / 180.0;
			m_canvas->SetShadowDir(std::cos(r), std::sin(r));
		});
		capture("msh_dir135");
		add("compare mesh dir", [=]() {
			cmp("msh_dir0", "msh_dir135", true, "mesh shadow follows the Settings shadow direction");
			for (MeshData* md : m_canvas->Meshes()) md->SetShadow(false);
			m_canvas->SetShadowDir(0.0, 0.0);
			m_canvas->RefreshView();
		}, false);

		// ---- 2: gradient background
		capture("bg_plain");
		add("grad bg on", [=]() { m_canvas->SetGradBg(true); });
		capture("bg_grad");
		add("grad bg check", [=]() {
			QImage img(st->dir + "/bg_grad.png");
			if (img.isNull()) { check(false, "bg_grad.png"); return; }
			const QRgb top = img.pixel(5, 2), mid = img.pixel(5, int(img.height() * 0.4)), bot = img.pixel(5, img.height() - 3);
			log("gradient background (black): top %d,%d,%d  40%%: %d,%d,%d  bottom %d,%d,%d", qRed(top), qGreen(top), qBlue(top),
				qRed(mid), qGreen(mid), qBlue(mid), qRed(bot), qGreen(bot), qBlue(bot));
			check(top != mid && qRed(mid) > 100, "gradient background drawn (bright band in the middle)");
			m_canvas->SetGradBg(false);
			m_canvas->RefreshView();
		}, false);
		capture("bg_plain2");
		add("grad bg off", [=]() { cmp("bg_plain", "bg_plain2", false, "gradient off: identical to before"); }, false);

		// ---- 4: Intrp. is view-wide
		add("intp off", [=]() {
			QCheckBox* intp = nullptr;
			for (QCheckBox* c : m_view->findChildren<QCheckBox*>()) if (c->text() == "Intrp.") intp = c;
			check(intp != nullptr, "Intrp. check box");
			if (intp) intp->setChecked(false);
			m_canvas->SetScaleFactor(4.0);
		});
		capture("intp_off");
		add("intp check", [=]() {
			bool all_off = true;
			for (VolumeData* v : *vols) all_off &= !v->GetInterpolate();
			check(!m_canvas->GetIntp() && all_off, "Intrp. off: every shown volume set to nearest (StartLoopUpdate)");
			for (QCheckBox* c : m_view->findChildren<QCheckBox*>()) if (c->text() == "Intrp.") c->setChecked(true);
		});
		capture("intp_on");
		add("intp compare", [=]() {
			bool all_on = true;
			for (VolumeData* v : *vols) all_on &= v->GetInterpolate();
			check(m_canvas->GetIntp() && all_on, "Intrp. on again for every volume");
			cmp("intp_off", "intp_on", true, "nearest vs linear interpolation differ");
			m_canvas->SetScaleFactor(1.0);
		}, false);

		// ---- 5: slice mode (macOS) for a slab of <= 3 voxels
		auto saved_provider = std::make_shared<std::function<RenderCanvas::ClipSlabInfo()>>();
		add("slab", [=]() {
			(*vols)[1]->SetDisp(false);
			for (MeshData* md : m_canvas->Meshes()) md->SetDisp(false);
			m_canvas->SetVolMethod(RenderCanvas::VolMethodSeq);
			m_clip->SetClipRange(2, 30, 32);
			log("slab z %s, dist %d", zr().toLocal8Bit().constData(), m_clip->GetDist(2));
			m_canvas->RefreshView();
		});
		capture("slab_slice");
		add("slab no slice", [=]() {
			*saved_provider = m_canvas->GetClipSlabProvider();
			m_canvas->SetClipSlabProvider(nullptr);
			m_canvas->RefreshView();
		});
		capture("slab_noslice");
		add("slab compare", [=]() {
#ifdef _DARWIN
			cmp("slab_noslice", "slab_slice", true, "3-voxel slab drawn in slice mode (differs from the normal ray cast)");
#endif
			m_canvas->SetClipSlabProvider(*saved_provider);
			m_clip->SetClipRange(2, 0, 64);
			(*vols)[1]->SetDisp(true);
			for (MeshData* md : m_canvas->Meshes()) md->SetDisp(true);
			m_canvas->RefreshView();
		});

		// ---- 6: clipping planes while hovering the V. AOV slider
		add("aov hover", [=]() {
			QSlider* s = m_view->AovSlider();
			m_view->SetCursorOverride(true, s->mapToGlobal(s->rect().center()));
			m_view->PollAovHover();
			check(m_canvas->GetDrawClip() && m_canvas->GetClipMask() == -1, "pointer over V. AOV: clipping planes shown");
			m_canvas->RefreshView();
		});
		capture("aov_hover");
		add("aov leave", [=]() {
			cmp("bg_plain", "aov_hover", true, "planes visible while hovering");
			m_view->SetCursorOverride(true, m_view->AovSlider()->mapToGlobal(QPoint(-400, -400)));
			m_view->PollAovHover();
			check(!m_canvas->GetDrawClip(), "pointer off the slider: planes hidden");
			m_view->SetCursorOverride(false);
		});

		// ---- 7: mouse_focus
		add("mouse focus", [=]() {
			QPushButton* one = nullptr;
			for (QPushButton* b : m_view->findChildren<QPushButton*>()) if (b->text() == "1:1") one = b;
			if (!one) { check(false, "1:1 button"); return; }
			QWidget* tree = m_tree->Tree();
			tree->setFocus();
			m_canvas->SetMouseFocus(false);
			one->click();
			QWidget* f1 = tree->window()->focusWidget();
			m_canvas->SetMouseFocus(true);
			one->click();
			QWidget* f2 = tree->window()->focusWidget();
			const bool on_canvas = f2 && (f2 == cv || cv->isAncestorOf(f2));
			check(f1 == tree, "mouse_focus off: 1:1 leaves the focus where it was");
			check(on_canvas, "mouse_focus on: 1:1 gives the canvas the focus");
			m_canvas->SetMouseFocus(false);
			m_canvas->SetScaleFactor(1.0);
		});

		// ---- 9: annotation layer with a mesh
		add("annotation mesh", [=]() {
			if (m_canvas->Meshes().empty()) { check(false, "a mesh is loaded"); return; }
			MeshData* md = m_canvas->Meshes()[0];
			md->SetDisp(false);
			m_canvas->RefreshView();
		});
		capture("ann_before");
		add("annotation load", [=]() {
			MeshData* md = m_canvas->Meshes()[0];
			const QString f = st->dir + "/ann_mesh.txt";
			QFile file(f);
			if (file.open(QIODevice::WriteOnly | QIODevice::Text))
			{
				file.write(QString("Name: ann_mesh\nDisplay: 1\nMesh: %1\nAlpha: 1\n").arg(md->GetName().ToQt()).toUtf8());
				file.close();
			}
			Annotations* ann = m_data_mgr->LoadAnnotations(VVDString(f));
			check(ann && ann->GetMesh() == md, "annotation file with \"Mesh: " + md->GetName().ToQt() + "\" carries the mesh");
			if (ann) { m_canvas->AddAnnotations(ann); RefreshTree(); }
			m_canvas->RefreshView();
		});
		capture("ann_mesh");
		add("annotation compare", [=]() {
			cmp("ann_before", "ann_mesh", true, "annotation layer drawn as its (hidden) mesh");
			for (int i = 0; i < m_canvas->GetLayerNum(); i++)
				if (TreeLayer* l = m_canvas->GetLayer(i); l && l->IsA() == 4) { m_canvas->RemoveAnnotations((Annotations*)l); break; }
			m_canvas->Meshes()[0]->SetDisp(true);
			RefreshTree();
			m_canvas->RefreshView();
		});

		// ---- 10: Settings "Link all rendering views' rotations"
		add("settings rot link", [=]() {
			QCheckBox* link = nullptr;
			for (QCheckBox* c : m_settings->findChildren<QCheckBox*>()) if (c->text().startsWith("Link all rendering views")) link = c;
			if (!link) { check(false, "Settings link check box"); return; }
			link->setChecked(true);
			check(m_canvas->GetLinkedRot(), "Settings link on: the view's rotation link is on");
			link->setChecked(false);
			check(!m_canvas->GetLinkedRot(), "Settings link off: off");
		});

		// ---- 3: double click on a ruler point = animated move
		auto manip = std::make_shared<std::pair<FLIVR::Point, int>>();
		auto manip_frames = std::make_shared<std::pair<long long, int>>(0, 0);
		add("ruler point", [=]() {
			m_ruler_tool->ClearRulers();
			// a locator placed by a click (Maximum Intensity) on the second blob of
			// volA, as the Measurement tool does
			QPoint at(-1, -1);
			double best = -1.0;
			for (int y = 0; y < cv->height(); y += 3)
				for (int x = 0; x < cv->width(); x += 3)
				{
					FLIVR::Point p;
					if (m_ruler_tool->GetPointVolume(p, x, y, (*vols)[0], 1, false) > 0.0)
					{
						const double d = std::fabs(p.x() - 85) + std::fabs(std::fabs(p.y()) - 80);
						if (best < 0 || d < best) { best = d; at = QPoint(x, y); }
					}
				}
			m_ruler_tool->SetPointVolumeMode(1);
			m_ruler_tool->SetRulerType(2);
			m_ruler_tool->SetIntMode(5);
			m_canvas->SetCurrentVolume((*vols)[0]);
			cv->SimulateMouse(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
			m_ruler_tool->SetIntMode(1);
			Ruler* r = m_ruler_tool->GetLastRuler();
			check(r && r->GetNumPoint() == 1, QString("locator placed at %1,%2").arg(at.x()).arg(at.y()));
			if (r) log("locator point %.2f, %.2f, %.2f", r->GetPoint(0)->x(), r->GetPoint(0)->y(), r->GetPoint(0)->z());
			m_canvas->RefreshView();
		});
		capture("manip_before");
		add("double click point", [=]() {
			// a pixel where the ruler point is picked (RulerTool::GetEditingRulerPoint)
			QPoint wp(-1, -1);
			for (int y = 0; y < cv->height() && wp.x() < 0; y += 2)
				for (int x = 0; x < cv->width(); x += 2)
					if (m_ruler_tool->GetEditingRulerPoint(x, y)) { wp = QPoint(x, y); break; }
			check(wp.x() >= 0, "the ruler point can be picked on screen");
			double ox, oy, oz;
			m_canvas->GetObjCenter(ox, oy, oz);
			Ruler* r = m_ruler_tool->GetLastRuler();
			if (!r) return;
			const FLIVR::Point pt = *r->GetPoint(0);
			manip->first = FLIVR::Point(ox - pt.x(), -(oy - pt.y()), -(oz - pt.z()));
			manip_frames->first = m_canvas->PresentedFrames();
			cv->SimulateMouse(QEvent::MouseButtonDblClick, wp, Qt::LeftButton, Qt::LeftButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, wp, Qt::LeftButton, Qt::NoButton);
			check(m_canvas->IsManipulating(), QString("double click on the ruler point (%1,%2) starts the animated move").arg(wp.x()).arg(wp.y()));
			manip->second = 0;
		}, false);
		auto wait_fn = std::make_shared<std::function<void()>>();
		*wait_fn = [=]() {
			if (m_canvas->IsManipulating() && manip->second < 400)
			{
				manip->second++;
				m_canvas->RefreshView();
				st->steps.push_front({ "wait manip", *wait_fn, false });
				return;
			}
			double x, y, z;
			m_canvas->GetObjTrans(x, y, z);
			log("presented frames during the move: %lld", m_canvas->PresentedFrames() - manip_frames->first);
			log("move finished after %d ticks: obj trans %.2f,%.2f,%.2f (target %.2f,%.2f,%.2f)", manip->second, x, y, z,
				manip->first.x(), manip->first.y(), manip->first.z());
			check(!m_canvas->IsManipulating() && std::fabs(x - manip->first.x()) < 1e-6 && std::fabs(y - manip->first.y()) < 1e-6 &&
				std::fabs(z - manip->first.z()) < 1e-6, "animated move ends at the point (object translation)");
		};
		add("wait manip", [=]() { (*wait_fn)(); }, false);
		capture("manip_after");
		add("manip compare", [=]() {
			cmp("manip_before", "manip_after", true, "the view moved");
			// where the point is now (centre of the view?)
			QPoint wp(-1, -1);
			for (int y = 0; y < cv->height() && wp.x() < 0; y += 2)
				for (int x = 0; x < cv->width(); x += 2)
					if (m_ruler_tool->GetEditingRulerPoint(x, y)) { wp = QPoint(x, y); break; }
			log("ruler point picked at %d,%d after the move (view %dx%d)", wp.x(), wp.y(), cv->width(), cv->height());
			m_ruler_tool->ClearRulers();
			m_canvas->SetObjTrans(0, 0, 0);
		}, false);
	}

	if (kind == "na")
	{
		// NA mode (wx SelLabelSegVolumeMax / GetPointAndLabelMax): load a
		// volume with its .lbl (e.g. ball.nrrd + ball.lbl)
		auto ids = std::make_shared<std::vector<int>>();
		auto at = std::make_shared<std::pair<QPoint, int>>(QPoint(-1, -1), 0);
		add("na setup", [=]() {
			VolumeData* vd = cur();
			if (!vd || !vd->GetLabel(false)) { check(false, "volume with a label"); return; }
			vd->SetNAMode(true);
			Nrrd* lbl = vd->GetLabel(false)->getNrrd();
			std::set<int> found;
			const long long n = VoxelCount(vd);
			for (long long i = 0; i < n; i++)
			{
				const int v = lbl->type == nrrdTypeUInt ? ((const unsigned int*)lbl->data)[i] :
					lbl->type == nrrdTypeUShort ? ((const unsigned short*)lbl->data)[i] : ((const unsigned char*)lbl->data)[i];
				if (v > 0) found.insert(v);
			}
			ids->assign(found.begin(), found.end());
			for (int id : *ids) vd->SetSegmentMask(id, 1);
			log("NA mode on %ls: %d labels", vd->GetName().c_str(), (int)ids->size());
			m_canvas->RefreshView();
		});
		capture("na00");
		add("na find", [=]() {
			VolumeData* vd = cur();
			if (ids->empty()) return;
			std::vector<VolumeData*> ref = { vd };
			for (int y = cv->height() / 4; y < cv->height() * 3 / 4 && at->second == 0; y += 4)
				for (int x = cv->width() / 4; x < cv->width() * 3 / 4; x += 4)
				{
					FLIVR::Point p;
					int lv = 0;
					if (m_ruler_tool->GetPointAndLabelMax(p, lv, x, y, vd, ref) > 0.0 && lv > 0) { *at = { QPoint(x, y), lv }; break; }
				}
			log("label %d under %d,%d", at->second, at->first.x(), at->first.y());
			check(at->second > 0, "GetPointAndLabelMax found a label");
		}, false);
		add("na dblclick", [=]() {
			VolumeData* vd = cur();
			if (at->second <= 0) return;
			cv->SimulateMouse(QEvent::MouseButtonDblClick, at->first, Qt::LeftButton, Qt::LeftButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, at->first, Qt::LeftButton, Qt::NoButton);
			check(vd->GetSegmentMask(at->second) == 2, "double click highlights the segment (1 -> 2)");
		});
		capture("na01_highlight");
		add("na dblclick 2", [=]() {
			VolumeData* vd = cur();
			if (at->second <= 0) return;
			cv->SimulateMouse(QEvent::MouseButtonDblClick, at->first, Qt::LeftButton, Qt::LeftButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, at->first, Qt::LeftButton, Qt::NoButton);
			check(vd->GetSegmentMask(at->second) == 1, "second double click: back to shown (2 -> 1)");
		});
		add("na mode 1", [=]() {
			VolumeData* vd = cur();
			if (at->second <= 0 || ids->size() < 1) return;
			for (int id : *ids) vd->SetSegmentMask(id, 1);
			if (ids->size() > 1) vd->SetSegmentMask(ids->front() == at->second ? ids->back() : ids->front(), 2);
			// wx mode 1 loops forever here (iterator never advanced); the port returns
			const bool r = SelLabelSegVolumeMax(1, { vd }, at->first);
			int twos = 0;
			for (int id : *ids) twos += vd->GetSegmentMask(id) == 2;
			check(r && vd->GetSegmentMask(at->second) == 2 && twos == 1, QString("mode 1 returns and leaves only the picked segment highlighted (%1 highlighted)").arg(twos));
		});
	}

	if (kind == "warptarget")
	{
		// Measurement "Target:" (output grid) with the rulers of the opened
		// project. VVD_QT_WT_RUNS = "src,target,ttype,interp,stiffness,tag;..."
		// (src: volume index or name; target: auto | same | index | name;
		// ttype 0..4 as the Transform list, 5 = Cells with the saved / default
		// options, VVD_QT_WT_CELLDEV = device index); VVD_QT_WT_GRAB=1:
		// <tag>_before.png (source + target) and <tag>_after.png (target +
		// result) of the 3D view; VVD_QT_WT_RAW=1: <tag>.raw. Logs the volumes'
		// grids / world boxes, each result's grid, nonzero voxels and their
		// bounding box, md5, and per ruler the distance from the end (fixed)
		// point to the nearest nonzero result voxel next to the distance from
		// the start (moving) point to the nearest nonzero source voxel.
		struct WT { QStringList runs; int idx = 0; VolumeData* src = nullptr; VolumeData* res = nullptr; VolumeData* tgt = nullptr; };
		auto wt = std::make_shared<WT>();
		wt->runs = qEnvironmentVariable("VVD_QT_WT_RUNS").split(';', Qt::SkipEmptyParts);
		const bool grab = qEnvironmentVariableIntValue("VVD_QT_WT_GRAB") != 0;
		const bool raw = qEnvironmentVariableIntValue("VVD_QT_WT_RAW") != 0;
		auto vol_of = [this](const QString& spec) -> VolumeData* {
			bool ok = false;
			int i = spec.toInt(&ok);
			const std::vector<VolumeData*>& vols = m_canvas->Volumes();
			if (ok) return (i >= 0 && i < (int)vols.size()) ? vols[i] : nullptr;
			for (VolumeData* v : vols) if (v->GetName().ToQt() == spec) return v;
			return nullptr;
		};
		auto world_box = [](VolumeData* v) {
			FLIVR::Transform* tf = v->GetTexture()->transform();
			FLIVR::Point a = tf->project(FLIVR::Point(0, 0, 0)), b = tf->project(FLIVR::Point(1, 1, 1));
			return QString::asprintf("world (%.4f, %.4f, %.4f) - (%.4f, %.4f, %.4f)", a.x(), a.y(), a.z(), b.x(), b.y(), b.z());
		};
		auto describe = [log, world_box](const char* what, VolumeData* v) {
			int rx, ry, rz; double sx, sy, sz;
			v->GetResolution(rx, ry, rz); v->GetSpacings(sx, sy, sz);
			log("%s %ls: %dx%dx%d spacing %.6f %.6f %.6f bits %d, physical box %.4f x %.4f x %.4f, %s", what, v->GetName().c_str(),
				rx, ry, rz, sx, sy, sz, v->GetTexture()->nb(0) * 8, rx * sx, ry * sy, rz * sz, world_box(v).toLocal8Bit().constData());
		};
		// distance (physical) from world point p to the nearest nonzero voxel
		// of v (voxel centres at (i + 0.5) * spacing, as the GPU warp); -1:
		// none within r voxels. inside: p's own voxel index lies in the grid
		auto nearest = [](VolumeData* v, const FLIVR::Point& p, int r, bool* inside, double* val) -> double {
			Nrrd* d = DataNrrd(v);
			int rx, ry, rz; double sx, sy, sz;
			v->GetResolution(rx, ry, rz); v->GetSpacings(sx, sy, sz);
			FLIVR::Point n = v->GetTexture()->transform()->unproject(p);
			const double fx = n.x() * rx - 0.5, fy = n.y() * ry - 0.5, fz = n.z() * rz - 0.5;
			const int cx = (int)std::lround(fx), cy = (int)std::lround(fy), cz = (int)std::lround(fz);
			if (inside) *inside = cx >= 0 && cy >= 0 && cz >= 0 && cx < rx && cy < ry && cz < rz;
			if (val) *val = (*inside) ? Val(d, ((long long)cz * ry + cy) * rx + cx) : -1;
			double best = -1;
			for (int z = std::max(0, cz - r); z <= std::min(rz - 1, cz + r); z++)
			for (int y = std::max(0, cy - r); y <= std::min(ry - 1, cy + r); y++)
			for (int x = std::max(0, cx - r); x <= std::min(rx - 1, cx + r); x++)
			{
				if (Val(d, ((long long)z * ry + y) * rx + x) == 0.0) continue;
				const double dx = (x - fx) * sx, dy = (y - fy) * sy, dz = (z - fz) * sz;
				const double dd = std::sqrt(dx * dx + dy * dy + dz * dz);
				if (best < 0 || dd < best) best = dd;
			}
			return best;
		};
		add("wt volumes", [=]() {
			for (VolumeData* v : m_canvas->Volumes()) describe("volume", v);
			std::vector<Ruler*>* rl = m_ruler_tool->GetRulerList();
			log("rulers: %d", (int)rl->size());
			for (Ruler* r : *rl)
				for (int k = 0; k < r->GetNumPoint(); k++)
				{
					FLIVR::Point p = *r->GetPoint(k);
					QString in;
					for (VolumeData* v : m_canvas->Volumes())
					{
						FLIVR::Point n = v->GetTexture()->transform()->unproject(p);
						const bool ins = n.x() >= 0 && n.x() <= 1 && n.y() >= 0 && n.y() <= 1 && n.z() >= 0 && n.z() <= 1;
						in += QString(" | in %1: %2 (normalized %3, %4, %5)").arg(v->GetName().ToQt()).arg(ins ? "yes" : "NO")
							.arg(n.x(), 0, 'f', 4).arg(n.y(), 0, 'f', 4).arg(n.z(), 0, 'f', 4);
					}
					log("  ruler %ls point %d world (%.4f, %.4f, %.4f) physical (%.4f, %.4f, %.4f) volume '%ls'%s", r->GetNameDisp().c_str(), k,
						p.x(), p.y(), p.z(), p.x(), p.y(), -p.z(), r->GetPointVolName(k).c_str(), in.toLocal8Bit().constData());
				}
		});
		for (int ri = 0; ri < wt->runs.size(); ri++)
		{
			const QStringList f = wt->runs[ri].split(',');
			if (f.size() < 6) continue;
			const QString tag = f[5];
			add("wt setup " + tag, [=]() {
				for (VolumeData* v : m_canvas->Volumes()) v->SetDisp(false);
				wt->src = vol_of(f[0]);
				if (!wt->src) { check(false, "source " + f[0]); return; }
				MeasureDialog* dlg = EnsureMeasureDialog();
				dlg->show();
				m_canvas->SetCurrentVolume(wt->src);
				RefreshTree();
				m_tree->SelectVolume(wt->src);
				wt->src->SetDisp(true);
				QComboBox* tc = dlg->findChild<QComboBox*>("warp_target");
				// every volume of the project is shown for the Target list
				for (VolumeData* v : m_canvas->Volumes())
					if (!v->GetName().ToQt().contains("_WARPED") && !v->GetName().ToQt().contains("_CELLS") &&
						!v->GetName().ToQt().contains("_AFFINE") && !v->GetName().ToQt().contains("_SIM") &&
						!v->GetName().ToQt().contains("_RIGID") && !v->GetName().ToQt().contains("_TRANS"))
						v->SetDisp(true);
				dlg->UpdateTargetCombo();
				if (!tc) { check(false, "Target combo found"); return; }
				QStringList items;
				for (int i = 0; i < tc->count(); i++) items << tc->itemText(i) + " [" + tc->itemData(i).toString() + "]";
				log("run %s: source %ls, Target list: %s", tag.toLocal8Bit().constData(), wt->src->GetName().c_str(),
					items.join(" | ").toLocal8Bit().constData());
				QString data = f[1];
				if (data != "auto" && data != "same")
				{
					VolumeData* t = vol_of(f[1]);
					data = t ? "vol:" + t->GetName().ToQt() : "?";
				}
				const int ti = tc->findData(data);
				check(ti >= 0, "target choice " + data);
				if (ti >= 0) { tc->setCurrentIndex(ti); Q_EMIT tc->activated(ti); }
				if (grab && ri == 0)
				{
					dlg->adjustSize();
					dlg->grab().save(st->dir + "/measure_dialog_target.png");
				}
				wt->tgt = dlg->GetWarpTarget();
				log("run %s: Target '%s' -> %ls%s", tag.toLocal8Bit().constData(), tc->currentText().toLocal8Bit().constData(),
					wt->tgt ? wt->tgt->GetName().c_str() : L"(source grid)",
					WarpRulersReversed(wt->src, wt->tgt, *m_ruler_tool->GetRulerList()) ? " (rulers reversed: end points = moving)" : "");
				// before picture: source + target only
				for (VolumeData* v : m_canvas->Volumes()) v->SetDisp(v == wt->src || v == wt->tgt);
				m_canvas->RefreshView();
			});
			if (grab) capture(tag + "_before");
			add("wt apply " + tag, [=]() {
				if (!wt->src) return;
				MeasureDialog* dlg = EnsureMeasureDialog();
				// the Target list needs the target shown
				QComboBox* type = nullptr; QComboBox* interp = nullptr; QSlider* stiff = dlg->findChild<QSlider*>(); QPushButton* apply = nullptr;
				for (QComboBox* c : dlg->findChildren<QComboBox*>())
				{
					if (c->findText("Thin Plate Spline") >= 0) type = c;
					if (c->findText("Nearest Neighbor") >= 0) interp = c;
				}
				for (QPushButton* b : dlg->findChildren<QPushButton*>()) if (b->text() == "Apply Transform") apply = b;
				if (!type || !interp || !stiff || !apply) { check(false, "warp controls found"); return; }
				type->setCurrentIndex(f[2].toInt());
				interp->setCurrentIndex(f[3].toInt());
				stiff->setValue(int(std::lround(f[4].toDouble() * 1000.0)));
				if (f[2].toInt() == MeasureDialog::WarpCells)
				{
					QComboBox* dev = dlg->findChild<QComboBox*>("cell_device");
					if (dev) dev->setCurrentIndex(std::min(qEnvironmentVariableIntValue("VVD_QT_WT_CELLDEV"), dev->count() - 1));
					log("cells device '%s'", dev ? dev->currentText().toLocal8Bit().constData() : "?");
				}
				const std::vector<VolumeData*> before = m_canvas->Volumes();
				QElapsedTimer t; t.start();
				apply->click();
				const qint64 ms = t.elapsed();
				wt->res = nullptr;
				for (VolumeData* v : m_canvas->Volumes())
					if (std::find(before.begin(), before.end(), v) == before.end()) wt->res = v;
				check(wt->res != nullptr, "run " + tag + ": result volume added");
				if (!wt->res) return;
				VolumeData* res = wt->res;
				describe("result", res);
				Nrrd* d = DataNrrd(res);
				int rx, ry, rz; double sx, sy, sz;
				res->GetResolution(rx, ry, rz); res->GetSpacings(sx, sy, sz);
				const int nb = res->GetTexture()->nb(0);
				const size_t bytes = (size_t)rx * ry * rz * nb;
				long long nz = 0;
				int x0 = rx, y0 = ry, z0 = rz, x1 = -1, y1 = -1, z1 = -1;
				for (int z = 0; z < rz; z++)
				for (int y = 0; y < ry; y++)
				{
					const long long row = ((long long)z * ry + y) * rx;
					for (int x = 0; x < rx; x++)
					{
						if (Val(d, row + x) == 0.0) continue;
						nz++;
						x0 = std::min(x0, x); x1 = std::max(x1, x);
						y0 = std::min(y0, y); y1 = std::max(y1, y);
						z0 = std::min(z0, z); z1 = std::max(z1, z);
					}
				}
				QByteArray md5 = QCryptographicHash::hash(QByteArrayView((const char*)d->data, (qsizetype)bytes), QCryptographicHash::Md5).toHex();
				log("RESULT %s %ls %dx%dx%d spacing %.6f bits %d max %g nonzero %lld md5 %s time %lld ms", tag.toLocal8Bit().constData(),
					res->GetName().c_str(), rx, ry, rz, sx, nb * 8, res->GetMaxValue(), nz, md5.constData(), (long long)ms);
				if (nz > 0)
				{
					FLIVR::Transform* tf = res->GetTexture()->transform();
					FLIVR::Point a = tf->project(FLIVR::Point(x0 / double(rx), y0 / double(ry), z0 / double(rz)));
					FLIVR::Point b = tf->project(FLIVR::Point((x1 + 1) / double(rx), (y1 + 1) / double(ry), (z1 + 1) / double(rz)));
					log("  nonzero bbox voxels x %d-%d y %d-%d z %d-%d, physical (%.3f, %.3f, %.3f) - (%.3f, %.3f, %.3f)", x0, x1, y0, y1, z0, z1,
						a.x(), a.y(), -a.z(), b.x(), b.y(), -b.z());
				}
				if (raw) { QFile fo(st->dir + "/" + tag + ".raw"); if (fo.open(QIODevice::WriteOnly)) { fo.write((const char*)d->data, (qint64)bytes); fo.close(); } }
				// landmarks
				const bool rev = WarpRulersReversed(wt->src, wt->tgt, *m_ruler_tool->GetRulerList());
				int near_ok = 0, n_lm = 0;
				for (Ruler* r : *m_ruler_tool->GetRulerList())
				{
					if (r->GetRulerType() != 0 || r->GetNumPoint() < 2) continue;
					FLIVR::Point pm = *r->GetPoint(rev ? 1 : 0), pf = *r->GetPoint(rev ? 0 : 1);
					bool in_s = false, in_r = false; double vs = -1, vr = -1;
					const double ds = nearest(wt->src, pm, 30, &in_s, &vs);
					const double dr = nearest(res, pf, 30, &in_r, &vr);
					n_lm++;
					// the fixed point lands as close to the warped object as the
					// moving point is to the source object (+ one result voxel)
					const bool ok = in_r && dr >= 0 && (ds < 0 || dr <= ds + 1.5 * sx);
					near_ok += ok;
					log("  ruler %ls: moving (%.3f, %.3f, %.3f) in source %s value %g nearest nonzero %.4f | fixed (%.3f, %.3f, %.3f) in result %s value %g nearest nonzero %.4f %s",
						r->GetNameDisp().c_str(), pm.x(), pm.y(), -pm.z(), in_s ? "yes" : "NO", vs, ds, pf.x(), pf.y(), -pf.z(), in_r ? "yes" : "NO", vr, dr,
						ok ? "ok" : "FAR");
				}
				log("run %s: %d of %d end points at the warped object", tag.toLocal8Bit().constData(), near_ok, n_lm);
				// after picture: target + result
				for (VolumeData* v : m_canvas->Volumes()) v->SetDisp(v == res || v == wt->tgt || (!wt->tgt && v == res));
				m_canvas->RefreshView();
			});
			if (grab) capture(tag + "_after");
		}
	}

	add("done", [st, log]() {
		log("done: %d failed checks", st->fails);
		QTimer::singleShot(300, qApp, []() { QCoreApplication::quit(); });
	}, false);

	// ------------------------------------------------------------- runner
	connect(cv, &RenderCanvas::frameCaptured, this, [st, log](const QString& path, bool ok) {
		if (!st->waiting_capture) return;
		st->waiting_capture = false;
		log("captured %s%s", path.toLocal8Bit().constData(), ok ? "" : " (FAILED)");
	});
	auto* timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [st, cv, timer, log]() {
		if (st->waiting_capture)
			return;
		if (st->settling)
		{
			// a finished frame after the step: loop done, two frames presented
			if (cv->PresentedFrames() < st->frames0 + 2 || !cv->IsUpdateLoopDone())
			{
				st->settle.restart();
				if (cv->PresentedFrames() < st->frames0 + 2) cv->RefreshView();
				return;
			}
			if (st->settle.elapsed() < 200)
				return;
			st->settling = false;
		}
		if (st->steps.empty())
		{
			timer->stop();
			return;
		}
		Step s = st->steps.front();
		st->steps.pop_front();
		QElapsedTimer t;
		t.start();
		s.fn();
		if (t.elapsed() > 500)
			log("(step '%s' took %lld ms)", s.name.toLocal8Bit().constData(), (long long)t.elapsed());
		if (s.settle)
		{
			st->settling = true;
			st->frames0 = cv->PresentedFrames();
			st->settle.restart();
			cv->RefreshView();
		}
	});
	timer->start(50);
}
