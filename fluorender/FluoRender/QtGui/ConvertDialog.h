#ifndef VVD_QT_CONVERTDIALOG_H
#define VVD_QT_CONVERTDIALOG_H

// "Convert": Qt port of the wx ConvertDlg (volume -> polygon mesh through
// Converters/VolumeMeshConv). The new mesh is registered in the DataManager
// and handed out through meshCreated(); the receiver adds it to the view /
// tree (wx: GetView(0)->AddMeshData(md); RefreshGL(); UpdateTree()).

#include <QDialog>

class QSlider;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QPlainTextEdit;
class AnalysisContext;
class MeshData;

class ConvertDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ConvertDialog(AnalysisContext* ctx, QWidget* parent = nullptr);
	~ConvertDialog() override;

public Q_SLOTS:
	// Re-read the threshold range from the current volume (wx: GetSettings(vrv)).
	// Call when the selected volume changes; also run on show.
	void GetSettings();

Q_SIGNALS:
	// Emitted after a successful conversion. md is owned by the DataManager
	// (already in its mesh list, i.e. DataManager::GetLastMeshData()).
	void meshCreated(MeshData* md);

protected:
	void showEvent(QShowEvent* e) override;

private Q_SLOTS:
	void OnCnvVolMeshThreshChange(int ival);
	void OnCnvVolMeshThreshText(const QString& str);
	void OnCnvVolMeshDownsampleChange(int ival);
	void OnCnvVolMeshDownsampleText(const QString& str);
	void OnCnvVolMeshDownsampleZChange(int ival);
	void OnCnvVolMeshDownsampleZText(const QString& str);
	void OnCnvVolMeshConvert();

private:
	AnalysisContext* m_ctx;
	double m_max_value;
	double m_cnv_vol_mesh_thresh;

	QSlider* m_cnv_vol_mesh_thresh_sldr;
	QLineEdit* m_cnv_vol_mesh_thresh_text;
	QSlider* m_cnv_vol_mesh_downsample_sldr;
	QLineEdit* m_cnv_vol_mesh_downsample_text;
	QSlider* m_cnv_vol_mesh_downsample_z_sldr;
	QLineEdit* m_cnv_vol_mesh_downsample_z_text;
	QCheckBox* m_cnv_vol_mesh_usetransf_chk;
	QCheckBox* m_cnv_vol_mesh_selected_chk;
	QCheckBox* m_cnv_vol_mesh_weld_chk;
	QPushButton* m_cnv_vol_mesh_convert_btn;
	QPlainTextEdit* m_stat_text;
};

#endif
