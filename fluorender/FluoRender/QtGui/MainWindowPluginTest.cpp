// Testing aid for batch 4b (plugins): VVD_QT_TEST=b4b_<kind> (+ VVD_QT_GRAB,
// VVD_QT_TEST_DIR), "[b4b] PASS/FAIL" lines on stderr, pictures and dumps in
// the test directory, quits. See docs/QT_PORT.md ("Batch 4b").
//
//   b4b_ui      the Plugins button / menu, one pane per plugin, floating,
//               RunPlugin / PluginExists, /ui_layout keys of projects (none
//               before a pane exists), p_plugins.vrp for b4b_uirt
//   b4b_uirt    p_plugins.vrp on the command line: the panes come back
//   b4b_fiji    the Fiji Interface with a real Fiji (VVD_QT_FIJI_PATH): the
//               loaded volume(s) sent with VVD_QT_FIJI_CMD (default
//               "Invert,stack"), Fiji started from the plugin, the result
//               received back; raw dumps <dir>/fiji_sent_<i>.raw /
//               fiji_recv_<i>.raw for an independent check; VVD_QT_FIJI_MASK=1
//               also sends a mask; the command again on the connected Fiji
//   b4b_ipc     the IPC server alone (a client script drives it; quits after
//               VVD_QT_IPC_WAIT ms)
//   b4b_nblast  NBLAST Search on the selected mesh (an SWC on the command
//               line) with the databases of VVD_QT_NBLAST_DBLIST; list,
//               previews, delete / undo / redo, double click, save with the
//               export options, import, the dialogs
//   b4b_na      the Neuron Annotator from -p "NAPlugin -l .. -v .." on the
//               command line: segments, MIPs, list check / select / double
//               click, thumbnails following the width, export combined
//               fragments, the project (p_na.vrp) for b4b_nart
//   b4b_nart    p_na.vrp on the command line: NA state restored

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
#include "PaneScrollArea.h"
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTreeWidget>
#include <QHeaderView>
#include <QProcess>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTcpSocket>
#include <QProgressDialog>
#include <functional>
#include <memory>
#include <vector>
#include <cmath>

#include "QtPluginManager.h"
#include "RenderCanvas.h"
#include "TreePanel.h"
#include "DataManager.h"
#include "Core/FileUtil.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/Texture.h"
#include "FijiInterfacePluginQt.h"
#include "WxIpcServer.h"
#include "NBLASTPluginQt.h"
#include "NAPluginQt.h"

namespace
{
	struct PSeq
	{
		std::vector<std::function<void(std::function<void()>)>> steps;
		size_t i = 0;
		void add(std::function<void(std::function<void()>)> f) { steps.push_back(std::move(f)); }
		void run(QObject* ctx, const std::shared_ptr<PSeq>& self, int delay = 0)
		{
			QTimer::singleShot(delay, ctx, [ctx, self]() {
				if (self->i >= self->steps.size()) return;
				auto f = self->steps[self->i++];
				f([ctx, self]() { self->run(ctx, self, 50); });
			});
		}
	};
	QByteArray VolumeBytes(VolumeData* vd, bool mask = false)
	{
		if (!vd) return QByteArray();
		auto n = mask ? vd->GetMask(true) : vd->GetVolume(true);
		if (!n || !n->getNrrd() || !n->getNrrd()->data) return QByteArray();
		int nx, ny, nz;
		vd->GetResolution(nx, ny, nz);
		const int t = n->getNrrd()->type;
		const size_t bytes = (t == nrrdTypeUChar || t == nrrdTypeChar) ? 1 : (t == nrrdTypeUShort || t == nrrdTypeShort) ? 2 : 4;
		return QByteArray((const char*)n->getNrrd()->data, (qsizetype)((size_t)nx * ny * nz * bytes));
	}
	bool WriteFile(const QString& f, const QByteArray& b)
	{
		QFile file(f);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
		return file.write(b) == b.size();
	}
	QString ReadText(const QString& f)
	{
		QFile file(f);
		if (!file.open(QIODevice::ReadOnly)) return QString();
		return QString::fromUtf8(file.readAll());
	}
	void SendKey(QWidget* w, int key, Qt::KeyboardModifiers m = Qt::NoModifier)
	{
		QKeyEvent press(QEvent::KeyPress, key, m);
		QKeyEvent rel(QEvent::KeyRelease, key, m);
		QApplication::sendEvent(w, &press);
		QApplication::sendEvent(w, &rel);
	}
}

