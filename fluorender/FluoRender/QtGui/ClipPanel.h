#ifndef VVD_QT_CLIPPANEL_H
#define VVD_QT_CLIPPANEL_H

// "Clipping Planes" panel: Qt port of ClippingView (wx). Per axis a vertical
// dual-handle range slider with min/max value boxes, Link checkbox, YZ/XZ/XY
// buttons, and clipping-plane rotation controls.
//
// Targets (wx SetVolumeData / SetMeshData / SetGroup): the selected volume or
// mesh; with "Sync All Chan." the view-level planes of the RenderCanvas
// (VRenderVulkanView::SetSyncClippingPlanes / linked params), with "Sync
// Group" a DataGroup's. Hovering the pane (or "Display Planes") shows the
// planes in the canvas (wx ClippingView::OnIdle -> m_draw_clip).

#include <QWidget>
#include <vector>

class QCheckBox;
class QComboBox;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QSlider;
class VolumeData;
class MeshData;
class ClippingLayer;
class RenderCanvas;
class DualSlider;
namespace FLIVR { class Plane; }

class ClipPanel : public QWidget
{
	Q_OBJECT
public:
	explicit ClipPanel(QWidget* parent = nullptr);
	~ClipPanel() override;   // wx ~ClippingView: SaveDefault

	// the view whose planes / rotations / overlay this pane drives
	void SetRenderCanvas(RenderCanvas* canvas);
	RenderCanvas* GetRenderCanvas() const { return m_canvas; }
	void SetVolume(VolumeData* vd);   // wx SetVolumeData; nullptr: ClearData
	VolumeData* GetVolume() const { return m_vd; }
	void SetMeshData(MeshData* md);   // wx SetMeshData; nullptr: ClearData
	MeshData* GetMeshData() const { return m_md; }
	// wx SetGroup: the DataGroup / MeshGroup of the selection ("Sync Group")
	void SetGroup(ClippingLayer* group);
	ClippingLayer* GetGroup() const { return m_group; }
	void ClearData();
	int GetSelType() const { return m_sel_type; }   // 2 volume, 3 mesh, -1 none
	void GetSettings();               // refresh controls from the target

	bool GetChannLink() const;
	void SetChannLink(bool v);
	bool GetGroupSync() const;
	bool GetLink(int axis) const;     // 0..2 = X,Y,Z
	void SetLink(int axis, bool v);   // wx SetXLink / SetYLink / SetZLink
	int  GetPlaneMode() const;        // 0 Normal,1 Frame,2 LowTrans,3 LowTransBack,4 NormalBack
	void SetPlaneMode(int mode);
	bool GetFixRotations() const;
	bool GetDisplayPlanes() const;    // wx GetHoldPlanes
	bool GetHoldPlanes() const { return GetDisplayPlanes(); }
	void SetClippingPlaneRotations(double rx, double ry, double rz, bool update_only_ui = false);
	void GetClippingPlaneRotations(double& rx, double& ry, double& rz) const;
	// clip range per axis in slider units (voxels of the combined box)
	void GetClipRange(int axis, int& lo, int& hi) const;

