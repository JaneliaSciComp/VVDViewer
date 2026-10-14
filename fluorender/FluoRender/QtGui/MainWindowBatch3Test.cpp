// Testing aid for the batch-3 wx parity work (panels and dialogs, see
// docs/QT_PORT.md): VVD_QT_TEST=b3_<kind> (+ VVD_QT_GRAB / VVD_QT_TEST_DIR),
// "[b3] PASS/FAIL" lines on stderr, captures in the test directory, quits.
//
//   b3_panels    volA.nrrd volB.nrrd mesh.obj (+ VVD_QT_B3_DATA=<dir> with
//                octa2.obj / octa3.obj): Mesh Manipulate pane, annotation
//                properties pane + Measurement annotation rows / export,
//                Volume pane (Save as Default to VVD_QT_VOLUME_DFT, Reset,
//                MIP / Alpha side effects, Depth-mode MIP refusal, Shadow / NR
//                / Sample Rate in Depth mode, 8-bit Max Value), mesh Sync in /
//                outside a mesh group, the Tester dialog hidden
//   b3_maxval    u16.nrrd f32.nrrd: Max Value scalar-scale rules
//   b3_paint     volA.nrrd: paint-ruler mode (Shift in a 2pt ruler mode, the
//                selection centre becomes the ruler point), tablet pressure
//   b3_nrna      ball.nrrd (+ ball.lbl): Noise Reduction Erase in NA mode
//                (Calculate 11) and not in NA mode (Calculate 6)
//   b3_script4d  ts_T1.nrrd with VVD_QT_TIME_SEQ=1 (3 time points + .lbl /
//                .msk): every 4D script task, then playback in batch mode
//   b3_modes     any data, VVD_QT_TEST_MODE=speed,param,wiref: test modes

#include "MainWindow.h"

#include <QTimer>
#include <QApplication>
#include <QDockWidget>
#include <QStackedWidget>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QLabel>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QElapsedTimer>
#include <QTextStream>
#include <QMenu>
#include <deque>
#include <functional>
#include <memory>
#include <cmath>
#include <set>

#include "RenderCanvas.h"
#include "TreePanel.h"
#include "VolumePropPanel.h"
#include "MeshPropPanel.h"
#include "AnnotationPropPanel.h"
#include "ClipPanel.h"
#include "MeasureDialog.h"
#include "RulerTool.h"
#include "BrushTool.h"
#include "MoviePanel.h"
#include "SettingsDialog.h"
#include "NoiseReductionDialog.h"
#include "DataManager.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/Texture.h"

namespace
{
	long long CountNZ(const Nrrd* n, long long count)
	{
		if (!n || !n->data) return -1;
		long long c = 0;
		if (n->type == nrrdTypeUChar) { const unsigned char* p = (const unsigned char*)n->data; for (long long i = 0; i < count; i++) c += p[i] != 0; }
		else if (n->type == nrrdTypeUInt) { const unsigned int* p = (const unsigned int*)n->data; for (long long i = 0; i < count; i++) c += p[i] != 0; }
		else if (n->type == nrrdTypeUShort) { const unsigned short* p = (const unsigned short*)n->data; for (long long i = 0; i < count; i++) c += p[i] != 0; }
		return c;
	}
	long long Voxels(VolumeData* vd) { int x, y, z; vd->GetResolution(x, y, z); return (long long)x * y * z; }
}

