#ifndef VVD_QT_TRACEDIALOG_H
#define VVD_QT_TRACEDIALOG_H

// "Components & Tracking": Qt port of the wx TraceDlg (TraceDlg.cpp) and its
// TraceListCtrl.
//
//   pages  Track Map  (track map file: Load / Save / Save As; New map:
//                      progress, iterations "X", Generate, Refine T,
//                      Refine All)
//          Selection  (component ID, X, FullCompt, Replace, Append, Clear;
//                      component size slider / text)
//          Linkage    (ID, X, Append, Clear, "Auto Link" check tool;
//                      Excl. Link, Link IDs, Isolate, Unlink IDs)
//          Modify     (new ID, X, Append, Clear, "Auto Assign ID" check
//                      tool; Assign ID, Add ID, Replace ID, Combine,
//                      Divide; Segment exists but is hidden, as wx)
//          Analysis   (Convert to: Rulers, UniIDs; Information: Compnts,
//                      Links, Save As)
//   below  Ghosts: Tail, number (slider + text), Lead; ID Lists (Current T /
//          Previous T, < Backward / Forward >); Output text
//
// It works on the trace group of the current view (RenderCanvas::
// GetTraceGroup, wx VRenderVulkanView::m_trace_group) and its current
// volume's mask / label. As in wx the dialog exists from start-up, hidden:
// wx never shows it (its menu entries are commented out: VRenderFrame
// m_tb_menu_edit / m_top_tools ID_Trace, the tree's "Components &&
// Tracking..."), but the view keys f (CellFull) and l (CellLink) and the
// brush's auto ID / manual assist after each stroke call it.
// MainWindow::ShowTraceDlg shows it (wx ShowTraceDlg: a floating pane).

#include <QWidget>
#include <QTreeWidget>
#include <functional>
#include <vector>

#include "DataManager.h"

class QTabWidget;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QSpinBox;
class QSlider;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QToolButton;
class RenderCanvas;
class RulerTool;
class TraceDialog;

// wx TraceListCtrl: ID list of the current / previous time point
class TraceListWidget : public QTreeWidget
{
	Q_OBJECT
public:
	TraceListWidget(TraceDialog* dlg, int type, QWidget* parent = nullptr);
	// wx Append(gtype, id, color, size, cx, cy, cz)
	void Append(const QString& gtype, unsigned int id, const QColor& color,
		int size, double cx, double cy, double cz);
	// wx UpdateTraces(vrv): the trace group's cell list, sorted
	void UpdateTraces(RenderCanvas* canvas);
	void DeleteSelection();          // wx DeleteSelection
	QString GetText(int item, int col) const;
	int ItemCount() const { return topLevelItemCount(); }
	bool IsItemSelected(int item) const;
	void DeleteAllItems() { clear(); }
	void CopySelection();            // wx OnCopySelection (first selected ID)
	int Type() const { return m_type; }   // 0 current, 1 previous

protected:
	void keyPressEvent(QKeyEvent* e) override;
	void contextMenuEvent(QContextMenuEvent* e) override;

private:
	TraceDialog* m_dlg;
	RenderCanvas* m_canvas = nullptr;
	int m_type = 0;
};

class TraceDialog : public QWidget
{
	Q_OBJECT
public:
	// services of the main window (wx: VRenderFrame / the view)
	struct Hooks
	{
		std::function<RulerTool*(RenderCanvas*)> ruler_tool;    // m_view->GetRulerList()
		std::function<void()> measure_settings;                 // MeasureDlg::GetSettings(m_view)
		std::function<void()> brush_clear;                      // TreePanel::BrushClear
		std::function<void(int)> frame_step;                    // VMovieView::UpFrame / DownFrame
	};

	explicit TraceDialog(QWidget* parent = nullptr);
	~TraceDialog() override;
	void SetHooks(const Hooks& h) { m_hooks = h; }

	// wx GetSettings(vrv) / GetView / UpdateList
	void GetSettings(RenderCanvas* canvas);
	RenderCanvas* GetView() const { return m_view; }
	void UpdateList();
	// the view / a volume is going away
	void ViewClosed(RenderCanvas* canvas);
	void VolumeRemoved(VolumeData* vd);

	bool GetManualAssist() const { return m_manual_assist; }
	bool GetAutoID() const { return m_auto_id; }
	void SetManualAssist(bool b);     // the "Auto Link" check tool
	void SetAutoID(bool b);           // the "Auto Assign ID" check tool

	// cell operations (wx)
	void CellUpdate();
	void CellFull();
	void CellLink(bool exclusive);
	void CellNewID(bool append);
	void CellEraseID();
	void CompDelete();
	void CompClear();
	// the view's Segment (wx VRenderVulkanView::Segment end): auto ID
	// (selector mode 1 / 2 / 4: CellNewID(true), 3: CellEraseID) and manual
	// assist (CellUpdate unless auto ID, CellLink(true))
	void OnViewSegmented(int sel_mode);
	// measurement / output
	void Measure();
	QString OutputMeasureResult();
	bool SaveOutputResult(const QString& filename);
	// automatic tracking
	void GenMap();
	void RefineMap(int t = -1);

