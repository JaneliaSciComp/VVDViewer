#ifndef VVD_QT_MESHPROPPANEL_H
#define VVD_QT_MESHPROPPANEL_H

// Mesh "Properties" panel: Qt port of MPropView (same controls, same
// arrangement), plus MeshManipPanel, the port of MManipulator
// (translation / rotation / scaling text boxes with spin arrows).
//
// MPropView layout:
//   left column : "Material" box -- Diffuse Color, Specular Color, Shininess
//   right column: Transparency, Shadow, Lighting, [Scaling], [Size limit],
//                 Radius Scale (SWC only), Label, Sync (Group)/(Name) (ROI
//                 trees only), Sync
// Scaling and Size limit rows exist but are hidden, exactly as in wx.

#include <QWidget>

class QSlider;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QLabel;
class QToolButton;
class QColor;
class MeshData;
class RenderCanvas;

class MeshPropPanel : public QWidget
{
	Q_OBJECT
public:
	explicit MeshPropPanel(QWidget* parent = nullptr);

	// view: the canvas showing the mesh; "Sync" applies the settings to all
	// meshes of that view (wx: MPropView::SetMeshData(md, vrv)). nullptr md
	// disables the panel.
	void SetMeshData(MeshData* md, RenderCanvas* view = nullptr);
	MeshData* GetMeshData() const { return m_md; }
	void GetSettings();     // refresh controls from the mesh
	// wx MPropView::GetSyncGroup / GetSyncName (SWC segment visibility toggles)
	bool GetSyncGroup() const { return m_group_sync; }
	bool GetSyncName() const { return m_name_sync; }

Q_SIGNALS:
	void meshChanged();          // something that needs a redraw changed
	void meshBoundsChanged();    // SWC radius scale changed: recompute view bounds
	                             // (wx: InitView(INIT_BOUNDS | INIT_CENTER)) and redraw

private:
	void UpdateSync();
	void UpdateRadScale();
	void SetPickerColor(QPushButton* b, const QColor& c);

	void OnLightingCheck();
	void OnDiffChange();
	void OnSpecChange();
	void OnShineChange(int v);
	void OnShineText();
	void OnAlphaChange(int v);
	void OnAlphaText();
	void OnScaleChange(int v);
	void OnScaleText();
	void OnShadowCheck();
	void OnShadowChange(int v);
	void OnShadowText();
	void OnSizeCheck();
	void OnSizeChange(int v);
	void OnSizeText();
	void OnLabelCheck();
	void OnSyncGroupCheck();
	void OnSyncNameCheck();
	void OnSyncCheck();

	MeshData* m_md = nullptr;
	RenderCanvas* m_view = nullptr;
	bool m_updating = false;
	bool m_group_sync = false;
	bool m_name_sync = false;
	bool m_sync = false;

	// lighting / material
	QCheckBox* m_light_chk = nullptr;
	QPushButton* m_diff_picker = nullptr;
	QPushButton* m_spec_picker = nullptr;
	QSlider* m_shine_sldr = nullptr;
	QLineEdit* m_shine_text = nullptr;
	QSlider* m_alpha_sldr = nullptr;
	QLineEdit* m_alpha_text = nullptr;
	QWidget* m_scale_row = nullptr;
	QSlider* m_scale_sldr = nullptr;
	QLineEdit* m_scale_text = nullptr;
	// shadow
	QCheckBox* m_shadow_chk = nullptr;
	QSlider* m_shadow_sldr = nullptr;
	QLineEdit* m_shadow_text = nullptr;
	// size limiter
	QWidget* m_size_row = nullptr;
	QCheckBox* m_size_chk = nullptr;
	QSlider* m_size_sldr = nullptr;
	QLineEdit* m_size_text = nullptr;
	// swc radius
	QLabel* m_r_st = nullptr;
	QLineEdit* m_r_text = nullptr;
	// label / sync
	QCheckBox* m_label_chk = nullptr;
	QCheckBox* m_sync_group_chk = nullptr;
	QCheckBox* m_sync_name_chk = nullptr;
	QCheckBox* m_sync_chk = nullptr;
};

// Port of MManipulator. The wx frame shows it instead of MPropView for the
// tree's "mesh manipulation" selection (OnSelection type 6).
class MeshManipPanel : public QWidget
{
	Q_OBJECT
public:
	explicit MeshManipPanel(QWidget* parent = nullptr);

	void SetMeshData(MeshData* md) { m_md = md; setEnabled(md != nullptr); }
	MeshData* GetMeshData() const { return m_md; }
	void GetData();        // refresh text boxes from the mesh

Q_SIGNALS:
	void meshChanged();

private:
	struct Field { QLineEdit* text = nullptr; QToolButton* up = nullptr; QToolButton* down = nullptr; };
	Field MakeField(QWidget* row_parent);
	void Step(QLineEdit* text, double delta);   // OnSpinUp / OnSpinDown
	void OnValueEnter(QLineEdit* text);
	void UpdateData();

	MeshData* m_md = nullptr;
	Field m_x_trans, m_y_trans, m_z_trans;
	Field m_x_rot, m_y_rot, m_z_rot;
	Field m_x_scl, m_y_scl, m_z_scl;
};

#endif // VVD_QT_MESHPROPPANEL_H
