#ifndef VVD_QT_MEASUREDIALOG_H
#define VVD_QT_MEASUREDIALOG_H

// "Measurement": Qt port of the wx MeasureDlg + RulerListCtrl
// (MeasureDlg.cpp). Drives a RulerTool (the ruler part of the wx view).
//
//   toolbar   Locator / 2pt Ruler / 2+pt Ruler / Edit (check tools),
//             Delete, Delete All, Export (.txt / BigWarp .csv), Import (.csv)
//   options   Pointing Method, Auto-select (not in wx: each click picks the
//             displayed volume with the brightest voxel on the ray, see
//             RulerTool::SetRulerAutoSelect), Transient, Use Volume Properties,
//             Scatter Rulers + Density, Transform + Interpolation,
//             Stiffness, Apply Transform
//   list      ID (wx "Name"), Start Volume, End Volume (not in wx: the
//             volumes the first / last point were picked on, empty for the
//             view plane), Color, Length, Angle/Pitch, Start/End Points
//             (X, Y, Z), Time, Volumes/Score (rulers of the current time
//             point). Double-click: edit the name (or pick the colour in the
//             Color column); drag a row to reorder; Delete/Backspace deletes.
//             The text export keeps the wx column order (+ start / end
//             volume appended), whatever the list order.

#include <QDialog>
#include <vector>

class QToolBar;
class QAction;
class QComboBox;
class QCheckBox;
class QPushButton;
class QLineEdit;
class QSlider;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class RulerTool;
class Ruler;
class AnalysisContext;
class SettingsDialog;
class RulerListWidget;
class RigidCellsReportDialog;
class QDoubleSpinBox;
class QSpinBox;
class QWidget;
namespace rigidcells { struct Params; }

class MeasureDialog : public QDialog
{
	Q_OBJECT
public:
	// settings may be null; when set, option changes are also stored there
	// (wx: frame->GetSettingDlg()->SetPointVolumeMode / SetRulerUseTransf /
	// SetRulerTimeDep).
	MeasureDialog(RulerTool* tool, AnalysisContext* ctx, SettingsDialog* settings, QWidget* parent = nullptr);
	~MeasureDialog() override;

	// list columns (RowText)
	enum Column { ColID = 0, ColStartVol, ColEndVol, ColColor, ColLength, ColAngle, ColPoints, ColTime, ColInfo, ColNum };

	// TPS warp stiffness (lambda)
	double GetWarpLambda() const { return m_warp_lambda; }
	// 0=Thin Plate Spline, 1=Affine, 2=Similarity, 3=Rigid, 4=Translation,
	// 5=Cells: shape preserving (RigidCellsWarp.h)
	int GetWarpTransformType() const;
	enum { WarpCells = 5 };
	// the cell transform's options as set in the dialog (stiffness = the
	// Stiffness slider, same dimensionless value)
	rigidcells::Params GetCellParams() const;
	// results of the last cell transform (created on first use)
	RigidCellsReportDialog* GetCellReportDialog() const { return m_cell_report; }
	// 0=Nearest Neighbor, 1=Linear
	int GetWarpInterpolation() const;
	// RulerListCtrl::GetCount: rows of the rulers (ann null) or of one
	// annotations layer (its texts above the Min Score threshold, counted by
	// the last UpdateList)
	long GetCount(class Annotations* ann = nullptr) const;
	// rows of the list (rulers + annotations), for tests
	int RowCount() const;
	QString RowText(int row, int col) const;
	// RulerListCtrl::Export: the text file of the toolbar's Export (.txt)
	void Export(const QString& filename);
	// "Apply Transform" with the current options (also Ctrl+B in the view)
	void ApplyTransform() { OnWarp(); }

public Q_SLOTS:
	// wx MeasureDlg::GetSettings(vrv): tool state -> toolbar / options, then
	// UpdateList(). Connected to RulerTool::rulersChanged / intModeChanged.
	void GetSettings();
	// wx MeasureDlg::GetSettings(vrv): another render view's ruler tool
	void SetTool(RulerTool* tool);
	RulerTool* GetTool() const { return m_tool; }
	// wx UpdateList / RulerListCtrl::UpdateRulers
	void UpdateList();

protected:
	void showEvent(QShowEvent* e) override;

private Q_SLOTS:
	void OnNewLocator(bool checked);
	void OnNewRuler(bool checked);
	void OnNewRulerMP(bool checked);
	void OnRulerEdit(bool checked);
	void OnDelete();
	void OnDeleteAll();
	void OnImport();
	void OnExport();
	void OnIntensityMethodsCombo(int index);
	void OnUseTransferCheck(bool checked);
	void OnTransientCheck(bool checked);
	void OnAutoSelectCheck(bool checked);
	void OnScatterRulers();
	void OnWarp();
	void OnWarpStiffness(int value);
	void OnWarpType(int index);
	void OnCellOptionChanged();
	void OnItemDoubleClicked(QTreeWidgetItem* item, int column);
	void OnItemChanged(QTreeWidgetItem* item, int column);

private:
	friend class RulerListWidget;
	// RulerListCtrl
	void DeleteSelection();
	void DeleteAll(bool cur_time = true);
	void DragTo(int from_row, int to_row);
	QString UnitText() const;
	Ruler* RulerOf(QTreeWidgetItem* item) const;
	void FillItem(QTreeWidgetItem* item, Ruler* ruler);

	RulerTool* m_tool;
	AnalysisContext* m_ctx;
	SettingsDialog* m_settings;
	bool m_updating = false;

	QToolBar* m_toolbar;
	QAction* m_locator_act;
	QAction* m_ruler_act;
	QAction* m_ruler_mp_act;
	QAction* m_ruler_edit_act;
	QComboBox* m_int_method_combo;
	QCheckBox* m_use_transfer_chk;
	QCheckBox* m_transient_chk;
	QCheckBox* m_auto_select_chk;
	QPushButton* m_scatter_btn;
	QLineEdit* m_density_txt;
	QPushButton* m_warp_btn;
	QComboBox* m_warp_type_combo;
	QComboBox* m_warp_interp_combo;
	QSlider* m_warp_stiffness_sldr;
	QLabel* m_warp_stiffness_text;
	double m_warp_lambda = 0.0;
	// "Cells: shape preserving" options (shown when that transform is selected)
	QWidget* m_cell_panel = nullptr;
	QComboBox* m_cell_local_combo = nullptr;
	QComboBox* m_cell_objects_combo = nullptr;
	QDoubleSpinBox* m_cell_beta = nullptr;
	QDoubleSpinBox* m_cell_sigma_min = nullptr;
	QDoubleSpinBox* m_cell_neurite = nullptr;
	QSpinBox* m_cell_samples = nullptr;
	QSpinBox* m_cell_min_voxels = nullptr;
	QDoubleSpinBox* m_cell_radius = nullptr;
	QComboBox* m_cell_device_combo = nullptr;
	QCheckBox* m_cell_loo_chk = nullptr;
	RigidCellsReportDialog* m_cell_report = nullptr;
	void BuildCellPanel();
	void UpdateCellEnables();

	RulerListWidget* m_rulerlist;
	// wx RulerListCtrl::m_counts: [0] rulers of the current time point, then
	// one entry per shown annotations layer of the view (layer order)
	std::vector<long> m_counts;
	void CountAnnotations();
};

#endif // VVD_QT_MEASUREDIALOG_H
