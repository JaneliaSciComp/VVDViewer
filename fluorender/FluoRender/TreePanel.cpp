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
#include "TreePanel.h"
#include "VRenderFrame.h"
#include "tick.xpm"
#include "cross.xpm"
#include "Formats/png_resource.h"
#include "Formats/brkxml_writer.h"
#include <boost/lexical_cast.hpp>

#include <wx/valnum.h>

//resources
#include "img/icons.h"

using boost::property_tree::wptree;

BEGIN_EVENT_TABLE(DataTreeCtrl, wxTreeCtrl)
	EVT_CONTEXT_MENU(DataTreeCtrl::OnContextMenu)
	EVT_MENU(ID_ToggleDisp, DataTreeCtrl::OnToggleDisp)
	EVT_MENU(ID_Rename, DataTreeCtrl::OnRenameMenu)
	EVT_MENU(ID_Duplicate, DataTreeCtrl::OnDuplicate)
	EVT_MENU(ID_Save, DataTreeCtrl::OnSave)
    EVT_MENU(ID_SaveSegVol, DataTreeCtrl::OnSaveSegmentedVolume)
    EVT_MENU(ID_ShowEntireVolume, DataTreeCtrl::OnShowEntireVolume)
    EVT_MENU(ID_HideOutsideMask, DataTreeCtrl::OnHideOutsideOfMask)
    EVT_MENU(ID_HideInsideMask, DataTreeCtrl::OnHideInsideOfMask)
	EVT_MENU(ID_ExportMask, DataTreeCtrl::OnExportMask)
	EVT_MENU(ID_ImportMask, DataTreeCtrl::OnImportMask)
	EVT_MENU(ID_BakeVolume, DataTreeCtrl::OnBakeVolume)
	EVT_MENU(ID_Isolate, DataTreeCtrl::OnIsolate)
	EVT_MENU(ID_ShowAll, DataTreeCtrl::OnShowAll)
    EVT_MENU(ID_AddSegments, DataTreeCtrl::OnAddSegments)
	EVT_MENU(ID_ExportMetadata, DataTreeCtrl::OnExportMetadata)
	EVT_MENU(ID_ImportMetadata, DataTreeCtrl::OnImportMetadata)
	EVT_MENU(ID_ShowAllSegChildren, DataTreeCtrl::OnShowAllSegChildren)
	EVT_MENU(ID_HideAllSegChildren, DataTreeCtrl::OnHideAllSegChildren)
	EVT_MENU(ID_ShowAllNamedSeg, DataTreeCtrl::OnShowAllNamedSeg)
	EVT_MENU(ID_HideAllSeg, DataTreeCtrl::OnHideAllSeg)
	EVT_MENU(ID_DeleteAllSeg, DataTreeCtrl::OnDeleteAllSeg)
	EVT_MENU(ID_RemoveData, DataTreeCtrl::OnRemoveData)
	EVT_MENU(ID_CloseView, DataTreeCtrl::OnCloseView)
	EVT_MENU(ID_ManipulateData, DataTreeCtrl::OnManipulateData)
	EVT_MENU(ID_AddDataGroup, DataTreeCtrl::OnAddDataGroup)
	EVT_MENU(ID_AddMeshGroup, DataTreeCtrl::OnAddMeshGroup)
	EVT_MENU(ID_AddSegGroup, DataTreeCtrl::OnAddSegGroup)
	EVT_MENU(ID_Expand, DataTreeCtrl::OnExpand)
	EVT_MENU(ID_Edit, DataTreeCtrl::OnEdit)
	EVT_MENU(ID_Info, DataTreeCtrl::OnInfo)
	EVT_MENU(ID_Trace, DataTreeCtrl::OnTrace)
	EVT_MENU(ID_NoiseCancelling, DataTreeCtrl::OnNoiseCancelling)
	EVT_MENU(ID_Counting, DataTreeCtrl::OnCounting)
	EVT_MENU(ID_Colocalization, DataTreeCtrl::OnColocalization)
	EVT_MENU(ID_Convert, DataTreeCtrl::OnConvert)
	EVT_MENU(ID_RandomizeColor, DataTreeCtrl::OnRandomizeColor)
	EVT_MENU(ID_ExportAllSegments, DataTreeCtrl::OnExportAllSegments)
	EVT_MENU(ID_FlipH, DataTreeCtrl::OnFlipH)
	EVT_MENU(ID_FlipV, DataTreeCtrl::OnFlipV)
	EVT_MENU(ID_ExportMeshMask, DataTreeCtrl::OnExportMeshMask)
    EVT_MENU(ID_ToggleNAMode, DataTreeCtrl::OnToggleNAMode)
    EVT_MENU(ID_SetSameColor, DataTreeCtrl::SetSameColorToAllDatasetsInGroup)
	EVT_TREE_SEL_CHANGED(wxID_ANY, DataTreeCtrl::OnSelChanged)
	EVT_TREE_SEL_CHANGING(wxID_ANY, DataTreeCtrl::OnSelChanging)
	EVT_TREE_DELETE_ITEM(wxID_ANY, DataTreeCtrl::OnDeleting)
	EVT_TREE_ITEM_ACTIVATED(wxID_ANY, DataTreeCtrl::OnAct)
	EVT_TREE_BEGIN_DRAG(wxID_ANY, DataTreeCtrl::OnBeginDrag)
	EVT_TREE_END_DRAG(wxID_ANY, DataTreeCtrl::OnEndDrag)
    EVT_TREE_ITEM_EXPANDING(wxID_ANY, DataTreeCtrl::OnExpandItem)
	EVT_KEY_DOWN(DataTreeCtrl::OnKeyDown)
	EVT_KEY_UP(DataTreeCtrl::OnKeyUp)
	EVT_TREE_BEGIN_LABEL_EDIT(wxID_ANY, DataTreeCtrl::OnRename)
	EVT_TREE_END_LABEL_EDIT(wxID_ANY, DataTreeCtrl::OnRenamed)
END_EVENT_TABLE()

bool DataTreeCtrl::m_md_save_indv = false;
int DataTreeCtrl::m_save_scale_level = 0;

DataTreeCtrl::DataTreeCtrl(
	wxWindow* frame,
	wxWindow* parent,
	wxWindowID id,
	const wxPoint& pos,
	const wxSize& size,
	long style) :
wxTreeCtrl(parent, id, pos, size, style),
	m_frame(frame),
	m_fixed(false),
	m_scroll_pos(-1),
	m_rename(false)
{
	wxImageList *images = new wxImageList(16, 16, true);
	wxIcon icons[2];
	icons[0] = wxIcon(cross_xpm);
	icons[1] = wxIcon(tick_xpm);
	images->Add(icons[0]);
	images->Add(icons[1]);
	AssignImageList(images);
	SetDoubleBuffered(true); 
}

DataTreeCtrl::~DataTreeCtrl()
{
	TraversalDelete(GetRootItem());
}

wxString DataTreeCtrl::GetItemBaseText(wxTreeItemId itemid)
{
	wxString result;

	if (itemid.IsOk())
	{
		wxString raw_name;
		raw_name = GetItemText(itemid);
		result = raw_name.BeforeFirst(wxT('|'));
		if (result != raw_name)
		{
			size_t t = result.find_last_not_of(wxT(' '));
			if (t != wxString::npos) result = result.SubString(0, t);
		}
	}

	return result;
}

//delete
void DataTreeCtrl::DeleteAll()
{
	if (!IsEmpty())
	{
		//safe deletion, may be unnecessary
		TraversalDelete(GetRootItem());
		DeleteAllItems();
	}
}

//traversal delete
void DataTreeCtrl::TraversalDelete(wxTreeItemId item)
{
	wxTreeItemIdValue cookie;
	wxTreeItemId child_item = GetFirstChild(item, cookie);
	if (child_item.IsOk())
		TraversalDelete(child_item);
	child_item = GetNextChild(item, cookie);
	while (child_item.IsOk())
	{
		TraversalDelete(child_item);
		child_item = GetNextChild(item, cookie);
	}

	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
	if (item_data)
        delete item_data;
	SetItemData(item, 0);
}

void DataTreeCtrl::TraversalDeleteItem(wxTreeItemId item)
{
    wxTreeItemIdValue cookie;
    wxTreeItemId child_item = GetFirstChild(item, cookie);
    if (child_item.IsOk())
        TraversalDeleteItem(child_item);
    child_item = GetNextChild(item, cookie);
    while (child_item.IsOk())
    {
        TraversalDeleteItem(child_item);
        child_item = GetNextChild(item, cookie);
    }

    LayerInfo* item_data = (LayerInfo*)GetItemData(item);
    if (item_data)
        delete item_data;
    SetItemData(item, 0);
    Delete(item);
}

void DataTreeCtrl::TraversalDeleteChildren(wxTreeItemId item)
{
    wxTreeItemIdValue cookie;
    wxTreeItemId child_item = GetFirstChild(item, cookie);
    if (child_item.IsOk())
        TraversalDelete(child_item);
    child_item = GetNextChild(item, cookie);
    while (child_item.IsOk())
    {
        TraversalDelete(child_item);
        child_item = GetNextChild(item, cookie);
    }
    DeleteChildren(item);
}

void DataTreeCtrl::DeleteSelection()
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (sel_item.IsOk() && vr_frame)
	{
		MeshData* clmd = NULL;
		VolumeData* clvd = NULL;
		wxString clmdname, clvdname;
		int cl_sel_type = vr_frame->GetClippingView()->GetSelType();
		if (cl_sel_type == 2)
		{
			clvd = vr_frame->GetClippingView()->GetVolumeData();
			if (clvd)
				clvdname = clvd->GetName();
		}
		else if (cl_sel_type == 3)
		{
			clmd = vr_frame->GetClippingView()->GetMeshData();
			if (clmd)
				clmdname = clmd->GetName();
		}

		wxString name_data = GetItemBaseText(sel_item);
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (!item_data)
			return;
		wxTreeItemId par_item = GetItemParent(sel_item);
		if (!par_item.IsOk())
			return;
		wxString par_name = GetItemBaseText(par_item);

		wxTreeItemId next_item = GetNextSibling(sel_item);
		if (!next_item.IsOk())
			GetPrevSibling(sel_item);

		LayerInfo* par_item_data = (LayerInfo*)GetItemData(par_item);
		if (item_data->type == 7 || item_data->type == 8)
		{
			wxTreeItemId vitem = GetParentVolItem(sel_item);
			if (!vitem.IsOk()) return;
			wxString vname = GetItemBaseText(vitem);
			LayerInfo* vitem_data = (LayerInfo*)GetItemData(vitem);
			if (!vitem_data) return;
			VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(vname);
			if (!vd) return;

			int next_id = vd->GetNextSiblingROI(item_data->id);
			vd->SetROISel(name_data.ToStdWstring(), false, true);
			vd->EraseROITreeNode(name_data.ToStdWstring());
			vr_frame->UpdateTree();
			SelectROI(vd, next_id);
			vr_frame->RefreshVRenderViews();
		}
		else if (par_item_data)
		{
			switch (par_item_data->type)
			{
			case 1://view
				{
					VRenderView* vrv = vr_frame->GetView(par_name);
					if (!vrv)
						break;
					if (item_data->type == 2)//volume data
					{
						VolumeData* vd = vrv->GetVolumeData(name_data);
						if (vd)
						{
							vd->SetDisp(true);
							vrv->RemoveVolumeData(name_data);
							if (vrv->GetVolMethod() == VOL_METHOD_MULTI)
							{
								AdjustView* adjust_view = vr_frame->GetAdjustView();
								if (adjust_view)
								{
									adjust_view->SetRenderView(vrv);
									adjust_view->UpdateSync();
								}
							}
						}
					}
					else if (item_data->type == 3)//mesh data
					{
						MeshData* md = vrv->GetMeshData(name_data);
						if (md)
						{
							md->SetDisp(true);
							vrv->RemoveMeshData(name_data);
						}
					}
					else if (item_data->type == 4)//annotations
					{
						Annotations* ann = vrv->GetAnnotations(name_data);
						if (ann)
						{
							ann->SetDisp(true);
							vrv->RemoveAnnotations(name_data);
						}
					}
					else if (item_data->type == 5)//group
					{
						vrv->RemoveGroup(name_data);
					}
					else if (item_data->type == 6)//mesh group
					{
						vrv->RemoveGroup(name_data);
					}

					if (!next_item.IsOk())
					{
						vr_frame->UpdateTree();
						vr_frame->RefreshVRenderViews();
						vr_frame->OnSelection(1);
					}
				}
				break;
			case 5://group
				{
					wxTreeItemId gpar_item = GetItemParent(par_item);
					wxString gpar_name = GetItemBaseText(gpar_item);
					VRenderView* vrv = vr_frame->GetView(gpar_name);
					if (!vrv)
						break;
					if (item_data->type == 2)
						vrv->RemoveVolumeData(name_data);
					if (!next_item.IsOk())
					{
						if (!vr_frame->GetDataManager()->GetVolumeData(clvdname) || !vr_frame->GetDataManager()->GetMeshData(clmdname))
							vr_frame->GetClippingView()->ClearData();

						vr_frame->UpdateTree();
						vr_frame->RefreshVRenderViews();
						vr_frame->OnSelection(1);
					}

					if (vrv->GetVolMethod() == VOL_METHOD_MULTI)
					{
						AdjustView* adjust_view = vr_frame->GetAdjustView();
						if (adjust_view)
						{
							adjust_view->SetRenderView(vrv);
							adjust_view->UpdateSync();
						}
					}
				}
				break;
			case 6://mesh group
				{
					wxTreeItemId gpar_item = GetItemParent(par_item);
					wxString gpar_name = GetItemBaseText(gpar_item);
					VRenderView* vrv = vr_frame->GetView(gpar_name);
					if (!vrv)
						break;
					if (item_data->type==3)
						vrv->RemoveMeshData(name_data);
					if (!next_item.IsOk())
					{
						if (!vr_frame->GetDataManager()->GetVolumeData(clvdname) || !vr_frame->GetDataManager()->GetMeshData(clmdname))
							vr_frame->GetClippingView()->ClearData();

						vr_frame->UpdateTree();
						vr_frame->RefreshVRenderViews();
						vr_frame->OnSelection(1);
					}
				}
				break;
			}
			if (next_item.IsOk())
			{
				LayerInfo* item_data = (LayerInfo*)GetItemData(next_item);
				wxString name = GetItemBaseText(next_item);
				if (item_data)
				{
					vr_frame->UpdateTree(name, item_data->type);
					vr_frame->RefreshVRenderViews();
				}
				else
				{
					vr_frame->UpdateTree();
					vr_frame->RefreshVRenderViews();
					vr_frame->OnSelection(1);
					if ((cl_sel_type == 2 && !vr_frame->GetDataManager()->GetVolumeData(clvdname)) ||
						(cl_sel_type == 3 && !vr_frame->GetDataManager()->GetMeshData(clmdname)))
						vr_frame->GetClippingView()->ClearData();
				}
			}
			else
			{
				if ((cl_sel_type == 2 && !vr_frame->GetDataManager()->GetVolumeData(clvdname)) ||
					(cl_sel_type == 3 && !vr_frame->GetDataManager()->GetMeshData(clmdname)))
					vr_frame->GetClippingView()->ClearData();
			}
		}
	}
}

void DataTreeCtrl::OnContextMenu(wxContextMenuEvent &event )
{
	if (m_fixed)
		return;

	int flag;
	wxTreeItemId sel_item = HitTest(ScreenToClient(event.GetPosition()), flag);
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
    VRenderView* view = (VRenderView*)vr_frame->GetView(0);

	if (sel_item.IsOk() && vr_frame)
	{
		SelectItem(sel_item);

		wxPoint point = event.GetPosition();
		// If from keyboard
		if (point.x == -1 && point.y == -1) {
			wxSize size = GetSize();
			point.x = size.x / 2;
			point.y = size.y / 2;
		} else {
			point = ScreenToClient(point);
		}

		wxMenu menu;
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			switch (item_data->type)
			{
			case 0:  //root
				if (IsExpanded(sel_item))
					menu.Append(ID_Expand, "Collapse");
				else
					menu.Append(ID_Expand, "Expand");
				break;
			case 1:  //view
				{
					menu.Append(ID_ToggleDisp, "Toggle Visibility");
					if (IsExpanded(sel_item))
						menu.Append(ID_Expand, "Collapse");
					else
						menu.Append(ID_Expand, "Expand");
					menu.AppendSeparator();
					menu.Append(ID_RandomizeColor, "Randomize Colors");
					menu.Append(ID_AddDataGroup, "Add Volume Group");
					menu.Append(ID_AddMeshGroup, "Add Mesh Group");
					wxString str = GetItemBaseText(sel_item);
					if (str != vr_frame->GetView(0)->GetName())
						menu.Append(ID_CloseView, "Close");
				}
				break;
			case 2:  //volume data
				{
					wxString name = GetItemBaseText(sel_item);
					DataManager *d_manage = vr_frame->GetDataManager();
					if (!d_manage) break;
					VolumeData* vd = d_manage->GetVolumeData(name);
					if (!vd) break;
					menu.Append(ID_ToggleDisp, "Toggle Visibility");
					menu.Append(ID_Rename, "Rename");
					menu.Append(ID_Duplicate, "Duplicate");
					menu.Append(ID_Save, "Save");
                    menu.Append(ID_SaveSegVol, "Save 3D Painted Region");
                    menu.Append(ID_ShowEntireVolume, "Show Entire Volume");
                    menu.Append(ID_HideOutsideMask, "Show Only Inside of Mask");
                    menu.Append(ID_HideInsideMask, "Show Only Outside of Mask");
					menu.Append(ID_ExportMask, "Export Mask");
					menu.Append(ID_ImportMask, "Import Mask");
					menu.Append(ID_BakeVolume, "Bake");
					menu.Append(ID_Isolate, "Isolate");
					menu.Append(ID_ShowAll, "Show All");
					//menu.Append(ID_ImportMetadata, "Import Metadata");
					menu.Append(ID_FlipH, "Flip Horizontally");
					menu.Append(ID_FlipV, "Flip Vertically");
                    //menu.Append(ID_ToggleNAMode, "Toggle NA Mode");
					//menu.Append(ID_ExportMeshMask, "Export Mesh Mask...");
					if (vd->GetColormapMode() == 3)
					{
						menu.AppendSeparator();
                        menu.Append(ID_AddSegments, "Add Segments");
						menu.Append(ID_ExportMetadata, "Export Metadata");
						menu.Append(ID_AddSegGroup, "Add Segment Group");
						menu.Append(ID_ShowAllSegChildren, "Select Children");
						menu.Append(ID_HideAllSegChildren, "Deselect Children");
						menu.Append(ID_ShowAllNamedSeg, "Select All Named Segments");
						menu.Append(ID_HideAllSeg, "Deselect All Segments");
						menu.Append(ID_DeleteAllSeg, "Delete All Segments");
					}
					menu.AppendSeparator();
					menu.Append(ID_RandomizeColor, "Randomize Colors");
                    menu.Append(ID_SetSameColor, "Set the Same Color to All Siblings");
					menu.Append(ID_AddDataGroup, "Add Volume Group");
					menu.Append(ID_RemoveData, "Delete");
					menu.AppendSeparator();
					menu.Append(ID_Edit, "Analyze...");
					menu.Append(ID_Info, "Information...");
					//menu.Append(ID_Trace, "Components && Tracking...");
					menu.Append(ID_NoiseCancelling, "Noise Reduction...");
					menu.Append(ID_Counting, "Counting and Volume...");
					menu.Append(ID_Colocalization, "Colocalization Analysis...");
					menu.Append(ID_Convert, "Convert...");
				}
				break;
			case 3:  //mesh data
				menu.Append(ID_ToggleDisp, "Toggle Visibility");
				menu.Append(ID_Rename, "Rename");
				menu.Append(ID_Save, "Save");
				menu.Append(ID_Duplicate, "Duplicate");
				menu.Append(ID_Isolate, "Isolate");
				menu.Append(ID_ShowAll, "Show All");
				menu.AppendSeparator();
				menu.Append(ID_RandomizeColor, "Randomize Colors");
                menu.Append(ID_SetSameColor, "Set the Same Color to All Siblings");
				menu.Append(ID_AddMeshGroup, "Add Mesh Group");
				menu.Append(ID_RemoveData, "Delete");
				menu.AppendSeparator();
				menu.Append(ID_ManipulateData, "Manipulate");
				break;
			case 4:  //annotations
				menu.Append(ID_Rename, "Rename");
				menu.Append(ID_Save, "Save");
				menu.Append(ID_ExportAllSegments, "Export Segments");
				break;
			case 5:  //data group
				menu.Append(ID_ToggleDisp, "Toggle Visibility");
				menu.Append(ID_Rename, "Rename");
				//menu.Append(ID_Duplicate, "Duplicate");
				if (IsExpanded(sel_item))
					menu.Append(ID_Expand, "Collapse");
				else
					menu.Append(ID_Expand, "Expand");
				menu.AppendSeparator();
				menu.Append(ID_RandomizeColor, "Randomize Colors");
                menu.Append(ID_SetSameColor, "Set the Same Color to All Children");
				menu.Append(ID_AddDataGroup, "Add Volume Group");
				menu.Append(ID_RemoveData, "Delete");
				break;
			case 6:  //mesh group
				menu.Append(ID_ToggleDisp, "Toggle Visibility");
				menu.Append(ID_Rename, "Rename");
				//menu.Append(ID_Duplicate, "Duplicate");
				if (IsExpanded(sel_item))
					menu.Append(ID_Expand, "Collapse");
				else
					menu.Append(ID_Expand, "Expand");
				menu.AppendSeparator();
				menu.Append(ID_RandomizeColor, "Randomize Colors");
                menu.Append(ID_SetSameColor, "Set the Same Color to All Children");
				menu.Append(ID_AddMeshGroup, "Add Mesh Group");
				menu.Append(ID_RemoveData, "Delete");
				break;
			case 7:
				menu.Append(ID_ToggleDisp, "Toggle Visibility");
				menu.Append(ID_AddSegGroup, "Add Segment Group");
				menu.Append(ID_RemoveData, "Delete");
				break;
			case 8:
				menu.Append(ID_ToggleDisp, "Toggle Visibility");
				menu.Append(ID_AddSegGroup, "Add Segment Group");
				menu.Append(ID_ShowAllSegChildren, "Select Children");
				menu.Append(ID_HideAllSegChildren, "Deselect Children");
				menu.Append(ID_RemoveData, "Delete");
				break;
			}
			PopupMenu( &menu, point.x, point.y );
		}
	}
}

