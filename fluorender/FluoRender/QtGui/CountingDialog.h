#ifndef VVD_QT_COUNTINGDIALOG_H
#define VVD_QT_COUNTINGDIALOG_H

// "Counting and Volume": Qt port of the wx CountingDlg (component analyzer:
// threshold, min/max size, component count, total volume, export).

#include <QDialog>

class QSlider;
class QLineEdit;
class QPushButton;
class QCheckBox;
class AnalysisContext;

class CountingDialog : public QDialog
{
	Q_OBJECT
public:
	explicit CountingDialog(AnalysisContext* ctx, QWidget* parent = nullptr);
	~CountingDialog() override;

public Q_SLOTS:
	// Re-read the threshold range from the current volume (wx: GetSettings(vrv)).
	// Call when the selected volume changes; also run on show.
	void GetSettings();

protected:
	void showEvent(QShowEvent* e) override;

private Q_SLOTS:
	void OnCAThreshChange(int ival);
	void OnCAThreshText(const QString& str);
	void OnCAAnalyzeBtn();
	void OnCAIgnoreMaxChk(bool checked);
	void OnCAMultiChannBtn();
	void OnCARandomColorBtn();
	void OnCAAnnotationsBtn();

private:
	// default_brush_settings.dft (ca_select_only, ca_min, ca_max,
	// ca_ignore_max, ca_thresh), as the wx dialog does
	void LoadDefault();

	AnalysisContext* m_ctx;
	double m_max_value;
	double m_dft_thresh;

	QCheckBox* m_ca_select_only_chk;
	QLineEdit* m_ca_min_text;
	QLineEdit* m_ca_max_text;
	QCheckBox* m_ca_ignore_max_chk;
	QSlider* m_ca_thresh_sldr;
	QLineEdit* m_ca_thresh_text;
	QPushButton* m_ca_analyze_btn;
	QPushButton* m_ca_multi_chann_btn;
	QPushButton* m_ca_random_color_btn;
	QPushButton* m_ca_annotations_btn;
	QLineEdit* m_ca_comps_text;
	QLineEdit* m_ca_volume_text;
	QLineEdit* m_ca_vol_unit_text;
};

#endif
