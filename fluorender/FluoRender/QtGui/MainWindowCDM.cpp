// "Save Color Depth MIP..." (Workspace tree, volume context menu; Qt only):
// the options dialog of the Fiji macro Color_Depth_MIP_batch_0308_2021.ijm, a
// save dialog, then the engine port (ColorDepthMIP.cpp) on a worker thread
// with a progress dialog; the result is a PackBits RGB TIFF.
//
// Testing aid VVD_QT_TEST=cdm (RunCdmTest): the first volume's context menu
// entry is triggered without dialogs; see docs/QT_PORT.md, Testing aids.

#include "MainWindow.h"
#include "TestRun.h"
#include "TreePanel.h"
#include "RenderCanvas.h"
#include "SettingsDialog.h"
#include "ColorDepthMIPDialog.h"
#include "ColorDepthMIP.h"
#include "DataManager.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

namespace
{
// VVD_QT_CDM_MODE (0..4), _SCALE, _REVERSE, _GAMMA, _SKELETON, _DSLT
// (Line / Normal), _START, _END, _MEAN, _THREADS override the options
cdm::Options CdmOptionsFromEnv(cdm::Options o)
{
	bool ok = false;
	int v = qEnvironmentVariableIntValue("VVD_QT_CDM_MODE", &ok);
	if (ok && v >= 0 && v <= 4) o.mode = v;
	v = qEnvironmentVariableIntValue("VVD_QT_CDM_SCALE", &ok); if (ok) o.colorScale = v != 0;
	v = qEnvironmentVariableIntValue("VVD_QT_CDM_REVERSE", &ok); if (ok) o.reversed = v != 0;
	v = qEnvironmentVariableIntValue("VVD_QT_CDM_GAMMA", &ok); if (ok) o.gamma14 = v != 0;
	v = qEnvironmentVariableIntValue("VVD_QT_CDM_SKELETON", &ok); if (ok) o.skeletonBolded = v != 0;
	const QString dslt = qEnvironmentVariable("VVD_QT_CDM_DSLT");
	if (!dslt.isEmpty()) o.dsltLine = dslt.compare("normal", Qt::CaseInsensitive) != 0;
	v = qEnvironmentVariableIntValue("VVD_QT_CDM_START", &ok); if (ok) o.startMIP = v;
	v = qEnvironmentVariableIntValue("VVD_QT_CDM_END", &ok); if (ok) o.endMIP = v;
	v = qEnvironmentVariableIntValue("VVD_QT_CDM_MEAN", &ok); if (ok && v > 0 && v < 256) o.desiredMean = v;
	v = qEnvironmentVariableIntValue("VVD_QT_CDM_THREADS", &ok); if (ok) o.threads = v;
	return o;
}
}

