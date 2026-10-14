#ifndef VVD_QT_NBLASTPLUGINQT_H
#define VVD_QT_NBLASTPLUGINQT_H

// "NBLAST Search" plugin of the Qt front end: port of plugin_nblast/vvd
// (NBLASTGuiPlugin.cpp, NBLASTGuiPluginWindow.cpp).
//
// The search runs plugin_nblast/r/nblast_search.R with Rscript (installed in
// the bundle's Resources / next to the executable):
//   Rscript nblast_search.R <query> <db1.rds,db2.rds,...> <out name> <out dir>
//           <top N> [<db names> [forward|mean]]
// query: the selected volume (its mask when it has one, else the volume) as
// <temp>/vvdnbtmpv.nrrd, or the selected mesh (an SWC) as vvdnbtmpsk.swc. A
// volume whose name has no "skeleton" in it is first skeletonized in Fiji
// ("NBLAST Skeletonize" through the Fiji Interface plugin, mask sent, voxel
// override off); the search starts when Fiji reports the command finished.
// Result file <out dir>/<out name>.txt (the R script's format):
//   line 1: database names, comma separated
//   line 2: database (.rds) paths, comma separated
//   then one line per hit: name,database index,score
// plus <out name>.png, the query's z projection (overlay of the previews).
// Each database folder (the .rds file's directory) has the previews:
//   MIP_thumb/<name>.png, swc_thumb/<name>.png   list thumbnails
//   swc_prev/<name>.png, MIP/<name>.png (+ bg.png) preview images
//   swc/<name>.swc, volume/<name>.nrrd            loaded by double click
//
// Differences from wx: the R script runs as a child process (wx on macOS
// opened it in a Terminal window through term.sh and waited for that
// window), its output goes to a progress dialog and stderr; every selected
// database is checked for existence (wx skipped the last one).

#include <QObject>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <QDialog>
#include <QImage>
#include <vector>

#include "PluginApi.h"

class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;
class QRadioButton;
class QPushButton;
class QLineEdit;
class QGroupBox;
class QGridLayout;
class QToolBar;
class QSplitter;

#define NB_PLUGIN_VERSION "1.00"

class NBLASTPluginQt : public QObject, public vvd::QtGuiPlugin
{
	Q_OBJECT
public:
	NBLASTPluginQt();
	~NBLASTPluginQt() override;

	VVDString GetName() const override { return VVDString(L"NBLAST Search"); }
	VVDString GetDisplayName() const override { return VVDString(L"NBLAST Search"); }
	VVDString GetId() const override { return VVDString(L"{EEFDF66-5FBB-4719-AF17-76C1C82D3FE1}"); }
	QWidget* CreatePanel(QWidget* parent) override;
	void OnInit() override;
	void OnDestroy() override {}
	bool OnRun(const VVDString& options) override { (void)options; return true; }

	void SetRPath(const QString& path) { m_R_path = path; }
	QString GetRPath() const { return m_R_path; }
	void SetNlibPath(const QString& path) { m_nlib_path = path; }
	QString GetNlibPath() const { return m_nlib_path; }
	void SetOutDir(const QString& path) { m_out_dir = path; }
	QString GetOutDir() const { return m_out_dir; }
	void SetFileName(const QString& name) { m_ofname = name; }
	QString GetFileName() const { return m_ofname; }
	void SetResultNum(const QString& num) { m_rnum = num; }
	QString GetResultNum() const { return m_rnum; }
	void SetDatabaseNames(const QString& names) { m_db_names = names; }
	QString GetDatabaseNames() const { return m_db_names; }
	void SetScoringMethod(const QString& mtd) { m_scmtd = mtd; }
	QString GetScoringMethod() const { return m_scmtd; }
	static void SetExportMIPs(bool val) { m_exp_mip = val; }
	static bool GetExportMIPs() { return m_exp_mip; }
	static void SetExportSWCPrevImgs(bool val) { m_exp_swcprev = val; }
	static bool GetExportSWCPrevImgs() { return m_exp_swcprev; }
	static void SetExportSWCs(bool val) { m_exp_swc = val; }
	static bool GetExportSWCs() { return m_exp_swc; }
	static void SetExportVolumes(bool val) { m_exp_vol = val; }
	static bool GetExportVolumes() { return m_exp_vol; }
	static void SetPrefixScore(bool val) { m_pfx_score = val; }
	static bool GetPrefixScore() { return m_pfx_score; }
	static void SetPrefixDatabase(bool val) { m_pfx_db = val; }
	static bool GetPrefixDatabase() { return m_pfx_db; }

	// NBLAST_plugin_settings.dft
	void LoadConfigFile();
	void SaveConfigFile();
	static QString ResourcePath(const QString& name, bool for_save);

