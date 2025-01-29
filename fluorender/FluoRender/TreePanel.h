/*
For more information, please see: http://software.sci.utah.edu

The MIT License

Copyright (c) 2014 Scientific Computing and Imaging Institute,
University of Utah.


Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/
#include <wx/wx.h>
#include <wx/treectrl.h>
#include "compatibility.h"
#include "utility.h"
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <boost/property_tree/ptree.hpp>
#include <boost/optional.hpp>
#include "DLLExport.h"

#ifndef _TREEPANEL_H_
#define _TREEPANEL_H_

//tree icon
#define icon_change	1
#define icon_key	"None"

class VRenderView;
class VolumeData;
class MeshData;

//---------------------------------------

//-------------------------------------

//tree item data
class EXPORT_API LayerInfo : public wxTreeItemData
{
public:
	LayerInfo()
	{
		type = -1;
		id = -1;
		icon = -1;
	}

	int type;	//0-root; 1-view; 
				//2-volume data; 3-mesh data;
				//5-group; 6-mesh group
				//7-volume segment; 8-segment group
                //9-mesh segment; 10-mesh segment group

	int id;		//for volume segments

	int icon;
};

class VolVisState
{
public:
	bool disp;
	bool na_mode;
	std::map<int, int> label;
    std::unordered_set<int> v_sel_ids;
    std::map<int, bool> mesh_segs;

	VolVisState()
	{
		disp = true;
		na_mode = false;
	}

	~VolVisState()
	{

	}

	VolVisState(const VolVisState& copy)
	{
		disp = copy.disp;
		na_mode = copy.na_mode;
		label = copy.label;
        v_sel_ids = copy.v_sel_ids;
        mesh_segs = copy.mesh_segs;
	}

	VolVisState& operator=(const VolVisState& copy)
	{
		disp = copy.disp;
		na_mode = copy.na_mode;
		label = copy.label;
        v_sel_ids = copy.v_sel_ids;
        mesh_segs = copy.mesh_segs;

		return (*this);
	}
};


class EXPORT_API DataTreeCtrl: public wxTreeCtrl, Notifier
{
	enum
	{
		ID_TreeCtrl = wxID_HIGHEST+501,
		ID_ToggleDisp,
		ID_Rename,
		ID_Duplicate,
		ID_Save,
        ID_SaveSegVol,
        ID_ShowEntireVolume,
        ID_HideOutsideMask,
        ID_HideInsideMask,
		ID_ExportMask,
		ID_ImportMask,
		ID_BakeVolume,
		ID_Isolate,
		ID_ShowAll,
		ID_ExportMetadata,
		ID_ImportMetadata,
		ID_ShowAllSegChildren,
		ID_HideAllSegChildren,
		ID_ShowAllNamedSeg,
		ID_HideAllSeg,
		ID_DeleteAllSeg,
		ID_RemoveData,
		ID_CloseView,
		ID_ManipulateData,
		ID_AddDataGroup,
		ID_AddMeshGroup,
		ID_AddSegGroup,
        ID_AddSegments,
		ID_Expand,
		ID_Edit,
		ID_Info,
		ID_Trace,
		ID_NoiseCancelling,
		ID_Counting,
		ID_Colocalization,
		ID_Convert,
		ID_RandomizeColor,
		ID_ExportAllSegments,
		ID_FlipH,
		ID_FlipV,
		ID_ExportMeshMask,
        ID_ToggleNAMode,
        ID_SetSameColor,
        ID_ExpandItem
	};

public:
	DataTreeCtrl(wxWindow* frame,
		wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos=wxDefaultPosition,
		const wxSize& size=wxDefaultSize,
		long style=wxTR_HAS_BUTTONS|
		wxTR_TWIST_BUTTONS|
		wxTR_LINES_AT_ROOT|
		wxTR_NO_LINES|
		wxTR_FULL_ROW_HIGHLIGHT|
		wxTR_EDIT_LABELS);
	~DataTreeCtrl();

	//icon operations
	//change the color of the icon dual
	void ChangeIconColor(int i, wxColor c);
	void AppendIcon();
	void ClearIcons();
	int GetIconNum();

	//item operations
	//delete all
	void DeleteAll();
	void DeleteSelection();
	//traversal delete
	void TraversalDelete(wxTreeItemId item);
    void TraversalDeleteItem(wxTreeItemId item);
    void TraversalDeleteChildren(wxTreeItemId item);
	//root item
	wxTreeItemId AddRootItem(const wxString &text);
	void ExpandRootItem();
	//view item
	wxTreeItemId AddViewItem(const wxString &text);
	void SetViewItemImage(const wxTreeItemId& item, int image);
	//volume data item
	wxTreeItemId AddVolItem(wxTreeItemId par_item, const wxString &text);
	wxTreeItemId AddVolItem(wxTreeItemId par_item, VolumeData* vd);
	void UpdateVolItem(wxTreeItemId item, VolumeData* vd);
	void UpdateROITreeIcons(VolumeData* vd);
	void UpdateROITreeIconColor(VolumeData* vd);
	wxTreeItemId GetNextSibling_loop(wxTreeItemId item);
	wxTreeItemId FindTreeItem(wxString name);
	wxTreeItemId FindTreeItem(wxTreeItemId par_item, const wxString& name, bool roi_tree=false);
    wxTreeItemId FindTreeItemBySegmentID(wxTreeItemId par_item, int id);
	void SetVolItemImage(const wxTreeItemId item, int image);
	//mesh data item
	wxTreeItemId AddMeshItem(wxTreeItemId par_item, const wxString &text);
    wxTreeItemId AddMeshItem(wxTreeItemId par_item, MeshData* md);
    void UpdateMeshItem(wxTreeItemId item, MeshData* md);
    void UpdateROITreeIcons(MeshData* md);
    void UpdateROITreeIconColor(MeshData* md);
	void SetMeshItemImage(const wxTreeItemId item, int image);
	//annotation item
	wxTreeItemId AddAnnotationItem(wxTreeItemId par_item, const wxString &text);
	void SetAnnotationItemImage(const wxTreeItemId item, int image);
	//group item
	wxTreeItemId AddGroupItem(wxTreeItemId par_item, const wxString &text);
	void SetGroupItemImage(const wxTreeItemId item, int image);
	//mesh group item
	wxTreeItemId AddMGroupItem(wxTreeItemId par_item, const wxString &text);
	void SetMGroupItemImage(const wxTreeItemId item, int image);

	void UpdateSelection();
	wxString GetCurrentSel();
	int GetCurrentSelType();
	int TraversalSelect(wxTreeItemId item, wxString name);
	void Select(wxString view, wxString name);
	void SelectROI(VolumeData* vd, int id);
    void SelectROI(MeshData* vd, int id);

	//brush commands (from the panel)
	void BrushClear();
	void BrushCreate();
	void BrushCreateInv();

	VRenderView* GetCurrentView();

	void SetFix(bool fix) { m_fixed = fix; }
	bool isFixed() { return m_fixed; }

	void SaveExpState();
	void SaveExpState(wxTreeItemId node, const wxString& prefix=wxT(""));
	void LoadExpState(bool expand_newitem=true);
	void LoadExpState(wxTreeItemId node, const wxString& prefix=wxT(""), bool expand_newitem=true);
	std::string ExportExpState();
	void ImportExpState(const std::string &state);

	void TraversalExpand(wxTreeItemId item);
	wxTreeItemId GetParentVolItem(wxTreeItemId item);
    wxTreeItemId GetParentMeshItem(wxTreeItemId item);
	void ExpandDataTreeItem(wxString name, bool expand_children=false);
	void CollapseDataTreeItem(wxString name, bool collapse_children=false);

	wxString GetItemBaseText(wxTreeItemId itemid);

	void UndoVisibility();
	void RedoVisibility();
	void ClearVisHistory();
	void PushVisHistory();
    
    void ShowAllDatasets();
    void ShowAllDatasetsTraversal(wxTreeItemId item);

    void HideOtherDatasets();
	void HideOtherDatasets(wxString name);
	void HideOtherDatasets(wxTreeItemId item);
	void HideOtherDatasetsTraversal(wxTreeItemId item, wxTreeItemId self);
    
    void HideOtherVolumes();
	void HideOtherVolumes(wxString name);
	void HideOtherVolumes(wxTreeItemId item);
	void HideOtherVolumesTraversal(wxTreeItemId item, wxTreeItemId self);
	
    void HideSelectedItem();
    
    void GetSelectedItem(wxString &name, int &type);

	friend class TreePanel;

private:
	wxWindow* m_frame;

	std::unordered_map<wxString, VolVisState> m_vtmp;
	std::vector<std::unordered_map<wxString, VolVisState>> m_v_undo, m_v_redo;
    
    std::unordered_map<wxString, VolVisState> m_mtmp;
    std::vector<std::unordered_map<wxString, VolVisState>> m_m_undo, m_m_redo;

	//drag
	wxTreeItemId m_drag_item;
	wxTreeItemId m_drag_nb_item;
	int m_insert_mode;
	//fix current selection
	bool m_fixed;
	//remember the pos
	int m_scroll_pos;
	int m_rename;

	static bool m_md_save_indv;
	static int m_save_scale_level;
	static bool m_crop_output_image;

	std::unordered_map<wxString, bool> m_exp_state;

private:

	void GetVisHistoryTraversal(wxTreeItemId item);
	void SetVisHistoryTraversal(wxTreeItemId item);

	static wxWindow* CreateExtraControl(wxWindow* parent);
	void OnCh1Check(wxCommandEvent &event);
	void OnCh2Check(wxCommandEvent& event);
	void OnTxt1Change(wxCommandEvent& event);

	//change the color of just one icon of the dual,
	//either enable(type=0), or disable(type=1)
	void ChangeIconColor(int which, wxColor c, int type);

	void UpdateROITreeIcons(wxTreeItemId par_item, VolumeData* vd);
	void UpdateROITreeIconColor(wxTreeItemId par_item, VolumeData* vd);
    void UpdateROITreeIcons(wxTreeItemId par_item, MeshData* md);
    void UpdateROITreeIconColor(wxTreeItemId par_item, MeshData* md);
	void BuildROITree(wxTreeItemId par_item, const boost::property_tree::wptree& tree, VolumeData *vd);
    void BuildROITree(wxTreeItemId par_item, const boost::property_tree::wptree& tree, MeshData *md);
    void InitROITree(wxTreeItemId par_item, const boost::property_tree::wptree& tree, MeshData *md);
    void AddChildrenROITree(wxTreeItemId par_item, const boost::property_tree::wptree& tree, MeshData *md);
    void ExpandROITree(wxTreeItemId item);

	void OnContextMenu(wxContextMenuEvent &event );

	void OnToggleDisp(wxCommandEvent& event);
	void OnDuplicate(wxCommandEvent& event);
	void OnSave(wxCommandEvent& event);
    void OnSaveSegmentedVolume(wxCommandEvent& event);
    void OnShowEntireVolume(wxCommandEvent& event);
    void OnHideOutsideOfMask(wxCommandEvent& event);
    void OnHideInsideOfMask(wxCommandEvent& event);
	void OnExportMask(wxCommandEvent& event);
	void OnImportMask(wxCommandEvent& event);
	void OnBakeVolume(wxCommandEvent& event);
	void OnRenameMenu(wxCommandEvent& event);
	void OnIsolate(wxCommandEvent& event);
	void OnShowAll(wxCommandEvent& event);
	void OnExportMetadata(wxCommandEvent& event);
	void OnImportMetadata(wxCommandEvent& event);
	void OnShowAllSegChildren(wxCommandEvent& event);
	void OnHideAllSegChildren(wxCommandEvent& event);
	void OnShowAllNamedSeg(wxCommandEvent& event);
	void OnHideAllSeg(wxCommandEvent& event);
	void OnDeleteAllSeg(wxCommandEvent& event);
	void OnRemoveData(wxCommandEvent& event);
	void OnCloseView(wxCommandEvent& event);
	void OnManipulateData(wxCommandEvent& event);
	void OnAddDataGroup(wxCommandEvent& event);
	void OnAddMeshGroup(wxCommandEvent& event);
	void OnAddSegGroup(wxCommandEvent& event);
	void OnExpand(wxCommandEvent& event);
	void OnEdit(wxCommandEvent& event);
	void OnInfo(wxCommandEvent& event);
	void OnTrace(wxCommandEvent& event);
	void OnNoiseCancelling(wxCommandEvent& event);
	void OnCounting(wxCommandEvent& event);
	void OnColocalization(wxCommandEvent& event);
	void OnConvert(wxCommandEvent& event);
	void OnRandomizeColor(wxCommandEvent& event);
	void OnExportAllSegments(wxCommandEvent& event);
	void OnFlipH(wxCommandEvent& event);
	void OnFlipV(wxCommandEvent& event);
	void OnExportMeshMask(wxCommandEvent& event);
    void OnToggleNAMode(wxCommandEvent& event);
    void SetSameColorToAllDatasetsInGroup(wxCommandEvent& event);
    void OnAddSegments(wxCommandEvent& event);

	void OnSelChanged(wxTreeEvent& event);
	void OnSelChanging(wxTreeEvent& event);
	void OnDeleting(wxTreeEvent& event);
	void OnAct(wxTreeEvent &event);
	void OnBeginDrag(wxTreeEvent& event);
	void OnEndDrag(wxTreeEvent& event);
	void OnRename(wxTreeEvent& event);
	void OnRenamed(wxTreeEvent& event);
    void OnExpandItem(wxTreeEvent& event);

	void OnDragging(wxMouseEvent& event);

	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);

	DECLARE_EVENT_TABLE()
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

class EXPORT_API TreePanel : public wxPanel, Observer
{
public:
	enum
	{
		ID_ToggleView = wxID_HIGHEST+551,
		ID_Save,
		ID_BakeVolume,
		ID_RemoveData,
		ID_BrushAppend,
		ID_BrushDesel,
		ID_BrushDiffuse,
		ID_BrushErase,
		ID_BrushClear,
		ID_BrushCreate
	};

	TreePanel(wxWindow* frame,
		wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0,
		const wxString& name = "TreePanel");
	~TreePanel();

	DataTreeCtrl* GetTreeCtrl();

	//icon operations
	void ChangeIconColor(int i, wxColor c);
	void AppendIcon();
	void ClearIcons();
	int GetIconNum();

	//item operations
	void SelectItem(wxTreeItemId item);
	void Expand(wxTreeItemId item);
	void ExpandAll();
	void DeleteAll();
	void TraversalDelete(wxTreeItemId item);
	wxTreeItemId AddRootItem(const wxString &text);
	void ExpandRootItem();
	wxTreeItemId AddViewItem(const wxString &text);
	void SetViewItemImage(const wxTreeItemId& item, int image);
	wxTreeItemId AddVolItem(wxTreeItemId par_item, const wxString &text);
	wxTreeItemId AddVolItem(wxTreeItemId par_item, VolumeData* vd);
	void UpdateROITreeIcons(VolumeData* vd);
	void UpdateROITreeIconColor(VolumeData* vd);
	void UpdateVolItem(wxTreeItemId item, VolumeData* vd);
	wxTreeItemId FindTreeItem(wxTreeItemId par_item, wxString name);
	wxTreeItemId FindTreeItem(wxString name);
	void SetVolItemImage(const wxTreeItemId item, int image);
	wxTreeItemId AddMeshItem(wxTreeItemId par_item, const wxString &text);
    wxTreeItemId AddMeshItem(wxTreeItemId par_item, MeshData* md);
    void UpdateROITreeIcons(MeshData* md);
    void UpdateROITreeIconColor(MeshData* md);
    void UpdateMeshItem(wxTreeItemId item, MeshData* md);
	void SetMeshItemImage(const wxTreeItemId item, int image);
	wxTreeItemId AddAnnotationItem(wxTreeItemId par_item, const wxString &text);
	void SetAnnotationItemImage(const wxTreeItemId item, int image);
	wxTreeItemId AddGroupItem(wxTreeItemId par_item, const wxString &text);
	void SetGroupItemImage(const wxTreeItemId item, int image);
	wxTreeItemId AddMGroupItem(wxTreeItemId par_item, const wxString &text);
	void SetMGroupItemImage(const wxTreeItemId item, int image);
	void SetItemName(const wxTreeItemId item, const wxString& name);

	//seelction
	void UpdateSelection();
	wxString GetCurrentSel();
	void Select(wxString view, wxString name);
	void SelectROI(VolumeData* vd, int id);
    void SelectROI(MeshData* vd, int id);
    
	//set the brush icon down
	void SelectBrush(int id);
	int GetBrushSelected();
	//control from outside
	void BrushAppend();
	void BrushDiffuse();
	void BrushDesel();
	void BrushClear();
	void BrushErase();
	void BrushCreate();
	void BrushSolid(bool state);

	VRenderView* GetCurrentView();

	void SetFix(bool fix) { if (m_datatree) m_datatree->SetFix(fix); }
	bool isFixed() { return m_datatree ? m_datatree->isFixed() : false; }

	void SaveExpState();
	void LoadExpState(bool expand_newitem = true);
	std::string ExportExpState();
	void ImportExpState(const std::string &state);

	wxTreeItemId GetParentVolItem(wxTreeItemId item);
	wxTreeItemId GetNextSibling_loop(wxTreeItemId item);
	void ExpandDataTreeItem(wxString name, bool expand_children=false);
	void CollapseDataTreeItem(wxString name, bool collapse_children=false);

	void doAction(ActionInfo *info);

	void UndoVisibility();
	void RedoVisibility();
	void ClearVisHistory();
	void PushVisHistory();
    void ShowAllDatasets();
    void HideOtherDatasets();
	void HideOtherDatasets(wxString name);
    void HideOtherVolumes();
	void HideOtherVolumes(wxString name);
	void HideSelectedItem();
    
    void GetSelectedItem(wxString &name, int &type);

private:
	wxWindow* m_frame;
	DataTreeCtrl* m_datatree;
	wxToolBar *m_toolbar;

	void OnSave(wxCommandEvent& event);
	void OnBakeVolume(wxCommandEvent& event);
	void OnToggleView(wxCommandEvent& event);
	void OnRemoveData(wxCommandEvent& event);
	//brush commands
	void OnBrushAppend(wxCommandEvent& event);
	void OnBrushDesel(wxCommandEvent& event);
	void OnBrushDiffuse(wxCommandEvent& event);
	void OnBrushErase(wxCommandEvent& event);
	void OnBrushClear(wxCommandEvent& event);
	void OnBrushCreate(wxCommandEvent& event);

	DECLARE_EVENT_TABLE()
};

#endif//_TREEPANEL_H_