void MainWindow::SaveColorDepthMIP(VolumeData* vd)
{
	if (!vd) return;
	cdm::Volume vol;
	std::string err;
	const QString name = vd->GetName().ToQt();
	if (!cdm::GetVolume(vd, vol, err))
	{
		const QString msg = tr("Color Depth MIP of %1: %2").arg(name, QString::fromStdString(err));
		if (VVDQtScriptedRun()) fprintf(stderr, "[cdm] FAIL %s\n", msg.toLocal8Bit().constData());
		else QMessageBox::warning(this, tr("Save Color Depth MIP"), msg);
		statusBar()->showMessage(msg, 15000);
		return;
	}

	// scripted runs (VVD_QT_TEST=cdm): the options from VVD_QT_CDM_*, the
	// file VVD_QT_CDM_OUT, no dialogs
	const QString scripted_out = VVDQtScriptedRun() ? qEnvironmentVariable("VVD_QT_CDM_OUT") : QString();
	cdm::Options opt = m_settings->GetColorDepthMIPOptions();
	const QString info = tr("%1: %2 x %3 x %4, %5-bit").arg(name).arg(vol.nx).arg(vol.ny).arg(vol.nz).arg(vol.bits);
	QString path;
	if (!scripted_out.isEmpty())
	{
		opt = CdmOptionsFromEnv(opt);
		path = scripted_out;
		const QString grab = qEnvironmentVariable("VVD_QT_CDM_GRAB");
		if (!grab.isEmpty())
		{
			ColorDepthMIPDialog dlg(this);
			dlg.SetOptions(opt);
			dlg.SetVolumeInfo(info);
			dlg.adjustSize();
			dlg.grab().save(grab + "/cdm_dialog.png");
		}
	}
	else
	{
		ColorDepthMIPDialog dlg(this);
		dlg.SetOptions(opt);
		dlg.SetVolumeInfo(info);
		if (dlg.exec() != QDialog::Accepted) return;
		opt = dlg.GetOptions();
		m_settings->SetColorDepthMIPOptions(opt);   // written to fluorender.set on exit
		// default folder: the volume's file; default name: the macro's
		QString dir;
		const QFileInfo src(vd->GetPath().ToQt());
		if (!vd->GetPath().empty() && src.dir().exists()) dir = src.absolutePath();
		else dir = m_last_dir;
		const QString def = QString::fromStdWString(cdm::DefaultFileName(vd->GetName()));
		path = QFileDialog::getSaveFileName(this, tr("Save Color Depth MIP"), QDir(dir).filePath(def),
			tr("TIFF file (*.tif *.tiff)"));
		if (path.isEmpty()) return;
		if (!path.endsWith(".tif", Qt::CaseInsensitive) && !path.endsWith(".tiff", Qt::CaseInsensitive))
			path += ".tif";
	}

	// the computation on a worker thread; the modal progress dialog keeps the
	// volume from being changed or closed meanwhile
	QProgressDialog prog(tr("Creating the Color Depth MIP of %1...").arg(name), tr("Cancel"), 0, 1000, this);
	prog.setWindowTitle(tr("Save Color Depth MIP"));
	prog.setWindowModality(Qt::ApplicationModal);
	prog.setMinimumDuration(0);
	prog.setAutoClose(false);
	prog.setAutoReset(false);
	prog.setValue(0);
	std::atomic<bool> cancel(false), done(false);
	std::atomic<int> permille(0);
	std::mutex mtx;
	std::string stage;
	cdm::Result res;
	bool wrote = false;
	std::string werr;
	const std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
	const std::wstring data_dir = cdm::DataDir();
	auto t0 = std::chrono::steady_clock::now();
	std::thread worker([&]() {
		cdm::Compute(vol, opt, data_dir, res, [&](double f, const std::string& s) {
			permille = (int)(f * 1000);
			{ std::lock_guard<std::mutex> lk(mtx); stage = s; }
			return !cancel.load();
		});
		if (res.ok) wrote = cdm::WriteTiff(wpath, res, &werr);
		done = true;
	});
	QEventLoop loop;
	QTimer timer;
	timer.setInterval(50);
	connect(&timer, &QTimer::timeout, &loop, [&]() {
		prog.setValue(std::min(999, permille.load()));
		{
			std::lock_guard<std::mutex> lk(mtx);
			if (!stage.empty()) prog.setLabelText(tr("Creating the Color Depth MIP of %1...\n%2").arg(name, QString::fromStdString(stage)));
		}
		if (prog.wasCanceled()) cancel = true;
		if (done) loop.quit();
	});
	timer.start();
	loop.exec();
	worker.join();
	prog.close();
	const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

	QString msg;
	if (res.canceled)
		msg = tr("Color Depth MIP canceled");
	else if (!res.ok || !wrote)
	{
		msg = tr("Color Depth MIP of %1 failed: %2").arg(name, QString::fromStdString(res.ok ? werr : res.error));
		if (!VVDQtScriptedRun()) QMessageBox::warning(this, tr("Save Color Depth MIP"), msg);
	}
	else
	{
		const bool seg = opt.mode == cdm::Options::SegmentationAuto || opt.mode == cdm::Options::SegmentationNoLowerCut;
		if (seg)
			msg = tr("Color Depth MIP saved: %1 (Max %2, signal size %3%, threshold size %4%; %5 s)")
				.arg(QDir::toNativeSeparators(path)).arg(res.applyV).arg(res.sigsize, 0, 'f', 2)
				.arg(res.sigsizethre, 0, 'f', 2).arg(secs, 0, 'f', 1);
		else
			msg = tr("Color Depth MIP saved: %1 (%2; %3 s)").arg(QDir::toNativeSeparators(path))
				.arg(QString::fromUtf8(cdm::ModeLabel(opt.mode))).arg(secs, 0, 'f', 1);
		m_last_dir = QFileInfo(path).absolutePath();
	}
	statusBar()->showMessage(msg, 30000);
	if (VVDQtScriptedRun())
	{
		fprintf(stderr, "[cdm] %s %s\n", res.ok && wrote ? "PASS" : "FAIL", msg.toLocal8Bit().constData());
		fprintf(stderr, "[cdm] applyV=%g sigsize=%g sigsizethre=%g Inimax=%g Inimin=%g DefMaxValue=%g bitd=%d time=%.2f s\n",
			res.applyV, res.sigsize, res.sigsizethre, res.Inimax, res.Inimin, res.DefMaxValue, res.bitd, secs);
		if (qEnvironmentVariableIsSet("VVD_CDM_DEBUG"))
			fprintf(stderr, "%s", res.log.c_str());
	}
}

