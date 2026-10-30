#ifndef VVD_QT_TREEPANEL_H
#define VVD_QT_TREEPANEL_H

// Workspace tree: Qt port of the wx TreePanel / DataTreeCtrl (TreePanel.cpp)
// and of the tree building in VRenderFrame (UpdateTree, UpdateTreeIcons,
// UpdateTreeColors).
//
// Structure (as wx): "Active Datasets" -> "Render View:1" -> the view's
// layers in layer order: volume groups (-> volumes -> segment trees of
// volumes in colormap-index mode), meshes, mesh groups (-> meshes),
// annotations. Every item but the root carries the wx light-bulb icon
// (tick = shown, cross = hidden) tinted with the data colour (white for
// views and groups). The tree is single-selection like wx.
//
// Interaction (as wx):
//   * double-click / Enter / "Toggle View" button / "Toggle Visibility":
//     toggle visibility (wx OnAct; with Ctrl held: randomize colours)
//   * right-click: the wx context menu of the item type (OnContextMenu)
//   * Delete / Backspace: delete the selection (DeleteSelection)
//   * "Rename" menu entry: in-place label edit (OnRenameMenu / OnRenamed)
//   * drag & drop: DataTreeCtrl::OnEndDrag -- reorder within the view or a
//     group, move into / out of / between groups; a volume dropped on the
//     view item or on empty space leaves its group and, through
//     OrganizeLayers in UpdateTree, lands in the last empty group or a new
//     one (that is how wx makes a new group by drag & drop)
//   * brush tool buttons (Highlight / Diffuse / Reset fix the tree while
//     painting, wx m_fixed)

#include <QWidget>
#include <QString>
#include <QTreeWidget>
#include <map>
#include <set>
#include <unordered_set>
#include <functional>
#include <vector>

#include "Core/VVDString.h"

class QAction;
class QMenu;
class QTreeWidgetItem;
class DataManager;
class RenderCanvas;
class VolumeData;
class MeshData;
class DataGroup;
class MeshGroup;
class Annotations;
class TreePanel;

// QTreeWidget with the wx DataTreeCtrl drag & drop: the drag only carries
// the item, the drop is handed to TreePanel::OnEndDrag (the layer model is
// changed and the tree rebuilt from it; Qt never moves items itself).
class DataTreeWidget : public QTreeWidget
{
	Q_OBJECT
public:
	explicit DataTreeWidget(TreePanel* panel, QWidget* parent = nullptr);
	QTreeWidgetItem* DragItem() const { return m_drag_item; }
	void SetDragItem(QTreeWidgetItem* item) { m_drag_item = item; }   // testing
protected:
	void startDrag(Qt::DropActions supportedActions) override;
	void dragEnterEvent(QDragEnterEvent* e) override;
	void dragMoveEvent(QDragMoveEvent* e) override;
	void dropEvent(QDropEvent* e) override;
	void keyPressEvent(QKeyEvent* e) override;
private:
	TreePanel* m_panel = nullptr;
	QTreeWidgetItem* m_drag_item = nullptr;
};

class TreePanel : public QWidget
{
	Q_OBJECT
public:
	explicit TreePanel(QWidget* parent = nullptr);

	// item types (wx LayerInfo::type)
	enum { TypeRoot = 0, TypeView = 1, TypeVolume = 2, TypeMesh = 3, TypeAnnotations = 4,
		TypeGroup = 5, TypeMGroup = 6, TypeSegment = 7, TypeSegGroup = 8,
		TypeMeshSeg = 9, TypeMeshSegGroup = 10 };

	void SetDataManager(DataManager* mgr) { m_mgr = mgr; }
	// the render views, in order (wx VRenderFrame::GetViewList): one
	// "Render View:<n>" item each
	void SetCanvases(const std::vector<RenderCanvas*>& canvases) { m_canvases = canvases; }
	void SetCanvas(RenderCanvas* canvas) { m_canvases = { canvas }; }
	// the view an item belongs to (wx GetItemParent up to the view item;
	// null item / root: the first view)
	RenderCanvas* CanvasOf(const QTreeWidgetItem* item) const;
	RenderCanvas* CurCanvas() const;
	// select the item of a view (wx: a click on "Render View:<n>")
	bool SelectView(RenderCanvas* canvas);
	// wx VRenderFrame::GetCompression (Bake / Save 3D Painted Region)
	void SetCompressionGetter(std::function<bool()> f) { m_get_compress = std::move(f); }

