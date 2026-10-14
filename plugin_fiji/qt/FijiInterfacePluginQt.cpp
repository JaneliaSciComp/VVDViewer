#include "FijiInterfacePluginQt.h"
#include "WxIpcServer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>
#include <QProgressDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QApplication>
#include <cstring>
#include <cstdint>

#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include "TestRun.h"
#include "QtPluginManager.h"
#include "RenderCanvas.h"
#include "DataManager.h"
#include "Core/FileUtil.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/Texture.h"
#include "FLIVR/Color.h"

using namespace wxipc;

vvd::Plugin* CreateFijiInterfacePluginQt()
{
	return new FijiInterfacePluginQt();
}

namespace
{
	// wxMilliSleep on the GUI thread, with the sockets / timers still served
	void WaitMs(int ms)
	{
		QElapsedTimer t;
		t.start();
		while (t.elapsed() < ms)
			QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
	}
	// wxString((const char*)data): the bytes up to the first NUL
	QString CStr(const QByteArray& data)
	{
		const int n = data.indexOf('\0');
		return QString::fromUtf8(n >= 0 ? data.left(n) : data);
	}
	int32_t I32(const char* p) { int32_t v; memcpy(&v, p, 4); return v; }
	double F64(const char* p) { double v; memcpy(&v, p, 8); return v; }
	void Report(QWidget* parent, const QString& title, const QString& msg)
	{
		if (VVDQtScriptedRun())
			fprintf(stderr, "[fiji] %s: %s\n", title.toLocal8Bit().constData(), msg.toLocal8Bit().constData());
		else
			QMessageBox::information(parent, title, msg);
	}
	// wxCopyFile(src, dst) (overwrite). The destination is removed first so
	// that a symbolic link there is replaced, never written through.
	void CopyOver(const QString& src, const QString& dst)
	{
		if (QFileInfo(dst).isSymLink() || QFileInfo::exists(dst))
			QFile::remove(dst);
		QFile::copy(src, dst);
	}
}

// ---------------------------------------------------------------------------
// plugin (wx SampleGuiPlugin1)
// ---------------------------------------------------------------------------

FijiInterfacePluginQt::FijiInterfacePluginQt()
{
}

FijiInterfacePluginQt::~FijiInterfacePluginQt()
{
	delete m_server;
	m_server = nullptr;
}

QWidget* FijiInterfacePluginQt::CreatePanel(QWidget* parent)
{
	return new FijiInterfacePanel(this, parent);
}

void FijiInterfacePluginQt::OnInit()
{
	LoadConfigFile();
	// testing aid: the Fiji to use (VVD_QT_FIJI_PATH=<Fiji.app>)
	const QString test_path = qEnvironmentVariable("VVD_QT_FIJI_PATH");
	if (!test_path.isEmpty()) m_fiji_path = test_path;
	m_server = new WxIpcServer();
	m_server->SetAcceptedTopic("FijiVVDPlugin");
	connect(m_server, &WxIpcServer::connectionAccepted, this, &FijiInterfacePluginQt::OnConnection);
	// wx m_server->Create("8002")
	bool ok = false;
	const int port = qEnvironmentVariableIntValue("VVD_QT_FIJI_PORT", &ok);   // testing aid
	if (!m_server->Create(ok ? QString::number(port) : QString("8002")))
		fprintf(stderr, "[fiji] cannot listen on port %s (another VVDViewer running?)\n", ok ? QString::number(port).toLocal8Bit().constData() : "8002");

	// a scripted test run never starts Fiji by itself
	if (m_launch_fiji_startup && !VVDQtScriptedRun())
		StartFiji();
}

void FijiInterfacePluginQt::OnDestroy()
{
	CloseFiji();
}

bool FijiInterfacePluginQt::OnRun(const VVDString& options)
{
	// wx notifyAll(FI_RUN, options) -> the window (SendCommand)
	Q_EMIT runRequested(options.ToQt());
	return true;
}