void DataTreeCtrl::ExpandROITree(wxTreeItemId item)
{
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

    if (item.IsOk() && vr_frame)
    {
        SetEvtHandlerEnabled(false);
        LayerInfo* item_data = (LayerInfo*)GetItemData(item);
        wxString name = GetItemBaseText(item);
        if (item_data)
        {
            switch (item_data->type)
            {
            case 3:  //mesh data
                {
                    DataManager *d_manage = vr_frame->GetDataManager();
                    if (!d_manage) break;
                    MeshData* md = d_manage->GetMeshData(name);
                    if (!md) break;
                    if (md->isTree())
                    {
                        AddChildrenROITree(item, *md->getROITree(), md);
                    }
                }
            break;
            case 9://mesh segment
            case 10://mesh segment group
                {
                    wxTreeItemId mesh_item = GetParentMeshItem(item);
                    if (mesh_item.IsOk())
                    {
                        wxString mname = GetItemBaseText(mesh_item);
                        MeshData* md = vr_frame->GetDataManager()->GetMeshData(mname);
                        if (md)
                        {
                            boost::property_tree::wptree *child = md->getROITree(item_data->id);
                            AddChildrenROITree(item, *child, md);
                        }
                    }
                }
            break;
            }
        }
        SetEvtHandlerEnabled(true);
    }
}

void DataTreeCtrl::OnExpandItem(wxTreeEvent& event)
{
    wxTreeItemId sel_item = event.GetItem();
    ExpandROITree(sel_item);
    
    event.Skip();
}

void DataTreeCtrl::OnToggleDisp(wxCommandEvent& event)
{
	wxTreeEvent tevent;
	OnAct(tevent);
}

void DataTreeCtrl::OnDuplicate(wxCommandEvent& event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (sel_item.IsOk() && vr_frame)
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		wxString name = GetItemBaseText(sel_item);
		if (item_data)
		{
			switch (item_data->type)
			{
			case 2:  //volume data
				{
					DataManager *d_manage = vr_frame->GetDataManager();
					if (!d_manage) break;
					VolumeData* vd = d_manage->GetVolumeData(name);
					if (!vd) break;
					VRenderView* view = GetCurrentView();
					if (view)
					{
						VolumeData* vd_add = vr_frame->GetDataManager()->DuplicateVolumeData(vd, false);
						DataGroup *group = view->AddVolumeData(vd_add);
						vr_frame->OnSelection(2, view, group, vd_add, 0);
						vr_frame->UpdateTree(vd->GetName());
						if (view->GetVolMethod() == VOL_METHOD_MULTI)
						{
							AdjustView* adjust_view = vr_frame->GetAdjustView();
							if (adjust_view)
							{
								adjust_view->SetRenderView(view);
								adjust_view->UpdateSync();
							}
						}
					}
				}
				break;
			case 3:  //mesh data
				{
					DataManager *d_manage = vr_frame->GetDataManager();
					if (!d_manage) break;
					MeshData* md = d_manage->GetMeshData(name);
					if (!md) break;
					VRenderView* view = GetCurrentView();
					if (view)
					{
						MeshData* md_add = vr_frame->GetDataManager()->DuplicateMeshData(md, false);
						view->AddMeshData(md_add);
						vr_frame->OnSelection(3, 0, 0, 0, md_add);
						vr_frame->UpdateTree(md_add->GetName());
					}
				}
				break;
			}
		}
	}
}

void DataTreeCtrl::OnRenameMenu(wxCommandEvent& event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	if (sel_item.IsOk())
	{
		m_rename = true;
		EditLabel(sel_item);
	}
}

void DataTreeCtrl::OnRename(wxTreeEvent& event)
{
	if (m_fixed || !m_rename)
	{
		event.Veto();
		return;
	}

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (sel_item.IsOk() && vr_frame)
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			switch (item_data->type)
			{
			case 0:  //root
				event.Veto();
				break;
			case 1:  //view
				event.Veto();
				break;
			case 2:  //volume data
				
				break;
			case 3:  //mesh data
				
				break;
			case 4:  //annotations

				break;
			case 5:  //data group

				break;
			case 6:  //mesh group

				break;
			case 7: //segment group

				break;
			case 8: //segment

				break;
			}
		}
	}
}

void DataTreeCtrl::OnRenamed(wxTreeEvent& event)
{
	if (m_fixed)
		return;

	m_rename = false;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	wxString prev_name = GetItemBaseText(sel_item);
	wxString name = event.GetLabel();

	if (sel_item.IsOk() && vr_frame && prev_name != name && name != wxString(""))
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			switch (item_data->type)
			{
			case 0:  //root

				break;
			case 1:  //view

				break;
			case 2:  //volume data
				{
					DataManager *d_manage = vr_frame->GetDataManager();
					if (!d_manage) break;
					VolumeData* vd = d_manage->GetVolumeData(prev_name);
					if (!vd) break;
					name = d_manage->CheckNewName(name, DATA_VOLUME);
					vd->SetName(name);
					SetItemText(sel_item, name);
					vr_frame->UpdateTree(name, item_data->type);
					vr_frame->OnSelection(2, 0, 0, vd);
				}
				break;
			case 3:  //mesh data
				{
					DataManager *d_manage = vr_frame->GetDataManager();
					if (!d_manage) break;
					MeshData* md = d_manage->GetMeshData(prev_name);
					if (!md) break;
					name = d_manage->CheckNewName(name, DATA_MESH);
					md->SetName(name);
					SetItemText(sel_item, name);
					vr_frame->UpdateTree(name, item_data->type);
					vr_frame->OnSelection(3, 0, 0, 0, md);

				}
				break;
			case 4:  //annotations
				{
					DataManager *d_manage = vr_frame->GetDataManager();
					if (!d_manage) break;
					Annotations* an = d_manage->GetAnnotations(prev_name);
					if (!an) break;
					name = d_manage->CheckNewName(name, DATA_ANNOTATIONS);
					an->SetName(name);
					if (vr_frame->GetPropView()) vr_frame->GetPropView()->SetLabel(name);
					SetItemText(sel_item, name);
					vr_frame->UpdateTree(name, item_data->type);
					vr_frame->OnSelection(4, 0, 0, 0, 0, an);
				}
				break;
			case 5:  //data group
			case 6:  //mesh group
				{
					wxTreeItemId par_item = GetItemParent(sel_item);
					if (par_item.IsOk())
					{
						LayerInfo* par_data = (LayerInfo*)GetItemData(par_item);
						if (par_data && par_data->type == 1)
						{
							//group in view
							wxString vname = GetItemBaseText(par_item);
							VRenderView* vrv = vr_frame->GetView(vname);
							if (vrv)
							{
								if (item_data->type == 5)
								{
									DataGroup *group = vrv->GetGroup(prev_name);
									if (!group) break;
									name = vrv->CheckNewGroupName(name, LAYER_DATAGROUP);
									group->SetName(name);
									SetItemText(sel_item, name);
									vr_frame->UpdateTree(name, item_data->type);
									vr_frame->OnSelection(5, 0, group);
								}
								else if (item_data->type == 6)
								{
									MeshGroup *mgroup = vrv->GetMGroup(prev_name);
									if (!mgroup) break;
									name = vrv->CheckNewGroupName(name, LAYER_MESHGROUP);
									mgroup->SetName(name);
									SetItemText(sel_item, name);
									vr_frame->UpdateTree(name, item_data->type);
									vr_frame->OnSelection(6);
								}
							}
						}
					}
				}
				break;
			case 7: //segment group
			case 8: //segment
				{
					DataManager *d_manage = vr_frame->GetDataManager();
					wxTreeItemId vol_item = GetParentVolItem(sel_item);
					if (vol_item.IsOk())
					{
						wxString vname = GetItemBaseText(vol_item);
						VolumeData* vd = d_manage->GetVolumeData(vname);
						if (vd && item_data->id != -1)
						{
							vd->SetROIName(name.ToStdWstring(), item_data->id);
							UpdateVolItem(vol_item, vd);
							if (vr_frame->GetPropView()) vr_frame->GetPropView()->UpdateUIsROI();
							SetItemText(sel_item, name);
							vr_frame->UpdateTree(name, item_data->type);
							vr_frame->OnSelection(2, 0, 0, vd);
						}
					}
				}
				break;
			}
		}
	}
	if (name == wxString(""))
	{
		SetItemText(sel_item, prev_name);
	}

	event.Veto();
}

void DataTreeCtrl::OnSave(wxCommandEvent& event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (!sel_item.IsOk() || !vr_frame)
		return;
	LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
	if (!item_data)
		return;

	wxString name = GetItemBaseText(sel_item);

	if (item_data->type == 2) //volume
	{
		VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);

		wxString formats;
		if (vd && vd->isBrxml())
		{
			formats = "Muti-page Tiff file (*.tif, *.tiff)|*.tif;*.tiff|"\
					  "Single-page Tiff sequence (*.tif)|*.tif;*.tiff";
		}
		else
		{
			formats = "Muti-page Tiff file (*.tif, *.tiff)|*.tif;*.tiff|"\
					  "Single-page Tiff sequence (*.tif)|*.tif;*.tiff|"\
					  "Nrrd file (*.nrrd)|*.nrrd";
		}
		wxFileDialog *fopendlg = new wxFileDialog(
			m_frame, "Save Volume Data", "", "",
			formats,
			wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
		fopendlg->SetExtraControlCreator(CreateExtraControl);

		int rval = fopendlg->ShowModal();

		if (rval == wxID_OK)
		{
			wxString filename = fopendlg->GetPath();
			if (vd)
				vd->Save(filename, fopendlg->GetFilterIndex(), false, true, true, true, GetCurrentView() ? GetCurrentView()->GetVolumeLoader() : NULL, true, m_save_scale_level);
		}
		if (vd && vd->isBrxml())
			vr_frame->RefreshVRenderViews();
		delete fopendlg;
	}
	else if (item_data->type == 3) //mesh
	{
		wxFileDialog *fopendlg = new wxFileDialog(
			m_frame, "Save Mesh Data", "", "",
			"OBJ file (*.obj)|*.obj",
			wxFD_SAVE|wxFD_OVERWRITE_PROMPT);

		int rval = fopendlg->ShowModal();

		if (rval == wxID_OK)
		{
			wxString filename = fopendlg->GetPath();

			VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
			MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
			if (md)
				md->Save(filename);
		}
		delete fopendlg;
	}
	else if (item_data->type == 4) //annotation
	{
		wxFileDialog *fopendlg = new wxFileDialog(
			m_frame, "Save Annotations", "", "",
			"Text file (*.txt)|*.txt",
			wxFD_SAVE|wxFD_OVERWRITE_PROMPT);

		int rval = fopendlg->ShowModal();

		if (rval == wxID_OK)
		{
			wxString filename = fopendlg->GetPath();
			Annotations* ann = vr_frame->GetDataManager()->GetAnnotations(name);
			if (ann)
				ann->Save(filename);
		}
		delete fopendlg;
	}
}

void DataTreeCtrl::OnSaveSegmentedVolume(wxCommandEvent& event)
{
    if (m_fixed)
        return;
    
    wxTreeItemId sel_item = GetSelection();
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    
    if (!sel_item.IsOk() || !vr_frame)
        return;
    LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
    if (!item_data)
        return;
    
    wxString name = GetItemBaseText(sel_item);
    
    if (item_data->type == 2) //volume
    {
        VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
        
        wxString formats;
        if (vd && vd->isBrxml())
        {
            formats = "Single-page Tiff sequence (*.tif)|*.tif;*.tiff";
        }
        else
        {
            formats = "Muti-page Tiff file (*.tif, *.tiff)|*.tif;*.tiff|"\
            "Single-page Tiff sequence (*.tif)|*.tif;*.tiff|"\
            "Nrrd file (*.nrrd)|*.nrrd";
        }
        wxFileDialog *fopendlg = new wxFileDialog(
                                                  m_frame, "Save Volume Data", "", "",
                                                  formats,
                                                  wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
        fopendlg->SetExtraControlCreator(CreateExtraControl);
        
        int rval = fopendlg->ShowModal();
        
        if (rval == wxID_OK)
        {
            wxString filename = fopendlg->GetPath();
            if (vd)
            {
                auto mskmode = vd->GetMaskHideMode();
                vd->SetMaskHideMode(VOL_MASK_HIDE_OUTSIDE);
                vd->Save(filename, fopendlg->GetFilterIndex(), false, VRenderFrame::GetCompression(), true, true, GetCurrentView() ? GetCurrentView()->GetVolumeLoader() : NULL, true);
                vd->SetMaskHideMode(mskmode);
            }
        }
        if (vd && vd->isBrxml())
            vr_frame->RefreshVRenderViews();
        delete fopendlg;
    }
}


void DataTreeCtrl::OnShowEntireVolume(wxCommandEvent& event)
{
    if (m_fixed)
        return;
    
    wxTreeItemId sel_item = GetSelection();
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    
    if (!sel_item.IsOk() || !vr_frame)
        return;
    LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
    if (!item_data)
        return;
    
    wxString name = GetItemBaseText(sel_item);
    
    if (item_data->type == 2) //volume
    {
        VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
        if (!vd) return;
        vd->SetMaskHideMode(VOL_MASK_HIDE_NONE);
        vr_frame->RefreshVRenderViews();
    }
}


void DataTreeCtrl::OnHideOutsideOfMask(wxCommandEvent& event)
{
    if (m_fixed)
        return;
    
    wxTreeItemId sel_item = GetSelection();
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    
    if (!sel_item.IsOk() || !vr_frame)
        return;
    LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
    if (!item_data)
        return;
    
    wxString name = GetItemBaseText(sel_item);
    
    if (item_data->type == 2) //volume
    {
        VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
        if (!vd) return;
        vd->SetMaskHideMode(VOL_MASK_HIDE_OUTSIDE);
        vr_frame->RefreshVRenderViews();
    }
}

void DataTreeCtrl::OnHideInsideOfMask(wxCommandEvent &event)
{
    if (m_fixed)
        return;
    
    wxTreeItemId sel_item = GetSelection();
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    
    if (!sel_item.IsOk() || !vr_frame)
        return;
    LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
    if (!item_data)
        return;
    
    wxString name = GetItemBaseText(sel_item);
    
    if (item_data->type == 2) //volume
    {
        VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
        if (!vd) return;
        vd->SetMaskHideMode(VOL_MASK_HIDE_INSIDE);
        vr_frame->RefreshVRenderViews();
    }
}

void DataTreeCtrl::OnExportMask(wxCommandEvent& event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (!sel_item.IsOk() || !vr_frame)
		return;
	LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
	if (!item_data)
		return;

	wxString name = GetItemBaseText(sel_item);

	if (item_data->type == 2) //volume
	{
		VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
		
		wxFileDialog *fopendlg = new wxFileDialog(
			m_frame, "Export Mask Data", "", "",
			"Nrrd file (*.nrrd)|*.nrrd",
			wxFD_SAVE|wxFD_OVERWRITE_PROMPT);

		int rval = fopendlg->ShowModal();

		if (rval == wxID_OK)
		{
			wxString filename = fopendlg->GetPath();
			if (vd)
				vd->ExportMask(filename);
		}

		delete fopendlg;
	}
}


void DataTreeCtrl::OnImportMask(wxCommandEvent& event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (!sel_item.IsOk() || !vr_frame)
		return;
	LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
	if (!item_data)
		return;

	wxString name = GetItemBaseText(sel_item);

	if (item_data->type == 2) //volume
	{
		VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
		
		wxFileDialog *fopendlg = new wxFileDialog(
			m_frame, "Import Mask Data", "", "",
			"All Supported|*.msk;*.nrrd",
			wxFD_OPEN);

		int rval = fopendlg->ShowModal();

		if (rval == wxID_OK)
		{
			wxString filename = fopendlg->GetPath();
			if (vd)
				vd->ImportMask(filename);
		}

		delete fopendlg;

		vr_frame->RefreshVRenderViews();
	}
}

void DataTreeCtrl::OnBakeVolume(wxCommandEvent& event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (!sel_item.IsOk() || !vr_frame)
		return;
	LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
	if (!item_data)
		return;

	wxString name = GetItemBaseText(sel_item);

	if (item_data->type == 2) //volume
	{
		VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
		if (vd && vd->isBrxml())
			return;

		wxFileDialog *fopendlg = new wxFileDialog(
			m_frame, "Bake Volume Data", "", "",
			"Muti-page Tiff file (*.tif, *.tiff)|*.tif;*.tiff|"\
			"Single-page Tiff sequence (*.tif)|*.tif;*.tiff|"\
			"Nrrd file (*.nrrd)|*.nrrd",
			wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
		fopendlg->SetExtraControlCreator(CreateExtraControl);

		int rval = fopendlg->ShowModal();

		if (rval == wxID_OK)
		{
			wxString filename = fopendlg->GetPath();
			if (vd)
				vd->Save(filename, fopendlg->GetFilterIndex(), true, VRenderFrame::GetCompression(), false, false, NULL, true);
		}

		delete fopendlg;
	}
}

void DataTreeCtrl::OnIsolate(wxCommandEvent& event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (sel_item.IsOk() && vr_frame)
	{
		wxString viewname = "";
		wxString itemname = "";
		int item_type = 0;

		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			item_type = item_data->type;
			itemname = GetItemBaseText(sel_item);
			wxTreeItemId par_item = GetItemParent(sel_item);
			if (par_item.IsOk())
			{
				LayerInfo* par_data = (LayerInfo*)GetItemData(par_item);
				if (par_data)
				{
					if (par_data->type == 1)
					{
						//view
						viewname = GetItemBaseText(par_item);
					}
					else if (par_data->type == 5 ||
						par_data->type == 6)
					{
						wxTreeItemId gpar_item = GetItemParent(par_item);
						if (gpar_item.IsOk())
						{
							LayerInfo* gpar_data = (LayerInfo*)GetItemData(gpar_item);
							if (gpar_data && gpar_data->type==1)
							{
								//view
								viewname = GetItemBaseText(gpar_item);
							}
						}
					}
				}
			}
		}

		VRenderView* vrv = vr_frame->GetView(viewname);
		if (vrv)
		{
			vrv->Isolate(item_type, itemname);
			vrv->RefreshGL();
			vr_frame->UpdateTreeIcons();
		}

		UpdateSelection();
	}
}

void DataTreeCtrl::OnShowAll(wxCommandEvent& event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (sel_item.IsOk() && vr_frame)
	{
		wxString viewname = "";
		wxString itemname = "";
		int item_type = 0;

		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			item_type = item_data->type;
			itemname = GetItemBaseText(sel_item);
			wxTreeItemId par_item = GetItemParent(sel_item);
			if (par_item.IsOk())
			{
				LayerInfo* par_data = (LayerInfo*)GetItemData(par_item);
				if (par_data)
				{
					if (par_data->type == 1)
					{
						//view
						viewname = GetItemBaseText(par_item);
					}
					else if (par_data->type == 5 ||
						par_data->type == 6)
					{
						wxTreeItemId gpar_item = GetItemParent(par_item);
						if (gpar_item.IsOk())
						{
							LayerInfo* gpar_data = (LayerInfo*)GetItemData(gpar_item);
							if (gpar_data && gpar_data->type==1)
							{
								//view
								viewname = GetItemBaseText(gpar_item);
							}
						}
					}
				}
			}
		}

		VRenderView* vrv = vr_frame->GetView(viewname);
		if (vrv)
		{
			vrv->ShowAll();
			vrv->RefreshGL();
			vr_frame->UpdateTreeIcons();
		}

		UpdateSelection();
	}
}


void DataTreeCtrl::OnAddSegments(wxCommandEvent& event)
{
    if (m_fixed)
        return;

    wxTreeItemId sel_item = GetSelection();
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    if (!vr_frame) return;
    DataManager* d_manager = vr_frame->GetDataManager();
    if (!d_manager) return;
    
    if (sel_item.IsOk())
    {
        wxString viewname = "";
        LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
        if (item_data)
        {
            int item_type = item_data->type;
            wxString itemname = GetItemBaseText(sel_item);
            if (item_type == 2)
            {
                VolumeData* vd = d_manager->GetVolumeData(GetItemBaseText(sel_item));
                if (vd && vd->GetColormapMode() == 3)
                    vd->GenAllROINames();
            }
        }
        vr_frame->RefreshVRenderViews();
        vr_frame->UpdateTree();
    }
}


void DataTreeCtrl::OnImportMetadata(wxCommandEvent& event)
{
	if (m_fixed)
	return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager* d_manager = vr_frame->GetDataManager();
	if (!d_manager) return;
	
	if (sel_item.IsOk())
	{
		wxString viewname = "";
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			int item_type = item_data->type;
			wxString itemname = GetItemBaseText(sel_item);
			if (item_type == 2)
			{
				VolumeData* vd = d_manager->GetVolumeData(GetItemBaseText(sel_item));
				if (vd)
				{
					wxFileDialog *fopendlg = new wxFileDialog(
						m_frame, "Open Metadata", "", "",
						"XML file (*.xml)|*.xml",
						wxFD_OPEN);

					int rval = fopendlg->ShowModal();

					if (rval == wxID_OK)
					{
						wxString filename = fopendlg->GetPath();
						vd->ImportROITreeXML(filename.ToStdWstring());
						if (!vd->ExportROITree().empty())
							vd->SetColormapMode(3);
					}
					delete fopendlg;
				}
			}
		}
		wxCommandEvent ev;
		OnShowAllNamedSeg(ev);

		vr_frame->RefreshVRenderViews();
		vr_frame->UpdateTree();
	}
}

void DataTreeCtrl::OnExportMetadata(wxCommandEvent& event)
{
	if (m_fixed)
	return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager* d_manager = vr_frame->GetDataManager();
	if (!d_manager) return;
	
	if (sel_item.IsOk())
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			int item_type = item_data->type;
			wxString itemname = GetItemBaseText(sel_item);
			if (item_type == 2)
			{
				VolumeData* vd = d_manager->GetVolumeData(GetItemBaseText(sel_item));
				if (vd && vd->isBrxml())
				{
					wxFileDialog *fopendlg = new wxFileDialog(
						m_frame, "Save a VVD file with Metadata", "", "",
						"VVD file (*.vvd)|*.vvd",
						wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
					fopendlg->SetExtraControlCreator(CreateExtraControl);

					int rval = fopendlg->ShowModal();

					if (rval == wxID_OK)
					{
						wxString filename = fopendlg->GetPath();
						BRKXMLReader *br = (BRKXMLReader *)vd->GetReader();
						tinyxml2::XMLDocument *doc = br->GetVVDXMLDoc();

						BRKXMLWriter bw;
						wstring rtree = vd->ExportROITree();
						if (!rtree.empty())
						{
							bw.AddROITreeToMetadata(rtree);
							if (m_md_save_indv)
								bw.SaveVVDXML_ExternalMetadata(filename.ToStdWstring(), doc);
							else
								bw.SaveVVDXML_Metadata(filename.ToStdWstring(), doc);
						}
					}
					delete fopendlg;
				}
				else
				{
					wxFileDialog *fopendlg = new wxFileDialog(
						m_frame, "Save Metadata", "", "_metadata.xml",
						"XML file (*.xml)|*.xml",
						wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
					
					int rval = fopendlg->ShowModal();

					if (rval == wxID_OK)
					{
						wxString filename = fopendlg->GetPath();
						BRKXMLReader *br = (BRKXMLReader *)vd->GetReader();
						
						BRKXMLWriter bw;
						wstring rtree = vd->ExportROITree();
						if (!rtree.empty())
						{
							bw.AddROITreeToMetadata(rtree);
							bw.SaveMetadataXML(filename.ToStdWstring());
						}
					}
					delete fopendlg;
				}
			}
		}
	}
}