	// wx MoveLinkedClippingPlanes: 's' (dir 1) / 'w' (dir 0) in the canvas
	// (RenderCanvas::clipMoveRequested)
	void MoveLinkedClippingPlanes(int dir);
	// wx GetXdist / GetYdist / GetZdist (axis 0..2): slab width in the boxes
	int GetDist(int axis) const;
	void LoadDefault();               // default_clip_settings.dft (plane_mode)
	void SaveDefault();

Q_SIGNALS:
	void clipChanged();               // planes/rotations/mode changed; redraw
	void alignToViewRequested();      // kept for compatibility (Align to View is handled here)

protected:
	void enterEvent(QEnterEvent* e) override;
	void leaveEvent(QEvent* e) override;

private:
	// wx On{X,Y,Z}{1,2}ClipEdit: which 0 = low plane, 1 = high plane; value
	// in the box's units (voxels * factor)
	void EditPlane(int axis, int which, double value);
	void OnSliderMoved(int axis, int lo, int hi, bool lowMoved);
	void OnLinkToggled(int axis, bool on);
	void OnPlaneButton(int axis);     // wx OnYZClipBtn / OnXZClipBtn / OnXYClipBtn
	void OnClipReset();               // wx OnClipResetBtn
	void OnRotEdit(int axis, double v);   // wx On{X,Y,Z}RotEdit
	void OnRotSpin(int axis, int v);
	void OnRotReset();                // wx OnRotResetBtn
	void OnAlign();                   // wx OnSetZeroBtn ("Align to View")
	void OnFix(bool on);              // wx OnFixRotsCheck
	void OnHold(bool on);             // wx OnHoldPlanesCheck
	void OnPlaneMode(int mode);       // wx OnPlaneModesCombo
	void OnSyncChannels(bool on);     // wx OnLinkChannelsCheck
	void OnSyncGroup(bool on);        // wx OnLinkGroupCheck
	void OnSliderKey(int axis, int dir);  // wx OnSliderKeyDown ',' / '.'
public:
	// wx OnSliderRClick (which: 0 = the low / x1 slider, 1 = the high / x2
	// slider); public for the scripted tests
	void OnSliderRClick(int axis, int which);
	// testing: the low / high plane of an axis in voxels (as typed into the
	// boxes; a linked axis keeps its slab width)
	void SetClipRange(int axis, int lo, int hi) { EditPlane(axis, 0, lo * m_factor[axis]); EditPlane(axis, 1, hi * m_factor[axis]); }
private:

	std::vector<FLIVR::Plane*>* Planes() const;
	ClippingLayer* Layer() const;     // m_vd / m_md as ClippingLayer
	bool ViewSync() const;
	bool GroupSync() const;
	void CalcBoundingBoxDimensions(double& w, double& h, double& d);
	void CalcAndSetCombinedClippingPlanes();
	void SyncClippingPlanes();
	void UpdateCanvasDisplay();       // plane mode / target -> canvas
	void RefreshViews(bool full = true);
	void ReapplyViewRotations();      // wx vrv->SetRotations(GetRotations())
	void ReapplyClipRotations();      // wx vrv->Set/GetClippingPlaneRotations
	void SetEnabledAll(bool on);
	void SetRotationsEnabled(bool on);
	void SetRangeUi(int axis);
	QString FormatValue(int axis, double v) const;
	double RotBoxValue(int axis) const;

	RenderCanvas* m_canvas = nullptr;
	VolumeData* m_vd = nullptr;
	MeshData* m_md = nullptr;
	ClippingLayer* m_group = nullptr;
	int m_sel_type = -1;
	bool m_blocked = false;
	bool m_hold_planes = false;
	bool m_fix_rots = false;
	bool m_mouse_in = false;
	bool m_draw_clip = false;
	int m_plane_mode = 0;             // PM_NORMAL
	int m_res[3] = {512, 512, 512};   // slider maxima (bounding box, voxels)
	double m_factor[3] = {1.0, 1.0, 1.0};
	int m_lo[3] = {0, 0, 0};
	int m_hi[3] = {512, 512, 512};
	bool m_link[3] = {false, false, false};
	int m_sldr_dist[3] = {0, 0, 0};
	int m_spinLast[3] = {0, 0, 0};

	QCheckBox* m_syncChan = nullptr;
	QCheckBox* m_syncGroup = nullptr;
	QComboBox* m_mode = nullptr;
	QPushButton* m_clipReset = nullptr;
	DualSlider* m_dual[3] = {nullptr, nullptr, nullptr};
	QDoubleSpinBox* m_minBox[3] = {nullptr, nullptr, nullptr};
	QDoubleSpinBox* m_maxBox[3] = {nullptr, nullptr, nullptr};
	QCheckBox* m_linkChk[3] = {nullptr, nullptr, nullptr};
	QPushButton* m_planeBtn[3] = {nullptr, nullptr, nullptr};
	QSpinBox* m_distBox[3] = {nullptr, nullptr, nullptr};
	QCheckBox* m_fix = nullptr;
	QCheckBox* m_display = nullptr;
	QPushButton* m_align = nullptr;
	QPushButton* m_rotReset = nullptr;
	QDoubleSpinBox* m_rotBox[3] = {nullptr, nullptr, nullptr};
	QSpinBox* m_rotSpin[3] = {nullptr, nullptr, nullptr};
	QSlider* m_rotSldr[3] = {nullptr, nullptr, nullptr};
};

#endif