// wx FijiServer::OnAcceptConnection + doAction(FI_CONNECT)
void FijiInterfacePluginQt::OnConnection(WxIpcConnection* conn)
{
	m_conn = conn;
	connect(conn, &WxIpcConnection::poked, this, &FijiInterfacePluginQt::OnPoke);
	// a closed connection is deleted (wx: HandleDisconnect; m_conn -> null)
	connect(conn, &WxIpcConnection::disconnected, conn, &QObject::deleteLater);
	// wx OnAdvise / OnStartAdvise / OnStopAdvise: message boxes
	connect(conn, &WxIpcConnection::advised, this, [](const QString& topic, const QString&, const QByteArray& data, int) {
		fprintf(stderr, "[fiji] OnAdvise %s: %s\n", topic.toLocal8Bit().constData(), CStr(data).toLocal8Bit().constData()); });
	connect(conn, &WxIpcConnection::startAdvise, this, [](const QString& topic, const QString& item) {
		fprintf(stderr, "[fiji] OnStartAdvise(\"%s\",\"%s\")\n", topic.toLocal8Bit().constData(), item.toLocal8Bit().constData()); });
	connect(conn, &WxIpcConnection::stopAdvise, this, [](const QString& topic, const QString& item) {
		fprintf(stderr, "[fiji] OnStopAdvise(\"%s\",\"%s\")\n", topic.toLocal8Bit().constData(), item.toLocal8Bit().constData()); });
	m_booting = false;
}

// wx FijiServerConnection::OnPoke
void FijiInterfacePluginQt::OnPoke(const QString& topic, const QString& item, const QByteArray& data, int format)
{
	(void)topic;
	WxIpcConnection* conn = qobject_cast<WxIpcConnection*>(sender());
	if (!conn) conn = m_conn;
	const int newbuffersize = 1024 * 1024 * 128;
	switch (format)
	{
	case wxIPC_TEXT:
		if (item == "version")
		{
			// FI_VERSION_CHECK
			m_fiji_plugin_ver = CStr(data);
			Q_EMIT versionChecked(m_fiji_plugin_ver);
			if (conn) { conn->SetRcvBufSize(newbuffersize); conn->SetSndBufSize(newbuffersize); }
		}
		if (item == "confirm")
		{
			if (conn) conn->Poke(item, data, wxIPC_TEXT);
			// FI_CONFIRM
			m_initialized = true;
			if (conn)
			{
				conn->SetRcvBufSize(newbuffersize);
				conn->SetSndBufSize(newbuffersize);
				conn->SetTimeout(30);
			}
		}
		if (item == "pid")
			m_pid = CStr(data);   // FI_PID
		if (item == "com_finish")
			OnCommandFinished(data);
		break;
	case wxIPC_PRIVATE:
		if (item == "settimeout" && data.size() == 4)
		{
			const int sec = I32(data.constData());
			if (sec > 0 && conn) conn->SetTimeout(sec);
		}
		if (item == "setrcvbufsize" && data.size() >= 4)
		{
			const int sz = I32(data.constData());
			if (sz > 0 && conn) conn->SetRcvBufSize(sz);   // (+ wxMilliSleep(300) in wx)
		}
		if (item == "setsndbufsize" && data.size() >= 4)
		{
			const int sz = I32(data.constData());
			if (sz > 0 && conn) conn->SetSndBufSize(sz);
		}
		if (item == "volume")
			OnVolumeData(data);
		break;
	}
}