void DataTreeCtrl::OnCh1Check(wxCommandEvent &event)
{
   wxCheckBox* ch1 = (wxCheckBox*)event.GetEventObject();
   if (ch1)
      m_md_save_indv = ch1->GetValue();
}

void DataTreeCtrl::OnTxt1Change(wxCommandEvent& event)
{
	wxTextCtrl* txt1 = (wxTextCtrl*)event.GetEventObject();
	if (txt1)
		m_save_scale_level = wxAtoi(txt1->GetValue());
}

wxWindow* DataTreeCtrl::CreateExtraControl(wxWindow* parent)
{
   wxPanel* panel = new wxPanel(parent, 0, wxDefaultPosition, wxSize(400, 90));

   wxBoxSizer *group1 = new wxStaticBoxSizer(
         new wxStaticBox(panel, wxID_ANY, "Additional Options"), wxVERTICAL);

   //compressed
   wxCheckBox* ch1 = new wxCheckBox(panel, wxID_HIGHEST+3004,
         "Save metadata as an individual file");
   ch1->Connect(ch1->GetId(), wxEVT_COMMAND_CHECKBOX_CLICKED,
         wxCommandEventHandler(DataTreeCtrl::OnCh1Check), NULL, panel);
   if (ch1)
      ch1->SetValue(m_md_save_indv);

   wxBoxSizer* sizer1 = new wxBoxSizer(wxHORIZONTAL);
   wxIntegerValidator<unsigned int> vald_int;
   wxTextCtrl* txt1 = new wxTextCtrl(panel, wxID_HIGHEST + 3005, "0",
	   wxDefaultPosition, wxSize(34, 22), 0, vald_int);
   wxString default_level = wxString::Format(wxT("%i"), m_save_scale_level);
   txt1->SetValue(default_level);
   txt1->Connect(txt1->GetId(), wxEVT_COMMAND_TEXT_UPDATED,
	   wxCommandEventHandler(DataTreeCtrl::OnTxt1Change), NULL, panel);
   wxStaticText* st = new wxStaticText(panel, 0,
	   "Scale level (for resolution pyramid)");
   sizer1->Add(txt1);
   sizer1->Add(10, 10);
   sizer1->Add(st);

   //group
   group1->Add(10, 10);
   group1->Add(ch1);
   group1->Add(10, 10);
   group1->Add(sizer1);
   group1->Add(10, 10);

   panel->SetSizer(group1);
   panel->Layout();

   return panel;
}

void DataTreeCtrl::OnShowAllSegChildren(wxCommandEvent& event)
{
	if (m_fixed)
	return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager* d_manager = vr_frame->GetDataManager();
	if (!d_manager) return;
	
	if (sel_item.IsOk())
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			int item_type = item_data->type;
			wxString itemname = GetItemBaseText(sel_item);
			if (item_type == 8)
			{

				wxTreeItemId vol_item = GetParentVolItem(sel_item);
				if (vol_item.IsOk())
				{
					wxString vname = GetItemBaseText(vol_item);
					VolumeData* vd = d_manager->GetVolumeData(vname);
					if (vd)
					{
						int id = vd->GetROIid(GetItemBaseText(sel_item).ToStdWstring());
						if (id != -1)
						{
							vd->SetROISel(GetItemBaseText(sel_item).ToStdWstring(), true);
							vd->SetROISelChildren(GetItemBaseText(sel_item).ToStdWstring(), true);
						}

						vr_frame->UpdateTreeIcons();
						vr_frame->RefreshVRenderViews();
					}
				}
			}
			if (item_type == 2)
			{
				VolumeData* vd = d_manager->GetVolumeData(GetItemBaseText(sel_item));
				if (vd)
				{
					vd->SetROISelChildren(wstring(), true);
					vr_frame->UpdateTreeIcons();
					vr_frame->RefreshVRenderViews();
				}
			}
		}
	}
}

void DataTreeCtrl::OnHideAllSegChildren(wxCommandEvent& event)
{
	if (m_fixed)
	return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager* d_manager = vr_frame->GetDataManager();
	if (!d_manager) return;

	if (sel_item.IsOk())
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			int item_type = item_data->type;
			wxString itemname = GetItemBaseText(sel_item);
			if (item_type == 8)
			{

				wxTreeItemId vol_item = GetParentVolItem(sel_item);
				if (vol_item.IsOk())
				{
					wxString vname = GetItemBaseText(vol_item);
					VolumeData* vd = d_manager->GetVolumeData(vname);
					if (vd)
					{
						int id = vd->GetROIid(GetItemBaseText(sel_item).ToStdWstring());
						if (id != -1)
							vd->SetROISelChildren(GetItemBaseText(sel_item).ToStdWstring(), false);

						vr_frame->UpdateTreeIcons();
						vr_frame->RefreshVRenderViews();
					}
				}
			}
			if (item_type == 2)
			{
				VolumeData* vd = d_manager->GetVolumeData(GetItemBaseText(sel_item));
				if (vd)
				{
					vd->SetROISelChildren(wstring(), false);
					vr_frame->UpdateTreeIcons();
					vr_frame->RefreshVRenderViews();
				}
			}
		}
	}
}

void DataTreeCtrl::OnShowAllNamedSeg(wxCommandEvent& event)
{
	if (m_fixed)
	return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager* d_manager = vr_frame->GetDataManager();
	if (!d_manager) return;

	if (sel_item.IsOk())
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			int item_type = item_data->type;
			wxString itemname = GetItemBaseText(sel_item);
			VolumeData* vd = NULL;
			if (item_type == 8)
			{

				wxTreeItemId vol_item = GetParentVolItem(sel_item);
				if (vol_item.IsOk())
				{
					wxString vname = GetItemBaseText(vol_item);
					vd = d_manager->GetVolumeData(vname);
				}
			}
			if (item_type == 2)
				vd = d_manager->GetVolumeData(GetItemBaseText(sel_item));

			if (vd)
			{
				vd->SelectAllNamedROI();
				vr_frame->UpdateTreeIcons();
				vr_frame->RefreshVRenderViews();
			}
		}
	}
}

void DataTreeCtrl::OnHideAllSeg(wxCommandEvent& event)
{
	if (m_fixed)
	return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager* d_manager = vr_frame->GetDataManager();
	if (!d_manager) return;

	if (sel_item.IsOk())
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			int item_type = item_data->type;
			wxString itemname = GetItemBaseText(sel_item);
			VolumeData* vd = NULL;
			if (item_type == 8)
			{

				wxTreeItemId vol_item = GetParentVolItem(sel_item);
				if (vol_item.IsOk())
				{
					wxString vname = GetItemBaseText(vol_item);
					vd = d_manager->GetVolumeData(vname);
				}
			}
			if (item_type == 2)
				vd = d_manager->GetVolumeData(GetItemBaseText(sel_item));

			if (vd)
			{
				vd->DeselectAllROI();
				vr_frame->UpdateTreeIcons();
				vr_frame->RefreshVRenderViews();
			}
		}
	}
}

void DataTreeCtrl::OnDeleteAllSeg(wxCommandEvent& event)
{
	if (m_fixed)
	return;

	wxMessageDialog dialog(this, "Do you want to proceed?", "Delete all segments",
		wxOK | wxCANCEL | wxCENTRE);
	int result = dialog.ShowModal();

	if (result == wxID_CANCEL)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager* d_manager = vr_frame->GetDataManager();
	if (!d_manager) return;

	if (sel_item.IsOk())
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			int item_type = item_data->type;
			wxString itemname = GetItemBaseText(sel_item);
			VolumeData* vd = NULL;
			wxString vname;
			if (item_type == 8)
			{

				wxTreeItemId vol_item = GetParentVolItem(sel_item);
				if (vol_item.IsOk())
				{
					vname = GetItemBaseText(vol_item);
					vd = d_manager->GetVolumeData(vname);
				}
			}
			if (item_type == 2)
			{
				vname = GetItemBaseText(sel_item);
				vd = d_manager->GetVolumeData(vname);
			}

			if (vd)
			{
				vd->ClearROIs();
				vr_frame->UpdateTree();
				wxTreeItemId vitem = FindTreeItem(vname);
				if (vitem.IsOk()) SelectItem(vitem);
				vr_frame->RefreshVRenderViews();
			}
		}
	}
}

void DataTreeCtrl::OnRemoveData(wxCommandEvent& event)
{
	DeleteSelection();
}

void DataTreeCtrl::OnCloseView(wxCommandEvent& event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (sel_item.IsOk() && vr_frame)
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data && item_data->type == 1)//view
		{
			wxString name = GetItemBaseText(sel_item);
			vr_frame->DeleteVRenderView(name);
		}
	}
}

void DataTreeCtrl::OnManipulateData(wxCommandEvent& event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (sel_item.IsOk() && vr_frame)
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data && item_data->type == 3)//mesh data
		{
			wxString name = GetItemBaseText(sel_item);
			MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
			vr_frame->OnSelection(6, 0, 0, 0, md);
		}
	}
}

void DataTreeCtrl::OnAddMeshGroup(wxCommandEvent &event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;

	if (!sel_item.IsOk())
	{
		wxTreeItemIdValue cookie;
		sel_item = GetFirstChild(GetRootItem(), cookie);
	}

	if (sel_item.IsOk())
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data && item_data->type == 1)
		{
			//view
			wxString name = GetItemBaseText(sel_item);
			VRenderView* vrv = vr_frame->GetView(name);
			if (vrv)
			{
				wxString group_name = vrv->AddMGroup();
				AddMGroupItem(sel_item, group_name);
				Expand(sel_item);
			}
		}
		else if (item_data && item_data->type == 2)
		{
			//volume
			wxTreeItemId par_item = GetItemParent(sel_item);
			if (par_item.IsOk())
			{
				LayerInfo* par_data = (LayerInfo*) GetItemData(par_item);
				if (par_data && par_data->type == 1)
				{
					//volume in view
					wxString name = GetItemBaseText(par_item);
					VRenderView* vrv = vr_frame->GetView(name);
					if (vrv)
					{
						wxString group_name = vrv->AddMGroup();
						AddMGroupItem(par_item, group_name);
					}
				}
				else if (par_data && par_data->type == 5)
				{
					//volume in group
					wxTreeItemId gpar_item = GetItemParent(par_item);
					if (gpar_item.IsOk())
					{
						LayerInfo* gpar_data = (LayerInfo*)GetItemData(gpar_item);
						if (gpar_data && gpar_data->type == 1)
						{
							wxString name = GetItemBaseText(gpar_item);
							VRenderView* vrv = vr_frame->GetView(name);
							if (vrv)
							{
								wxString group_name = vrv->AddMGroup();
								AddMGroupItem(gpar_item, group_name);
							}
						}
					}
				}
			}
		}
		else if (item_data && item_data->type == 3)
		{
			//mesh
			wxTreeItemId par_item = GetItemParent(sel_item);
			if (par_item.IsOk())
			{
				LayerInfo* par_data = (LayerInfo*) GetItemData(par_item);
				if (par_data && par_data->type == 1)
				{
					//mesh in view
					wxString name = GetItemBaseText(par_item);
					VRenderView* vrv = vr_frame->GetView(name);
					if (vrv)
					{
						wxString group_name = vrv->AddMGroup();
						AddMGroupItem(par_item, group_name);
					}
				}
				else if (par_data && par_data->type == 6)
				{
					//mesh in group
					wxTreeItemId gpar_item = GetItemParent(par_item);
					if (gpar_item.IsOk())
					{
						LayerInfo* gpar_data = (LayerInfo*)GetItemData(gpar_item);
						if (gpar_data && gpar_data->type == 1)
						{
							wxString name = GetItemBaseText(gpar_item);
							VRenderView* vrv = vr_frame->GetView(name);
							if (vrv)
							{
								wxString group_name = vrv->AddMGroup();
								AddMGroupItem(gpar_item, group_name);
							}
						}
					}
				}
			}
		}
		else if ((item_data && item_data->type == 5) ||
			(item_data && item_data->type == 6))
		{
			//group
			wxTreeItemId par_item = GetItemParent(sel_item);
			if (par_item.IsOk())
			{
				LayerInfo* par_data = (LayerInfo*)GetItemData(par_item);
				if (par_data && par_data->type == 1)
				{
					//group in view
					wxString name = GetItemBaseText(par_item);
					VRenderView* vrv = vr_frame->GetView(name);
					if (vrv)
					{
						wxString group_name = vrv->AddMGroup();
						AddMGroupItem(par_item, group_name);
					}
				}
			}
		}
	}
}

void DataTreeCtrl::OnAddDataGroup(wxCommandEvent& event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;

	if (!sel_item.IsOk())
	{
		wxTreeItemIdValue cookie;
		sel_item = GetFirstChild(GetRootItem(), cookie);
	}

	if (sel_item.IsOk())
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data && item_data->type == 1)
		{
			//view
			wxString name = GetItemBaseText(sel_item);
			VRenderView* vrv = vr_frame->GetView(name);
			if (vrv)
			{
				wxString group_name = vrv->AddGroup();
				AddGroupItem(sel_item, group_name);
				Expand(sel_item);
			}
		}
		else if (item_data && item_data->type == 2)
		{
			//volume
			wxTreeItemId par_item = GetItemParent(sel_item);
			if (par_item.IsOk())
			{
				LayerInfo* par_data = (LayerInfo*) GetItemData(par_item);
				if (par_data && par_data->type == 1)
				{
					//volume in view
					wxString name = GetItemBaseText(par_item);
					VRenderView* vrv = vr_frame->GetView(name);
					if (vrv)
					{
						wxString group_name = vrv->AddGroup();
						AddGroupItem(par_item, group_name);
					}
				}
				else if (par_data && par_data->type == 5)
				{
					//volume in group
					wxTreeItemId gpar_item = GetItemParent(par_item);
					if (gpar_item.IsOk())
					{
						LayerInfo* gpar_data = (LayerInfo*)GetItemData(gpar_item);
						if (gpar_data && gpar_data->type == 1)
						{
							wxString name = GetItemBaseText(gpar_item);
							VRenderView* vrv = vr_frame->GetView(name);
							if (vrv)
							{
								wxString group_name = vrv->AddGroup();
								AddGroupItem(gpar_item, group_name);
							}
						}
					}
				}
			}
		}
		else if (item_data && item_data->type == 3)
		{
			//mesh
			wxTreeItemId par_item = GetItemParent(sel_item);
			if (par_item.IsOk())
			{
				LayerInfo* par_data = (LayerInfo*) GetItemData(par_item);
				if (par_data && par_data->type == 1)
				{
					//mesh in view
					wxString name = GetItemBaseText(par_item);
					VRenderView* vrv = vr_frame->GetView(name);
					if (vrv)
					{
						wxString group_name = vrv->AddGroup();
						AddGroupItem(par_item, group_name);
					}
				}
				else if (par_data && par_data->type == 6)
				{
					//mesh in group
					wxTreeItemId gpar_item = GetItemParent(par_item);
					if (gpar_item.IsOk())
					{
						LayerInfo* gpar_data = (LayerInfo*)GetItemData(gpar_item);
						if (gpar_data && gpar_data->type == 1)
						{
							wxString name = GetItemBaseText(gpar_item);
							VRenderView* vrv = vr_frame->GetView(name);
							if (vrv)
							{
								wxString group_name = vrv->AddGroup();
								AddGroupItem(gpar_item, group_name);
							}
						}
					}
				}
			}
		}
		else if ((item_data && item_data->type == 5) ||
			(item_data && item_data->type == 6))
		{
			//group
			wxTreeItemId par_item = GetItemParent(sel_item);
			if (par_item.IsOk())
			{
				LayerInfo* par_data = (LayerInfo*)GetItemData(par_item);
				if (par_data && par_data->type == 1)
				{
					//group in view
					wxString name = GetItemBaseText(par_item);
					VRenderView* vrv = vr_frame->GetView(name);
					if (vrv)
					{
						wxString group_name = vrv->AddGroup();
						AddGroupItem(par_item,group_name);
					}
				}
			}
		}
	}
}

void DataTreeCtrl::OnAddSegGroup(wxCommandEvent& event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	if (!sel_item.IsOk())
		return;

	LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
	if (!item_data)
		return;

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager *d_manage = vr_frame->GetDataManager();
	if (!d_manage) return;
	
	if (item_data->type == 2)
	{
		wxString vname = GetItemBaseText(sel_item);
		VolumeData* vd = d_manage->GetVolumeData(vname);
		if (!vd) return;
		int gid = vd->AddROIGroup();
		vd->AddSelID(gid);
		vr_frame->UpdateTree();
		TreePanel* tree_panel = vr_frame->GetTree();
		if (tree_panel)
			tree_panel->SelectROI(vd, gid);
	}
	else if (item_data->type == 7 || item_data->type == 8)
	{
		wxTreeItemId vitem = GetParentVolItem(sel_item);
		if (!vitem.IsOk()) return;
		wxString vname = GetItemBaseText(vitem);
		VolumeData* vd = d_manage->GetVolumeData(vname);
		if (!vd) return;

		wxTreeItemId pitem = GetItemParent(sel_item);
		if (!pitem.IsOk()) return;
		LayerInfo* pitem_data = (LayerInfo*)GetItemData(pitem);
		if (!pitem_data) return;

		wxString parent_name(wxT(""));
		if (item_data->type == 7)
		{
			if (pitem_data->type == 8)
				parent_name = GetItemBaseText(pitem);
		}
		else if (item_data->type == 8)
			parent_name = GetItemBaseText(sel_item);

		int gid = vd->AddROIGroup(parent_name.ToStdWstring());
		vd->AddSelID(gid);
		vr_frame->UpdateTree();
		TreePanel* tree_panel = vr_frame->GetTree();
		if (tree_panel)
			tree_panel->SelectROI(vd, gid);
	}
}

void DataTreeCtrl::OnExpand(wxCommandEvent &event)
{
	wxTreeItemId sel_item = GetSelection();
	if (IsExpanded(sel_item))
		Collapse(sel_item);
	else
		Expand(sel_item);
}

//edit
void DataTreeCtrl::OnEdit(wxCommandEvent &event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
		vr_frame->ShowPaintTool();
}

//measurement
void DataTreeCtrl::OnInfo(wxCommandEvent &event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
		vr_frame->ShowInfoDlg();
}

//trace
void DataTreeCtrl::OnTrace(wxCommandEvent &event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
		vr_frame->ShowTraceDlg();
}

//noise cancelling
void DataTreeCtrl::OnNoiseCancelling(wxCommandEvent &event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
		vr_frame->ShowNoiseCancellingDlg();
}

//counting
void DataTreeCtrl::OnCounting(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
		vr_frame->ShowCountingDlg();
}

//colocalzation
void DataTreeCtrl::OnColocalization(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
		vr_frame->ShowColocalizationDlg();
}

//convert
void DataTreeCtrl::OnConvert(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
		vr_frame->ShowConvertDlg();
}

//randomize color
void DataTreeCtrl::OnRandomizeColor(wxCommandEvent& event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	if (!sel_item.IsOk()) return;

	LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
	if (!item_data) return;

	wxString name = GetItemBaseText(sel_item);
	if (item_data->type == 1)
	{
		//view
		VRenderView* vrv = vr_frame->GetView(name);
		if (vrv)
			vrv->RandomizeColor();
	}
	else if (item_data->type == 2)
	{
		//volume
		VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
		if (vd)
			vd->RandomizeColor();
	}
	else if (item_data->type == 3)
	{
		//mesh
		MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
		if (md)
			md->RandomizeColor();
	}
	else if (item_data->type == 5)
	{
		//volume group
		wxString par_name = GetItemBaseText(GetItemParent(sel_item));
		VRenderView* vrv = vr_frame->GetView(par_name);
		if (vrv)
		{
			DataGroup* group = vrv->GetGroup(name);
			if (group)
				group->RandomizeColor();
		}
	}
	else if (item_data->type == 6)
	{
		//mesh group
		wxString par_name = GetItemBaseText(GetItemParent(sel_item));
		VRenderView* vrv = vr_frame->GetView(par_name);
		if (vrv)
		{
			MeshGroup* group = vrv->GetMGroup(name);
			if (group)
				group->RandomizeColor();
		}
	}

	m_scroll_pos = GetScrollPos(wxVERTICAL);
	vr_frame->UpdateTree(name);
	SetScrollPos(wxVERTICAL, m_scroll_pos);
	UpdateSelection();
	vr_frame->RefreshVRenderViews();
}

void DataTreeCtrl::SetSameColorToAllDatasetsInGroup(wxCommandEvent& event)
{
    if (m_fixed)
        return;

    wxTreeItemId sel_item = GetSelection();
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    if (!vr_frame) return;
    if (!sel_item.IsOk()) return;

    LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
    if (!item_data) return;

    wxString name = GetItemBaseText(sel_item);
    if (item_data->type == 2)
    {
        //volume
        VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
        if (vd)
        {
            Color c = vd->GetColor();
            
            wxTreeItemId par_item = GetItemParent(sel_item);
            wxTreeItemIdValue cookie;
            wxTreeItemId child_item = GetFirstChild(par_item, cookie);
            while (child_item.IsOk())
            {
                LayerInfo* c_item_data = (LayerInfo*)GetItemData(child_item);
                if (!c_item_data)
                    continue;
                wxString c_name = GetItemBaseText(child_item);
                VolumeData* c_vd = vr_frame->GetDataManager()->GetVolumeData(c_name);
                if (c_vd)
                    c_vd->SetColor(c);
                child_item = GetNextChild(par_item, cookie);
            }
        }
    }
    else if (item_data->type == 3)
    {
        // mesh
        MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
        if (md)
        {
            Color amb, diff, spec;
            double shine, alpha;
            md->GetMaterial(amb, diff, spec, shine, alpha);
            
            wxTreeItemId par_item = GetItemParent(sel_item);
            wxTreeItemIdValue cookie;
            wxTreeItemId child_item = GetFirstChild(par_item, cookie);
            while (child_item.IsOk())
            {
                LayerInfo* c_item_data = (LayerInfo*)GetItemData(child_item);
                if (!c_item_data)
                    continue;
                wxString c_name = GetItemBaseText(child_item);
                MeshData* c_md = vr_frame->GetDataManager()->GetMeshData(c_name);
                if (c_md)
                    c_md->SetMaterial(amb, diff, spec);
                child_item = GetNextChild(par_item, cookie);
            }
        }
    }
    else if (item_data->type == 5)
    {
        //volume group
        Color c;
        wxTreeItemIdValue cookie;
        wxTreeItemId child_item = GetFirstChild(sel_item, cookie);
        bool first = true;
        while (child_item.IsOk())
        {
            LayerInfo* c_item_data = (LayerInfo*)GetItemData(child_item);
            if (!c_item_data)
                continue;
            wxString c_name = GetItemBaseText(child_item);
            VolumeData* c_vd = vr_frame->GetDataManager()->GetVolumeData(c_name);
            if (c_vd)
                c_vd->SetColor(c);
            if (c_vd)
            {
                if (first)
                {
                    c = c_vd->GetColor();
                    first = false;
                }
                else
                    c_vd->SetColor(c);
            }
            child_item = GetNextChild(sel_item, cookie);
        }
    }
    else if (item_data->type == 6)
    {
        //mesh group
        Color amb, diff, spec;
        double shine, alpha;
        wxTreeItemIdValue cookie;
        wxTreeItemId child_item = GetFirstChild(sel_item, cookie);
        bool first = true;
        while (child_item.IsOk())
        {
            LayerInfo* c_item_data = (LayerInfo*)GetItemData(child_item);
            if (!c_item_data)
                continue;
            wxString c_name = GetItemBaseText(child_item);
            MeshData* c_md = vr_frame->GetDataManager()->GetMeshData(c_name);
            if (c_md)
            {
                if (first)
                {
                    c_md->GetMaterial(amb, diff, spec, shine, alpha);
                    first = false;
                }
                else
                    c_md->SetMaterial(amb, diff, spec);
            }
            child_item = GetNextChild(sel_item, cookie);
        }
    }

    m_scroll_pos = GetScrollPos(wxVERTICAL);
    vr_frame->UpdateTree(name);
    SetScrollPos(wxVERTICAL, m_scroll_pos);
    UpdateSelection();
    vr_frame->RefreshVRenderViews();
}