	// wx VRenderFrame::UpdateTree(name, type): OrganizeLayers, rebuild from
	// the canvas layer list (expansion state kept, new items expanded as
	// wx), then select the item called name of the given type (-1 any). An
	// empty name keeps the current selection.
	void UpdateTree(const QString& name = QString(), int type = -1);
	// wx UpdateTreeIcons / UpdateTreeColors: visibility bulbs / colours
	void UpdateTreeIcons();
	void UpdateTreeColors();
	void SelectVolume(VolumeData* vd);
	// the item of vd in the view of canvas (any view when null)
	void SelectVolume(VolumeData* vd, RenderCanvas* canvas);
	VolumeData* CurrentVolume() const;
	void SelectMesh(MeshData* md);
	MeshData* CurrentMesh() const;
	// testing (VVD_QT_FILEDLG_RUN): the selected item's save actions
	// "savevolume" | "savesegmented" | "bake" | "exportmeta" (the dialogs'
	// own testing aids, FileDialogs.h, choose the file)
	bool RunFileAction(const QString& action);
	// wx GetCurrentSel / GetCurrentSelType
	QString GetCurrentSel() const;
	int GetCurrentSelType() const;
	// wx Select(view, name): select the item called name (any type)
	bool SelectByName(const QString& name, int type = -1);
	// Set the brush tool buttons' check state without emitting a request
	// (wx TreePanel::SelectBrush). paint_mode: 2 Highlight, 4 Diffuse,
	// 3 Reset, anything else: none.
	void SelectBrush(int paint_mode);
	// wx TreePanel::GetBrushSelected: 2 / 4 / 3, or 0 when none is down
	int GetBrushSelected() const;
	// wx /tree_panel/exp_states (ExportExpState / ImportExpState)
	QString ExportExpState();
	void ImportExpState(const QString& state);

	// ---- wx DataTreeCtrl actions (also used by tests)
	void OnAct(bool ctrl = false);           // toggle visibility / randomize colours
	void DeleteSelection();
	// wx OnEndDrag: src dropped onto dst (null: empty space); drop_y is the
	// drop position in viewport coordinates (before / after dst)
	void OnEndDrag(QTreeWidgetItem* src, QTreeWidgetItem* dst, int drop_y);
	// the context menu of an item (wx OnContextMenu); the caller owns it
	QMenu* BuildContextMenu(QTreeWidgetItem* item);
	QTreeWidget* Tree() const { return m_tree; }
	// testing: a mouse drag of src onto dst (null: the empty space below the
	// items), dropped on the lower (after) or upper half of dst, delivered
	// as drag-enter / drag-move / drop events to the tree's viewport
	bool SimulateDrag(QTreeWidgetItem* src, QTreeWidgetItem* dst, bool after);
	QTreeWidgetItem* FindItem(const QString& name, int type = -1) const;
	bool IsFixed() const { return m_fixed; }

	QTreeWidgetItem* ParentVolItem(QTreeWidgetItem* item) const;   // wx GetParentVolItem
	QTreeWidgetItem* ParentMeshItem(QTreeWidgetItem* item) const;  // wx GetParentMeshItem
	// wx DataTreeCtrl::SelectROI: select the segment item id of vd
	void SelectROI(VolumeData* vd, int id);
	// wx DataTreeCtrl::SelectROI(MeshData*, id): select the segment item id
	// of a tree mesh (SWC), loading the lazily built segment items on the way
	void SelectROI(MeshData* md, int id);
	// wx MPropView sync options read by OnAct on mesh segments
	// (GetSyncGroup / GetSyncName)
	void SetMeshSyncGetter(std::function<void(bool& group, bool& name)> f) { m_mesh_sync = std::move(f); }