// wx SampleGuiPlugin1::doAction(FI_VOLUMEDATA)
void FijiInterfacePluginQt::OnVolumeData(const QByteArray& block)
{
	if (!m_host) return;
	DataManager* dm = m_host->PluginDataManager();
	if (!dm) return;
	VolumeData* cur_vol = m_host->GetCurSelVol();

	const char* ptr = block.constData();
	size_t chk = (size_t)block.size();
	auto need = [&](size_t n) { return chk >= n; };
	if (!need(4)) return;
	const int name_len = I32(ptr); ptr += 4; chk -= 4;
	if (name_len < 0 || !need((size_t)name_len + 7 * 4 + 3 * 8)) return;
	const QString name = CStr(QByteArray(ptr, name_len));
	ptr += name_len; chk -= name_len;
	const int nx = I32(ptr); ptr += 4; chk -= 4;
	const int ny = I32(ptr); ptr += 4; chk -= 4;
	const int nz = I32(ptr); ptr += 4; chk -= 4;
	const int bd = I32(ptr); ptr += 4; chk -= 4;
	const int r = I32(ptr); ptr += 4; chk -= 4;
	const int g = I32(ptr); ptr += 4; chk -= 4;
	const int b = I32(ptr); ptr += 4; chk -= 4;
	const double spcx = F64(ptr); ptr += 8; chk -= 8;
	const double spcy = F64(ptr); ptr += 8; chk -= 8;
	const double spcz = F64(ptr); ptr += 8; chk -= 8;

	const size_t vsize = (size_t)nx * (size_t)ny * (size_t)nz * (size_t)(bd / 8);
	m_last_received = QString("%1 %2x%3x%4 %5-bit rgb %6,%7,%8 spc %9,%10,%11 %12")
		.arg(name).arg(nx).arg(ny).arg(nz).arg(bd).arg(r).arg(g).arg(b).arg(spcx).arg(spcy).arg(spcz)
		.arg(chk == vsize ? "ok" : "size mismatch");
	fprintf(stderr, "[fiji] volume received: %s\n", m_last_received.toLocal8Bit().constData());
	if (chk != vsize || (bd != 8 && bd != 16))
		return;

	bool set_as_mask = false;
	if (!m_sent_mask.isEmpty() && m_sent_mask == name && bd == 8)
	{
		VolumeData* src = dm->GetVolumeData(VVDString(name));
		if (src)
		{
			int resx, resy, resz;
			src->GetResolution(resx, resy, resz);
			if (resx == nx && resy == ny && resz == nz)
				set_as_mask = true;
		}
		m_sent_mask.clear();
	}

	if (set_as_mask)
	{
		VolumeData* vd = VolumeData::DeepCopy(*dm->GetVolumeData(VVDString(name)), false, dm);
		if (!vd) return;
		if (!vd->GetMask(false)) vd->AddEmptyMask();
		auto vlnrrd = vd->GetMask(false);
		if (vlnrrd && vlnrrd->getNrrd() && vlnrrd->getNrrd()->data)
			memcpy(vlnrrd->getNrrd()->data, ptr, vsize);
		m_host->PluginAddVolume(vd, nullptr);
		m_received << vd->GetName().ToQt();
	}
	else
	{
		VolumeData* vd = new VolumeData();
		vd->AddEmptyData(bd, nx, ny, nz, spcx, spcy, spcz);
		FLIVR::Color col((double)r / 255.0, (double)g / 255.0, (double)b / 255.0);
		vd->SetName(VVDString(name));
		vd->SetColor(col);
		vd->SetBaseSpacings(spcx, spcy, spcz);
		vd->SetSpcFromFile(true);
		dm->SetVolumeDefault(vd);
		auto vlnrrd = vd->GetVolume(false);
		if (vlnrrd && vlnrrd->getNrrd() && vlnrrd->getNrrd()->data)
			memcpy(vlnrrd->getNrrd()->data, ptr, vsize);
		if (bd == 16)
		{
			// wx: the largest value -> max value, scalar scale 65535 / max
			const unsigned short* p = (const unsigned short*)ptr;
			int maxval = 0;
			for (size_t i = 0; i < vsize / 2; i++)
				if (maxval < (int)p[i]) maxval = p[i];
			vd->SetMaxValue((double)maxval);
			double scalar_scale = 1.0;
			if (maxval > 0.0)
				scalar_scale = 65535.0 / maxval;
			vd->SetScalarScale(scalar_scale);
		}
		const int vid = dm->GetVolumeIndex(VVDString(name));
		bool replaced = false;
		VolumeData* old = vid >= 0 ? dm->GetVolumeData(vid) : nullptr;
		if (vid >= 0)
		{
			// wx vframe->GetView(0)->ReplaceVolumeData(name, vd)
			replaced = m_host->PluginReplaceVolume(name, vd, nullptr);
			if (replaced)
			{
				if (cur_vol == old) cur_vol = vd;
				m_host->PluginUpdateTree(name, 2);
			}
			else
				delete vd;   // not in view 0 (wx leaked it)
		}
		else
			m_host->PluginAddVolume(vd, nullptr);
		if (vid < 0 || replaced)
			m_received << vd->GetName().ToQt();
		// wx vframe->OnSelection(2, 0, 0, cur_vol)
		if (cur_vol && dm->GetVolumeIndex(cur_vol->GetName()) >= 0)
			m_host->PluginSelect(2, nullptr, nullptr, cur_vol);
	}
	m_host->PluginRefreshViews(false);
}

