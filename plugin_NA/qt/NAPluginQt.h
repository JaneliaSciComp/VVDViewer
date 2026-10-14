#ifndef VVD_QT_NAPLUGINQT_H
#define VVD_QT_NAPLUGINQT_H

// "Neuron Annotator Plugin" (name "NAPlugin") of the Qt front end: port of
// plugin_NA/vvd (NeuronAnnotatorPlugin.cpp = NAGuiPlugin +
// MIPGeneratorThread, NeuronAnnotatorPluginWindow.cpp = NAGuiPluginWindow +
// NAListCtrl).
//
// runNALoader(label, volume, chspec, spacings, prefix): the label file
// (.v3dpbd, V3DPBDReader) and the volume (any format; StartupLoad with 80 %
// of the graphics memory as the size budget) are loaded; chspec says which
// of the volume's channels are signals ('s', up to 3) and which is the
// reference ('r'), default "sssr". The signal channels share the label (the
// first gets it, the others SetSharedLabelName) and an empty mask, the
// reference shares the mask; all go into NA mode with every segment shown.
// One entry per segment (the background and the fragments sorted by voxel
// count, largest first; plus "Reference"), each with a maximum intensity
// projection thumbnail made by worker threads (wx MIPGeneratorThread).
// List: check box = visibility (checking a fragment makes it active, i.e.
// highlighted: segment mask 2), selecting = active, deselecting = shown;
// a double click toggles the check box; Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z
// undo / redo the list's deletions (wx: the Delete key is disabled); the
// thumbnails follow the list width. Toolbar: Import (label + h5j volume),
// export (the shown fragments combined into one .nrrd, SaveCombinedFragment).
// Command line: -p "NAPlugin -l <label> -v <volume> [-c <chspec>]
// [-s <x>x<y>x<z>] [-d <prefix>]". Projects: <project>_files/Neuron
// Annotator Plugin.set (wx Save/LoadProjectSettingFile), defaults in
// NA_plugin_settings.dft.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <QImage>
#include <vector>
#include <memory>
#include <mutex>
#include <map>

#include "PluginApi.h"
#include "FLIVR/BBox.h"

class QTreeWidget;
class QTreeWidgetItem;
class QToolBar;
class QTimer;
class V3DPBDReader;
class BaseReader;
namespace FLIVR { class VL_Nrrd; }

#define NA_PLUGIN_VERSION "1.00"

struct NASegment
{
	int id = 0;
	size_t size = 0;
	FLIVR::BBox bbox;
	QImage image;
	QImage thumbnail;
	int visible = 1;   // wx: uninitialised until SetSegmentVisibility
};

class NAPluginQt : public QObject, public vvd::QtGuiPlugin
{
	Q_OBJECT
public:
	NAPluginQt();
	~NAPluginQt() override;

	VVDString GetName() const override { return VVDString(L"NAPlugin"); }
	VVDString GetDisplayName() const override { return VVDString(L"Neuron Annotator Plugin"); }
	VVDString GetId() const override { return VVDString(L"{EEFDF66-5FBB-4719-AF17-76C1C82D3FE1}"); }
	QWidget* CreatePanel(QWidget* parent) override;
	void OnInit() override;
	void OnDestroy() override {}
	void OnTreeUpdate() override;
	bool OnRun(const VVDString& options) override;
	void LoadProjectSettingFile(const VVDString& path) override;
	void SaveProjectSettingFile(const VVDString& path) override;

	void Lock() { m_lock = true; }
	void Unlock() { m_lock = false; }

	void LoadConfigFile();
	void SaveConfigFile();
	static QString ConfigFilePath(bool for_save);

	bool runNALoader();
	bool runNALoader(const QString& id_path, const QString& vol_path, const QString& chspec = "sssr",
		const QString& spacings = QString(), const QString& prefix = QString(), bool set_dirty = false);
	void LoadSettings();
	bool runNALoaderRemote(const QString& url, const QString& usr, const QString& pwd, const QString& outdir, const QString& ofname);
	bool LoadSWC(const QString& name, const QString& swc_zip_path);
	bool LoadFiles(const QString& path, size_t datasize = 0, const QString& desc = QString());
	bool LoadNrrd(int id);
	void SetSegmentVisibility(int id, int vis, bool refresh = true);
	int GetSegmentVisibility(int id);
	void ToggleSegmentVisibility(int id);
	void PushVisHistory();
	void RefreshRenderViews();
	void SwitchPropPanel(bool ref);
	void SaveCombinedFragment(const QString& path);
	void ResizeThumbnails(int w);

