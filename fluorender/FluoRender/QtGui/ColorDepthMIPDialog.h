#ifndef VVD_QT_COLORDEPTHMIPDIALOG_H
#define VVD_QT_COLORDEPTHMIPDIALOG_H

// "Save Color Depth MIP..." options: the dialog of the Fiji macro
// Color_Depth_MIP_batch_0308_2021.ijm (brightness mode, colour scale,
// reversed colour, gamma 1.4, skeleton bolded MIP, DSLT version, MIP slice
// range) plus its second dialog (desired average value, segmentation modes).

#include <QDialog>
#include "ColorDepthMIP.h"

class QRadioButton;
class QCheckBox;
class QSpinBox;
class QLabel;

class ColorDepthMIPDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ColorDepthMIPDialog(QWidget* parent = nullptr);
	void SetOptions(const cdm::Options& o);
	cdm::Options GetOptions() const;
	// volume name / size line at the top
	void SetVolumeInfo(const QString& text);

private:
	void UpdateEnabled();
	QLabel* m_info = nullptr;
	QRadioButton* m_mode[5] = {};
	QCheckBox* m_scale = nullptr;
	QCheckBox* m_reverse = nullptr;
	QCheckBox* m_gamma = nullptr;
	QCheckBox* m_skeleton = nullptr;
	QRadioButton* m_dslt_normal = nullptr;
	QRadioButton* m_dslt_line = nullptr;
	QSpinBox* m_start = nullptr;
	QSpinBox* m_end = nullptr;
	QSpinBox* m_mean = nullptr;
	QLabel* m_mean_label = nullptr;
	int m_threads = 0;
};

#endif