// wx doAction(FI_COMMAND_FINISHED)
void FijiInterfacePluginQt::OnCommandFinished(const QByteArray& data)
{
	if (m_host)
	{
		DataManager* dm = m_host->PluginDataManager();
		RenderCanvas* view0 = m_host->PluginView(0);
		if (dm && view0)
		{
			dm->SetOverrideVox(m_ovvox);
			if (DataGroup* group = view0->GetCurrentVolGroup())
			{
				group->SetVolumeSyncSpc(m_gpsyncspc);
				group->SetVolumeSyncProp(m_gpsync);
			}
		}
	}
	m_tmp_ovvox = TMP_OVOX_NONE;
	Q_EMIT commandFinished(CStr(data));
}

bool FijiInterfacePluginQt::SendCommand(const QString& command, bool send_mask)
{
	if (!m_conn || !m_conn->GetConnected())
		return false;
	SendCurrentVolume(send_mask);

	DataManager* dm = m_host ? m_host->PluginDataManager() : nullptr;
	RenderCanvas* view0 = m_host ? m_host->PluginView(0) : nullptr;
	if (dm && view0)
	{
		m_ovvox = dm->GetOverrideVox();
		if (DataGroup* group = view0->GetCurrentVolGroup())
		{
			m_gpsync = group->GetVolumeSyncProp();
			m_gpsyncspc = group->GetVolumeSyncSpc();
			if (m_tmp_ovvox == TMP_OVOX_TRUE)
			{
				dm->SetOverrideVox(true);
				group->SetVolumeSyncSpc(true);
			}
			else if (m_tmp_ovvox == TMP_OVOX_FALSE)
			{
				dm->SetOverrideVox(false);
				group->SetVolumeSyncSpc(false);
				group->SetVolumeSyncProp(false);
			}
		}
	}
	if (!m_conn) return false;
	// wx: Poke("com", command) right after the volume. vvd_listener reads a
	// data block with read(buf) calls of the block's full size, so a
	// message that is already in its socket buffer while it is still
	// reading the volume would be read into the volume (overflow, the
	// listener stops). Give it time to finish the volume first.
	m_conn->Flush();
	WaitMs(300);
	return m_conn && m_conn->PokeString("com", command);
}

// wx SampleGuiPlugin1::SendCurrentVolume
bool FijiInterfacePluginQt::SendCurrentVolume(bool send_mask)
{
	if (!m_host || !m_conn || !m_conn->GetConnected()) return false;
	VolumeData* vd = m_host->GetCurSelVol();
	if (!vd) return false;

	std::shared_ptr<VL_Nrrd> vol;
	if (send_mask)
	{
		vol = vd->GetMask(true);
		if (!vol) vol = vd->GetVolume(true);
	}
	else
		vol = vd->GetVolume(true);
	if (!vol || !vol->getNrrd() || !vol->getNrrd()->data) return false;

	// wx wrote name.Len() (characters) and ToStdString() bytes; the UTF-8
	// bytes and their count here (the same for ASCII names)
	const QString name = vd->GetName().ToQt();
	const QByteArray name8 = name.toUtf8();
	int resx, resy, resz;
	vd->GetResolution(resx, resy, resz);
	double spcx, spcy, spcz;
	vd->GetSpacings(spcx, spcy, spcz);
	int bd;
	if (vol->getNrrd()->type == nrrdTypeChar || vol->getNrrd()->type == nrrdTypeUChar)
		bd = 8;
	else
		bd = 16;
	const size_t imagesize = (size_t)resx * (size_t)resy * (size_t)resz * (size_t)(bd / 8);
	const FLIVR::Color col = vd->GetColor();

	QByteArray buf;
	buf.reserve((qsizetype)(4 + name8.size() + 7 * 4 + 3 * 8 + imagesize));
	auto put32 = [&buf](int32_t v) { buf.append((const char*)&v, 4); };
	auto putd = [&buf](double v) { buf.append((const char*)&v, 8); };
	put32((int32_t)name8.size());
	buf.append(name8);
	put32(resx);
	put32(resy);
	put32(resz);
	put32(bd);
	put32((int32_t)(col.r() * 255));
	put32((int32_t)(col.g() * 255));
	put32((int32_t)(col.b() * 255));
	putd(spcx);
	putd(spcy);
	putd(spcz);
	buf.append((const char*)vol->getNrrd()->data, (qsizetype)imagesize);

	const int32_t tmp32 = (int32_t)buf.size();
	if (tmp32 > 0)
	{
		m_conn->SetSndBufSize(tmp32);
		m_conn->Poke("setrcvbufsize", QByteArray((const char*)&tmp32, 4), wxIPC_PRIVATE);
		m_conn->Flush();
		WaitMs(500);   // wx wxMilliSleep(500)
		if (!m_conn) return false;
		m_conn->Poke("volume", buf, wxIPC_PRIVATE);
		if (send_mask) m_sent_mask = name;
	}
	return true;
}