// VVD_QT_TEST=cdm: waits for the first volume, opens its context menu in the
// Workspace tree, triggers "Save Color Depth MIP..." (MainWindow::
// SaveColorDepthMIP without dialogs, VVD_QT_CDM_OUT), quits.
// VVD_QT_CDM_GRAB=<dir>: cdm_menu.png (the volume's menu), cdm_dialog.png.
void MainWindow::RunCdmTest()
{
	auto t0 = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());
	auto* timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this, timer, t0]() {
		VolumeData* vd = nullptr;
		for (VolumeData* v : m_canvas->Volumes())
			if (v && v->GetVolume(false)) { vd = v; break; }
		const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - *t0).count();
		if (!vd)
		{
			if (waited > 120)
			{
				timer->stop();
				fprintf(stderr, "[cdm] FAIL no volume loaded\n");
				QCoreApplication::quit();
			}
			return;
		}
		timer->stop();
		const QString grab = qEnvironmentVariable("VVD_QT_CDM_GRAB");
		QTreeWidgetItem* it = m_tree->FindItem(vd->GetName().ToQt(), TreePanel::TypeVolume);
		if (!it)
		{
			fprintf(stderr, "[cdm] FAIL no tree item for %ls\n", vd->GetName().c_str());
			QCoreApplication::quit();
			return;
		}
		m_tree->Tree()->setCurrentItem(it);
		QMenu* menu = m_tree->BuildContextMenu(it);
		QAction* act = nullptr;
		QStringList entries;
		if (menu)
		{
			for (QAction* a : menu->actions())
			{
				entries << (a->isSeparator() ? QString("----") : a->text());
				if (a->text() == tr("Save Color Depth MIP...")) act = a;
			}
			if (!grab.isEmpty())
			{
				menu->ensurePolished();
				menu->adjustSize();
				menu->grab().save(grab + "/cdm_menu.png");
			}
		}
		fprintf(stderr, "[cdm] volume menu: %s\n", entries.join(" | ").toLocal8Bit().constData());
		if (!act)
		{
			fprintf(stderr, "[cdm] FAIL no \"Save Color Depth MIP...\" entry\n");
			delete menu;
			QCoreApplication::quit();
			return;
		}
		act->trigger();   // TreePanel::OnSaveColorDepthMIP -> SaveColorDepthMIP
		delete menu;
		QTimer::singleShot(200, qApp, []() { QCoreApplication::quit(); });
	});
	timer->start(200);
}