void DataTreeCtrl::OnExportAllSegments(wxCommandEvent& event)
{
	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (!vr_frame) return;
	if (!sel_item.IsOk()) return;

	//select data
	wxString name = GetItemBaseText(sel_item);
	LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);

	if (!item_data || item_data->type != 4) return;

	Annotations* ann = vr_frame->GetDataManager()->GetAnnotations(name);
	VolumeData *vd = ann->GetVolume();

	if (!vd) return;
	
	wxDirDialog *dirdlg = new wxDirDialog(
		m_frame, "Save segments in", "",
		wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
	int rval = dirdlg->ShowModal();
	wxString out_dir;
	if (rval == wxID_OK)
	{
		out_dir = dirdlg->GetPath();
		delete dirdlg;
	}
	else
	{
		delete dirdlg;
		return;
	}

	vd->ExportEachSegment(out_dir, ann->GetLabel());
}

void DataTreeCtrl::OnFlipH(wxCommandEvent& event)
{
	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (!vr_frame) return;
	if (!sel_item.IsOk()) return;

	//select data
	wxString name = GetItemBaseText(sel_item);
	LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);

	if (!item_data || item_data->type != 2) return;

	VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);

	if (!vd) return;

	vd->FlipHorizontally();
	vr_frame->RefreshVRenderViews();
}

void DataTreeCtrl::OnFlipV(wxCommandEvent& event)
{
	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (!vr_frame) return;
	if (!sel_item.IsOk()) return;

	//select data
	wxString name = GetItemBaseText(sel_item);
	LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);

	if (!item_data || item_data->type != 2) return;

	VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);

	if (!vd) return;

	vd->FlipVertically();
	vr_frame->RefreshVRenderViews();
}

void DataTreeCtrl::OnExportMeshMask(wxCommandEvent& event)
{
	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (!vr_frame) return;
	if (!sel_item.IsOk()) return;

	//select data
	wxString name = GetItemBaseText(sel_item);
	LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);

	if (!item_data || item_data->type != 2) return;

	VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
	if (!vd) return;

	MeshData *mesh = vd->ExportMeshMask();
	if (!mesh) return;

	DataManager *d_manage = vr_frame->GetDataManager();
	if (!d_manage) return;
	VRenderView* view = GetCurrentView();
	if (view)
	{
		DataManager *d_manage = vr_frame->GetDataManager();
		if (!d_manage) return;
		VRenderView* view = GetCurrentView();
		if (view)
		{
			d_manage->AddMeshData(mesh);
			view->AddMeshData(mesh);
			vr_frame->OnSelection(3, 0, 0, 0, mesh);
			vr_frame->UpdateTree(mesh->GetName());
		}
	}

	vr_frame->RefreshVRenderViews();
}

void DataTreeCtrl::OnToggleNAMode(wxCommandEvent& event)
{
    wxTreeItemId sel_item = GetSelection();
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    
    if (!vr_frame) return;
    if (!sel_item.IsOk()) return;
    
    //select data
    wxString name = GetItemBaseText(sel_item);
    LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
    
    if (!item_data || item_data->type != 2) return;
    
    VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
    if (!vd) return;
    
    vd->SetNAMode(!vd->GetNAMode());
}

//
void DataTreeCtrl::UpdateSelection()
{
	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	notifyAll(0);

	if (!vr_frame)
		return;

	//clear volume A for all views
	for (int i=0; i<vr_frame->GetViewNum(); i++)
	{
		VRenderView* vrv = vr_frame->GetView(i);
		if (vrv)
			vrv->SetVolumeA(0);
	}

	if (sel_item.IsOk())
	{
		//select data
		wxString name = GetItemBaseText(sel_item);
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			switch (item_data->type)
			{
			case 0://root
				vr_frame->OnSelection(0);
				break;
			case 1://view
				//vr_frame->OnSelection(0);
				{
					wxString str = GetItemBaseText(sel_item);
					VRenderView* vrv = vr_frame->GetView(str);
					vr_frame->OnSelection(1, vrv, 0, 0, 0);
				}
				break;
			case 2://volume data
				{
					if (vr_frame->GetAdjustView())
					{
						wxTreeItemId par_item = GetItemParent(sel_item);
						if (par_item.IsOk())
						{
							LayerInfo* par_item_data = (LayerInfo*)GetItemData(par_item);
							if (par_item_data && par_item_data->type == 5)
							{
								//par is group
								wxString str = GetItemBaseText(GetItemParent(par_item));
								VRenderView* vrv = vr_frame->GetView(str);
								if (vrv)
								{
									VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
									str = GetItemBaseText(par_item);
									DataGroup* group = vrv->GetGroup(str);
									vr_frame->GetAdjustView()->SetGroupLink(group);
									vr_frame->OnSelection(2, vrv, group, vd, 0);
									vrv->SetVolumeA(vd);
									vr_frame->GetBrushToolDlg()->GetSettings(vrv);
									vr_frame->GetMeasureDlg()->GetSettings(vrv, false);
									vr_frame->GetTraceDlg()->GetSettings(vrv);
                                    vr_frame->GetConvertDlg()->GetSettings(vrv);
								}
							}
							else if (par_item_data && par_item_data->type == 1)
							{
								//par is view
								wxString str = GetItemBaseText(par_item);
								VRenderView* vrv = vr_frame->GetView(str);
								if (vrv)
								{
									VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
									vr_frame->GetAdjustView()->SetGroupLink(0);
									vr_frame->OnSelection(2, vrv, 0, vd);
									vrv->SetVolumeA(vd);
									vr_frame->GetBrushToolDlg()->GetSettings(vrv);
									vr_frame->GetMeasureDlg()->GetSettings(vrv, false);
									vr_frame->GetTraceDlg()->GetSettings(vrv);
                                    vr_frame->GetConvertDlg()->GetSettings(vrv);
								}
							}
						}
					}
				}
				break;
			case 3://mesh data
				{
					wxTreeItemId par_item = GetItemParent(sel_item);
					if (par_item.IsOk())
					{
						LayerInfo* par_item_data = (LayerInfo*)GetItemData(par_item);
						if (par_item_data && par_item_data->type == 6)
						{
							//par is group
							wxString str = GetItemBaseText(GetItemParent(par_item));
							VRenderView* vrv = vr_frame->GetView(str);
							if (vrv)
							{
								MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
                                if (md->isTree())
                                    md->SelectSegment();
								vr_frame->OnSelection(3, vrv, 0, 0, md);
							}
						}
						else if (par_item_data && par_item_data->type == 1)
						{
							//par is view
							wxString str = GetItemBaseText(par_item);
							VRenderView* vrv = vr_frame->GetView(str);
							if (vrv)
							{
								MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
                                if (md->isTree())
                                    md->SelectSegment();
								vr_frame->OnSelection(3, vrv, 0, 0, md);
							}
						}
					}
				}
				break;
			case 4://annotations
				{
					wxString par_name = GetItemBaseText(GetItemParent(sel_item));
					VRenderView* vrv = vr_frame->GetView(par_name);
					Annotations* ann = vr_frame->GetDataManager()->GetAnnotations(name);
					vr_frame->OnSelection(4, vrv, 0, 0, 0, ann);
				}
				break;
			case 5://group
				{
					wxString par_name = GetItemBaseText(GetItemParent(sel_item));
					VRenderView* vrv = vr_frame->GetView(par_name);
					if (vrv)
					{
						DataGroup* group = vrv->GetGroup(name);
						vr_frame->OnSelection(5, vrv, group);
					}
				}
				break;
			case 6://mesh group
				{
					wxString par_name = GetItemBaseText(GetItemParent(sel_item));
					VRenderView* vrv = vr_frame->GetView(par_name);
					if (vrv)
					{
						vr_frame->OnSelection(0);
					}
				}
				break;
			case 7://volume segments
			case 8://segment group
				{
					wxTreeItemId vitem = GetParentVolItem(sel_item);
					if (!vitem.IsOk()) break;
					wxString vname = GetItemBaseText(vitem);
					LayerInfo* vitem_data = (LayerInfo*)GetItemData(vitem);
					if (!vitem_data) break;
					VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(vname);
					if (!vd) break;
					int id = vd->GetROIid(name.ToStdWstring());
					vd->SetEditSelID(id);
					
					if (vr_frame->GetAdjustView())
					{
						wxTreeItemId par_item = GetItemParent(vitem);
						if (par_item.IsOk())
						{
							LayerInfo* par_item_data = (LayerInfo*)GetItemData(par_item);
							if (par_item_data && par_item_data->type == 5)
							{
								//par is group
								wxString str = GetItemBaseText(GetItemParent(par_item));
								VRenderView* vrv = vr_frame->GetView(str);
								if (vrv)
								{
									str = GetItemBaseText(par_item);
									DataGroup* group = vrv->GetGroup(str);
									vr_frame->GetAdjustView()->SetGroupLink(group);
									vr_frame->OnSelection(2, vrv, group, vd, 0);
									vrv->SetVolumeA(vd);
									vr_frame->GetBrushToolDlg()->GetSettings(vrv);
									vr_frame->GetMeasureDlg()->GetSettings(vrv);
									vr_frame->GetTraceDlg()->GetSettings(vrv);
								}
							}
							else if (par_item_data && par_item_data->type == 1)
							{
								//par is view
								wxString str = GetItemBaseText(par_item);
								VRenderView* vrv = vr_frame->GetView(str);
								if (vrv)
								{
									vr_frame->GetAdjustView()->SetGroupLink(0);
									vr_frame->OnSelection(2, vrv, 0, vd);
									vrv->SetVolumeA(vd);
									vr_frame->GetBrushToolDlg()->GetSettings(vrv);
									vr_frame->GetMeasureDlg()->GetSettings(vrv);
									vr_frame->GetTraceDlg()->GetSettings(vrv);
								}
							}
						}
					}
				}
				break;
            case 9://mesh segments
            case 10://mesh segment group
                {
                    wxTreeItemId mitem = GetParentMeshItem(sel_item);
                    if (!mitem.IsOk()) break;
                    wxString mname = GetItemBaseText(mitem);
                    LayerInfo* mitem_data = (LayerInfo*)GetItemData(mitem);
                    if (!mitem_data) break;
                    MeshData* md = vr_frame->GetDataManager()->GetMeshData(mname);
                    if (!md) break;
                    int id = item_data->id;
                    md->SelectSegment(id);
                        
                    if (vr_frame->GetAdjustView())
                    {
                        wxTreeItemId par_item = GetItemParent(mitem);
                        if (par_item.IsOk())
                        {
                            LayerInfo* par_item_data = (LayerInfo*)GetItemData(par_item);
                            if (par_item_data && par_item_data->type == 6)
                            {
                                //par is group
                                wxString str = GetItemBaseText(GetItemParent(par_item));
                                VRenderView* vrv = vr_frame->GetView(str);
                                if (vrv)
                                {
                                    vr_frame->OnSelection(3, vrv, 0, 0, md);
                                }
                            }
                            else if (par_item_data && par_item_data->type == 1)
                            {
                                //par is view
                                wxString str = GetItemBaseText(par_item);
                                VRenderView* vrv = vr_frame->GetView(str);
                                if (vrv)
                                {
                                    vr_frame->OnSelection(3, vrv, 0, 0, md);
                                }
                            }
                        }
                    }
                }
                break;
                    
			}
		}
	}
}

wxString DataTreeCtrl::GetCurrentSel()
{
	wxTreeItemId sel_item = GetSelection();
	if (sel_item.IsOk())
	{
		return GetItemBaseText(sel_item);
	}

	return "";
}

int DataTreeCtrl::GetCurrentSelType()
{
	wxTreeItemId sel_item = GetSelection();
	if (sel_item.IsOk())
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
			return item_data->type;
	}

	return -1;
}

int DataTreeCtrl::TraversalSelect(wxTreeItemId item, wxString name)
{
	int found = 0;
	wxTreeItemIdValue cookie;
	wxTreeItemId child_item = GetFirstChild(item, cookie);
	if (child_item.IsOk())
		found = TraversalSelect(child_item, name);
	child_item = GetNextChild(item, cookie);
	while (!found && child_item.IsOk())
	{
		found = TraversalSelect(child_item, name);
		child_item = GetNextChild(item, cookie);
	}

	wxString item_name = GetItemBaseText(item);
	if (item_name == name)
	{
		found = 1;
		SelectItem(item);
	}
	return found;
}

void DataTreeCtrl::Select(wxString view, wxString name)
{
	wxTreeItemIdValue cookie;
	wxTreeItemId root = GetRootItem();
	if (root.IsOk())
	{
		int found = 0;
		wxTreeItemId view_item = GetFirstChild(root, cookie);
		if (view_item.IsOk())
		{
			wxString view_name = GetItemBaseText(view_item);
			if (view_name == view ||
				view == "")
			{
				if (name == "")
				{
					SelectItem(view_item);
					found = 1;
				}
				else
					found = TraversalSelect(view_item, name);
			}
		}
		view_item = GetNextChild(root, cookie);
		while (!found && view_item.IsOk())
		{
			wxString view_name = GetItemBaseText(view_item);
			if (view_name == view ||
				view == "")
			{
				if (name == "")
				{
					SelectItem(view_item);
					found = 1;
				}
				else
					found = TraversalSelect(view_item, name);
			}
			view_item = GetNextChild(root, cookie);
		}

		if (!found)
			SelectItem(GetRootItem());
	}
}

void DataTreeCtrl::SelectROI(VolumeData* vd, int id)
{
	if (!vd)
		return;
	
	wxString rname = vd->GetROIName(id);
	wxTreeItemId vitem = FindTreeItem(vd->GetName());
	if (!vitem.IsOk())
			return;
	
	if (rname.IsEmpty())
	{
		wxTreeItemId sel_item = GetSelection();
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		bool sel_sgroup = false;
		if (sel_item.IsOk() && vr_frame)
		{
			LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
			wxTreeItemId sel_vitem = GetParentVolItem(sel_item);
			if (item_data && sel_vitem.IsOk())
			{
				if (GetItemBaseText(vitem) == GetItemBaseText(sel_vitem) && item_data->id < -1)
					sel_sgroup = true;
			}
		}

		if (!sel_sgroup) SelectItem(vitem);
	}
	else
	{
		wxTreeItemId ritem = FindTreeItem(vitem, rname, true);
		if (!ritem.IsOk())
			SelectItem(vitem);
		else
		{		
			int old_sposy = GetScrollPos(wxVERTICAL);
			TraversalExpand(ritem);
			SelectItem(ritem);
			int new_sposy = GetScrollPos(wxVERTICAL);
			if (old_sposy < new_sposy)
				SetScrollPos(wxVERTICAL, new_sposy-1);
		}
	}
}

void DataTreeCtrl::SelectROI(MeshData* md, int id)
{
    if (!md || id < 0)
        return;
    
    wxTreeItemId mitem = FindTreeItem(md->GetName());
    if (!mitem.IsOk())
            return;
    
    wxTreeItemId ritem = FindTreeItemBySegmentID(mitem, id);
    if (!ritem.IsOk())
    {
        wxString path = md->GetROIPath(id);
        if (path.IsEmpty())
            SelectItem(mitem);
        else
        {
            wxTreeItemId pitem;
            path = path.BeforeLast('.');
            vector<int> pids;
            while (!path.IsEmpty())
            {
                int pid = md->GetROIid(path.ToStdWstring());
                pitem = FindTreeItemBySegmentID(mitem, pid);
                if (pitem.IsOk())
                    break;
                pids.push_back(pid);
                path = path.BeforeLast('.');
            }
            if (!pitem.IsOk())
                pitem = mitem;
            ExpandROITree(pitem);
            for (auto i = pids.rbegin(); i != pids.rend(); ++i)
            {
                pitem = FindTreeItemBySegmentID(pitem, *i);
                if (pitem.IsOk())
                    ExpandROITree(pitem);
                else
                    break;
            }
            
            ritem = FindTreeItemBySegmentID(pitem, id);
            if (ritem.IsOk())
            {
                int old_sposy = GetScrollPos(wxVERTICAL);
                SetEvtHandlerEnabled(false);
                TraversalExpand(ritem);
                SetEvtHandlerEnabled(true);
                SelectItem(ritem);
                int new_sposy = GetScrollPos(wxVERTICAL);
                if (old_sposy < new_sposy)
                    SetScrollPos(wxVERTICAL, new_sposy-1);
            }
        }
    }
    else
    {
        int old_sposy = GetScrollPos(wxVERTICAL);
        SetEvtHandlerEnabled(false);
        TraversalExpand(ritem);
        SetEvtHandlerEnabled(true);
        SelectItem(ritem);
        int new_sposy = GetScrollPos(wxVERTICAL);
        if (old_sposy < new_sposy)
            SetScrollPos(wxVERTICAL, new_sposy-1);
    }
}

void DataTreeCtrl::OnSelChanged(wxTreeEvent& event)
{
	UpdateSelection();
}

void DataTreeCtrl::OnSelChanging(wxTreeEvent &event)
{
	if (m_fixed)
		event.Veto();
}

void DataTreeCtrl::OnDeleting(wxTreeEvent& event)
{
	UpdateSelection();
}

void DataTreeCtrl::OnAct(wxTreeEvent &event)
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	wxString name = "";
	bool rc = wxGetKeyState(WXK_CONTROL);

	if (sel_item.IsOk() && vr_frame)
	{
		name = GetItemBaseText(sel_item);
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			PushVisHistory();
			switch (item_data->type)
			{
			case 1://view
				{
					VRenderView* vrv = vr_frame->GetView(name);
					if (vrv)
					{
						if (rc)
							vrv->RandomizeColor();
						else
							vrv->ToggleDraw();
					}
				}
				break;
			case 2://volume data
				{
					VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
					if (vd)
					{
						if (rc)
							vd->RandomizeColor();
						else
						{
							vd->ToggleDisp();
							for (int i=0; i<vr_frame->GetViewNum(); i++)
							{
								VRenderView* vrv = vr_frame->GetView(i);
								if (vrv)
									vrv->SetVolPopDirty();
							}
						}
					}
				}
				break;
			case 3://mesh data
				{
					MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
					if (md)
					{
						if (rc)
							md->RandomizeColor();
						else
						{
							md->ToggleDisp();
							for (int i=0; i<vr_frame->GetViewNum(); i++)
							{
								VRenderView* vrv = vr_frame->GetView(i);
								if (vrv)
									vrv->SetMeshPopDirty();
							}
						}
					}
				}
				break;
			case 4://annotations
				{
					Annotations* ann = vr_frame->GetDataManager()->GetAnnotations(name);
					if (ann)
					{
						ann->ToggleDisp();
						vr_frame->GetMeasureDlg()->UpdateList();
					}
				}
				break;
			case 5://group
				{
					wxString par_name = GetItemBaseText(GetItemParent(sel_item));
					VRenderView* vrv = vr_frame->GetView(par_name);
					if (vrv)
					{
						DataGroup* group = vrv->GetGroup(name);
						if (group)
						{
							if (rc)
								group->RandomizeColor();
							else
							{
								group->ToggleDisp();
								vrv->SetVolPopDirty();
							}
						}
					}
				}
				break;
			case 6://mesh group
				{
					wxString par_name = GetItemBaseText(GetItemParent(sel_item));
					VRenderView* vrv = vr_frame->GetView(par_name);
					if (vrv)
					{
						MeshGroup* group = vrv->GetMGroup(name);
						if (group)
						{
							if (rc)
								group->RandomizeColor();
							else
							{
								group->ToggleDisp();
								vrv->SetMeshPopDirty();
							}
						}
					}
				}
				break;
			case 7://volume segment
			case 8://segment group
				{
					wxTreeItemId vol_item = GetParentVolItem(sel_item);
					if (vol_item.IsOk())
					{
						wxString vname = GetItemBaseText(vol_item);
						VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(vname);
						if (vd)
						{
							int id = vd->GetROIid(GetItemBaseText(sel_item).ToStdWstring());
							if (id != -1)
								vd->SetROISel(GetItemBaseText(sel_item).ToStdWstring(), !(vd->isSelID(id)));
						}
					}
				}
				break;
            case 9://mesh segment
            case 10://mesh segment group
                {
                    wxTreeItemId mesh_item = GetParentMeshItem(sel_item);
                    if (mesh_item.IsOk())
                    {
                        wxString mname = GetItemBaseText(mesh_item);
                        MeshData* md = vr_frame->GetDataManager()->GetMeshData(mname);
                        if (md)
                        {
                            LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
                            md->ToggleROIState(item_data->id);
                            if (vr_frame->GetMPropView())
                            {
                                bool state = md->GetROIState(item_data->id);
                                if (vr_frame->GetMPropView()->GetSyncGroup())
                                    md->SetROIStateSiblings(item_data->id, state);
                                if (vr_frame->GetMPropView()->GetSyncName())
                                    md->SetRoiStateByName(item_data->id, state);
                            }
                        }
                    }
                }
                break;
			}
		}

		m_scroll_pos = GetScrollPos(wxVERTICAL);
		if (rc)
			vr_frame->UpdateTree(name, item_data->type);
		else
			vr_frame->UpdateTreeIcons();
		SetScrollPos(wxVERTICAL, m_scroll_pos);
		UpdateSelection();
		vr_frame->RefreshVRenderViews(false, true);
	}
}