bool FijiInterfacePluginQt::StartFiji()
{
	if (m_initialized || m_booting)
		return true;

	QString fjpath = m_fiji_path;
#ifdef __APPLE__
	fjpath += "/Contents/MacOS/ImageJ-macosx";
#endif
	if (m_fiji_path.isEmpty() || !QFileInfo(fjpath).isFile())
	{
		Report(m_host ? m_host->PluginParentWidget() : nullptr, "Fiji Interface", "Error: Could not found Fiji");
		if (m_host)
		{
			if (!m_host->IsCreatedPluginWindow(GetName().ToQt()))
				m_host->CreatePluginWindow(GetName().ToQt());
			m_host->ToggleVisibilityPluginWindow(GetName().ToQt(), true);
		}
		return false;
	}

	const QString expath = QCoreApplication::applicationDirPath();
#ifdef __APPLE__
	const QString rootdir = m_fiji_path;
	const QString rdir = expath + "/../Resources/";
#else
	const QString rootdir = QFileInfo(m_fiji_path).absolutePath();
	const QString rdir = expath + "/";
#endif
	const QString macrodir = rootdir + "/macros/";
	const QString plugindir = rootdir + "/plugins/";
	for (const char* f : { "vvd_listener.txt", "vvd_run.txt", "vvd_quit.txt" })
		if (QFileInfo(macrodir).isDir() && QFileInfo::exists(rdir + f))
			CopyOver(rdir + f, macrodir + f);
	for (const char* f : { "vvd_listener.jar", "NBLAST_Skeletonize.jar", "Apply_Bigwarp_Filter.jar" })
		if (QFileInfo(plugindir).isDir() && QFileInfo::exists(rdir + f))
			CopyOver(rdir + f, plugindir + f);

	m_initialized = false;
#ifdef __APPLE__
	// wx: defaults write <Fiji>/Contents/Info LSUIElement 1 (no Dock icon)
	QProcess::execute("defaults", { "write", m_fiji_path + "/Contents/Info", "LSUIElement", "1" });
#endif
	m_booting = true;
	qint64 pid = 0;
	if (QProcess::startDetached(fjpath, { "-macro", "vvd_listener.txt" }, QString(), &pid))
	{
		m_fiji_pid = pid;
		m_pid = QString::number(pid);
	}
	else
	{
		m_booting = false;
		return false;
	}
	return true;
}

void FijiInterfacePluginQt::CloseFiji()
{
	if (m_fiji_path.isEmpty())
		return;
#ifdef _WIN32
	if (!m_pid.isEmpty())
		QProcess::execute("taskkill", { "/pid", m_pid, "/f" });
#else
	if (!m_pid.isEmpty())
	{
#ifdef __APPLE__
		QProcess::execute("defaults", { "write", m_fiji_path + "/Contents/Info", "LSUIElement", "0" });
#endif
		bool ok = false;
		const long pid = m_pid.toLong(&ok);
		if (ok && pid > 0) ::kill((pid_t)pid, SIGTERM);   // wx: wxExecute("kill " + pid)
	}
#endif
	const QString expath = QCoreApplication::applicationDirPath();
#ifdef __APPLE__
	const QString rootdir = m_fiji_path;
#else
	const QString rootdir = QFileInfo(m_fiji_path).absolutePath();
#endif
	(void)expath;
	const QString macrodir = rootdir + "/macros/";
	for (const char* f : { "vvd_listener.txt", "vvd_run.txt", "vvd_quit.txt" })
		if (QFileInfo::exists(macrodir + f))
			QFile::remove(macrodir + f);
	m_initialized = false;
	m_booting = false;
}

bool FijiInterfacePluginQt::isReady()
{
	if (!m_conn || !m_conn->GetConnected())
		return false;
	return m_initialized;
}