	const QImage* getSegMIPThumbnail(int id = 0) const;
	const QImage* getSegMIP(int id = -1) const;
	const QImage* getRefMIPThumbnail() const { return &m_ref_image_thumb; }
	const QImage* getRefMIP() const { return &m_ref_image; }
	int getSegCount() const { return (int)m_segs.size(); }
	const std::vector<NASegment>& Segments() const { return m_segs; }
	bool isRefExists() const { return m_nrrd_r != nullptr; }
	bool isSegExists() const { return m_nrrd_s[0] != nullptr; }
	bool isDirty() const { return m_dirty; }
	void setDirty(bool val) { m_dirty = val; }
	bool isReloaded() const { return m_reload_list; }
	void setReloaded(bool val) { m_reload_list = val; }
	QString getLabelPath() const { return m_id_path; }
	QString getVolumePath() const { return m_vol_path; }
	QString SignalName(int c) const { return c >= 0 && c < 3 ? m_vol_s[c] : QString(); }
	QString ReferenceName() const { return m_vol_r; }
	int SignalCount() const { return m_scount; }

	// MIP of segment s into m_segs[s] (wx MIPGeneratorThread::Entry body)
	void MakeSegmentMIP(int s);

private:
	V3DPBDReader* m_lbl_reader = nullptr;
	BaseReader* m_vol_reader = nullptr;
	std::vector<NASegment> m_segs;
	QImage m_ref_image;
	QImage m_ref_image_thumb;
	std::shared_ptr<FLIVR::VL_Nrrd> m_lbl_nrrd;
	std::shared_ptr<FLIVR::VL_Nrrd> m_nrrd_r;
	QString m_vol_r;
	std::shared_ptr<FLIVR::VL_Nrrd> m_nrrd_s[3];
	QString m_vol_s[3];
	double m_gmaxvals[3] = { 255.0, 255.0, 255.0 };
	int m_scount = 0;
	double m_xspc = -1.0;
	double m_yspc = -1.0;
	double m_zspc = -1.0;
	QString m_vol_suffix;
	bool m_dirty = false;
	bool m_reload_list = false;
	bool m_allsig_visible = true;
	bool m_lock = true;
	QString m_id_path;
	QString m_vol_path;
	QString m_prefix;

	void BuildSegmentsAndMIPs(size_t nx, size_t ny, size_t nz, const std::map<int, int>* vis);
};

// wx NAListCtrl
struct NAListItemData
{
	int dbid = 0;
	QString name;
	int mipid = -1;
	int itemid = -1;
	int imgid = 0;
	int visibility = 0;
};

class NAResultList : public QWidget
{
	Q_OBJECT
public:
	static constexpr int IMG_ID_REF = -2;
	static constexpr int IMG_ID_ALLSIG = -1;

	explicit NAResultList(QWidget* parent);
	void SetPlugin(NAPluginQt* plugin) { m_plugin = plugin; }

	void Append(int imgid, const QString& name, int mipid, bool visibility);
	int GetItemCount() const;
	int ItemImgId(int item) const;   // wx wxAtoi(GetText(item, 0))
	bool IsItemChecked(int item) const;
	void CheckItem(int item, bool check);
	bool IsItemSelected(int item) const;
	void SetItemSelected(int item, bool sel);

	void LoadResults(const QString& idpath, const QString& volpath, const QString& chspec, const QString& prefix = QString());
	void UpdateResults(bool refresh_items = false);
	void UpdateSelections();
	void DeleteSelection();
	void DeleteAll();
	void AddHistory(const NAListItemData& data);
	void Undo();
	void Redo();
	QTreeWidget* Tree() const { return m_tree; }
	int ThumbnailWidth() const;

protected:
	bool eventFilter(QObject* o, QEvent* e) override;

private:
	void OnItemChanged(QTreeWidgetItem* item, int column);
	void OnSelectionChanged();
	void OnTimer();
	struct Sel { int itemid; int visibility; };
	QTreeWidget* m_tree = nullptr;
	std::vector<QImage> m_images;
	std::vector<NAListItemData> m_listdata;
	std::vector<NAListItemData> m_history;
	std::vector<Sel> m_selbuf;
	QTimer* m_selTimer = nullptr;
	int m_history_pos = 0;
	NAPluginQt* m_plugin = nullptr;
	bool m_updating = false;
	std::vector<bool> m_prev_sel;   // selection before the change (wx item selected / deselected events)
	int m_last_width = -1;
};

// wx NAGuiPluginWindow
class NAPanel : public QWidget
{
	Q_OBJECT
public:
	NAPanel(NAPluginQt* plugin, QWidget* parent);
	~NAPanel() override;
	void OnImportResults();
	void OnExport();
	NAResultList* Results() const { return m_results; }
	QToolBar* ToolBar() const { return m_tb; }
private:
	void OnIdle();
	NAPluginQt* m_plugin = nullptr;
	NAResultList* m_results = nullptr;
	QToolBar* m_tb = nullptr;
	QWidget* m_nbpanel = nullptr;
	QTimer* m_idleTimer = nullptr;
};

#endif // VVD_QT_NAPLUGINQT_H