void DataTreeCtrl::OnBeginDrag(wxTreeEvent& event)
{
	if (m_fixed)
		return;

	//remember pos
	m_scroll_pos = GetScrollPos(wxVERTICAL);

	m_drag_item = event.GetItem();
	m_drag_nb_item = wxTreeItemId();
	m_insert_mode = -1;
	if (m_drag_item.IsOk())
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(m_drag_item);
		if (item_data)
		{
			switch (item_data->type)
			{
			case 0://root
				break;
			case 1://view
				break;
			case 2://volume data
				event.Allow();
				break;
			case 3://mesh data
				event.Allow();
				break;
			case 4://annotations
				event.Allow();
				break;
			case 5://group
				event.Allow();
				break;
			case 6://mesh group
				event.Allow();
				break;
			case 7://volume segment
				event.Allow();
				break;
			case 8://segment group
				event.Allow();
				break;
			}
		}
	}
	Connect(wxEVT_MOTION, wxMouseEventHandler(DataTreeCtrl::OnDragging), NULL, this);
}

void DataTreeCtrl::OnEndDrag(wxTreeEvent& event)
{
	if (m_fixed)
		return;

	int mouse_pos_y = event.GetPoint().y;

	wxTreeItemId src_item = m_drag_item,
		dst_item = event.GetItem(),
		src_par_item = src_item.IsOk()?GetItemParent(src_item):0,
		dst_par_item = dst_item.IsOk()?GetItemParent(dst_item):0;
	m_drag_item = (wxTreeItemId)0l;
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	SetEvtHandlerEnabled(false);
	Freeze();

	if (src_item.IsOk() && dst_item.IsOk() &&
		src_par_item.IsOk() &&
		dst_par_item.IsOk() &&
		vr_frame)
	{
		LayerInfo* src_item_data = (LayerInfo*)GetItemData(src_item);
		LayerInfo* src_par_item_data = (LayerInfo*)GetItemData(src_par_item);
		LayerInfo* dst_item_data = (LayerInfo*)GetItemData(dst_item);
		LayerInfo* dst_par_item_data = (LayerInfo*)GetItemData(dst_par_item);

		wxRect dst_item_rect;
		double dst_center_y = -1.0;
		if (GetBoundingRect(dst_item, dst_item_rect))
			dst_center_y = (dst_item_rect.GetTop() + dst_item_rect.GetBottom()) / 2.0;
		//insert_mode: 0-before dst; 1-after dst;
		int insert_mode = 0;
		if (dst_center_y >= 0.0 && mouse_pos_y > dst_center_y)
			insert_mode = 1;
		
		if (src_item_data && src_par_item_data &&
			dst_item_data && dst_par_item_data)
		{

			int src_type = src_item_data->type;
			int src_par_type = src_par_item_data->type;
			int dst_type = dst_item_data->type;
			int dst_par_type = dst_par_item_data->type;

			wxString src_name = GetItemBaseText(src_item);
			wxString src_par_name = GetItemBaseText(src_par_item);
			wxString dst_name = GetItemBaseText(dst_item);
			wxString dst_par_name = GetItemBaseText(dst_par_item);

			VolumeData *vd = NULL;

			if (src_par_type == 1 &&
				dst_par_type == 1 &&
				src_par_name == dst_par_name &&
				src_name != dst_name)
			{
				//move within the same view
				if (src_type == 2 && dst_type == 5)
				{
					//move volume to the group in the same view
					VRenderView* vrv = vr_frame->GetView(src_par_name);
					if (vrv)
					{
						wxString str("");
						vrv->MoveLayertoGroup(dst_name, src_name, str);
					}
				}
				else if (src_type==3 && dst_type==6)
				{
					//move mesh into a group
					VRenderView* vrv = vr_frame->GetView(src_par_name);
					if (vrv)
					{
						wxString str("");
						vrv->MoveMeshtoGroup(dst_name, src_name, str);
					}
				}
				else
				{
					VRenderView* vrv = vr_frame->GetView(src_par_name);
					if (vrv)
						vrv->MoveLayerinView(src_name, dst_name);
				}
			}
			else if (src_par_type == 5 &&
				dst_par_type == 5 &&
				src_par_name == dst_par_name &&
				src_name != dst_name)
			{
				//move volume within the same group
				wxString str = GetItemBaseText(GetItemParent(src_par_item));
				VRenderView* vrv = vr_frame->GetView(str);
				if (vrv)
					vrv->MoveLayerinGroup(src_par_name, src_name, dst_name, insert_mode);
			}
			else if (src_par_type == 5 && //par is group
				src_type == 2 && //src is volume
				dst_par_type == 1 && //dst's par is view
				dst_par_name == GetItemBaseText(GetItemParent(src_par_item))) //in same view
			{
				//move volume outside of the group
				if (dst_type == 5) //dst is group
				{
					VRenderView* vrv = vr_frame->GetView(dst_par_name);
					if (vrv)
					{
						wxString str("");
						vrv->MoveLayerfromtoGroup(src_par_name, dst_name, src_name, str);
					}
				}
				else
				{
					VRenderView *vrv = vr_frame->GetView(dst_par_name);
					if (vrv)
						vrv->MoveLayertoView(src_par_name, src_name, dst_name);
				}
			}
			else if (src_par_type == 1 && //src's par is view
				src_type == 2 && //src is volume
				dst_par_type == 5 && //dst's par is group
				src_par_name == GetItemBaseText(GetItemParent(dst_par_item))) //in the same view
			{
				//move volume into group
				VRenderView* vrv = vr_frame->GetView(src_par_name);
				if (vrv)
					vrv->MoveLayertoGroup(dst_par_name, src_name, dst_name);
			}
			else if (src_par_type == 5 && //src's par is group
				src_type == 2 && // src is volume
				dst_par_type == 5 && //dst's par is group
				dst_type == 2 && //dst is volume
				GetItemBaseText(GetItemParent(src_par_item)) == GetItemBaseText(GetItemParent(dst_par_item)) && // in the same view
				GetItemBaseText(src_par_item) != GetItemBaseText(dst_par_item))// par groups are different
			{
				//move volume from one group to another
				wxString str = GetItemBaseText(GetItemParent(src_par_item));
				VRenderView* vrv = vr_frame->GetView(str);
				if (vrv)
					vrv->MoveLayerfromtoGroup(src_par_name, dst_par_name, src_name, dst_name, insert_mode);
			}
			else if (src_type == 2 && //src is volume
				src_par_type == 5 && //src's par is group
				dst_type == 1 && //dst is view
				GetItemBaseText(GetItemParent(src_par_item)) == dst_name) //in the same view
			{
				//move volume outside of the group
				VRenderView* vrv = vr_frame->GetView(dst_name);
				if (vrv)
				{
					wxString str("");
					vrv->MoveLayertoView(src_par_name, src_name, str);
				}
			}
			else if (src_par_type == 6 &&
				dst_par_type == 6 &&
				src_par_name == dst_par_name &&
				src_name != dst_name)
			{
				//move mesh within the same group
				wxString str = GetItemBaseText(GetItemParent(src_par_item));
				VRenderView* vrv = vr_frame->GetView(str);
				if (vrv)
					vrv->MoveMeshinGroup(src_par_name, src_name, dst_name, insert_mode);
			}
			else if (src_par_type == 6 && //par is group
				src_type == 3 && //src is mesh
				dst_par_type == 1 && //dst's par is view
				dst_par_name == GetItemBaseText(GetItemParent(src_par_item))) //in same view
			{
				//move mesh outside of the group
				if (dst_type == 6) //dst is group
				{
					VRenderView* vrv = vr_frame->GetView(dst_par_name);
					if (vrv)
					{
						wxString str("");
						vrv->MoveMeshfromtoGroup(src_par_name, dst_name, src_name, str);
					}
				}
				else
				{
					VRenderView *vrv = vr_frame->GetView(dst_par_name);
					if (vrv)
						vrv->MoveMeshtoView(src_par_name, src_name, dst_name);
				}
			}
			else if (src_par_type == 1 && //src's par is view
				src_type == 3 && //src is mesh
				dst_par_type == 6 && //dst's par is group
				src_par_name == GetItemBaseText(GetItemParent(dst_par_item))) //in the same view
			{
				//move mesh into group
				VRenderView* vrv = vr_frame->GetView(src_par_name);
				if (vrv)
					vrv->MoveMeshtoGroup(dst_par_name, src_name, dst_name);
			}
			else if (src_par_type == 6 && //src's par is group
				src_type == 3 && // src is mesh
				dst_par_type == 6 && //dst's par is group
				dst_type == 3 && //dst is mesh
				GetItemBaseText(GetItemParent(src_par_item)) == GetItemBaseText(GetItemParent(dst_par_item)) && // in the same view
				GetItemBaseText(src_par_item) != GetItemBaseText(dst_par_item))// par groups are different
			{
				//move mesh from one group to another
				wxString str = GetItemBaseText(GetItemParent(src_par_item));
				VRenderView* vrv = vr_frame->GetView(str);
				if (vrv)
					vrv->MoveMeshfromtoGroup(src_par_name, dst_par_name, src_name, dst_name, insert_mode);
			}
			else if (src_type == 3 && //src is mesh
				src_par_type == 6 && //src's par is group
				dst_type == 1 && //dst is view
				GetItemBaseText(GetItemParent(src_par_item)) == dst_name) //in the same view
			{
				//move mesh outside of the group
				VRenderView* vrv = vr_frame->GetView(dst_name);
				if (vrv)
				{
					wxString str("");
					vrv->MoveMeshtoView(src_par_name, src_name, str);
				}
			}
			else if ( (src_type == 7 || src_type == 8) && (dst_type == 7 || dst_type == 8))
			{
				wxTreeItemId s_vitem = GetParentVolItem(src_item);
				wxTreeItemId d_vitem = GetParentVolItem(dst_item);
				if (s_vitem.IsOk() && d_vitem.IsOk() && s_vitem == d_vitem && vr_frame->GetDataManager())
				{
					vd = vr_frame->GetDataManager()->GetVolumeData(GetItemBaseText(s_vitem));
					if (vd && m_insert_mode != -1)
						vd->MoveROINode(src_item_data->id, dst_item_data->id, m_insert_mode);
				}
			}
			else if ( (src_type == 7 || src_type == 8) && (dst_type == 2))
			{
				wxTreeItemId s_vitem = GetParentVolItem(src_item);
				if (s_vitem.IsOk() && s_vitem == dst_item && vr_frame->GetDataManager())
				{
					vd = vr_frame->GetDataManager()->GetVolumeData(GetItemBaseText(s_vitem));
					if (vd)
						vd->MoveROINode(src_item_data->id, -1);
				}
			}

			if ((src_type == 7 || src_type == 8) && vd)
			{
				int roi_id = src_item_data->id;
				vr_frame->UpdateTree();
				SelectROI(vd, roi_id);
			}
			else
				vr_frame->UpdateTree(src_name, src_type);
			vr_frame->RefreshVRenderViews();
		}
	}
	else if (src_item.IsOk() && src_par_item.IsOk() &&
		!dst_item.IsOk() && vr_frame)
	{
		LayerInfo* src_item_data = (LayerInfo*)GetItemData(src_item);
		LayerInfo* src_par_item_data = (LayerInfo*)GetItemData(src_par_item);

		if (src_item_data && src_par_item_data)
		{
			//move volume out of the group
			int src_type = src_item_data->type;
			int src_par_type = src_par_item_data->type;

			wxString src_name = GetItemBaseText(src_item);
			wxString src_par_name = GetItemBaseText(src_par_item);

			if (src_type == 2 && src_par_type == 5)
			{
				wxString str = GetItemBaseText(GetItemParent(src_par_item));
				VRenderView* vrv = vr_frame->GetView(str);
				if (vrv)
				{
					wxString str("");
					vrv->MoveLayertoView(src_par_name, src_name, str);

					vr_frame->UpdateTree(src_name, src_type);
					vr_frame->RefreshVRenderViews();
				}
			}
		}
	}

	SetScrollPos(wxVERTICAL, m_scroll_pos);
	Disconnect(wxEVT_MOTION, wxMouseEventHandler(DataTreeCtrl::OnDragging));

	UpdateSelection();

	Thaw();
	SetEvtHandlerEnabled(true);
}

void DataTreeCtrl::OnDragging(wxMouseEvent& event)
{
	wxPoint pos = event.GetPosition();
	int flags = wxTREE_HITTEST_ONITEM;
	wxTreeItemId item = HitTest(pos, flags); // got to use it at last
	if (item.IsOk() && m_drag_item.IsOk() && item != m_drag_item)
	{
		LayerInfo *item_data = (LayerInfo *)GetItemData(item);
		LayerInfo *d_item_data = (LayerInfo *)GetItemData(m_drag_item);

		if (!item_data || !d_item_data)
			return;

		if (d_item_data->type != 7 && d_item_data->type != 8)
		{
			if (item_data->type == 7 || item_data->type == 8)
				SetItemDropHighlight(item, false);
		}
		else
		{
			wxTreeItemId vitem = GetParentVolItem(item);
			wxTreeItemId vditem = GetParentVolItem(m_drag_item);
			if ((item_data->type != 7 && item_data->type != 8) ||
				!vitem.IsOk() || !vditem.IsOk() || vitem != vditem)
			{
//				SetItemDropHighlight(m_drag_nb_item, false);
				SetItemDropHighlight(item, false);
				m_drag_nb_item = wxTreeItemId();
				return;
			}

			SetEvtHandlerEnabled(false);
			//Freeze();

			wxRect rect;
			int center_y = -1;
			if (GetBoundingRect(item, rect))
				center_y = (rect.GetTop() + rect.GetBottom()) / 2.0;
			wxTreeItemId nb;
			int nexty = rect.GetHeight() * 2 / 3;
			//insert_mode: 0-before dst; 1-after dst; 2-into group;
			if (center_y >= 0)
			{
				if (item_data->type == 7)
				{
					if (pos.y > center_y)
					{
						nb = HitTest(wxPoint(pos.x, pos.y+nexty), flags);
						m_insert_mode = 1;
					}
					else
					{
						nb = HitTest(wxPoint(pos.x, pos.y-nexty), flags);
						m_insert_mode = 0;
					}
				}
				else if (item_data->type == 8)
				{
					if (pos.y > center_y+rect.GetHeight()/4)
					{
						nb = HitTest(wxPoint(pos.x, pos.y+nexty), flags);
						m_insert_mode = 1;
						SetItemDropHighlight(item, false);
					}
					else if (pos.y < center_y-rect.GetHeight()/4)
					{
						nb = HitTest(wxPoint(pos.x, pos.y-nexty), flags);
						m_insert_mode = 0;
						SetItemDropHighlight(item, false);
					}
					else
					{
						m_insert_mode = 2;
						SetItemDropHighlight(item, true);
					}
				}
			}

			bool found = false;
			wxTreeItemId par = GetItemParent(item);
			if (par.IsOk() && nb.IsOk())
			{
				wxTreeItemIdValue cookie;
				wxTreeItemId child_item = GetFirstChild(par, cookie);
				if (child_item.IsOk() && child_item == nb)
					found = true;
				else
				{
					child_item = GetNextChild(par, cookie);
					while (child_item.IsOk() && !found)
					{
						if (child_item == nb)
							found = true;
						child_item = GetNextChild(par, cookie);
					}
				}
			}

//			if (m_drag_nb_item.IsOk() && m_drag_nb_item != item)
//				SetItemDropHighlight(m_drag_nb_item, false);

			if(nb.IsOk() && found)
			{
//				SetItemDropHighlight(nb);
				m_drag_nb_item = nb;
			}
			else
				m_drag_nb_item = wxTreeItemId();

			//Thaw();
			SetEvtHandlerEnabled(true);
		}
	}
}

void DataTreeCtrl::OnKeyDown(wxKeyEvent& event)
{
	if ( event.GetKeyCode() == WXK_DELETE ||
		event.GetKeyCode() == WXK_BACK)
		DeleteSelection();
	//event.Skip();
}

void DataTreeCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

//icons
void DataTreeCtrl::AppendIcon()
{
	wxImageList *images = GetImageList();
	if (!images)
		return;

	wxIcon icon0 = wxIcon(cross_xpm);
	wxIcon icon1 = wxIcon(tick_xpm);
	images->Add(icon0);
	images->Add(icon1);
}

void DataTreeCtrl::ClearIcons()
{
	wxImageList *images = GetImageList();
	if (!images)
		return;

	images->RemoveAll();
}

int DataTreeCtrl::GetIconNum()
{
	wxImageList* images = GetImageList();
	if (images)
		return images->GetImageCount()/2;
	else
		return 0;
}

void DataTreeCtrl::ChangeIconColor(int i, wxColor c)
{
	ChangeIconColor(i*2, c, 0);
	ChangeIconColor(i*2+1, c, 1);
}

void DataTreeCtrl::ChangeIconColor(int which, wxColor c, int type)
{
	int i;
	int icon_lines = 0;
	int icon_height = 0;

	wxImageList *images = GetImageList();
	if (!images)
		return;

	const char **orgn_data = type?tick_xpm:cross_xpm;
	char cc[8];

	int dummy;
	SSCANF(orgn_data[0], "%d %d %d %d",
		&dummy, &icon_height, &icon_lines, &dummy);
	icon_lines += icon_height+1;
	char **data = new char*[icon_lines];

	sprintf(cc, "#%02X%02X%02X", c.Red(), c.Green(), c.Blue());

	for (i=0; i<icon_lines; i++)
	{
		if (i==icon_change)
		{
			int len = strlen(orgn_data[i]);
			int len_key = strlen(icon_key);
			int len_chng = len+strlen(cc)-len_key+1;
			data[i] = new char[len_chng];
			memset(data[i], 0, len_chng);
			char *temp = new char[len_key+1];
			memset(temp, 0, len_key+1);
			int index = 0;
			for (int j=0; j<len; j++)
			{
				char val = orgn_data[i][j];
				if (j>=len_key-1)
				{
					for (int k=0; k<len_key; k++)
						temp[k] = orgn_data[i][j-(len_key-k-1)];
					if (!strcmp(temp, icon_key))
					{
						strcpy(data[i]+index-len_key+1, cc);
						index = index-len_key+1+strlen(cc);
						continue;
					}
				}
				data[i][index++] = val;
			}
			delete [] temp;
		}
		else
		{
			int len = strlen(orgn_data[i]);
			data[i] = new char[len+1];
			memcpy(data[i], orgn_data[i], len+1);
		}
	}
	wxIcon icon = wxIcon(data);
	images->Replace(which, icon);
	for (i=0; i<icon_lines; i++)
	{
		delete [] data[i];
	}
	delete []data;
}

//item operations
//root item
wxTreeItemId DataTreeCtrl::AddRootItem(const wxString &text)
{
	wxTreeItemId item = AddRoot(text);
	LayerInfo* item_data = new LayerInfo;
	item_data->type = 0;//root;
	SetItemData(item, item_data);
	return item;
}

void DataTreeCtrl::ExpandRootItem()
{
	Expand(GetRootItem());
}

//view item
wxTreeItemId DataTreeCtrl::AddViewItem(const wxString &text)
{
	wxTreeItemId item = AppendItem(GetRootItem(),text, 1);
	LayerInfo* item_data = new LayerInfo;
	item_data->type = 1;//view
	SetItemData(item, item_data);
	return item;
}

void DataTreeCtrl::SetViewItemImage(const wxTreeItemId& item, int image)
{
	SetItemImage(item , image);

	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
	if (item_data)
		item_data->icon = image;
}

//volume data item
wxTreeItemId DataTreeCtrl::AddVolItem(wxTreeItemId par_item, const wxString &text)
{
	wxTreeItemId item = AppendItem(par_item, text, 1);
	LayerInfo* item_data = new LayerInfo;
	item_data->type = 2;//volume data
	SetItemData(item, item_data);
	return item;
}

//volume data item
wxTreeItemId DataTreeCtrl::AddVolItem(wxTreeItemId par_item, VolumeData *vd)
{
	wxTreeItemId item = AppendItem(par_item, vd->GetName(), 1);
	LayerInfo* item_data = new LayerInfo;
	item_data->type = 2;//volume data
	SetItemData(item, item_data);

	if (vd->GetColormapMode() == 3)
		BuildROITree(item, *vd->getROITree(), vd);

	return item;
}

void DataTreeCtrl::UpdateROITreeIcons(VolumeData* vd)
{
	if (!vd) return;

	wxTreeItemId v_item = FindTreeItem(vd->GetName());
	if (!v_item.IsOk()) return;

	UpdateROITreeIcons(v_item, vd);
}

void DataTreeCtrl::UpdateROITreeIcons(wxTreeItemId par_item, VolumeData* vd)
{
	if (!par_item.IsOk() || !vd) return;

	wxTreeItemIdValue cookie;
	wxTreeItemId child_item = GetFirstChild(par_item, cookie);
	while (child_item.IsOk())
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(child_item);
		if (!item_data)
			continue;
		int ii = item_data->icon / 2;
		int id = item_data->id;
		SetItemImage(child_item, vd->isSelID(id)?2*ii+1:2*ii);
		item_data->icon = vd->isSelID(id)?2*ii+1:2*ii;

		UpdateROITreeIcons(child_item, vd);
		child_item = GetNextChild(par_item, cookie);
	}
}

void DataTreeCtrl::UpdateROITreeIconColor(VolumeData* vd)
{
	if (!vd) return;

	wxTreeItemId v_item = FindTreeItem(vd->GetName());
	if (!v_item.IsOk()) return;

	UpdateROITreeIconColor(v_item, vd);
}

void DataTreeCtrl::UpdateROITreeIconColor(wxTreeItemId par_item, VolumeData* vd)
{
	if (!par_item.IsOk() || !vd) return;

	wxTreeItemIdValue cookie;
	wxTreeItemId child_item = GetFirstChild(par_item, cookie);
	while (child_item.IsOk())
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(child_item);
		if (!item_data)
			continue;
		int ii = item_data->icon / 2;
		int id = item_data->id;
		unsigned char r = 255, g = 255, b = 255;
		if (item_data->type == 7)
			vd->GetIDColor(r, g, b, id);
		wxColor wxc(r, g, b);
		ChangeIconColor(ii, wxc);
		
		UpdateROITreeIconColor(child_item, vd);
		child_item = GetNextChild(par_item, cookie);
	}
}

void DataTreeCtrl::UpdateVolItem(wxTreeItemId item, VolumeData *vd)
{
	if (!item.IsOk() || !vd) return;

	SaveExpState();

	SetItemText(item, vd->GetName());

	LayerInfo* item_data = (LayerInfo *)GetItemData(item);
	item_data->type = 2;//volume data
	
	SetEvtHandlerEnabled(false);
	DeleteChildren(item);
	SetEvtHandlerEnabled(true);
	if (vd->GetColormapMode() == 3)
		BuildROITree(item, *vd->getROITree(), vd);

	LoadExpState();

	return;
}