	bool runNBLAST();
	bool runNBLAST(const QString& rpath, const QString& nlibpath, const QString& outdir, const QString& ofname,
		const QString& rnum, const QString& db_names = QString(), const QString& scmethod = "forward");
	bool runNBLASTremote(const QString& url, const QString& usr, const QString& pwd, const QString& nlibpath,
		const QString& outdir, const QString& ofname, const QString& rnum, const QString& db_names = QString(),
		const QString& scmethod = "forward");
	bool LoadSWC(const QString& name, const QString& swc_zip_path);
	bool LoadFiles(const QString& path);
	bool skeletonizeMask();

	// the R script (Resources/nblast_search.R, else next to the executable)
	static QString RScriptPath();
	// testing: VVD_QT_NBLAST_RSCRIPT=<file> replaces nblast_search.R (e.g. a
	// recorded result instead of the real search); the last command line
	QStringList LastCommand() const { return m_last_command; }
	int LastExitCode() const { return m_last_exit; }

private:
	bool SaveQuery(QString& tempdpath, bool volume_only);   // the query file of runNBLAST(remote)
	bool RunRScript(const QStringList& args);

	QString m_R_path;
	QString m_out_dir;
	QString m_nlib_path;
	QString m_ofname;
	QString m_rnum;
	QString m_db_names;
	QString m_scmtd;
	QString m_pid;
	QStringList m_last_command;
	int m_last_exit = -1;

	static bool m_exp_swc;
	static bool m_exp_swcprev;
	static bool m_exp_mip;
	static bool m_exp_vol;
	static bool m_pfx_score;
	static bool m_pfx_db;
};

// wx NBLASTDBListItemData
struct NBLASTDBListItemData
{
	QString name;
	QString path;
	QString usr;
	QString pwd;
	bool state = false;
};

// wx wxUsrPwdDialog
class NBUsrPwdDialog : public QDialog
{
	Q_OBJECT
public:
	NBUsrPwdDialog(QWidget* parent, const QString& title);
	QString GetUserNameText() const;
	QString GetPasswordText() const;
	void SetUserNameText(const QString& usr);
	void SetPasswordText(const QString& pwd);
private:
	QLineEdit* m_usrtxt = nullptr;
	QLineEdit* m_pwdtxt = nullptr;
};

// wx wxDBListDialog + NBLASTDatabaseListCtrl ("Edit NBLAST Database")
class NBDBListDialog : public QDialog
{
	Q_OBJECT
public:
	NBDBListDialog(QWidget* parent, const QString& title);

	std::vector<NBLASTDBListItemData> getList() const { return m_list; }
	void setState(size_t id, bool state) { if (id < m_list.size()) m_list[id].state = state; }
	bool getState(size_t id) const { return id < m_list.size() ? m_list[id].state : false; }
	void Add(const QString& path, const QString& name = QString(), bool selection = false);
	void DeleteSelection();
	void DeleteAll();
	// NBLAST_database_list.txt: name \t path \t true|false \t user \t password
	void LoadList();
	void SaveList();
	static QString ListPath(bool for_save);
	QTreeWidget* List() const { return m_listctrl; }
	QLineEdit* NewDBPath() const { return m_new_db_pick; }
	void SetUsrPwd(size_t id, const QString& usr, const QString& pwd)
	{ if (id < m_list.size()) { m_list[id].usr = usr; m_list[id].pwd = pwd; } }

protected:
	bool eventFilter(QObject* o, QEvent* e) override;

private:
	void UpdateList();
	void PlaceSettingButton();
	void OnSettingButton();
	void OnItemChanged(QTreeWidgetItem* item, int column);
	std::vector<NBLASTDBListItemData> m_list;
	QTreeWidget* m_listctrl = nullptr;
	QLineEdit* m_new_db_pick = nullptr;
	QPushButton* m_add_button = nullptr;
	QPushButton* m_setting_b = nullptr;
	bool m_updating = false;
};

// wx wxNBLASTSettingDialog ("NBLAST Settings")
class NBSettingDialog : public QDialog
{
	Q_OBJECT
public:
	NBSettingDialog(NBLASTPluginQt* plugin, QWidget* parent, const QString& title);
	void SaveSettings();
	QLineEdit* RPath() const { return m_RPickCtrl; }
	QLineEdit* TempDir() const { return m_tmpdirPickCtrl; }
	QLineEdit* ResultNum() const { return m_rnumTextCtrl; }
private:
	QLineEdit* m_RPickCtrl = nullptr;
	QLineEdit* m_tmpdirPickCtrl = nullptr;
	QLineEdit* m_rnumTextCtrl = nullptr;
	NBLASTPluginQt* m_plugin = nullptr;
};

// wx wxImagePanel (NBLAST)
class NBImagePanel : public QWidget
{
	Q_OBJECT
public:
	explicit NBImagePanel(QWidget* parent);
	void SetImage(const QString& file);
	void SetOverlayImage(const QString& file);
	void SetBackgroundImage(const QString& file);
	void ResetImage();
	void UpdateImage(bool ov_show);
	QSize CalcImageSizeKeepAspectRatio(int w, int h) const;
	double GetAspectRatio() const;
	const QImage& Image() const { return m_image; }
protected:
	void paintEvent(QPaintEvent*) override;
private:
	QImage m_image;
	QImage m_orgimage;
	QImage m_olimage;
	QImage m_bgimage;
};