	// the button handlers (also used by tests)
	void OnLoadTrace(const QString& file = QString());     // empty: file dialog
	void OnSaveTrace();
	void OnSaveasTrace(const QString& file = QString());
	void OnGenMapBtn() { GenMap(); }
	void OnRefineTBtn() { RefineMap(m_cur_time); }
	void OnRefineAllBtn() { RefineMap(); }
	void OnCompFull();
	void OnCompAppend(bool from_new_id = false);
	void OnCompExclusive();
	void OnCompClear() { CompClear(); }
	void OnCellLink() { CellLink(false); }
	void OnCellExclusiveLink() { CellLink(true); }
	void OnCellIsolate();
	void OnCellUnlink();
	void OnCellNewID() { CellNewID(false); }
	void OnCellAppendID() { CellNewID(true); }
	void OnCellReplaceID();
	void OnCellCombineID();
	void OnCellDivideID();
	void OnCellSegment() {}           // wx: empty (button hidden)
	void OnConvertToRulers();
	void OnConvertConsistent(const QString& out_dir = QString());   // empty: folder dialog
	void OnAnalyzeComp();
	void OnAnalyzeLink();
	void OnSaveResult(const QString& file = QString());
	void OnCellPrev();
	void OnCellNext();

	// testing access
	QTabWidget* Notebook() const { return m_notebook; }
	QLineEdit* CompIDText() const { return m_comp_id_text; }
	QLineEdit* CellNewIDText() const { return m_cell_new_id_text; }
	QLineEdit* CellSizeText() const { return m_cell_size_text; }
	QLineEdit* GhostNumText() const { return m_ghost_num_text; }
	QSpinBox* GenMapSpin() const { return m_gen_map_spin; }
	QCheckBox* GhostTailChk() const { return m_ghost_show_tail_chk; }
	QCheckBox* GhostLeadChk() const { return m_ghost_show_lead_chk; }
	TraceListWidget* ListCurr() const { return m_trace_list_curr; }
	TraceListWidget* ListPrev() const { return m_trace_list_prev; }
	QString StatText() const;
	QString TrackMapText() const;
	int CurTime() const { return m_cur_time; }
	int PrvTime() const { return m_prv_time; }

private:
	struct measure_info
	{
		unsigned int id;
		unsigned int total_num;
		double mean;
		double variance;
		double m2;
		double min;
		double max;
		double ext_sum;
		static bool cmp_id(const measure_info info1, const measure_info info2)
		{ return info1.id < info2.id; }
	};
	std::vector<measure_info> m_info_list;

	QWidget* CreateMapPage(QWidget* parent);
	QWidget* CreateSelectPage(QWidget* parent);
	QWidget* CreateLinkPage(QWidget* parent);
	QWidget* CreateModifyPage(QWidget* parent);
	QWidget* CreateAnalysisPage(QWidget* parent);
	void AddLabel(int item, TraceListWidget* list_ctrl, FL::CellList& list);
	// the selected rows of a list, or all rows when none is selected
	void FillList(TraceListWidget* list_ctrl, FL::CellList& list);
	double GetExt(unsigned int* data_label, unsigned long long index, unsigned int id,
		int nx, int ny, int nz, int i, int j, int k);
	unsigned int GetMappedID(unsigned int id, unsigned int* data_label1,
		unsigned int* data_label2, unsigned long long size);
	VolumeData* CurVolume() const;
	void SaveLabel(VolumeData* vd, const std::shared_ptr<FLIVR::VL_Nrrd>& label);   // wx: <data>.lbl of m_cur_time
	void StatSet(const QString& s);
	void StatAppend(const QString& s);
	void Yield();
	void UpdateCheckIcons();
	void OnCompIDText(bool from_second);
	void OnCellNewIDText();
	void OnGhostNumText();
	void OnCellSizeText();

	Hooks m_hooks;
	RenderCanvas* m_view = nullptr;   // wx m_view
	int m_cur_time = -1;
	int m_prv_time = -1;
	bool m_manual_assist = false;
	bool m_auto_id = false;

	QTabWidget* m_notebook = nullptr;
	//map page
	QLineEdit* m_load_trace_text = nullptr;
	QPushButton* m_load_trace_btn = nullptr;
	QPushButton* m_save_trace_btn = nullptr;
	QPushButton* m_saveas_trace_btn = nullptr;
	QProgressBar* m_gen_map_prg = nullptr;
	QSpinBox* m_gen_map_spin = nullptr;
	QPushButton* m_gen_map_btn = nullptr;
	QPushButton* m_refine_t_btn = nullptr;
	QPushButton* m_refine_all_btn = nullptr;
	//selection page
	QLineEdit* m_comp_id_text = nullptr;
	QSlider* m_cell_size_sldr = nullptr;
	QLineEdit* m_cell_size_text = nullptr;
	//link page
	QLineEdit* m_comp_id_text2 = nullptr;
	QToolButton* m_manual_assist_check = nullptr;
	//modify page
	QLineEdit* m_cell_new_id_text = nullptr;
	QToolButton* m_auto_id_chk = nullptr;
	QPushButton* m_cell_segment_btn = nullptr;
	//ghosts
	QSlider* m_ghost_num_sldr = nullptr;
	QLineEdit* m_ghost_num_text = nullptr;
	QCheckBox* m_ghost_show_tail_chk = nullptr;
	QCheckBox* m_ghost_show_lead_chk = nullptr;
	//lists
	QLabel* m_cell_time_curr_st = nullptr;
	QLabel* m_cell_time_prev_st = nullptr;
	TraceListWidget* m_trace_list_curr = nullptr;
	TraceListWidget* m_trace_list_prev = nullptr;
	//output
	QPlainTextEdit* m_stat_text = nullptr;
};

#endif // VVD_QT_TRACEDIALOG_H