void DataTreeCtrl::BuildROITree(wxTreeItemId par_item, const boost::property_tree::wptree& tree, VolumeData *vd)
{
	for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
	{
		if (const auto val = tree.get_optional<wstring>(child->first))
		{
			try
			{
				wptree subtree = child->second;
				int id = boost::lexical_cast<int>(child->first);
				if (id <= 0 || !vd || vd->IsROICombined(id) < 0)
				{
					wxString name = *val;
					wxTreeItemId item = AppendItem(par_item, name, 1);
					LayerInfo* item_data = new LayerInfo;
					item_data->type = (id > 0) ? 7 : 8;//7-volume segment : 8-segmnet group
					item_data->id = id;
				
					SetItemData(item, item_data);
					if (vd)
					{
						AppendIcon();
						unsigned char r = 255, g = 255, b = 255;
						if (item_data->type == 7)
							vd->GetIDColor(r, g, b, id);
						wxColor wxc(r, g, b);
						int ii = GetIconNum() - 1;
						ChangeIconColor(ii, wxc);
						SetItemImage(item, vd->isSelID(id) ? 2 * ii + 1 : 2 * ii);
						item_data->icon = vd->isSelID(id) ? 2 * ii + 1 : 2 * ii;
					}

					BuildROITree(item, subtree, vd);
				}
			}
			catch (boost::bad_lexical_cast e)
			{
				cerr << "DataTreeCtrl::BuildROITree(wxTreeItemId par_item, const boost::property_tree::wptree& tree): bad_lexical_cast" << endl;
			}
			
		}
	}
}

//return the item's parent if there is no sibling.
wxTreeItemId DataTreeCtrl::GetNextSibling_loop(wxTreeItemId item)
{
	wxTreeItemId def_rval;

	if (!item.IsOk())
		return def_rval;

	wxTreeItemId next_sib = GetNextSibling(item);

	if (next_sib.IsOk())
		return next_sib;
	else
	{
		wxTreeItemId prev_sib = GetPrevSibling(item);
		if (prev_sib.IsOk())
			return prev_sib;
		else
		{
			wxTreeItemId pitem = GetItemParent(item);
			if (pitem.IsOk())
				return pitem;
			else
				return def_rval;
		}
	}

	return def_rval;
}

wxTreeItemId DataTreeCtrl::FindTreeItem(wxString name)
{
	wxTreeItemId item = GetRootItem();
	wxTreeItemId rval;
	if (!item.IsOk()) return rval;

	return FindTreeItem(item, name);
}

wxTreeItemId DataTreeCtrl::FindTreeItem(wxTreeItemId par_item, const wxString& name, bool roi_tree)
{
	wxTreeItemId item = par_item;
	wxTreeItemId rval;
	if (!item.IsOk()) return rval;

	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
    if (!item_data)
        return rval;
	if (!roi_tree && (item_data->type == 7 || item_data->type == 8 || item_data->type == 9 || item_data->type == 10))
		return rval;

	if (GetItemBaseText(item) == name) return item;

	wxTreeItemIdValue cookie;
	wxTreeItemId child_item = GetFirstChild(item, cookie);
	while (child_item.IsOk())
	{
		rval = FindTreeItem(child_item, name, roi_tree);
		if (rval.IsOk()) return rval;
		child_item = GetNextChild(item, cookie);
	}

	return rval;
}

wxTreeItemId DataTreeCtrl::FindTreeItemBySegmentID(wxTreeItemId par_item, int id)
{
    wxTreeItemId item = par_item;
    wxTreeItemId rval;
    if (!item.IsOk()) return rval;

    LayerInfo* item_data = (LayerInfo*)GetItemData(item);
    if (!item_data)
        return rval;
    if ((item_data->type == 7 || item_data->type == 8 || item_data->type == 9 || item_data->type == 10) && item_data && item_data->id == id)
        return item;

    wxTreeItemIdValue cookie;
    wxTreeItemId child_item = GetFirstChild(item, cookie);
    while (child_item.IsOk())
    {
        rval = FindTreeItemBySegmentID(child_item, id);
        if (rval.IsOk()) return rval;
        child_item = GetNextChild(item, cookie);
    }

    return rval;
}

void DataTreeCtrl::SetVolItemImage(const wxTreeItemId item, int image)
{
	SetItemImage(item, image);

	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
	if (item_data)
		item_data->icon = image;
}

//mesh data item
wxTreeItemId DataTreeCtrl::AddMeshItem(wxTreeItemId par_item, const wxString &text)
{
	wxTreeItemId item = AppendItem(par_item, text, 1);
	LayerInfo* item_data = new LayerInfo;
	item_data->type = 3;//mesh data
	SetItemData(item, item_data);
	return item;
}

wxTreeItemId DataTreeCtrl::AddMeshItem(wxTreeItemId par_item, MeshData *md)
{
    wxTreeItemId item = AppendItem(par_item, md->GetName(), 1);
    LayerInfo* item_data = new LayerInfo;
    item_data->type = 3;//mesh data
    SetItemData(item, item_data);

    if (md->isTree())
        InitROITree(item, *md->getROITree(), md);
    
    return item;
}

void DataTreeCtrl::UpdateROITreeIcons(MeshData* md)
{
    if (!md) return;

    wxTreeItemId v_item = FindTreeItem(md->GetName());
    if (!v_item.IsOk()) return;

    UpdateROITreeIcons(v_item, md);
}

void DataTreeCtrl::UpdateROITreeIcons(wxTreeItemId par_item, MeshData* md)
{
    if (!par_item.IsOk() || !md) return;

    wxTreeItemIdValue cookie;
    wxTreeItemId child_item = GetFirstChild(par_item, cookie);
    while (child_item.IsOk())
    {
        LayerInfo* item_data = (LayerInfo*)GetItemData(child_item);
        if (!item_data)
        {
            child_item = GetNextChild(par_item, cookie);
            continue;
        }
        int ii = item_data->icon / 2;
        int id = item_data->id;
        SetItemImage(child_item, md->GetROIState(id)?2*ii+1:2*ii);
        item_data->icon = md->GetROIState(id)?2*ii+1:2*ii;

        UpdateROITreeIcons(child_item, md);
        child_item = GetNextChild(par_item, cookie);
    }
}

void DataTreeCtrl::UpdateROITreeIconColor(MeshData* md)
{
    if (!md) return;

    wxTreeItemId v_item = FindTreeItem(md->GetName());
    if (!v_item.IsOk()) return;

    UpdateROITreeIconColor(v_item, md);
}

void DataTreeCtrl::UpdateROITreeIconColor(wxTreeItemId par_item, MeshData* md)
{
    if (!par_item.IsOk() || !md) return;

    wxTreeItemIdValue cookie;
    wxTreeItemId child_item = GetFirstChild(par_item, cookie);
    while (child_item.IsOk())
    {
        LayerInfo* item_data = (LayerInfo*)GetItemData(child_item);
        if (!item_data)
        {
            child_item = GetNextChild(par_item, cookie);
            continue;
        }
        int ii = item_data->icon / 2;
        int id = item_data->id;
        unsigned char r = 255, g = 255, b = 255;
        if (item_data->type == 7)
            md->GetIDColor(r, g, b, id);
        wxColor wxc(r, g, b);
        ChangeIconColor(ii, wxc);
        
        UpdateROITreeIconColor(child_item, md);
        child_item = GetNextChild(par_item, cookie);
    }
}

void DataTreeCtrl::UpdateMeshItem(wxTreeItemId item, MeshData *md)
{
    if (!item.IsOk() || !md) return;

    SaveExpState();

    SetItemText(item, md->GetName());

    LayerInfo* item_data = (LayerInfo *)GetItemData(item);
    item_data->type = 3;//volume data
    
    DeleteChildren(item);
    if (md->isTree())
        BuildROITree(item, *md->getROITree(), md);

    LoadExpState();

    return;
}

void DataTreeCtrl::InitROITree(wxTreeItemId par_item, const boost::property_tree::wptree& tree, MeshData *md)
{
    for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
    {
        if (const auto val = tree.get_optional<wstring>(child->first))
        {
            try
            {
                wxTreeItemId dummy_item = AppendItem(par_item, wxT("dummy"), 1);
                SetItemData(dummy_item, NULL);
                return;
            }
            catch (boost::bad_lexical_cast e)
            {
                cerr << "DataTreeCtrl::InitROITree(wxTreeItemId par_item, const boost::property_tree::wptree& tree): bad_lexical_cast" << endl;
            }
        }
    }
}

void DataTreeCtrl::AddChildrenROITree(wxTreeItemId par_item, const boost::property_tree::wptree& tree, MeshData *md)
{
    TraversalDeleteChildren(par_item);
    for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
    {
        if (const auto val = tree.get_optional<wstring>(child->first))
        {
            try
            {
                wxString name = child->first;
                wxTreeItemId item = AppendItem(par_item, name, 1);
                const wptree &subtree = child->second;
                LayerInfo* item_data = new LayerInfo;
                int id = boost::lexical_cast<int>(*val);
                item_data->type = id >= 0 ? 9 : 10; //9-mesh segment; 10-mesh segment group
                item_data->id = id;
                SetItemData(item, item_data);
                if (md)
                {
                    AppendIcon();
                    unsigned char r = 255, g = 255, b = 255;
                    if (item_data->type == 9)
                        md->GetIDColor(r, g, b, id);
                    wxColor wxc(r, g, b);
                    int ii = GetIconNum()-1;
                    ChangeIconColor(ii, wxc);
                    SetItemImage(item, md->isSelID(id)?2*ii+1:2*ii);
                    item_data->icon = md->isSelID(id)?2*ii+1:2*ii;
                }
                if (subtree.begin() != subtree.end())
                {
                    wxTreeItemId dummy_item = AppendItem(item, wxT("dummy"), 1);
                    SetItemData(dummy_item, NULL);
                }
            }
            catch (boost::bad_lexical_cast e)
            {
                cerr << "DataTreeCtrl::InitROITree(wxTreeItemId par_item, const boost::property_tree::wptree& tree): bad_lexical_cast" << endl;
            }
        }
    }
}

void DataTreeCtrl::BuildROITree(wxTreeItemId par_item, const boost::property_tree::wptree& tree, MeshData *md)
{
    for (wptree::const_iterator child = tree.begin(); child != tree.end(); ++child)
    {
        if (const auto val = tree.get_optional<wstring>(child->first))
        {
            try
            {
                wxString name = child->first;
                wxTreeItemId item = AppendItem(par_item, name, 1);
                wptree subtree = child->second;
                LayerInfo* item_data = new LayerInfo;
                int id = boost::lexical_cast<int>(*val);
                item_data->type = id >= 0 ? 9 : 10; //9-mesh segment; 10-mesh segment group
                item_data->id = id;
                SetItemData(item, item_data);
                if (md)
                {
                    AppendIcon();
                    unsigned char r = 255, g = 255, b = 255;
                    if (item_data->type == 9)
                        md->GetIDColor(r, g, b, id);
                    wxColor wxc(r, g, b);
                    int ii = GetIconNum()-1;
                    ChangeIconColor(ii, wxc);
                    SetItemImage(item, md->isSelID(id)?2*ii+1:2*ii);
                    item_data->icon = md->isSelID(id)?2*ii+1:2*ii;
                }

                BuildROITree(item, subtree, md);
            }
            catch (boost::bad_lexical_cast e)
            {
                cerr << "DataTreeCtrl::BuildROITree(wxTreeItemId par_item, const boost::property_tree::wptree& tree): bad_lexical_cast" << endl;
            }
            
        }
    }
}

void DataTreeCtrl::SetMeshItemImage(const wxTreeItemId item, int image)
{
	SetItemImage(item, image);
	
	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
	if (item_data)
		item_data->icon = image;
}

//annotation item
wxTreeItemId DataTreeCtrl::AddAnnotationItem(wxTreeItemId par_item, const wxString &text)
{
	wxTreeItemId item = AppendItem(par_item, text, 1);
	LayerInfo* item_data = new LayerInfo;
	item_data->type = 4;//annotations
	SetItemData(item, item_data);

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	vr_frame->GetMeasureDlg()->UpdateList();

	return item;
}

void DataTreeCtrl::SetAnnotationItemImage(const wxTreeItemId item, int image)
{
	SetItemImage(item, image);

	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
	if (item_data)
		item_data->icon = image;
}

//group item
wxTreeItemId DataTreeCtrl::AddGroupItem(wxTreeItemId par_item, const wxString &text)
{
	wxTreeItemId item = AppendItem(par_item, text, 1);
	LayerInfo* item_data = new LayerInfo;
	item_data->type = 5;//group
	SetItemData(item, item_data);
	return item;
}

void DataTreeCtrl::SetGroupItemImage(const wxTreeItemId item, int image)
{
	SetItemImage(item, image);

	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
	if (item_data)
		item_data->icon = image;
}

//mesh group item
wxTreeItemId DataTreeCtrl::AddMGroupItem(wxTreeItemId par_item, const wxString &text)
{
	wxTreeItemId item = AppendItem(par_item, text, 1);
	LayerInfo* item_data = new LayerInfo;
	item_data->type = 6;//mesh group
	SetItemData(item, item_data);
	return item;
}

void DataTreeCtrl::SetMGroupItemImage(const wxTreeItemId item, int image)
{
	SetItemImage(item, image);

	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
	if (item_data)
		item_data->icon = image;
}

//brush commands (from the panel)
void DataTreeCtrl::BrushClear()
{
	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (sel_item.IsOk() && vr_frame)
	{
		//select data
		wxString name = GetItemBaseText(sel_item);
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data && item_data->type==2)
		{
			wxTreeItemId par_item = GetItemParent(sel_item);
			if (par_item.IsOk())
			{
				LayerInfo* par_item_data = (LayerInfo*)GetItemData(par_item);
				if (par_item_data && par_item_data->type == 5)
				{
					//par is group
					wxString str = GetItemBaseText(GetItemParent(par_item));
					VRenderView* vrv = vr_frame->GetView(str);
					if (vrv)
					{
						VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
						if (vd)
						{
							int int_mode = vrv->GetIntMode();
							int paint_mode = vrv->GetPaintMode();
							vrv->SetVolumeA(vd);
							vrv->SetPaintMode(6);
							vrv->Segment();
							vrv->RefreshGL();
							vrv->SetPaintMode(paint_mode);
							vrv->SetIntMode(int_mode);
						}
					}
				}
				else if (par_item_data && par_item_data->type == 1)
				{
					//par is view
					wxString str = GetItemBaseText(par_item);
					VRenderView* vrv = vr_frame->GetView(str);
					if (vrv)
					{
						VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
						if (vd)
						{
							int int_mode = vrv->GetIntMode();
							int paint_mode = vrv->GetPaintMode();
							vrv->SetVolumeA(vd);
							vrv->SetPaintMode(6);
							vrv->Segment();
							vrv->RefreshGL();
							vrv->SetPaintMode(paint_mode);
							vrv->SetIntMode(int_mode);
						}
					}
				}
			}
		}
	}
}

void DataTreeCtrl::BrushCreate()
{
	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (sel_item.IsOk() && vr_frame)
	{
		//select data
		wxString name = GetItemBaseText(sel_item);
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data && item_data->type==2)
		{
			wxTreeItemId par_item = GetItemParent(sel_item);
			if (par_item.IsOk())
			{
				LayerInfo* par_item_data = (LayerInfo*)GetItemData(par_item);
				if (par_item_data && par_item_data->type == 5)
				{
					//par is group
					wxString group_name = GetItemBaseText(par_item);
					wxString str = GetItemBaseText(GetItemParent(par_item));
					VRenderView* vrv = vr_frame->GetView(str);
					VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
					if (vd)
					{
						if (vd->GetNAMode())
						{
							vector<VolumeData*> vols;
							DataGroup* group = vrv->GetGroup(group_name);
							if (group)
							{
								for (int j = 0; j < group->GetVolumeNum(); j++)
								{
									VolumeData* gvd = group->GetVolumeData(j);
									if (gvd && gvd->GetNAMode() && (gvd->GetLabel(false) || !gvd->GetSharedLabelName().IsEmpty()))
										vols.push_back(gvd);
								}
								if (vols.size() > 0)
									vrv->SetVolumeA(vols[0]);
								if (vols.size() > 1)
									vrv->SetVolumeB(vols[1]);
								if (vols.size() > 2)
									vrv->SetVolumeC(vols[2]);
								vrv->Calculate(10, group_name);
							}
						}
						else
						{
							if (vrv)
							{
								vrv->SetVolumeA(vd);
								vrv->Calculate(5, group_name);
							}
						}
					}
				}
				else if (par_item_data && par_item_data->type == 1)
				{
					//par is view
					wxString str = GetItemBaseText(par_item);
					VRenderView* vrv = vr_frame->GetView(str);
					if (vrv)
					{
						VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
						if (vd)
						{
							vrv->SetVolumeA(vd);
							vrv->Calculate(5);
						}
					}
				}
			}
		}
	}
}

void DataTreeCtrl::BrushCreateInv()
{
	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (sel_item.IsOk() && vr_frame)
	{
		//select data
		wxString name = GetItemBaseText(sel_item);
		int cal_type = 6;

		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data && item_data->type==2)
		{
			wxTreeItemId par_item = GetItemParent(sel_item);
			if (par_item.IsOk())
			{
				LayerInfo* par_item_data = (LayerInfo*)GetItemData(par_item);
				if (par_item_data && par_item_data->type == 5)
				{
					//par is group
					wxString group_name = GetItemBaseText(par_item);
					wxString str = GetItemBaseText(GetItemParent(par_item));
					VRenderView* vrv = vr_frame->GetView(str);
					VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
					if (vd)
					{
						if (vd->GetNAMode())
						{
							vector<VolumeData*> vols;
							DataGroup* group = vrv->GetGroup(group_name);
							if (group)
							{
								for (int j = 0; j < group->GetVolumeNum(); j++)
								{
									VolumeData* gvd = group->GetVolumeData(j);
									if (gvd && gvd->GetNAMode() && (gvd->GetLabel(false) || !gvd->GetSharedLabelName().IsEmpty()))
										vols.push_back(gvd);
								}
								if (vols.size() > 0)
									vrv->SetVolumeA(vols[0]);
								if (vols.size() > 1)
									vrv->SetVolumeB(vols[1]);
								if (vols.size() > 2)
									vrv->SetVolumeC(vols[2]);
								vrv->Calculate(11, group_name);
							}
						}
						else
						{
							if (vrv)
							{
								vrv->SetVolumeA(vd);
								vrv->Calculate(cal_type, group_name);
							}
						}
					}
				}
				else if (par_item_data && par_item_data->type == 1)
				{
					//par is view
					wxString str = GetItemBaseText(par_item);
					VRenderView* vrv = vr_frame->GetView(str);
					if (vrv)
					{
						VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
						if (vd)
						{
							vrv->SetVolumeA(vd);
							vrv->Calculate(cal_type);
						}
					}
				}
			}
		}
	}
}

VRenderView* DataTreeCtrl::GetCurrentView()
{
	VRenderView* vrv = NULL;

	wxTreeItemId sel_item = GetSelection();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	
	if (!vr_frame) return NULL;
	if (!sel_item.IsOk()) return NULL;
	
	if (sel_item.IsOk())
	{
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data && item_data->type == 1)
		{
			//view
			wxString name = GetItemBaseText(sel_item);
			vrv = vr_frame->GetView(name);
		}
		else if (item_data)
		{
			//volume
			wxTreeItemId par_item = GetItemParent(sel_item);
			while (par_item.IsOk())
			{
				LayerInfo* par_data = (LayerInfo*) GetItemData(par_item);
				if (par_data && par_data->type == 1)
				{
					wxString name = GetItemBaseText(par_item);
					vrv = vr_frame->GetView(name);
					break;
				}
				par_item = GetItemParent(par_item);
			}
		}
	}

	return vrv;
}

void DataTreeCtrl::SaveExpState()
{
	wxTreeItemId item = GetRootItem();
	if (!item.IsOk()) return;

	m_exp_state.clear();

	SaveExpState(item);
}

void DataTreeCtrl::SaveExpState(wxTreeItemId node, const wxString& prefix)
{
	wxTreeItemId item = node;
	if (!item.IsOk()) return;
	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
	if (!item_data) return;
	m_exp_state[prefix + GetItemBaseText(item) + wxString::Format(wxT("#%i"),item_data->type)] = IsExpanded(item);

	wxTreeItemIdValue cookie;
	wxTreeItemId child_item = GetFirstChild(item, cookie);
	wxString child_prefix = prefix;
	if (item_data->type == 2)
		child_prefix = GetItemBaseText(item) + wxT(".");
	while (child_item.IsOk())
	{
		SaveExpState(child_item, child_prefix);
		child_item = GetNextChild(item, cookie);
	}
}

void DataTreeCtrl::LoadExpState(bool expand_newitem)
{
	wxTreeItemId item = GetRootItem();
	if (!item.IsOk()) return;

	LoadExpState(item, wxT(""), expand_newitem);
}

void DataTreeCtrl::LoadExpState(wxTreeItemId node, const wxString& prefix, bool expand_newitem)
{
	wxTreeItemId item = node;
	if (!item.IsOk()) return;
	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
	if (!item_data) return;
	
	bool is_new = false;
	wxString name = prefix + GetItemBaseText(item) + wxString::Format(wxT("#%i"),item_data->type);
	if (m_exp_state.find(name) != m_exp_state.end())
	{
		is_new = false;
		if (m_exp_state[name])
			Expand(item);
		else
			Collapse(item);
	}
	else
	{
		is_new = true;
		if (expand_newitem)
			TraversalExpand(item);
		else
			Collapse(item);
	}

	wxTreeItemIdValue cookie;
	wxTreeItemId child_item = GetFirstChild(item, cookie);
	bool expand_newitem_child = expand_newitem;
	wxString child_prefix = prefix;
	if (item_data && item_data->type == 2)
	{
		child_prefix = GetItemBaseText(item) + wxT(".");
		if (is_new)
			expand_newitem_child = false;
	}
	while (child_item.IsOk())
	{
		LoadExpState(child_item, child_prefix, expand_newitem_child);
		child_item = GetNextChild(item, cookie);
	}
}

string DataTreeCtrl::ExportExpState()
{
	string rval;

	SaveExpState();

	for (auto ite : m_exp_state)
	{
		rval += ite.first + "\n";
		if (ite.second) rval += "1";
		else rval += "0";
		rval += "\n";
	}

	return rval;
}

void DataTreeCtrl::ImportExpState(const string &state)
{
	stringstream ss(state);

	m_exp_state.clear();

	while (1)
	{
		string key, strval;
		if (!getline(ss, key))
			break;
		if (!getline(ss, strval))
			break;
		
		if (strval == "1")
			m_exp_state[key] = true;
		else if (strval == "0")
			m_exp_state[key] = false;
	}

	LoadExpState();
}


void DataTreeCtrl::TraversalExpand(wxTreeItemId item)
{
	if (!item.IsOk()) return;

	wxTreeItemId parent = GetItemParent(item);
	while (parent.IsOk())
	{
		Expand(parent);
		parent = GetItemParent(parent);
	}
}