// wx NBLASTListItemData
struct NBLASTListItemData
{
	int dbid = -1;
	QString name;
	QString dbname;
	int swcid = -1;
	int mipid = -1;
	QString score;
	int itemid = -1;
};

// wx NBLASTListCtrl: columns 0 (hidden), 1 database index (hidden), Name,
// Database, Skeleton (thumbnail), Volume Data (thumbnail), Score
class NBResultList : public QWidget
{
	Q_OBJECT
public:
	explicit NBResultList(QWidget* parent);

	void Append(const QString& name, const QString& dbname, const QString& score, int mipid = -1, int swcid = -1,
		int dbid = -1, int index = -1);
	QString GetText(int item, int col) const;
	int GetImageId(int item, int col) const;
	int GetItemCount() const;
	int GetSelected() const;
	void Select(int item);

	void LoadResults(const QString& csvfilepath);
	void SaveResults(const QString& txtpath, bool export_swc = false, bool export_swcprev = false,
		bool export_mip = false, bool export_vol = false, bool pfx_score = false, bool pfx_db = false, bool zip = false);
	QString GetListFilePath() const { return m_rfpath; }

	void DeleteSelection();
	void DeleteAll();
	void AddHistory(const NBLASTListItemData& data);
	void Undo();
	void Redo();
	QTreeWidget* Tree() const { return m_tree; }
	// wx OnLeftDClick on the Skeleton (4) / Volume Data (5) column
	void ActivateCell(int item, int col);

Q_SIGNALS:
	void openFile(const QString& path);      // wx NB_OPEN_FILE
	void setImages(const QString& swcprev, const QString& mip);   // wx NB_SET_IMAGE

protected:
	bool eventFilter(QObject* o, QEvent* e) override;

private:
	void OnSelect();
	QTreeWidget* m_tree = nullptr;
	std::vector<QImage> m_images;   // wx m_images (index 0: the black dummy)
	QSize m_thumb;
	QStringList m_dbdirs;
	QStringList m_dbpaths;
	QStringList m_dbnames;
	std::vector<NBLASTListItemData> m_listdata;
	std::vector<NBLASTListItemData> m_history;
	int m_history_pos = 0;
	QString m_rfpath;
};

// wx NBLASTGuiPluginWindow
class NBLASTPanel : public QWidget
{
	Q_OBJECT
public:
	NBLASTPanel(NBLASTPluginQt* plugin, QWidget* parent);
	~NBLASTPanel() override;

	void EnableControls(bool enable = true);
	void OnRunNBLAST();                // wx OnSENDEVENTBUTTONClick
	void OnImportResults();
	void OnSettingButton();
	void OnEditDB();
	void OnSave();
	void ReloadDatabaseChecks();
	bool WaitingForFiji() const { return m_waitingforFiji; }

	NBResultList* Results() const { return m_results; }
	NBImagePanel* SwcImage() const { return m_swcImagePanel; }
	NBImagePanel* MipImage() const { return m_mipImagePanel; }
	QCheckBox* OverlayCheck() const { return m_overlayChk; }
	const std::vector<QCheckBox*>& DatabaseChecks() const { return m_nlib_chks; }
	QRadioButton* ForwardRadio() const { return m_sc_forward; }
	QRadioButton* MeanRadio() const { return m_sc_mean; }
	QToolBar* ToolBar() const { return m_tb; }

	// wx CreateExtraNBLASTControl ("Additional Options" of Save)
	static QWidget* CreateExtraNBLASTControl(QWidget* parent);

private:
	void OnSetImages(const QString& swcprev, const QString& mip);
	void OnOverlayCheck();
	void OnInterop(const QString& message);
	void BuildDatabaseChecks(const std::vector<NBLASTDBListItemData>& list);

	NBLASTPluginQt* m_plugin = nullptr;
	QSplitter* m_splitterWindow = nullptr;
	NBResultList* m_results = nullptr;
	NBImagePanel* m_swcImagePanel = nullptr;
	NBImagePanel* m_mipImagePanel = nullptr;
	QPushButton* m_CommandButton = nullptr;
	QCheckBox* m_overlayChk = nullptr;
	bool m_waitingforR = false;
	bool m_waitingforFiji = false;
	std::vector<NBLASTDBListItemData> m_nlib_list;
	std::vector<QCheckBox*> m_nlib_chks;
	QGroupBox* m_nlib_box = nullptr;
	QGridLayout* m_nlib_grid = nullptr;
	QWidget* m_nbpanel = nullptr;
	QToolBar* m_tb = nullptr;
	QRadioButton* m_sc_forward = nullptr;
	QRadioButton* m_sc_mean = nullptr;
};

#endif // VVD_QT_NBLASTPLUGINQT_H