void MainWindow::RunBatch3Test(const QString& kind)
{
	struct Step { QString name; std::function<void()> fn; bool settle = true; };
	struct State
	{
		std::deque<Step> steps;
		QString dir;
		long long frames0 = 0;
		QElapsedTimer settle;
		bool waiting_capture = false;
		bool settling = false;
		int fails = 0;
		bool hold = false;   // a step waits for something (playback)
	};
	auto st = std::make_shared<State>();
	st->dir = qEnvironmentVariable("VVD_QT_TEST_DIR");
	if (st->dir.isEmpty())
		st->dir = QFileInfo(qEnvironmentVariable("VVD_QT_GRAB")).absolutePath();
	QDir().mkpath(st->dir);
	RenderCanvas* cv = m_canvas;

	auto log = [](const char* fmt, auto... args) {
		fprintf(stderr, "[b3] ");
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
	auto capture = [st, cv, add](const QString& name) {
		add("capture " + name, [st, cv, name]() {
			st->waiting_capture = true;
			cv->CaptureToFile(st->dir + "/" + name + ".png");
		}, false);
	};
	auto pixdiff = [st](const QString& a_, const QString& b_) -> long long {
		QImage a(st->dir + "/" + a_ + ".png"), b(st->dir + "/" + b_ + ".png");
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
	auto find_vol = [this](const QString& name_part) -> VolumeData* {
		for (VolumeData* v : m_canvas->Volumes())
			if (v->GetName().ToQt().contains(name_part)) return v;
		return nullptr;
	};
	auto find_mesh = [this](const QString& name_part) -> MeshData* {
		for (MeshData* m : m_canvas->Meshes())
			if (m->GetName().ToQt().contains(name_part)) return m;
		return nullptr;
	};
	// a modal message box opened by the next step is answered after 400 ms;
	// its text is kept in *msg
	auto answer_box = [](std::shared_ptr<QString> msg) {
		QTimer::singleShot(400, qApp, [msg]() {
			if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
			{
				*msg = box->text();
				box->accept();
			}
		});
	};
	auto stroke = [cv](QPoint a, QPoint b) {
		cv->SimulateMouse(QEvent::MouseMove, a, Qt::NoButton, Qt::NoButton);
		cv->SimulateMouse(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
		const int n = 12;
		for (int i = 1; i <= n; i++)
			cv->SimulateMouse(QEvent::MouseMove, a + (b - a) * i / n, Qt::NoButton, Qt::LeftButton);
		cv->SimulateMouse(QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
	};
	// bright-pixel centroids of the left / right half of a capture (widget px)
	auto blob_centers = [st, cv](const QString& name, QPoint& left, QPoint& right) {
		QImage img(st->dir + "/" + name + ".png");
		if (img.isNull()) return false;
		img = img.convertToFormat(QImage::Format_RGB32);
		double sx = 0, n = 0;
		for (int y = 0; y < img.height(); y++)
			for (int x = 0; x < img.width(); x++)
			{
				QRgb c = img.pixel(x, y);
				if (qRed(c) + qGreen(c) + qBlue(c) > 100) { sx += x; n++; }
			}
		if (n == 0) return false;
		const double mid = sx / n;
		double lx = 0, ly = 0, ln = 0, rx = 0, ry = 0, rn = 0;
		for (int y = 0; y < img.height(); y++)
			for (int x = 0; x < img.width(); x++)
			{
				QRgb c = img.pixel(x, y);
				if (qRed(c) + qGreen(c) + qBlue(c) <= 160) continue;
				if (x < mid) { lx += x; ly += y; ln++; }
				else { rx += x; ry += y; rn++; }
			}
		if (ln == 0 || rn == 0) return false;
		const double fx = double(cv->width()) / img.width(), fy = double(cv->height()) / img.height();
		left = QPoint(int(lx / ln * fx), int(ly / ln * fy));
		right = QPoint(int(rx / rn * fx), int(ry / rn * fy));
		return true;
	};

	add("start", [=]() {
		log("kind %s: %d volumes, %d meshes; application active %d, active window %s", kind.toLocal8Bit().constData(), (int)m_canvas->Volumes().size(), (int)m_canvas->Meshes().size(),
			(int)(QGuiApplication::applicationState() == Qt::ApplicationActive), QApplication::activeWindow() ? QApplication::activeWindow()->metaObject()->className() : "none");
	});

	// =================================================================== panels
	if (kind == "b3_panels")
	{
		const QString data = qEnvironmentVariable("VVD_QT_B3_DATA");
		// ---- 10: Tester hidden without the param test mode; no constant redraw
		add("tester hidden", [=]() {
			check(m_tester && !m_tester->isVisible() && m_tester->windowTitle() == "Tester",
				"Tester dialog exists, hidden without the parameter test mode");
			check(!m_canvas->GetTestSpeed() && !m_canvas->GetDrawGrid(), "speed / wireframe test modes off by default");
			if (MeshData* md = find_mesh("mesh.obj")) m_tree->SelectMesh(md);   // selected: drawn with its box
		});
		// ---- 1: Mesh Manipulate pane
		capture("manip0");
		add("manip show", [=]() {
			MeshData* md = find_mesh("mesh.obj");
			if (!md) { check(false, "mesh.obj loaded"); return; }
			Q_EMIT m_tree->manipulateRequested(md);   // tree context menu "Manipulate"
			check(m_prop_stack->currentWidget() == m_mesh_manip, "Manipulate shows the Mesh Manipulate pane in the Properties dock");
			check(m_prop_dock->windowTitle() == "Manipulations - " + md->GetName().ToQt(),
				"dock caption \"" + m_prop_dock->windowTitle() + "\"");
			QList<QLineEdit*> f = m_mesh_manip->findChildren<QLineEdit*>();
			QStringList t;
			for (QLineEdit* e : f) t << e->text();
			check(f.size() == 9 && t.join(",") == "0.00,0.00,0.00,0.00,0.00,0.00,1.00,1.00,1.00",
				"GetData: translation / rotation / scaling boxes " + t.join(","));
			check(m_cur_sel_type == 6, "m_cur_sel_type = 6");
		});
		add("manip edit", [=]() {
			MeshData* md = find_mesh("mesh.obj");
			QList<QLineEdit*> f = m_mesh_manip->findChildren<QLineEdit*>();
			QList<QToolButton*> b = m_mesh_manip->findChildren<QToolButton*>();
			if (f.size() != 9 || b.size() != 18) { check(false, "9 boxes / 18 spin arrows"); return; }
			f[0]->setText("20");
			Q_EMIT f[0]->returnPressed();
			b[2 * 4]->click();       // rotation Y up
			b[2 * 8 + 1]->click();   // scaling Z down
			double x, y, z;
			md->GetTranslation(x, y, z);
			double rx, ry, rz;
			md->GetRotation(rx, ry, rz);
			double sx, sy, sz;
			md->GetScaling(sx, sy, sz);
			log("translation %.3f %.3f %.3f, rotation %.3f %.3f %.3f, scaling %.3f %.3f %.3f; boxes %s %s %s",
				x, y, z, rx, ry, rz, sx, sy, sz, f[0]->text().toLocal8Bit().constData(),
				f[4]->text().toLocal8Bit().constData(), f[8]->text().toLocal8Bit().constData());
			check(x == 20.0 && f[0]->text() == "20.000", "Enter in X translation: SetTranslation(20, 0, 0), box \"%.3f\"");
			check(std::fabs(ry - 1.0) < 1e-9 && f[4]->text() == "1.000", "Y rotation up arrow: +1 (1.000)");
			check(std::fabs(sz - 0.0) < 1e-9 && f[8]->text() == "0.000", "Z scaling down arrow: 1 - 1 = 0 (0.000)");
			// scaling back to 1 so the mesh stays visible
			f[8]->setText("1");
			Q_EMIT f[8]->returnPressed();
		});
		capture("manip1");
		add("manip compare", [=]() {
			const long long d = pixdiff("manip0", "manip1");
			check(d > 0, QString("the translated / rotated mesh renders differently (%1 pixels)").arg(d));
			MeshData* md = find_mesh("mesh.obj");
			md->SetTranslation(0, 0, 0);
			md->SetRotation(0, 0, 0);
			md->SetScaling(1, 1, 1);
			// the tree item is still the current one: select it anew
			if (VolumeData* va = find_vol("volA")) m_tree->SelectVolume(va);
			m_tree->SelectMesh(md);
			check(m_prop_stack->currentWidget() == m_mesh_prop, "selecting the mesh again shows the mesh properties");
			m_canvas->RefreshView();
		});
		capture("manip2");
		add("manip restored", [=]() {
			check(pixdiff("manip0", "manip2") == 0, "transform reset: the render equals the first capture");
		});

		// ---- 2 / 5: annotations
		auto ann_mesh = std::make_shared<Annotations*>(nullptr);
		auto ann_vol = std::make_shared<Annotations*>(nullptr);
		add("annotations load", [=]() {
			MeshData* md = find_mesh("mesh.obj");
			VolumeData* va = find_vol("volA");
			const QString f1 = st->dir + "/ann_score.txt";
			const QString f2 = st->dir + "/ann_vol.txt";
			QFile a(f1);
			if (a.open(QIODevice::WriteOnly | QIODevice::Text))
				a.write(QString("Name: ann_score\nDisplay: 1\nMemo:\nhello memo\nMemo Update: 0\nMesh: %1\nAlpha: 0.5\nThreshold: 20\nMaxScore: 100\n"
					"Components:\nID\tX\tY\tZ\tscore\n\nA\t10\t20\t30\t90\nB\t11\t21\t31\t60\nC\t12\t22\t32\t30\nD\t13\t23\t33\t10\nE\t14\t24\t34\t5\n\n")
					.arg(md->GetName().ToQt()).toUtf8());
			a.close();
			QFile b(f2);
			if (b.open(QIODevice::WriteOnly | QIODevice::Text))
				b.write(QString("Name: ann_vol\nDisplay: 1\nMemo:\nread only\nMemo Update: 1\nVolume: %1\n"
					"Components:\nID\tX\tY\tZ\tvoxels\n\n1\t64\t50\t30\t1234\n\n").arg(va->GetName().ToQt()).toUtf8());
			b.close();
			*ann_mesh = m_data_mgr->LoadAnnotations(VVDString(f1));
			*ann_vol = m_data_mgr->LoadAnnotations(VVDString(f2));
			check(*ann_mesh && (*ann_mesh)->GetMesh() == md && (*ann_mesh)->GetTextNum() == 5, "mesh annotation with 5 scored texts");
			check(*ann_vol && (*ann_vol)->GetVolume() == va && (*ann_vol)->GetTextNum() == 1, "volume annotation with 1 text");
			if (*ann_mesh) m_canvas->AddAnnotations(*ann_mesh);
			if (*ann_vol) m_canvas->AddAnnotations(*ann_vol);
			RefreshTree();
		});
		add("annotation pane (mesh)", [=]() {
			Annotations* ann = *ann_mesh;
			if (!ann) return;
			MeshData* md = ann->GetMesh();
			check(m_tree->SelectAnnotations(ann), "tree selects the annotations item");
			check(m_prop_stack->currentWidget() == m_ann_prop, "selecting annotations shows the annotation pane");
			check(m_prop_dock->windowTitle() == "Properties - ann_score", "caption \"" + m_prop_dock->windowTitle() + "\"");
			check(m_clip->GetMeshData() == md, "Clipping Planes follow the annotation's mesh");
			AnnotationPropPanel* p = m_ann_prop;
			check(p->MemoText()->toPlainText() == "hello memo\n" && !p->MemoText()->isReadOnly() && p->MemoUpdateButton()->isEnabled(),
				"memo \"" + p->MemoText()->toPlainText() + "\", editable, Update enabled");
			check(!p->AlphaSlider()->isHidden() && !p->ThresholdSlider()->isHidden() && !p->ColorButton()->isHidden(),
				"mesh annotation: Transparency / Min Score / Color shown");
			check(p->AlphaText()->text() == "0.50" && p->AlphaSlider()->value() == 127, "Transparency 0.50 (slider " + QString::number(p->AlphaSlider()->value()) + ")");
			check(p->ThresholdSlider()->maximum() == 101 && p->ThresholdText()->text() == "20", "Min Score 20, slider max = MaxScore + 1 = " + QString::number(p->ThresholdSlider()->maximum()));
			check(p->NumParticlesLabel()->text().trimmed() == "Number of Particles:  3", "\"" + p->NumParticlesLabel()->text().trimmed() + "\" (scores 90 60 30 above 20)");
			// threshold
			p->ThresholdText()->setText("50");
			check(std::fabs(md->GetThreshold() - 50.0) < 1e-9 && p->ThresholdSlider()->value() == 50, "Min Score 50 -> mesh threshold 50");
			check(p->NumParticlesLabel()->text().trimmed() == "Number of Particles:  2", "\"" + p->NumParticlesLabel()->text().trimmed() + "\"");
			p->ThresholdSlider()->setValue(25);
			check(p->ThresholdText()->text() == "25" && std::fabs(md->GetThreshold() - 25.0) < 1e-9 &&
				p->NumParticlesLabel()->text().trimmed() == "Number of Particles:  3", "slider 25 -> text, threshold, 3 particles");
			p->ThresholdText()->setText("50");
			// alpha
			p->AlphaSlider()->setValue(200);
			check(p->AlphaText()->text() == "0.78" && std::fabs(ann->GetAlpha() - 0.78) < 1e-9, "Transparency slider 200 -> \"0.78\", annotation alpha " + QString::number(ann->GetAlpha()));
			// colour
			p->ApplyColor(QColor(255, 0, 0));
			FLIVR::Color amb, diff, spec;
			double shine, alpha;
			md->GetMaterial(amb, diff, spec, shine, alpha);
			check(diff.r() == 1.0 && diff.g() == 0.0 && std::fabs(amb.r() - 0.3) < 1e-9, QString("Color red: mesh diffuse (%1,%2,%3), ambient %4").arg(diff.r()).arg(diff.g()).arg(diff.b()).arg(amb.r()));
			// memo
			p->MemoText()->setPlainText("new memo");
			p->MemoUpdateButton()->click();
			check(ann->GetMemo() == "new memo", "Update stores the memo");
		});
		add("measurement annotation rows", [=]() {
			MeasureDialog* dlg = EnsureMeasureDialog();
			dlg->UpdateList();
			QStringList rows;
			for (int i = 0; i < dlg->RowCount(); i++)
				rows << dlg->RowText(i, MeasureDialog::ColID) + " | " + dlg->RowText(i, MeasureDialog::ColPoints) + " | " + dlg->RowText(i, MeasureDialog::ColInfo);
			log("measurement rows: %s", rows.join("; ").toLocal8Bit().constData());
			check(dlg->RowCount() == 3, QString("3 rows (0 rulers + 2 annotations above 50 + 1 volume annotation), got %1").arg(dlg->RowCount()));
			check(rows.size() == 3 && rows[0] == "A  (ann_score) | (10.00, 20.00, 30.00) | 90" &&
				rows[1] == "B  (ann_score) | (11.00, 21.00, 31.00) | 60" &&
				rows[2] == "1  (ann_vol) | (64.00, 50.00, 30.00) | 1234 voxels", "row texts as wx OnGetItemText");
			check(dlg->GetCount(*ann_mesh) == 2 && dlg->GetCount(*ann_vol) == 1 && dlg->GetCount() == 0, "GetCount per layer");
			const QString f = st->dir + "/measure_export.txt";
			dlg->Export(f);
			QFile in(f);
			in.open(QIODevice::ReadOnly | QIODevice::Text);
			const QStringList lines = QString::fromUtf8(in.readAll()).split('\n', Qt::SkipEmptyParts);
			log("export: %s", lines.join(" // ").toLocal8Bit().constData());
			check(lines.size() == 4 && lines[1] == "A  (ann_score)\tN/A\t0.00\t0.0\t10.00\t20.00\t30.00\t\t\t\tN/A\t90\t\t" &&
				lines[3] == "1  (ann_vol)\tN/A\t0.00\t0.0\t64.00\t50.00\t30.00\t\t\t\tN/A\t1234 voxels\t\t",
				"text export: header + the wx annotation lines (+ the empty start / end volume columns)");
			// hidden annotations leave the list
			(*ann_vol)->SetDisp(false);
			dlg->UpdateList();
			check(dlg->RowCount() == 2, "a hidden annotation layer has no rows");
			(*ann_vol)->SetDisp(true);
		});
		add("annotation pane (volume, read only)", [=]() {
			Annotations* ann = *ann_vol;
			if (!ann) return;
			m_tree->SelectAnnotations(ann);
			AnnotationPropPanel* p = m_ann_prop;
			check(m_prop_stack->currentWidget() == m_ann_prop && p->GetAnnotations() == ann, "volume annotation shown");
			check(p->MemoText()->isReadOnly() && !p->MemoUpdateButton()->isEnabled() && p->MemoText()->toPlainText() == "read only\n",
				"Memo Update: 1 -> read-only memo, Update disabled");
			check(p->AlphaSlider()->isHidden() && p->ThresholdSlider()->isHidden() && p->ColorButton()->isHidden() && p->NumParticlesLabel()->isHidden(),
				"no mesh: Transparency / Min Score / particles / Color hidden");
			check(m_clip->GetVolume() == ann->GetVolume(), "Clipping Planes follow the annotation's volume");
			// cleanup: the annotations leave the view
			for (Annotations* a : { *ann_mesh, *ann_vol })
			{
				m_canvas->RemoveAnnotations(a);
				m_data_mgr->RemoveAnnotations(a->GetName());
			}
			*ann_mesh = *ann_vol = nullptr;
			m_ann_prop->SetAnnotations(nullptr);
			RefreshTree();
			MeshData* md = find_mesh("mesh.obj");
			FLIVR::Color c(0.3, 0.3, 1.0);   // the default colour back for later captures
			md->SetColor(c, MESH_COLOR_DIFF);
		});

		// ---- 3: Volume pane
		add("volume select", [=]() {
			VolumeData* va = find_vol("volA");
			m_tree->SelectVolume(va);
			check(m_prop_stack->currentWidget() == m_prop && m_prop->GetVolume() == va, "volA in the Volume pane");
		});
		add("save as default", [=]() {
			VolumeData* va = find_vol("volA");
			m_prop->GammaBox()->setValue(1.7);
			m_prop->AlphaBox()->setValue(100);
			m_prop->SaveDefault();
			const QString f = VolumePropPanel::DefaultSettingsPath();
			QFile in(f);
			const bool ok = in.open(QIODevice::ReadOnly | QIODevice::Text);
			const QString txt = ok ? QString::fromUtf8(in.readAll()) : QString();
			log("default file %s:\n%s", f.toLocal8Bit().constData(), txt.toLocal8Bit().constData());
			QStringList keys;
			for (const QString& l : txt.split('\n', Qt::SkipEmptyParts)) keys << l.section('=', 0, 0);
			check(ok && keys.join(",") == "extract_boundary,gamma,low_offset,high_offset,low_thresholding,high_thresholding,low_shading,high_shading,alpha,sample_rate,x_spacing,y_spacing,z_spacing,luminance,colormap_low,colormap_hi,enable_alpha,enable_shading,enable_inv,enable_mip,noise_rd,enable_shadow,shadow_intensity",
				"default_volume_settings.dft keys in the wx order: " + keys.join(","));
			check(txt.contains("gamma=1.7\n") && txt.contains("high_offset=1\n"), "gamma=1.7, high_offset=1");
			check(std::fabs(m_data_mgr->m_vol_gam - 1.7) < 1e-9 && std::fabs(m_data_mgr->m_vol_alf - 100.0 / 255.0) < 1e-9,
				QString("DataManager defaults: gamma %1, alpha %2 (100 / 255)").arg(m_data_mgr->m_vol_gam).arg(m_data_mgr->m_vol_alf));
			check(std::fabs(va->Get3DGamma() - 1.7) < 1e-9, "the volume has gamma 1.7");
		});
		add("reset", [=]() {
			VolumeData* va = find_vol("volA");
			va->Set3DGamma(3.0);
			va->SetAlpha(0.9);
			va->SetMode(1);
			va->SetInvert(true);
			double h, s, v;
			va->GetHSV(h, s, v);
			FLIVR::Color c(FLIVR::HSVColor(h, s, 0.4));
			va->SetColor(c);
			m_data_mgr->m_vol_lum = 0.8;
			m_prop->GetSettings();
			m_prop->ResetDefault();
			check(std::fabs(va->Get3DGamma() - 1.7) < 1e-9 && std::fabs(va->GetAlpha() - 100.0 / 255.0) < 1e-9,
				QString("Reset: gamma %1, alpha %2 from the saved defaults").arg(va->Get3DGamma()).arg(va->GetAlpha()));
			check(va->GetMode() == 0 && !va->GetInvert(), "Reset: MIP / Inv from the defaults (off)");
			check(std::fabs(va->GetLuminance() - 0.8) < 1e-6, QString("Reset: luminance = HSV value %1").arg(va->GetLuminance()));
			check(std::fabs(m_prop->GammaBox()->value() - 1.7) < 1e-9 && m_prop->ThresholdLabel() == "Threshold:", "controls show the reset values");
			m_data_mgr->m_vol_lum = 1.0;
			m_prop->ResetDefault();
		});
		add("mip check", [=]() {
			VolumeData* va = find_vol("volA");
			m_prop->MipCheck()->click();
			check(va->GetMode() == 1, "MIP on");
			check(!m_prop->RowEnabled(3) && !m_prop->RowEnabled(7) && !m_prop->RowEnabled(5) && !m_prop->RowEnabled(2) &&
				m_prop->RowEnabled(0) && m_prop->RowEnabled(1) && m_prop->ThresholdLabel() == "Shade Threshold:",
				"MIP: alpha / right threshold / boundary / luminance disabled, gamma / saturation enabled, \"" + m_prop->ThresholdLabel() + "\"");
			m_prop->GetSettings();
			check(!m_prop->RowEnabled(3) && !m_prop->RowEnabled(2) && m_prop->ThresholdLabel() == "Shade Threshold:", "GetSettings keeps the MIP state");
			m_prop->MipCheck()->click();
			check(va->GetMode() == 0 && m_prop->RowEnabled(3) && m_prop->RowEnabled(7) && m_prop->RowEnabled(5) && m_prop->RowEnabled(2) &&
				m_prop->ThresholdLabel() == "Threshold:", "MIP off: re-enabled, \"Threshold:\"");
		});
		add("alpha check", [=]() {
			VolumeData* va = find_vol("volA");
			const bool sh = va->GetShading();
			m_prop->AlphaCheck()->click();
			check(!va->GetEnableAlpha() && !m_prop->RowEnabled(3) && !m_prop->RowEnabled(4), "Alpha off: alpha slider / box disabled, low shading disabled");
			if (sh) check(!m_prop->ShadingCheck()->isChecked() && !va->GetVR()->get_shading(), "Alpha off with shading: the Shading box unchecked, renderer shading off");
			m_prop->AlphaCheck()->click();
			check(va->GetEnableAlpha() && m_prop->RowEnabled(3), "Alpha on: enabled again");
			if (sh) check(m_prop->ShadingCheck()->isChecked() && va->GetVR()->get_shading() && m_prop->RowEnabled(4), "Alpha on: shading back");
		});
		auto msg = std::make_shared<QString>();
		add("mip in depth mode", [=]() {
			VolumeData* va = find_vol("volA");
			m_canvas->SetVolMethod(RenderCanvas::VolMethodMulti);
			answer_box(msg);
			m_prop->MipCheck()->click();
			check(*msg == "MIP is not supported in Depth mode." && !m_prop->MipCheck()->isChecked() && va->GetMode() == 0,
				"Depth mode: MIP refused with \"" + *msg + "\", box unchecked");
		});
		add("depth mode: all volumes", [=]() {
			VolumeData* va = find_vol("volA");
			VolumeData* vb = find_vol("volB");
			m_prop->SampleRateBox()->setValue(0.7);
			check(std::fabs(va->GetSampleRate() - 0.7) < 1e-9 && std::fabs(vb->GetSampleRate() - 0.7) < 1e-9, "Depth mode: Sample Rate 0.7 on every volume");
			m_prop->ShadowCheck()->click();
			check(va->GetShadow() && vb->GetShadow(), "Depth mode: Shadow on every volume");
			m_prop->ShadowBox()->setValue(0.4);
			double sa, sb;
			va->GetShadowParams(sa);
			vb->GetShadowParams(sb);
			check(std::fabs(sa - 0.4) < 1e-9 && std::fabs(sb - 0.4) < 1e-9, "Depth mode: shadow intensity on every volume");
			m_prop->NRCheck()->click();
			check(va->GetNR() && vb->GetNR(), "Depth mode: Smoothing on every volume");
			m_prop->NRCheck()->click();
			m_prop->ShadowCheck()->click();
			check(!va->GetNR() && !vb->GetNR() && !va->GetShadow() && !vb->GetShadow(), "and off again");
			m_canvas->SetVolMethod(RenderCanvas::VolMethodComp);
			m_prop->SampleRateBox()->setValue(0.9);
			check(std::fabs(va->GetSampleRate() - 0.9) < 1e-9 && std::fabs(vb->GetSampleRate() - 0.7) < 1e-9, "Composite mode, no group sync: Sample Rate on the volume only");
			m_prop->ShadowCheck()->click();
			check(va->GetShadow() && !vb->GetShadow(), "Composite mode: Shadow on the volume only");
			m_prop->ShadowCheck()->click();
			m_prop->SampleRateBox()->setValue(1.0);
			vb->SetSampleRate(1.0);
		});
		add("max value 8-bit", [=]() {
			VolumeData* va = find_vol("volA");
			m_prop->MaxValueBox()->setValue(1000);
			check(va->GetMaxValue() == 1000 && va->GetScalarScale() == 1.0, QString("8-bit: Max Value 1000, scalar scale %1 (wx: 1.0)").arg(va->GetScalarScale()));
			m_prop->MaxValueBox()->setValue(255);
			check(va->GetMaxValue() == 255 && va->GetScalarScale() == 1.0, "8-bit: back to 255");
		});

		// ---- 4: mesh Sync in / outside a mesh group
		add("mesh group load", [=]() {
			LoadMeshFiles({ data + "/octa2.obj", data + "/octa3.obj" });
		});
		add("mesh sync", [=]() {
			MeshData* m1 = find_mesh("mesh.obj");
			MeshData* m2 = find_mesh("octa2");
			MeshData* m3 = find_mesh("octa3");
			if (!m1 || !m2 || !m3) { check(false, "mesh.obj + octa2 / octa3 loaded"); return; }
			check(!m_canvas->GetParentMGroup(m1) && m_canvas->GetParentMGroup(m2) && m_canvas->GetParentMGroup(m2) == m_canvas->GetParentMGroup(m3),
				"octa2 / octa3 in one mesh group, mesh.obj at the view level");
			auto alpha = [](MeshData* md) { FLIVR::Color a, d, s; double sh, al; md->GetMaterial(a, d, s, sh, al); return al; };
			QCheckBox* sync = nullptr;
			// grouped mesh: the group
			m_tree->SelectMesh(m2);
			for (QCheckBox* c : m_mesh_prop->findChildren<QCheckBox*>()) if (c->text() == " Sync") sync = c;
			double a2 = 0.3;
			m2->SetFloat(a2, MESH_FLOAT_ALPHA);
			sync->click();
			log("alpha after Sync on octa2: mesh %.2f octa2 %.2f octa3 %.2f", alpha(m1), alpha(m2), alpha(m3));
			check(std::fabs(alpha(m3) - 0.3) < 1e-9 && std::fabs(alpha(m1) - 0.3) > 1e-3, "Sync on a grouped mesh: its group only");
			sync->click();   // off
			// view-level mesh: the ungrouped shown meshes
			m_tree->SelectMesh(m1);
			double a1 = 0.6;
			m1->SetFloat(a1, MESH_FLOAT_ALPHA);
			sync->click();
			log("alpha after Sync on mesh.obj: mesh %.2f octa2 %.2f octa3 %.2f", alpha(m1), alpha(m2), alpha(m3));
			check(std::fabs(alpha(m2) - 0.3) < 1e-9 && std::fabs(alpha(m3) - 0.3) < 1e-9, "Sync on a view-level mesh: the grouped meshes keep theirs");
			sync->click();
		});
	}

	// =================================================================== max value
	if (kind == "b3_maxval")
	{
		add("max value", [=]() {
			VolumeData* vu = find_vol("u16");
			VolumeData* vf = find_vol("f32");
			if (!vu || !vf) { check(false, "u16 + f32 loaded"); return; }
			m_tree->SelectVolume(vu);
			log("u16: max %.1f scale %.4f; f32: max %.4f scale %.4f", vu->GetMaxValue(), vu->GetScalarScale(), vf->GetMaxValue(), vf->GetScalarScale());
			m_prop->MaxValueBox()->setValue(5000);
			check(vu->GetMaxValue() == 5000 && std::fabs(vu->GetScalarScale() - 65535.0 / 5000) < 1e-9,
				QString("16-bit: Max Value 5000, scalar scale %1 = 65535 / 5000").arg(vu->GetScalarScale()));
			m_tree->SelectVolume(vf);
			m_prop->MaxValueBox()->setValue(2.0);
			check(vf->GetMaxValue() == 2.0 && std::fabs(vf->GetScalarScale() - 0.5) < 1e-9,
				QString("float: Max Value 2, scalar scale %1 = 1 / 2").arg(vf->GetScalarScale()));
			QSlider* alpha_sl = nullptr;
			for (QSlider* s : m_prop->findChildren<QSlider*>()) if (s->maximum() == 200) alpha_sl = s;
			check(alpha_sl != nullptr, "float data with max 2: sliders run 0 .. 200 (wx sldr_max = max x 100)");
		});
	}

	// =================================================================== paint ruler + tablet
	if (kind == "b3_paint")
	{
		capture("pr0");
		auto cl = std::make_shared<std::pair<QPoint, QPoint>>();
		add("locate", [=]() {
			check(blob_centers("pr0", cl->first, cl->second), "two blobs found");
			log("blob centres (widget px): %d,%d and %d,%d", cl->first.x(), cl->first.y(), cl->second.x(), cl->second.y());
			VolumeData* va = m_canvas->Volumes().empty() ? nullptr : m_canvas->Volumes()[0];
			m_tree->SelectVolume(va);
			m_sel->SetVolume(va);
		}, false);
		add("ruler mode", [=]() {
			m_ruler_tool->ClearRulers();
			m_ruler_tool->SetRulerType(0);
			m_ruler_tool->SetIntMode(5);
			check(m_ruler_tool->GetIntMode() == 5 && m_brush_tool->GetIntMode() == 5, "2pt ruler mode: ruler and brush in int mode 5");
			m_brush_tool->SetTestShift(1);
			m_brush_tool->UpdateBrushStateNow();
			check(m_brush_tool->GetIntMode() == 7 && m_ruler_tool->GetIntMode() == 7 && m_brush_tool->GetPaintMode() == 1,
				QString("Shift: brush %1, ruler %2 (wx SetBrush 5 -> 7), selector mode %3 (1)").arg(m_brush_tool->GetIntMode()).arg(m_ruler_tool->GetIntMode()).arg(m_brush_tool->GetPaintMode()));
		}, false);
		add("paint point 1", [=]() {
			stroke(cl->first - QPoint(4, 0), cl->first + QPoint(4, 0));
			std::vector<Ruler*>* rl = m_ruler_tool->GetRulerList();
			Ruler* r = rl->empty() ? nullptr : rl->back();
			FLIVR::Point* p = r && r->GetNumPoint() > 0 ? r->GetPoint(0) : nullptr;
			if (p) log("ruler point 0: %.2f %.2f %.2f, info %ls = %ls", p->x(), p->y(), p->z(), r->GetInfoNames().c_str(), r->GetInfoValues().c_str());
			check(rl->size() == 1 && r && r->GetNumPoint() == 1 && r->GetRulerType() == 0, "release: one 2pt ruler with one point");
			check(p && std::fabs(p->x() - 40.5) < 10 && std::fabs(p->y() - 50.5) < 10, "the point is the selection centre on blob 1 (40, 50)");
			check(r && r->GetInfoNames() == L"v0", "info name v0, value = selected size");
		});
		add("paint point 2", [=]() {
			check(m_brush_tool->GetIntMode() == 7, "mode 8 -> 7 after the frame");
			stroke(cl->second - QPoint(4, 0), cl->second + QPoint(4, 0));
			std::vector<Ruler*>* rl = m_ruler_tool->GetRulerList();
			Ruler* r = rl->empty() ? nullptr : rl->back();
			FLIVR::Point* p = r && r->GetNumPoint() > 1 ? r->GetPoint(1) : nullptr;
			if (p) log("ruler point 1: %.2f %.2f %.2f, info %ls = %ls, length %.2f", p->x(), p->y(), p->z(), r->GetInfoNames().c_str(), r->GetInfoValues().c_str(), r->GetLength());
			check(rl->size() == 1 && r && r->GetNumPoint() == 2, "second stroke: the same ruler gets its second point");
			check(p && std::fabs(p->x() - 85.5) < 12 && std::fabs(p->y() - 80.5) < 12, "point 2 = centre of blob 2 only (the press cleared the selection: wx SetPaintMode(6))");
			check(r && r->GetInfoNames() == L"v0\tv1", "info names v0, v1");
		});
		add("shift release", [=]() {
			m_brush_tool->SetTestShift(0);
			m_brush_tool->UpdateBrushStateNow();
			check(m_brush_tool->GetIntMode() == 5 && m_ruler_tool->GetIntMode() == 5, "Shift released: both back in ruler mode 5");
			m_ruler_tool->SetIntMode(1);
			check(m_brush_tool->GetIntMode() == 1, "ruler mode off: brush mode 1");
			m_brush_tool->SetTestShift(-1);
			m_brush_tool->BrushClear();
		});
		// ---- 8: tablet pressure
		auto counts = std::make_shared<std::vector<long long>>();
		auto mask_count = [this]() -> long long {
			VolumeData* vd = CurrentVolume();
			if (!vd) return -1;
			auto m = vd->GetMask(true);
			return m ? CountNZ(m->getNrrd(), Voxels(vd)) : 0;
		};
		auto pen_stroke = [=](bool use_pres, double pen) {
			add(QString("stroke use_pres %1 pen %2").arg(use_pres).arg(pen), [=]() {
				m_brush_tool->SetBrushUsePres(use_pres);
				m_brush_tool->SelectBrushMode(2);
				const QPoint a = cl->first - QPoint(10, 0), b = cl->first + QPoint(10, 0);
				if (pen > 0) cv->SimulateTablet(QEvent::TabletPress, a, pen, Qt::LeftButton);
				cv->SimulateMouse(QEvent::MouseMove, a, Qt::NoButton, Qt::NoButton);
				cv->SimulateMouse(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
				for (int i = 1; i <= 10; i++)
				{
					const QPoint p = a + (b - a) * i / 10;
					if (pen > 0) cv->SimulateTablet(QEvent::TabletMove, p, pen, Qt::LeftButton);
					cv->SimulateMouse(QEvent::MouseMove, p, Qt::NoButton, Qt::LeftButton);
				}
				const double pr = m_brush_tool->GetPressure();
				if (pen > 0) cv->SimulateTablet(QEvent::TabletRelease, b, 0.0, Qt::NoButton);
				cv->SimulateMouse(QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
				m_brush_tool->SelectBrushMode(0);
				bool act; double cp;
				cv->GetTabletPressure(act, cp);
				log("use_pres %d, pen %.2f: brush pressure while drawing %.2f; after release pen down %d pressure %.2f", (int)use_pres, pen, pr, (int)act, cp);
				check(std::fabs(pr - (pen > 0 ? pen * 2.0 : 1.0)) < 1e-6 && !act, QString("brush pressure %1 (pen %2 x 2, mouse 1.0)").arg(pr).arg(pen));
			});
			add("count", [=]() {
				counts->push_back(mask_count());
				log("mask voxels: %lld", counts->back());
				m_brush_tool->BrushClear();
			});
		};
		pen_stroke(false, 0.0);    // mouse, as before
		pen_stroke(true, 0.0);     // pressure on, no pen: 1.0
		pen_stroke(true, 0.25);    // pen at 0.25 -> 0.5: half the radius
		pen_stroke(false, 0.25);   // pen, pressure off: as the mouse
		add("tablet compare", [=]() {
			if (counts->size() != 4) { check(false, "4 strokes"); return; }
			const auto& c = *counts;
			check(c[0] > 0 && c[0] == c[1] && c[0] == c[3], QString("mouse / pressure-without-pen / pen-with-pressure-off strokes select the same voxels (%1 %2 %3)").arg(c[0]).arg(c[1]).arg(c[3]));
			check(c[2] > 0 && c[2] < c[0], QString("pen pressure 0.5 with pressure on: a thinner stroke (%1 < %2)").arg(c[2]).arg(c[0]));
			m_brush_tool->SetBrushUsePres(false);
		});
	}

	// =================================================================== NR erase in NA mode
	if (kind == "b3_nrna")
	{
		auto trigger = [this](const QString& entry) {
			for (QAction* a : m_analysis_menu->actions()) if (a->text().startsWith(entry)) { a->trigger(); return; }
		};
		auto erase = [=](bool na, const QString& expect) {
			add(QString("erase na %1").arg(na), [=]() {
				VolumeData* vd = find_vol("ball");
				if (!vd || !vd->GetLabel(false)) { check(false, "ball.nrrd with its label"); return; }
				for (VolumeData* v : m_canvas->Volumes()) v->SetDisp(v == vd);
				m_tree->SelectVolume(vd);
				vd->SetNAMode(na);
				if (!vd->GetMask(false)) vd->AddEmptyMask();
				trigger("Noise Reduction");
				QPushButton* btn = nullptr;
				if (m_nr_dlg) for (QPushButton* b : m_nr_dlg->findChildren<QPushButton*>()) if (b->text() == "Erase") btn = b;
				if (!btn) { check(false, "Noise Reduction Erase button"); return; }
				const std::vector<VolumeData*> before = m_canvas->Volumes();
				btn->click();
				VolumeData* r = nullptr;
				for (VolumeData* v : m_canvas->Volumes())
					if (std::find(before.begin(), before.end(), v) == before.end()) r = v;
				log("NA mode %d: Erase -> %ls", (int)na, r ? r->GetName().c_str() : L"(none)");
				check(r && r->GetName().ToQt().endsWith(expect), QString("NA mode %1: Erase = Calculate(%2), result *%3").arg(na).arg(na ? 11 : 6).arg(expect));
				m_nr_dlg->hide();
				vd->SetNAMode(false);
			});
		};
		erase(true, "_EXTRACTED");   // VolumeCalculator names type 11 results _EXTRACTED
		erase(false, "_DELETED");
	}

	// =================================================================== 4D script
	if (kind == "b3_script4d")
	{
		auto vol = [this]() { return m_canvas->Volumes().empty() ? nullptr : m_canvas->Volumes()[0]; };
		auto write_script = [st](const QString& name, const QString& body) {
			const QString f = st->dir + "/" + name + ".txt";
			QFile o(f);
			o.open(QIODevice::WriteOnly | QIODevice::Text);
			o.write(body.toUtf8());
			return f;
		};
		auto set_script = [=](const QString& f) {
			m_settings->SetRunScript(true);
			m_settings->SetScriptFile(f);
		};
		auto read_nrrd_count = [](const QString& f, int value) -> long long {
			// count voxels == value (value < 0: != 0) of a raw uint8 / uint32 test nrrd
			QFile in(f);
			if (!in.open(QIODevice::ReadOnly)) return -1;
			QByteArray all = in.readAll();
			const int hdr = all.indexOf("\n\n");
			const bool u32 = all.left(hdr).contains("uint32");
			const char* d = all.constData() + hdr + 2;
			const long long n = (all.size() - hdr - 2) / (u32 ? 4 : 1);
			long long c = 0;
			for (long long i = 0; i < n; i++)
			{
				const unsigned int v = u32 ? ((const unsigned int*)d)[i] : (unsigned char)d[i];
				c += value < 0 ? v != 0 : v == (unsigned int)value;
			}
			return c;
		};
		const QString d4 = qEnvironmentVariable("VVD_QT_B3_DATA4D");
		add("series", [=]() {
			VolumeData* vd = vol();
			int s, e, c;
			m_canvas->Get4DSeqFrames(s, e, c);
			log("series %ls: frames %d..%d (cur %d), time num %d", vd ? vd->GetName().c_str() : L"-", s, e, c, vd && vd->GetReader() ? vd->GetReader()->GetTimeNum() : 0);
			check(vd && e - s == 2, "3-time-point series loaded");
			m_tree->SelectVolume(vd);
		});
		// script disabled: nothing happens
		add("disabled", [=]() {
			const QString f = write_script("fetch", "[tasks]\ntasknum=1\n[tasks/task0]\ntype=fetch_mask\n");
			m_settings->SetRunScript(false);
			m_settings->SetScriptFile(f);
			m_canvas->Set4DSeqFrame(1);
			check(!m_canvas->GetRun4DScript() && m_canvas->GetScriptFile() == f, "Set4DSeqFrame reads the 4D Script settings (run off)");
			VolumeData* vd = vol();
			const long long before = vd->GetMask(false) ? CountNZ(vd->GetMask(false)->getNrrd(), Voxels(vd)) : -2;
			Run4DScript();
			const long long after = vd->GetMask(false) ? CountNZ(vd->GetMask(false)->getNrrd(), Voxels(vd)) : -2;
			check(before == after, "run off: Run4DScript does nothing");
		});
		add("fetch_mask", [=]() {
			set_script(st->dir + "/fetch.txt");
			m_canvas->Set4DSeqFrame(1);
			VolumeData* vd = vol();
			Run4DScript();
			const long long mk = vd->GetMask(false) ? CountNZ(vd->GetMask(false)->getNrrd(), Voxels(vd)) : -1;
			const long long lb = vd->GetLabel(false) ? CountNZ(vd->GetLabel(false)->getNrrd(), Voxels(vd)) : -1;
			const long long emk = read_nrrd_count(d4 + "/ts_T2.msk", -1), elb = read_nrrd_count(d4 + "/ts_T2.lbl", -1);
			log("frame 1 (%ls): mask %lld (ts_T2.msk %lld), label %lld (ts_T2.lbl %lld)", vd->GetName().c_str(), mk, emk, lb, elb);
			check(mk == emk && lb == elb && emk > 0, "fetch_mask: mask and label of the new time point (ts_T2.msk / .lbl)");
		});
		add("selection_tracking", [=]() {
			const QString f = write_script("track", "[tasks]\ntasknum=1\n[tasks/task0]\ntype=selection_tracking\nsize_limit=10\n");
			set_script(f);
			VolumeData* vd = vol();
			m_canvas->Set4DSeqFrame(2);   // data T3; mask / label of T2 still loaded
			Run4DScript();
			const long long mk = vd->GetMask(false) ? CountNZ(vd->GetMask(false)->getNrrd(), Voxels(vd)) : -1;
			const long long e5 = read_nrrd_count(d4 + "/ts_T3.lbl", 5), e7 = read_nrrd_count(d4 + "/ts_T3.lbl", 7);
			log("frame 2: mask %lld; ts_T3.lbl label 5: %lld, label 7: %lld", mk, e5, e7);
			// the T2 mask (label 1) selects T2 label 1; label value 1 does not occur in T3 (5 / 7): empty mask, as wx without a trace group
			check(mk == 0, "selection_tracking without a trace group: the selected label values (1) are kept, T3 has none -> empty mask");
			const long long lb = vd->GetLabel(false) ? read_nrrd_count(d4 + "/ts_T3.lbl", -1) : -1;
			check(vd->GetLabel(false) && CountNZ(vd->GetLabel(false)->getNrrd(), Voxels(vd)) == lb, "the T3 label replaced the old one");
		});
		// same label values: T1 -> T2 (labels 1 / 2 in both). The fetched mask
		// reaches the GPU in the frames between the steps (in a playback the
		// previous time point was drawn before the script runs again)
		add("selection_tracking T1", [=]() {
			// the view takes the script settings at Set4DSeqFrame (wx): set them first
			set_script(st->dir + "/fetch.txt");
			m_canvas->Set4DSeqFrame(0);
			Run4DScript();                 // T1 mask (label 1) + label
			VolumeData* vd = vol();
			log("T1 fetched: %ls, mask (system) %lld", vd->GetName().c_str(), vd->GetMask(false) ? CountNZ(vd->GetMask(false)->getNrrd(), Voxels(vd)) : -1);
		});

		add("selection_tracking T2", [=]() {
			VolumeData* vd = vol();
			set_script(st->dir + "/track.txt");
			auto cnt = [&](bool ret) { auto m = vd->GetMask(ret); return m ? CountNZ(m->getNrrd(), Voxels(vd)) : -1; };
			log("T1 state: %ls, mask (system memory) %lld, label %lld", vd->GetName().c_str(), cnt(false),
				vd->GetLabel(false) ? CountNZ(vd->GetLabel(false)->getNrrd(), Voxels(vd)) : -1);
			m_canvas->Set4DSeqFrame(1);
			log("after Set4DSeqFrame(1): %ls, mask (system) %lld, mask (read back) %lld", vd->GetName().c_str(), cnt(false), cnt(true));
			Run4DScript();
			const long long mk2 = vd->GetMask(false) ? CountNZ(vd->GetMask(false)->getNrrd(), Voxels(vd)) : -1;
			const long long e1 = read_nrrd_count(d4 + "/ts_T2.lbl", 1);
			log("T1 -> T2: mask %lld, ts_T2.lbl label 1: %lld", mk2, e1);
			check(mk2 == e1 && e1 > 0, "selection_tracking T1 -> T2: the mask follows label 1 into the new time point");
		});
		add("noise_reduction", [=]() {
			const QString out = st->dir + "/nr/";
			QDir(out).removeRecursively();
			const QString f = write_script("nr", QString("[tasks]\ntasknum=1\n[tasks/task0]\ntype=noise_reduction\nthreshold=100\nvoxelsize=5\nformat=2\nsavepath=%1\n").arg(out));
			set_script(f);
			m_canvas->Set4DSeqFrame(1);
			const int nvol = (int)m_canvas->Volumes().size();
			Run4DScript();
			const QStringList files = QDir(out).entryList(QDir::Files);
			log("noise_reduction wrote: %s", files.join(", ").toLocal8Bit().constData());
			check(files.size() == 1 && files[0] == "ts_T2.nrrd_DELETED_T1.nrrd" && (int)m_canvas->Volumes().size() == nvol,
				"noise_reduction: <savepath><name>_DELETED_T<frame>.nrrd saved, nothing added to the view");
			VolumeData* vd = vol();
			check(!vd->GetMask(false) && !vd->GetLabel(false), "mask and label deleted afterwards (wx DeleteMask / DeleteLabel)");
			if (!files.isEmpty())
			{
				const long long in_nz = read_nrrd_count(d4 + "/ts_T2.nrrd", -1);
				const long long out_nz = read_nrrd_count(out + files[0], -1);
				log("noise_reduction: input nonzero %lld, saved nonzero %lld (3 isolated noise voxels in the input)", in_nz, out_nz);
				check(in_nz - out_nz == 3, "noise_reduction: components below 5 voxels (the 3 single noise voxels) erased, the blobs kept");
			}
		});
		add("random_colors", [=]() {
			const QString out = st->dir + "/rc/";
			QDir(out).removeRecursively();
			const QString f = write_script("rc", QString("[tasks]\ntasknum=1\n[tasks/task0]\ntype=random_colors\nhuemode=1\nsavepath=%1\n").arg(out));
			set_script(f);
			m_canvas->Set4DSeqFrame(2);
			Run4DScript();
			const QStringList files = QDir(out).entryList(QDir::Files);
			log("random_colors wrote: %s", files.join(", ").toLocal8Bit().constData());
			check(files.size() == 3 && files.filter("_T2_COMP").size() == 3,
				"random_colors: three <name>_T2_COMP<i>.tif");
		});
		add("separate_channels", [=]() {
			const QString out = st->dir + "/sc/";
			QDir(out).removeRecursively();
			const QString f = write_script("sc", QString("[tasks]\ntasknum=2\n[tasks/task0]\ntype=external_exe\n[tasks/task1]\ntype=separate_channels\nformat=2\nsavepath=%1\n").arg(out));
			set_script(f);
			m_canvas->Set4DSeqFrame(0);
			Run4DScript();
			const QStringList files = QDir(out).entryList(QDir::Files);
			log("separate_channels wrote: %s", files.join(", ").toLocal8Bit().constData());
			check(files.contains("ts_T1.nrrd_T0.nrrd"), "separate_channels: <name>_T0.nrrd (+ its label, wx Save defaults; external_exe ignored)");
		});
		// playback in batch mode (wx VMovieView OnPrev / OnTimer)
		add("playback", [=]() {
			const QString out = st->dir + "/play/";
			QDir(out).removeRecursively();
			const QString f = write_script("play", QString("[tasks]\ntasknum=1\n[tasks/task0]\ntype=separate_channels\nformat=2\nsavepath=%1\n").arg(out));
			set_script(f);
			m_canvas->Set4DSeqFrame(0);   // wx: the view reads the script settings here
			m_movie->m_rot_chk->setChecked(false);
			m_movie->m_seq_chk->setChecked(true);
			m_movie->m_time_start_text->setText("0");
			m_movie->m_time_end_text->setText("2");
			m_movie->m_fps_text->setText("10");
			m_movie->m_movie_time->setText("1.0");
			st->hold = true;
			auto conn = std::make_shared<QMetaObject::Connection>();
			*conn = connect(m_movie, &MoviePanel::runningChanged, this, [=](bool running) {
				if (running) return;
				disconnect(*conn);
				const QStringList files = QDir(out).entryList(QDir::Files);
				log("playback wrote: %s", files.join(", ").toLocal8Bit().constData());
				int nn = 0;
				for (const QString& fn : files) nn += fn.endsWith(".nrrd");
				check(nn == 3 && files.contains("ts_T1.nrrd_T0.nrrd") && files.contains("ts_T2.nrrd_T1.nrrd") && files.contains("ts_T3.nrrd_T2.nrrd"),
					"playback with the script on: the script ran once per time point (T0, T1, T2)");
				st->hold = false;
			});
			m_movie->Play();
		}, false);
		add("playback off", [=]() {
			const QString out = st->dir + "/play/";
			QDir(out).removeRecursively();
			m_settings->SetRunScript(false);
			m_canvas->Set4DSeqFrame(0);
			st->hold = true;
			auto conn = std::make_shared<QMetaObject::Connection>();
			*conn = connect(m_movie, &MoviePanel::runningChanged, this, [=](bool running) {
				if (running) return;
				disconnect(*conn);
				check(QDir(out).entryList(QDir::Files).isEmpty(), "playback with the script off: nothing written");
				st->hold = false;
			});
			m_movie->Play();
		}, false);
	}

	// =================================================================== test modes
	if (kind == "b3_modes")
	{
		auto f0 = std::make_shared<long long>(0);
		add("modes", [=]() {
			check(m_settings->GetTestMode(1) && m_settings->GetTestMode(2) && m_settings->GetTestMode(3), "VVD_QT_TEST_MODE=speed,param,wiref");
			check(m_tester && m_tester->isVisible() && m_tester->windowTitle() == "Tester" && m_tester->findChild<QPushButton*>("B1Btn"),
				"param: the Tester dialog (\"Click Me\") is shown");
			if (QPushButton* b = m_tester->findChild<QPushButton*>("B1Btn")) b->click();   // no-op, as wx
			check(m_canvas->GetTestSpeed() && m_canvas->GetDrawGrid() && m_data_mgr->m_vol_test_wiref, "speed / wireframe (grid, m_vol_test_wiref) on");
			*f0 = m_canvas->PresentedFrames();
		}, false);
		add("speed", [=]() {
			// one second idle
			QElapsedTimer t; t.start();
			while (t.elapsed() < 1000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
			const long long n = m_canvas->PresentedFrames() - *f0;
			log("speed test: %lld frames in 1 s idle", n);
			check(n > 20, "speed test mode: the view redraws continuously");
		}, false);
		capture("grid");
		add("grid off", [=]() { m_canvas->SetTestWiref(false); m_canvas->SetTestSpeed(false); });
		capture("nogrid");
		add("grid compare", [=]() {
			const long long d = pixdiff("grid", "nogrid");
			check(d > 0, QString("wireframe test mode draws the grid (%1 pixels differ)").arg(d));
			*f0 = m_canvas->PresentedFrames();
			QElapsedTimer t; t.start();
			while (t.elapsed() < 1000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
			const long long n = m_canvas->PresentedFrames() - *f0;
			check(n < 10, QString("speed off: no constant redraw (%1 frames in 1 s)").arg(n));
		}, false);
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
		if (st->waiting_capture || st->hold)
			return;
		if (st->settling)
		{
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
		s.fn();
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