wxTreeItemId DataTreeCtrl::GetParentVolItem(wxTreeItemId item)
{
	wxTreeItemId rval;

	if (!item.IsOk()) return rval;

	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
	if (item_data->type != 7 && item_data->type != 8)
		return rval;

	wxTreeItemId parent = GetItemParent(item);
	while (parent.IsOk())
	{
		item_data = (LayerInfo*)GetItemData(parent);
		if (item_data->type == 2)
		{
			rval = parent;
			break;
		}
		parent = GetItemParent(parent);
	}
	return rval;
}

wxTreeItemId DataTreeCtrl::GetParentMeshItem(wxTreeItemId item)
{
    wxTreeItemId rval;

    if (!item.IsOk()) return rval;

    LayerInfo* item_data = (LayerInfo*)GetItemData(item);
    if (item_data->type != 9 && item_data->type != 10)
        return rval;

    wxTreeItemId parent = GetItemParent(item);
    while (parent.IsOk())
    {
        item_data = (LayerInfo*)GetItemData(parent);
        if (item_data->type == 3)
        {
            rval = parent;
            break;
        }
        parent = GetItemParent(parent);
    }
    return rval;
}

void DataTreeCtrl::ExpandDataTreeItem(wxString name, bool expand_children)
{
	wxTreeItemId item = FindTreeItem(name);
	if (!item.IsOk()) return;

	if (expand_children)
		ExpandAllChildren(item);
	else
		Expand(item);
}

void DataTreeCtrl::CollapseDataTreeItem(wxString name, bool collapse_children)
{
	wxTreeItemId item = FindTreeItem(name);
	if (!item.IsOk()) return;

	if (collapse_children)
		CollapseAllChildren(item);
	else
		Collapse(item);
}

void DataTreeCtrl::RedoVisibility()
{
	if (m_v_redo.empty())
		return;

	m_vtmp.clear();
	GetVisHistoryTraversal(GetRootItem());
	m_v_undo.push_back(m_vtmp);

	m_vtmp = m_v_redo[m_v_redo.size() - 1];

	SetVisHistoryTraversal(GetRootItem());

	m_v_redo.pop_back();

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	vr_frame->UpdateTreeIcons();
	vr_frame->RefreshVRenderViews(false, true);
}

void DataTreeCtrl::ClearVisHistory()
{
	m_v_undo.clear();
	m_v_redo.clear();
}

void DataTreeCtrl::PushVisHistory()
{
	m_vtmp.clear();
	GetVisHistoryTraversal(GetRootItem());

	m_v_undo.push_back(m_vtmp);
    while (m_v_undo.size() > 10)
        m_v_undo.erase(m_v_undo.begin());
	m_v_redo.clear();
}

void DataTreeCtrl::GetVisHistoryTraversal(wxTreeItemId item)
{
	wxTreeItemIdValue cookie;
	wxTreeItemId child_item = GetFirstChild(item, cookie);
	if (child_item.IsOk())
		GetVisHistoryTraversal(child_item);
	child_item = GetNextChild(item, cookie);
	while (child_item.IsOk())
	{
		GetVisHistoryTraversal(child_item);
		child_item = GetNextChild(item, cookie);
	}

	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
	if (item_data && item_data->type != 7 && item_data->type != 8 && item_data->type != 9 && item_data->type != 10)
	{
		wxString name = GetItemBaseText(item);
		VolVisState state;
		state.disp = (item_data->icon % 2 == 0) ? false : true;

		if (item_data->type == 2)
		{
			VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
			VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
			if (vd)
			{
				state.na_mode = vd->GetNAMode();
                state.v_sel_ids = vd->GetSelIDs();
				auto ids = vd->GetActiveSegIDs();
				if (ids)
				{
					auto it = ids->begin();
					while (it != ids->end())
					{
						int ival = vd->GetSegmentMask(*it);
						state.label[*it] = ival;
						it++;
					}
				}
			}
		}
        if (item_data->type == 3)
        {
            VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
            MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
            if (md && md->isTree() && md->GetMR())
                state.mesh_segs = md->GetMR()->get_all_roi_state();
        }

		m_vtmp[name] = state;
	}
}

void DataTreeCtrl::UndoVisibility()
{
	if (m_v_undo.empty())
		return;
	
	m_vtmp.clear();
	GetVisHistoryTraversal(GetRootItem());
	m_v_redo.push_back(m_vtmp);
	
	m_vtmp = m_v_undo[m_v_undo.size() - 1];
	SetVisHistoryTraversal(GetRootItem());

	m_v_undo.pop_back();
	
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	vr_frame->UpdateTreeIcons();
	vr_frame->RefreshVRenderViews(false, true);
}

void DataTreeCtrl::SetVisHistoryTraversal(wxTreeItemId item)
{
	wxTreeItemIdValue cookie;
	wxTreeItemId child_item = GetFirstChild(item, cookie);
	if (child_item.IsOk())
		SetVisHistoryTraversal(child_item);
	child_item = GetNextChild(item, cookie);
	while (child_item.IsOk())
	{
		SetVisHistoryTraversal(child_item);
		child_item = GetNextChild(item, cookie);
	}

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	wxString name = "";

	if (item.IsOk() && vr_frame)
	{
		name = GetItemBaseText(item);
		LayerInfo* item_data = (LayerInfo*)GetItemData(item);
		if (item_data && m_vtmp.find(name) != m_vtmp.end())
		{
			switch (item_data->type)
			{
			case 1://view
			{
				VRenderView* vrv = vr_frame->GetView(name);
				if (vrv)
				{
					vrv->SetDraw(m_vtmp[name].disp);
				}
			}
			break;
			case 2://volume data
			{
				VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
				if (vd)
				{
					vd->SetDisp(m_vtmp[name].disp);
					//vd->SetNAMode(m_vtmp[name].na_mode);
                    vd->SetSelIDs(m_vtmp[name].v_sel_ids);
					auto it = m_vtmp[name].label.begin();
					while (it != m_vtmp[name].label.end())
					{
						vd->SetSegmentMask(it->first, it->second);
						it++;
					}
					for (int i = 0; i < vr_frame->GetViewNum(); i++)
					{
						VRenderView* vrv = vr_frame->GetView(i);
						if (vrv)
							vrv->SetVolPopDirty();
					}
				}
			}
			break;
			case 3://mesh data
			{
				MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
				if (md)
				{
					md->SetDisp(m_vtmp[name].disp);
                    if (md->isTree() && md->GetMR())
                        md->GetMR()->set_all_roi_state(m_vtmp[name].mesh_segs);
					for (int i = 0; i < vr_frame->GetViewNum(); i++)
					{
						VRenderView* vrv = vr_frame->GetView(i);
						if (vrv)
							vrv->SetMeshPopDirty();
					}
				}
			}
			break;
			case 4://annotations
			{
				Annotations* ann = vr_frame->GetDataManager()->GetAnnotations(name);
				if (ann)
				{
					ann->SetDisp(m_vtmp[name].disp);
					vr_frame->GetMeasureDlg()->UpdateList();
				}
			}
			break;
			case 5://group
			{
				wxString par_name = GetItemBaseText(GetItemParent(item));
				VRenderView* vrv = vr_frame->GetView(par_name);
				if (vrv)
				{
					DataGroup* group = vrv->GetGroup(name);
					if (group)
					{
						group->SetDisp(m_vtmp[name].disp);
						vrv->SetVolPopDirty();
					}
				}
			}
			break;
			case 6://mesh group
			{
				wxString par_name = GetItemBaseText(GetItemParent(item));
				VRenderView* vrv = vr_frame->GetView(par_name);
				if (vrv)
				{
					MeshGroup* group = vrv->GetMGroup(name);
					if (group)
					{
						group->SetDisp(m_vtmp[name].disp);
						vrv->SetMeshPopDirty();
					}
				}
			}
			break;
			/*case 7://volume segment
			case 8://segment group
			{
				wxTreeItemId vol_item = GetParentVolItem(sel_item);
				if (vol_item.IsOk())
				{
					wxString vname = GetItemBaseText(vol_item);
					VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(vname);
					if (vd)
					{
						int id = vd->GetROIid(GetItemBaseText(sel_item).ToStdWstring());
						if (id != -1)
							vd->SetROISel(GetItemBaseText(sel_item).ToStdWstring(), !(vd->isSelID(id)));
					}
				}
			}
			break;*/
			}
		}
	}
}

void DataTreeCtrl::ShowAllDatasets()
{
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    if (!vr_frame) return;
    
    wxTreeItemId item = GetSelection();
    if (!item.IsOk()) return;

    LayerInfo* item_data = (LayerInfo*)GetItemData(item);
    //if (!item_data || (item_data->type != 2 && item_data->type != 3 && item_data->type != 4 && item_data->type != 7 && item_data->type != 8 && item_data->type != 9 && item_data->type != 10))
    //    return;
    
    PushVisHistory();
    
    bool sub_groups = false;
    
    wxString name = GetItemBaseText(item);
    if (item_data)
    {
        switch (item_data->type)
        {
        case 2://volume
        {
            VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
            if (vd)
            {
                if (vd->GetColormapMode() == 3)
                {
                    vd->ClearSelIDs();
                    vd->SetROISel(L"", true, true);
                    if (vd->GetIDColDispMode() == 2)
                    {
                        vd->SetIDColDispMode(0);
                        if (vr_frame->GetPropView())
                            vr_frame->GetPropView()->UpdateUIsROI();
                    }
                    sub_groups = true;
                }
            }
        }
        break;
        case 7://volume segment
        case 8://segment group
        {
            wxTreeItemId vol_item = GetParentVolItem(item);
            if (vol_item.IsOk())
            {
                wxString vname = GetItemBaseText(vol_item);
                VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(vname);
                if (vd)
                {
                    if (vd->GetColormapMode() == 3)
                    {
                        vd->ClearSelIDs();
                        vd->SetROISel(L"", true, true);
                        sub_groups = true;
                    }
                }
            }
        }
        break;
        case 3://mesh
        {
            MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
            if (md)
            {
                if (md->isTree())
                {
                    md->SetROIStateTraverse(true);
                    sub_groups = true;
                }
            }
        }
        break;
        case 9://mesh segment
        case 10://mesh segment group
        {
            wxTreeItemId mesh_item = GetParentMeshItem(item);
            if (mesh_item.IsOk())
            {
                wxString mname = GetItemBaseText(mesh_item);
                MeshData* md = vr_frame->GetDataManager()->GetMeshData(mname);
                if (md)
                {
                    if (md->isTree())
                    {
                        md->SetROIStateTraverse(true);
                        sub_groups = true;
                    }
                }
            }
        }
        break;
        }
    }

    if (!sub_groups)
        ShowAllDatasetsTraversal(GetRootItem());

    //m_scroll_pos = GetScrollPos(wxVERTICAL);
    vr_frame->UpdateTreeIcons();
    //SetScrollPos(wxVERTICAL, m_scroll_pos);
    //UpdateSelection();
    vr_frame->RefreshVRenderViews(false, true);
}

void DataTreeCtrl::ShowAllDatasetsTraversal(wxTreeItemId item)
{
    wxTreeItemIdValue cookie;
    wxTreeItemId child_item = GetFirstChild(item, cookie);
    if (child_item.IsOk())
        ShowAllDatasetsTraversal(child_item);
    child_item = GetNextChild(item, cookie);
    while (child_item.IsOk())
    {
        ShowAllDatasetsTraversal(child_item);
        child_item = GetNextChild(item, cookie);
    }

    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    wxString name = "";

    if (item.IsOk() && vr_frame)
    {
        name = GetItemBaseText(item);
        LayerInfo* item_data = (LayerInfo*)GetItemData(item);
        if (item_data)
        {
            switch (item_data->type)
            {
            case 2://volume data
            {
                VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
                if (vd)
                {
                    vd->SetDisp(true);
                    for (int i = 0; i < vr_frame->GetViewNum(); i++)
                    {
                        VRenderView* vrv = vr_frame->GetView(i);
                        if (vrv)
                            vrv->SetVolPopDirty();
                    }
                }
            }
            break;
            case 3://mesh data
            {
                MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
                if (md)
                {
                    md->SetDisp(true);
                    for (int i = 0; i < vr_frame->GetViewNum(); i++)
                    {
                        VRenderView* vrv = vr_frame->GetView(i);
                        if (vrv)
                            vrv->SetMeshPopDirty();
                    }
                }
            }
            break;
            case 4://annotations
            {
                Annotations* ann = vr_frame->GetDataManager()->GetAnnotations(name);
                if (ann)
                {
                    ann->SetDisp(true);
                    vr_frame->GetMeasureDlg()->UpdateList();
                }
            }
            break;
            }
        }
    }
}

void DataTreeCtrl::HideOtherDatasets()
{
    wxTreeItemId item;
    HideOtherDatasets(item);
}

void DataTreeCtrl::HideOtherDatasets(wxString name)
{
	wxTreeItemId item = FindTreeItem(name);
	HideOtherDatasets(item);
}

void DataTreeCtrl::HideOtherDatasets(wxTreeItemId item)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
    
    if (!item.IsOk())
        item = GetSelection();
	if (!item.IsOk()) return;

	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
	if (!item_data || (item_data->type != 2 && item_data->type != 3 && item_data->type != 4 && item_data->type != 7 && item_data->type != 8 && item_data->type != 9 && item_data->type != 10))
		return;
    
    PushVisHistory();
    
    bool sub_groups = false;
    
    wxString name = GetItemBaseText(item);
    if (item_data)
    {
        switch (item_data->type)
        {
        case 2://volume data
        {
            VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
            if (!vd)
                return;
            if (!vd->GetDisp())
            {
                vd->SetDisp(true);
                for (int i = 0; i < vr_frame->GetViewNum(); i++)
                {
                    VRenderView* vrv = vr_frame->GetView(i);
                    if (vrv)
                        vrv->SetVolPopDirty();
                }
            }
            else if (vd->GetColormapMode() == 3)
            {
                vd->SetIDColDispMode(2);
                sub_groups = true;
                if (vr_frame->GetPropView())
                    vr_frame->GetPropView()->UpdateUIsROI();
            }
        }
        break;
        case 3://mesh data
        {
            MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
            if (!md)
                return;
            if (!md->GetDisp())
            {
                md->SetDisp(true);
                for (int i = 0; i < vr_frame->GetViewNum(); i++)
                {
                    VRenderView* vrv = vr_frame->GetView(i);
                    if (vrv)
                        vrv->SetMeshPopDirty();
                }
            }
        }
        break;
        case 4://annotations
        {
            Annotations* ann = vr_frame->GetDataManager()->GetAnnotations(name);
            if (!ann || !ann->GetDisp())
                return;
            if (!ann->GetDisp())
                ann->SetDisp(true);
        }
        break;
        case 7://volume segment
        case 8://segment group
        {
            wxTreeItemId vol_item = GetParentVolItem(item);
            if (vol_item.IsOk())
            {
                wxString vname = GetItemBaseText(vol_item);
                VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(vname);
                if (vd)
                {
                    wstring ws_name = GetItemBaseText(item).ToStdWstring();
                    int id = vd->GetROIid(ws_name);
                    vd->ClearSelIDs();
                    vd->SetROISel(ws_name, true, true);
                }
            }
            sub_groups = true;
        }
        break;
        case 9://mesh segment
        case 10://mesh segment group
        {
            wxTreeItemId mesh_item = GetParentMeshItem(item);
            if (mesh_item.IsOk())
            {
                wxString mname = GetItemBaseText(mesh_item);
                MeshData* md = vr_frame->GetDataManager()->GetMeshData(mname);
                if (md)
                {
                    md->SetROIStateTraverse(false, -INT_MAX, item_data->id);
                    md->SetROIStateTraverse(true, item_data->id, item_data->id);
                }
            }
            sub_groups = true;
        }
        break;
        }
    }
    
    if (!sub_groups)
        HideOtherDatasetsTraversal(GetRootItem(), item);

	//m_scroll_pos = GetScrollPos(wxVERTICAL);
	vr_frame->UpdateTreeIcons();
	//SetScrollPos(wxVERTICAL, m_scroll_pos);
	//UpdateSelection();
	vr_frame->RefreshVRenderViews(false, true);
}

void DataTreeCtrl::HideOtherDatasetsTraversal(wxTreeItemId item, wxTreeItemId self)
{
	wxTreeItemIdValue cookie;
	wxTreeItemId child_item = GetFirstChild(item, cookie);
	if (child_item.IsOk())
		HideOtherDatasetsTraversal(child_item, self);
	child_item = GetNextChild(item, cookie);
	while (child_item.IsOk())
	{
		HideOtherDatasetsTraversal(child_item, self);
		child_item = GetNextChild(item, cookie);
	}

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	wxString name = "";

	if (item.IsOk() && vr_frame && item != self)
	{
		name = GetItemBaseText(item);
		LayerInfo* item_data = (LayerInfo*)GetItemData(item);
		if (item_data)
		{
			switch (item_data->type)
			{
			case 2://volume data
			{
				VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
				if (vd)
				{
					vd->SetDisp(false);
					for (int i = 0; i < vr_frame->GetViewNum(); i++)
					{
						VRenderView* vrv = vr_frame->GetView(i);
						if (vrv)
							vrv->SetVolPopDirty();
					}
				}
			}
			break;
			case 3://mesh data
			{
				MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
				if (md)
				{
					md->SetDisp(false);
					for (int i = 0; i < vr_frame->GetViewNum(); i++)
					{
						VRenderView* vrv = vr_frame->GetView(i);
						if (vrv)
							vrv->SetMeshPopDirty();
					}
				}
			}
			break;
			case 4://annotations
			{
				Annotations* ann = vr_frame->GetDataManager()->GetAnnotations(name);
				if (ann)
				{
					ann->SetDisp(false);
					vr_frame->GetMeasureDlg()->UpdateList();
				}
			}
			break;
			/*case 7://volume segment
			case 8://segment group
			{
				wxTreeItemId vol_item = GetParentVolItem(sel_item);
				if (vol_item.IsOk())
				{
					wxString vname = GetItemBaseText(vol_item);
					VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(vname);
					if (vd)
					{
						int id = vd->GetROIid(GetItemBaseText(sel_item).ToStdWstring());
						if (id != -1)
							vd->SetROISel(GetItemBaseText(sel_item).ToStdWstring(), !(vd->isSelID(id)));
					}
				}
			}
			break;*/
			}
		}
	}
}

void DataTreeCtrl::HideOtherVolumes()
{
    wxTreeItemId item;
    HideOtherVolumes(item);
}

void DataTreeCtrl::HideOtherVolumes(wxString name)
{
	wxTreeItemId item = FindTreeItem(name);
	HideOtherVolumes(item);
}

void DataTreeCtrl::HideOtherVolumes(wxTreeItemId item)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

    if (!item.IsOk())
        item = GetSelection();
	if (!item.IsOk()) return;

	LayerInfo* item_data = (LayerInfo*)GetItemData(item);
	if (!item_data || (item_data->type != 2 && item_data->type != 3 && item_data->type != 4))
		return;

	PushVisHistory();

	HideOtherVolumesTraversal(GetRootItem(), item);

	//m_scroll_pos = GetScrollPos(wxVERTICAL);
	vr_frame->UpdateTreeIcons();
	//SetScrollPos(wxVERTICAL, m_scroll_pos);
	//UpdateSelection();
	vr_frame->RefreshVRenderViews(false, true);
}

void DataTreeCtrl::HideOtherVolumesTraversal(wxTreeItemId item, wxTreeItemId self)
{
	wxTreeItemIdValue cookie;
	wxTreeItemId child_item = GetFirstChild(item, cookie);
	if (child_item.IsOk())
		HideOtherVolumesTraversal(child_item, self);
	child_item = GetNextChild(item, cookie);
	while (child_item.IsOk())
	{
		HideOtherVolumesTraversal(child_item, self);
		child_item = GetNextChild(item, cookie);
	}

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	wxString name = "";

	if (item.IsOk() && vr_frame && item != self)
	{
		name = GetItemBaseText(item);
		LayerInfo* item_data = (LayerInfo*)GetItemData(item);
		if (item_data)
		{
			switch (item_data->type)
			{
			case 2://volume data
			{
				VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
				if (vd)
				{
					vd->SetDisp(false);
					for (int i = 0; i < vr_frame->GetViewNum(); i++)
					{
						VRenderView* vrv = vr_frame->GetView(i);
						if (vrv)
							vrv->SetVolPopDirty();
					}
				}
			}
			break;
			/*case 7://volume segment
			case 8://segment group
			{
				wxTreeItemId vol_item = GetParentVolItem(sel_item);
				if (vol_item.IsOk())
				{
					wxString vname = GetItemBaseText(vol_item);
					VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(vname);
					if (vd)
					{
						int id = vd->GetROIid(GetItemBaseText(sel_item).ToStdWstring());
						if (id != -1)
							vd->SetROISel(GetItemBaseText(sel_item).ToStdWstring(), !(vd->isSelID(id)));
					}
				}
			}
			break;*/
			}
		}
	}
}

void DataTreeCtrl::HideSelectedItem()
{
	if (m_fixed)
		return;

	wxTreeItemId sel_item = GetSelection();

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	wxString name = "";
	
	if (sel_item.IsOk() && vr_frame)
	{
		PushVisHistory();

		name = GetItemBaseText(sel_item);
		LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
		if (item_data)
		{
			switch (item_data->type)
			{
			case 2://volume data
			{
				VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(name);
				if (vd)
				{
					vd->SetDisp(false);
					for (int i = 0; i < vr_frame->GetViewNum(); i++)
					{
						VRenderView* vrv = vr_frame->GetView(i);
						if (vrv)
							vrv->SetVolPopDirty();
					}
				}
			}
			break;
			case 3://mesh data
			{
				MeshData* md = vr_frame->GetDataManager()->GetMeshData(name);
				if (md)
				{
					md->SetDisp(false);
					for (int i = 0; i < vr_frame->GetViewNum(); i++)
					{
						VRenderView* vrv = vr_frame->GetView(i);
						if (vrv)
							vrv->SetMeshPopDirty();
					}
				}
			}
			break;
            case 7://volume segment
            case 8://segment group
            {
                wxTreeItemId vol_item = GetParentVolItem(sel_item);
                if (vol_item.IsOk())
                {
                    wxString vname = GetItemBaseText(vol_item);
                    VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(vname);
                    if (vd)
                    {
                        int id = vd->GetROIid(GetItemBaseText(sel_item).ToStdWstring());
                        if (id == -1)
                            return;
                        vd->SetROISel(GetItemBaseText(sel_item).ToStdWstring(), false);
                    }
                }
            }
            break;
                case 9://mesh segment
                case 10://mesh segment group
                {
                    wxTreeItemId mesh_item = GetParentMeshItem(sel_item);
                    if (mesh_item.IsOk())
                    {
                        wxString mname = GetItemBaseText(mesh_item);
                        MeshData* md = vr_frame->GetDataManager()->GetMeshData(mname);
                        if (md)
                            md->SetROIState(item_data->id, false);
                    }
                }
                break;
			}
		}

		//m_scroll_pos = GetScrollPos(wxVERTICAL);
		vr_frame->UpdateTreeIcons();
		//SetScrollPos(wxVERTICAL, m_scroll_pos);
		//UpdateSelection();
		vr_frame->RefreshVRenderViews(false, true);
	}
}