	// ---- wx DataTreeCtrl visibility history (render view context menu
	// "Undo visibility" / "Redo visibility"): the shown state of every view,
	// volume (+ selected / NA segments), mesh (+ SWC segments), annotation and
	// group; at most 10 undo steps
	void PushVisHistory();
	void UndoVisibility();
	void RedoVisibility();
	void ClearVisHistory();
	int VisUndoCount() const { return (int)m_v_undo.size(); }
	int VisRedoCount() const { return (int)m_v_redo.size(); }
	// wx DataTreeCtrl::ShowAllDatasets / HideOtherDatasets / HideOtherVolumes
	// / HideSelectedItem (render view context menu "Show all" / "Show only
	// selected" / "Hide selected"), for the selected item
	void ShowAllDatasets();
	void HideOtherDatasets(QTreeWidgetItem* item = nullptr);
	void HideOtherVolumes(QTreeWidgetItem* item = nullptr);
	void HideSelectedItem();
	static int ItemType(const QTreeWidgetItem* item);
	static QString ItemName(const QTreeWidgetItem* item);
	// the annotations of a selectionChanged(4, ...) (valid during the signal)
	Annotations* SelectedAnnotations() const { return m_sel_ann; }
	// select an annotations item (testing aid; wx: a click on the item)
	bool SelectAnnotations(Annotations* ann);

Q_SIGNALS:
	// the tree was rebuilt / its icons or colours were updated (wx
	// UpdateTree / UpdateTreeIcons / UpdateTreeColors -> PluginManager::OnTreeUpdate)
	void treeUpdated();
	// wx UpdateSelection -> VRenderFrame::OnSelection(type, vrv, group, vd, md):
	// 0 root, 1 view, 2 volume (group = its DataGroup or null), 3 mesh,
	// 4 annotations, 5 volume group, 6 mesh group (wx: OnSelection(0)).
	// Segment items select their volume (type 2).
	void selectionChanged(int type, DataGroup* group, VolumeData* vd, MeshData* md);
	void volumeRemoveRequested(VolumeData* vd);
	void meshRemoveRequested(MeshData* md);
	// the volumes / meshes of the group were removed from the view: delete
	// them from the data manager (wx RemoveGroup does both)
	void groupRemoved(const std::vector<VolumeData*>& vols, const std::vector<MeshData*>& meshes);
	// a volume's segment display changed (wx GetPropView()->UpdateUIsROI())
	void roiDisplayChanged();
	// annotation visibility changed (wx GetMeasureDlg()->UpdateList())
	void annotationsChanged();
	void annotationsRemoveRequested(Annotations* ann);
	// wx RefreshVRenderViews (tree: also UpdateTreeColors)
	void refreshRequested();
	// wx OnEdit / OnInfo / OnNoiseCancelling / OnCounting / OnColocalization /
	// OnConvert: 0 Analyze (paint tool), 1 Information, 2 Noise Reduction,
	// 3 Counting and Volume, 4 Colocalization, 5 Convert
	void toolRequested(int which);
	// wx OnManipulateData (OnSelection(6, .., md))
	void manipulateRequested(MeshData* md);
	// brush toolbar (wx TreePanel::OnBrushAppend/Diffuse/Desel/Clear/Erase/
	// Create). Highlight / Diffuse / Reset are toggles: brushModeRequested(2 /
	// 4 / 3) when pressed down, brushModeRequested(0) when released
	// (wx BrushAppend etc.: view int mode 2 + paint mode, or int mode 1).
	void brushModeRequested(int paint_mode);
	void brushClearRequested();     // "Reset All" (wx BrushClear)
	void brushEraseRequested();     // "Erase" (wx BrushErase -> Calculate(6))
	void brushExtractRequested();   // "Extract" (wx BrushCreate -> Calculate(5))
	// the "Close" entry of a view other than the first (wx OnCloseView)
	void closeViewRequested(RenderCanvas* canvas);
	// "Save Color Depth MIP..." of a volume (Qt only)
	void colorDepthMipRequested(VolumeData* vd);

private Q_SLOTS:
	void OnItemChanged(QTreeWidgetItem* item, int column);
	void OnCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
	void OnContextMenu(const QPoint& pos);

private:
	friend class DataTreeWidget;
	Annotations* m_sel_ann = nullptr;
	VolumeData* VolumeOf(const QTreeWidgetItem* item) const;
	MeshData* MeshOf(const QTreeWidgetItem* item) const;
	void UpdateSelection();                  // wx UpdateSelection
	void UpdateToolbar();                    // wx TreePanel::doAction
	QIcon BulbIcon(const QColor& c, bool on) const;
	void SetItemBulb(QTreeWidgetItem* item, const QColor& c, bool on);
	QTreeWidgetItem* AddItem(QTreeWidgetItem* parent, const QString& text, int type, void* ptr, int id = 0);
	void BuildROITree(QTreeWidgetItem* par_item, const void* tree, VolumeData* vd);
	// SWC segment trees, built on demand as wx (InitROITree: a "dummy" child,
	// AddChildrenROITree when the item is expanded)
	void InitMeshROITree(QTreeWidgetItem* par_item, MeshData* md);
	void AddChildrenROITree(QTreeWidgetItem* par_item, const void* tree, MeshData* md);
	void ExpandROITree(QTreeWidgetItem* item);
	QTreeWidgetItem* FindItemBySegmentID(QTreeWidgetItem* par_item, int id) const;
	struct VisState
	{
		bool disp = true;
		bool na_mode = false;
		std::map<int, int> label;
		std::unordered_set<int> v_sel_ids;
		std::map<int, bool> mesh_segs;
	};
	void GetVisHistoryTraversal(QTreeWidgetItem* item);
	void SetVisHistoryTraversal(QTreeWidgetItem* item);
	void ShowAllDatasetsTraversal(QTreeWidgetItem* item);
	void HideOtherDatasetsTraversal(QTreeWidgetItem* item, QTreeWidgetItem* self, bool volumes_only);
	std::map<QString, VisState> m_vtmp;
	std::vector<std::map<QString, VisState>> m_v_undo, m_v_redo;
	std::function<void(bool&, bool&)> m_mesh_sync;
	void SaveExpState();
	void RestoreScroll(int value);   // keep the list position across rebuilds
	void SaveExpState(QTreeWidgetItem* item, const QString& prefix);
	void LoadExpState(QTreeWidgetItem* item, const QString& prefix, bool expand_newitem);
	static void TraversalExpand(QTreeWidgetItem* item);
	void RefreshViews();