// fiji_interface_settings.dft: <bundle>/Contents/Resources on macOS, else
// next to the executable or in the user config dir (wx Load/SaveConfigFile)
QString FijiInterfacePluginQt::ConfigFilePath(bool for_save)
{
	const QString name = "fiji_interface_settings.dft";
	const QString expath = QCoreApplication::applicationDirPath();
#ifdef __APPLE__
	(void)for_save;
	return expath + "/../Resources/" + name;
#else
	QString dft = expath + "/" + name;
	const QString dft2 = QString::fromStdWString(vvd::GetUserConfigDir()) + "/" + name;
	if (for_save) { if (!QFileInfo::exists(dft) && QFileInfo::exists(dft2)) dft = dft2; }
	else if (!QFileInfo::exists(dft)) dft = dft2;
	return dft;
#endif
}

void FijiInterfacePluginQt::LoadConfigFile()
{
	const QString dft = ConfigFilePath(false);
	if (!QFileInfo::exists(dft)) return;
	vvd::IniFile fconfig;
	if (!fconfig.Load(VVDString(dft))) return;
	VVDString str;
	bool bval = false;
	if (fconfig.Read(L"fiji_path", str)) m_fiji_path = str.ToQt();
	if (fconfig.Read(L"launch_fiji", bval)) m_launch_fiji_startup = bval;
	if (fconfig.Read(L"send_mask", bval)) m_sendmask = bval;
}

void FijiInterfacePluginQt::SaveConfigFile()
{
	// scripted test runs leave the default files alone (TestRun.h)
	if (VVDQtScriptedRun()) return;
	vvd::IniFile fconfig;
	fconfig.Write(L"fiji_path", VVDString(m_fiji_path));
	fconfig.Write(L"launch_fiji", m_launch_fiji_startup);
	fconfig.Write(L"send_mask", m_sendmask);
	fconfig.Save(VVDString(ConfigFilePath(true)));
}

// ---------------------------------------------------------------------------
// window (wx SampleGuiPluginWindow1)
// ---------------------------------------------------------------------------

FijiInterfacePanel::FijiInterfacePanel(FijiInterfacePluginQt* plugin, QWidget* parent)
	: QWidget(parent), m_plugin(plugin)
{
	setObjectName("FijiInterfacePanel");
	QString fpath;
	bool sendmask = false;
	bool launch_fiji = false;
	if (m_plugin)
	{
		fpath = m_plugin->GetFijiPath();
		sendmask = m_plugin->GetSendMask();
		launch_fiji = m_plugin->GetLaunchFijiAtStartup();
	}
	auto* sizer = new QVBoxLayout(this);
	sizer->setContentsMargins(5, 5, 5, 5);
	sizer->setSpacing(5);
	sizer->addWidget(new QLabel(tr("Info:"), this));

	QString str = "Connectiong to Fiji...";   // (sic, wx)
	if (m_plugin && m_plugin->isReady())
		str = QString("VVD plugin ver. %1   Fiji plugin ver. %2").arg(FI_PLUGIN_VERSION).arg(m_plugin->GetFijiPluginVer());
	m_VerTextCtrl = new QLineEdit(str, this);
	m_VerTextCtrl->setObjectName("FijiVersionText");
	m_VerTextCtrl->setReadOnly(true);
	m_VerTextCtrl->setMinimumWidth(500);
	sizer->addWidget(m_VerTextCtrl);

	// wxFilePickerCtrl: path + Browse
	auto* row = new QHBoxLayout();
	m_FijiPickCtrl = new QLineEdit(fpath, this);
	m_FijiPickCtrl->setObjectName("FijiPathText");
	auto* browse = new QPushButton(tr("Browse..."), this);
	browse->setObjectName("FijiPathBrowse");
	row->addWidget(m_FijiPickCtrl, 1);
	row->addWidget(browse);
	sizer->addLayout(row);
	connect(browse, &QPushButton::clicked, this, [this]() {
#ifdef __APPLE__
		const QString filter = tr("Applications (*.app);;All files (*)");
#else
		const QString filter = tr("All files (*)");
#endif
		const QString f = QFileDialog::getOpenFileName(this, tr("path"), m_FijiPickCtrl->text(), filter);
		if (!f.isEmpty()) m_FijiPickCtrl->setText(f);
	});

	m_LaunchChk = new QCheckBox(tr("Launch Fiji at startup "), this);
	m_LaunchChk->setObjectName("FijiLaunchCheck");
	m_LaunchChk->setChecked(launch_fiji);
	sizer->addSpacing(5);
	sizer->addWidget(m_LaunchChk);
	sizer->addSpacing(15);

	m_CommandTextCtrl = new QLineEdit(tr("Open..."), this);
	m_CommandTextCtrl->setObjectName("FijiCommandText");
	sizer->addWidget(m_CommandTextCtrl);

	m_SendMaskChk = new QCheckBox(tr("Process mask "), this);
	m_SendMaskChk->setObjectName("FijiSendMaskCheck");
	m_SendMaskChk->setChecked(sendmask);
	sizer->addSpacing(5);
	sizer->addWidget(m_SendMaskChk);

	m_CommandButton = new QPushButton(tr("Send event"), this);
	m_CommandButton->setObjectName("FijiSendButton");
	sizer->addWidget(m_CommandButton, 0, Qt::AlignHCenter);
	sizer->addStretch(1);
	connect(m_CommandButton, &QPushButton::clicked, this, &FijiInterfacePanel::OnSendEventButton);

	m_pctimer = new QTimer(this);
	connect(m_pctimer, &QTimer::timeout, this, &FijiInterfacePanel::OnPendingCommandTimer);
	m_wtimer = new QTimer(this);
	connect(m_wtimer, &QTimer::timeout, this, &FijiInterfacePanel::OnWaitTimer);

	if (m_plugin)
	{
		connect(m_plugin, &FijiInterfacePluginQt::versionChecked, this, [this](const QString& ver) {
			if (m_VerTextCtrl)
				m_VerTextCtrl->setText(QString("VVD plugin ver. %1   Fiji plugin ver. %2").arg(FI_PLUGIN_VERSION).arg(ver));
		});
		connect(m_plugin, &FijiInterfacePluginQt::commandFinished, this, &FijiInterfacePanel::OnCommandFinished);
		connect(m_plugin, &FijiInterfacePluginQt::runRequested, this, &FijiInterfacePanel::OnRun);
	}
}