void DataTreeCtrl::GetSelectedItem(wxString &name, int &type)
{
    name = "";
    type = -1;

    wxTreeItemId sel_item = GetSelection();

    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    
    if (sel_item.IsOk())
    {
        LayerInfo* item_data = (LayerInfo*)GetItemData(sel_item);
        if (item_data)
        {
            name = GetItemBaseText(sel_item);
            type = item_data->type;
        }
    }
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(TreePanel, wxPanel)
	EVT_TOOL(ID_ToggleView, TreePanel::OnToggleView)
	EVT_TOOL(ID_Save, TreePanel::OnSave)
	EVT_TOOL(ID_BakeVolume, TreePanel::OnBakeVolume)
	EVT_TOOL(ID_RemoveData, TreePanel::OnRemoveData)
	//brush commands
	EVT_TOOL(ID_BrushAppend, TreePanel::OnBrushAppend)
	EVT_TOOL(ID_BrushDesel, TreePanel::OnBrushDesel)
	EVT_TOOL(ID_BrushDiffuse, TreePanel::OnBrushDiffuse)
	EVT_TOOL(ID_BrushClear, TreePanel::OnBrushClear)
	EVT_TOOL(ID_BrushErase, TreePanel::OnBrushErase)
	EVT_TOOL(ID_BrushCreate, TreePanel::OnBrushCreate)
	END_EVENT_TABLE()

	TreePanel::TreePanel(wxWindow* frame,
	wxWindow* parent,
	wxWindowID id,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name) :
wxPanel(parent, id, pos, size, style, name),
	m_frame(frame)
{
	SetEvtHandlerEnabled(false);
	Freeze();

	//create data tree
	m_datatree = new DataTreeCtrl(frame, this, wxID_ANY);
	m_datatree->addObserver(this);

	//create tool bar
	m_toolbar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_FLAT|wxTB_TOP|wxTB_NODIVIDER);
    m_toolbar->SetToolBitmapSize(wxSize(20,20));
	m_toolbar->AddTool(ID_ToggleView, "Toggle View",
		wxGetBitmapFromMemory(listicon_toggle),
		"Toggle the visibility of current selection");
	m_toolbar->AddTool(ID_Save, "Save As",
         wxGetBitmapFromMemory(listicon_save),
         "Save: Save the selected volume dataset");
	m_toolbar->EnableTool(ID_Save, false);
	m_toolbar->AddTool(ID_BakeVolume, "Bake",
         wxGetBitmapFromMemory(listicon_bake),
         "Bake: Apply the volume properties and save");
	m_toolbar->EnableTool(ID_BakeVolume, false);
	m_toolbar->AddTool(ID_RemoveData, "Delete",
		wxGetBitmapFromMemory(listicon_delete),
		"Delete current selection");
	m_toolbar->AddSeparator();
	m_toolbar->AddCheckTool(ID_BrushAppend, "Highlight",
		wxGetBitmapFromMemory(listicon_brushappend),
		wxNullBitmap,
		"Highlight structures by painting on the render view (hold Shift)");
	m_toolbar->AddCheckTool(ID_BrushDiffuse, "Diffuse",
		wxGetBitmapFromMemory(listicon_brushdiffuse),
		wxNullBitmap,
		"Diffuse highlighted structures by painting (hold Z)");
	m_toolbar->AddCheckTool(ID_BrushDesel, "Reset",
		wxGetBitmapFromMemory(listicon_brushdesel),
		wxNullBitmap,
		"Reset highlighted structures by painting (hold X)");
	m_toolbar->AddTool(ID_BrushClear, "Reset All",
		wxGetBitmapFromMemory(listicon_brushclear),
		"Reset all highlighted structures");
	m_toolbar->AddSeparator();
	m_toolbar->AddTool(ID_BrushErase, "Erase",
		wxGetBitmapFromMemory(listicon_brusherase),
		"Erase highlighted structures");
	m_toolbar->AddTool(ID_BrushCreate, "Extract",
		wxGetBitmapFromMemory(listicon_brushcreate),
		"Extract highlighted structures out and create a new volume");
	m_toolbar->Realize();

	//organize positions
	wxBoxSizer* sizer_v = new wxBoxSizer(wxVERTICAL);

	sizer_v->Add(m_toolbar, 0, wxEXPAND);
	sizer_v->Add(m_datatree, 1, wxEXPAND);

	SetSizer(sizer_v);
	Layout();

	Thaw();
	SetEvtHandlerEnabled(true);
}

TreePanel::~TreePanel()
{
}

DataTreeCtrl* TreePanel::GetTreeCtrl()
{
	return m_datatree;
}

void TreePanel::ChangeIconColor(int i, wxColor c)
{
	if (m_datatree)
		m_datatree->ChangeIconColor(i, c);
}

void TreePanel::AppendIcon()
{
	if (m_datatree)
		m_datatree->AppendIcon();
}

void TreePanel::ClearIcons()
{
	if (m_datatree)
		m_datatree->ClearIcons();
}

int TreePanel::GetIconNum()
{
	int num = 0;
	if (m_datatree)
		num = m_datatree->GetIconNum();
	return num;
}

void TreePanel::SelectItem(wxTreeItemId item)
{
	if (m_datatree)
		m_datatree->SelectItem(item);
}

void TreePanel::Expand(wxTreeItemId item)
{
	if (m_datatree)
		m_datatree->Expand(item);
}

void TreePanel::ExpandAll()
{
	if (m_datatree)
	{
		m_datatree->ExpandAll();
		m_datatree->SetScrollPos(wxVERTICAL, 0);
	}
}

void TreePanel::DeleteAll()
{
	if (m_datatree)
		m_datatree->DeleteAll();
}

void TreePanel::TraversalDelete(wxTreeItemId item)
{
	if (m_datatree)
		m_datatree->TraversalDelete(item);
}

wxTreeItemId TreePanel::AddRootItem(const wxString &text)
{
	wxTreeItemId id;
	if (m_datatree)
		id = m_datatree->AddRootItem(text);
	return id;
}

void TreePanel::ExpandRootItem()
{
	if (m_datatree)
		m_datatree->ExpandRootItem();
}

wxTreeItemId TreePanel::AddViewItem(const wxString &text)
{
	wxTreeItemId id;
	if (m_datatree)
		id = m_datatree->AddViewItem(text);
	return id;
}

void TreePanel::SetViewItemImage(const wxTreeItemId& item, int image)
{
	if (m_datatree)
		m_datatree->SetViewItemImage(item, image);
}

wxTreeItemId TreePanel::AddVolItem(wxTreeItemId par_item, const wxString &text)
{
	wxTreeItemId id;
	if (m_datatree)
		id = m_datatree->AddVolItem(par_item, text);
	return id;
}

wxTreeItemId TreePanel::AddVolItem(wxTreeItemId par_item, VolumeData* vd)
{
	wxTreeItemId id;
	if (m_datatree)
		id = m_datatree->AddVolItem(par_item, vd);
	return id;
}

void TreePanel::UpdateROITreeIcons(VolumeData* vd)
{
	if (m_datatree)
		m_datatree->UpdateROITreeIcons(vd);
}
void TreePanel::UpdateROITreeIconColor(VolumeData* vd)
{
	if (m_datatree)
		m_datatree->UpdateROITreeIconColor(vd);
}

void TreePanel::UpdateVolItem(wxTreeItemId item, VolumeData* vd)
{
	if (m_datatree)
		m_datatree->UpdateVolItem(item, vd);
}

wxTreeItemId TreePanel::FindTreeItem(wxTreeItemId par_item, wxString name)
{
	wxTreeItemId id;
	if (m_datatree)
		id = m_datatree->FindTreeItem(par_item, name);
	return id;
}

wxTreeItemId TreePanel::FindTreeItem(wxString name)
{
	wxTreeItemId id;
	if (m_datatree)
		id = m_datatree->FindTreeItem(name);
	return id;
}

void TreePanel::SetVolItemImage(const wxTreeItemId item, int image)
{
	if (m_datatree)
		m_datatree->SetVolItemImage(item, image);
}

wxTreeItemId TreePanel::AddMeshItem(wxTreeItemId par_item, const wxString &text)
{
	wxTreeItemId id;
	if (m_datatree)
		id = m_datatree->AddMeshItem(par_item, text);
	return id;
}

wxTreeItemId TreePanel::AddMeshItem(wxTreeItemId par_item, MeshData* md)
{
    wxTreeItemId id;
    if (m_datatree)
        id = m_datatree->AddMeshItem(par_item, md);
    return id;
}

void TreePanel::UpdateROITreeIcons(MeshData* md)
{
    if (m_datatree)
        m_datatree->UpdateROITreeIcons(md);
}
void TreePanel::UpdateROITreeIconColor(MeshData* md)
{
    if (m_datatree)
        m_datatree->UpdateROITreeIconColor(md);
}

void TreePanel::UpdateMeshItem(wxTreeItemId item, MeshData* md)
{
    if (m_datatree)
        m_datatree->UpdateMeshItem(item, md);
}

void TreePanel::SetMeshItemImage(const wxTreeItemId item, int image)
{
	if (m_datatree)
		m_datatree->SetMeshItemImage(item, image);
}

wxTreeItemId TreePanel::AddAnnotationItem(wxTreeItemId par_item, const wxString &text)
{
	wxTreeItemId id;
	if (m_datatree)
		id = m_datatree->AddAnnotationItem(par_item, text);
	return id;
}

void TreePanel::SetAnnotationItemImage(const wxTreeItemId item, int image)
{
	if (m_datatree)
		m_datatree->SetAnnotationItemImage(item, image);
}

wxTreeItemId TreePanel::AddGroupItem(wxTreeItemId par_item, const wxString &text)
{
	wxTreeItemId id;
	if (m_datatree)
		id = m_datatree->AddGroupItem(par_item, text);
	return id;
}

void TreePanel::SetGroupItemImage(const wxTreeItemId item, int image)
{
	if (m_datatree)
		m_datatree->SetGroupItemImage(item, image);
}

wxTreeItemId TreePanel::AddMGroupItem(wxTreeItemId par_item, const wxString &text)
{
	wxTreeItemId id;
	if (m_datatree)
		id = m_datatree->AddMGroupItem(par_item, text);
	return id;
}

void TreePanel::SetMGroupItemImage(const wxTreeItemId item, int image)
{
	if (m_datatree)
		m_datatree->SetMGroupItemImage(item, image);
}

void TreePanel::SetItemName(const wxTreeItemId item, const wxString &name)
{
	if (m_datatree)
		m_datatree->SetItemText(item, name);
}

void TreePanel::UpdateSelection()
{
	if (m_datatree)
		m_datatree->UpdateSelection();
}

wxString TreePanel::GetCurrentSel()
{
	wxString str = "";
	if (m_datatree)
		str = m_datatree->GetCurrentSel();
	return str;
}

void TreePanel::Select(wxString view, wxString name)
{
	if (m_datatree)
		m_datatree->Select(view, name);
}

void TreePanel::SelectROI(VolumeData* vd, int id)
{
	if (m_datatree)
		m_datatree->SelectROI(vd, id);
}

void TreePanel::SelectROI(MeshData* md, int id)
{
    if (m_datatree)
        m_datatree->SelectROI(md, id);
}

void TreePanel::SelectBrush(int id)
{
	m_toolbar->ToggleTool(ID_BrushAppend, false);
	m_toolbar->ToggleTool(ID_BrushDiffuse, false);
	m_toolbar->ToggleTool(ID_BrushDesel, false);
	m_datatree->m_fixed = false;

	switch (id)
	{
	case ID_BrushAppend:
		m_toolbar->ToggleTool(ID_BrushAppend, true);
		m_datatree->m_fixed = true;
		break;
	case ID_BrushDiffuse:
		m_toolbar->ToggleTool(ID_BrushDiffuse, true);
		m_datatree->m_fixed = true;
		break;
	case ID_BrushDesel:
		m_toolbar->ToggleTool(ID_BrushDesel, true);
		m_datatree->m_fixed = true;
		break;
	}
}

int TreePanel::GetBrushSelected()
{
	if (m_toolbar->GetToolState(ID_BrushAppend))
		return ID_BrushAppend;
	else if (m_toolbar->GetToolState(ID_BrushDiffuse))
		return ID_BrushDiffuse;
	else if (m_toolbar->GetToolState(ID_BrushDesel))
		return ID_BrushDesel;
	else
		return 0;
}

void TreePanel::doAction(ActionInfo *info)
{
	if (m_datatree)
	{
		int type = m_datatree->GetCurrentSelType();
		if (type == 2)
		{
			m_toolbar->EnableTool(ID_Save, true);
			m_toolbar->EnableTool(ID_BakeVolume, true);
		}
		else
		{
			m_toolbar->EnableTool(ID_Save, false);
			m_toolbar->EnableTool(ID_BakeVolume, false);
		}
	}
    delete info;
}

void TreePanel::OnSave(wxCommandEvent& event)
{
	if (m_datatree)
		m_datatree->OnSave(event);
}

void TreePanel::OnBakeVolume(wxCommandEvent& event)
{
	if (m_datatree)
		m_datatree->OnBakeVolume(event);
}

void TreePanel::OnToggleView(wxCommandEvent &event)
{
	if (m_datatree)
	{
		wxTreeEvent tree_event;
		m_datatree->OnAct(tree_event);
	}
}

void TreePanel::OnRemoveData(wxCommandEvent &event)
{
	if (m_datatree)
		m_datatree->DeleteSelection();
}

void TreePanel::OnBrushAppend(wxCommandEvent &event)
{
	BrushAppend();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame && vr_frame->GetBrushToolDlg())
	{
		if (m_toolbar->GetToolState(ID_BrushAppend))
			vr_frame->GetBrushToolDlg()->SelectBrush(BrushToolDlg::ID_BrushAppend);
		else
			vr_frame->GetBrushToolDlg()->SelectBrush(0);
	}
}

void TreePanel::OnBrushDiffuse(wxCommandEvent &event)
{
	BrushDiffuse();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame && vr_frame->GetBrushToolDlg())
	{
		if (m_toolbar->GetToolState(ID_BrushDiffuse))
			vr_frame->GetBrushToolDlg()->SelectBrush(BrushToolDlg::ID_BrushDiffuse);
		else
			vr_frame->GetBrushToolDlg()->SelectBrush(0);
	}
}

void TreePanel::OnBrushDesel(wxCommandEvent &event)
{
	BrushDesel();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame && vr_frame->GetBrushToolDlg())
	{
		if (m_toolbar->GetToolState(ID_BrushDesel))
			vr_frame->GetBrushToolDlg()->SelectBrush(BrushToolDlg::ID_BrushDesel);
		else
			vr_frame->GetBrushToolDlg()->SelectBrush(0);
	}
}

void TreePanel::OnBrushClear(wxCommandEvent &event)
{
	BrushClear();
}

void TreePanel::OnBrushErase(wxCommandEvent &event)
{
	BrushErase();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame && vr_frame->GetBrushToolDlg())
		vr_frame->GetBrushToolDlg()->SelectBrush(0);
}

void TreePanel::OnBrushCreate(wxCommandEvent &event)
{
	BrushCreate();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame && vr_frame->GetBrushToolDlg())
		vr_frame->GetBrushToolDlg()->SelectBrush(0);
}

//control from outside
void TreePanel::BrushAppend()
{
	m_toolbar->ToggleTool(ID_BrushDiffuse, false);
	m_toolbar->ToggleTool(ID_BrushDesel, false);

	if (m_toolbar->GetToolState(ID_BrushAppend))
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			for (int i=0; i<vr_frame->GetViewNum(); i++)
			{
				VRenderView* vrv = vr_frame->GetView(i);
				if (vrv)
				{
					vrv->SetIntMode(2);
					vrv->SetPaintMode(2);
					m_datatree->m_fixed = true;
				}
			}
		}
	}
	else
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			for (int i=0; i<vr_frame->GetViewNum(); i++)
			{
				VRenderView* vrv = vr_frame->GetView(i);
				if (vrv)
				{
					vrv->SetIntMode(1);
					m_datatree->m_fixed = false;
				}
			}
		}
	}
}

void TreePanel::BrushDiffuse()
{
	m_toolbar->ToggleTool(ID_BrushAppend, false);
	m_toolbar->ToggleTool(ID_BrushDesel, false);

	if (m_toolbar->GetToolState(ID_BrushDiffuse))
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			for (int i=0; i<vr_frame->GetViewNum(); i++)
			{
				VRenderView* vrv = vr_frame->GetView(i);
				if (vrv)
				{
					vrv->SetIntMode(2);
					vrv->SetPaintMode(4);
					m_datatree->m_fixed = true;
				}
			}
		}
	}
	else
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			for (int i=0; i<vr_frame->GetViewNum(); i++)
			{
				VRenderView* vrv = vr_frame->GetView(i);
				if (vrv)
				{
					vrv->SetIntMode(1);
					m_datatree->m_fixed = false;
				}
			}
		}
	}
}

void TreePanel::BrushSolid(bool state)
{
	m_toolbar->ToggleTool(ID_BrushAppend, false);
	m_toolbar->ToggleTool(ID_BrushDiffuse, false);
	m_toolbar->ToggleTool(ID_BrushDesel, false);

	if (state)
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			for (int i=0; i<vr_frame->GetViewNum(); i++)
			{
				VRenderView* vrv = vr_frame->GetView(i);
				if (vrv)
				{
					vrv->SetIntMode(2);
					vrv->SetPaintMode(8);
					m_datatree->m_fixed = true;
				}
			}
		}
	}
	else
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			for (int i=0; i<vr_frame->GetViewNum(); i++)
			{
				VRenderView* vrv = vr_frame->GetView(i);
				if (vrv)
				{
					vrv->SetIntMode(1);
					m_datatree->m_fixed = false;
				}
			}
		}
	}
}

void TreePanel::BrushDesel()
{
	m_toolbar->ToggleTool(ID_BrushAppend, false);
	m_toolbar->ToggleTool(ID_BrushDiffuse, false);

	if (m_toolbar->GetToolState(ID_BrushDesel))
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			for (int i=0; i<vr_frame->GetViewNum(); i++)
			{
				VRenderView* vrv = vr_frame->GetView(i);
				if (vrv)
				{
					vrv->SetIntMode(2);
					vrv->SetPaintMode(3);
					m_datatree->m_fixed = true;
				}
			}
		}
	}
	else
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			for (int i=0; i<vr_frame->GetViewNum(); i++)
			{
				VRenderView* vrv = vr_frame->GetView(i);
				if (vrv)
				{
					vrv->SetIntMode(1);
					m_datatree->m_fixed = false;
				}
			}
		}
	}
}

void TreePanel::BrushClear()
{
	if (m_datatree)
		m_datatree->BrushClear();

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		for (int i=0; i<vr_frame->GetViewNum(); i++)
		{
			VRenderView* vrv = vr_frame->GetView(i);
			if (vrv)
			{
				vrv->SetIntMode(4);
			}
		}
	}
}

void TreePanel::BrushErase()
{
	m_toolbar->ToggleTool(ID_BrushAppend, false);
	m_toolbar->ToggleTool(ID_BrushDiffuse, false);
	m_toolbar->ToggleTool(ID_BrushDesel, false);

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		for (int i=0; i<vr_frame->GetViewNum(); i++)
		{
			VRenderView* vrv = vr_frame->GetView(i);
			if (vrv)
			{
				vrv->SetIntMode(1);
				m_datatree->m_fixed = false;
			}
		}
	}

	if (m_datatree)
		m_datatree->BrushCreateInv();
}

void TreePanel::BrushCreate()
{
	m_toolbar->ToggleTool(ID_BrushAppend, false);
	m_toolbar->ToggleTool(ID_BrushDiffuse, false);
	m_toolbar->ToggleTool(ID_BrushDesel, false);

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		for (int i=0; i<vr_frame->GetViewNum(); i++)
		{
			VRenderView* vrv = vr_frame->GetView(i);
			if (vrv)
			{
				vrv->SetIntMode(1);
				m_datatree->m_fixed = false;
			}
		}
	}

	if (m_datatree)
		m_datatree->BrushCreate();
}

VRenderView* TreePanel::GetCurrentView()
{
	if (!m_datatree) return NULL;

	return m_datatree->GetCurrentView();
}

void TreePanel::SaveExpState()
{
	if (m_datatree) 
		m_datatree->SaveExpState();
}

void TreePanel::LoadExpState(bool expand_newitem)
{
	if (m_datatree) 
		m_datatree->LoadExpState(expand_newitem);
}

string TreePanel::ExportExpState()
{
	if (!m_datatree) return string();
	return m_datatree->ExportExpState();
}

void TreePanel::ImportExpState(const string &state)
{
	if (m_datatree) 
		m_datatree->ImportExpState(state);
}

wxTreeItemId TreePanel::GetParentVolItem(wxTreeItemId item)
{
	if (m_datatree) 
		return m_datatree->GetParentVolItem(item);
	else
		return wxTreeItemId();
}

wxTreeItemId TreePanel::GetNextSibling_loop(wxTreeItemId item)
{
	if (m_datatree) 
		return m_datatree->GetNextSibling_loop(item);
	else
		return wxTreeItemId();
}

void TreePanel::ExpandDataTreeItem(wxString name, bool expand_children)
{
	if (m_datatree) 
		m_datatree->ExpandDataTreeItem(name, expand_children);
}

void TreePanel::CollapseDataTreeItem(wxString name, bool collapse_children)
{
	if (m_datatree) 
		m_datatree->CollapseDataTreeItem(name, collapse_children);
}

void TreePanel::UndoVisibility()
{
	if (m_datatree)
		m_datatree->UndoVisibility();
}
void TreePanel::RedoVisibility()
{
	if (m_datatree)
		m_datatree->RedoVisibility();
}
void TreePanel::ClearVisHistory()
{
	if (m_datatree)
		m_datatree->ClearVisHistory();
}
void TreePanel::PushVisHistory()
{
	if (m_datatree)
		m_datatree->PushVisHistory();
}
void TreePanel::ShowAllDatasets()
{
    if (m_datatree)
        m_datatree->ShowAllDatasets();
}
void TreePanel::HideOtherDatasets()
{
    if (m_datatree)
        m_datatree->HideOtherDatasets();
}
void TreePanel::HideOtherDatasets(wxString name)
{
	if (m_datatree)
		m_datatree->HideOtherDatasets(name);
}
void TreePanel::HideOtherVolumes()
{
    if (m_datatree)
        m_datatree->HideOtherVolumes();
}
void TreePanel::HideOtherVolumes(wxString name)
{
	if (m_datatree)
		m_datatree->HideOtherVolumes(name);
}
void TreePanel::HideSelectedItem()
{
	if (m_datatree)
		m_datatree->HideSelectedItem();
}

void TreePanel::GetSelectedItem(wxString &name, int &type)
{
    if (m_datatree)
        m_datatree->GetSelectedItem(name, type);
}
