#ifndef VVD_QT_NOISEREDUCTIONDIALOG_H
#define VVD_QT_NOISEREDUCTIONDIALOG_H

// "Noise Reduction": Qt port of the wx NoiseCancellingDlg.
// Threshold / voxel-size component analysis preview, erase (extract the
// unselected part into a "_DELETED" volume), enhance-selection toggle.

#include <QDialog>

class QSlider;
class QLineEdit;
class QPushButton;
class QCheckBox;
class AnalysisContext;

class NoiseReductionDialog : public QDialog
{
	Q_OBJECT
public:
	explicit NoiseReductionDialog(AnalysisContext* ctx, QWidget* parent = nullptr);
	~NoiseReductionDialog() override;

	// Defaults (wx: VRenderFrame reads "nr_thresh"/"nr_size" from its settings
	// and passes them in; BrushToolDlg keeps the same values). thresh is
	// normalized (0..1 of the volume max value), size in voxels.
	void SetDftThresh(double thresh) { m_dft_thresh = thresh; }
	double GetDftThresh() const { return m_dft_thresh; }
	void SetDftSize(double size) { m_dft_size = size; }
	double GetDftSize() const { return m_dft_size; }

public Q_SLOTS:
	// Re-read ranges from the current volume (wx: GetSettings(vrv)).
	// Call when the selected volume changes; also run on show.
	void GetSettings();

protected:
	void showEvent(QShowEvent* e) override;

private Q_SLOTS:
	void OnThresholdChange(int ival);
	void OnThresholdText(const QString& str);
	void OnVoxelChange(int ival);
	void OnVoxelText(const QString& str);
	void OnPreviewBtn();
	void OnEraseBtn();
	void OnEnhanceSelChk(bool checked);

private:
	AnalysisContext* m_ctx;
	double m_max_value;
	double m_dft_thresh;
	double m_dft_size;
	bool m_previewed;

	QSlider* m_threshold_sldr;
	QLineEdit* m_threshold_text;
	QPushButton* m_preview_btn;
	QSlider* m_voxel_sldr;
	QLineEdit* m_voxel_text;
	QPushButton* m_erase_btn;
	QCheckBox* m_enhance_sel_chk;
};

#endif