	// context menu actions (wx DataTreeCtrl::On*)
	void OnToggleDisp() { OnAct(false); }
	void OnRenameMenu();
	void OnRenamed(QTreeWidgetItem* item, const QString& prev_name, const QString& name);
	void OnDuplicate();
	void OnSave();
	void OnSaveSegmentedVolume();
	void OnSaveColorDepthMIP();
	void OnSetMaskHideMode(int mode);
	void OnExportMask();
	void OnImportMask();
	void OnBakeVolume();
	void OnIsolate();
	void OnShowAll();
	void OnFlip(bool horizontal);
	void OnAddSegments();
	void OnExportMetadata();
	void OnAddSegGroup();
	void OnShowAllSegChildren(bool show);
	void OnShowAllNamedSeg();
	void OnHideAllSeg();
	void OnDeleteAllSeg();
	void OnRandomizeColor();
	void OnSetSameColor();
	void OnAddDataGroup();
	void OnAddMeshGroup();
	void OnCloseView();          // wx OnCloseView -> VRenderFrame::DeleteVRenderView
	void MoveToView(QTreeWidgetItem* src_item, QTreeWidgetItem* dst_item);   // Qt: drop onto another view
	void OnExpand();
	void OnExportAllSegments();

	bool Compression() const { return m_get_compress ? m_get_compress() : true; }
	std::function<bool()> m_get_compress;
	DataTreeWidget* m_tree = nullptr;
	DataManager* m_mgr = nullptr;
	std::vector<RenderCanvas*> m_canvases;
	bool m_updating = false;
	bool m_fixed = false;              // wx m_fixed (brush painting)
	QTreeWidgetItem* m_rename_item = nullptr;
	QString m_rename_prev;
	std::map<QString, bool> m_exp_state;   // wx m_exp_state
	bool m_exp_loaded = false;
	QAction* m_save_act = nullptr;
	QAction* m_bake_act = nullptr;
	QAction* m_brush_append = nullptr;
	QAction* m_brush_diffuse = nullptr;
	QAction* m_brush_desel = nullptr;
	void OnBrushToggle(QAction* act, int paint_mode, bool checked);
};

#endif // VVD_QT_TREEPANEL_H
