#ifndef VVD_QT_FIJIINTERFACEPLUGINQT_H
#define VVD_QT_FIJIINTERFACEPLUGINQT_H

// "Fiji Interface" plugin of the Qt front end: port of plugin_fiji/vvd
// (FijiInterfacePlugin.cpp = SampleGuiPlugin1 + FijiServer /
// FijiServerConnection, FijiInterfacePluginWindow.cpp = SampleGuiPluginWindow1).
//
// VVDViewer is the IPC server (port 8002, topic "FijiVVDPlugin", the wx
// TCP IPC protocol of WxIpcServer.h); Fiji runs the vvd_listener plugin
// (plugin_fiji/fiji/plugins/vvd_listener.java, started by the macro
// vvd_listener.txt) which connects, confirms with a random pass word
// ("confirm", echoed back), then waits for
//   "setrcvbufsize" (int32), "volume" (the current volume), "com" (a
//   command, run with vvd_run.txt)
// and answers with the result image(s) ("volume") and "com_finish".
//
// Volume block (both directions, wx SendCurrentVolume / FI_VOLUMEDATA):
//   int32 name_len, name bytes (Fiji: with a NUL), int32 nx, ny, nz,
//   int32 bits (8 / 16), int32 r, g, b (0-255), double spcx, spcy, spcz,
//   nx*ny*nz*(bits/8) voxels; native byte order (little-endian)

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QWidget>
#include <QElapsedTimer>
#include <QPointer>

#include "PluginApi.h"

class WxIpcServer;
class WxIpcConnection;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QTimer;
class QProgressDialog;

#define FI_PLUGIN_VERSION "1.01"
#define TMP_OVOX_NONE 0
#define TMP_OVOX_TRUE 1
#define TMP_OVOX_FALSE 2

class FijiInterfacePluginQt : public QObject, public vvd::QtGuiPlugin
{
	Q_OBJECT
public:
	FijiInterfacePluginQt();
	~FijiInterfacePluginQt() override;

	VVDString GetName() const override { return VVDString(L"Fiji Interface"); }
	VVDString GetDisplayName() const override { return VVDString(L"Fiji Interface"); }
	VVDString GetId() const override { return VVDString(L"{4E97DF66-5FBB-4719-AF17-76C1C82D3FE1}"); }
	QWidget* CreatePanel(QWidget* parent) override;
	void OnInit() override;
	void OnDestroy() override;
	bool OnRun(const VVDString& options) override;

	void SetFijiPath(const QString& path) { m_fiji_path = path; }
	QString GetFijiPath() const { return m_fiji_path; }
	QString GetFijiPluginVer() const { return m_fiji_plugin_ver; }
	void SetSendMask(bool val) { m_sendmask = val; }
	bool GetSendMask() const { return m_sendmask; }
	void SetLaunchFijiAtStartup(bool val) { m_launch_fiji_startup = val; }
	bool GetLaunchFijiAtStartup() const { return m_launch_fiji_startup; }
	void SetTempOverrideVox(int flag) { m_tmp_ovvox = flag; }
	int GetTempOverrideVox() const { return m_tmp_ovvox; }
	QString GetPID() const { return m_pid; }

	// fiji_interface_settings.dft (fiji_path, launch_fiji, send_mask)
	void LoadConfigFile();
	void SaveConfigFile();
	static QString ConfigFilePath(bool for_save);

	bool StartFiji();
	void CloseFiji();
	bool isReady();

	bool SendCommand(const QString& command, bool send_mask = false);
	bool SendCurrentVolume(bool send_mask);

	// testing: the server / connection
	WxIpcServer* Server() const { return m_server; }
	WxIpcConnection* Connection() const { return m_conn; }
	// testing: volumes received from Fiji since start-up (names)
	const QStringList& ReceivedVolumes() const { return m_received; }
	// testing: the last volume block received (name, bits, size, chk ok)
	QString LastReceived() const { return m_last_received; }

Q_SIGNALS:
	// wx notifyAll(FI_VERSION_CHECK / FI_COMMAND_FINISHED / FI_RUN) to the window
	void versionChecked(const QString& version);
	void commandFinished(const QString& command);
	void runRequested(const QString& options);

private:
	void OnConnection(WxIpcConnection* conn);
	void OnPoke(const QString& topic, const QString& item, const QByteArray& data, int format);
	void OnVolumeData(const QByteArray& data);   // wx doAction(FI_VOLUMEDATA)
	void OnCommandFinished(const QByteArray& data);

	QString m_fiji_path;
	bool m_initialized = false;
	bool m_booting = false;
	bool m_sendmask = false;
	bool m_launch_fiji_startup = false;
	int m_tmp_ovvox = TMP_OVOX_NONE;
	bool m_ovvox = false;
	bool m_gpsyncspc = false;
	bool m_gpsync = false;
	QString m_pid;
	QString m_sent_mask;
	WxIpcServer* m_server = nullptr;
	QPointer<WxIpcConnection> m_conn;
	QString m_fiji_plugin_ver;
	qint64 m_fiji_pid = 0;
	QStringList m_received;
	QString m_last_received;
};

// wx SampleGuiPluginWindow1
class FijiInterfacePanel : public QWidget
{
	Q_OBJECT
public:
	FijiInterfacePanel(FijiInterfacePluginQt* plugin, QWidget* parent);
	~FijiInterfacePanel() override;

	void EnableControls(bool enable = true);
	void SendCommand(const QString& com, bool send_mask);
	bool IsWaiting() const { return m_waitingforfiji || (m_prg_diag != nullptr); }

	QLineEdit* VersionText() const { return m_VerTextCtrl; }
	QLineEdit* FijiPath() const { return m_FijiPickCtrl; }
	QLineEdit* CommandText() const { return m_CommandTextCtrl; }
	QCheckBox* SendMaskCheck() const { return m_SendMaskChk; }
	QCheckBox* LaunchCheck() const { return m_LaunchChk; }
	QPushButton* SendButton() const { return m_CommandButton; }

private:
	void OnSendEventButton();
	void OnPendingCommandTimer();
	void OnWaitTimer();
	void OnRun(const QString& options);
	void OnCommandFinished(const QString& command);
	void ActivateProcess(const QString& pid);

	FijiInterfacePluginQt* m_plugin = nullptr;
	QLineEdit* m_VerTextCtrl = nullptr;
	QLineEdit* m_FijiPickCtrl = nullptr;
	QLineEdit* m_CommandTextCtrl = nullptr;
	QPushButton* m_CommandButton = nullptr;
	QCheckBox* m_LaunchChk = nullptr;
	QCheckBox* m_SendMaskChk = nullptr;
	QTimer* m_pctimer = nullptr;
	QTimer* m_wtimer = nullptr;
	QElapsedTimer m_pcwatch;
	QProgressDialog* m_prg_diag = nullptr;
	bool m_waitingforfiji = false;
	QString pendingcommand;
	bool pendingcom_msk = false;
};

#endif // VVD_QT_FIJIINTERFACEPLUGINQT_H