FijiInterfacePanel::~FijiInterfacePanel()
{
	m_pctimer->stop();
	m_wtimer->stop();
	delete m_prg_diag;
	m_prg_diag = nullptr;
	if (m_plugin)
	{
		m_plugin->SetFijiPath(m_FijiPickCtrl->text());
		m_plugin->SetLaunchFijiAtStartup(m_LaunchChk->isChecked());
		m_plugin->SetSendMask(m_SendMaskChk->isChecked());
		m_plugin->SaveConfigFile();
	}
}

void FijiInterfacePanel::EnableControls(bool enable)
{
	for (QWidget* w : { (QWidget*)m_FijiPickCtrl, (QWidget*)m_CommandTextCtrl, (QWidget*)m_CommandButton,
		(QWidget*)m_SendMaskChk, (QWidget*)m_LaunchChk })
		if (w) w->setEnabled(enable);
}

void FijiInterfacePanel::ActivateProcess(const QString& pid)
{
	// wx: osascript 'tell application "System Events" to set frontmost of
	// the first process whose unix id is <pid> to true' (Windows:
	// ActivatePID.vbs). Not in scripted test runs (it may ask for the
	// automation permission).
	if (pid.isEmpty() || VVDQtScriptedRun()) return;
#if defined(__APPLE__)
	QProcess::startDetached("osascript", { "-e",
		QString("tell application \"System Events\" to set frontmost of the first process whose unix id is %1 to true").arg(pid) });
#elif defined(_WIN32)
	const QString act = QCoreApplication::applicationDirPath() + "/ActivatePID.vbs";
	if (QFileInfo::exists(act))
		QProcess::startDetached("cscript", { "//nologo", act, pid });
#endif
}

void FijiInterfacePanel::OnRun(const QString& options)
{
	// wx doAction(FI_RUN): "command[;send_mask[;override_vox]]"
	const QStringList args = options.split(';', Qt::SkipEmptyParts);
	if (args.size() == 1)
		SendCommand(args[0], m_SendMaskChk->isChecked());
	else if (args.size() >= 2)
	{
		if (m_plugin && args.size() >= 3)
		{
			if (args[2] == "true") m_plugin->SetTempOverrideVox(TMP_OVOX_TRUE);
			if (args[2] == "false") m_plugin->SetTempOverrideVox(TMP_OVOX_FALSE);
		}
		SendCommand(args[0], args[1] == "true");
	}
}

