#ifndef VVD_QT_COLOCALIZATIONDIALOG_H
#define VVD_QT_COLOCALIZATIONDIALOG_H

// "Colocalization Analysis": Qt port of ColocalizationDlg (wx).
// Rows: Volume A [Load][name], Volume B [Load][name], Min Size slider+box,
// Max Size slider+box ("Ignored" at the top end), [Select Both] ... [Colocalization].
// Load takes AnalysisContext::CurrentVolume(); the name boxes are editable and
// resolved through the DataManager when Colocalization is pressed.

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QSlider;
class QStringListModel;
class VolumeData;
class AnalysisContext;

class ColocalizationDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ColocalizationDialog(AnalysisContext* ctx, QWidget* parent = nullptr);

	// wx GetSettings(vrv): sync the "Select Both" box from the view's selector.
	void GetSettings();
	void SetVolumeA(VolumeData* vd);
	void SetVolumeB(VolumeData* vd);   // also turns on Select Both
	// resolve the A/B name boxes through the DataManager
	void LoadVolumes();

protected:
	void showEvent(QShowEvent* e) override;

private Q_SLOTS:
	void OnLoadA();
	void OnLoadB();
	void OnMinSizeChange(int v);
	void OnMinSizeText(const QString& s);
	void OnMaxSizeChange(int v);
	void OnMaxSizeText(const QString& s);
	void OnSelectBothChk();
	void OnColocalizationBtn();

private:
	void RefreshNameCompleter();

	AnalysisContext* m_ctx;
	VolumeData* m_vol_a;
	VolumeData* m_vol_b;

	QPushButton* m_calc_load_a_btn;
	QLineEdit* m_calc_a_text;
	QPushButton* m_calc_load_b_btn;
	QLineEdit* m_calc_b_text;
	QSlider* m_min_size_sldr;
	QLineEdit* m_min_size_text;
	QSlider* m_max_size_sldr;
	QLineEdit* m_max_size_text;
	QCheckBox* m_select_both_chk;
	QPushButton* m_colocalization_btn;
	QStringListModel* m_names;
};

#endif