void MainWindow::RunPluginTest(const QString& kind)
{
	const QString dir = qEnvironmentVariableIsSet("VVD_QT_TEST_DIR") ? qEnvironmentVariable("VVD_QT_TEST_DIR") :
		QFileInfo(qEnvironmentVariable("VVD_QT_GRAB")).absolutePath();
	QDir().mkpath(dir);
	auto fails = std::make_shared<int>(0);
	auto log = [](const QString& s) { fprintf(stderr, "[b4b] %s\n", s.toLocal8Bit().constData()); fflush(stderr); };
	auto check = [fails, log](bool ok, const QString& what) {
		if (!ok) (*fails)++;
		log(QString(ok ? "PASS " : "FAIL ") + what);
	};
	auto seq = std::make_shared<PSeq>();
	auto wait = [seq](int ms) { seq->add([ms](std::function<void()> next) { QTimer::singleShot(ms, next); }); };
	auto step = [seq](std::function<void()> f) { seq->add([f](std::function<void()> next) { f(); next(); }); };
	// poll cond every 100 ms until true or timeout_ms
	auto wait_until = [seq, log](std::function<bool()> cond, int timeout_ms, const QString& what) {
		seq->add([cond, timeout_ms, what, log](std::function<void()> next) {
			auto t = std::make_shared<QElapsedTimer>();
			t->start();
			auto timer = new QTimer();
			QObject::connect(timer, &QTimer::timeout, [timer, t, cond, timeout_ms, what, next, log]() {
				const bool ok = cond();
				if (ok || t->elapsed() > timeout_ms)
				{
					timer->stop();
					timer->deleteLater();
					log(QString("%1: %2 after %3 ms").arg(what, ok ? "done" : "TIMEOUT").arg(t->elapsed()));
					next();
				}
			});
			timer->start(100);
		});
	};
	auto finish = [seq, fails, log, this, dir]() {
		seq->add([fails, log, this, dir](std::function<void()>) {
			grab().save(dir + "/window_end.png");
			log(QString("done: %1 failure(s)").arg(*fails));
			QCoreApplication::quit();
		});
	};
	auto grab_dock = [this, dir](const QString& name, const QString& file) {
		if (QDockWidget* d = PluginDock(name))
			d->grab().save(dir + "/" + file);
	};
	auto prj_value = [](const QString& prj, const QString& key) {
		vvd::IniFile ini;
		ini.Load(VVDString(prj));
		VVDString v;
		return ini.Read(VVDString(key), v) ? v.ToQt() : QString("<missing>");
	};

	if (kind == "b4b_ui")
	{
		step([=]() {
			check(m_plugin_manager && m_plugin_manager->GetGuiPlugins().size() == 3, "three GUI plugins loaded");
			QStringList texts;
			for (QAction* a : m_tb_menu_plugin->actions()) texts << a->text();
			log("plugin menu: " + texts.join(" | "));
			check(texts == QStringList({ "Fiji Interface", "NBLAST Search", "Neuron Annotator Plugin" }),
				"menu entries = display names in the wx order");
			check(m_plugins_act && m_plugins_act->isEnabled(), "Plugins toolbar button enabled");
			check(m_plugin_list == QStringList({ "Fiji Interface", "NBLAST Search", "NAPlugin" }), "plugin names (wx GetName)");
			check(PluginExists("NAPlugin") && PluginExists("Fiji Interface") && !PluginExists("Neuron Annotator Plugin"),
				"PluginExists by name");
			check(!RunPlugin("NoSuchPlugin", QString()), "RunPlugin of an unknown plugin returns false");
			check(!IsCreatedPluginWindow("NBLAST Search"), "no pane before it is shown");
			check(GetPluginManager()->GetPluginsPath().endsWith("/PlugIns/qt"), "plugins folder " + QtPluginManager::GetPluginsPath());
			// a project before any pane exists: no plugin keys, no _files folder
			const QString p0 = dir + "/p_noplugin.vrp";
			QDir(p0 + "_files").removeRecursively();
			SaveProject(p0);
			check(prj_value(p0, "/ui_layout/Fiji Interface") == "<missing>" && !QFileInfo::exists(p0 + "_files"),
				"project without plugin panes: no plugin keys / folder");
		});
		step([=]() {
			// the menu entries (wx OnPluginMenuSelect)
			for (QAction* a : m_tb_menu_plugin->actions()) a->trigger();
		});
		wait(800);
		step([=]() {
			for (const QString& n : { QString("Fiji Interface"), QString("NBLAST Search"), QString("NAPlugin") })
			{
				QDockWidget* d = PluginDock(n);
				check(d && IsShownPluginWindow(n) && d->widget(), "pane shown: " + n + (d ? " (" + d->windowTitle() + ")" : QString()));
			}
			check(PluginDock("NAPlugin") && PluginDock("NAPlugin")->windowTitle() == "Neuron Annotator Plugin", "pane caption = display name");
			grab_dock("Fiji Interface", "pane_fiji.png");
			grab_dock("NBLAST Search", "pane_nblast.png");
			grab_dock("NAPlugin", "pane_na.png");
			grab().save(dir + "/window_panes.png");
			// popup position of the toolbar menu
			if (QWidget* btn = m_main_tb->widgetForAction(m_plugins_act))
				check(btn->isEnabled() && btn->objectName() == "PluginsButton", "toolbar button widget");
			PluginDock("NBLAST Search")->setFloating(true);
			ToggleVisibilityPluginWindow("Fiji Interface", false);
		});
		wait(500);
		step([=]() {
			check(!IsShownPluginWindow("Fiji Interface") && IsCreatedPluginWindow("Fiji Interface"), "hidden, still created");
			check(PluginDock("NBLAST Search")->isFloating(), "NBLAST pane floating");
			const QString p1 = dir + "/p_plugins.vrp";
			QDir(p1 + "_files").removeRecursively();
			SaveProject(p1);
			check(prj_value(p1, "/ui_layout/Fiji Interface") == "0" && prj_value(p1, "/ui_layout/Fiji Interface_float") == "0",
				"project: Fiji Interface=0, _float=0");
			check(prj_value(p1, "/ui_layout/NBLAST Search") == "1" && prj_value(p1, "/ui_layout/NBLAST Search_float") == "1",
				"project: NBLAST Search=1, _float=1");
			check(prj_value(p1, "/ui_layout/Neuron Annotator Plugin") == "1" && prj_value(p1, "/ui_layout/Neuron Annotator Plugin_float") == "0",
				"project: Neuron Annotator Plugin=1, _float=0");
			const QString set = prj_value(p1, "/ui_layout/Neuron Annotator Plugin_setting");
			check(set == p1 + "_files/Neuron Annotator Plugin.set" && QFileInfo::exists(set), "NA setting file: " + set);
			check(prj_value(p1, "/ui_layout/Fiji Interface_setting") == "<missing>", "no setting key for plugins without a file");
			log("NA .set:\n" + ReadText(set));
			// back to docked for the window picture
			PluginDock("NBLAST Search")->setFloating(false);
		});
		finish();
	}
	else if (kind == "b4b_lib")
	{
		// a shared-library plugin in the plugins folder (a non-GUI one)
		step([=]() {
			QStringList texts;
			for (QAction* a : m_tb_menu_plugin->actions()) texts << a->text();
			log("plugin menu: " + texts.join(" | "));
			check(m_plugin_manager->GetNonGuiPlugins().size() == 1 && texts.size() == 4 && texts.last() == "Sample NonGui",
				"library plugin loaded, last menu entry (non-GUI plugins after the GUI ones, wx)");
			m_tb_menu_plugin->actions().last()->trigger();   // wx OnPluginMenuSelect -> Work()
			check(QFileInfo::exists(qEnvironmentVariable("VVD_SAMPLE_OUT")), "menu entry ran Work()");
			check(RunPlugin("Sample NonGui", QString()), "RunPlugin of a non-GUI plugin");
		});
		finish();
	}
	else if (kind == "b4b_uirt")
	{
		wait(1500);
		step([=]() {
			check(IsCreatedPluginWindow("NBLAST Search") && IsShownPluginWindow("NBLAST Search"), "NBLAST pane restored, shown");
			check(PluginDock("NBLAST Search") && PluginDock("NBLAST Search")->isFloating(), "NBLAST pane floating again");
			check(IsShownPluginWindow("NAPlugin") && !PluginDock("NAPlugin")->isFloating(), "NA pane restored, docked");
			check(!IsCreatedPluginWindow("Fiji Interface"), "Fiji pane (saved hidden, never created) not created (wx)");
			PluginDock("NBLAST Search")->setFloating(false);
		});
		finish();
	}
	else if (kind == "b4b_fiji")
	{
		auto* fiji = dynamic_cast<FijiInterfacePluginQt*>(m_plugin_manager->GetGuiPlugin("Fiji Interface"));
		const QString cmd = qEnvironmentVariableIsSet("VVD_QT_FIJI_CMD") ? qEnvironmentVariable("VVD_QT_FIJI_CMD") : QString("Invert,stack");
		const bool with_mask = qEnvironmentVariableIntValue("VVD_QT_FIJI_MASK") != 0;
		struct St { std::vector<QString> names; std::vector<QByteArray> sent; int idx = 0; int received = 0; QStringList recv_before; };
		auto st = std::make_shared<St>();
		auto panel = [this]() -> FijiInterfacePanel* {
			QDockWidget* d = PluginDock("Fiji Interface");
			return d ? qobject_cast<FijiInterfacePanel*>(DockContent(d)) : nullptr;
		};
		step([=]() {
			check(fiji != nullptr, "Fiji Interface plugin");
			check(fiji && fiji->Server() && fiji->Server()->IsListening(), QString("IPC server listening on port %1").arg(fiji ? fiji->Server()->Port() : 0));
			for (VolumeData* vd : m_canvas->Volumes()) st->names.push_back(vd->GetName().ToQt());
			check(!st->names.empty(), QString("%1 volume(s) loaded").arg(st->names.size()));
			ToggleVisibilityPluginWindow("Fiji Interface", true);
		});
		wait(500);
		// each loaded volume: select, send the command, wait for the result
		const int nvol = 3;
		for (int vi = 0; vi < nvol; vi++)
		{
			step([=]() {
				st->idx = vi;
				if (vi >= (int)st->names.size()) return;
				VolumeData* vd = m_data_mgr->GetVolumeData(VVDString(st->names[vi]));
				m_tree->SelectVolume(vd);
				check(GetCurSelVol() == vd, "selected " + st->names[vi]);
				if (with_mask && vi == 0 && vd)
				{
					// a mask: the voxels above 100 (sent instead of the volume)
					auto v = vd->GetVolume(false);
					int nx, ny, nz; vd->GetResolution(nx, ny, nz);
					const size_t n = (size_t)nx * ny * nz;
					const unsigned char* src = (const unsigned char*)v->getNrrd()->data;
					auto* dst = new unsigned char[n];
					for (size_t i = 0; i < n; i++) dst[i] = src[i] > 100 ? 255 : 0;
					Nrrd* mn = nrrdNew();
					nrrdWrap(mn, dst, nrrdTypeUChar, 3, (size_t)nx, (size_t)ny, (size_t)nz);
					double sx, sy, sz; vd->GetSpacings(sx, sy, sz);
					nrrdAxisInfoSet(mn, nrrdAxisInfoSpacing, sx, sy, sz);
					nrrdAxisInfoSet(mn, nrrdAxisInfoMax, sx * nx, sy * ny, sz * nz);
					nrrdAxisInfoSet(mn, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
					nrrdAxisInfoSet(mn, nrrdAxisInfoSize, (size_t)nx, (size_t)ny, (size_t)nz);
					vd->LoadMask(std::make_shared<FLIVR::VL_Nrrd>(mn));
				}
				const QByteArray sent = VolumeBytes(vd, with_mask && vi == 0);
				st->sent.push_back(sent);
				WriteFile(dir + QString("/fiji_sent_%1.raw").arg(vi), sent);
				int nx, ny, nz; vd->GetResolution(nx, ny, nz);
				log(QString("sending %1 (%2x%3x%4, %5 bytes) with \"%6\"%7").arg(st->names[vi]).arg(nx).arg(ny).arg(nz)
					.arg(sent.size()).arg(cmd).arg(with_mask && vi == 0 ? " (mask)" : ""));
				st->recv_before = fiji->ReceivedVolumes();
				FijiInterfacePanel* p = panel();
				p->CommandText()->setText(cmd);
				if (p->SendMaskCheck()->isChecked() != (with_mask && vi == 0)) p->SendMaskCheck()->click();
				p->SendButton()->click();   // wx OnSENDEVENTBUTTONClick
			});
			wait_until([=]() {
				if (vi >= (int)st->names.size()) return true;
				FijiInterfacePanel* p = panel();
				return p && !p->IsWaiting() && fiji->ReceivedVolumes().size() > st->recv_before.size();
			}, 240000, QString("Fiji result for volume %1").arg(vi));
			step([=]() {
				if (vi >= (int)st->names.size()) return;
				FijiInterfacePanel* p = panel();
				check(fiji->isReady(), "connected and confirmed (isReady)");
				check(p->VersionText()->text() == "finished", "info text: " + p->VersionText()->text());
				log("last received: " + fiji->LastReceived());
				const QString rname = fiji->ReceivedVolumes().isEmpty() ? QString() : fiji->ReceivedVolumes().last();
				VolumeData* rvd = m_data_mgr->GetVolumeData(VVDString(rname));
				check(rvd != nullptr, "received volume in the data manager: " + rname);
				if (!rvd) return;
				const bool masked = with_mask && vi == 0;
				const QByteArray recv = VolumeBytes(rvd, masked);
				WriteFile(dir + QString("/fiji_recv_%1.raw").arg(vi), recv);
				check(recv.size() == st->sent[vi].size(), QString("same size back (%1 bytes)").arg(recv.size()));
				if (masked)
				{
					check(rname != st->names[vi] && m_data_mgr->GetVolumeData(VVDString(st->names[vi])),
						"mask result: a copy of the volume with the mask (" + rname + "), original kept");
					check(VolumeBytes(rvd) == VolumeBytes(m_data_mgr->GetVolumeData(VVDString(st->names[vi]))), "copy has the original data");
				}
				else
				{
					check(rname == st->names[vi] && m_canvas->GetVolumeData(VVDString(rname)) == rvd, "same name: replaced in the view");
					check(m_data_mgr->GetVolumeNum() == (int)st->names.size() + (with_mask ? 1 : 0), QString("volume count %1").arg(m_data_mgr->GetVolumeNum()));
					check(m_tree->FindItem(rname, TreePanel::TypeVolume) != nullptr, "tree item of the replaced volume");
				}
				int bits = rvd->GetVolume(false)->getNrrd()->type == nrrdTypeUShort ? 16 : 8;
				if (bits == 16)
				{
					const unsigned short* q = (const unsigned short*)recv.constData();
					int mx = 0; for (qsizetype i = 0; i < recv.size() / 2; i++) mx = std::max(mx, (int)q[i]);
					check(std::fabs(rvd->GetMaxValue() - mx) < 1e-9 && std::fabs(rvd->GetScalarScale() - (mx > 0 ? 65535.0 / mx : 1.0)) < 1e-9,
						QString("16-bit fix-up: max %1, scalar scale %2").arg(rvd->GetMaxValue()).arg(rvd->GetScalarScale()));
				}
				// the command "Invert": every voxel v -> max - v; a check of the
				// round trip only (the exact Fiji result is compared outside)
				long long diff = 0;
				for (qsizetype i = 0; i < std::min(recv.size(), st->sent[vi].size()); i++) diff += recv[i] != st->sent[vi][i];
				log(QString("bytes changed by Fiji: %1 of %2").arg(diff).arg(recv.size()));
				m_canvas->RefreshView();
				grab().save(dir + QString("/window_fiji_%1.png").arg(vi));
			});
		}
		// the command once more on the connected Fiji (wx: no restart); with
		// "Invert,stack" the original data come back
		step([=]() {
			if (st->names.empty()) return;
			VolumeData* vd = m_data_mgr->GetVolumeData(VVDString(st->names[0]));
			m_tree->SelectVolume(vd);
			st->recv_before = fiji->ReceivedVolumes();
			FijiInterfacePanel* p = panel();
			if (p->SendMaskCheck()->isChecked()) p->SendMaskCheck()->click();
			// through RunPlugin (wx FI_RUN: "command;send_mask;override_vox")
			RunPlugin("Fiji Interface", cmd + ";false;false");
		});
		wait_until([=]() {
			FijiInterfacePanel* p = panel();
			return st->names.empty() || (p && !p->IsWaiting() && fiji->ReceivedVolumes().size() > st->recv_before.size());
		}, 120000, "second command (RunPlugin)");
		step([=]() {
			if (st->names.empty()) return;
			VolumeData* vd = m_data_mgr->GetVolumeData(VVDString(st->names[0]));
			const QByteArray twice = VolumeBytes(vd);
			WriteFile(dir + "/fiji_recv_twice.raw", twice);
			if (cmd.startsWith("Invert") && !with_mask)
				check(twice == st->sent[0], "Invert applied twice = the original data");
			check(fiji->GetTempOverrideVox() == TMP_OVOX_NONE, "temporary voxel override reset after com_finish");
		});
		finish();
	}
	else if (kind == "b4b_ipc")
	{
		auto* fiji = dynamic_cast<FijiInterfacePluginQt*>(m_plugin_manager->GetGuiPlugin("Fiji Interface"));
		bool ok = false;
		const int wait_ms = qEnvironmentVariableIntValue("VVD_QT_IPC_WAIT", &ok);
		step([=]() {
			check(fiji && fiji->Server()->IsListening(), QString("listening on %1").arg(fiji ? fiji->Server()->Port() : 0));
			fprintf(stdout, "READY %d\n", fiji ? (int)fiji->Server()->Port() : 0);
			fflush(stdout);
		});
		// the client (a script) connects, confirms, pokes; then "com"
		wait_until([=]() { return fiji && fiji->isReady(); }, ok ? wait_ms : 20000, "client confirmed");
		step([=]() {
			check(fiji->isReady(), "isReady after confirm");
			// a volume to the client (SendCurrentVolume) + a command
			if (VolumeData* vd = m_canvas->GetCurrentVolume()) m_tree->SelectVolume(vd);
			check(fiji->SendCommand("Test Command,arg=1", false), "SendCommand");
		});
		wait_until([=]() { return fiji->ReceivedVolumes().size() > 0; }, ok ? wait_ms : 20000, "volume from the client");
		wait(1500);
		step([=]() {
			log("received: " + fiji->ReceivedVolumes().join(","));
			log("last: " + fiji->LastReceived());
		});
		finish();
	}
	else if (kind == "b4b_nblast")
	{
		auto* nb = dynamic_cast<NBLASTPluginQt*>(m_plugin_manager->GetGuiPlugin("NBLAST Search"));
		auto panel = [this]() -> NBLASTPanel* {
			QDockWidget* d = PluginDock("NBLAST Search");
			return d ? qobject_cast<NBLASTPanel*>(DockContent(d)) : nullptr;
		};
		struct St { QString respath; int rows = 0; QString first; };
		auto st = std::make_shared<St>();
		step([=]() {
			check(nb != nullptr, "NBLAST plugin");
			log("Rscript: " + nb->GetRPath() + ", out dir " + nb->GetOutDir() + ", top " + nb->GetResultNum());
			ToggleVisibilityPluginWindow("NBLAST Search", true);
			check(GetCurSelType() == 3 && GetCurSelMesh(), "a mesh selected (the query SWC)");
		});
		wait(500);
		step([=]() {
			NBLASTPanel* p = panel();
			check(p && !p->DatabaseChecks().empty(), QString("%1 database check box(es)").arg(p ? p->DatabaseChecks().size() : 0));
			for (QCheckBox* c : p->DatabaseChecks()) if (!c->isChecked()) c->click();
			if (qEnvironmentVariableIntValue("VVD_QT_NBLAST_MEAN")) p->MeanRadio()->click();
			check(nb->GetScoringMethod() == (qEnvironmentVariableIntValue("VVD_QT_NBLAST_MEAN") ? "mean" : "forward"),
				"scoring method " + nb->GetScoringMethod());
			grab_dock("NBLAST Search", "nblast_before.png");
			p->OnRunNBLAST();   // wx OnSENDEVENTBUTTONClick (R runs synchronously)
			st->respath = nb->GetOutDir() + "/vvd_nblast_tmp_result.txt";
			log("R command: " + nb->LastCommand().join(' '));
			check(nb->LastExitCode() == 0, QString("Rscript exit code %1").arg(nb->LastExitCode()));
			check(QFileInfo::exists(st->respath), "result file " + st->respath);
			log("result file:\n" + ReadText(st->respath));
			st->rows = p->Results()->GetItemCount();
			check(st->rows > 0, QString("%1 hit(s) listed").arg(st->rows));
			if (st->rows > 0) st->first = p->Results()->GetText(0, 2);
		});
		wait(500);
		step([=]() {
			NBLASTPanel* p = panel();
			NBResultList* r = p->Results();
			QString rows;
			for (int i = 0; i < r->GetItemCount(); i++)
				rows += QString("\n  %1 | db %2 %3 | swc img %4 | mip img %5 | score %6").arg(r->GetText(i, 2), r->GetText(i, 1),
					r->GetText(i, 3)).arg(r->GetImageId(i, 4)).arg(r->GetImageId(i, 5)).arg(r->GetText(i, 6));
			log("list:" + rows);
			check(r->GetSelected() == 0, "first hit selected after loading (wx)");
			check(!p->SwcImage()->Image().isNull() && !p->MipImage()->Image().isNull(), "preview images of the selected hit");
			grab_dock("NBLAST Search", "nblast_results.png");
			p->SwcImage()->grab().save(dir + "/nblast_swcprev.png");
			p->MipImage()->grab().save(dir + "/nblast_mip.png");
			p->OverlayCheck()->click();   // overlay off
			p->SwcImage()->grab().save(dir + "/nblast_swcprev_nooverlay.png");
			p->OverlayCheck()->click();
			// delete / undo / redo (keys of the list)
			const int n = r->GetItemCount();
			r->Tree()->setFocus();
			r->Select(0);
			SendKey(r->Tree(), Qt::Key_Delete);
			check(r->GetItemCount() == n - 1 && (n < 2 || r->GetText(0, 2) != st->first), "Delete removes the selected hit");
			SendKey(r->Tree(), Qt::Key_Z, Qt::ControlModifier);
			check(r->GetItemCount() == n && r->GetText(0, 2) == st->first, "Ctrl+Z puts it back at its row");
			SendKey(r->Tree(), Qt::Key_Y, Qt::ControlModifier);
			check(r->GetItemCount() == n - 1, "Ctrl+Y deletes it again");
			SendKey(r->Tree(), Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
			check(r->GetItemCount() == n - 1, "Ctrl+Shift+Z (redo) with nothing to redo");
			SendKey(r->Tree(), Qt::Key_Z, Qt::ControlModifier);
			check(r->GetItemCount() == n, "undo again");
			// double click on the Skeleton cell loads the hit's SWC
			const int meshes = m_data_mgr->GetMeshNum();
			r->Select(0);
			r->ActivateCell(0, 4);
			check(m_data_mgr->GetMeshNum() == meshes + 1, QString("Skeleton cell: SWC loaded (%1 meshes)").arg(m_data_mgr->GetMeshNum()));
			const int vols = m_data_mgr->GetVolumeNum();
			r->ActivateCell(0, 5);
			check(m_data_mgr->GetVolumeNum() == vols + (QFileInfo::exists(QFileInfo(nb->LastCommand().value(3).split(',').value(0)).absolutePath() + "/volume/" + r->GetText(0, 2) + ".nrrd") ? 1 : 0),
				QString("Volume Data cell: volume loaded when it exists (%1 volumes)").arg(m_data_mgr->GetVolumeNum()));
			// Save with the export options (file dialog testing hooks:
			// VVD_QT_FILEDLG_FILE_NBLASTSAVE / _OPTS_NBLASTSAVE)
			p->OnSave();
			const QString saved = qEnvironmentVariable("VVD_QT_FILEDLG_FILE_NBLASTSAVE");
			check(saved.isEmpty() || QFileInfo::exists(saved), "saved results " + saved);
			if (!saved.isEmpty())
			{
				log("saved file:\n" + ReadText(saved));
				QStringList files = QDir(QFileInfo(saved).absolutePath()).entryList(QDir::Files, QDir::Name);
				log("export folder: " + files.join(", "));
				// import the saved file
				p->Results()->DeleteAll();
				p->OnImportResults();
				check(p->Results()->GetItemCount() == n, QString("imported %1 rows").arg(p->Results()->GetItemCount()));
			}
		});
		finish();
	}
	else if (kind == "b4b_nbvol")
	{
		// a volume query: NBLAST asks the Fiji Interface for "NBLAST
		// Skeletonize" (mask sent, voxel override off), Fiji's result comes
		// back as a copy with the skeleton as its mask, Fiji reports the
		// command finished (wxEVT_GUI_PLUGIN_INTEROP), the search runs on it
		auto* nb = dynamic_cast<NBLASTPluginQt*>(m_plugin_manager->GetGuiPlugin("NBLAST Search"));
		auto* fiji = dynamic_cast<FijiInterfacePluginQt*>(m_plugin_manager->GetGuiPlugin("Fiji Interface"));
		auto panel = [this]() -> NBLASTPanel* {
			QDockWidget* d = PluginDock("NBLAST Search");
			return d ? qobject_cast<NBLASTPanel*>(DockContent(d)) : nullptr;
		};
		step([=]() {
			ToggleVisibilityPluginWindow("NBLAST Search", true);
			QFile::remove(nb->GetOutDir() + "/vvd_nblast_tmp_result.txt");
		});
		wait(500);
		step([=]() {
			NBLASTPanel* p = panel();
			for (QCheckBox* c : p->DatabaseChecks()) if (!c->isChecked()) c->click();
			check(GetCurSelType() == 2 && GetCurSelVol(), "a volume selected: " + (GetCurSelVol() ? GetCurSelVol()->GetName().ToQt() : QString()));
			p->OnRunNBLAST();
			check(p->WaitingForFiji() && IsCreatedPluginWindow("Fiji Interface") && !IsShownPluginWindow("Fiji Interface"),
				"waiting for Fiji; Fiji pane created hidden (skeletonizeMask)");
		});
		wait_until([=]() { NBLASTPanel* p = panel(); return !p->WaitingForFiji() && p->Results()->GetItemCount() > 0; }, 300000,
			"skeleton from Fiji + NBLAST");
		step([=]() {
			NBLASTPanel* p = panel();
			log("Fiji received: " + fiji->ReceivedVolumes().join(",") + " / " + fiji->LastReceived());
			VolumeData* sk = fiji->ReceivedVolumes().isEmpty() ? nullptr : m_data_mgr->GetVolumeData(VVDString(fiji->ReceivedVolumes().last()));
			check(sk && sk->GetMask(false), "Fiji's skeleton as the mask of a copy: " + (sk ? sk->GetName().ToQt() : QString()));
			if (sk)
			{
				const QByteArray m = VolumeBytes(sk, true);
				long long n = 0; for (char c : m) n += (unsigned char)c != 0;
				log(QString("skeleton voxels: %1 of %2").arg(n).arg(m.size()));
				check(n > 0 && n < m.size() / 4, "a thin skeleton");
			}
			log("R command: " + nb->LastCommand().join(' '));
			check(nb->LastCommand().value(2).endsWith("vvdnbtmpv.nrrd") && nb->LastExitCode() == 0, "query = the mask as vvdnbtmpv.nrrd, Rscript ok");
			check(p->Results()->GetItemCount() > 0, QString("%1 hits").arg(p->Results()->GetItemCount()));
			log("first hits: " + p->Results()->GetText(0, 2) + " " + p->Results()->GetText(0, 6) + ", " + p->Results()->GetText(1, 2) + " " + p->Results()->GetText(1, 6));
			grab_dock("NBLAST Search", "nbvol_results.png");
		});
		finish();
	}
	else if (kind == "b4b_nbdlg")
	{
		// the NBLAST dialogs: database editor, settings, user / password
		auto* nb = dynamic_cast<NBLASTPluginQt*>(m_plugin_manager->GetGuiPlugin("NBLAST Search"));
		step([=]() {
			ToggleVisibilityPluginWindow("NBLAST Search", true);
			NBDBListDialog dlg(this, "Edit NBLAST Database");
			const size_t n0 = dlg.getList().size();
			log(QString("database list: %1 entries").arg(n0));
			dlg.NewDBPath()->setText("/tmp/some/db_extra.rds");
			dlg.findChild<QPushButton*>("NBAddDBButton")->click();
			check(dlg.getList().size() == n0 + 1 && dlg.getList().back().name == "db_extra", "Add Database: name from the file name");
			QTreeWidgetItem* it = dlg.List()->topLevelItem((int)n0);
			it->setText(0, "renamed");
			check(dlg.getList().back().name == "renamed", "name edited in place");
			dlg.SetUsrPwd(n0, "user1", "pw1");
			dlg.setState(n0, true);
			dlg.show();
			dlg.grab().save(dir + "/nblast_dbdlg.png");
			dlg.SaveList();
			const QString saved = qEnvironmentVariable("VVD_QT_NBLAST_DBLIST_SAVE");
			log("saved list:\n" + ReadText(saved));
			const QString last = ReadText(saved).split('\n').last();
			check(last == "renamed\t/tmp/some/db_extra.rds\ttrue\tuser1\tpw1", "list line: name, path, state, user, password (no final newline)");
			dlg.List()->setCurrentItem(it);
			SendKey(dlg.List(), Qt::Key_Delete);
			check(dlg.getList().size() == n0, "Delete key removes the selected database");
			NBSettingDialog sd(nb, this, "NBLAST Settings");
			sd.show();
			sd.grab().save(dir + "/nblast_settings.png");
			sd.ResultNum()->setText("7");
			sd.SaveSettings();
			check(nb->GetResultNum() == "7", "Setting dialog: Show Top -> result number");
			NBUsrPwdDialog up(this, "Set User/Password");
			up.SetUserNameText("u");
			up.SetPasswordText("p");
			up.show();
			up.grab().save(dir + "/nblast_usrpwd.png");
			check(up.GetUserNameText() == "u" && up.GetPasswordText() == "p", "user / password dialog");
		});
		finish();
	}
	else if (kind == "b4b_naimp")
	{
		// Import (label dialog, then the volume dialog: no <label>.h5j next to it)
		auto* na = dynamic_cast<NAPluginQt*>(m_plugin_manager->GetGuiPlugin("NAPlugin"));
		step([=]() { ToggleVisibilityPluginWindow("NAPlugin", true); });
		wait(500);
		step([=]() {
			NAPanel* p = qobject_cast<NAPanel*>(DockContent(PluginDock("NAPlugin")));
			p->OnImportResults();
			check(p->Results()->GetItemCount() == na->getSegCount() + 1 && na->getSegCount() == 7,
				QString("imported: %1 rows").arg(p->Results()->GetItemCount()));
			VolumeData* rv = m_data_mgr->GetVolumeData(VVDString(na->ReferenceName()));
			check(rv && !rv->GetDisp() && !p->Results()->IsItemChecked(0), "LoadResults: the reference hidden, unchecked");
			bool all = true;
			for (int i = 0; i < na->getSegCount(); i++) all &= na->GetSegmentVisibility(i) == 1;
			check(all, "LoadResults: every segment shown");
			grab_dock("NAPlugin", "b4b_naimp_list.png");
		});
		finish();
	}
	else if (kind == "b4b_na" || kind == "b4b_nart")
	{
		auto* na = dynamic_cast<NAPluginQt*>(m_plugin_manager->GetGuiPlugin("NAPlugin"));
		auto panel = [this]() -> NAPanel* {
			QDockWidget* d = PluginDock("NAPlugin");
			return d ? qobject_cast<NAPanel*>(DockContent(d)) : nullptr;
		};
		auto dump_state = [=](const QString& tag) {
			QString s;
			for (int i = 0; i < na->getSegCount(); i++)
				s += QString(" %1:%2(%3v)=%4").arg(i).arg(na->Segments()[i].id).arg(na->Segments()[i].size).arg(na->GetSegmentVisibility(i));
			log(tag + " segments:" + s);
		};
		wait(1500);
		wait_until([=]() { NAPanel* p = panel(); return p && p->Results()->GetItemCount() > 0; }, 10000, "NA list filled");
		step([=]() {
			check(na != nullptr && IsShownPluginWindow("NAPlugin"), "NA plugin pane shown (-p shows it)");
			NAPanel* p = panel();
			if (!p) return;
			NAResultList* r = p->Results();
			log(QString("signals: %1 %2 %3, reference %4, %5 segment(s), list %6 rows").arg(na->SignalName(0), na->SignalName(1),
				na->SignalName(2), na->ReferenceName()).arg(na->getSegCount()).arg(r->GetItemCount()));
			dump_state(kind);
			check(na->SignalCount() >= 1 && na->getSegCount() >= 2, "signals and segments");
			check(r->GetItemCount() == na->getSegCount() + (na->isRefExists() ? 1 : 0), "one row per segment (+ Reference)");
			for (int i = 1; i + 1 < na->getSegCount(); i++)
				if (na->Segments()[i].size < na->Segments()[i + 1].size) { check(false, "fragments sorted by size"); break; }
			VolumeData* s0 = m_data_mgr->GetVolumeData(VVDString(na->SignalName(0)));
			check(s0 && s0->GetNAMode() && s0->GetTexture()->nlabel() >= 0, "signal 0 in NA mode with the label");
			for (int c = 1; c < na->SignalCount(); c++)
			{
				VolumeData* sc = m_data_mgr->GetVolumeData(VVDString(na->SignalName(c)));
				check(sc && sc->GetSharedLabelName().ToQt() == na->SignalName(0) && sc->GetSharedMaskName().ToQt() == na->SignalName(0),
					"signal " + QString::number(c) + " shares label / mask of signal 0");
			}
			if (kind == "b4b_na")
			{
				VolumeData* rv = m_data_mgr->GetVolumeData(VVDString(na->ReferenceName()));
				// (-p: OnRun -> runNALoader; only Import's LoadResults hides the reference)
				check(rv && rv->GetSharedMaskName().ToQt() == na->SignalName(0), "reference shares the mask of signal 0");
				// (MoveLayertoView, then UpdateTree's OrganizeLayers puts it in a group of its own, as wx)
				check(rv && m_canvas->GetParentGroup(rv) != m_canvas->GetParentGroup(m_data_mgr->GetVolumeData(VVDString(na->SignalName(0)))),
					"reference out of the signal group");
				check(m_canvas->GetForceHideMask(), "view: masks hidden (SetForceHideMask)");
			}
			// MIPs: thumbnails to files (an independent check recomputes them)
			for (int i = 0; i < na->getSegCount(); i++)
				na->getSegMIP(i)->save(dir + QString("/%1_mip_%2.png").arg(kind).arg(i));
			if (na->isRefExists()) na->getRefMIP()->save(dir + "/" + kind + "_mip_ref.png");
			grab_dock("NAPlugin", kind + "_list.png");
		});
		if (kind == "b4b_na")
		{
			step([=]() {
				NAResultList* r = panel()->Results();
				const int off = na->isRefExists() ? 1 : 0;
				// uncheck fragment 1 -> hidden (segment mask 0)
				const int row = off + 1;
				r->CheckItem(row, false);
				check(na->GetSegmentVisibility(1) == 0, "uncheck fragment 1 -> segment mask 0");
				r->CheckItem(row, true);
				check(na->GetSegmentVisibility(1) == 2, "check fragment 1 -> active (mask 2, wx NA_SET_ACTIVE)");
				// double click toggles the check box
				QTreeWidgetItem* it = r->Tree()->topLevelItem(off + 2);
				Q_EMIT r->Tree()->itemDoubleClicked(it, 2);
				check(!r->IsItemChecked(off + 2) && na->GetSegmentVisibility(2) == 0, "double click: fragment 2 unchecked, hidden");
				Q_EMIT r->Tree()->itemDoubleClicked(it, 2);
				check(r->IsItemChecked(off + 2) && na->GetSegmentVisibility(2) == 2, "double click again: checked, active");
				// select / deselect (30 ms timer)
				r->Tree()->clearSelection();
				r->SetItemSelected(off + 1, false);
			});
			wait(300);
			step([=]() {
				dump_state("after deselect");
				check(na->GetSegmentVisibility(1) >= 1, "fragment 1 shown");
				NAResultList* r = panel()->Results();
				r->Tree()->setCurrentItem(r->Tree()->topLevelItem((na->isRefExists() ? 1 : 0) + 3));
			});
			wait(300);
			step([=]() {
				dump_state("after selecting fragment 3");
				check(na->getSegCount() <= 3 || na->GetSegmentVisibility(3) == 2, "selected fragment 3 -> active (2)");
				// the view's context menu "Show all" (MainWindow NA plumbing) ->
				// OnTreeUpdate -> list check states follow
				na->SetSegmentVisibility(1, 0);
				m_tree->UpdateTreeIcons();
			});
			wait(400);
			step([=]() {
				NAResultList* r = panel()->Results();
				check(!r->IsItemChecked((na->isRefExists() ? 1 : 0) + 1), "OnTreeUpdate: the list follows a change made elsewhere");
				OnShowAllFragments();
			});
			wait(400);
			step([=]() {
				NAResultList* r = panel()->Results();
				check(r->IsItemChecked((na->isRefExists() ? 1 : 0) + 1) && na->GetSegmentVisibility(1) == 1,
					"view menu Show all -> fragment 1 checked again");
				// thumbnails follow the list width
				const int w0 = na->getSegMIPThumbnail(0)->width();
				QDockWidget* d = PluginDock("NAPlugin");
				d->setFloating(true);
				d->resize(d->width() + 150, d->height());
			});
			wait(800);
			step([=]() {
				NAResultList* r = panel()->Results();
				log(QString("thumbnail width %1 (list %2)").arg(na->getSegMIPThumbnail(0)->width()).arg(r->ThumbnailWidth()));
				check(std::abs(na->getSegMIPThumbnail(0)->width() - r->ThumbnailWidth()) <= 1, "thumbnails resized to the list width");
				grab_dock("NAPlugin", "b4b_na_list_wide.png");
				PluginDock("NAPlugin")->setFloating(false);
				// delete (API; the key is disabled as wx) / undo / redo
				const int n = r->GetItemCount();
				r->Tree()->clearSelection();
				r->SetItemSelected(n - 1, true);
				r->DeleteSelection();
				check(r->GetItemCount() == n - 1, "DeleteSelection");
				SendKey(r->Tree(), Qt::Key_Z, Qt::ControlModifier);
				check(r->GetItemCount() == n, "Ctrl+Z: row back");
				SendKey(r->Tree(), Qt::Key_Y, Qt::ControlModifier);
				check(r->GetItemCount() == n - 1, "Ctrl+Y: deleted again");
				SendKey(r->Tree(), Qt::Key_Delete);
				check(r->GetItemCount() == n - 1, "Delete key does nothing (wx: commented out)");
				r->UpdateResults(true);
				// hide fragment 2, export the combined fragments
				na->SetSegmentVisibility(2, 0);
				const QString out = qEnvironmentVariable("VVD_QT_FILEDLG_FILE_NAEXPORT");
				panel()->OnExport();
				check(out.isEmpty() || QFileInfo::exists(out), "export combined fragments: " + out);
				dump_state("export state");
				// the project with the NA pane
				const QString prj = dir + "/p_na.vrp";
				QDir(prj + "_files").removeRecursively();
				SaveProject(prj);
				const QString set = prj_value(prj, "/ui_layout/Neuron Annotator Plugin_setting");
				check(QFileInfo::exists(set), "project NA setting file " + set);
				log("NA .set:\n" + ReadText(set));
			});
		}
		else
		{
			step([=]() {
				check(na->GetSegmentVisibility(2) == 0, "project: fragment 2 hidden again");
				check(na->GetSegmentVisibility(1) == 1, "project: fragment 1 shown");
			});
		}
		finish();
	}
	else
	{
		log("unknown test " + kind);
		finish();
	}
	seq->run(this, seq, 300);
}