void FijiInterfacePanel::SendCommand(const QString& com_in, bool send_mask)
{
	const QString com = com_in;   // (may be pendingcommand, cleared below)
	const QString fpath = m_FijiPickCtrl->text();
	if (!m_plugin) return;
	m_plugin->SetFijiPath(fpath);
	QWidget* frame = m_plugin->GetHost() ? m_plugin->GetHost()->PluginParentWidget() : nullptr;
	if (!m_plugin->isReady())
	{
		if (m_plugin->StartFiji())
		{
			pendingcommand = com;
			pendingcom_msk = send_mask;
			m_pctimer->start(50);
			m_pcwatch.start();
			delete m_prg_diag;
			m_prg_diag = new QProgressDialog(tr("Please wait."), tr("Cancel"), 0, 0, frame);
			m_prg_diag->setObjectName("FijiConnectProgress");
			m_prg_diag->setWindowTitle(tr("Connecting to Fiji..."));
			m_prg_diag->setWindowModality(Qt::ApplicationModal);
			m_prg_diag->setMinimumDuration(0);
			m_prg_diag->setAutoClose(false);
			m_prg_diag->setAutoReset(false);
			m_prg_diag->show();
		}
	}
	else
	{
		ActivateProcess(m_plugin->GetPID());
		m_waitingforfiji = true;
		EnableControls(false);
		delete m_prg_diag;
		m_prg_diag = new QProgressDialog(tr("Please wait."), tr("Cancel"), 0, 0, frame);
		m_prg_diag->setObjectName("FijiWaitProgress");
		m_prg_diag->setWindowTitle(tr("Waiting for Fiji..."));
		m_prg_diag->setWindowModality(Qt::ApplicationModal);
		m_prg_diag->setMinimumDuration(0);
		m_prg_diag->setAutoClose(false);
		m_prg_diag->setAutoReset(false);
		m_prg_diag->show();
		m_wtimer->start(50);
		pendingcommand.clear();
		pendingcom_msk = m_SendMaskChk->isChecked();
		// (wx sends before showing the dialog; the volume transfer runs the
		// event loop here, so the dialog is up first)
		m_plugin->SendCommand(com, send_mask);
	}
}

void FijiInterfacePanel::OnSendEventButton()
{
	SendCommand(m_CommandTextCtrl->text(), m_SendMaskChk->isChecked());
	// wx: wxEVT_GUI_PLUGIN_INTEROP with the info text
	if (m_plugin && m_plugin->GetHost())
		m_plugin->GetHost()->PostPluginInterop(m_VerTextCtrl->text());
}

void FijiInterfacePanel::OnPendingCommandTimer()
{
	if (m_prg_diag && m_prg_diag->wasCanceled())
	{
		m_pctimer->stop();
		delete m_prg_diag;
		m_prg_diag = nullptr;
		if (m_plugin) m_plugin->CloseFiji();
		return;
	}
	const qint64 t = m_pcwatch.elapsed();
	if (!m_plugin || m_plugin->isReady() || t >= 35000)
	{
		m_pctimer->stop();
		delete m_prg_diag;
		m_prg_diag = nullptr;
		// wx closed Fiji and reported a time out whenever the connection took
		// 15 s or more -- even when Fiji had connected by then (a slow first
		// start); here only when it has not connected within the 35 s
		if (m_plugin && !m_plugin->isReady())
		{
			m_plugin->CloseFiji();
			Report(m_plugin->GetHost() ? m_plugin->GetHost()->PluginParentWidget() : nullptr, "Fiji Interface", "Time out");
		}
		if (m_plugin && m_plugin->isReady())
			SendCommand(pendingcommand, pendingcom_msk);
	}
}

void FijiInterfacePanel::OnWaitTimer()
{
	if (m_prg_diag && m_prg_diag->wasCanceled())
	{
		m_wtimer->stop();
		delete m_prg_diag;
		m_prg_diag = nullptr;
		m_waitingforfiji = false;
		if (m_plugin) m_plugin->CloseFiji();
		EnableControls(true);
	}
}

void FijiInterfacePanel::OnCommandFinished(const QString& command)
{
	m_VerTextCtrl->setText("finished");
	if (m_plugin && m_plugin->GetHost())
		m_plugin->GetHost()->PostPluginInterop("Fiji Interface,finished," + command);
	if (m_prg_diag && m_waitingforfiji)
	{
		delete m_prg_diag;
		m_prg_diag = nullptr;
		EnableControls(true);
		m_waitingforfiji = false;
		m_wtimer->stop();
		// wx: this VVDViewer process to the front
		if (QWidget* w = window())
		{
			w->raise();
			w->activateWindow();
		}
		ActivateProcess(QString::number(QCoreApplication::applicationPid()));
	}
}
