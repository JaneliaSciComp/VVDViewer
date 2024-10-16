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

#include "VRenderView.h"
#include "VRenderFrame.h"
#include <tiffio.h>
#include <wx/utils.h>
#include <wx/valnum.h>
#include <map>
#include <wx/gdicmn.h>
#include <wx/display.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <algorithm>
#include <limits>
#include <unordered_set>
#include "png_resource.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
//#include <boost/process.hpp>
#if defined(_WIN32)
  #include <Windows.h>
#elif defined(__WXMAC__)
  #include "MetalCompatibility.h"
#elif defined(__WXGTK__)
  #include <gtk/gtk.h>
  #if defined(VK_USE_PLATFORM_WAYLAND_KHR) || defined(VK_USE_PLATFORM_XCB_KHR)
    #include <wayland-client.h>
    #include "xdg-shell-client-protocol.h"
	#include <gdk/gdkwayland.h>
    #include <gdk/gdkx.h>
    #include <X11/Xlib-xcb.h>
  #endif
#endif

int VRenderView::m_id = 1;
ImgShaderFactory VRenderVulkanView::m_img_shader_factory;

//uint64_t st_time, ed_time;
//char dbgstr[50];
//long long milliseconds_now() {
//    static LARGE_INTEGER s_frequency;
//    static BOOL s_use_qpc = QueryPerformanceFrequency(&s_frequency);
//    if (s_use_qpc) {
//        LARGE_INTEGER now;
//        QueryPerformanceCounter(&now);
//        return (1000LL * now.QuadPart) / s_frequency.QuadPart;
//    }
//    else {
//        return GetTickCount64();
//    }
//}

////////////////////////////////////////////////////////////////////////


BEGIN_EVENT_TABLE(LMTreeCtrl, wxTreeCtrl)
	//EVT_SET_FOCUS(LMTreeCtrl::OnSetFocus)
	EVT_MENU(ID_Expand, LMTreeCtrl::OnExpand)
	EVT_TREE_ITEM_ACTIVATED(wxID_ANY, LMTreeCtrl::OnAct)
	EVT_TREE_SEL_CHANGED(wxID_ANY, LMTreeCtrl::OnSelectChanged)
	EVT_KEY_DOWN(LMTreeCtrl::OnKeyDown)
	EVT_KEY_UP(LMTreeCtrl::OnKeyUp)
	END_EVENT_TABLE()

	LMTreeCtrl::LMTreeCtrl(
	VRenderVulkanView *glview,
	wxWindow* parent,
	wxWindowID id,
	const wxPoint& pos,
	const wxSize& size,
	long style) :
wxTreeCtrl(parent, id, pos, size, style),
	m_glview(glview),
	m_schtxtctrl(NULL)
{
	wxTreeItemId rootitem = AddRoot(wxT("Root"));
	LMItemInfo* item_data = new LMItemInfo;
	item_data->type = 0;//root;
	SetItemData(rootitem, item_data);

	//	ShowOnlyMachedItems(wxT(""));
}

LMTreeCtrl::~LMTreeCtrl()
{
}

void LMTreeCtrl::Append(wxString name, Point p)
{
}

void LMTreeCtrl::ShowOnlyMachedItems(wxString str)
{
	if (!m_glview) return;

	str.MakeLower();

	UnselectAll();

	SetEvtHandlerEnabled(false);
	Freeze();
	DeleteAllItems();

	// rebuild tree
	wxTreeItemId rootitem = AddRoot(wxT("Root"));
	LMItemInfo* item_data = new LMItemInfo;
	item_data->type = 0;//root;
	SetItemData(rootitem, item_data);

	vector<Ruler*>*  landmarks = m_glview->GetLandmarks();
	for(int i = 0; i < (*landmarks).size(); i++)
	{
		if((*landmarks)[i])
		{
			wxString text = (*landmarks)[i]->GetNameDisp();
			wxString lower = text;
			lower.MakeLower();
			if(str.IsEmpty() || lower.Find(str) == 0)
			{
				wxTreeItemId item = AppendItem(GetRootItem(), text);
				LMItemInfo* item_data = new LMItemInfo;
				item_data->type = 1;
				item_data->p = *(*landmarks)[i]->GetPoint(0);
				SetItemData(item, item_data);
			}
		}
	}

	SortChildren(GetRootItem());

	Thaw();
	SetEvtHandlerEnabled(true);

	ExpandAll();
	wxTreeItemIdValue cookie2;
	wxTreeItemId topitem = GetFirstChild(GetRootItem(), cookie2);
	ScrollTo(topitem);
}

void LMTreeCtrl::CopyToSchTextCtrl()
{
	wxTreeItemId sel_item = GetSelection();
	if(sel_item.IsOk() && m_schtxtctrl)
	{
		m_schtxtctrl->SetValue(GetItemText(sel_item));
	}
}

void LMTreeCtrl::StartManip(wxString str)
{

}

void LMTreeCtrl::OnSetFocus(wxFocusEvent& event)
{
	CopyToSchTextCtrl();
	Hide();
	event.Skip();
}

void LMTreeCtrl::OnAct(wxTreeEvent &event)
{
	wxTreeItemId sel_item = GetSelection();
	if(sel_item.IsOk())
	{

	}
}

void LMTreeCtrl::OnSelectChanged(wxTreeEvent &event)
{
	wxTreeItemId sel_item = GetSelection();
	if(sel_item.IsOk() && FindFocus() == (wxWindow *)this)
	{
		if (m_glview)
		{
			double cx, cy, cz;
			LMItemInfo* itemdata = (LMItemInfo *)GetItemData(sel_item);
			if (itemdata)
			{
				Point p = itemdata->p;
				m_glview->GetObjCenters(cx, cy, cz);
				Point trans = Point(cx-p.x(), -(cy-p.y()), -(cz-p.z()));
				m_glview->StartManipulation(NULL, NULL, NULL, &trans, NULL);
			}
		}
		CopyToSchTextCtrl();
		Hide();
		//Unselect();
	}
	event.Skip();
}

void LMTreeCtrl::OnExpand(wxCommandEvent &event)
{

}

void LMTreeCtrl::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

void LMTreeCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}


////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(LMSeacher, wxTextCtrl)

	//EVT_CHILD_FOCUS(LMSeacher::OnSetChildFocus)
	EVT_SET_FOCUS(LMSeacher::OnSetFocus)
	EVT_KILL_FOCUS(LMSeacher::OnKillFocus)
	EVT_TEXT(wxID_ANY, LMSeacher::OnSearchText)
	EVT_TEXT_ENTER(wxID_ANY, LMSeacher::OnEnterInSearcher)
	//EVT_IDLE(LMSeacher::OnIdle)
	EVT_KEY_DOWN(LMSeacher::OnKeyDown)
	EVT_KEY_UP(LMSeacher::OnKeyUp)
	EVT_MOUSE_EVENTS(LMSeacher::OnMouse)
	END_EVENT_TABLE()

	LMSeacher::LMSeacher(
	VRenderVulkanView* glview,
	wxWindow* parent,
	wxWindowID id,
	const wxString& text,
	const wxPoint& pos,
	const wxSize& size,
	long style) :
wxTextCtrl(parent, id, "", pos, size, style),
	m_glview(glview)
{
	SetEvtHandlerEnabled(false);
	Freeze();

	SetHint(text);

	m_dummy = new wxButton(parent, id, text, pos, size);
	m_dummy->Hide();

	wxSize listsize = size;
	listsize.SetHeight(300);
	m_list = new LMTreeCtrl(glview, parent, id, wxPoint(pos.x, pos.y + 30), listsize);
	m_list->SetSearcher(this);

	m_list->Hide();

	Thaw();
	SetEvtHandlerEnabled(true);
}

LMSeacher::~LMSeacher()
{

}

void LMSeacher::KillFocus()
{
	if(m_glview) m_glview->SetKeyLock(false);
	m_dummy->SetFocus();
	m_list->Hide();
}

void LMSeacher::OnSetChildFocus(wxChildFocusEvent& event)
{
	if(m_glview) m_glview->SetKeyLock(true);
	m_list->ShowOnlyMachedItems(GetValue());
	m_list->Show();
	event.Skip();
}

void LMSeacher::OnSetFocus(wxFocusEvent& event)
{
	if(m_glview) m_glview->SetKeyLock(true);
	m_list->ShowOnlyMachedItems(GetValue());
	m_list->Show();
	event.Skip();
}

void LMSeacher::OnKillFocus(wxFocusEvent& event)
{
	if(m_glview) m_glview->SetKeyLock(false);
	m_list->Hide();
	event.Skip();
}

void LMSeacher::OnSearchText(wxCommandEvent &event)
{
	if(m_glview) m_glview->SetKeyLock(true);
	m_list->ShowOnlyMachedItems(GetValue());
	m_list->Show();
}

void LMSeacher::OnEnterInSearcher(wxCommandEvent &event)
{
	if(m_glview) m_glview->SetKeyLock(false);
	m_list->CopyToSchTextCtrl();
	wxString str = GetValue();
	str.MakeLower();
	if (m_glview)
	{
		vector<Ruler*>*  landmarks = m_glview->GetLandmarks();
		for(int i = 0; i < (*landmarks).size(); i++)
		{
			if((*landmarks)[i])
			{
				wxString text = (*landmarks)[i]->GetNameDisp();
				wxString lower = text;
				lower.MakeLower();
				if(lower == str)
				{
					double cx, cy, cz;
					Point *p = (*landmarks)[i]->GetPoint(0);
					m_glview->GetObjCenters(cx, cy, cz);
					Point trans = Point(cx-p->x(), -(cy-p->y()), -(cz-p->z()));
					m_glview->StartManipulation(NULL, NULL, NULL, &trans, NULL);
					break;
				}
			}
		}
	}
	//

	m_dummy->SetFocus();
	m_list->Hide();
	event.Skip();
}

void LMSeacher::OnKeyDown(wxKeyEvent& event)
{
	if(event.GetKeyCode() == WXK_DOWN)
	{
		wxTreeItemId sel_item = m_list->GetSelection();
		if(sel_item.IsOk())
		{
			wxTreeItemId nextsib = m_list->GetNextSibling(sel_item);
			if(nextsib.IsOk()) m_list->SelectItem(nextsib);
		}
		else 
		{
			wxTreeItemIdValue cookie;
			wxTreeItemId first = m_list->GetFirstChild(m_list->GetRootItem(), cookie);
			if(first.IsOk()) m_list->SelectItem(first);
		}

		return;
	}
	if(event.GetKeyCode() == WXK_UP)
	{
		wxTreeItemId sel_item = m_list->GetSelection();
		if(sel_item.IsOk())
		{
			wxTreeItemId prevsib = m_list->GetPrevSibling(sel_item);
			if(prevsib.IsOk()) m_list->SelectItem(prevsib);
			else m_list->UnselectAll();
		}

		return;
	}
	event.Skip();
}

void LMSeacher::OnIdle(wxIdleEvent &event)
{
	if(VRenderFrame::GetKeyState(WXK_DOWN) && m_list->IsShown())
	{
		wxTreeItemId sel_item = m_list->GetSelection();
		if(sel_item.IsOk())
		{
			wxTreeItemId nextsib = m_list->GetNextSibling(sel_item);
			if(nextsib.IsOk()) m_list->SelectItem(nextsib);
		}
		else 
		{
			wxTreeItemIdValue cookie;
			wxTreeItemId first = m_list->GetFirstChild(m_list->GetRootItem(), cookie);
			if(first.IsOk())
			{
				m_list->SelectItem(first);
			}
		}
	}
}

void LMSeacher::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void LMSeacher::OnMouse(wxMouseEvent& event)
{
	event.Skip();
}

//////////////////////////////////////////////////////////////////////////

wxCriticalSection* VRenderVulkanView::ms_pThreadCS = nullptr;

BEGIN_EVENT_TABLE(VRenderVulkanView, wxWindow)
	EVT_MENU(ID_CTXMENU_SHOW_ALL, VRenderVulkanView::OnShowAllDatasets)
	EVT_MENU(ID_CTXMENU_HIDE_OTHER_VOLS, VRenderVulkanView::OnHideOtherDatasets)
	EVT_MENU(ID_CTXMENU_HIDE_THIS_VOL, VRenderVulkanView::OnHideSelectedDataset)
	EVT_MENU(ID_CTXMENU_SHOW_ALL_FRAGMENTS, VRenderVulkanView::OnShowAllFragments)
    EVT_MENU(ID_CTXMENU_DESELECT_ALL_FRAGMENTS, VRenderVulkanView::OnDeselectAllFragments)
	EVT_MENU(ID_CTXMENU_HIDE_OTHER_FRAGMENTS, VRenderVulkanView::OnHideOtherFragments)
	EVT_MENU(ID_CTXMENU_HIDE_SELECTED_FLAGMENTS, VRenderVulkanView::OnHideSelectedFragments)
	EVT_MENU(ID_CTXMENU_JOIN_SEGMENTS, VRenderVulkanView::OnJoinSegments)
	EVT_MENU(ID_CTXMENU_SPLIT_SEGMENTS, VRenderVulkanView::OnSplitSegments)
	EVT_MENU(ID_CTXMENU_UNDO_VISIBILITY_SETTING_CHANGES, VRenderVulkanView::OnUndoVisibilitySettings)
	EVT_MENU(ID_CTXMENU_REDO_VISIBILITY_SETTING_CHANGES, VRenderVulkanView::OnRedoVisibilitySettings)
	EVT_PAINT(VRenderVulkanView::OnDraw)
	EVT_SIZE(VRenderVulkanView::OnResize)
	EVT_ERASE_BACKGROUND(VRenderVulkanView::OnErase)
#if defined(__WXGTK__)
	EVT_IDLE(VRenderVulkanView::OnIdle)
#else
	EVT_TIMER(ID_Timer ,VRenderVulkanView::OnIdle)
#endif
	EVT_MOUSE_EVENTS(VRenderVulkanView::OnMouse)
	EVT_KEY_DOWN(VRenderVulkanView::OnKeyDown)
END_EVENT_TABLE()

VRenderVulkanView::VRenderVulkanView(wxWindow* frame,
	wxWindow *parent,
	wxWindowID id,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name)
	: wxWindow(parent, id, pos, size, style, name),
	//public
	//capture modes
	m_capture(false),
	m_capture_rotat(false),
	m_capture_rotate_over(false),
	m_capture_tsequ(false),
	m_capture_bat(false),
	m_capture_param(false),
	m_capture_resx(-1),
	m_capture_resy(-1),
	m_tile_rendering(false),
	m_postdraw(false),
	//begin and end frame
	m_begin_frame(0),
	m_end_frame(0),
	//counters
	m_tseq_cur_num(0),
	m_tseq_prv_num(0),
	m_param_cur_num(0),
	m_total_frames(0),
	//hud
	m_updating(true),
	m_draw_annotations(true),
	m_draw_camctr(false),
	m_camctr_size(2.0),
	m_draw_info(false),
	m_draw_coord(false),
	m_drawing_coord(false),
	m_draw_frame(false),
	m_test_speed(false),
	m_draw_clip(false),
	m_draw_legend(true),
	m_mouse_focus(false),
	m_test_wiref(false),
	m_draw_rulers(true),
	//current volume
	m_cur_vol(0),
	//clipping settings
	m_clip_mask(-1),
	m_clip_mode(2),
	//scale bar
	m_disp_scale_bar(false),
	m_disp_scale_bar_text(false),
	m_sb_length(50),
	m_sb_unit(1),
	m_sb_height(0.0),
	//ortho size
	m_ortho_left(0.0),
	m_ortho_right(1.0),
	m_ortho_bottom(0.0),
	m_ortho_top(1.0),
	//scale factor
	m_scale_factor(1.0),
	m_scale_factor_saved(1.0),
	//mode in determining depth of volume
	m_point_volume_mode(0),
	//ruler use volume transfer function
	m_ruler_use_transf(false),
	//ruler time dependent
	m_ruler_time_dep(true),
	//linked rotation
	m_linked_rot(false),
	//private
	m_frame(frame),
	m_vrv((VRenderView*)parent),
	//populated lists of data
	m_vd_pop_dirty(true),
	m_md_pop_dirty(true),
	//ruler type
	m_ruler_type(0),
	m_editing_ruler_point(0),
	//traces
	m_trace_group(0),
	//multivolume
	m_mvr(0),
	//initializaion
	m_initialized(false),
	m_init_view(false),
	//bg color
	m_bg_color(0.0, 0.0, 0.0),
	m_bg_color_inv(1.0, 1.0, 1.0),
	m_grad_bg(false),
	//frustrum
	m_aov(15.0),
	m_near_clip(0.1),
	m_far_clip(100.0),
	//interpolation
	m_intp(true),
	//previous focus
	m_prev_focus(0),
	//interactive modes
	m_int_mode(1),
	m_force_clear(false),
	m_interactive(false),
	m_clear_buffer(false),
	m_adaptive(true),
	m_brush_state(0),
	//resizing
	m_resize(false),
	m_resize_ol1(false),
	m_resize_ol2(false),
	m_resize_paint(false),
	//brush tools
	m_draw_brush(false),
	m_paint_enable(false),
	m_paint_display(false),
	//paint buffer
	m_clear_paint(true),
	m_tiled_image(NULL),
	//camera controls
	m_persp(false),
	m_free(false),
	//camera distance
	m_distance(10.0),
	m_init_dist(10.0),
	//camera translation
	m_transx(0.0), m_transy(0.0), m_transz(0.0),
	m_transx_saved(0.0), m_transy_saved(0.0), m_transz_saved(0.0),
	//camera rotation
	m_rotx(0.0), m_roty(0.0), m_rotz(0.0),
	m_rotx_saved(0.0), m_roty_saved(0.0), m_rotz_saved(0.0),
	//camera center
	m_ctrx(0.0), m_ctry(0.0), m_ctrz(0.0),
	m_ctrx_saved(0.0), m_ctry_saved(0.0), m_ctrz_saved(0.0),
	//camera direction
	m_up(0.0, 1.0, 0.0),
	m_head(0.0, 0.0, -1.0),
	//object center
	m_obj_ctrx(0.0), m_obj_ctry(0.0), m_obj_ctrz(0.0),
	//object translation
	m_obj_transx(0.0), m_obj_transy(0.0), m_obj_transz(0.0),
	m_obj_transx_saved(0.0), m_obj_transy_saved(0.0), m_obj_transz_saved(0.0),
	//object rotation
	m_obj_rotx(0.0), m_obj_roty(0.0), m_obj_rotz(0.0),
	m_rot_lock(false),
	//object bounding box
	m_radius(5.0),
	//mouse position
	old_mouse_X(-1), old_mouse_Y(-1),
	prv_mouse_X(-1), prv_mouse_Y(-1),
	//draw controls
	m_draw_all(true),
	m_draw_bounds(false),
	m_draw_grid(false),
	m_draw_type(1),
	m_vol_method(VOL_METHOD_SEQ),
	m_peeling_layers(1),
	m_blend_slices(false),
	//fog
	m_use_fog(true),
	m_fog_intensity(0.0),
	m_fog_start(0.0),
	m_fog_end(0.0),
	//movie properties
	m_init_angle(0.0),
	m_start_angle(0.0),
	m_end_angle(0.0),
	m_cur_angle(0.0),
	m_step(0.0),
	m_rot_axis(0),
	m_movie_seq(0),
	m_rewind(false),
	m_fr_length(0),
	m_stages(0),
	m_4d_rewind(false),
	//movie frame properties
	m_frame_x(-1),
	m_frame_y(-1),
	m_frame_w(-1),
	m_frame_h(-1),
	//post image processing
	m_gamma(Color(1.0, 1.0, 1.0)),
	m_brightness(Color(1.0, 1.0, 1.0)),
	m_hdr(0.0, 0.0, 0.0),
    m_levels(Color(1.0, 1.0, 1.0)),
	m_sync_r(false),
	m_sync_g(false),
	m_sync_b(false),
	//volume color map
	//m_value_1(0.0),
	m_value_2(0.0),
	m_value_3(0.25),
	m_value_4(0.5),
	m_value_5(0.75),
	m_value_6(1.0),
	//m_value_7(1.0),
	m_color_1(Color(0.0, 0.0, 1.0)),
	m_color_2(Color(0.0, 0.0, 1.0)),
	m_color_3(Color(0.0, 1.0, 1.0)),
	m_color_4(Color(0.0, 1.0, 0.0)),
	m_color_5(Color(1.0, 1.0, 0.0)),
	m_color_6(Color(1.0, 0.0, 0.0)),
	m_color_7(Color(1.0, 0.0, 0.0)),
	//paint brush presssure
	m_use_pres(false),
	m_on_press(false),
#ifdef _WIN32
	m_hTab(0),
#endif
	//paint stroke radius
	m_brush_radius1(10),
	m_brush_radius2(30),
	m_use_brush_radius2(true),
	//paint stroke spacing
	m_brush_spacing(0.1),
	//clipping plane rotations
	m_rotx_cl(0), m_roty_cl(0), m_rotz_cl(0),
	m_pressure(0.0),
	//selection
	m_pick(false),
	m_draw_mask(true),
	m_clear_mask(false),
	m_draw_label(false),
	//move view
	m_move_left(false),
	m_move_right(false),
	m_move_up(false),
	m_move_down(false),
	//move time
	m_tseq_forward(false),
	m_tseq_backward(false),
	//move clip
	m_clip_up(false),
	m_clip_down(false),
	//full cell
	m_cell_full(false),
	//link cells
	m_cell_link(false),
	//new cell id
	m_cell_new_id(false),
	m_pre_draw(true),
	m_key_lock(false),
	m_manip(false),
	m_manip_fps(20.0),
	m_manip_time(300),
	m_min_ppi(20),
	m_res_mode(0),
	m_res_scale(1.0),
	m_draw_landmarks(false),
	m_draw_overlays_only(false),
	m_enhance_sel(true),
	m_adaptive_res(false),
	m_int_res(false),
	m_dpeel(false),
	m_load_in_main_thread(true),
	m_finished_peeling_layer(0),
	m_fix_sclbar(false),
	m_fixed_sclbar_len(0.0),
	m_fixed_sclbar_fac(0.0),
	m_sclbar_digit(3),
	m_clear_final_buffer(false),
    m_refresh(false),
	m_refresh_start_loop(false),
	m_recording(false),
	m_recording_frame(false),
	m_abort(false),
    m_easy_2d_adjust(false),
	m_loader_run(true),
	m_ebd_run(true),
    m_undo_keydown(false),
    m_redo_keydown(false),
    m_use_fog_mesh(false),
    m_use_absolute_value(false),
	m_peeling_layer_progress(0),
	m_mesh_rendering_progress(0)
{
	SetEvtHandlerEnabled(false);
	Freeze();

#if defined(_WIN32) || defined(__WXMAC__)
	InitVulkan();
#endif

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
        m_selector.SetDataManager(vr_frame->GetDataManager());
	}

	goTimer = new nv::Timer(10);
	m_sb_num = "50";

#ifdef _WIN32
	//tablet initialization
	if (m_use_pres && LoadWintab())
	{
		gpWTInfoA(0, 0, NULL);
		m_hTab = TabletInit((HWND)GetHWND());
	}
#endif

	LoadDefaultBrushSettings();

	//m_searcher = new LMSeacher(this, (wxWindow *)this, ID_Searcher, wxT("Search"), wxPoint(20, 20), wxSize(200, -1), wxTE_PROCESS_ENTER);
	//m_searcher->Hide();
#if defined(__WXMAC__)
	m_dummy = new wxButton(this, wxID_ANY, wxT(""), wxPoint(-8, 8), wxSize(1,1));
#else
    m_dummy = new wxButton(this, wxID_ANY, wxT(""), wxPoint(-1, -1), wxSize(1,1));
#endif

	Thaw();
	SetEvtHandlerEnabled(true);
	
	m_idleTimer = new wxTimer(this, ID_Timer);
	m_idleTimer->Start(50);
}

#if defined(VK_USE_PLATFORM_WAYLAND_KHR) || defined(VK_USE_PLATFORM_XCB_KHR)

void VVVUpdatePosition(VRenderVulkanView* win)
{
    int x, y;
    gdk_window_get_origin(win->GTKGetDrawingWindow(), &x, &y);
    wl_subsurface_set_position(win->m_wlSubsurface, x, y);
}

extern "C"
{

	static void wl_global(void *data,
						  struct wl_registry *wl_registry,
						  uint32_t name,
						  const char *interface,
						  uint32_t)
	{
		VRenderVulkanView *vvv = static_cast<VRenderVulkanView *>(data);

		if (!strcmp(interface, "wl_compositor"))
			vvv->m_wlCompositor = (struct wl_compositor *)wl_registry_bind(wl_registry, name, &wl_compositor_interface, 3);
		else if (!strcmp(interface, "wl_subcompositor"))
			vvv->m_wlSubcompositor = (struct wl_subcompositor *)wl_registry_bind(wl_registry, name, &wl_subcompositor_interface, 1);
	}

	static void wl_global_remove(void *,
								 struct wl_registry *,
								 uint32_t)
	{
	}

	static const struct wl_registry_listener wl_registry_listener = {
		wl_global,
		wl_global_remove};

	static void wl_frame_callback_handler(void *data,
										  struct wl_callback *,
										  uint32_t)
	{
		VRenderVulkanView *vvv = static_cast<VRenderVulkanView *>(data);
		vvv->m_readyToDraw = true;
		g_clear_pointer(&vvv->m_wlFrameCallbackHandler, wl_callback_destroy);
		vvv->SendSizeEvent();
		gtk_widget_queue_draw(vvv->m_wxwindow);
	}

	static const struct wl_callback_listener wl_frame_listener = {
		wl_frame_callback_handler};

	static void gtk_vvv_size_callback(GtkWidget *widget,
										   GtkAllocation *,
										   VRenderVulkanView *win)
	{
		int scale = gtk_widget_get_scale_factor(widget);
		//wl_egl_window_resize(win->m_wlEGLWindow, win->m_width * scale, win->m_height * scale, 0, 0);

		VVVUpdatePosition(win);
	}
}
#endif

void VRenderVulkanView::InitVulkan()
{
	if (m_vulkan)
		return;

	SetEvtHandlerEnabled(false);
	Freeze();

	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(), NULL);
#ifdef _DARWIN
	wxString dft = expath + "/../Resources/" + SETTING_FILE_NAME;
#else
	wxString dft = expath + GETSLASHS() + SETTING_FILE_NAME;
	if (!wxFileExists(dft))
		dft = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + SETTING_FILE_NAME;
#endif
	wxFileInputStream is(dft);
	if (!is.IsOk())
		return;
	wxFileConfig fconfig(is);

	int gpu = -1;
	int val = -1;
	if (fconfig.Read("gpu", &val))
		gpu = val;

#if defined(_WIN32)
	m_vulkan = make_shared<VVulkan>();
	m_vulkan->initVulkan(gpu);
	m_vulkan->setWindow((HWND)GetHWND(), GetModuleHandle(NULL));
#elif defined(__WXMAC__)
	m_vulkan = make_shared<VVulkan>();
	m_vulkan->initVulkan(gpu);
    makeViewMetalCompatible(GetHandle());
	m_vulkan->setWindow(GetHandle());
#elif defined(__WXGTK__)
	GtkWidget* gtk_widget = GetHandle();
	GdkWindow *gtk_window = gtk_widget_get_window(gtk_widget);
	if (!gtk_window)
	{
		Thaw();
		SetEvtHandlerEnabled(true);
		return;
	}
	GdkDisplay *gtk_display = gdk_window_get_display(gtk_window);
	m_vulkan = make_shared<VVulkan>();
#if defined(VK_USE_PLATFORM_WAYLAND_KHR) || defined(VK_USE_PLATFORM_XCB_KHR)
	if (GDK_IS_WAYLAND_DISPLAY(gtk_display))
	{
		//m_readyToDraw = false;
		m_vulkan->initVulkan(VVulkan::PLATFORM_WAYLAND, gpu);
		wl_display* wdisp = gdk_wayland_display_get_wl_display(gtk_display);
		wl_surface* wsurf = gdk_wayland_window_get_wl_surface(gtk_window);
		m_vulkan->setWindow(wdisp, wsurf);

		int w = gdk_window_get_width(gtk_window);
        int h = gdk_window_get_height(gtk_window);
        struct wl_display *display = gdk_wayland_display_get_wl_display(gdk_window_get_display(gtk_window));
        struct wl_surface *surface = gdk_wayland_window_get_wl_surface(gtk_window);
        struct wl_registry *registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &wl_registry_listener, this);
        wl_display_roundtrip(display);
        if ( !m_wlCompositor || !m_wlSubcompositor )
        {
            wxFAIL_MSG("Invalid Wayland compositor or subcompositor");
            return;
        }
        m_wlSurface = wl_compositor_create_surface(m_wlCompositor);
        m_wlRegion = wl_compositor_create_region(m_wlCompositor);
        m_wlSubsurface = wl_subcompositor_get_subsurface(m_wlSubcompositor,
                                                         m_wlSurface,
                                                         surface);
        wl_surface_set_input_region(m_wlSurface, m_wlRegion);
        wl_subsurface_set_desync(m_wlSubsurface);
        VVVUpdatePosition(this);
		m_vulkan->setWindow(display, m_wlSurface);

		m_wlFrameCallbackHandler = wl_surface_frame(surface);
        wl_callback_add_listener(m_wlFrameCallbackHandler, &wl_frame_listener, this);
        g_signal_connect(m_widget, "size-allocate", G_CALLBACK(gtk_vvv_size_callback), this);
	}
	else
	{
		m_vulkan->initVulkan(VVulkan::PLATFORM_X11, gpu);
		Display *dpy = GDK_DISPLAY_XDISPLAY(gtk_display);
    	xcb_window_t win = GDK_WINDOW_XID(gtk_window);
		xcb_connection_t *c = xcb_connect(NULL, NULL);
    	if (xcb_connection_has_error(c) > 0) {
    	    printf("Cannot connect to XCB.\nExiting ...\n");
    	    fflush(stdout);
    	    return;
    	}
		m_vulkan->setWindow(win, c);
	}
#endif
#endif
	m_vulkan->prepare();

	m_v2drender = make_shared<Vulkan2dRender>(m_vulkan);
	FLIVR::TextureRenderer::set_vulkan(m_vulkan, m_v2drender);

	m_vulkan->ResetRenderSemaphores();

	FLIVR::VolumeRenderer::init();
	FLIVR::MeshRenderer::init(m_vulkan);
    
    wxSize wsize = GetSize();
    m_vulkan->setSize(wsize.GetWidth(), wsize.GetHeight());

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		auto setting_dlg = vr_frame->GetSettingDlg();
		wxString font_file = setting_dlg ? setting_dlg->GetFontFile() : "";
		std::string exePath = wxStandardPaths::Get().GetExecutablePath().ToStdString();
		exePath = exePath.substr(0, exePath.find_last_of(std::string() + GETSLASH()));
		if (font_file != "")
			font_file = wxString(exePath) + GETSLASH() + wxString("Fonts") +
			GETSLASH() + font_file;
		else
			font_file = wxString(exePath) + GETSLASH() + wxString("Fonts") +
			GETSLASH() + wxString("FreeSans.ttf");
		m_text_renderer = new TextRenderer(font_file.ToStdString(), m_v2drender);
		if (setting_dlg)
			m_text_renderer->SetSize(setting_dlg->GetTextSize());
	}

	Thaw();
	SetEvtHandlerEnabled(true);
}

VRenderVulkanView::~VRenderVulkanView()
{
	goTimer->stop();
	delete goTimer;
	m_idleTimer->Stop();
	wxDELETE(m_idleTimer);
#ifdef _WIN32
	//tablet
	if (m_hTab)
		gpWTClose(m_hTab);
#endif

	m_loader.StopAll();
    m_ebd.ClearQueues();
    m_ebd.Join();

	int i;
	//delete groups
	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		if (m_layer_list[i]->IsA() == 5)//group
		{
			DataGroup* group = (DataGroup*)m_layer_list[i];
			delete group;
		}
		else if (m_layer_list[i]->IsA() == 6)//mesh group
		{
			MeshGroup* group = (MeshGroup*)m_layer_list[i];
			delete group;
		}
	}

	//delete rulers
	for (i=0; i<(int)m_ruler_list.size(); i++)
	{
		if (m_ruler_list[i])
			delete m_ruler_list[i];
	}

	for(int i = 0; i < m_landmarks.size(); i++)
	{
		if (m_landmarks[i])
			delete m_landmarks[i];
	}

	FLIVR::VolumeRenderer::finalize();
	FLIVR::TextureRenderer::finalize_vulkan();
	m_v2drender.reset();
	//m_vulkan.reset();
}

double VRenderVulkanView::GetAvailableGraphicsMemory(int device_id)
{
	double ret = 0.0;
	if (m_vulkan)
	{
		if (device_id >= 0 && device_id < m_vulkan->devices.size())
			ret = m_vulkan->devices[device_id]->available_mem;
	}

	return ret;
}

void VRenderVulkanView::OnErase(wxEraseEvent& event)
{

}

void VRenderVulkanView::SetSearcherVisibility(bool visibility)
{
	if(visibility)
	{
		m_draw_landmarks = true;
		//m_searcher->Show();
	}
	else
	{
		m_draw_landmarks = false;
		//m_searcher->Hide();
	}
}

#ifdef _WIN32
//tablet init
HCTX VRenderVulkanView::TabletInit(HWND hWnd)
{
	/* get default region */
	gpWTInfoA(WTI_DEFCONTEXT, 0, &m_lc);

	/* modify the digitizing region */
	m_lc.lcOptions |= CXO_MESSAGES;
	m_lc.lcPktData = PACKETDATA;
	m_lc.lcPktMode = PACKETMODE;
	m_lc.lcMoveMask = PACKETDATA;
	m_lc.lcBtnUpMask = m_lc.lcBtnDnMask;

	/* output in 10000 x 10000 grid */
	m_lc.lcOutOrgX = m_lc.lcOutOrgY = 0;
	m_lc.lcOutExtX = 10000;
	m_lc.lcOutExtY = 10000;

	/* open the region */
	return gpWTOpenA(hWnd, &m_lc, TRUE);

}
#endif

void VRenderVulkanView::OnResize(wxSizeEvent& event)
{
	Resize();
}

void VRenderVulkanView::Resize(bool refresh)
{
	wxSize size = GetSize();
	if (size.GetWidth() == 0 || size.GetHeight() == 0) {
		return;
	}

	if (m_vd_pop_dirty)
		PopVolumeList();

	int i;
	for (i=0; i<(int)m_vd_pop_list.size(); i++)
	{
		VolumeData* vd = m_vd_pop_list[i];
		if (vd)
		{
			VolumeRenderer* vr = vd->GetVR();
			if (vr)
				vr->resize();
		}
	}

	if (m_mvr)
		m_mvr->resize();

	m_resize = true;
	m_resize_ol1 = true;
	m_resize_ol2 = true;
	m_resize_paint = true;

	if (refresh)
	{
		if (m_vulkan)
			m_vulkan->setSize(size.GetWidth(), size.GetHeight());
		wxRect refreshRect(size);
		RefreshRect(refreshRect, false);
		RefreshGL();
	}
}

void VRenderVulkanView::Init()
{
	if (!m_initialized)
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			vr_frame->SetTextureRendererSettings();
			vr_frame->SetTextureUndos();
		}
		goTimer->start();
		
		//if (vr_frame && vr_frame->GetSettingDlg()) KernelProgram::set_device_id(vr_frame->GetSettingDlg()->GetCLDeviceID());
		//KernelProgram::init_kernels_supported();

		m_initialized = true;
	}
}

void VRenderVulkanView::Clear()
{
	m_loader.RemoveAllLoadedData();
	if (m_vulkan)
		m_vulkan->clearTexPools();

	//delete groups
	for (int i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		if (m_layer_list[i]->IsA() == 5)//group
		{
			DataGroup* group = (DataGroup*)m_layer_list[i];
			delete group;
		}
		else if (m_layer_list[i]->IsA() == 6)//mesh group
		{
			MeshGroup* group = (MeshGroup*)m_layer_list[i];
			delete group;
		}
	}
	m_cur_vol = NULL;

	m_layer_list.clear();
}

void VRenderVulkanView::HandleProjection(int nx, int ny)
{
	double aspect = (double)nx / (double)ny;
	if (!m_free)
		m_distance = m_radius/tan(d2r(m_aov/2.0))/m_scale_factor;
	else
		m_scale_factor = m_radius/tan(d2r(m_aov/2.0)) / ( CalcCameraDistance() + (m_radius/tan(d2r(m_aov/2.0))/12.0) );
	if (aspect>1.0)
	{
		m_ortho_left = -m_radius*aspect/m_scale_factor;
		m_ortho_right = -m_ortho_left;
		m_ortho_bottom = -m_radius/m_scale_factor;
		m_ortho_top = -m_ortho_bottom;
	}
	else
	{
		m_ortho_left = -m_radius/m_scale_factor;
		m_ortho_right = -m_ortho_left;
		m_ortho_bottom = -m_radius/aspect/m_scale_factor;
		m_ortho_top = -m_ortho_bottom;
	}
	if (m_persp)
	{
		m_proj_mat = glm::perspective(glm::radians(m_aov), aspect, m_near_clip, m_far_clip);
		if (m_tile_rendering) {
			int tilex = m_current_tileid % m_tile_xnum;
			int tiley = m_current_tileid / m_tile_xnum;
			double tilew = 2.0 * m_tile_w / nx;
			double tileh = 2.0 * m_tile_h / ny;
			double scalex = (double)nx/(double)(m_tile_w);
			double scaley = (double)ny/(double)(m_tile_h);
			double tx = -1.0 * (-1.0 + tilew/2.0 + tilew*tilex);
			double ty = -1.0 * (-1.0 + tileh/2.0 + tileh*tiley);
			auto scale = glm::scale(glm::vec3((float)scalex, (float)scaley, 1.f));
			auto translate = glm::translate(glm::vec3((float)tx, (float)ty, 0.f));
			m_proj_mat = scale * translate * m_proj_mat;
		}
	}
	else
	{
		m_proj_mat = glm::ortho(m_ortho_left, m_ortho_right, m_ortho_bottom, m_ortho_top,
			-m_far_clip/100.0, m_far_clip);
		if (m_tile_rendering) {
			int tilex = m_current_tileid % m_tile_xnum;
			int tiley = m_current_tileid / m_tile_xnum;
			float shiftX = (double)m_tile_w * (m_ortho_right - m_ortho_left)/m_capture_resx;
			float shiftY = (double)m_tile_h * (m_ortho_top - m_ortho_bottom)/m_capture_resy;
			m_proj_mat = glm::ortho(m_ortho_left + shiftX*tilex, m_ortho_left + shiftX*(tilex+1),
				m_ortho_bottom + shiftY*tiley, m_ortho_bottom + shiftY*(tiley+1), -m_far_clip/100.0, m_far_clip);
		}
	}
}

void VRenderVulkanView::HandleCamera()
{
	Vector pos(m_transx, m_transy, m_transz);
	pos.normalize();
	if (m_free)
		pos *= m_radius*0.01;
	else
		pos *= m_distance;
	m_transx = pos.x();
	m_transy = pos.y();
	m_transz = pos.z();

	if (m_free)
		m_mv_mat = glm::lookAt(glm::vec3(m_transx+m_ctrx, m_transy+m_ctry, m_transz+m_ctrz),
		glm::vec3(m_ctrx, m_ctry, m_ctrz),
		glm::vec3(m_up.x(), m_up.y(), m_up.z()));
	else
		m_mv_mat = glm::lookAt(glm::vec3(m_transx, m_transy, m_transz),
		glm::vec3(0.0), glm::vec3(m_up.x(), m_up.y(), m_up.z()));
}

//depth buffer calculation
double VRenderVulkanView::CalcZ(double z)
{
	double result = 0.0;
	if (m_persp)
	{
		if (z != 0.0)
		{
			result = (m_far_clip+m_near_clip)/(m_far_clip-m_near_clip)/2.0 +
				(-m_far_clip*m_near_clip)/(m_far_clip-m_near_clip)/z+0.5;
		}
	}
	else
		result = (z-m_near_clip)/(m_far_clip-m_near_clip);
	return result;
}

void VRenderVulkanView::CalcFogRange()
{
	BBox bbox;
	bool use_box = false;
	if (m_cur_vol)
	{
		bbox = m_cur_vol->GetBounds();
		use_box = true;
	}
	else if (!m_md_pop_list.empty())
	{
		for (size_t i=0; i<m_md_pop_list.size(); ++i)
		{
			if (m_md_pop_list[i]->GetDisp())
			{
				bbox.extend(m_md_pop_list[i]->GetBounds());
				use_box = true;
			}
		}
	}

	if (use_box)
	{
		Transform mv;
		mv.set(glm::value_ptr(m_mv_mat));

		double minz, maxz;
		minz = numeric_limits<double>::max();
		maxz = -numeric_limits<double>::max();

		vector<Point> points;
		points.push_back(Point(bbox.min().x(), bbox.min().y(), bbox.min().z()));
		points.push_back(Point(bbox.min().x(), bbox.min().y(), bbox.max().z()));
		points.push_back(Point(bbox.min().x(), bbox.max().y(), bbox.min().z()));
		points.push_back(Point(bbox.min().x(), bbox.max().y(), bbox.max().z()));
		points.push_back(Point(bbox.max().x(), bbox.min().y(), bbox.min().z()));
		points.push_back(Point(bbox.max().x(), bbox.min().y(), bbox.max().z()));
		points.push_back(Point(bbox.max().x(), bbox.max().y(), bbox.min().z()));
		points.push_back(Point(bbox.max().x(), bbox.max().y(), bbox.max().z()));

		Point p;
		for (size_t i=0; i<points.size(); ++i)
		{
			p = mv.transform(points[i]);
			minz = p.z()<minz?p.z():minz;
			maxz = p.z()>maxz?p.z():maxz;
		}

		minz = fabs(minz);
		maxz = fabs(maxz);
		m_fog_start = minz<maxz?minz:maxz;
		m_fog_end = maxz>minz?maxz:minz;
	}
	else
	{
		m_fog_start = m_distance - m_radius/2.0;
		m_fog_start = m_fog_start<0.0?0.0:m_fog_start;
		m_fog_end = m_distance + m_radius/4.0;
	}
}

//draw the volume data only
void VRenderVulkanView::Draw()
{


	int nx = m_nx;
	int ny = m_ny;

	//gradient background
	if (m_grad_bg)
	{
		DrawGradBg();
	}

	if (VolumeRenderer::get_done_update_loop() && !m_draw_overlays_only) StartLoopUpdate();

	if (m_draw_all)
	{
		//projection
		if (m_tile_rendering) HandleProjection(m_capture_resx, m_capture_resy);
		else HandleProjection(nx, ny);
		//Transformation
		HandleCamera();

		glm::mat4 mv_temp = m_mv_mat;
		//translate object
		m_mv_mat = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
		//rotate object
		m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
		m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
		m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
		//center object
		m_mv_mat = glm::translate(m_mv_mat, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

		if (VolumeRenderer::get_done_update_loop() && m_draw_overlays_only)
			DrawFinalBuffer(m_frame_clear);
		else
		{
			if (m_use_fog)
				CalcFogRange();

			if (m_draw_grid)
				DrawGrid();

			if (m_draw_clip)
				DrawClippingPlanes(false, BACK_FACE);

			//draw the volumes
			if (m_md_pop_list.empty() && m_vd_pop_list.empty())
			{
				if (m_frame_clear) {
					vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
					
					if (current_fbo)
					{
						Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
						if (!current_fbo->renderPass)
						{
							params.pipeline =
								m_v2drender->preparePipeline(
									IMG_SHDR_BLEND_BRIGHT_BACKGROUND_HDR,
									V2DRENDER_BLEND_OVER,
									current_fbo->attachments[0]->format,
									current_fbo->attachments.size(),
									0,
									current_fbo->attachments[0]->is_swapchain_images);
							current_fbo->replaceRenderPass(params.pipeline.pass);
						}
						params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 1.0f };
						m_v2drender->clear(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
						m_frame_clear = false;
					}

				}
			}
			else if (!m_dpeel) DrawVolumes();
			else DrawVolumesDP();
		}


		//draw the clipping planes
		if (m_draw_clip)
			DrawClippingPlanes(true, FRONT_FACE);

		if (m_draw_bounds)
			DrawBounds();

		if (m_draw_annotations)
			DrawAnnotations();

		if (m_draw_rulers)
			DrawRulers();

		//traces
		DrawTraces();

		m_mv_mat = mv_temp;
	}

	m_draw_overlays_only = false;
}

//draw meshes
//peel==true -- depth peeling
bool VRenderVulkanView::DrawMeshes(const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf, int peel, const std::shared_ptr<vks::VTexture> depth_tex)
{
	int count = 0;
	if (TextureRenderer::get_mem_swap() && m_mesh_rendering_progress > 0)
		clear_framebuf = false;
	for (int i=0; i<(int)m_layer_list.size() ; i++)
	{
		if (!m_layer_list[i])
			continue;
		if (m_layer_list[i]->IsA() == 3)
		{
			MeshData* md = (MeshData*)m_layer_list[i];
			if (md && md->GetDisp())
			{
				count++;
				if (TextureRenderer::get_mem_swap() && count <= m_mesh_rendering_progress)
					continue;

                bool cur_draw_bounds = false;
                if (m_capture)
                {
                    cur_draw_bounds = md->GetDrawBounds();
                    md->SetDrawBounds(false);
                }
                
				md->SetMatrices(m_mv_mat, m_proj_mat);
				md->SetFog(m_use_fog && m_use_fog_mesh, m_fog_intensity, m_fog_start, m_fog_end);
				md->SetDepthTex(depth_tex);
				md->SetDevice(m_vulkan->devices[0]);
				md->Draw(framebuf, clear_framebuf, peel);
				clear_framebuf = false;

				m_mesh_rendering_progress++;
                
                if (m_capture)
                    md->SetDrawBounds(cur_draw_bounds);
			}
			if (TextureRenderer::get_mem_swap())
			{
				uint32_t rn_time = GET_TICK_COUNT();
				if (rn_time - TextureRenderer::get_st_time() > TextureRenderer::get_up_time())
					return false;
			}
		}
		else if (m_layer_list[i]->IsA() == 6)
		{
			MeshGroup* group = (MeshGroup*)m_layer_list[i];
			if (group && group->GetDisp())
			{
				for (int j=0; j<(int)group->GetMeshNum(); j++)
				{
					MeshData* md = group->GetMeshData(j);
					if (md && md->GetDisp())
					{
						count++;
						if (TextureRenderer::get_mem_swap() && count <= m_mesh_rendering_progress)
							continue;

                        bool cur_draw_bounds = false;
                        if (m_capture)
                        {
                            cur_draw_bounds = md->GetDrawBounds();
                            md->SetDrawBounds(false);
                        }
                        
						md->SetMatrices(m_mv_mat, m_proj_mat);
						md->SetFog(m_use_fog && m_use_fog_mesh, m_fog_intensity, m_fog_start, m_fog_end);
						md->SetDepthTex(depth_tex);
						md->SetDevice(m_vulkan->devices[0]);
						md->Draw(framebuf, clear_framebuf, peel);
						clear_framebuf = false;

						m_mesh_rendering_progress++;
                        
                        if (m_capture)
                            md->SetDrawBounds(cur_draw_bounds);
					}
					if (TextureRenderer::get_mem_swap())
					{
						uint32_t rn_time = GET_TICK_COUNT();
						if (rn_time - TextureRenderer::get_st_time() > TextureRenderer::get_up_time())
							return false;
					}
				}
			}
		}
		
	}

	m_mesh_rendering_progress = 0;
	return true;
}

//draw volumes
//peel==true -- depth peeling
void VRenderVulkanView::DrawVolumes(int peel)
{

	int finished_bricks = 0;
	if (TextureRenderer::get_mem_swap())
	{
		finished_bricks = TextureRenderer::get_finished_bricks();
		TextureRenderer::reset_finished_bricks();
	}

	PrepFinalBuffer();

	bool dp = (m_md_pop_list.size() > 0);
	vks::VulkanDevice* prim_dev = m_vulkan->devices[0];

	//draw
	if ( (!m_drawing_coord && m_int_mode!=7 && m_updating) ||
		(!m_drawing_coord && (m_int_mode == 1 || m_int_mode == 3 || m_int_mode == 4 || m_int_mode == 5 || (m_int_mode == 6 && !m_editing_ruler_point) || m_int_mode == 8 || m_force_clear)) )
	{
		m_updating = false;
		m_force_clear = false;

		if (TextureRenderer::get_mem_swap())
		{
			//set start time for the texture renderer
			TextureRenderer::set_st_time(GET_TICK_COUNT());

			TextureRenderer::set_interactive(m_interactive);
		}

		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame &&
			vr_frame->GetSettingDlg() &&
			vr_frame->GetSettingDlg()->GetUpdateOrder() == 1)
		{
			ClearFinalBuffer();
		}
		else
			ClearFinalBuffer();

		if (TextureRenderer::get_cur_brick_num() == 0 && m_md_pop_list.size() > 0)
		{
			int nx = m_nx;
			int ny = m_ny;

			int peeling_layers;

			if (!m_mdtrans) peeling_layers = 1;
			else peeling_layers = m_peeling_layers;

			if (m_resize)
			{
				m_dp_fbo_list.clear();
				m_dp_ctex_list.clear();
				m_dp_tex_list.clear();

				m_finished_peeling_layer = 0;
				m_mesh_rendering_progress = 0;
				m_peeling_layer_progress = 0;
			}

			if (m_dp_fbo_list.size() < peeling_layers)
			{
				m_dp_fbo_list.resize(peeling_layers);
				m_dp_ctex_list.resize(peeling_layers);
				m_dp_tex_list.resize(peeling_layers);
			}

			for (int i = m_finished_peeling_layer; i < peeling_layers; i++)
			{
				if (!m_dp_fbo_list[i])
				{
					m_dp_fbo_list[i] = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
					m_dp_fbo_list[i]->w = m_nx;
					m_dp_fbo_list[i]->h = m_ny;
					m_dp_fbo_list[i]->device = prim_dev;

					m_dp_ctex_list[i] = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
					m_dp_fbo_list[i]->addAttachment(m_dp_ctex_list[i]);

					m_dp_tex_list[i] = prim_dev->GenTexture2D(
						m_vulkan->depthFormat,
						VK_FILTER_LINEAR,
						m_nx, m_ny,
						VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
						VK_IMAGE_USAGE_TRANSFER_DST_BIT |
						VK_IMAGE_USAGE_SAMPLED_BIT |
						VK_IMAGE_USAGE_TRANSFER_SRC_BIT
					);
					m_dp_fbo_list[i]->addAttachment(m_dp_tex_list[i]);
				}

				bool finished = false;
				if (i==0)
				{
					finished = DrawMeshes(m_dp_fbo_list[i], true, 0);
				}
				else
				{
					m_msh_dp_fr_tex = m_dp_tex_list[i - 1];
					finished = DrawMeshes(m_dp_fbo_list[i], true, 1, m_dp_tex_list[i-1]);
				}

				if (finished)
					m_finished_peeling_layer = i + 1;
				else
					break;
			}

			//combine result images of mesh renderer
			std::vector<Vulkan2dRender::V2DRenderParams> v2drender_params;
			Vulkan2dRender::V2dPipeline pl = 
				m_v2drender->preparePipeline(
					IMG_SHADER_TEXTURE_LOOKUP_BLEND,
					V2DRENDER_BLEND_OVER,
					VK_FORMAT_R32G32B32A32_SFLOAT,
					1,
					0,
					m_fbo_final->attachments[0]->is_swapchain_images);
			int layers = m_finished_peeling_layer - 1;
			if (layers < 0) layers = 0;
			for (int i = layers; i >= 0; i--)
			{
				Vulkan2dRender::V2DRenderParams param;
				param.pipeline = pl;
				param.clear = false;
				param.tex[0] = m_dp_ctex_list[i].get();
				v2drender_params.push_back(param);
			}
			v2drender_params[0].clear = m_clear_final_buffer;
			m_clear_final_buffer = false;
			vks::VulkanSemaphoreSettings sem = prim_dev->GetNextRenderSemaphoreSettings();
			v2drender_params[0].waitSemaphores = sem.waitSemaphores;
			v2drender_params[0].waitSemaphoreCount = sem.waitSemaphoreCount;
			v2drender_params[0].signalSemaphores = sem.signalSemaphores;
			v2drender_params[0].signalSemaphoreCount = sem.signalSemaphoreCount;
			if (!m_fbo_final->renderPass)
				m_fbo_final->replaceRenderPass(pl.pass);
			m_v2drender->seq_render(m_fbo_final, v2drender_params.data(), v2drender_params.size());

			if (m_md_pop_list[0]->GetShadow())
			{
				double darkness = 0.0;
				m_md_pop_list[0]->GetShadowParams(darkness);
				DrawOLShadowsMesh(m_dp_tex_list[0], (float)darkness);
			}

			if (TextureRenderer::get_total_brick_num() == 0 && TextureRenderer::get_mem_swap() && m_finished_peeling_layer >= peeling_layers)
			{
				TextureRenderer::set_done_update_loop(true);
			}
		}

		//setup
		PopVolumeList();

		vector<VolumeData*> quota_vd_list;

		//handle intermixing modes
		if (m_vol_method == VOL_METHOD_MULTI)
		{
			if (dp)
			{
				m_vol_dp_bk_tex = m_dp_tex_list[0];
				DrawVolumesMulti(m_vd_pop_list, 1);
			}
			else DrawVolumesMulti(m_vd_pop_list, peel);
			
			//draw masks
			if (m_draw_mask)
				DrawVolumesComp(m_vd_pop_list, true, peel);
		}
		else
		{
			int i, j;
			vector<VolumeData*> list;
			for (i=(int)m_layer_list.size()-1; i>=0; i--)
			{
				if (!m_layer_list[i])
					continue;
				switch (m_layer_list[i]->IsA())
				{
				case 2://volume data (this won't happen now)
					{
						VolumeData* vd = (VolumeData*)m_layer_list[i];
						if (vd && vd->GetDisp())
						{
							if (TextureRenderer::get_mem_swap() &&
								TextureRenderer::get_interactive() &&
								quota_vd_list.size()>0)
							{
								if (find(quota_vd_list.begin(),
									quota_vd_list.end(), vd)!=
									quota_vd_list.end())
									list.push_back(vd);
							}
							else
								list.push_back(vd);
						}
					}
					break;
				case 5://group
					{
						if (!list.empty())
						{
							DrawVolumesComp(list, false, peel);
							//draw masks
							if (m_draw_mask)
								DrawVolumesComp(list, true, peel);
							list.clear();
						}
						DataGroup* group = (DataGroup*)m_layer_list[i];
						if (!group->GetDisp())
							continue;
						for (j=group->GetVolumeNum()-1; j>=0; j--)
						{
							VolumeData* vd = group->GetVolumeData(j);
							if (vd && vd->GetDisp())
							{
								if (TextureRenderer::get_mem_swap() &&
									TextureRenderer::get_interactive() &&
									quota_vd_list.size()>0)
								{
									if (find(quota_vd_list.begin(),
										quota_vd_list.end(), vd)!=
										quota_vd_list.end())
										list.push_back(vd);
								}
								else
									list.push_back(vd);
							}
						}
						if (!list.empty())
						{
							if (group->GetBlendMode() == VOL_METHOD_MULTI)
							{
								if (dp)
								{
									m_vol_dp_bk_tex = m_dp_tex_list[0];
									DrawVolumesMulti(list, 1);
								}
								DrawVolumesMulti(list, peel);
							}
							else
								DrawVolumesComp(list, false, peel);
							//draw masks
							if (m_draw_mask)
								DrawVolumesComp(list, true, peel);
							list.clear();
						}
					}
					break;
				}
			}
		}
	}

	if (m_clear_final_buffer && m_fbo_final) {
		Vulkan2dRender::V2DRenderParams params_final = m_v2drender->GetNextV2dRenderSemaphoreSettings();

		if (!m_fbo_final->renderPass)
		{
			params_final.pipeline =
				m_v2drender->preparePipeline(
					IMG_SHDR_BRIGHTNESS_CONTRAST_HDR,
					V2DRENDER_BLEND_ADD,
					m_fbo_final->attachments[0]->format,
					m_fbo_final->attachments.size(),
					0,
					m_fbo_final->attachments[0]->is_swapchain_images);
			m_fbo_final->replaceRenderPass(params_final.pipeline.pass);
		}
		params_final.clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
		m_clear_final_buffer = false;
		m_v2drender->clear(m_fbo_final, params_final);
	}

	//final composition
	if (m_tile_rendering) {
		//generate textures & buffer objects
		//frame buffer for final
		if (!m_fbo_tiled_tmp)
		{
			m_fbo_tiled_tmp = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
			m_fbo_tiled_tmp->w = m_nx;
			m_fbo_tiled_tmp->h = m_ny;
			m_fbo_tiled_tmp->device = prim_dev;

			m_tex_tiled_tmp = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
			m_fbo_tiled_tmp->addAttachment(m_tex_tiled_tmp);
		}

		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHADER_TEXTURE_LOOKUP,
				V2DRENDER_BLEND_OVER,
				m_fbo_tiled_tmp->attachments[0]->format,
				m_fbo_tiled_tmp->attachments.size(),
				0,
				m_fbo_tiled_tmp->attachments[0]->is_swapchain_images);
		params.tex[0] = m_tex_final.get();
		if (m_current_tileid == 0)
			params.clear = true;

		std::vector<Vulkan2dRender::Vertex> tile_verts;
		m_tile_vobj.vertOffset = sizeof(Vulkan2dRender::Vertex) * 4 * m_current_tileid;
		params.obj = &m_tile_vobj;

		if (!m_fbo_tiled_tmp->renderPass)
			m_fbo_tiled_tmp->replaceRenderPass(params.pipeline.pass);

		m_v2drender->render(m_fbo_tiled_tmp, params);


		Vulkan2dRender::V2DRenderParams params2 = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
		params2.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHDR_BLEND_BRIGHT_BACKGROUND_HDR,
				V2DRENDER_BLEND_OVER,
				current_fbo->attachments[0]->format,
				current_fbo->attachments.size(),
				0,
				current_fbo->attachments[0]->is_swapchain_images);
		params2.tex[0] = m_tex_tiled_tmp.get();
		params2.loc[0] = glm::vec4( 1.0f, 1.0f, 1.0f, 1.0f );
		params2.loc[1] = glm::vec4( 1.0f, 1.0f, 1.0f, 1.0f );
		params2.loc[2] = glm::vec4( 0.0f, 0.0f, 0.0f, 0.0f );
		params2.clear = m_frame_clear;
		params2.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 1.0f };
		m_frame_clear = false;
		
		if (!current_fbo->renderPass)
			current_fbo->replaceRenderPass(params2.pipeline.pass);

		m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params2);
	}
	else {
		//draw the final buffer to the windows buffer
		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHDR_BLEND_BRIGHT_BACKGROUND_HDR,
				V2DRENDER_BLEND_OVER,
				current_fbo->attachments[0]->format,
				current_fbo->attachments.size(),
				0,
				current_fbo->attachments[0]->is_swapchain_images);
		params.tex[0] = m_tex_final.get();
		params.loc[0] = glm::vec4( 1.0f, 1.0f, 1.0f, 1.0f );
		params.loc[1] = glm::vec4( 1.0f, 1.0f, 1.0f, 1.0f );
		params.loc[2] = glm::vec4( 0.0f, 0.0f, 0.0f, 0.0f );
		params.clear = m_frame_clear;
		params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 1.0f };
		m_frame_clear = false;
		
		if (!current_fbo->renderPass)
			current_fbo->replaceRenderPass(params.pipeline.pass);

		m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
	}

	if (m_interactive)
	{
		m_interactive = false;
		m_clear_buffer = true;
		m_refresh_start_loop = true;
	}

	if (m_int_res)
	{
		m_int_res = false;
		m_refresh_start_loop = true;
	}

	if (m_manip)
	{
		m_pre_draw = true;
		m_refresh_start_loop = true;
	}

	if (m_tile_rendering && m_current_tileid < m_tile_xnum*m_tile_ynum) {
		if ((TextureRenderer::get_start_update_loop() && TextureRenderer::get_done_update_loop()) ||
				!TextureRenderer::get_mem_swap() ||
				TextureRenderer::get_total_brick_num() == 0 ) {
			DrawTile();
			m_current_tileid++;
			if (m_capture) m_postdraw = true;
			if (m_current_tileid < m_tile_xnum*m_tile_ynum) m_refresh = true;
		}
	}

	if (TextureRenderer::get_mem_swap())
	{
		TextureRenderer::set_consumed_time(GET_TICK_COUNT() - TextureRenderer::get_st_time());
		if (TextureRenderer::get_start_update_loop() &&
			TextureRenderer::get_done_update_loop())
		{
			TextureRenderer::reset_update_loop();
			vkQueueWaitIdle(m_vulkan->vulkanDevice->queue);
            if (m_capture) m_postdraw = true;
            //ed_time = milliseconds_now();
            //sprintf(dbgstr, "Frame Draw: %lld \n", ed_time - st_time);
            //OutputDebugStringA(dbgstr);
		}
	}

	//if (refresh)
	//	RefreshGL();
}

void VRenderVulkanView::DrawVolumesDP()
{
	int finished_bricks = 0;
	if (TextureRenderer::get_mem_swap())
	{
		finished_bricks = TextureRenderer::get_finished_bricks();
		TextureRenderer::reset_finished_bricks();
	}

	bool dp = (m_md_pop_list.size() > 0);

	int peeling_layers;
	if (!m_mdtrans || m_peeling_layers <= 0) peeling_layers = 0;
	else peeling_layers = m_peeling_layers;

	int nx = m_nx;
	int ny = m_ny;

	PrepFinalBuffer();

	vks::VulkanDevice* prim_dev = m_vulkan->devices[0];

	if (TextureRenderer::get_mem_swap())
	{
		//set start time for the texture renderer
		TextureRenderer::set_st_time(GET_TICK_COUNT());

		TextureRenderer::set_interactive(m_interactive);
	}

	bool first = (m_finished_peeling_layer == 0 && (!TextureRenderer::get_mem_swap() || TextureRenderer::get_cur_brick_num() == 0)) ? true : false;

	//draw
	if ((!m_drawing_coord &&
		m_int_mode != 7 &&
		m_updating) ||
		(!m_drawing_coord &&
		(m_int_mode == 1 ||
			m_int_mode == 3 ||
			m_int_mode == 4 ||
			m_int_mode == 5 ||
			(m_int_mode == 6 &&
				!m_editing_ruler_point) ||
			m_int_mode == 8 ||
			m_force_clear)))
	{

		m_updating = false;
		m_force_clear = false;

		for (int s = m_finished_peeling_layer; s <= peeling_layers; s++)
		{
			if (s == 0 || (TextureRenderer::get_mem_swap() && TextureRenderer::get_total_brick_num() > 0)) ClearFinalBuffer();

			int dptexnum = peeling_layers > 0 ? peeling_layers : 1;
			if (m_finished_peeling_layer == 0 && TextureRenderer::get_cur_brick_num() == 0 && m_md_pop_list.size() > 0)
			{
				if (m_resize)
				{
					m_dp_fbo_list.clear();
					m_dp_ctex_list.clear();
					m_dp_tex_list.clear();
				}

				if (m_dp_fbo_list.size() < dptexnum)
				{
					m_dp_fbo_list.resize(dptexnum);
					m_dp_ctex_list.resize(dptexnum);
					m_dp_tex_list.resize(dptexnum);
				}

				for (int i = m_peeling_layer_progress; i < dptexnum; i++)
				{
					if (!m_dp_fbo_list[i])
					{
						m_dp_fbo_list[i] = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
						m_dp_fbo_list[i]->w = m_nx;
						m_dp_fbo_list[i]->h = m_ny;
						m_dp_fbo_list[i]->device = prim_dev;

						m_dp_ctex_list[i] = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
						m_dp_fbo_list[i]->addAttachment(m_dp_ctex_list[i]);

						m_dp_tex_list[i] = prim_dev->GenTexture2D(
							m_vulkan->depthFormat,
							VK_FILTER_LINEAR,
							m_nx, m_ny,
							VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
							VK_IMAGE_USAGE_TRANSFER_DST_BIT |
							VK_IMAGE_USAGE_SAMPLED_BIT |
							VK_IMAGE_USAGE_TRANSFER_SRC_BIT
						);
						m_dp_fbo_list[i]->addAttachment(m_dp_tex_list[i]);
					}

					bool finished = false;
					if (i == 0)
					{
						finished = DrawMeshes(m_dp_fbo_list[i], true, 0);
					}
					else
					{
						m_msh_dp_fr_tex = m_dp_tex_list[i - 1];
						finished = DrawMeshes(m_dp_fbo_list[i], true, 1, m_dp_tex_list[i - 1]);
					}

					if (finished)
						m_peeling_layer_progress = i + 1;
					else
						break;
				}
			}

			if (m_peeling_layer_progress < dptexnum)
				break;

			int peel = 0;
			if (m_dp_tex_list.size() >= peeling_layers && !m_dp_tex_list.empty() && m_md_pop_list.size() > 0)
			{
				if (TextureRenderer::get_update_order() == 0)
				{
					if (s == 0)
					{
						peel = 2;
						m_vol_dp_fr_tex = m_dp_tex_list[(peeling_layers-1)-s];
					}
					else if (s == peeling_layers)
					{
						peel = 1;
						m_vol_dp_bk_tex = m_dp_tex_list[(peeling_layers-1)-(s-1)];
					}
					else
					{
						peel = 3;
						m_vol_dp_fr_tex = m_dp_tex_list[(peeling_layers-1)-s];
						m_vol_dp_bk_tex = m_dp_tex_list[(peeling_layers-1)-(s-1)];
					}
				}
				else
				{
					if (s == 0)
					{
						peel = 1;
						m_vol_dp_bk_tex = m_dp_tex_list[s];
					}
					else if (s == peeling_layers)
					{
						peel = 2;
						m_vol_dp_fr_tex = m_dp_tex_list[s-1];
					}
					else
					{
						peel = 3;
						m_vol_dp_fr_tex = m_dp_tex_list[s-1];
						m_vol_dp_bk_tex = m_dp_tex_list[s];
					}
				}
			}

			//setup
			PopVolumeList();

			vector<VolumeData*> quota_vd_list;

			//handle intermixing modes
			if (m_vol_method == VOL_METHOD_MULTI)
			{
				DrawVolumesMulti(m_vd_pop_list, peel);
				//draw masks
				if (m_draw_mask)
					DrawVolumesComp(m_vd_pop_list, true, peel);
			}
			else
			{
				int i, j;
				vector<VolumeData*> list;
				for (i=(int)m_layer_list.size()-1; i>=0; i--)
				{
					if (!m_layer_list[i])
						continue;
					switch (m_layer_list[i]->IsA())
					{
					case 2://volume data (this won't happen now)
						{
							if (m_finished_peeling_layer != 0)
								continue;
							VolumeData* vd = (VolumeData*)m_layer_list[i];
							if (vd && vd->GetDisp())
							{
								if (TextureRenderer::get_mem_swap() &&
									TextureRenderer::get_interactive() &&
									quota_vd_list.size()>0)
								{
									if (find(quota_vd_list.begin(),
										quota_vd_list.end(), vd)!=
										quota_vd_list.end())
										list.push_back(vd);
								}
								else
									list.push_back(vd);
							}
						}
						break;
					case 5://group
						{
							if (!list.empty())
							{
								DrawVolumesComp(list, false, peel);
								//draw masks
								if (m_draw_mask)
									DrawVolumesComp(list, true, peel);
								list.clear();
							}
							DataGroup* group = (DataGroup*)m_layer_list[i];
							if (!group->GetDisp())
								continue;
//							if (m_finished_peeling_layer != 0 && group->GetBlendMode() != VOL_METHOD_MULTI)
//								continue;
							for (j=group->GetVolumeNum()-1; j>=0; j--)
							{
								VolumeData* vd = group->GetVolumeData(j);
								if (vd && vd->GetDisp())
								{
									if (TextureRenderer::get_mem_swap() &&
										TextureRenderer::get_interactive() &&
										quota_vd_list.size()>0)
									{
										if (find(quota_vd_list.begin(),
											quota_vd_list.end(), vd)!=
											quota_vd_list.end())
											list.push_back(vd);
									}
									else
										list.push_back(vd);
								}
							}
							if (!list.empty())
							{
								if (group->GetBlendMode() == VOL_METHOD_MULTI)
									DrawVolumesMulti(list, peel);
								else
									DrawVolumesComp(list, false, peel);
								//draw masks
								if (m_draw_mask)
									DrawVolumesComp(list, true, peel);
								list.clear();
							}
						}
						break;
					}
				}
			}

			if (m_fbo_temp && (m_fbo_temp->w != m_nx || m_fbo_temp->h != m_ny))
			{
				m_fbo_temp.reset();
				m_tex_temp.reset();
				m_fbo_temp_restore.reset();
			}
			if (!m_fbo_temp)
			{
				m_fbo_temp = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
				m_fbo_temp->w = m_nx;
				m_fbo_temp->h = m_ny;
				m_fbo_temp->device = prim_dev;

				m_tex_temp = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
				m_fbo_temp->addAttachment(m_tex_temp);

				m_fbo_temp_restore = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
				m_fbo_temp_restore->w = m_nx;
				m_fbo_temp_restore->h = m_ny;
				m_fbo_temp_restore->device = prim_dev;
				m_fbo_temp_restore->addAttachment(m_fbo_final->attachments[0]);
			}
			
			int texid = TextureRenderer::get_update_order() == 0 ? (peeling_layers-1)-s : s;
			if (m_dp_ctex_list.size() > texid && m_md_pop_list.size() > 0 &&
				((TextureRenderer::get_start_update_loop() && TextureRenderer::get_done_update_loop()) || !TextureRenderer::get_mem_swap() || TextureRenderer::get_total_brick_num() == 0) )
			{
				Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

				params.clear = m_clear_final_buffer;
				m_clear_final_buffer = false;
				params.pipeline =
					m_v2drender->preparePipeline(
						IMG_SHADER_TEXTURE_LOOKUP_BLEND,
						V2DRENDER_BLEND_OVER_INV,
						m_fbo_temp_restore->attachments[0]->format,
						m_fbo_temp_restore->attachments.size(),
						0,
						m_fbo_temp_restore->attachments[0]->is_swapchain_images);
				params.tex[0] = m_dp_ctex_list[texid].get();

				if (!m_fbo_temp_restore->renderPass)
					m_fbo_temp_restore->replaceRenderPass(params.pipeline.pass);
				m_v2drender->render(m_fbo_temp_restore, params);
			}

			if (TextureRenderer::get_mem_swap()/* && TextureRenderer::get_total_brick_num() > 0*/)
			{
				unsigned int rn_time = GET_TICK_COUNT();
				TextureRenderer::set_consumed_time(rn_time - TextureRenderer::get_st_time());

				if (TextureRenderer::get_total_brick_num() == 0 && m_peeling_layer_progress >= dptexnum)
					TextureRenderer::set_done_update_loop(true);

				if (TextureRenderer::get_start_update_loop() && !TextureRenderer::get_done_update_loop())
					break;

				if (TextureRenderer::get_start_update_loop() && TextureRenderer::get_done_update_loop())
				{
					m_finished_peeling_layer++;
					if (m_finished_peeling_layer <= peeling_layers)
					{
						StartLoopUpdate(false);
						finished_bricks = TextureRenderer::get_finished_bricks();
						TextureRenderer::reset_finished_bricks();
						
						if (TextureRenderer::get_mem_swap() &&
							TextureRenderer::get_start_update_loop())
						{
							TextureRenderer::validate_temp_final_buffer();

							Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

							params.clear = true;
							params.pipeline =
								m_v2drender->preparePipeline(
									IMG_SHADER_TEXTURE_LOOKUP,
									V2DRENDER_BLEND_DISABLE,
									m_fbo_temp->attachments[0]->format,
									m_fbo_temp->attachments.size(),
									0,
									m_fbo_temp->attachments[0]->is_swapchain_images);
							params.tex[0] = m_tex_final.get();

							if (!m_fbo_temp->renderPass)
								m_fbo_temp->replaceRenderPass(params.pipeline.pass);
							m_v2drender->render(m_fbo_temp, params);
						}
					}
					else
						TextureRenderer::reset_update_loop();
				}

				if (rn_time - TextureRenderer::get_st_time() > TextureRenderer::get_up_time())
					break;
			}
			else
				m_finished_peeling_layer++;
		}
	}

	//final composition
	if (m_tile_rendering) {
		//generate textures & buffer objects
		//frame buffer for final
		if (!m_fbo_tiled_tmp)
		{
			m_fbo_tiled_tmp = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
			m_fbo_tiled_tmp->w = m_nx;
			m_fbo_tiled_tmp->h = m_ny;
			m_fbo_tiled_tmp->device = prim_dev;

			m_tex_tiled_tmp = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
			m_fbo_tiled_tmp->addAttachment(m_tex_tiled_tmp);
		}

		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHADER_TEXTURE_LOOKUP,
				V2DRENDER_BLEND_OVER,
				m_fbo_tiled_tmp->attachments[0]->format,
				m_fbo_tiled_tmp->attachments.size(),
				0,
				m_fbo_tiled_tmp->attachments[0]->is_swapchain_images);
		params.tex[0] = m_tex_final.get();
		if (m_current_tileid == 0)
			params.clear = true;

		std::vector<Vulkan2dRender::Vertex> tile_verts;
		m_tile_vobj.vertOffset = sizeof(Vulkan2dRender::Vertex) * 4 * m_current_tileid;
		params.obj = &m_tile_vobj;

		if (!m_fbo_tiled_tmp->renderPass)
			m_fbo_tiled_tmp->replaceRenderPass(params.pipeline.pass);

		m_v2drender->render(m_fbo_tiled_tmp, params);


		Vulkan2dRender::V2DRenderParams params2 = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
		params2.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHDR_BLEND_BRIGHT_BACKGROUND_HDR,
				V2DRENDER_BLEND_OVER,
				current_fbo->attachments[0]->format,
				current_fbo->attachments.size(),
				0,
				current_fbo->attachments[0]->is_swapchain_images);
		params2.tex[0] = m_tex_tiled_tmp.get();
		params2.loc[0] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		params2.loc[1] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		params2.loc[2] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
		params2.clear = m_frame_clear;
		params2.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b() };
		m_frame_clear = false;

		if (!current_fbo->renderPass)
			current_fbo->replaceRenderPass(params2.pipeline.pass);

		m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params2);
	}
	else {
		//draw the final buffer to the windows buffer
		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHDR_BLEND_BRIGHT_BACKGROUND_HDR,
				V2DRENDER_BLEND_OVER,
				current_fbo->attachments[0]->format,
				current_fbo->attachments.size(),
				0,
				current_fbo->attachments[0]->is_swapchain_images);
		params.tex[0] = m_tex_final.get();
		params.loc[0] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		params.loc[1] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		params.loc[2] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
		params.clear = m_frame_clear;
		params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b() };
		m_frame_clear = false;

		if (!current_fbo->renderPass)
			current_fbo->replaceRenderPass(params.pipeline.pass);

		m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
	}

	if (m_interactive)
	{
		m_interactive = false;
		m_clear_buffer = true;
		m_refresh_start_loop = true;
	}

	if (m_int_res)
	{
		m_int_res = false;
		m_refresh_start_loop = true;
	}

	if (m_manip)
	{
		m_pre_draw = true;
		m_refresh_start_loop = true;
	}

	if (m_tile_rendering && m_current_tileid < m_tile_xnum * m_tile_ynum) {
		if ((TextureRenderer::get_start_update_loop() && TextureRenderer::get_done_update_loop()) ||
			!TextureRenderer::get_mem_swap() ||
			TextureRenderer::get_total_brick_num() == 0) {
			DrawTile();
			m_current_tileid++;
			if (m_capture) m_postdraw = true;
			if (m_current_tileid < m_tile_xnum * m_tile_ynum) m_refresh = true;
		}
	}

/*	if (TextureRenderer::get_mem_swap())
	{
		TextureRenderer::set_consumed_time(GET_TICK_COUNT() - TextureRenderer::get_st_time());
		if (TextureRenderer::get_start_update_loop() &&
			TextureRenderer::get_done_update_loop())
		{
			TextureRenderer::reset_update_loop();
			vkQueueWaitIdle(m_vulkan->vulkanDevice->queue);
			ed_time = milliseconds_now();
			sprintf(dbgstr, "Frame Draw: %lld \n", ed_time - st_time);
			OutputDebugStringA(dbgstr);
		}
	}*/

	//if (refresh)
	//	RefreshGL();
}

void VRenderVulkanView::DrawAnnotations()
{
	if (!m_text_renderer)
		return;

	int nx, ny;
	nx = m_nx;
	ny = m_ny;
	float sx, sy;
	sx = 2.0/nx;
	sy = 2.0/ny;
	float px, py;

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(m_mv_mat));
	p.set(glm::value_ptr(m_proj_mat));

	Color text_color = GetTextColor();

	for (size_t i=0; i<m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
        switch (m_layer_list[i]->IsA())
        {
        case 3://mesh data
            {
                MeshData* md = (MeshData*)m_layer_list[i];
                if (md && md->GetDisp() && md->isTree())
                {
                    auto labels = md->GetSubmeshLabels();
                    auto active_ids = md->GetActiveLabelSet();

                    for(auto ite = active_ids->begin(); ite != active_ids->end(); ++ite)
                    {
                        if(labels->count(*ite) > 0 && md->GetROIVisibility(*ite))
                        {
                            MeshRenderer::SubMeshLabel &lbl = (*labels)[*ite];
                            for (auto pos : lbl.points)
                            {
                                if (!md->InsideClippingPlanes(pos))
                                    continue;
                                pos.z(-pos.z());
                                pos = mv.transform(pos);
                                pos = p.transform(pos);
                                if (pos.x() >= -1.0 && pos.x() <= 1.0 &&
                                    pos.y() >= -1.0 && pos.y() <= 1.0)
                                {
                                    if (pos.z()<=0.0 || pos.z()>=1.0)
                                        continue;
                                    px = pos.x()*nx/2.0;
                                    py = pos.y()*ny/2.0;
                                    m_text_renderer->RenderText(
                                        m_vulkan->frameBuffers[m_vulkan->currentBuffer],
                                        lbl.name, text_color,
                                        px*sx, py*sy);
                                }
                            }
                        }
                    }
                }
                else if (md && md->GetDisp() && md->GetLabelVisibility() && md->GetAnnotations())
                {
                    Annotations* ann = md->GetAnnotations();
                    string str;
                    Point pos;
                    wstring wstr;
                    for (int j=0; j<ann->GetTextNum(); ++j)
                    {
                        str = ann->GetTextText(j);
                        wstr = s2ws(str);
                        pos = ann->GetTextPos(j);
                        if (!md->InsideClippingPlanes(pos))
                            continue;
                        //pos = ann->GetTextTransformedPos(j);
                        pos.z(-pos.z());
                        pos = mv.transform(pos);
                        pos = p.transform(pos);
                        if (pos.x() >= -1.0 && pos.x() <= 1.0 &&
                            pos.y() >= -1.0 && pos.y() <= 1.0)
                        {
                            if (pos.z()<=0.0 || pos.z()>=1.0)
                                continue;
                            px = pos.x()*nx/2.0;
                            py = pos.y()*ny/2.0;
                            m_text_renderer->RenderText(
                                m_vulkan->frameBuffers[m_vulkan->currentBuffer],
                                wstr, text_color,
                                px*sx, py*sy);
                        }
                    }
                }
            }
            break;
        case 6://mesh group
            {
                MeshGroup* group = (MeshGroup*)m_layer_list[i];
                if (!group->GetDisp())
                    continue;
                for (int j=0; j<group->GetMeshNum(); j++)
                {
                    MeshData* md = group->GetMeshData(j);
                    if (md && md->GetDisp() && md->GetLabelVisibility() && md->GetAnnotations())
                    {
                        Annotations* ann = md->GetAnnotations();
                        string str;
                        Point pos;
                        wstring wstr;
                        for (int j=0; j<ann->GetTextNum(); ++j)
                        {
                            str = ann->GetTextText(j);
                            wstr = s2ws(str);
                            pos = ann->GetTextPos(j);
                            if (!md->InsideClippingPlanes(pos))
                                continue;
                            //pos = ann->GetTextTransformedPos(j);
                            pos.z(-pos.z());
                            pos = mv.transform(pos);
                            pos = p.transform(pos);
                            if (pos.x() >= -1.0 && pos.x() <= 1.0 &&
                                pos.y() >= -1.0 && pos.y() <= 1.0)
                            {
                                if (pos.z()<=0.0 || pos.z()>=1.0)
                                    continue;
                                px = pos.x()*nx/2.0;
                                py = pos.y()*ny/2.0;
                                m_text_renderer->RenderText(
                                    m_vulkan->frameBuffers[m_vulkan->currentBuffer],
                                    wstr, text_color,
                                    px*sx, py*sy);
                            }
                        }
                    }
                }
            }
            break;
        case 4:
            {
                Annotations* ann = (Annotations*)m_layer_list[i];
                if (!ann) continue;
                if (ann->GetDisp())
                {
                    MeshData* md = ann->GetMesh();
                    if (md)
                    {
                        vks::VulkanDevice* prim_dev = m_vulkan->devices[0];
                        if (m_anno_fbo && (m_anno_fbo->w != m_nx || m_anno_fbo->h != m_ny))
                        {
                            m_anno_fbo.reset();
                            m_anno_tex.reset();
                            m_anno_dp_tex.reset();
                        }
                        if (!m_anno_fbo)
                        {
                            m_anno_fbo = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
                            m_anno_fbo->w = m_nx;
                            m_anno_fbo->h = m_ny;
                            m_anno_fbo->device = prim_dev;

                            m_anno_tex = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
                            m_anno_fbo->addAttachment(m_anno_tex);
                            
                            m_anno_dp_tex = prim_dev->GenTexture2D(
                                m_vulkan->depthFormat,
                                VK_FILTER_LINEAR,
                                m_nx, m_ny,
                                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                            );
                            m_anno_fbo->addAttachment(m_anno_dp_tex);
                        }
                        
                        md->SetDepthTex(nullptr);
                        md->SetMatrices(m_mv_mat, m_proj_mat);
                        md->SetFog(m_use_fog && m_use_fog_mesh, m_fog_intensity, m_fog_start, m_fog_end);
                        md->SetDevice(m_vulkan->devices[0]);
                        md->SetDrawBounds(false);
                        md->Draw(m_anno_fbo, true, 0);
                        
                        Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
                        vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
                        params.pipeline =
                            m_v2drender->preparePipeline(
                                IMG_SHDR_BRIGHTNESS_CONTRAST,
                                V2DRENDER_BLEND_ADD,
                                current_fbo->attachments[0]->format,
                                current_fbo->attachments.size(),
                                0,
                                current_fbo->attachments[0]->is_swapchain_images);
                        params.loc[0] = glm::vec4(1.0f);
                        params.loc[1] = glm::vec4((float)ann->GetAlpha(), (float)ann->GetAlpha(), (float)ann->GetAlpha(), 1.0f);
                        params.tex[0] = m_anno_tex.get();
                        params.clear = m_frame_clear;
                        params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b() };
                        m_frame_clear = false;
                        if (!current_fbo->renderPass)
                            current_fbo->replaceRenderPass(params.pipeline.pass);
                        m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
                    }
                    else
                    {
                        string str;
                        Point pos;
                        wstring wstr;
                        for (int j=0; j<ann->GetTextNum(); ++j)
                        {
                            str = ann->GetTextText(j);
                            wstr = s2ws(str);
                            pos = ann->GetTextPos(j);
                            if (!ann->InsideClippingPlanes(pos))
                                continue;
                            pos = ann->GetTextTransformedPos(j);
                            pos = mv.transform(pos);
                            pos = p.transform(pos);
                            if (pos.x() >= -1.0 && pos.x() <= 1.0 &&
                                pos.y() >= -1.0 && pos.y() <= 1.0)
                            {
                                if (pos.z()<=0.0 || pos.z()>=1.0)
                                    continue;
                                px = pos.x()*nx/2.0;
                                py = pos.y()*ny/2.0;
                                m_text_renderer->RenderText(
                                    m_vulkan->frameBuffers[m_vulkan->currentBuffer],
                                    wstr, text_color,
                                    px*sx, py*sy);
                            }
                        }
                    }
                }
            }
            break;
        }
	}
}

//get populated mesh list
//stored in m_md_pop_list
void VRenderVulkanView::PopMeshList()
{
	if (!m_md_pop_dirty)
		return;

	int i, j;
	m_md_pop_list.clear();

	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			return;
		switch (m_layer_list[i]->IsA())
		{
		case 3://mesh data
			{
				MeshData* md = (MeshData*)m_layer_list[i];
				if (md->GetDisp())
					m_md_pop_list.push_back(md);
			}
			break;
		case 6://mesh group
			{
				MeshGroup* group = (MeshGroup*)m_layer_list[i];
				if (!group->GetDisp())
					continue;
				for (j=0; j<group->GetMeshNum(); j++)
				{
					if (group->GetMeshData(j) &&
						group->GetMeshData(j)->GetDisp())
						m_md_pop_list.push_back(group->GetMeshData(j));
				}
			}
			break;
		}
	}
	m_md_pop_dirty = false;
}

//get populated volume list
//stored in m_vd_pop_list
void VRenderVulkanView::PopVolumeList()
{
	if (!m_vd_pop_dirty)
		return;

	int i, j;
	m_vd_pop_list.clear();
    
    bool isbrxml = false;
	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 2://volume data
			{
				VolumeData* vd = (VolumeData*)m_layer_list[i];
				if (vd->GetDisp())
                {
					m_vd_pop_list.push_back(vd);
                    if (vd->isBrxml())
                        isbrxml = true;
                }
			}
			break;
		case 5://group
			{
				DataGroup* group = (DataGroup*)m_layer_list[i];
				if (!group->GetDisp())
					continue;
				for (j=0; j<group->GetVolumeNum(); j++)
				{
                    VolumeData* vd = group->GetVolumeData(j);
					if (vd && vd->GetDisp())
                    {
						m_vd_pop_list.push_back(vd);
                        if (vd->isBrxml())
                            isbrxml = true;
                    }
				}
			}
			break;
		}
	}
    
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    if (vr_frame)
    {
        for (int i=0; i<vr_frame->GetViewNum(); i++)
        {
            VRenderView* view = vr_frame->GetView(i);
            view->ToggleQualityDropDownBox(isbrxml);
        }
    }
    
	m_vd_pop_dirty = false;
}

//organize layers in view
//put all volume data under view into last group of the view
//if no group in view
void VRenderVulkanView::OrganizeLayers()
{
	DataGroup* le_group = 0;
	int i, j, k;

	//find last empty group
	for (i=GetLayerNum()-1; i>=0; i--)
	{
		TreeLayer* layer = GetLayer(i);
		if (layer && layer->IsA()==5)
		{
			//layer is group
			DataGroup* group = (DataGroup*)layer;
			if (group->GetVolumeNum() == 0)
			{
				le_group = group;
				break;
			}
		}
	}

	for (i=0; i<GetLayerNum(); i++)
	{
		TreeLayer* layer = GetLayer(i);
		if (layer && layer->IsA() == 2)
		{
			//layer is volume
			VolumeData* vd = (VolumeData*)layer;
			wxString name = vd->GetName();
			if (le_group)
			{
				for (j=0; j<(int)m_layer_list.size(); j++)
				{
					if (!m_layer_list[j])
						continue;
					if (m_layer_list[j]->IsA() == 2)
					{

						VolumeData* vd = (VolumeData*)m_layer_list[j];
						if (vd && vd->GetName() == name)
						{
							m_layer_list.erase(m_layer_list.begin()+j);
							m_vd_pop_dirty = true;
							m_cur_vol = NULL;
							break;
						}
					}
					else if(m_layer_list[j]->IsA() == 5)
					{

						DataGroup* group = (DataGroup*)m_layer_list[j];
						bool found = false;
						for (k=0; k<group->GetVolumeNum(); k++)
						{
							VolumeData* vd = group->GetVolumeData(k);
							if (vd && vd->GetName() == name)
							{
								group->RemoveVolumeData(k);
								m_vd_pop_dirty = true;
								m_cur_vol = NULL;
								found = true;
								break;
							}
						}
						if (found) break;
					}
				}
				le_group->InsertVolumeData(le_group->GetVolumeNum(), vd);
			}
			else
			{
				wxString group_name = AddGroup("");
				le_group = GetGroup(group_name);
				if (le_group)
				{
					for (j=0; j<(int)m_layer_list.size(); j++)
					{
						if (!m_layer_list[j])
							continue;
						if (m_layer_list[j]->IsA() == 2)
						{

							VolumeData* vd = (VolumeData*)m_layer_list[j];
							if (vd && vd->GetName() == name)
							{
								m_layer_list.erase(m_layer_list.begin()+j);
								m_vd_pop_dirty = true;
								m_cur_vol = NULL;
								break;
							}
						}
						else if(m_layer_list[j]->IsA() == 5)
						{

							DataGroup* group = (DataGroup*)m_layer_list[j];
							bool found = false;
							for (k=0; k<group->GetVolumeNum(); k++)
							{
								VolumeData* vd = group->GetVolumeData(k);
								if (vd && vd->GetName() == name)
								{
									group->RemoveVolumeData(k);
									m_vd_pop_dirty = true;
									m_cur_vol = NULL;
									found = true;
									break;
								}
							}
							if (found) break;
						}
					}
					le_group->InsertVolumeData(le_group->GetVolumeNum(), vd);
				}
			}

			VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
			if (vr_frame)
			{
				AdjustView* adjust_view = vr_frame->GetAdjustView();
				if (adjust_view)
				{
					adjust_view->SetGroupLink(le_group);
					adjust_view->UpdateSync();
				}
			}
		}
	}
}

void VRenderVulkanView::RandomizeColor()
{
	for (int i=0; i<(int)m_layer_list.size(); i++)
	{
		if (m_layer_list[i])
			m_layer_list[i]->RandomizeColor();
	}
}

void VRenderVulkanView::ClearVolList()
{
	m_loader.RemoveAllLoadedData();
	if (m_vulkan)
		m_vulkan->clearTexPools();
	m_vd_pop_list.clear();
}

void VRenderVulkanView::ClearMeshList()
{
	m_md_pop_list.clear();
}

//interactive modes
int VRenderVulkanView::GetIntMode()
{
	return m_int_mode;
}

void VRenderVulkanView::SetIntMode(int mode)
{
	m_int_mode = mode;
	if (m_int_mode == 1)
	{
		m_brush_state = 0;
		m_draw_brush = false;
	}
}

//set use 2d rendering results
void VRenderVulkanView::SetPaintUse2d(bool use2d)
{
	m_selector.SetPaintUse2d(use2d);
}

bool VRenderVulkanView::GetPaintUse2d()
{
	return m_selector.GetPaintUse2d();
}

//segmentation mdoe selection
void VRenderVulkanView::SetPaintMode(int mode)
{
	m_selector.SetMode(mode);
	m_brush_state = mode;
}

int VRenderVulkanView::GetPaintMode()
{
	return m_selector.GetMode();
}

void VRenderVulkanView::DrawCircle(double cx, double cy,
							   double radius, Color &color, glm::mat4 &matrix)
{

	int secs = 60;
	double deg = 0.0;

	vector<Vulkan2dRender::Vertex> vertex;
	vector<uint32_t> index;
	vertex.reserve(secs * 6);
	for (size_t i = 0; i < secs; ++i)
	{
		deg = i * 2 * PI / secs;
		vertex.push_back(
			Vulkan2dRender::Vertex{
				{(float)(cx + radius * sin(deg)), (float)(cy + radius * cos(deg)), 0.0f},
				{0.0f, 0.0f, 0.0f}
			}
		);
		index.push_back(i);
	}
	index.push_back(0);

	if (m_brush_vobj.vertBuf.buffer == VK_NULL_HANDLE)
	{
		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_brush_vobj.vertBuf,
			vertex.size() * sizeof(Vulkan2dRender::Vertex),
			vertex.data()));

		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_brush_vobj.idxBuf,
			index.size() * sizeof(uint32_t),
			index.data()));

		m_brush_vobj.idxCount = index.size();
		m_brush_vobj.idxOffset = 0;
		m_brush_vobj.vertCount = vertex.size();
		m_brush_vobj.vertOffset = 0;
	}
	m_brush_vobj.vertBuf.map();
	m_brush_vobj.idxBuf.map();
	m_brush_vobj.vertBuf.copyTo(vertex.data(), vertex.size()*sizeof(Vulkan2dRender::Vertex));
	m_brush_vobj.idxBuf.copyTo(index.data(), index.size() * sizeof(uint32_t));
	m_brush_vobj.vertBuf.unmap();
	m_brush_vobj.idxBuf.unmap();


	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
	params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), 1.0f);
	params.matrix[0] = matrix;
	params.clear = m_frame_clear;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b() };
	m_frame_clear = false;

	params.obj = &m_brush_vobj;

	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);

	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
}

//draw the brush shape
void VRenderVulkanView::DrawBrush()
{
	double pressure = m_use_pres&&m_on_press?m_pressure:1.0;

	wxPoint pos1(old_mouse_X, old_mouse_Y);
	wxRect reg = GetClientRect();
	if (reg.Contains(pos1))
	{
		int i;
		int nx, ny;
		nx = m_nx;
		ny = m_ny;
		double cx = pos1.x;
		double cy = pos1.y;
		float sx, sy;
		sx = 2.0/nx;
		sy = 2.0/ny;

		//draw the circles
		//set up the matrices
		glm::mat4 proj_mat = glm::ortho(float(0), float(nx), float(0), float(ny));

		//attributes
		int mode = m_selector.GetMode();
		Color text_color = GetTextColor();

		if (mode == 1 ||
			mode == 2 ||
			mode == 8)
		{
			DrawCircle(cx, cy, m_brush_radius1*pressure,
				text_color, proj_mat);
		}

		if (mode == 1 ||
			mode == 2 ||
			mode == 3 ||
			mode == 4)
		{
			DrawCircle(cx, cy, m_brush_radius2*pressure,
				text_color, proj_mat);
		}

		float px, py;
		px = cx-7-nx/2.0;
		py = cy+3-ny/2.0;
		wstring wstr;
		switch (mode)
		{
		case 1:
			wstr = L"S";
			break;
		case 2:
			wstr = L"+";
			break;
		case 3:
			wstr = L"-";
			break;
		case 4:
			wstr = L"*";
			break;
		}
		m_text_renderer->RenderText(m_vulkan->frameBuffers[m_vulkan->currentBuffer], 
			wstr, text_color, px*sx, py*sy);

	}
}

//paint strokes on the paint fbo
void VRenderVulkanView::PaintStroke()
{
	int nx, ny;
	nx = m_nx;
	ny = m_ny;

	double pressure = m_use_pres?m_pressure:1.0;

	vks::VulkanDevice* prim_dev = m_vulkan->devices[0];

	if (m_fbo_paint && (m_fbo_paint->w != nx || m_fbo_paint->h != ny))
	{
		m_fbo_paint.reset();
		m_tex_paint.reset();
	}
	if (!m_fbo_paint)
	{
		m_fbo_paint = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_paint->w = m_nx;
		m_fbo_paint->h = m_ny;
		m_fbo_paint->device = prim_dev;

		m_tex_paint = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
		m_fbo_paint->addAttachment(m_tex_paint);
	}

	double px = double(old_mouse_X - prv_mouse_X);
	double py = double(old_mouse_Y - prv_mouse_Y);
	double dist = sqrt(px * px + py * py);
	double step = m_brush_radius1 * pressure * m_brush_spacing;
	int repeat = int(dist / step + 0.5);
	double spx = (double)prv_mouse_X;
	double spy = (double)prv_mouse_Y;
	if (repeat > 0)
	{
		px /= repeat;
		py /= repeat;
	}

	double x, y;
	double radius1 = m_brush_radius1;
	double radius2 = m_brush_radius2;
	for (int i = 0; i <= repeat; i++)
	{
		x = spx + i * px;
		y = spy + i * py;
		switch (m_selector.GetMode())
		{
		case 3:
			radius1 = m_brush_radius2;
			break;
		case 4:
			radius1 = 0.0;
			break;
		case 8:
			radius2 = radius1;
			break;
		default:
			break;
		}
		
		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHDR_PAINT,
				V2DRENDER_BLEND_MAX,
				m_fbo_paint->attachments[0]->format,
				m_fbo_paint->attachments.size(),
				0,
				m_fbo_paint->attachments[0]->is_swapchain_images);
		//params.tex[0] = m_tex_paint.get();
		params.loc[0] = glm::vec4(float(x), float(y), float(radius1 * pressure), float(radius2 * pressure));
		params.loc[1] = glm::vec4((float)nx, (float)ny, 0.0f, 0.0f);
		params.clear = m_clear_paint;
		m_clear_paint = false;
		
		if (!m_fbo_paint->renderPass)
			m_fbo_paint->replaceRenderPass(params.pipeline.pass);
		m_v2drender->render(m_fbo_paint, params);
	}
}

//show the stroke buffer
void VRenderVulkanView::DisplayStroke()
{
	if (!m_tex_paint)
		return;

	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHADER_TEXTURE_LOOKUP,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images);
	params.tex[0] = m_tex_paint.get();
	params.clear = m_frame_clear;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b() };
	m_frame_clear = false;

	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);

	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
}

//set 2d weights
void VRenderVulkanView::Set2dWeights()
{
	m_selector.Set2DWeight(m_tex_final, m_tex_wt2?m_tex_wt2:m_tex);
}

VolumeData *VRenderVulkanView::CopyLevel(VolumeData *src, int lv)
{
	if (!src->isBrxml()) return NULL;

	bool swap_selection = (m_cur_vol == src);

	VolumeData *vd_new = src->CopyLevel(lv);
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	DataManager* mgr = vr_frame->GetDataManager();
	if (vd_new && vr_frame && mgr)
	{
		wxString group_name;
		DataGroup* group = 0;

		for (int i=(int)m_layer_list.size()-1; i>=0; i--)
		{
			if (!m_layer_list[i]) continue;

			if (m_layer_list[i]->IsA() == 5) //group
			{
				DataGroup* group = (DataGroup*)m_layer_list[i];
				for (int j=group->GetVolumeNum()-1; j>=0; j--)
				{
					VolumeData* gvd = group->GetVolumeData(j);
					if (gvd == src)
					{
						group_name = group->GetName();
						break;
					}
				}
			}
		}
		if (group_name.IsEmpty())
		{
			group_name = AddGroup("");
			group = GetGroup(group_name);
			group->SetGamma(src->GetGamma());
			group->SetBrightness(src->GetBrightness());
			group->SetHdr(src->GetHdr());
            group->SetLevels(src->GetLevels());
			group->SetSyncR(src->GetSyncR());
			group->SetSyncG(src->GetSyncG());
			group->SetSyncB(src->GetSyncB());
		}
		
		/*
		wxString src_name = src->GetName();
		ReplaceVolumeData(src_name, vd_new);
		mgr->AddVolumeData(vd_new);
		if (swap_selection) m_cur_vol = vd_new;
		SetVolPopDirty();
		PopVolumeList();
		wxString select = swap_selection ? vd_new->GetName() : m_cur_vol->GetName();
		TreePanel *tree = vr_frame->GetTree();
		if (tree)
		{
			bool fix;
			fix = tree->isFixed();
			tree->SetFix(false);
			vr_frame->UpdateTree(select, 2, false); //UpdateTree line1: m_tree_panel->DeleteAll(); <- buffer overrun
			tree->SetFix(fix);
		}
		if (swap_selection) vr_frame->OnSelection(2, m_vrv, group, vd_new);
		*/
	}

	return vd_new;
}

//segment volumes in current view
void VRenderVulkanView::Segment()
{
    HandleCamera();

	//translate object
	m_mv_mat = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
	//rotate object
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
	//center object
	m_mv_mat = glm::translate(m_mv_mat, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

	m_selector.Set2DMask(m_tex_paint);
	m_selector.Set2DWeight(m_tex_final, m_tex_wt2?m_tex_wt2:m_tex);
	//orthographic
	m_selector.SetOrthographic(!m_persp);
    m_selector.SetUseAbsoluteValue(m_use_absolute_value);

	bool copied = false;

	if (m_selector.GetSelectGroup())
	{
		VolumeData* vd = m_selector.GetVolume();
		DataGroup* group = 0;
		if (vd)
		{
			for (int i=0; i<GetLayerNum(); i++)
			{
				TreeLayer* layer = GetLayer(i);
				if (layer && layer->IsA() == 5)
				{
					DataGroup* tmp_group = (DataGroup*)layer;
					for (int j=0; j<tmp_group->GetVolumeNum(); j++)
					{
						VolumeData* tmp_vd = tmp_group->GetVolumeData(j);
						if (tmp_vd && tmp_vd==vd)
						{
							group = tmp_group;
							break;
						}
					}
				}
				if (group)
					break;
			}
			//select the group
			if (group && group->GetVolumeNum()>0)
			{
				vector<VolumeData *> cp_vd_list;
				VolumeData* tmp_vd;
				for (int i=0; i<group->GetVolumeNum(); i++)
				{
					tmp_vd = group->GetVolumeData(i);
					tmp_vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
					m_selector.SetVolume(tmp_vd);
					m_loader.SetMemoryLimitByte((long long)TextureRenderer::mainmem_buf_size_*1024LL*1024LL);
					m_loader.PreloadLevel(tmp_vd, tmp_vd->GetMaskLv(), true);
					m_selector.Select(m_brush_radius2-m_brush_radius1);
				}
				m_selector.SetVolume(vd);
			}
		}
	}
	else if (m_selector.GetSelectBoth())
	{
		VolumeData* vd = m_calculator.GetVolumeA();

		if (vd)
		{
			vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
			m_selector.SetVolume(vd);
			m_loader.SetMemoryLimitByte((long long)TextureRenderer::mainmem_buf_size_*1024LL*1024LL);
			m_loader.PreloadLevel(vd, vd->GetMaskLv(), true);
			m_selector.Select(m_brush_radius2-m_brush_radius1);
		}

		vd = m_calculator.GetVolumeB();

		if (vd)
		{
			vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
			m_selector.SetVolume(vd);
			m_loader.SetMemoryLimitByte((long long)TextureRenderer::mainmem_buf_size_*1024LL*1024LL);
			m_loader.PreloadLevel(vd, vd->GetMaskLv(), true);
			m_selector.Select(m_brush_radius2-m_brush_radius1);
		}
	}
	else
	{
		VolumeData* vd = m_selector.GetVolume();
		if (!vd && m_cur_vol)
		{
			m_selector.SetVolume(m_cur_vol);
			vd = m_selector.GetVolume();
		}

		if (vd)
		{
			m_loader.SetMemoryLimitByte((long long)TextureRenderer::mainmem_buf_size_ * 1024LL * 1024LL);
			m_loader.PreloadLevel(vd, vd->GetMaskLv(), true);
			m_selector.Select(m_brush_radius2 - m_brush_radius1);
		}
	}

	if (copied == true) RefreshGL(false, true);

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame && vr_frame->GetTraceDlg())
	{
		if (vr_frame->GetTraceDlg()->GetAutoID())
		{
			if (m_selector.GetMode() == 1 ||
				m_selector.GetMode() == 2 ||
				m_selector.GetMode() == 4)
				vr_frame->GetTraceDlg()->CellNewID(true);
			else if (m_selector.GetMode() == 3)
				vr_frame->GetTraceDlg()->CellEraseID();
		}
		if (vr_frame->GetTraceDlg()->GetManualAssist())
		{
			if (!vr_frame->GetTraceDlg()->GetAutoID())
				vr_frame->GetTraceDlg()->CellUpdate();
			vr_frame->GetTraceDlg()->CellLink(true);
		}
	}
	if (vr_frame && vr_frame->GetBrushToolDlg())
	{
		vr_frame->GetBrushToolDlg()->GetSettings(m_vrv);
	}
}

//label volumes in current view
void VRenderVulkanView::Label()
{
	m_selector.Label(0);
}

//remove noise
int VRenderVulkanView::CompAnalysis(double min_voxels, double max_voxels, double thresh, bool select, bool gen_ann, int iter_limit)
{
    int return_val = 0;

	bool copied = false;
	VolumeData *vd = m_selector.GetVolume();
	
/*
 if (vd && vd->isBrxml())
	{
		vd = CopyLevel(vd);
		m_selector.SetVolume(vd);
		if (vd) copied = true;
	}
*/

	if (!select)
	{
		m_selector.Set2DMask(m_tex_paint);
		m_selector.Set2DWeight(m_tex_final, m_tex_wt2?m_tex_wt2:m_tex);
		m_selector.SetSizeMap(false);
		return_val = m_selector.CompAnalysis(min_voxels, max_voxels, thresh, 1.0, select, gen_ann, iter_limit);
	}
	else
	{
		m_selector.SetSizeMap(false);
		return_val = m_selector.CompAnalysis(min_voxels, max_voxels, thresh, 1.0, select, gen_ann, iter_limit);
	}

	Annotations* ann = m_selector.GetAnnotations();
	if (ann)
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			DataManager* mgr = vr_frame->GetDataManager();
			if (mgr)
				mgr->AddAnnotations(ann);
		}
	}

	if (copied) RefreshGL(false, true);

	return return_val;
}

void VRenderVulkanView::CompExport(int mode, bool select)
{
	switch (mode)
	{
	case 0://multi channels
		m_selector.CompExportMultiChann(select);
		break;
	case 1://random colors
		{
			wxString sel_name = m_selector.GetVolume()->GetName();
			VolumeData* vd_r = 0;
			VolumeData* vd_g = 0;
			VolumeData* vd_b = 0;
			VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
			if (vr_frame)
			{
				for (int i=0; i<vr_frame->GetDataManager()->GetVolumeNum(); i++)
				{
					VolumeData* vd = vr_frame->GetDataManager()->GetVolumeData(i);
					wxString name = vd->GetName();
					if (name==sel_name+"_COMP1")
						vd_r = vd;
					else if (name==sel_name+"_COMP2")
						vd_g = vd;
					else if (name==sel_name+"_COMP3")
						vd_b = vd;
				}
			}
			m_selector.CompExportRandomColor(1, vd_r, vd_g, vd_b, select);
		}
		break;
	}
	vector<VolumeData*> *vol_list = m_selector.GetResultVols();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		wxString group_name = "";
		DataGroup* group = 0;
		for (int i=0; i<(int)vol_list->size(); i++)
		{
			VolumeData* vd = (*vol_list)[i];
			if (vd)
			{
				vr_frame->GetDataManager()->AddVolumeData(vd);
				//vr_frame->GetDataManager()->SetVolumeDefault(vd);
				if (i==0)
				{
					group_name = AddGroup("");
					group = GetGroup(group_name);
				}
				AddVolumeData(vd, group_name);
			}
		}
		if (group)
		{
			group->SetSyncRAll(true);
			group->SetSyncGAll(true);
			group->SetSyncBAll(true);
			VolumeData* vd = m_selector.GetVolume();
			if (vd)
			{
				FLIVR::Color col = vd->GetGamma();
				group->SetGammaAll(col);
				col = vd->GetBrightness();
				group->SetBrightnessAll(col);
				col = vd->GetHdr();
				group->SetHdrAll(col);
                col = vd->GetLevels();
                group->SetLevelsAll(col);
			}
		}
		vr_frame->UpdateTree(m_selector.GetVolume()->GetName(), 2);
		RefreshGL();
	}
}

void VRenderVulkanView::ShowAnnotations(bool select_annotations)
{
	Annotations* ann = m_selector.GetAnnotations();
	if (ann)
	{
		AddAnnotations(ann);
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
        {
            if (select_annotations)
                vr_frame->UpdateTree(ann->GetName(), 4);
			else
                vr_frame->UpdateTree(vr_frame->GetCurSelVol()->GetName(), 2);
        }
	}
}

int VRenderVulkanView::NoiseAnalysis(double min_voxels, double max_voxels, double thresh)
{
   	int return_val = 0;

	bool copied = false;
	VolumeData *vd = m_selector.GetVolume();
	if (vd && vd->isBrxml())
	{
		vd = CopyLevel(vd);
		m_selector.SetVolume(vd);
		if (vd) copied = true;
	}

	m_selector.Set2DMask(m_tex_paint);
	m_selector.Set2DWeight(m_tex_final, m_tex_wt2?m_tex_wt2:m_tex);
	return_val = m_selector.CompAnalysis(min_voxels, max_voxels, thresh, 1.0, false, false);

	if (copied) RefreshGL(false, true);

	return return_val;
}

void VRenderVulkanView::NoiseRemoval(int iter, double thresh)
{
    VolumeData* vd = m_selector.GetVolume();
	if (!vd || !vd->GetMask(false)) return;

	//if(!vd->GetMask()) NoiseAnalysis(0.0, iter, thresh);

	wxString name = vd->GetName();
	if (name.Find("_NR")==wxNOT_FOUND)
	{
		m_selector.NoiseRemoval(iter, thresh, 1);
		vector<VolumeData*> *vol_list = m_selector.GetResultVols();
		if(!vol_list || vol_list->empty()) return;
		VolumeData* vd_new = (*vol_list)[0];
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vd_new && vr_frame)
		{
			vr_frame->GetDataManager()->AddVolumeData(vd_new);
			wxString group_name = AddGroup("");
			DataGroup *group = GetGroup(group_name);
			group->SetGamma(vd->GetGamma());
			group->SetBrightness(vd->GetBrightness());
			group->SetHdr(vd->GetHdr());
            group->SetLevels(vd->GetLevels());
			group->SetSyncR(vd->GetSyncR());
			group->SetSyncG(vd->GetSyncG());
			group->SetSyncB(vd->GetSyncB());
			AddVolumeData(vd_new, group_name);
			vd->SetDisp(false);
			vr_frame->UpdateTree(vd_new->GetName(), 2);
		}
	}
	else
		m_selector.NoiseRemoval(iter, thresh);
}

//EVE
string VRenderVulkanView::EVEAnalysis(int min_radius, int max_radius, double thresh, int target_lv)
{
	string return_val = "";

	VolumeData* vd = m_selector.GetVolume();

	if (!select)
	{
		m_selector.Set2DMask(m_tex_paint);
		m_selector.Set2DWeight(m_tex_final, m_tex_wt2 ? m_tex_wt2 : m_tex);
	}
	m_selector.SetSizeMap(false);
	m_selector.EVEAnalysis(min_radius, max_radius, thresh, target_lv);

	Annotations* ann = m_selector.GetAnnotations();
	if (ann)
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			DataManager* mgr = vr_frame->GetDataManager();
			if (mgr)
            {
				mgr->AddAnnotations(ann);
            }
		}
		return_val = ann->GetName();
        ShowAnnotations(true);
	}

	return return_val;
}

//brush properties
//load default
void VRenderVulkanView::LoadDefaultBrushSettings()
{
	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(),NULL);
#ifdef _DARWIN
	wxString dft = expath + "/../Resources/default_brush_settings.dft";
#else
	wxString dft = expath + GETSLASHS() + "default_brush_settings.dft";
	if (!wxFileExists(dft))
		dft = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "default_brush_settings.dft";
#endif
	wxFileInputStream is(dft);
	if (!is.IsOk())
		return;
	wxFileConfig fconfig(is);

	double val;
	int ival;
	bool bval;

	//brush properties
	if (fconfig.Read("brush_ini_thresh", &val))
		m_selector.SetBrushIniThresh(val);
	if (fconfig.Read("brush_gm_falloff", &val))
		m_selector.SetBrushGmFalloff(val);
	if (fconfig.Read("brush_scl_falloff", &val))
		m_selector.SetBrushSclFalloff(val);
	if (fconfig.Read("brush_scl_translate", &val))
	{
		m_selector.SetBrushSclTranslate(val);
		m_calculator.SetThreshold(val);
	}
	//auto thresh
	if (fconfig.Read("auto_thresh", &bval))
		m_selector.SetEstimateThreshold(bval);
	//edge detect
	if (fconfig.Read("edge_detect", &bval))
		m_selector.SetEdgeDetect(bval);
	//hidden removal
	if (fconfig.Read("hidden_removal", &bval))
		m_selector.SetHiddenRemoval(bval);
	//select group
	if (fconfig.Read("select_group", &bval))
		m_selector.SetSelectGroup(bval);
	//2d influence
	if (fconfig.Read("brush_2dinfl", &val))
		m_selector.SetW2d(val);
	//size 1
	if (fconfig.Read("brush_size1", &val) && val>0.0)
		m_brush_radius1 = val;
	//size 2 link
	if (fconfig.Read("use_brush_size2", &bval))
	{
		m_use_brush_radius2 = bval;
		m_selector.SetUseBrushSize2(bval);
	}
	//size 2
	if (fconfig.Read("brush_size2", &val) && val>0.0)
		m_brush_radius2 = val;
	//iterations
	if (fconfig.Read("brush_iters", &ival))
	{
		switch (ival)
		{
		case 1:
			m_selector.SetBrushIteration(BRUSH_TOOL_ITER_WEAK);
			break;
		case 2:
			m_selector.SetBrushIteration(BRUSH_TOOL_ITER_NORMAL);
			break;
		case 3:
			m_selector.SetBrushIteration(BRUSH_TOOL_ITER_STRONG);
			break;
		}
	}
	if (fconfig.Read("use_dslt", &bval))
		m_selector.SetUseDSLTBrush(bval);
	if (fconfig.Read("dslt_r", &val) && val>0.0)
		m_selector.SetBrushDSLT_R(val);
	if (fconfig.Read("dslt_q", &val) && val>0.0)
		m_selector.SetBrushDSLT_Q(val);
	if (fconfig.Read("dslt_c", &val))
		m_selector.SetBrushDSLT_C(val);
}

void VRenderVulkanView::SetBrushUsePres(bool pres)
{
	m_use_pres = pres;
}

bool VRenderVulkanView::GetBrushUsePres()
{
	return m_use_pres;
}

void VRenderVulkanView::SetUseBrushSize2(bool val)
{
	m_use_brush_radius2 = val;
	m_selector.SetUseBrushSize2(val);
	if (!val)
		m_brush_radius2 = m_brush_radius1;
}

bool VRenderVulkanView::GetBrushSize2Link()
{
	return m_use_brush_radius2;
}

void VRenderVulkanView::SetBrushSize(double size1, double size2)
{
	if (size1 > 0.0)
		m_brush_radius1 = size1;
	if (size2 > 0.0)
		m_brush_radius2 = size2;
	if (!m_use_brush_radius2)
		m_brush_radius2 = m_brush_radius1;
}

double VRenderVulkanView::GetBrushSize1()
{
	return m_brush_radius1;
}

double VRenderVulkanView::GetBrushSize2()
{
	return m_brush_radius2;
}

void VRenderVulkanView::SetBrushSpacing(double spacing)
{
	if (spacing > 0.0)
		m_brush_spacing = spacing;
}

double VRenderVulkanView::GetBrushSpacing()
{
	return m_brush_spacing;
}

void VRenderVulkanView::SetBrushIteration(int num)
{
	m_selector.SetBrushIteration(num);
}

int VRenderVulkanView::GetBrushIteration()
{
	return m_selector.GetBrushIteration();
}

//brush translate
void VRenderVulkanView::SetBrushSclTranslate(double val)
{
	m_selector.SetBrushSclTranslate(val);
	m_calculator.SetThreshold(val);
}

double VRenderVulkanView::GetBrushSclTranslate()
{
	return m_selector.GetBrushSclTranslate();
}

//gm falloff
void VRenderVulkanView::SetBrushGmFalloff(double val)
{
	m_selector.SetBrushGmFalloff(val);
}

double VRenderVulkanView::GetBrushGmFalloff()
{
	return m_selector.GetBrushGmFalloff();
}

void VRenderVulkanView::SetW2d(double val)
{
	m_selector.SetW2d(val);
}

double VRenderVulkanView::GetW2d()
{
	return m_selector.GetW2d();
}

//edge detect
void VRenderVulkanView::SetEdgeDetect(bool value)
{
	m_selector.SetEdgeDetect(value);
}

bool VRenderVulkanView::GetEdgeDetect()
{
	return m_selector.GetEdgeDetect();
}

//hidden removal
void VRenderVulkanView::SetHiddenRemoval(bool value)
{
	m_selector.SetHiddenRemoval(value);
}

bool VRenderVulkanView::GetHiddenRemoval()
{
	return m_selector.GetHiddenRemoval();
}

//select group
void VRenderVulkanView::SetSelectGroup(bool value)
{
	m_selector.SetSelectGroup(value);
}

bool VRenderVulkanView::GetSelectGroup()
{
	return m_selector.GetSelectGroup();
}

void VRenderVulkanView::SetUseAbsoluteValue(bool value)
{
    m_use_absolute_value = value;
}
bool VRenderVulkanView::GetUseAbsoluteValue()
{
    return m_use_absolute_value;
}

//estimate threshold
void VRenderVulkanView::SetEstimateThresh(bool value)
{
	m_selector.SetEstimateThreshold(value);
}

bool VRenderVulkanView::GetEstimateThresh()
{
	return m_selector.GetEstimateThreshold();
}

//select both
void VRenderVulkanView::SetSelectBoth(bool value)
{
	m_selector.SetSelectBoth(value);
}

bool VRenderVulkanView::GetSelectBoth()
{
	return m_selector.GetSelectBoth();
}

void VRenderVulkanView::SetUseDSLTBrush(bool value)
{
	m_selector.SetUseDSLTBrush(value);
}

bool VRenderVulkanView::GetUseDSLTBrush()
{
	return m_selector.GetUseDSLTBrush();
}

void VRenderVulkanView::SetBrushDSLT_R(int rad)
{
	m_selector.SetBrushDSLT_R(rad);
}

int VRenderVulkanView::GetBrushDSLT_R()
{
	return m_selector.GetBrushDSLT_R();
}

void VRenderVulkanView::SetBrushDSLT_Q(int quality)
{
	m_selector.SetBrushDSLT_Q(quality);
}

int VRenderVulkanView::GetBrushDSLT_Q()
{
	return m_selector.GetBrushDSLT_Q();
}

void VRenderVulkanView::SetBrushDSLT_C(double c)
{
	m_selector.SetBrushDSLT_C(c);
}

double VRenderVulkanView::GetBrushDSLT_C()
{
	return m_selector.GetBrushDSLT_C();
}


//calculations
void VRenderVulkanView::SetVolumeA(VolumeData* vd)
{
	m_calculator.SetVolumeA(vd);
	m_selector.SetVolume(vd);
}

void VRenderVulkanView::SetVolumeB(VolumeData* vd)
{
	m_calculator.SetVolumeB(vd);
}

void VRenderVulkanView::SetVolumeC(VolumeData* vd)
{
	m_calculator.SetVolumeC(vd);
}

void VRenderVulkanView::CalculateSingle(int type, wxString prev_group, bool add)
{
	bool copiedA = false;
	bool copiedB = false;
	bool copiedC = false;
	VolumeData* vd_A = m_calculator.GetVolumeA();
	VolumeData* vd_prevA = vd_A;
	if (vd_A && vd_A->isBrxml())
	{
		vd_prevA = vd_A;
		vd_A = CopyLevel(vd_A);
		if (vd_A)
			copiedA = true;
	}
	VolumeData* vd_B = m_calculator.GetVolumeB();
	VolumeData* vd_prevB = vd_B;
	if (vd_B && vd_B->isBrxml())
	{
		vd_prevB = vd_B;
		vd_B = CopyLevel(vd_B);
		if (vd_B) 
			copiedB = true;
	}
	VolumeData* vd_C = m_calculator.GetVolumeC();
	VolumeData* vd_prevC = vd_C;
	if (vd_C && vd_C->isBrxml())
	{
		vd_prevC = vd_C;
		vd_C = CopyLevel(vd_C);
		if (vd_C)
			copiedC = true;
	}
	m_calculator.SetVolumeA(vd_A);
	m_calculator.SetVolumeB(vd_B);
	m_calculator.SetVolumeC(vd_C);

	m_calculator.Calculate(type);
	VolumeData* vd = m_calculator.GetResult();
	if (vd)
	{
		if (type == 1 ||
			type == 2 ||
			type == 3 ||
			type == 4 ||
			type == 5 ||
			type == 6 ||
			type == 8 ||
			type == 9 ||
			type == 10 ||
			type == 11)
		{
			VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
			if (vr_frame)
			{
				//copy 2d adjust & color
				VolumeData* vd_a = m_calculator.GetVolumeA();
				if (vd_a)
				{
					//clipping planes
					vector<Plane*>* planes = vd_a->GetVR() ? vd_a->GetVR()->get_planes() : 0;
					if (planes && vd->GetVR())
						vd->GetVR()->set_planes(planes);
					//transfer function
					vd->Set3DGamma(vd_a->Get3DGamma());
					vd->SetBoundary(vd_a->GetBoundary());
					vd->SetOffset(vd_a->GetOffset());
					vd->SetLeftThresh(vd_a->GetLeftThresh());
					vd->SetRightThresh(vd_a->GetRightThresh());
					FLIVR::Color col = vd_a->GetColor();
					vd->SetColor(col);
					vd->SetAlpha(vd_a->GetAlpha());
					//shading
					vd->SetShading(vd_a->GetShading());
					double amb, diff, spec, shine;
					vd_a->GetMaterial(amb, diff, spec, shine);
					vd->SetMaterial(amb, diff, spec, shine);
					//shadow
					vd->SetShadow(vd_a->GetShadow());
					double shadow;
					vd_a->GetShadowParams(shadow);
					vd->SetShadowParams(shadow);
					//sample rate
					vd->SetSampleRate(vd_a->GetSampleRate());
					//2d adjusts
					col = vd_a->GetGamma();
					vd->SetGamma(col);
					col = vd_a->GetBrightness();
					vd->SetBrightness(col);
					col = vd_a->GetHdr();
					vd->SetHdr(col);
                    col = vd_a->GetLevels();
                    vd->SetLevels(col);
					vd->SetSyncR(vd_a->GetSyncR());
					vd->SetSyncG(vd_a->GetSyncG());
					vd->SetSyncB(vd_a->GetSyncB());
					vd->SetMaxValue(vd_a->GetMaxValue());
					vd->SetScalarScale(vd_a->GetScalarScale());
				}

				if (add)
				{
					bool btmp = vr_frame->GetDataManager()->GetOverrideVox();
					vr_frame->GetDataManager()->SetOverrideVox(false);
					vr_frame->GetDataManager()->AddVolumeData(vd);
					//vr_frame->GetDataManager()->SetVolumeDefault(vd);
					AddVolumeData(vd, prev_group);
					vr_frame->GetDataManager()->SetOverrideVox(btmp);

					if (type == 5 ||
						type == 6 ||
						type == 9)
					{
						if (vd_a)
							vd_a->SetDisp(false);
					}
					else if (type == 1 ||
						type == 2 ||
						type == 3 ||
						type == 4)
					{
						if (vd_a)
							vd_a->SetDisp(false);
						VolumeData* vd_b = m_calculator.GetVolumeB();
						if (vd_b)
							vd_b->SetDisp(false);
					}
					else if (type == 10 || type == 11)
					{
						if (vd_a)
							vd_a->SetDisp(false);
						VolumeData* vd_b = m_calculator.GetVolumeB();
						if (vd_b)
							vd_b->SetDisp(false);
						VolumeData* vd_c = m_calculator.GetVolumeC();
						if (vd_c)
							vd_c->SetDisp(false);
					}

					if (copiedA)
						vd_prevA->SetDisp(vd_A->GetDisp());
					if (copiedB)
						vd_prevB->SetDisp(vd_B->GetDisp());
					if (copiedC)
						vd_prevC->SetDisp(vd_C->GetDisp());

					vr_frame->UpdateTree(vd->GetName(), 2, false); //UpdateTree line1: m_tree_panel->DeleteAll(); <- buffer overrun
					m_calculator.SetVolumeA(vd_prevA);
				}
			}
			RefreshGL();

			if (copiedA)
				delete vd_A;
			if (copiedB)
				delete vd_B;
			if (copiedC)
				delete vd_C;
		}
		else if (type == 7)
		{
			VolumeData* vd_a = m_calculator.GetVolumeA();
			if (vd_a)
			{
				wxString nm = vd_A->GetName();
				ReplaceVolumeData(nm, vd_a);
				VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
				if (vr_frame)
					vr_frame->GetPropView()->SetVolumeData(vd_a);
			}
			RefreshGL();

			if (copiedB)
				delete vd_B;
			if (copiedC)
				delete vd_C;
		}
	}

	if (copiedA || copiedB || copiedC)
	{
		RefreshGL(false, true);
	}
}

void VRenderVulkanView::Calculate(int type, wxString prev_group, bool add)
{
	if (type == 5 ||
		type == 6 ||
		type == 7)
	{
		vector<VolumeData*> vd_list;
		if (m_selector.GetSelectGroup())
		{
			VolumeData* vd = m_calculator.GetVolumeA();
			DataGroup* group = 0;
			if (vd)
			{
				for (int i=0; i<GetLayerNum(); i++)
				{
					TreeLayer* layer = GetLayer(i);
					if (layer && layer->IsA()==5)
					{
						DataGroup* tmp_group = (DataGroup*)layer;
						for (int j=0; j<tmp_group->GetVolumeNum(); j++)
						{
							VolumeData* tmp_vd = tmp_group->GetVolumeData(j);
							if (tmp_vd && tmp_vd==vd)
							{
								group = tmp_group;
								break;
							}
						}
					}
					if (group)
						break;
				}
			}
			if (group && group->GetVolumeNum()>1)
			{
				for (int i=0; i<group->GetVolumeNum(); i++)
				{
					VolumeData* tmp_vd = group->GetVolumeData(i);
					if (tmp_vd && tmp_vd->GetDisp())
						vd_list.push_back(tmp_vd);
				}
				for (size_t i=0; i<vd_list.size(); ++i)
				{
					m_calculator.SetVolumeA(vd_list[i]);
					CalculateSingle(type, prev_group, add);
				}
				m_calculator.SetVolumeA(vd);
			}
			else
				CalculateSingle(type, prev_group, add);
		}
		else
			CalculateSingle(type, prev_group, add);
	}
	else
		CalculateSingle(type, prev_group, add);
}

void VRenderVulkanView::StartTileRendering(int w_, int h_, int tilew_, int tileh_)
{
	m_tile_rendering = true;
	m_capture_change_res = true;
	m_capture_resx = w_;
	m_capture_resy = h_;
	m_tile_w = tilew_;
	m_tile_h = tileh_;
	m_tile_xnum = w_/tilew_ + (w_%tilew_ > 0);
	m_tile_ynum = h_/tileh_ + (h_%tileh_ > 0);
	m_current_tileid = 0;

	m_tmp_res_mode = m_res_mode;
	m_res_mode = 1;
	if (!m_tmp_sample_rates.empty())
		m_tmp_sample_rates.clear();
	PopVolumeList();
	for (VolumeData* vd : m_vd_pop_list) {
		if (vd) {
			m_tmp_sample_rates[vd] = vd->GetSampleRate();
			//vd->SetSampleRate(1.0);
		}
	}

	if (m_capture) {
		if (m_tiled_image)
			delete [] m_tiled_image;
		m_tiled_image = new unsigned char [(size_t)m_capture_resx*(size_t)m_capture_resy*3];
	}

	if (m_tile_vobj.vertBuf.buffer != VK_NULL_HANDLE)
		m_tile_vobj.vertBuf.destroy();
	if (m_tile_vobj.idxBuf.buffer != VK_NULL_HANDLE)
		m_tile_vobj.idxBuf.destroy();

	vector<Vulkan2dRender::Vertex> verts;
	std::vector<uint32_t> indices = { 0,1,2, 2,3,0 };

	int w = GetSize().x;
	int h = GetSize().y;

	double win_aspect = (double)w / (double)h;
	double ren_aspect = (double)m_capture_resx / (double)m_capture_resy;
	double stpos_x = -1.0;
	double stpos_y = -1.0;
	double edpos_x = 1.0;
	double edpos_y = 1.0;
	double tilew = 1.0;
	double tileh = 1.0;

	double xbound = 1.0;
	double ybound = -1.0;

	for (int tileid = 0; tileid < m_tile_xnum * m_tile_ynum; tileid++) {

		if (win_aspect > ren_aspect) {
			tilew = 2.0 * m_tile_w * h / m_capture_resy / w;
			tileh = 2.0 * m_tile_h * h / m_capture_resy / h;
			stpos_x = -1.0 + 2.0 * (w - h * ren_aspect) / 2.0 / w;
			stpos_y = -1.0;
			xbound = stpos_x + 2.0 * h * ren_aspect / w;
			ybound = 1.0;

			stpos_x = stpos_x + tilew * (tileid % m_tile_xnum);
			stpos_y = stpos_y + tileh * (tileid / m_tile_xnum);
			edpos_x = stpos_x + tilew;
			edpos_y = stpos_y + tileh;
		}
		else {
			tilew = 2.0 * m_tile_w * w / m_capture_resx / w;
			tileh = 2.0 * m_tile_h * w / m_capture_resx / h;
			stpos_x = -1.0;
			stpos_y = -1.0 + 2.0 * (h - w / ren_aspect) / 2.0 / h;
			xbound = 1.0;
			ybound = stpos_y + 2.0 * w / ren_aspect / h;

			stpos_x = stpos_x + tilew * (tileid % m_tile_xnum);
			stpos_y = stpos_y + tileh * (tileid / m_tile_xnum);
			edpos_x = stpos_x + tilew;
			edpos_y = stpos_y + tileh;
		}

		double tex_x = 1.0;
		double tex_y = 1.0;
		if (edpos_x > xbound) {
			tex_x = (xbound - stpos_x) / tilew;
			edpos_x = xbound;
		}
		if (edpos_y > ybound) {
			tex_y = (ybound - stpos_y) / tileh;
			edpos_y = ybound;
		}

		verts.push_back(Vulkan2dRender::Vertex{ {(float)edpos_x, (float)edpos_y, 0.0f}, {(float)tex_x, (float)tex_y, 0.0f} });
		verts.push_back(Vulkan2dRender::Vertex{ {(float)stpos_x, (float)edpos_y, 0.0f}, {0.0f,         (float)tex_y, 0.0f} });
		verts.push_back(Vulkan2dRender::Vertex{ {(float)stpos_x, (float)stpos_y, 0.0f}, {0.0f,         0.0f,         0.0f} });
		verts.push_back(Vulkan2dRender::Vertex{ {(float)edpos_x, (float)stpos_y, 0.0f}, {(float)tex_x, 0.0f,         0.0f} });

	}

	VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
		&m_tile_vobj.vertBuf,
		verts.size() * sizeof(Vulkan2dRender::Vertex),
		verts.data()));

	VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
		&m_tile_vobj.idxBuf,
		indices.size() * sizeof(uint32_t),
		indices.data()));

	m_tile_vobj.idxCount = indices.size();
	m_tile_vobj.idxOffset = 0;
	m_tile_vobj.vertCount = verts.size();
	m_tile_vobj.vertOffset = 0;

	Resize();
}

void VRenderVulkanView::EndTileRendering()
{
	m_tile_rendering = false;
	m_res_mode = m_tmp_res_mode;
	PopVolumeList();
/*	for (VolumeData* vd : m_vd_pop_list) {
		if (vd && m_tmp_sample_rates.count(vd) > 0)
			vd->SetSampleRate(m_tmp_sample_rates[vd]);
	}
*/	if (m_tiled_image != NULL) {
		delete [] m_tiled_image;
		m_tiled_image = NULL;
	}

	Resize(false);
}

void VRenderVulkanView::Get1x1DispSize(int &w, int &h)
{
	int nx = GetSize().x;
	int ny = GetSize().y;
	PopVolumeList();

	double maxvpd = 0.0;
	for (VolumeData* vd : m_vd_pop_list) {
		Texture *vtex = vd->GetTexture();
		if (vtex)
		{
			double res_scale = 1.0;
			double aspect = (double)nx / (double)ny;
			double spc_x;
			double spc_y;
			double spc_z;
			vtex->get_spacings(spc_x, spc_y, spc_z, 0);
			spc_x = spc_x<EPS?1.0:spc_x;
			spc_y = spc_y<EPS?1.0:spc_y;

			double sf;
			if (aspect > 1.0)
				sf = 2.0*m_radius/spc_y/double(ny);
			else
				sf = 2.0*m_radius/spc_x/double(nx);

			double vx_per_dot = m_scale_factor / sf;

			if (maxvpd < vx_per_dot) maxvpd = vx_per_dot;
		}
	}

	w = (int)(nx / maxvpd);
	h = (int)(ny / maxvpd);
}

void VRenderVulkanView::DrawTile()
{
	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;

	int nx, ny;
	nx = m_nx;
	ny = m_ny;

	//generate textures & buffer objects
	if (m_fbo_tile && (m_fbo_tile->w != nx || m_fbo_tile->h != ny))
	{
		m_fbo_tile.reset();
		m_tex_tile.reset();
	}

	if (!m_fbo_tile)
	{
		m_fbo_tile = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_tile->w = m_nx;
		m_fbo_tile->h = m_ny;
		m_fbo_tile->device = prim_dev;

		m_tex_tile = prim_dev->GenTexture2D(VK_FORMAT_R8G8B8A8_UNORM, VK_FILTER_LINEAR, m_nx, m_ny);
		m_fbo_tile->addAttachment(m_tex_tile);
	}

	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 0.0f };
	params.clear = true;

	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_BLEND_BRIGHT_BACKGROUND_HDR,
			V2DRENDER_BLEND_OVER,
			m_fbo_tile->attachments[0]->format,
			m_fbo_tile->attachments.size(),
			0,
			m_fbo_tile->attachments[0]->is_swapchain_images);
	params.tex[0] = m_tex_final.get();
	params.loc[0] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	params.loc[1] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	params.loc[2] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

	if (!m_fbo_tile->renderPass)
		m_fbo_tile->replaceRenderPass(params.pipeline.pass);

	m_v2drender->render(m_fbo_tile, params);
}

//draw out the framebuffer after composition
void VRenderVulkanView::PrepFinalBuffer()
{
	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;

	int nx, ny;
	nx = m_nx;
	ny = m_ny;

	//generate textures & buffer objects
	//frame buffer for final
	if (m_fbo_final && (m_fbo_final->w != nx || m_fbo_final->h != ny))
	{
		m_fbo_final.reset();
		m_tex_final.reset();
	}
	if (!m_fbo_final)
	{
		m_fbo_final = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_final->w = m_nx;
		m_fbo_final->h = m_ny;
		m_fbo_final->device = prim_dev;

		m_tex_final = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
		m_fbo_final->addAttachment(m_tex_final);
	}

}

void VRenderVulkanView::ClearFinalBuffer()
{
	m_clear_final_buffer = true;
}

void VRenderVulkanView::DrawFinalBuffer(bool clear)
{
	//draw the final buffer to the windows buffer
	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_BLEND_BRIGHT_BACKGROUND_HDR,
			V2DRENDER_BLEND_OVER,
			current_fbo->attachments[0]->format,
			current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images);
	params.tex[0] = m_tex_final.get();
	params.loc[0] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	params.loc[1] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	params.loc[2] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

	params.clear = clear;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 1.0f };
	m_frame_clear = false;
	
	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);

	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
}

//Draw the volmues with compositing
//peel==true -- depth peeling
void VRenderVulkanView::DrawVolumesComp(vector<VolumeData*> &list, bool mask, int peel)
{
	if (list.size() <= 0)
		return;

	vks::VulkanDevice *prim_dev = m_vulkan->vulkanDevice;

	int i;

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	//count volumes with mask
	//calculate sampling frequency factor
	int cnt_mask = 0;
	bool use_tex_wt2 = false;
	double sampling_frq_fac = 2 / min(m_ortho_right-m_ortho_left, m_ortho_top-m_ortho_bottom);
	for (i=0; i<(int)list.size(); i++)
	{
		VolumeData* vd = list[i];
		if (!vd || !vd->GetDisp())
			continue;
		Texture *tex = vd->GetTexture();
		if (tex)
		{
			/*			Transform *field_trans = tex->transform();
			Vector spcv[3] = {Vector(1.0, 0.0, 0.0), Vector(0.0, 1.0, 0.0), Vector(0.0, 0.0, 1.0)};
			double maxlen = -1;
			for(int i = 0; i < 3 ; i++)
			{
			// index space view direction
			Vector v;
			v = field_trans->project(spcv[i]);
			v.safe_normalize();
			v = field_trans->project(spcv[i]);

			double len = Dot(spcv[i], v);
			if(len > maxlen) maxlen = len;
			}
			if (maxlen > sampling_frq_fac) sampling_frq_fac = maxlen;
			*/
			if ( (tex->nmask()!=-1 || !vd->GetSharedMaskName().IsEmpty()) && vd->GetMaskHideMode() == VOL_MASK_HIDE_NONE)
			{
				cnt_mask++;
				if (vr_frame &&
					vd==vr_frame->GetCurSelVol() &&
					!mask)
					use_tex_wt2 = true;
			}
		}
	}

	if (mask && cnt_mask==0)
		return;

	int nx, ny;
	nx = m_nx;
	ny = m_ny;

	if (m_fbo && (m_fbo->w != nx || m_fbo->h != ny))
	{
		m_fbo.reset();
		m_tex.reset();
	}
	if (use_tex_wt2 && m_fbo_wt2 && (m_fbo_wt2->w != nx || m_fbo_wt2->h != ny))
	{
		m_fbo_wt2.reset();
		m_tex_wt2.reset();
	}

	//generate textures & buffer objects
	//frame buffer for each volume
	if (!m_fbo)
	{
		m_fbo = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo->w = m_nx;
		m_fbo->h = m_ny;
		m_fbo->device = prim_dev;

		m_tex = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
		m_fbo->addAttachment(m_tex);
	}
	if (use_tex_wt2 && !m_fbo_wt2)
	{
		m_fbo_wt2 = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_wt2->w = m_nx;
		m_fbo_wt2->h = m_ny;
		m_fbo_wt2->device = prim_dev;

		m_tex_wt2 = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
		m_fbo_wt2->addAttachment(m_tex_wt2);
	}
	
	//draw each volume to fbo
	for (i=0; i<(int)list.size(); i++)
	{
		VolumeData* vd = list[i];
		if (!vd || !vd->GetDisp())
			continue;
		if (mask)
		{
			if (vd->GetMaskHideMode() != VOL_MASK_HIDE_NONE)
				continue;
			//when run script
			VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
			if (vr_frame &&
				vr_frame->GetMovieView() &&
				vr_frame->GetMovieView()->IsRunningScript() &&
				vd->GetMask(false) &&
				vd->GetLabel(false))
				continue;

			if ( vd->GetTexture() && (vd->GetTexture()->nmask()!=-1 || !vd->GetSharedMaskName().IsEmpty()) )
			{
				Color tmp_hdr;
				if (m_enhance_sel)
				{
					Color mask_color = vd->GetMaskColor();
					double hdr_r = 0.0;
					double hdr_g = 0.0;
					double hdr_b = 0.0;
					if (mask_color.r() > 0.0)
						hdr_r = 0.4;
					if (mask_color.g() > 0.0)
						hdr_g = 0.4;
					if (mask_color.b() > 0.0)
						hdr_b = 0.4;
					Color hdr_color = Color(hdr_r, hdr_g, hdr_b);
					tmp_hdr = vd->GetHdr();
					vd->SetHdr(hdr_color);
				}

				//wxString dbgstr = wxString::Format("Drawing Mask: duration %d\n", duration);
				//OutputDebugStringA(dbgstr.ToStdString().c_str());

				vd->SetMaskMode(1);
				int vol_method = m_vol_method;
				m_vol_method = VOL_METHOD_COMP;
				if (vd->GetMode() == 1)
					DrawMIP(vd, m_fbo, peel, sampling_frq_fac);
				else
					DrawOVER(vd, m_fbo, peel, sampling_frq_fac);
				vd->SetMaskMode(0);
				m_vol_method = vol_method;

				if (m_enhance_sel) vd->SetHdr(tmp_hdr);
			}
		}
		else
		{
			bool wt2_enable = false;
			if (vr_frame && vd==vr_frame->GetCurSelVol() &&
				vd->GetTexture() && use_tex_wt2)
			{
				wt2_enable = true;
			}
			if (vd->GetBlendMode()!=2)
			{
				//when run script
				VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
				if (vr_frame &&
					vr_frame->GetMovieView() &&
					vr_frame->GetMovieView()->IsRunningScript() &&
					vd->GetMask(false) &&
					vd->GetLabel(false))
					vd->SetMaskMode(4);

				if (vd->GetMode() == 1)
					DrawMIP(vd, wt2_enable ? m_fbo_wt2 : m_fbo, peel, sampling_frq_fac);
				else
					DrawOVER(vd, wt2_enable ? m_fbo_wt2 : m_fbo, peel, sampling_frq_fac);
			}
		}
	}
}

void VRenderVulkanView::SetBufferScale(int mode)
{
	m_res_scale = 1.0;
	switch(m_res_mode)
	{
	case 1:
		m_res_scale = 1;
		break;
	case 2:
		m_res_scale = 1.5;
		break;
	case 3:
		m_res_scale = 2.0;
		break;
	case 4:
		m_res_scale = 3.0;
		break;
	default:
		m_res_scale = 1.0;
	}
}

void VRenderVulkanView::switchLevel(VolumeData *vd)
{
	if(!vd) return;

	int nx, ny;
	nx = m_nx;
	ny = m_ny;
	/*
	if(m_min_ppi < 0)m_min_ppi = 60;
	//wxDisplay disp(wxDisplay::GetFromWindow(m_frame));
	wxSize disp_ppi = wxGetDisplayPPI();
	double disp_ppi_x = m_min_ppi;
	double disp_ppi_y = m_min_ppi;
	if(disp_ppi.GetX() > 0)disp_ppi_x = disp_ppi.GetX();
	if(disp_ppi.GetY() > 0)disp_ppi_y = disp_ppi.GetY();
	*/
	Texture *vtex = vd->GetTexture();
	if (vtex && vtex->isBrxml())
	{
		int prev_lv = vd->GetLevel();
		int new_lv = 0;

		if (m_res_mode == 5)
			new_lv = vtex->GetLevelNum()-1;

		if (m_res_mode > 0 && m_res_mode < 5)
		{
			double res_scale = 1.0;
			switch(m_res_mode)
			{
			case 1:
				res_scale = 1;
				break;
			case 2:
				res_scale = 1.5;
				break;
			case 3:
				res_scale = 2.0;
				break;
			case 4:
				res_scale = 3.0;
				break;
			default:
				res_scale = 1.0;
			}
			vector<double> sfs;
			vector<double> spx, spy, spz;
			int lvnum = vtex->GetLevelNum();
			for (int i = 0; i < lvnum; i++)
			{
				double aspect = (double)nx / (double)ny;

				double spc_x;
				double spc_y;
				double spc_z;
				vtex->get_spacings(spc_x, spc_y, spc_z, i);
				spc_x = spc_x<EPS?1.0:spc_x;
				spc_y = spc_y<EPS?1.0:spc_y;

				spx.push_back(spc_x);
				spy.push_back(spc_y);
				spz.push_back(spc_z);

				double sf;
				if (aspect > 1.0)
					sf = 2.0*m_radius*res_scale/spc_y/double(ny);
				else
					sf = 2.0*m_radius*res_scale/spc_x/double(nx);
				sfs.push_back(sf);
			}

			int lv = lvnum - 1;
			if (!m_manip)
			{
				for (int i = lvnum - 1; i >= 0; i--)
				{
					if (m_scale_factor > ( m_int_res ? sfs[i]*16.0 : sfs[i])) lv = i - 1;
				}
			}
			if (lv < 0) lv = 0;
			if (lv >= lvnum) lv = lvnum - 1;
			new_lv = lv;
			
			/*
			if (lv == 0)
			{
				
				double vxsize_on_screen = m_scale_factor / (sfs[0]/res_scale);
				if (vxsize_on_screen > 1.5)
				{
					double fac = 0.0;
					switch(m_res_mode)
					{
					case 1:
						fac = 0.0;
						break;
					case 2:
						fac = 0.5;
						break;
					case 3:
						fac = 1.0;
						break;
					case 4:
						fac = 2.0;
						break;
					default:
						fac = 0.0;
					}
					double bscale = (vxsize_on_screen - 1.5)*fac + 1.0;
					vd->GetVR()->set_buffer_scale(1.0/bscale);
				}
				else
				vd->GetVR()->set_buffer_scale(1.0);
			}
			else*/
				vd->GetVR()->set_buffer_scale(1.0);
		}
		if (prev_lv != new_lv)
		{
			vector<TextureBrick*> *bricks = vtex->get_bricks();
			if (bricks)
			{
				for (int i = 0; i < bricks->size(); i++)
					(*bricks)[i]->set_disp(false);
			}
			vd->SetLevel(new_lv);
			vtex->set_sort_bricks();
		}
	}

	if (vtex && !vtex->isBrxml())
	{
/*		double max_res_scale = 1.0;
		switch(m_res_mode)
		{
		case 1:
			max_res_scale = 1;
			break;
		case 2:
			max_res_scale = 1.5;
			break;
		case 3:
			max_res_scale = 2.0;
			break;
		case 4:
			max_res_scale = 3.0;
			break;
		default:
			max_res_scale = 1.0;
		}

		if (max_res_scale > 1.0)
		{
			vector<double> sfs;
			vector<double> spx, spy, spz;

			double aspect = (double)nx / (double)ny;

			double spc_x;
			double spc_y;
			double spc_z;
			vtex->get_spacings(spc_x, spc_y, spc_z);
			spc_x = spc_x<EPS?1.0:spc_x;
			spc_y = spc_y<EPS?1.0:spc_y;

			spx.push_back(spc_x);
			spy.push_back(spc_y);
			spz.push_back(spc_z);

			double sf;
			if (aspect > 1.0)
				sf = 2.0*m_radius/spc_y/double(ny);
			else
				sf = 2.0*m_radius/spc_x/double(nx);

			double vxsize_on_screen = m_scale_factor / sf;

			if (vxsize_on_screen > 1.5)
			{
				double fac = 0.0;
				switch(m_res_mode)
				{
				case 1:
					fac = 0.0;
					break;
				case 2:
					fac = 0.25;
					break;
				case 3:
					fac = 0.5;
					break;
				case 4:
					fac = 1.0;
					break;
				default:
					fac = 0.0;
				}
				double bscale = (vxsize_on_screen - 1.5)*fac + 1.0;
				vd->GetVR()->set_buffer_scale(1.0/bscale);
			}
			else
				vd->GetVR()->set_buffer_scale(1.0);
		}
		else*/
			vd->GetVR()->set_buffer_scale(1.0);
	}
}

void VRenderVulkanView::DrawOVER(VolumeData* vd, std::unique_ptr<vks::VFrameBuffer>& fb, int peel, double sampling_frq_fac)
{
	ShaderProgram* img_shader = 0;

	bool do_over = true;

	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
	if (TextureRenderer::get_mem_swap() && m_fbo_temp && (m_fbo_temp->w != m_nx || m_fbo_temp->h != m_ny))
	{
		m_fbo_temp.reset();
		m_tex_temp.reset();
		m_fbo_temp_restore.reset();
	}
	if (TextureRenderer::get_mem_swap() && !m_fbo_temp)
	{
		m_fbo_temp = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_temp->w = m_nx;
		m_fbo_temp->h = m_ny;
		m_fbo_temp->device = prim_dev;

		m_tex_temp = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
		m_fbo_temp->addAttachment(m_tex_temp);

		m_fbo_temp_restore = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_temp_restore->w = m_nx;
		m_fbo_temp_restore->h = m_ny;
		m_fbo_temp_restore->device = prim_dev;
		m_fbo_temp_restore->addAttachment(m_fbo_final->attachments[0]);
	}

	if (TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_start_update_loop() &&
		!TextureRenderer::get_done_update_loop())
	{
		if (vd->GetVR()->get_done_loop(0) && 
			( !vd->isActiveMask()  || vd->GetVR()->get_done_loop(TEXTURE_RENDER_MODE_MASK)  ) &&
			( !vd->isActiveLabel() || vd->GetVR()->get_done_loop(TEXTURE_RENDER_MODE_LABEL) )   )
			do_over = false;

		if (!do_over && !(vd->GetShadow() || !vd->GetVR()->get_done_loop(3)))
			return;

		if (do_over)
		{
			//before rendering this channel, save m_fbo_final to m_fbo_temp
			if (TextureRenderer::get_mem_swap() &&
				TextureRenderer::get_start_update_loop() &&
				TextureRenderer::get_save_final_buffer())
			{
				TextureRenderer::reset_save_final_buffer();
				TextureRenderer::validate_temp_final_buffer();

				Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

				params.clear = true;
				params.pipeline =
					m_v2drender->preparePipeline(
						IMG_SHADER_TEXTURE_LOOKUP,
						V2DRENDER_BLEND_DISABLE,
						m_fbo_temp->attachments[0]->format,
						m_fbo_temp->attachments.size(),
						0,
						m_fbo_temp->attachments[0]->is_swapchain_images);
				params.tex[0] = m_tex_final.get();
				
				if (!m_fbo_temp->renderPass)
					m_fbo_temp->replaceRenderPass(params.pipeline.pass);
				m_v2drender->render(m_fbo_temp, params);
			}
		}

		if (TextureRenderer::get_streaming())
		{
			unsigned int rn_time = GET_TICK_COUNT();
			if (rn_time - TextureRenderer::get_st_time() >
				TextureRenderer::get_up_time()/* && vd->GetMaskMode() != 1*/)
				return;
		}
	}

	if (do_over)
	{
		bool clear = false;
		if (!TextureRenderer::get_mem_swap() ||
			(TextureRenderer::get_mem_swap() &&
			TextureRenderer::get_clear_chan_buffer()))
		{
			TextureRenderer::reset_clear_chan_buffer();
			clear = true;
		}

		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		Texture* ext_lbl = NULL;
		if (!vd->GetSharedLabelName().IsEmpty() && vr_frame && vr_frame->GetDataManager())
		{
			VolumeData* lbl = vr_frame->GetDataManager()->GetVolumeData(vd->GetSharedLabelName());
			if (lbl)
				ext_lbl = lbl->GetTexture();
		}
        Texture* ext_msk = NULL;
        if (!vd->GetSharedMaskName().IsEmpty() && vr_frame && vr_frame->GetDataManager())
        {
            VolumeData* msk = vr_frame->GetDataManager()->GetVolumeData(vd->GetSharedMaskName());
            if (msk)
                ext_msk = msk->GetTexture();
        }

		if (vd->GetVR())
			vd->GetVR()->set_depth_peel(peel);
		vd->SetStreamMode(0);
		vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
		vd->SetFog(m_use_fog, m_fog_intensity, m_fog_start, m_fog_end);
		VkClearColorValue clear_color = { 0.0, 0.0, 0.0, 0.0 };
		vd->Draw(fb, clear, !m_persp, m_interactive, m_scale_factor, sampling_frq_fac, clear_color, ext_msk, ext_lbl);
	}

	if (vd->GetShadow() && vd->GetVR()->get_done_loop(0))
	{
		vector<VolumeData*> list;
		list.push_back(vd);
		DrawOLShadows(list, fb);
	}

	//bind fbo for final composition
	if (TextureRenderer::get_mem_swap() && m_fbo_temp && m_tex_temp && TextureRenderer::isvalid_temp_final_buffer())
	{
		//restore m_fbo_temp to m_fbo_final
		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

		params.clear = m_clear_final_buffer;
		m_clear_final_buffer = false;

		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHADER_TEXTURE_LOOKUP,
				V2DRENDER_BLEND_DISABLE,
				m_fbo_temp_restore->attachments[0]->format,
				m_fbo_temp_restore->attachments.size(),
				0,
				m_fbo_temp_restore->attachments[0]->is_swapchain_images);
		params.tex[0] = m_tex_temp.get();
		
		if (!m_fbo_temp_restore->renderPass)
			m_fbo_temp_restore->replaceRenderPass(params.pipeline.pass);
		m_v2drender->render(m_fbo_temp_restore, params);
	}
	
	Vulkan2dRender::V2DRenderParams params_final = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	
    if (m_easy_2d_adjust)
    {
        params_final.pipeline =
        m_v2drender->preparePipeline(
                                     IMG_SHDR_LEVELS,
                                     m_vol_method == VOL_METHOD_COMP ? V2DRENDER_BLEND_ADD : V2DRENDER_BLEND_OVER,
                                     m_fbo_final->attachments[0]->format,
                                     m_fbo_final->attachments.size(),
                                     0,
                                     m_fbo_final->attachments[0]->is_swapchain_images);
        params_final.tex[0] = fb->attachments[0].get();
        Color gamma = vd->GetGamma();
        Color levels = vd->GetLevels();
        params_final.loc[0] = glm::vec4((float)gamma.r(), (float)gamma.g(), (float)gamma.b(), 1.0f);
        params_final.loc[1] = glm::vec4((float)levels.r(), (float)levels.g(), (float)levels.b(), 1.0f);
    }
    else
    {
        params_final.pipeline =
        m_v2drender->preparePipeline(
                                     IMG_SHDR_BRIGHTNESS_CONTRAST_HDR,
                                     m_vol_method == VOL_METHOD_COMP ? V2DRENDER_BLEND_ADD : V2DRENDER_BLEND_OVER,
                                     m_fbo_final->attachments[0]->format,
                                     m_fbo_final->attachments.size(),
                                     0,
                                     m_fbo_final->attachments[0]->is_swapchain_images);
        params_final.tex[0] = fb->attachments[0].get();
        Color gamma = vd->GetGamma();
        Color brightness = vd->GetBrightness();
        Color hdr = vd->GetHdr();
        params_final.loc[0] = glm::vec4((float)gamma.r(), (float)gamma.g(), (float)gamma.b(), 1.0f);
        params_final.loc[1] = glm::vec4((float)brightness.r(), (float)brightness.g(), (float)brightness.b(), 1.0f);
        params_final.loc[2] = glm::vec4((float)hdr.r(), (float)hdr.g(), (float)hdr.b(), 0.0f);
    }
	params_final.clear = m_clear_final_buffer;
	m_clear_final_buffer = false;

	if (!m_fbo_final->renderPass)
		m_fbo_final->replaceRenderPass(params_final.pipeline.pass);
	m_v2drender->render(m_fbo_final, params_final);
}

void VRenderVulkanView::DrawMIP(VolumeData* vd, std::unique_ptr<vks::VFrameBuffer>& fb, int peel, double sampling_frq_fac)
{
	bool do_mip = true;

	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
	if (TextureRenderer::get_mem_swap() && m_fbo_temp && (m_fbo_temp->w != m_nx || m_fbo_temp->h != m_ny))
	{
		m_fbo_temp.reset();
		m_tex_temp.reset();
	}
	if (TextureRenderer::get_mem_swap() && !m_fbo_temp)
	{
		m_fbo_temp = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_temp->w = m_nx;
		m_fbo_temp->h = m_ny;
		m_fbo_temp->device = prim_dev;

		m_tex_temp = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
		m_fbo_temp->addAttachment(m_tex_temp);
	}

	ShaderProgram* img_shader = 0;
	if (TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_start_update_loop() &&
		!TextureRenderer::get_done_update_loop())
	{
		if (vd->GetVR()->get_done_loop(1) && 
			( !vd->isActiveMask()  || vd->GetVR()->get_done_loop(TEXTURE_RENDER_MODE_MASK)  ) &&
			( !vd->isActiveLabel() || vd->GetVR()->get_done_loop(TEXTURE_RENDER_MODE_LABEL) )   )
			do_mip = false;

		if (!do_mip && !(vd->GetShadow() || !vd->GetVR()->get_done_loop(3)) && !(vd->GetShading() || !vd->GetVR()->get_done_loop(2)))
			return;

		if (do_mip)
		{
			//before rendering this channel, save m_fbo_final to m_fbo_temp
			if (TextureRenderer::get_mem_swap() &&
				TextureRenderer::get_start_update_loop() &&
				TextureRenderer::get_save_final_buffer())
			{
				TextureRenderer::reset_save_final_buffer();
				TextureRenderer::validate_temp_final_buffer();

				Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

				params.clear = true;
				params.pipeline =
					m_v2drender->preparePipeline(
						IMG_SHADER_TEXTURE_LOOKUP,
						V2DRENDER_BLEND_DISABLE,
						m_fbo_temp->attachments[0]->format,
						m_fbo_temp->attachments.size(),
						0,
						m_fbo_temp->attachments[0]->is_swapchain_images);
				params.tex[0] = m_tex_final.get();

				m_fbo_temp->replaceRenderPass(params.pipeline.pass);
				m_v2drender->render(m_fbo_temp, params);
			}
		}

		if (TextureRenderer::get_streaming())
		{
			unsigned int rn_time = GET_TICK_COUNT();
			if (rn_time - TextureRenderer::get_st_time() >
				TextureRenderer::get_up_time())
				return;
		}
	}

	int nx, ny;
	nx = m_nx;
	ny = m_ny;

	bool shading = vd->GetVR()->get_shading();
	bool shadow = vd->GetShadow();
	int color_mode = vd->GetColormapMode();
	bool enable_alpha = vd->GetEnableAlpha();

	if (do_mip)
	{
		if (m_fbo_ol1 && (m_fbo_ol1->w != nx || m_fbo_ol1->h != ny))
		{
			m_fbo_ol1.reset();
			m_tex_ol1.reset();
		}
		if (!m_fbo_ol1)
		{
			m_fbo_ol1 = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
			m_fbo_ol1->w = m_nx;
			m_fbo_ol1->h = m_ny;
			m_fbo_ol1->device = prim_dev;

			m_tex_ol1 = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
			m_fbo_ol1->addAttachment(m_tex_ol1);
		}
		
		//bind the fbo
		bool clear = false;
		if (!TextureRenderer::get_mem_swap() ||
			(TextureRenderer::get_mem_swap() &&
			TextureRenderer::get_clear_chan_buffer()))
		{
			TextureRenderer::reset_clear_chan_buffer();
			clear = true;
		}

		if (vd->GetVR())
			vd->GetVR()->set_depth_peel(peel);
		if (color_mode == 1)
		{
			vd->SetMode(3);
			vd->SetFog(false, m_fog_intensity, m_fog_start, m_fog_end);
		}
		else
		{
			vd->SetMode(1);
			vd->SetFog(m_use_fog, m_fog_intensity, m_fog_start, m_fog_end);
		}
		//turn off alpha
		//if (color_mode == 1)
		//	vd->SetEnableAlpha(false);

		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		Texture* ext_lbl = NULL;
		if (!vd->GetSharedLabelName().IsEmpty() && vr_frame && vr_frame->GetDataManager())
		{
			VolumeData* lbl = vr_frame->GetDataManager()->GetVolumeData(vd->GetSharedLabelName());
			if (lbl)
				ext_lbl = lbl->GetTexture();
		}
        Texture* ext_msk = NULL;
        if (!vd->GetSharedMaskName().IsEmpty() && vr_frame && vr_frame->GetDataManager())
        {
            VolumeData* msk = vr_frame->GetDataManager()->GetVolumeData(vd->GetSharedMaskName());
            if (msk)
                ext_msk = msk->GetTexture();
        }
        
		VkClearColorValue clear_color = { 0.0, 0.0, 0.0, 0.0 };
        
		//draw
		vd->SetStreamMode(1);
		vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
		vd->Draw(m_fbo_ol1, clear, !m_persp, m_interactive, m_scale_factor, sampling_frq_fac, clear_color, ext_msk, ext_lbl);
		//
		if (color_mode == 1)
		{
			vd->RestoreMode();
			//restore alpha
			vd->SetEnableAlpha(enable_alpha);
		}

		//bind fbo for final composition
		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		params.clear = true;

		if (color_mode == 1)
		{
			//2d adjustment
			params.pipeline =
				m_v2drender->preparePipeline(
					IMG_SHDR_GRADIENT_MAP,
					V2DRENDER_BLEND_OVER,
					fb->attachments[0]->format,
					fb->attachments.size(),
					vd->GetColormap(),
					fb->attachments[0]->is_swapchain_images);
			double lo, hi;
			vd->GetColormapValues(lo, hi);
			params.loc[0] = glm::vec4((float)lo, (float)hi, (float)(hi - lo), enable_alpha ? 0.0f : 1.0f);
		}
		else
		{
			params.pipeline =
				m_v2drender->preparePipeline(
					IMG_SHADER_TEXTURE_LOOKUP,
					V2DRENDER_BLEND_OVER,
					fb->attachments[0]->format,
					fb->attachments.size(),
					0,
					fb->attachments[0]->is_swapchain_images);
		}
		params.tex[0] = m_tex_ol1.get();
		if (!fb->renderPass)
			fb->replaceRenderPass(params.pipeline.pass);
		m_v2drender->render(fb, params);

	}

	if (shadow && vd->GetVR()->get_done_loop(1))
	{
		vector<VolumeData*> list;
		list.push_back(vd);
		DrawOLShadows(list, fb);
	}

	//bind fbo for final composition
	if (TextureRenderer::get_mem_swap() && m_fbo_temp && m_tex_temp && TextureRenderer::isvalid_temp_final_buffer())
	{
		//restore m_fbo_temp to m_fbo_final
		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

		params.clear = m_clear_final_buffer;
		m_clear_final_buffer = false;

		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHADER_TEXTURE_LOOKUP,
				V2DRENDER_BLEND_DISABLE,
				m_fbo_final->attachments[0]->format,
				m_fbo_final->attachments.size(),
				0,
				m_fbo_final->attachments[0]->is_swapchain_images);
		params.tex[0] = m_tex_temp.get();
		
		if (!m_fbo_final->renderPass)
			m_fbo_final->replaceRenderPass(params.pipeline.pass);
		m_v2drender->render(m_fbo_final, params);
	}

	Vulkan2dRender::V2DRenderParams params_final = m_v2drender->GetNextV2dRenderSemaphoreSettings();

    if (m_easy_2d_adjust)
    {
        params_final.pipeline =
        m_v2drender->preparePipeline(
                                     IMG_SHDR_LEVELS,
                                     m_vol_method == VOL_METHOD_COMP ? V2DRENDER_BLEND_ADD : V2DRENDER_BLEND_OVER,
                                     m_fbo_final->attachments[0]->format,
                                     m_fbo_final->attachments.size(),
                                     0,
                                     m_fbo_final->attachments[0]->is_swapchain_images);
        params_final.tex[0] = fb->attachments[0].get();
        Color gamma = vd->GetGamma();
        Color levels = vd->GetLevels();
        params_final.loc[0] = glm::vec4((float)gamma.r(), (float)gamma.g(), (float)gamma.b(), 1.0f);
        params_final.loc[1] = glm::vec4((float)levels.r(), (float)levels.g(), (float)levels.b(), 1.0f);
    }
    else
    {
        params_final.pipeline =
        m_v2drender->preparePipeline(
                                     IMG_SHDR_BRIGHTNESS_CONTRAST_HDR,
                                     m_vol_method == VOL_METHOD_COMP ? V2DRENDER_BLEND_ADD : V2DRENDER_BLEND_OVER,
                                     m_fbo_final->attachments[0]->format,
                                     m_fbo_final->attachments.size(),
                                     0,
                                     m_fbo_final->attachments[0]->is_swapchain_images);
        params_final.tex[0] = fb->attachments[0].get();
        Color gamma = vd->GetGamma();
        Color brightness = vd->GetBrightness();
        Color hdr = vd->GetHdr();
        params_final.loc[0] = glm::vec4((float)gamma.r(), (float)gamma.g(), (float)gamma.b(), 1.0f);
        params_final.loc[1] = glm::vec4((float)brightness.r(), (float)brightness.g(), (float)brightness.b(), 1.0f);
        params_final.loc[2] = glm::vec4((float)hdr.r(), (float)hdr.g(), (float)hdr.b(), 0.0f);
    }
	params_final.clear = m_clear_final_buffer;
	m_clear_final_buffer = false;

	if (!m_fbo_final->renderPass)
		m_fbo_final->replaceRenderPass(params_final.pipeline.pass);
	m_v2drender->render(m_fbo_final, params_final);

	vd->SetColormapMode(color_mode);
}

void VRenderVulkanView::DrawOLShading(VolumeData* vd, std::unique_ptr<vks::VFrameBuffer>& fb)
{
	if (TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_start_update_loop() &&
		!TextureRenderer::get_done_update_loop())
	{
		if (TextureRenderer::get_done_update_loop())
			return;

		if (vd->GetVR()->get_done_loop(2))
			return;

		if (TextureRenderer::get_save_final_buffer())
			TextureRenderer::reset_save_final_buffer();
		if (TextureRenderer::get_clear_chan_buffer())
			TextureRenderer::reset_clear_chan_buffer();
	}

	//shading pass
	vd->GetVR()->set_shading(true);
	vd->SetMode(2);
	int colormode = vd->GetColormapMode();
	vd->SetStreamMode(2);
	vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
	vd->SetFog(m_use_fog, m_fog_intensity, m_fog_start, m_fog_end);
	double sampling_frq_fac = 2 / min(m_ortho_right-m_ortho_left, m_ortho_top-m_ortho_bottom);
	VkClearColorValue clear_color = { 1.0, 1.0, 1.0, 1.0 };
	vd->Draw(m_fbo_ol1, true, !m_persp, m_interactive, m_scale_factor, sampling_frq_fac, clear_color);
	vd->RestoreMode();
	vd->SetColormapMode(colormode);

	//bind fbo for final composition
	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

	params.clear = true;
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHADER_TEXTURE_LOOKUP,
			V2DRENDER_BLEND_SHADE_SHADOW,
			fb->attachments[0]->format,
			fb->attachments.size(),
			0,
			fb->attachments[0]->is_swapchain_images);
	params.tex[0] = m_tex_ol1.get();

	if (!fb->renderPass)
		fb->replaceRenderPass(params.pipeline.pass);
	m_v2drender->render(fb, params);
}

//get mesh shadow
bool VRenderVulkanView::GetMeshShadow(double &val)
{
	for (int i=0 ; i<(int)m_layer_list.size() ; i++)
	{
		if (!m_layer_list[i])
			continue;
		if (m_layer_list[i]->IsA() == 3)
		{
			MeshData* md = (MeshData*)m_layer_list[i];
			if (md && md->GetDisp())
			{
				md->GetShadowParams(val);
				return md->GetShadow();
			}
		}
		else if (m_layer_list[i]->IsA() == 6)
		{
			MeshGroup* group = (MeshGroup*)m_layer_list[i];
			if (group && group->GetDisp())
			{
				for (int j=0; j<(int)group->GetMeshNum(); j++)
				{
					MeshData* md = group->GetMeshData(j);
					if (md && md->GetDisp())
					{
						md->GetShadowParams(val);
						return md->GetShadow();
					}
				}
			}
		}
	}
	val = 0.0;
	return false;
}

void VRenderVulkanView::DrawOLShadowsMesh(const std::shared_ptr<vks::VTexture>& tex_depth, double darkness)
{
	int nx, ny;
	nx = m_nx;
	ny = m_ny;

	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;

	//generate gradient map from depth map
	if (m_fbo_ol2 && (m_fbo_ol2->w != nx || m_fbo_ol2->h != ny))
	{
		m_fbo_ol2.reset();
		m_tex_ol2.reset();
	}
	if (!m_fbo_ol2)
	{
		m_fbo_ol2 = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_ol2->w = m_nx;
		m_fbo_ol2->h = m_ny;
		m_fbo_ol2->device = prim_dev;

		m_tex_ol2 = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
		m_fbo_ol2->addAttachment(m_tex_ol2);
	}
	
	Vulkan2dRender::V2DRenderParams params_ol2 = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	params_ol2.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_DEPTH_TO_GRADIENT,
			V2DRENDER_BLEND_DISABLE,
			m_fbo_ol2->attachments[0]->format,
			m_fbo_ol2->attachments.size(),
			0,
			m_fbo_ol2->attachments[0]->is_swapchain_images);
	params_ol2.tex[0] = tex_depth.get();
	
	double dir_x = 0.0, dir_y = 0.0;
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame && vr_frame->GetSettingDlg())
		vr_frame->GetSettingDlg()->GetShadowDir(dir_x, dir_y);

	params_ol2.loc[0] = { 1.0f/nx, 1.0f/ny, m_persp ? 2e10f : 1e6f, 0.0f };
	params_ol2.loc[1] = { (float)dir_x, (float)dir_y, 0.0f, 0.0f };

	params_ol2.clear = true;
	params_ol2.clearColor = { 1.0, 1.0, 1.0, 1.0 };
	
	if (!m_fbo_ol2->renderPass)
		m_fbo_ol2->replaceRenderPass(params_ol2.pipeline.pass);
	m_v2drender->render(m_fbo_ol2, params_ol2);
	
	//draw shadow
	Vulkan2dRender::V2DRenderParams params_final;
	params_final.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_GRADIENT_TO_SHADOW_MESH,
			V2DRENDER_BLEND_SHADE_SHADOW,
			m_fbo_final->attachments[0]->format,
			m_fbo_final->attachments.size(),
			0,
			m_fbo_final->attachments[0]->is_swapchain_images);
	params_final.tex[0] = m_tex_ol2.get();
	params_final.tex[1] = tex_depth.get();
	params_final.tex[2] = m_tex_final.get();	//TODO: FIX (copy texture?) This causes validation errors (image layout mismatch)
	params_final.loc[0] = { 1.0f / nx, 1.0f / ny, max(m_scale_factor, 1.0), 0.0f };
	params_final.loc[1] = { (float)darkness, 0.0f, 0.0f, 0.0f };
	params_final.clear = m_clear_final_buffer;
	m_clear_final_buffer = false;

	if (!m_fbo_final->renderPass)
		m_fbo_final->replaceRenderPass(params_final.pipeline.pass);
	m_v2drender->render(m_fbo_final, params_final);

	//bool drawshadow = false;
	//for (int i = 0; i < (int)m_md_pop_list.size(); i++)
	//{
	//	MeshData* md = m_md_pop_list[i];
	//	if (md && md->GetShadow())
	//	{
	//		drawshadow = true;
	//		break;
	//	}
	//}
	//if (!drawshadow)
	//	return;

	//int nx, ny;
	//nx = m_nx;
	//ny = m_ny;

	//vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;

	////write ID and depth map
	//if (m_fbo_pick && (m_fbo_pick->w != nx || m_fbo_pick->h != ny))
	//{
	//	m_fbo_pick.reset();
	//	m_tex_pick.reset();
	//	m_tex_pick_depth.reset();
	//}
	//if (!m_fbo_pick)
	//{
	//	m_fbo_pick = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
	//	m_fbo_pick->w = m_nx;
	//	m_fbo_pick->h = m_ny;
	//	m_fbo_pick->device = prim_dev;

	//	m_tex_pick = prim_dev->GenTexture2D(VK_FORMAT_R32_UINT, VK_FILTER_NEAREST, m_nx, m_ny);
	//	m_fbo_pick->addAttachment(m_tex_pick);

	//	m_tex_pick_depth = prim_dev->GenTexture2D(
	//		m_vulkan->depthFormat,
	//		VK_FILTER_NEAREST,
	//		m_nx, m_ny,
	//		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
	//		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
	//		VK_IMAGE_USAGE_SAMPLED_BIT |
	//		VK_IMAGE_USAGE_TRANSFER_SRC_BIT
	//	);
	//	m_fbo_pick->addAttachment(m_tex_pick_depth);
	//}

	//VkRect2D scissor = { 0, 0, m_nx, m_ny };
	//bool clear = true;
	//for (int i = 0; i < (int)m_md_pop_list.size(); i++)
	//{
	//	MeshData* md = m_md_pop_list[i];
	//	if (md)
	//	{
	//		md->SetMatrices(m_mv_mat, m_proj_mat);
	//		md->DrawInt(i + 1, m_fbo_pick, clear);
	//		clear = false;
	//	}
	//}

	////generate gradient map from depth map
	//if (m_fbo_ol2 && (m_fbo_ol2->w != nx || m_fbo_ol2->h != ny))
	//{
	//	m_fbo_ol2.reset();
	//	m_tex_ol2.reset();
	//}
	//if (!m_fbo_ol2)
	//{
	//	m_fbo_ol2 = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
	//	m_fbo_ol2->w = m_nx;
	//	m_fbo_ol2->h = m_ny;
	//	m_fbo_ol2->device = prim_dev;

	//	m_tex_ol2 = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
	//	m_fbo_ol2->addAttachment(m_tex_ol2);
	//}
	//
	//Vulkan2dRender::V2DRenderParams params_ol2 = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	//params_ol2.pipeline =
	//	m_v2drender->preparePipeline(
	//		IMG_SHDR_DEPTH_TO_GRADIENT,
	//		V2DRENDER_BLEND_DISABLE,
	//		m_fbo_ol2->attachments[0]->format,
	//		m_fbo_ol2->attachments.size(),
	//		0,
	//		m_fbo_ol2->attachments[0]->is_swapchain_images);
	//params_ol2.tex[0] = m_tex_pick_depth.get();
	//
	//double dir_x = 0.0, dir_y = 0.0;
	//VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	//if (vr_frame && vr_frame->GetSettingDlg())
	//	vr_frame->GetSettingDlg()->GetShadowDir(dir_x, dir_y);

	//params_ol2.loc[0] = { 1.0f/nx, 1.0f/ny, m_persp ? 2e10f : 1e6f, 0.0f };
	//params_ol2.loc[1] = { (float)dir_x, (float)dir_y, 0.0f, 0.0f };

	//params_ol2.clear = true;
	//params_ol2.clearColor = { 1.0, 1.0, 1.0, 1.0 };

	//m_fbo_ol2->replaceRenderPass(params_ol2.pipeline.pass);
	//m_v2drender->render(m_fbo_ol2, params_ol2);
	//
	////draw shadow
	//std::vector<Vulkan2dRender::V2DRenderParams> v2drender_params;
	//Vulkan2dRender::V2dPipeline pl =
	//	m_v2drender->preparePipeline(
	//		IMG_SHDR_GRADIENT_TO_SHADOW_MESH,
	//		V2DRENDER_BLEND_SHADE_SHADOW,
	//		m_fbo_final->attachments[0]->format,
	//		m_fbo_final->attachments.size(),
	//		0,
	//		m_fbo_final->attachments[0]->is_swapchain_images);
	//for (int i = 0; i < (int)m_md_pop_list.size(); i++)
	//{
	//	MeshData* md = m_md_pop_list[i];
	//	if (md && md->GetShadow())
	//	{
	//		double darkness = 0.0;
	//		md->GetShadowParams(darkness);

	//		Vulkan2dRender::V2DRenderParams param;
	//		param.pipeline = pl;
	//		param.clear = false;
	//		param.tex[0] = m_tex_ol2.get();
	//		param.tex[1] = m_tex_pick.get();
	//		param.tex[2] = m_tex_final.get();
	//		param.loc[0] = { 1.0f / nx, 1.0f / ny, max(m_scale_factor, 1.0), 0.0f };
	//		param.loc[1] = { (float)darkness, (float)(i+1), 0.0f, 0.0f };
	//		v2drender_params.push_back(param);
	//	}
	//}
	//v2drender_params[0].clear = m_clear_final_buffer;
	//m_clear_final_buffer = false;
	//vks::VulkanSemaphoreSettings sem = prim_dev->GetNextRenderSemaphoreSettings();
	//v2drender_params[0].waitSemaphores = sem.waitSemaphores;
	//v2drender_params[0].waitSemaphoreCount = sem.waitSemaphoreCount;
	//v2drender_params[0].signalSemaphores = sem.signalSemaphores;
	//v2drender_params[0].signalSemaphoreCount = sem.signalSemaphoreCount;
	//m_fbo_final->replaceRenderPass(pl.pass);
	//m_v2drender->seq_render(m_fbo_final, v2drender_params.data(), v2drender_params.size());

}

void VRenderVulkanView::DrawOLShadows(vector<VolumeData*> &vlist, std::unique_ptr<vks::VFrameBuffer>& fb)
{
	if (vlist.empty())
		return;

	if (TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_start_update_loop())
	{
		if (TextureRenderer::get_done_update_loop())
			return;
		else
		{
			bool doShadow = false;
			for (int i = 0; i < vlist.size(); i++)
			{
				if (vlist[0]->GetVR())
					doShadow |= !(vlist[0]->GetVR()->get_done_loop(3));
			}
			if (!doShadow)
				return;
		}

		if (TextureRenderer::get_save_final_buffer())
			TextureRenderer::reset_save_final_buffer();
	}

	double sampling_frq_fac = 2 / min(m_ortho_right-m_ortho_left, m_ortho_top-m_ortho_bottom);

	int nx, ny;
	nx = m_nx;
	ny = m_ny;

	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
	
	//gradient pass
	if (m_fbo_ol1 && (m_fbo_ol1->w != nx || m_fbo_ol1->h != ny))
	{
		m_fbo_ol1.reset();
		m_tex_ol1.reset();
	}
	if (!m_fbo_ol1)
	{
		m_fbo_ol1 = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_ol1->w = m_nx;
		m_fbo_ol1->h = m_ny;
		m_fbo_ol1->device = prim_dev;

		m_tex_ol1 = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
		m_fbo_ol1->addAttachment(m_tex_ol1);
	}
	
	bool clear = false;
	if (!TextureRenderer::get_mem_swap() ||
		(TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_clear_chan_buffer()))
	{
		TextureRenderer::reset_clear_chan_buffer();
		clear = true;
	}

	double shadow_darkness = 0.0;
	VkClearColorValue clear_color = { 1.0, 1.0, 1.0, 1.0 };

	if (vlist.size() == 1 && vlist[0]->GetShadow())
	{
		VolumeData* vd = vlist[0];

		if (vd->GetTexture())
		{
			//save
			int colormode = vd->GetColormapMode();
			bool shading = vd->GetVR()->get_shading();
			//set to draw depth
			vd->GetVR()->set_shading(false);
			vd->SetMode(0);
			vd->SetColormapMode(2);
			vd->Set2dDmap(m_tex_ol1);
			//draw
			vd->SetStreamMode(3);
			int uporder = TextureRenderer::get_update_order();
			TextureRenderer::set_update_order(0);
			vd->GetTexture()->set_sort_bricks();
			vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
			vd->SetFog(m_use_fog, m_fog_intensity, m_fog_start, m_fog_end);
			vd->Draw(m_fbo_ol1, clear, !m_persp, m_interactive, m_scale_factor, sampling_frq_fac, clear_color);
			//restore
			vd->RestoreMode();
			vd->SetColormapMode(colormode);
			vd->GetVR()->set_shading(shading);
			vd->GetShadowParams(shadow_darkness);
			TextureRenderer::set_update_order(uporder);
			vd->GetTexture()->set_sort_bricks();
		}

	}
	else
	{
		vector<int> colormodes;
		vector<bool> shadings;
		vector<VolumeData*> list;

		int uporder = TextureRenderer::get_update_order();
		TextureRenderer::set_update_order(0);

		//geenerate list
		int i;
		for (i=0; i<(int)vlist.size(); i++)
		{
			VolumeData* vd = vlist[i];
			if (vd && vd->GetShadow() && vd->GetTexture())
			{
				colormodes.push_back(vd->GetColormapMode());
				shadings.push_back(vd->GetVR()->get_shading());
				list.push_back(vd);
			}
		}
		if (!list.empty())
		{
			m_mvr->clear_vr();
			m_mvr->set_main_membuf_size((long long)TextureRenderer::mainmem_buf_size_ * 1024LL * 1024LL);
			for (i=0; i<(int)list.size(); i++)
			{
				VolumeData* vd = list[i];
				vd->GetVR()->set_shading(false);
				vd->SetMode(0);
				vd->SetColormapMode(2);
				vd->Set2dDmap(m_tex_ol1);
				vd->GetTexture()->set_sort_bricks();
				VolumeRenderer* vr = list[i]->GetVR();
				if (vr)
				{
					list[i]->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
					list[i]->SetFog(m_use_fog, m_fog_intensity, m_fog_start, m_fog_end);
					m_mvr->add_vr(vr);
					m_mvr->set_sampling_rate(vr->get_sampling_rate());
					m_mvr->SetNoiseRed(vr->GetNoiseRed());
				}
			}
			m_mvr->set_colormap_mode(2);
			//draw
			m_mvr->draw(m_fbo_ol1, clear, m_interactive, !m_persp, m_scale_factor, m_intp, sampling_frq_fac, clear_color);
			//restore
			m_mvr->set_colormap_mode(0);
			for (i=0; i<(int)list.size(); i++)
			{
				VolumeData* vd = list[i];
				vd->RestoreMode();
				vd->SetColormapMode(colormodes[i]);
				vd->GetVR()->set_shading(shadings[i]);
				vd->GetTexture()->set_sort_bricks();
			}
			list[0]->GetShadowParams(shadow_darkness);
		}

		TextureRenderer::set_update_order(uporder);
	}

	//
	if (!TextureRenderer::get_mem_swap() ||
		(TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_clear_chan_buffer()))
	{
		if (m_fbo_ol2 && (m_fbo_ol2->w != nx || m_fbo_ol2->h != ny))
		{
			m_fbo_ol2.reset();
			m_tex_ol2.reset();
		}
		if (!m_fbo_ol2)
		{
			m_fbo_ol2 = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
			m_fbo_ol2->w = m_nx;
			m_fbo_ol2->h = m_ny;
			m_fbo_ol2->device = prim_dev;

			m_tex_ol2 = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
			m_fbo_ol2->addAttachment(m_tex_ol2);
		}

		//bind the fbo
		Vulkan2dRender::V2DRenderParams params_ol2 = m_v2drender->GetNextV2dRenderSemaphoreSettings();

		params_ol2.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHDR_DEPTH_TO_GRADIENT,
				V2DRENDER_BLEND_DISABLE,
				m_fbo_ol2->attachments[0]->format,
				m_fbo_ol2->attachments.size(),
				0,
				m_fbo_ol2->attachments[0]->is_swapchain_images);
		params_ol2.tex[0] = m_tex_ol1.get();

		double dir_x = 0.0, dir_y = 0.0;
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame && vr_frame->GetSettingDlg())
			vr_frame->GetSettingDlg()->GetShadowDir(dir_x, dir_y);

		params_ol2.loc[0] = { 1.0f / nx, 1.0f / ny, m_persp ? 2e10f : 1e6f, 0.0f };
		params_ol2.loc[1] = { (float)dir_x, (float)dir_y, 0.0f, 0.0f };

		params_ol2.clear = true;
		params_ol2.clearColor = { 1.0f, 1.0f, 1.0f, 1.0f };

		m_fbo_ol2->replaceRenderPass(params_ol2.pipeline.pass);
		m_v2drender->render(m_fbo_ol2, params_ol2);

		//bind fbo for final composition
		Vulkan2dRender::V2DRenderParams params_final = m_v2drender->GetNextV2dRenderSemaphoreSettings();

		params_final.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHDR_GRADIENT_TO_SHADOW,
				V2DRENDER_BLEND_SHADE_SHADOW,
				fb->attachments[0]->format,
				fb->attachments.size(),
				0,
				fb->attachments[0]->is_swapchain_images);
		params_final.tex[0] = m_tex_ol2.get();
		params_final.tex[1] = fb->attachments[0].get();	//TODO: FIX (copy texture?) This causes validation errors (image layout mismatch)
		params_final.loc[0] = { 1.0f / nx, 1.0f / ny, max(m_scale_factor, 1.0), 0.0f };
		params_final.loc[1] = { (float)shadow_darkness, 0.0f, 0.0f, 0.0f };

		if (!fb->renderPass)
			fb->replaceRenderPass(params_final.pipeline.pass);
		m_v2drender->render(fb, params_final);

/*
		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		params.clear = true;
		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHADER_TEXTURE_LOOKUP,
				V2DRENDER_BLEND_DISABLE,
				fb->attachments[0]->format,
				fb->attachments.size(),
				0,
				fb->attachments[0]->is_swapchain_images);
		params.tex[0] = m_tex_ol2.get();
		if (!fb->renderPass)
			fb->replaceRenderPass(params.pipeline.pass);
		m_v2drender->render(fb, params);*/
	}
}

//draw multi volumes with depth consideration
//peel==true -- depth peeling
void VRenderVulkanView::DrawVolumesMulti(vector<VolumeData*> &list, int peel)
{
	if (list.empty())
		return;

	if (!m_mvr)
		m_mvr = new MultiVolumeRenderer();
	if (!m_mvr)
		return;

	ShaderProgram* img_shader;

	m_mvr->set_blend_slices(m_blend_slices);

	int i;
	bool use_tex_wt2 = false;
	bool shadow = false;
	bool doMulti = false;
	bool doShadow = false;
	m_mvr->clear_vr();
	VolumeRenderer* vrfirst = 0;
	for (i=0; i<(int)list.size(); i++)
	{
		if (list[i] && list[i]->GetDisp())
		{

			//switchLevel(list[i]);
			shadow |= list[i]->GetShadow();

			VolumeRenderer* vr = list[i]->GetVR();
			if (vr)
			{
				list[i]->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
				list[i]->SetFog(m_use_fog, m_fog_intensity, m_fog_start, m_fog_end);
				m_mvr->add_vr(vr);
				if (!vrfirst) vrfirst = vr;
				m_mvr->set_sampling_rate(vr->get_sampling_rate());
				m_mvr->SetNoiseRed(vr->GetNoiseRed());
				
				if (TextureRenderer::get_mem_swap() &&
					TextureRenderer::get_start_update_loop() &&
					!TextureRenderer::get_done_update_loop())
				{
					doMulti |= !vr->get_done_loop(0);
					doShadow |= !vr->get_done_loop(3);
				}
			}
			VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
			if (list[i]->GetTexture() &&
				list[i]->GetTexture()->nmask()!=-1 &&
				vr_frame &&
				list[i]==vr_frame->GetCurSelVol())
				use_tex_wt2 = true;
		}
	}

	if (!TextureRenderer::get_mem_swap())
	{
		doMulti = true;
		doShadow = true;
	}

	if (m_mvr->get_vr_num()<=0)
		return;

	int nx, ny;
	nx = m_nx;
	ny = m_ny;

	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
	if (TextureRenderer::get_mem_swap() && m_fbo_temp && (m_fbo_temp->w != m_nx || m_fbo_temp->h != m_ny))
	{
		m_fbo_temp.reset();
		m_tex_temp.reset();
		m_fbo_temp_restore.reset();
	}
	if (TextureRenderer::get_mem_swap() && !m_fbo_temp)
	{
		m_fbo_temp = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_temp->w = m_nx;
		m_fbo_temp->h = m_ny;
		m_fbo_temp->device = prim_dev;

		m_tex_temp = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
		m_fbo_temp->addAttachment(m_tex_temp);

		m_fbo_temp_restore = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_temp_restore->w = m_nx;
		m_fbo_temp_restore->h = m_ny;
		m_fbo_temp_restore->device = prim_dev;
		m_fbo_temp_restore->addAttachment(m_fbo_final->attachments[0]);
	}

	if (TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_start_update_loop() &&
		TextureRenderer::get_save_final_buffer())
	{
		TextureRenderer::reset_save_final_buffer();
		TextureRenderer::validate_temp_final_buffer();

		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

		params.clear = true;
		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHADER_TEXTURE_LOOKUP,
				V2DRENDER_BLEND_DISABLE,
				m_fbo_temp->attachments[0]->format,
				m_fbo_temp->attachments.size(),
				0,
				m_fbo_temp->attachments[0]->is_swapchain_images);
		params.tex[0] = m_tex_final.get();

		if (!m_fbo_temp->renderPass)
			m_fbo_temp->replaceRenderPass(params.pipeline.pass);
		m_v2drender->render(m_fbo_temp, params);
	}

	if (!doMulti && !(shadow && doShadow))
		return;

	m_mvr->set_depth_peel(peel);
	if (peel > 0)
	{
		m_mvr->set_front_depth_tex(m_vol_dp_fr_tex);
		m_mvr->set_back_depth_tex(m_vol_dp_bk_tex);
	}

	//generate textures & buffer objects
	//frame buffer for each volume
	if (m_fbo && (m_fbo->w != nx || m_fbo->h != ny))
	{
		m_fbo.reset();
		m_tex.reset();
	}
	if (use_tex_wt2 && m_fbo_wt2 && (m_fbo_wt2->w != nx || m_fbo_wt2->h != ny))
	{
		m_fbo_wt2.reset();
		m_tex_wt2.reset();
	}
	if (!m_fbo)
	{
		m_fbo = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo->w = m_nx;
		m_fbo->h = m_ny;
		m_fbo->device = prim_dev;

		m_tex = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
		m_fbo->addAttachment(m_tex);
	}
	if (use_tex_wt2 && !m_fbo_wt2)
	{
		m_fbo_wt2 = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_wt2->w = m_nx;
		m_fbo_wt2->h = m_ny;
		m_fbo_wt2->device = prim_dev;

		m_tex_wt2 = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, m_nx, m_ny);
		m_fbo_wt2->addAttachment(m_tex_wt2);
	}

	if (doMulti)
	{
		//bind the fbo
		bool clear = false;
		if (!TextureRenderer::get_mem_swap() ||
			(TextureRenderer::get_mem_swap() &&
			TextureRenderer::get_clear_chan_buffer()))
		{
			clear = true;
			TextureRenderer::reset_clear_chan_buffer();
		}

		//draw multiple volumes at the same time
		double sampling_frq_fac = 2 / min(m_ortho_right-m_ortho_left, m_ortho_top-m_ortho_bottom);
		m_mvr->set_main_membuf_size((long long)TextureRenderer::mainmem_buf_size_ * 1024LL * 1024LL);
		m_mvr->draw(use_tex_wt2 ? m_fbo_wt2 : m_fbo, clear, m_interactive, !m_persp, m_scale_factor, m_intp, sampling_frq_fac);
	}

	if (shadow && doShadow && vrfirst && vrfirst->get_done_loop(0))
	{
		//draw shadows
		DrawOLShadows(list, use_tex_wt2 ? m_fbo_wt2 : m_fbo);
	}

	//bind fbo for final composition
	if (TextureRenderer::get_mem_swap() && m_fbo_temp && m_tex_temp && TextureRenderer::isvalid_temp_final_buffer())
	{
		//restore m_fbo_temp to m_fbo_final
		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

		params.clear = m_clear_final_buffer;
		m_clear_final_buffer = false;

		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHADER_TEXTURE_LOOKUP,
				V2DRENDER_BLEND_DISABLE,
				m_fbo_temp_restore->attachments[0]->format,
				m_fbo_temp_restore->attachments.size(),
				0,
				m_fbo_temp_restore->attachments[0]->is_swapchain_images);
		params.tex[0] = m_tex_temp.get();

		if (!m_fbo_temp_restore->renderPass)
			m_fbo_temp_restore->replaceRenderPass(params.pipeline.pass);
		m_v2drender->render(m_fbo_temp_restore, params);
	}

	Vulkan2dRender::V2DRenderParams params_final = m_v2drender->GetNextV2dRenderSemaphoreSettings();

	int blend_method = V2DRENDER_BLEND_ADD;

	if (m_dpeel)
		blend_method = V2DRENDER_BLEND_OVER_INV;
	else
	{
		if (m_vol_method == VOL_METHOD_COMP)
			blend_method = V2DRENDER_BLEND_ADD;
		else if (m_vol_method == VOL_METHOD_SEQ)
			blend_method = V2DRENDER_BLEND_OVER;

	}
    
    if (m_easy_2d_adjust)
    {
        params_final.pipeline =
        m_v2drender->preparePipeline(
                                     IMG_SHDR_LEVELS,
                                     blend_method,
                                     m_fbo_final->attachments[0]->format,
                                     m_fbo_final->attachments.size(),
                                     0,
                                     m_fbo_final->attachments[0]->is_swapchain_images);
        params_final.tex[0] = use_tex_wt2 ? m_fbo_wt2->attachments[0].get() : m_fbo->attachments[0].get();
        Color gamma, levels;
        if (m_vol_method == VOL_METHOD_MULTI)
        {
            gamma = m_gamma;
            levels = m_levels;
        }
        else
        {
            VolumeData* vd = list[0];
            gamma = vd->GetGamma();
            levels = vd->GetLevels();
        }
        params_final.loc[0] = glm::vec4((float)gamma.r(), (float)gamma.g(), (float)gamma.b(), 1.0f);
        params_final.loc[1] = glm::vec4((float)levels.r(), (float)levels.g(), (float)levels.b(), 1.0f);
    }
    else
    {
        params_final.pipeline =
        m_v2drender->preparePipeline(
                                     IMG_SHDR_BRIGHTNESS_CONTRAST_HDR,
                                     blend_method,
                                     m_fbo_final->attachments[0]->format,
                                     m_fbo_final->attachments.size(),
                                     0,
                                     m_fbo_final->attachments[0]->is_swapchain_images);
        params_final.tex[0] = use_tex_wt2 ? m_fbo_wt2->attachments[0].get() : m_fbo->attachments[0].get();
        Color gamma, brightness, hdr;
        if (m_vol_method == VOL_METHOD_MULTI)
        {
            gamma = m_gamma;
            brightness = m_brightness;
            hdr = m_hdr;
        }
        else
        {
            VolumeData* vd = list[0];
            gamma = vd->GetGamma();
            brightness = vd->GetBrightness();
            hdr = vd->GetHdr();
        }
        params_final.loc[0] = glm::vec4((float)gamma.r(), (float)gamma.g(), (float)gamma.b(), 1.0f);
        params_final.loc[1] = glm::vec4((float)brightness.r(), (float)brightness.g(), (float)brightness.b(), 1.0f);
        params_final.loc[2] = glm::vec4((float)hdr.r(), (float)hdr.g(), (float)hdr.b(), 0.0f);
    }
    
	params_final.clear = m_clear_final_buffer;
	m_clear_final_buffer = false;

	if (!m_fbo_final->renderPass)
		m_fbo_final->replaceRenderPass(params_final.pipeline.pass);
	m_v2drender->render(m_fbo_final, params_final);
}

void VRenderVulkanView::SetBrush(int mode)
{
	m_prev_focus = FindFocus();
	SetFocus();
	bool autoid = false;
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame && vr_frame->GetTraceDlg())
		autoid = vr_frame->GetTraceDlg()->GetAutoID();

	if (m_int_mode == 5 ||
		m_int_mode == 7)
	{
		m_int_mode = 7;
		if (m_ruler_type == 3)
			m_selector.SetMode(8);
		else
			m_selector.SetMode(1);
	}
	else if (m_int_mode == 8)
	{
		if (m_ruler_type == 3)
			m_selector.SetMode(8);
		else
			m_selector.SetMode(1);
	}
	else
	{
		m_int_mode = 2;
		if (autoid && mode == 2)
			m_selector.SetMode(1);
		else
			m_selector.SetMode(mode);
	}
	m_paint_display = true;
	m_draw_brush = true;
}

void VRenderVulkanView::UpdateBrushState()
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	TreePanel* tree_panel = 0;
	BrushToolDlg* brush_dlg = 0;
	if (vr_frame)
	{
		tree_panel = vr_frame->GetTree();
		brush_dlg = vr_frame->GetBrushToolDlg();
	}

	if(m_key_lock) return;

	if (m_int_mode!=2 && m_int_mode!=7)
	{
		if (VRenderFrame::GetKeyState(WXK_SHIFT))
		{
			SetBrush(2);
			if (tree_panel)
				tree_panel->SelectBrush(TreePanel::ID_BrushAppend);
			if (brush_dlg)
				brush_dlg->SelectBrush(BrushToolDlg::ID_BrushAppend);
			m_clear_paint = true;
			RefreshGLOverlays();
		}
		else if (VRenderFrame::GetKeyState(wxKeyCode('Z')))
		{
			SetBrush(4);
			if (tree_panel)
				tree_panel->SelectBrush(TreePanel::ID_BrushDiffuse);
			if (brush_dlg)
				brush_dlg->SelectBrush(BrushToolDlg::ID_BrushDiffuse);
			m_clear_paint = true;
			RefreshGLOverlays();
		}
		else if (VRenderFrame::GetKeyState(wxKeyCode('X')))
		{
			SetBrush(3);
			if (tree_panel)
				tree_panel->SelectBrush(TreePanel::ID_BrushDesel);
			if (brush_dlg)
				brush_dlg->SelectBrush(BrushToolDlg::ID_BrushDesel);
			m_clear_paint = true;
			RefreshGLOverlays();
		}
	}
	else
	{
		if (m_brush_state)
		{
			if (VRenderFrame::GetKeyState(WXK_SHIFT))
			{
				m_brush_state = 0;
				SetBrush(2);
				if (tree_panel)
					tree_panel->SelectBrush(TreePanel::ID_BrushAppend);
				if (brush_dlg)
					brush_dlg->SelectBrush(BrushToolDlg::ID_BrushAppend);
				m_clear_paint = true;
				RefreshGLOverlays();
			}
			else if (VRenderFrame::GetKeyState(wxKeyCode('Z')))
			{
				m_brush_state = 0;
				SetBrush(4);
				if (tree_panel)
					tree_panel->SelectBrush(TreePanel::ID_BrushDiffuse);
				if (brush_dlg)
					brush_dlg->SelectBrush(BrushToolDlg::ID_BrushDiffuse);
				m_clear_paint = true;
				RefreshGLOverlays();
			}
			else if (VRenderFrame::GetKeyState(wxKeyCode('X')))
			{
				m_brush_state = 0;
				SetBrush(3);
				if (tree_panel)
					tree_panel->SelectBrush(TreePanel::ID_BrushDesel);
				if (brush_dlg)
					brush_dlg->SelectBrush(BrushToolDlg::ID_BrushDesel);
				m_clear_paint = true;
				RefreshGLOverlays();
			}
			else
			{
				SetBrush(m_brush_state);
				RefreshGLOverlays();
			}
		}
		else if (!VRenderFrame::GetKeyState(WXK_SHIFT) &&
			!VRenderFrame::GetKeyState(wxKeyCode('Z')) &&
			!VRenderFrame::GetKeyState(wxKeyCode('X')))
		{
			m_clear_paint = true;

			if (m_int_mode == 7)
				m_int_mode = 5;
			else
				m_int_mode = 1;
			m_paint_display = false;
			m_draw_brush = false;
			if (tree_panel)
				tree_panel->SelectBrush(0);
			if (brush_dlg)
				brush_dlg->SelectBrush(0);

			if (m_paint_enable)
			{
                SetForceHideMask(false);
				Segment();
				RefreshGL();
			}
			else
				RefreshGLOverlays();

			if (m_prev_focus)
			{
				m_prev_focus->SetFocus();
				m_prev_focus = 0;
			}
		}
	}
}

//selection
void VRenderVulkanView::Pick()
{
	if (m_draw_all)
	{
		VRenderFrame* frame = (VRenderFrame*)m_frame;
		if (!frame) return;
		TreePanel *tree_panel = frame->GetTree();
		if (!tree_panel) return;

		//tree_panel->SetEvtHandlerEnabled(false);
		//tree_panel->Freeze();

		if (!SelSegVolume(3))
		{
			PickVolume();
		}
        PickMesh();

		//tree_panel->Thaw();
		//tree_panel->SetEvtHandlerEnabled(true);
	}
}

void VRenderVulkanView::PickMesh()
{
	int i;
	int nx = GetSize().x;
	int ny = GetSize().y;
	if (nx<=0 || ny<=0)
		return;
	wxPoint mouse_pos = ScreenToClient(wxGetMousePosition());
	if (mouse_pos.x<0 || mouse_pos.x>=nx ||
		mouse_pos.y<=0 || mouse_pos.y>ny)
		return;

	//projection
	HandleProjection(nx, ny);
	//Transformation
	HandleCamera();
	//obj
	glm::mat4 mv_temp = m_mv_mat;
	//translate object
	m_mv_mat = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
	//rotate object
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
	//center object
	m_mv_mat = glm::translate(m_mv_mat, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

	//set up fbo
	vks::VulkanDevice *prim_dev = m_vulkan->vulkanDevice;
	if (m_fbo_pick && (m_fbo_pick->w != nx || m_fbo_pick->h != ny))
	{
		m_fbo_pick.reset();
		m_tex_pick.reset();
		m_tex_pick_depth.reset();
	}
	if (!m_fbo_pick)
	{
		m_fbo_pick = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_pick->w = m_nx;
		m_fbo_pick->h = m_ny;
		m_fbo_pick->device = prim_dev;

		m_tex_pick = prim_dev->GenTexture2D(VK_FORMAT_R32_UINT, VK_FILTER_NEAREST, m_nx, m_ny);
		m_fbo_pick->addAttachment(m_tex_pick);

		m_tex_pick_depth = prim_dev->GenTexture2D(
			m_vulkan->depthFormat,
			VK_FILTER_NEAREST,
			m_nx, m_ny,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		);
		m_fbo_pick->addAttachment(m_tex_pick_depth);
	}
	
	//bind
	VkRect2D scissor = { mouse_pos.x, mouse_pos.y, 1, 1 };
	bool clear = true;
	for (i=0; i<(int)m_md_pop_list.size(); i++)
	{
		MeshData* md = m_md_pop_list[i];
		if (md)
		{
			md->SetMatrices(m_mv_mat, m_proj_mat);
			md->SetDevice(m_vulkan->devices[0]);
			md->DrawInt(i+1, m_fbo_pick, clear);
			clear = false;
		}
	}
	
	unsigned int choose = 0;
	VkOffset2D offset = { mouse_pos.x, mouse_pos.y };
	VkExtent2D extent = { 1, 1 };
	prim_dev->DownloadSubTexture2D(m_fbo_pick->attachments[0], &choose, offset, extent);
	
	if (choose >0 && choose<=(int)m_md_pop_list.size())
	{
		MeshData* md = m_md_pop_list[choose-1];
		if (md)
		{
            VRenderFrame* frame = (VRenderFrame*)m_frame;
            if (frame && frame->GetTree())
            {
                if (md->isTree())
                {
                    md->SetMatrices(m_mv_mat, m_proj_mat);
                    md->SetDevice(m_vulkan->devices[0]);
                    md->DrawInt(0, m_fbo_pick, true, VkRect2D{0,0,0,0}, true);
                    prim_dev->DownloadSubTexture2D(m_fbo_pick->attachments[0], &choose, offset, extent);
                    VRenderFrame* frame = (VRenderFrame*)m_frame;
                    frame->GetTree()->SetFocus();
                    frame->GetTree()->SelectROI(md, choose);
                }
                else
                {
                    VRenderFrame* frame = (VRenderFrame*)m_frame;
                    frame->GetTree()->SetFocus();
                    frame->GetTree()->Select(m_vrv->GetName(), md->GetName());
                }
            }
		}
	}
	else
	{
		VRenderFrame* frame = (VRenderFrame*)m_frame;
		if (frame && frame->GetCurSelType()==3 && frame->GetTree())
			frame->GetTree()->Select(m_vrv->GetName(), "");
	}
	m_mv_mat = mv_temp;
}

void VRenderVulkanView::PickVolume()
{
	double dist = 0.0;
	double min_dist = -1.0;
	Point p;
	VolumeData* vd = 0;
	VolumeData* picked_vd = 0;
	for (int i=0; i<(int)m_vd_pop_list.size(); i++)
	{
		vd = m_vd_pop_list[i];
		if (!vd || !vd->GetDisp()) continue;

		int cmode = vd->GetColormapMode();
		double sel_id;
		if (cmode == 3)
			dist = GetPointAndIntVolume(p, sel_id, false, old_mouse_X, old_mouse_Y, vd, 0.3);
		else 
			dist = GetPointVolume(p, old_mouse_X, old_mouse_Y, vd, 2, true, 0.3);

		if (dist > 0.0)
		{
			if (min_dist < 0.0)
			{
				min_dist = dist;
				picked_vd = vd;
			}
			else
			{
				if (m_persp)
				{
					if (dist < min_dist)
					{
						min_dist = dist;
						picked_vd = vd;
					}
				}
				else
				{
					if (dist > min_dist)
					{
						min_dist = dist;
						picked_vd = vd;
					}
				}
			}
		}
	}

	if (picked_vd)
	{
		VRenderFrame* frame = (VRenderFrame*)m_frame;
		if (frame && frame->GetTree())
		{
			frame->GetTree()->SetFocus();
			frame->GetTree()->Select(m_vrv->GetName(), picked_vd->GetName());
		}
	}
}

bool VRenderVulkanView::SelSegVolume(int mode)
{
	double dist = 0.0;
	double min_dist = -1.0;
	Point p;
	VolumeData* vd = 0;
	VolumeData* picked_vd = 0;
	int picked_sel_id = 0;
	bool rval = false;
	for (int i=0; i<(int)m_vd_pop_list.size(); i++)
	{
		vd = m_vd_pop_list[i];
		if (!vd || !vd->GetDisp()) continue;

		int cmode = vd->GetColormapMode();
		if (cmode != 3) continue;
		double sel_id;
		dist = GetPointAndIntVolume(p, sel_id, false, old_mouse_X, old_mouse_Y, vd);

		if (dist > 0.0)
		{
			if (min_dist < 0.0)
			{
				min_dist = dist;
				picked_vd = vd;
				picked_sel_id = sel_id;
			}
			else
			{
				if (m_persp)
				{
					if (dist < min_dist)
					{
						min_dist = dist;
						picked_vd = vd;
						picked_sel_id = sel_id;
					}
				}
				else
				{
					if (dist > min_dist)
					{
						min_dist = dist;
						picked_vd = vd;
						picked_sel_id = sel_id;
					}
				}
			}
		}
	}

	if (picked_vd && picked_sel_id > 0)
	{
		int combined_seg_id = picked_vd->IsROICombined(picked_sel_id);
		if (combined_seg_id > 0)
			picked_sel_id = combined_seg_id;

		switch(mode)
		{
		case 0:
			if (picked_vd->isSelID(picked_sel_id))
				picked_vd->DelSelID(picked_sel_id);
			else
				picked_vd->AddSelID(picked_sel_id);
			break;
		case 1:
			if (picked_vd->isSelID(picked_sel_id))
				picked_vd->DelSelID(picked_sel_id);
			else
			{
				picked_vd->ClearSelIDs();
				picked_vd->AddSelID(picked_sel_id);
			}
			break;
		case 2:
			picked_vd->DelSelID(picked_sel_id);
			break;
		case 3:
			if (picked_vd->isSelID(picked_sel_id))
			{
				picked_vd->SetEditSelID(picked_sel_id);
			}
			break;
		}

		VRenderFrame* frame = (VRenderFrame*)m_frame;
		if (frame)
		{
			VPropView *vprop_view = frame->GetPropView();
			if (vprop_view)
				vprop_view->UpdateUIsROI();

			if (frame->GetTree())
			{
				if (mode != 3) frame->UpdateTreeIcons();
				frame->GetTree()->SetFocus();
				frame->GetTree()->SelectROI(picked_vd, picked_sel_id);
			}
		}
		rval = true;
	}

	return rval;
}


bool VRenderVulkanView::SelLabelSegVolumeMax(int mode, vector<VolumeData*> ref)
{
	double dist = 0.0;
	double min_dist = -1.0;
	Point p;
	VolumeData* vd = 0;
	VolumeData* picked_vd = 0;
	vector<VolumeData*> navds;
	int picked_sel_id = 0;
	bool rval = false;
	for (int i = 0; i < (int)m_vd_pop_list.size(); i++)
	{
		vd = m_vd_pop_list[i];
		if (!vd || !vd->GetDisp() || !vd->GetNAMode()) continue;
		navds.push_back(vd);
		if (!vd->GetLabel(false)) continue;

		if (picked_vd)
			continue;

		int sel_id;
		dist = GetPointAndLabelMax(p, sel_id, old_mouse_X, old_mouse_Y, vd, ref);
        //dist = GetPointAndLabel(p, sel_id, old_mouse_X, old_mouse_Y, vd);

		if (dist > 0.0)
		{
			if (min_dist < 0.0)
			{
				min_dist = dist;
				picked_vd = vd;
				picked_sel_id = sel_id;
			}
			else
			{
				if (m_persp)
				{
					if (dist < min_dist)
					{
						min_dist = dist;
						picked_vd = vd;
						picked_sel_id = sel_id;
					}
				}
				else
				{
					if (dist > min_dist)
					{
						min_dist = dist;
						picked_vd = vd;
						picked_sel_id = sel_id;
					}
				}
			}
		}
	}

	if (picked_vd && picked_sel_id > 0)
	{
		for (int i = 0; i < (int)navds.size(); i++)
		{
			switch (mode)
			{
			case 0:
				if (navds[i]->GetSegmentMask(picked_sel_id) == 2)
					navds[i]->SetSegmentMask(picked_sel_id, 1);
				else if (navds[i]->GetSegmentMask(picked_sel_id) == 1)
					navds[i]->SetSegmentMask(picked_sel_id, 2);
				break;
			case 1:
				if (navds[i]->GetSegmentMask(picked_sel_id) == 2)
					navds[i]->SetSegmentMask(picked_sel_id, false);
				else if (navds[i]->GetSegmentMask(picked_sel_id) == 1)
				{
					auto ids = navds[i]->GetActiveSegIDs();
					if (ids)
					{
						auto it = ids->begin();
						while (it != ids->end())
							navds[i]->SetSegmentMask(*it, 1);
					}
					navds[i]->SetSegmentMask(picked_sel_id, 2);
				}
				break;
			}

			VRenderFrame* frame = (VRenderFrame*)m_frame;
			if (frame)
			{
				//VPropView* vprop_view = frame->GetPropView();
				//if (vprop_view)
				//	vprop_view->UpdateUIsROI();

				if (frame->GetTree())
				{
					if (mode != 3) frame->UpdateTreeIcons();
					//frame->GetTree()->SetFocus();
				}
			}
			rval = true;
		}
	}

	return rval;
}

#if defined(__WXGTK__)
void VRenderVulkanView::OnIdle(wxIdleEvent& event)
#else
void VRenderVulkanView::OnIdle(wxTimerEvent& event)
#endif
{
	bool refresh = m_refresh | m_refresh_start_loop;
	bool ref_stat = false;
	bool start_loop = true;
	m_drawing_coord = false;

	//event.RequestMore(true);

	//check memory swap status
	if (TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_start_update_loop() &&
		!TextureRenderer::get_done_update_loop())
	{
		refresh = true;
		start_loop = m_refresh_start_loop;
	}
	m_refresh_start_loop = false;

	if (m_capture_rotat ||
		m_capture_tsequ ||
		m_capture_param ||
		m_test_speed)
	{
		refresh = true;
		if (TextureRenderer::get_mem_swap() &&
			TextureRenderer::get_done_update_loop())
			m_pre_draw = true;
	}

	wxPoint mouse_pos = wxGetMousePosition();
	wxRect view_reg = GetScreenRect();

	wxWindow *window = wxWindow::FindFocus();
	bool editting = false;
	if (window)
	{
		wxClassInfo *wclass = window->GetClassInfo();
		if (wclass) 
		{
			wxString cname = wclass->GetClassName();
			if (cname == "wxTextCtrl" || cname == "wxDirPickerCtrl" || cname == "wxFilePickerCtrl")
				editting = true;
		}
	}

	VRenderFrame* frame = (VRenderFrame*)m_frame;
	VRenderVulkanView* cur_glview = NULL;
	if (frame && frame->GetTree())
	{
		VRenderView *vrv = frame->GetTree()->GetCurrentView();
		if (vrv) cur_glview = vrv->m_glview;
	}

	if (window && !editting && !m_key_lock)
	{
		//move view
		//left
		if (!m_move_left &&
			VRenderFrame::GetKeyState(WXK_CONTROL) &&
			VRenderFrame::GetKeyState(WXK_LEFT))
		{
			m_move_left = true;

			m_head = Vector(-m_transx, -m_transy, -m_transz);
			m_head.normalize();
			Vector side = Cross(m_up, m_head);
			Vector trans = -(side*(int(0.8*(m_ortho_right-m_ortho_left))));
			m_obj_transx += trans.x();
			m_obj_transy += trans.y();
			m_obj_transz += trans.z();
			//if (m_persp) SetSortBricks();
			refresh = true;
		}
		if (m_move_left &&
			(!VRenderFrame::GetKeyState(WXK_CONTROL) ||
			!VRenderFrame::GetKeyState(WXK_LEFT)))
			m_move_left = false;
		//right
		if (!m_move_right &&
			VRenderFrame::GetKeyState(WXK_CONTROL) &&
			VRenderFrame::GetKeyState(WXK_RIGHT))
		{
			m_move_right = true;

			m_head = Vector(-m_transx, -m_transy, -m_transz);
			m_head.normalize();
			Vector side = Cross(m_up, m_head);
			Vector trans = side*(int(0.8*(m_ortho_right-m_ortho_left)));
			m_obj_transx += trans.x();
			m_obj_transy += trans.y();
			m_obj_transz += trans.z();
			//if (m_persp) SetSortBricks();
			refresh = true;
		}
		if (m_move_right &&
			(!VRenderFrame::GetKeyState(WXK_CONTROL) ||
			!VRenderFrame::GetKeyState(WXK_RIGHT)))
			m_move_right = false;
		//up
		if (!m_move_up &&
			VRenderFrame::GetKeyState(WXK_CONTROL) &&
			VRenderFrame::GetKeyState(WXK_UP))
		{
			m_move_up = true;

			m_head = Vector(-m_transx, -m_transy, -m_transz);
			m_head.normalize();
			Vector trans = -m_up*(int(0.8*(m_ortho_top-m_ortho_bottom)));
			m_obj_transx += trans.x();
			m_obj_transy += trans.y();
			m_obj_transz += trans.z();
			//if (m_persp) SetSortBricks();
			refresh = true;
		}
		if (m_move_up &&
			(!VRenderFrame::GetKeyState(WXK_CONTROL) ||
			!VRenderFrame::GetKeyState(WXK_UP)))
			m_move_up = false;
		//down
		if (!m_move_down &&
			VRenderFrame::GetKeyState(WXK_CONTROL) &&
			VRenderFrame::GetKeyState(WXK_DOWN))
		{
			m_move_down = true;

			m_head = Vector(-m_transx, -m_transy, -m_transz);
			m_head.normalize();
			Vector trans = m_up*(int(0.8*(m_ortho_top-m_ortho_bottom)));
			m_obj_transx += trans.x();
			m_obj_transy += trans.y();
			m_obj_transz += trans.z();
			//if (m_persp) SetSortBricks();
			refresh = true;
		}
		if (m_move_down &&
			(!VRenderFrame::GetKeyState(WXK_CONTROL) ||
			!VRenderFrame::GetKeyState(WXK_DOWN)))
			m_move_down = false;

		//move time sequence
		//forward
		if (!m_tseq_forward &&
			VRenderFrame::GetKeyState(wxKeyCode('d')) ||
			VRenderFrame::GetKeyState(WXK_SPACE))
		{
			m_tseq_forward = true;
			if (frame && frame->GetMovieView())
				frame->GetMovieView()->UpFrame();
			refresh = true;
		}
		if (m_tseq_forward &&
			!VRenderFrame::GetKeyState(wxKeyCode('d')) &&
			!VRenderFrame::GetKeyState(WXK_SPACE))
			m_tseq_forward = false;
		//backforward
		if (!m_tseq_backward &&
			VRenderFrame::GetKeyState(wxKeyCode('a')))
		{
			m_tseq_backward = true;
			if (frame && frame->GetMovieView())
				frame->GetMovieView()->DownFrame();
			refresh = true;
		}
		if (m_tseq_backward &&
			!VRenderFrame::GetKeyState(wxKeyCode('a')))
			m_tseq_backward = false;

		//move clip
		//up
		if (!m_clip_up &&
			VRenderFrame::GetKeyState(wxKeyCode('s')))
		{
			m_clip_up = true;
			if (frame && frame->GetClippingView())
				frame->GetClippingView()->MoveLinkedClippingPlanes(1);
			refresh = true;
		}
		if (m_clip_up &&
			!VRenderFrame::GetKeyState(wxKeyCode('s')))
			m_clip_up = false;
		//down
		if (!m_clip_down &&
			VRenderFrame::GetKeyState(wxKeyCode('w')))
		{
			m_clip_down = true;
			if (frame && frame->GetClippingView())
				frame->GetClippingView()->MoveLinkedClippingPlanes(0);
			refresh = true;
		}
		if (m_clip_down &&
			!VRenderFrame::GetKeyState(wxKeyCode('w')))
			m_clip_down = false;

		//draw_mask
        if (!m_force_hide_mask)
        {
            if (VRenderFrame::GetKeyState(wxKeyCode('V')) &&
                m_draw_mask)
            {
                m_draw_mask = false;
                refresh = true;
            }
            if (!VRenderFrame::GetKeyState(wxKeyCode('V')) &&
                !m_draw_mask)
            {
                m_draw_mask = true;
                refresh = true;
            }
        }
        else if (m_draw_mask)
        {
            m_draw_mask = false;
            refresh = true;
        }
	}

	if (window && !editting && view_reg.Contains(mouse_pos) && !m_key_lock)
	{
		UpdateBrushState();
	}

	if (window && !editting && this == cur_glview && !m_key_lock)
	{
		//cell full
		if (!m_cell_full &&
			VRenderFrame::GetKeyState(wxKeyCode('f')))
		{
			m_cell_full = true;
			if (frame && frame->GetTraceDlg())
				frame->GetTraceDlg()->CellFull();
			refresh = true;
		}
		if (m_cell_full &&
			!VRenderFrame::GetKeyState(wxKeyCode('f')))
			m_cell_full = false;
		//cell link
		if (!m_cell_link &&
			VRenderFrame::GetKeyState(wxKeyCode('l')))
		{
			m_cell_link = true;
			if (frame && frame->GetTraceDlg())
				frame->GetTraceDlg()->CellLink(false);
			refresh = true;
		}
		if (m_cell_link &&
			!VRenderFrame::GetKeyState(wxKeyCode('l')))
			m_cell_link = false;
		//new cell id
/*		if (!m_cell_new_id &&
			VRenderFrame::GetKeyState(wxKeyCode('n')))
		{
			m_cell_new_id = true;
			if (frame && frame->GetTraceDlg())
				frame->GetTraceDlg()->CellNewID(false);
			refresh = true;
		}
		if (m_cell_new_id &&
			!VRenderFrame::GetKeyState(wxKeyCode('n')))
			m_cell_new_id = false;
*/		//clear
/*		if (VRenderFrame::GetKeyState(wxKeyCode('c')) &&
			!m_clear_mask)
		{
			if (frame && frame->GetTraceDlg())
				frame->GetTraceDlg()->CompClear();
			m_clear_mask = true;
			refresh = true;
		}
		if (!VRenderFrame::GetKeyState(wxKeyCode('c')) &&
			m_clear_mask)
			m_clear_mask = false;
*/		//full screen
		if (VRenderFrame::GetKeyState(WXK_ESCAPE))
		{
			/*			if (GetParent() == m_vrv->m_full_frame)
			{
			Reparent(m_vrv);
			m_vrv->m_view_sizer->Add(this, 1, wxEXPAND);
			m_vrv->Layout();
			m_vrv->m_full_frame->Hide();
			#ifdef _WIN32
			if (frame && frame->GetSettingDlg() &&
			!frame->GetSettingDlg()->GetShowCursor())
			ShowCursor(true);
			#endif
			refresh = true;
			}
			*/		}

		if (VRenderFrame::GetKeyState(WXK_ALT) && VRenderFrame::GetKeyState(wxKeyCode('V')) && !m_key_lock)
		{
			if(m_cur_vol && m_cur_vol->isBrxml())
			{
				CopyLevel(m_cur_vol);
				m_selector.SetVolume(m_cur_vol);
			}
		}
        
        if (VRenderFrame::GetKeyState(wxKeyCode('Z')) && VRenderFrame::GetKeyState(WXK_CONTROL) && !VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT) && !m_undo_keydown && !m_key_lock)
        {
            VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
            if (frame && vr_frame->GetBrushToolDlg())
                vr_frame->GetBrushToolDlg()->BrushUndo();
            m_undo_keydown = true;
        }
        if (VRenderFrame::GetKeyState(wxKeyCode('Y')) && VRenderFrame::GetKeyState(WXK_CONTROL) && !VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT) && !m_redo_keydown && !m_key_lock)
        {
            VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
            if (frame && vr_frame->GetBrushToolDlg())
                vr_frame->GetBrushToolDlg()->BrushRedo();
            m_redo_keydown = true;
        }
        
        if (VRenderFrame::GetKeyState(wxKeyCode('B')) && VRenderFrame::GetKeyState(WXK_CONTROL) && !VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT) && !m_key_lock)
        {
            WarpCurrentVolume();
        }
	}
    
    if (!(VRenderFrame::GetKeyState(wxKeyCode('Z')) && VRenderFrame::GetKeyState(WXK_CONTROL) && !VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT)))
    {
        m_undo_keydown = false;
    }
    if (!(VRenderFrame::GetKeyState(wxKeyCode('Y')) && VRenderFrame::GetKeyState(WXK_CONTROL) && !VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT)))
    {
        m_redo_keydown = false;
    }

	if (window && !editting && this == cur_glview && !m_key_lock && m_tile_rendering)
	{
		if (VRenderFrame::GetKeyState(WXK_ESCAPE))
		{
			EndTileRendering();
		}
	}

	extern CURLM * _g_curlm;
	int handle_count;
	curl_multi_perform(_g_curlm, &handle_count);
	CURLMsg *msg = NULL;
	int remains;
	while( (msg = curl_multi_info_read(_g_curlm, &remains)) != NULL)
	{
		if(msg->msg == CURLMSG_DONE) RefreshGLOverlays();;
	}

	if (refresh)
	{
        m_refresh = false;
		m_updating = true;
		RefreshGL(ref_stat, start_loop);
	}
}

void VRenderVulkanView::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

void VRenderVulkanView::OnContextMenu(wxContextMenuEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;

	bool na_mode = false;
	for (int i = 0; i < m_vd_pop_list.size(); i++)
	{
		if (m_vd_pop_list[i]->GetDisp() && m_vd_pop_list[i]->GetNAMode())
		{
			na_mode = true;
			break;
		}
	}

	wxMenu menu;
	if (na_mode)
	{
		menu.Append(ID_CTXMENU_SHOW_ALL_FRAGMENTS, "Show all");
        menu.Append(ID_CTXMENU_DESELECT_ALL_FRAGMENTS, "Deselect all");
        menu.AppendSeparator();
		menu.Append(ID_CTXMENU_HIDE_OTHER_FRAGMENTS, "Show only selected");
		menu.Append(ID_CTXMENU_HIDE_SELECTED_FLAGMENTS, "Hide selected");
	}
	else
	{
		menu.Append(ID_CTXMENU_SHOW_ALL, "Show all");
        menu.AppendSeparator();
		menu.Append(ID_CTXMENU_HIDE_OTHER_VOLS, "Show only selected");
		menu.Append(ID_CTXMENU_HIDE_THIS_VOL, "Hide selected");
		menu.AppendSeparator();
		menu.Append(ID_CTXMENU_JOIN_SEGMENTS, "Join selected segments");
		menu.Append(ID_CTXMENU_SPLIT_SEGMENTS, "Split selected segments");
	}
    menu.AppendSeparator();
	menu.Append(ID_CTXMENU_UNDO_VISIBILITY_SETTING_CHANGES, "Undo visibility");
	menu.Append(ID_CTXMENU_REDO_VISIBILITY_SETTING_CHANGES, "Redo visibility");

	wxPoint point = event.GetPosition();
	// If from keyboard
	if (point.x == -1 && point.y == -1) {
		wxSize size = GetSize();
		point.x = size.x / 2;
		point.y = size.y / 2;
	}
	else {
		point = ScreenToClient(point);
	}

	PopupMenu(&menu, point.x, point.y);

}

void VRenderVulkanView::OnShowAllDatasets(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	TreePanel* tree_panel = vr_frame->GetTree();
	if (!tree_panel) return;

    tree_panel->ShowAllDatasets();
}
void VRenderVulkanView::OnHideOtherDatasets(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
    TreePanel* tree_panel = vr_frame->GetTree();
    if (!tree_panel) return;

    tree_panel->HideOtherDatasets();
}
void VRenderVulkanView::OnHideSelectedDataset(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;

	TreePanel* tree_panel = vr_frame->GetTree();
	if (!tree_panel) return;

	tree_panel->HideSelectedItem();
}

void VRenderVulkanView::OnShowAllFragments(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	TreePanel* tree_panel = vr_frame->GetTree();
	if (!tree_panel) return;

	vector<VolumeData*> na_vols;

	for (int i = 0; i < (int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 2://volume data
		{
			VolumeData* vd = (VolumeData*)m_layer_list[i];
			if (vd && vd->GetNAMode())
				na_vols.push_back(vd);
		}
		break;
		case 5://group
		{
			DataGroup* group = (DataGroup*)m_layer_list[i];
			if (!group->GetDisp())
				continue;
			for (int j = 0; j < group->GetVolumeNum(); j++)
			{
				VolumeData* vd = group->GetVolumeData(j);
				if (vd && vd->GetNAMode())
					na_vols.push_back(vd);
			}
		}
		break;
		}
	}

	bool changed = false;
	for (auto vd : na_vols)
	{
		auto ids = vd->GetActiveSegIDs();
		if (ids)
		{
			auto it = ids->begin();
			while (it != ids->end())
			{
				if (vd->GetSegmentMask(*it) == 0 && *it != 0)
				{
					if (!changed)
					{
						tree_panel->PushVisHistory();
						changed = true;
					}
					vd->SetSegmentMask(*it, 1);
				}
				it++;
			}
		}
	}

	if (changed)
	{
		RefreshGL();
		vr_frame->UpdateTreeIcons();
	}
}

void VRenderVulkanView::OnDeselectAllFragments(wxCommandEvent& event)
{
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    if (!vr_frame) return;
    TreePanel* tree_panel = vr_frame->GetTree();
    if (!tree_panel) return;
    
    vector<VolumeData*> na_vols;
    
    for (int i = 0; i < (int)m_layer_list.size(); i++)
    {
        if (!m_layer_list[i])
            continue;
        switch (m_layer_list[i]->IsA())
        {
            case 2://volume data
            {
                VolumeData* vd = (VolumeData*)m_layer_list[i];
                if (vd && vd->GetNAMode())
                    na_vols.push_back(vd);
            }
                break;
            case 5://group
            {
                DataGroup* group = (DataGroup*)m_layer_list[i];
                if (!group->GetDisp())
                    continue;
                for (int j = 0; j < group->GetVolumeNum(); j++)
                {
                    VolumeData* vd = group->GetVolumeData(j);
                    if (vd && vd->GetNAMode())
                        na_vols.push_back(vd);
                }
            }
                break;
        }
    }
    
    bool changed = false;
    for (auto vd : na_vols)
    {
        auto ids = vd->GetActiveSegIDs();
        if (ids)
        {
            auto it = ids->begin();
            while (it != ids->end())
            {
                if (vd->GetSegmentMask(*it) == 2 && *it != 0)
                {
                    if (!changed)
                    {
                        tree_panel->PushVisHistory();
                        changed = true;
                    }
                    vd->SetSegmentMask(*it, 1);
                }
                it++;
            }
        }
    }
    
    if (changed)
    {
        RefreshGL();
        vr_frame->UpdateTreeIcons();
    }
}

void VRenderVulkanView::OnHideOtherFragments(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	TreePanel* tree_panel = vr_frame->GetTree();
	if (!tree_panel) return;

	vector<VolumeData*> na_vols;

	for (int i = 0; i < (int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 2://volume data
		{
			VolumeData* vd = (VolumeData*)m_layer_list[i];
			if (vd && vd->GetNAMode())
				na_vols.push_back(vd);
		}
		break;
		case 5://group
		{
			DataGroup* group = (DataGroup*)m_layer_list[i];
			for (int j = 0; j < group->GetVolumeNum(); j++)
			{
				VolumeData* vd = group->GetVolumeData(j);
				if (vd && vd->GetNAMode())
					na_vols.push_back(vd);
			}
		}
		break;
		}
	}

	bool changed = false;
	for (auto vd : na_vols)
	{
		auto ids = vd->GetActiveSegIDs();
		if (ids)
		{
			auto it = ids->begin();
			while (it != ids->end())
			{
				if (vd->GetSegmentMask(*it) == 1)
				{
					if (!changed)
					{
						tree_panel->PushVisHistory();
						changed = true;
					}
					vd->SetSegmentMask(*it, 0);
				}
				it++;
			}
		}
	}

	if (changed)
	{
		RefreshGL();
		vr_frame->UpdateTreeIcons();
	}
}
void VRenderVulkanView::OnHideSelectedFragments(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	TreePanel* tree_panel = vr_frame->GetTree();
	if (!tree_panel) return;

	vector<VolumeData*> na_vols;

	for (int i = 0; i < (int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 2://volume data
		{
			VolumeData* vd = (VolumeData*)m_layer_list[i];
			if (vd && vd->GetNAMode())
				na_vols.push_back(vd);
		}
		break;
		case 5://group
		{
			DataGroup* group = (DataGroup*)m_layer_list[i];
			for (int j = 0; j < group->GetVolumeNum(); j++)
			{
				VolumeData* vd = group->GetVolumeData(j);
				if (vd && vd->GetNAMode())
					na_vols.push_back(vd);
			}
		}
		break;
		}
	}

	bool changed = false;
	for (auto vd : na_vols)
	{
		auto ids = vd->GetActiveSegIDs();
		if (ids)
		{
			auto it = ids->begin();
			while (it != ids->end())
			{
				if (vd->GetSegmentMask(*it) == 2)
				{
					if (!changed)
					{
						tree_panel->PushVisHistory();
						changed = true;
					}
					vd->SetSegmentMask(*it, 0);
				}
				it++;
			}
		}
	}

	if (changed)
	{
		RefreshGL();
		vr_frame->UpdateTreeIcons();
	}
}
void VRenderVulkanView::OnJoinSegments(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;

	TreePanel* tree_panel = vr_frame->GetTree();
	if (!tree_panel) return;

	if (m_cur_vol && m_cur_vol->GetColormapMode() == 3)
	{
		m_cur_vol->CombineSelectedROIs();
		vr_frame->UpdateROITree(m_cur_vol);
		RefreshGL();
	}
}
void VRenderVulkanView::OnSplitSegments(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;

	TreePanel* tree_panel = vr_frame->GetTree();
	if (!tree_panel) return;

	if (m_cur_vol && m_cur_vol->GetColormapMode() == 3)
	{
		m_cur_vol->SplitSelectedROIs();
		vr_frame->UpdateROITree(m_cur_vol);
		RefreshGL();
	}
}
void VRenderVulkanView::OnUndoVisibilitySettings(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;

	TreePanel* tree_panel = vr_frame->GetTree();
	if (!tree_panel) return;

	tree_panel->UndoVisibility();
}
void VRenderVulkanView::OnRedoVisibilitySettings(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;

	TreePanel* tree_panel = vr_frame->GetTree();
	if (!tree_panel) return;

	tree_panel->RedoVisibility();
}


void VRenderVulkanView::Set3DRotCapture(double start_angle,
									double end_angle,
									double step,
									int frames,
									int rot_axis,
									wxString &cap_file,
									bool rewind,
									int len)
{
	double x, y, z;
	GetRotations(x, y, z);

	//remove the chance of the x/y/z angles being outside 360.
	while (x > 360.)  x -=360.;
	while (x < -360.) x +=360.;
	while (y > 360.)  y -=360.;
	while (y < -360.) y +=360.;
	while (z > 360.)  z -=360.;
	while (z < -360.) z +=360.;
	if (360. - std::abs(x) < 0.001) x = 0.;
	if (360. - std::abs(y) < 0.001) y = 0.;
	if (360. - std::abs(z) < 0.001) z = 0.;
	m_step = step;
	m_total_frames = frames;
	m_rot_axis = rot_axis;
	m_cap_file = cap_file;
	m_rewind = rewind;
	//m_fr_length = len;

	m_movie_seq = 0;
	switch (m_rot_axis)
	{
	case 1: //X
		if (start_angle==0.) {
			m_init_angle = x;
			m_end_angle = x + end_angle;
		}
		m_cur_angle = x;
		m_start_angle = x;
		break;
	case 2: //Y
		if (start_angle==0.) {
			m_init_angle = y;
			m_end_angle = y + end_angle;
		}
		m_cur_angle = y;
		m_start_angle = y;
		break;
	case 3: //Z
		if (start_angle==0.) {
			m_init_angle = z;
			m_end_angle = z + end_angle;
		}
		m_cur_angle = z;
		m_start_angle = z;
		break;
	}
	m_capture = true;
	m_capture_rotat = true;
	m_capture_rotate_over = false;
	m_stages = 0;
}

void VRenderVulkanView::Set3DBatCapture(wxString &cap_file, int begin_frame, int end_frame)
{
	m_cap_file = cap_file;
	m_begin_frame = begin_frame;
	m_end_frame = end_frame;
	m_capture_bat = true;
	m_capture = true;

	if (!m_cap_file.IsEmpty() && m_total_frames>1)
	{
		wxString path = m_cap_file;
		int sep = path.Find(GETSLASH(), true);
		if (sep != wxNOT_FOUND)
		{
			sep++;
			path = path.Left(sep);
		}

		wxString new_folder = path + m_bat_folder + "_folder";
		CREATE_DIR(new_folder.fn_str());
	}

	//wxString s_fr_length = wxString ::Format("%d", end_frame);
	//m_fr_length = (int)s_fr_length.length();
}

void VRenderVulkanView::Set4DSeqCapture(wxString &cap_file, int begin_frame, int end_frame, bool rewind)
{
	m_cap_file = cap_file;
	m_tseq_cur_num = begin_frame;
	m_tseq_prv_num = begin_frame;
	m_begin_frame = begin_frame;
	m_end_frame = end_frame;
	m_capture_tsequ = true;
	m_capture = true;
	m_movie_seq = begin_frame;
	m_4d_rewind = rewind;
	VRenderFrame* vframe = (VRenderFrame*)m_frame;
	if (vframe && vframe->GetSettingDlg())
		m_run_script = vframe->GetSettingDlg()->GetRunScript();

	//wxString s_fr_length = wxString ::Format("%d", end_frame);
	//m_fr_length = (int)s_fr_length.length();
}

void VRenderVulkanView::SetParamCapture(wxString &cap_file, int begin_frame, int end_frame, bool rewind)
{
	m_cap_file = cap_file;
	m_param_cur_num = begin_frame;
	m_begin_frame = begin_frame;
	m_end_frame = end_frame;
	m_capture_param = true;
	m_capture = true;
	m_movie_seq = begin_frame;
	m_4d_rewind = rewind;

	//wxString s_fr_length = wxString ::Format("%d", end_frame);
	//m_fr_length = (int)s_fr_length.length();
}

void VRenderVulkanView::SetParams(double t)
{
	if (!m_vrv)
		return;
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	ClippingView* clip_view = vr_frame->GetClippingView();
	Interpolator *interpolator = vr_frame->GetInterpolator();
	if(!interpolator)
		return;
	FLKeyCode keycode;
	keycode.l0 = 1;
	keycode.l0_name = m_vrv->GetName();
    
    //for the view
    keycode.l1 = 1;
    keycode.l1_name = m_vrv->GetName();
    //link clipping planes
    keycode.l2 = 0;
    keycode.l2_name = "link_planes";
    bool bval;
    if (interpolator->GetBoolean(keycode, t, bval))
        clip_view->SetChannLink(bval);

	for (int i=0; i<GetAllVolumeNum(); i++)
	{
		VolumeData* vd = GetAllVolumeData(i);
		if (!vd) continue;

		keycode.l1 = 2;
		keycode.l1_name = vd->GetName();

		//display
		keycode.l2 = 0;
		keycode.l2_name = "display";
		bool bval;
		if (interpolator->GetBoolean(keycode, t, bval))
			vd->SetDisp(bval);

		//clipping planes
		vector<Plane*> *planes = vd->GetVR()->get_planes();
		if (!planes) continue;
		if(planes->size() != 6) continue;
		Plane *plane = 0;
		double val = 0;
		//x1
		plane = (*planes)[0];
		keycode.l2 = 0;
		keycode.l2_name = "x1_val";
		if (interpolator->GetDouble(keycode, t, val)) {
			plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedX1Param(val);
        }
		//x2
		plane = (*planes)[1];
		keycode.l2 = 0;
		keycode.l2_name = "x2_val";
        if (interpolator->GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedX2Param(val);
        }
		//y1
		plane = (*planes)[2];
		keycode.l2 = 0;
		keycode.l2_name = "y1_val";
        if (interpolator->GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedY1Param(val);
        }
		//y2
		plane = (*planes)[3];
		keycode.l2 = 0;
		keycode.l2_name = "y2_val";
        if (interpolator->GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedY2Param(val);
        }
		//z1
		plane = (*planes)[4];
		keycode.l2 = 0;
		keycode.l2_name = "z1_val";
        if (interpolator->GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedZ1Param(val);
        }
		//z2
		plane = (*planes)[5];
		keycode.l2 = 0;
		keycode.l2_name = "z2_val";
        if (interpolator->GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedZ2Param(val);
        }

		keycode.l2 = 0;
		keycode.l2_name = "gamma";
		if (interpolator->GetDouble(keycode, t, val))
			vd->Set3DGamma(val);

		keycode.l2 = 0;
		keycode.l2_name = "saturation";
		if (interpolator->GetDouble(keycode, t, val))
			vd->SetOffset(val);

		keycode.l2 = 0;
		keycode.l2_name = "luminance";
		if (interpolator->GetDouble(keycode, t, val))
			vd->SetLuminance(val);

		keycode.l2 = 0;
		keycode.l2_name = "alpha";
		if (interpolator->GetDouble(keycode, t, val))
			vd->SetAlpha(val);

		keycode.l2 = 0;
		keycode.l2_name = "boundary";
		if (interpolator->GetDouble(keycode, t, val))
			vd->SetBoundary(val);

		keycode.l2 = 0;
		keycode.l2_name = "left_threshold";
		if (interpolator->GetDouble(keycode, t, val))
			vd->SetLeftThresh(val);

		keycode.l2 = 0;
		keycode.l2_name = "right_threshold";
		if (interpolator->GetDouble(keycode, t, val))
			vd->SetRightThresh(val);
	}
    
    for (int i=0; i<GetAllMeshNum(); i++)
    {
        MeshData* md = GetAllMeshData(i);
        if (!md) continue;
        
        keycode.l1 = 2;
        keycode.l1_name = md->GetName();
        
        //display
        keycode.l2 = 0;
        keycode.l2_name = "display";
        bool bval;
        if (interpolator->GetBoolean(keycode, t, bval))
            md->SetDisp(bval);
        
        //clipping planes
        vector<Plane*> *planes = md->GetMR()->get_planes();
        if (!planes) continue;
        if(planes->size() != 6) continue;
        Plane *plane = 0;
        double val = 0;
        //x1
        plane = (*planes)[0];
        keycode.l2 = 0;
        keycode.l2_name = "x1_val";
        if (interpolator->GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedX1Param(val);
        }
        //x2
        plane = (*planes)[1];
        keycode.l2 = 0;
        keycode.l2_name = "x2_val";
        if (interpolator->GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedX2Param(val);
        }
        //y1
        plane = (*planes)[2];
        keycode.l2 = 0;
        keycode.l2_name = "y1_val";
        if (interpolator->GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedY1Param(val);
        }
        //y2
        plane = (*planes)[3];
        keycode.l2 = 0;
        keycode.l2_name = "y2_val";
        if (interpolator->GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedY2Param(val);
        }
        //z1
        plane = (*planes)[4];
        keycode.l2 = 0;
        keycode.l2_name = "z1_val";
        if (interpolator->GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedZ1Param(val);
        }
        //z2
        plane = (*planes)[5];
        keycode.l2 = 0;
        keycode.l2_name = "z2_val";
        if (interpolator->GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedZ2Param(val);
        }
        
        keycode.l2 = 0;
        keycode.l2_name = "transparency";
        if (interpolator->GetDouble(keycode, t, val))
            md->SetFloat(val, MESH_FLOAT_ALPHA);
    }
    
    m_vrv->SetEvtHandlerEnabled(false);
/*
	//t
	double frame;
	keycode.l2 = 0;
	keycode.l2_name = "frame";
	if (interpolator->GetDouble(keycode, t, frame))
		Set4DSeqFrame(int(frame + 0.5), false);

	//batch
	double batch;
	keycode.l2 = 0;
	keycode.l2_name = "batch";
	if (interpolator->GetDouble(keycode, t, batch))
		Set3DBatFrame(int(batch + 0.5));
*/
	bool bx, by, bz;
	//for the view
	keycode.l1 = 1;
	keycode.l1_name = m_vrv->GetName();
	//translation
	double tx, ty, tz;
	keycode.l2 = 0;
	keycode.l2_name = "translation_x";
	bx = interpolator->GetDouble(keycode, t, tx);
	keycode.l2_name = "translation_y";
	by = interpolator->GetDouble(keycode, t, ty);
	keycode.l2_name = "translation_z";
	bz = interpolator->GetDouble(keycode, t, tz);
	if (bx && by && bz)
		SetTranslations(tx, ty, tz);
	//centers
	keycode.l2_name = "center_x";
	bx = interpolator->GetDouble(keycode, t, tx);
	keycode.l2_name = "center_y";
	by = interpolator->GetDouble(keycode, t, ty);
	keycode.l2_name = "center_z";
	bz = interpolator->GetDouble(keycode, t, tz);
	if (bx && by && bz)
		SetCenters(tx, ty, tz);
	//obj translation
	keycode.l2_name = "obj_trans_x";
	bx = interpolator->GetDouble(keycode, t, tx);
	keycode.l2_name = "obj_trans_y";
	by = interpolator->GetDouble(keycode, t, ty);
	keycode.l2_name = "obj_trans_z";
	bz = interpolator->GetDouble(keycode, t, tz);
	if (bx && by && bz)
		SetObjTrans(tx, ty, tz);
	//scale
	double scale;
	keycode.l2_name = "scale";
	if (interpolator->GetDouble(keycode, t, scale))
		m_vrv->SetScaleFactor(scale);
	//rotation
	keycode.l2 = 0;
	keycode.l2_name = "rotation";
	Quaternion q;
	if (interpolator->GetQuaternion(keycode, t, q))
	{
		double rotx, roty, rotz;
		q.ToEuler(rotx, roty, rotz);
		SetRotations(rotx, roty, rotz, true);
	}
	//intermixing mode
	keycode.l2_name = "volmethod";
	int ival;
	if (interpolator->GetInt(keycode, t, ival))
		SetVolMethod(ival);
    
    m_vrv->SetEvtHandlerEnabled(true);

	if (clip_view)
		clip_view->SetVolumeData(vr_frame->GetCurSelVol());
	if (vr_frame)
	{
		vr_frame->UpdateTree(m_cur_vol?m_cur_vol->GetName():"", 2);
		int index = interpolator->GetKeyIndexFromTime(t);
		vr_frame->GetRecorderDlg()->SetSelection(index);
	}
	SetVolPopDirty();
}

void VRenderVulkanView::ResetMovieAngle()
{
	double rotx, roty, rotz;
	GetRotations(rotx, roty, rotz);

	switch (m_rot_axis)
	{
	case 1:  //x
		SetRotations(m_init_angle, roty, rotz);
		break;
	case 2:  //y
		SetRotations(rotx, m_init_angle, rotz);
		break;
	case 3:  //z
		SetRotations(rotx, roty, m_init_angle);
		break;
	}
	m_capture = false;
	m_capture_rotat = false;

	RefreshGL();
}

void VRenderVulkanView::StopMovie()
{
	m_capture = false;
	m_capture_rotat = false;
	m_capture_tsequ = false;
	m_capture_param = false;
}

void VRenderVulkanView::Get4DSeqFrames(int &start_frame, int &end_frame, int &cur_frame)
{
	for (int i=0; i<(int)m_vd_pop_list.size(); i++)
	{
		VolumeData* vd = m_vd_pop_list[i];
		if (vd && vd->GetReader())
		{
			BaseReader* reader = vd->GetReader();

			int vd_start_frame = 0;
			int vd_end_frame = reader->GetTimeNum()-1;
			int vd_cur_frame = reader->GetCurTime();

			if (i==0)
			{
				//first dataset
				start_frame = vd_start_frame;
				end_frame = vd_end_frame;
				cur_frame = vd_cur_frame;
			}
			else
			{
				//datasets after the first one
				if (vd_end_frame > end_frame)
					end_frame = vd_end_frame;
			}
		}
	}
}

void VRenderVulkanView::Set4DSeqFrame(int frame, bool run_script)
{
	int start_frame, end_frame, cur_frame;
	Get4DSeqFrames(start_frame, end_frame, cur_frame);
	if (frame > end_frame ||
		frame < start_frame)
		return;

	m_tseq_prv_num = m_tseq_cur_num;
	m_tseq_cur_num = frame;
	VRenderFrame* vframe = (VRenderFrame*)m_frame;
	if (vframe && vframe->GetSettingDlg())
	{
		m_run_script = vframe->GetSettingDlg()->GetRunScript();
		m_script_file = vframe->GetSettingDlg()->GetScriptFile();
	}

	for (int i=0; i<(int)m_vd_pop_list.size(); i++)
	{
		VolumeData* vd = m_vd_pop_list[i];
		if (vd && vd->GetReader())
		{
			BaseReader* reader = vd->GetReader();
			bool clear_pool = false;

			if (cur_frame != frame)
			{
				Texture *tex = vd->GetTexture();
				if(tex && tex->isBrxml())
				{
					BRKXMLReader *br = (BRKXMLReader *)reader;
					br->SetCurTime(frame);
					tex->set_FrameAndChannel(frame, vd->GetCurChannel());
					vd->SetCurTime(reader->GetCurTime());
					//update rulers
					if (vframe && vframe->GetMeasureDlg())
						vframe->GetMeasureDlg()->UpdateList();
					m_loader.RemoveAllLoadedBrick();
					clear_pool = true;
				}
				else
				{
					double spcx, spcy, spcz;
					vd->GetSpacings(spcx, spcy, spcz);

					auto data = m_loader.GetLoadedNrrd(vd, vd->GetCurChannel(), frame);
					if (!data)
					{
						if (m_loader.IsNrrdLoading(vd, vd->GetCurChannel(), frame))
						{
							uint32_t rn_time;
							unsigned long elapsed;
							long t;
							uint32_t st_time = GET_TICK_COUNT();
							do {
								wxMilliSleep(10);
								rn_time = GET_TICK_COUNT();
								elapsed = rn_time - st_time;
								data = m_loader.GetLoadedNrrd(vd, vd->GetCurChannel(), frame);
							} while (elapsed <= 5000 && !data);
						}
						if (!data)
							data = reader->Convert(frame, vd->GetCurChannel(), false);
					}

					if (!vd->Replace(data, false))
						continue;

					wxString data_path = wxString(reader->GetCurName(frame, vd->GetCurChannel()));
					wxString data_name = data_path.AfterLast(wxFileName::GetPathSeparator());
					if (reader->GetChanNum() > 1)
						data_name += wxString::Format("_Ch%d", vd->GetCurChannel()+1);
					vd->SetName(data_name);
					vd->SetPath(data_path);

					vd->SetCurTime(frame);
					vd->SetSpacings(spcx, spcy, spcz);

					//update rulers
					if (vframe && vframe->GetMeasureDlg())
						vframe->GetMeasureDlg()->UpdateList();

					//run script
					if (run_script)
						Run4DScript(m_script_file, vd);

					clear_pool = true;
				}
			}

			if (clear_pool && vd->GetVR())
				vd->GetVR()->clear_tex_current();
		}
	}
	RefreshGL();
 //very slow
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
		vr_frame->UpdateTreeFrames();

}

void VRenderVulkanView::Get3DBatFrames(int &start_frame, int &end_frame, int &cur_frame)
{
	m_bat_folder = "";

	for (int i=0; i<(int)m_vd_pop_list.size(); i++)
	{
		VolumeData* vd = m_vd_pop_list[i];
		if (vd && vd->GetReader())
		{
			BaseReader* reader = vd->GetReader();
			reader->SetBatch(true);

			int vd_cur_frame = reader->GetCurBatch();
			int vd_start_frame = -vd_cur_frame;
			int vd_end_frame = reader->GetBatchNum()-1-vd_cur_frame;

			if (i > 0)
				m_bat_folder += "_";
			m_bat_folder += wxString(reader->GetDataName());

			if (i==0)
			{
				//first dataset
				start_frame = vd_start_frame;
				end_frame = vd_end_frame;
				cur_frame = 0;
			}
			else
			{
				//datasets after the first one
				if (vd_start_frame < start_frame)
					start_frame = vd_start_frame;
				if (vd_end_frame > end_frame)
					end_frame = vd_end_frame;
			}
		}
	}
	cur_frame -= start_frame;
	end_frame -= start_frame;
	start_frame = 0;
}

void VRenderVulkanView::Set3DBatFrame(int offset)
{
	int i, j;
	vector<BaseReader*> reader_list;
	m_bat_folder = "";

	m_tseq_prv_num = m_tseq_cur_num;
	m_tseq_cur_num = offset;
	for (i=0; i<(int)m_vd_pop_list.size(); i++)
	{
		VolumeData* vd = m_vd_pop_list[i];
		if (vd && vd->GetReader() && vd->GetReader()->GetBatch())
		{
			Texture *tex = vd->GetTexture();
			BaseReader* reader = vd->GetReader();
			if(tex && tex->isBrxml())
			{
				BRKXMLReader *br = (BRKXMLReader *)reader;
				tex->set_FrameAndChannel(0, vd->GetCurChannel());
				vd->SetCurTime(reader->GetCurTime());
				wxString data_name = wxString(reader->GetDataName());
				if (i > 0)
					m_bat_folder += "_";
				m_bat_folder += data_name;

				int chan_num = 0;
				if (data_name.Find("_1ch")!=-1)
					chan_num = 1;
				else if (data_name.Find("_2ch")!=-1)
					chan_num = 2;
				if (chan_num>0 && vd->GetCurChannel()>=chan_num)
					vd->SetDisp(false);
				else
					vd->SetDisp(true);

				if (reader->GetChanNum() > 1)
					data_name += wxString::Format("_%d", vd->GetCurChannel()+1);
				vd->SetName(data_name);
			}
			else
			{
				//if (reader->GetOffset() == offset) return;
				bool found = false;
				for (j=0; j<(int)reader_list.size(); j++)
				{
					if (reader == reader_list[j])
					{
						found = true;
						break;
					}
				}
				if (!found)
				{
					reader->LoadOffset(offset);
					reader_list.push_back(reader);
				}

				double spcx, spcy, spcz;
				vd->GetSpacings(spcx, spcy, spcz);

				auto data = reader->Convert(0, vd->GetCurChannel(), true);
				if (vd->Replace(data, true))
					vd->SetDisp(true);
				else
				{
					vd->SetDisp(false);
					continue;
				}

				wxString data_name = wxString(reader->GetDataName());
				if (i > 0)
					m_bat_folder += "_";
				m_bat_folder += data_name;

				int chan_num = 0;
				if (data_name.Find("_1ch")!=-1)
					chan_num = 1;
				else if (data_name.Find("_2ch")!=-1)
					chan_num = 2;
				if (chan_num>0 && vd->GetCurChannel()>=chan_num)
					vd->SetDisp(false);
				else
					vd->SetDisp(true);

				if (reader->GetChanNum() > 1)
					data_name += wxString::Format("_%d", vd->GetCurChannel()+1);
				vd->SetName(data_name);
				vd->SetPath(wxString(reader->GetPathName()));
				vd->SetCurTime(reader->GetCurTime());
				//if (!reader->IsSpcInfoValid())
					vd->SetSpacings(spcx, spcy, spcz);
				//else
				//	vd->SetSpacings(reader->GetXSpc(), reader->GetYSpc(), reader->GetZSpc());
				if (vd->GetVR())
					vd->GetVR()->clear_tex_current();
			}
		}
	}

	InitView(INIT_BOUNDS|INIT_CENTER);

	RefreshGL();

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		vr_frame->UpdateTree(
			vr_frame->GetCurSelVol()?
			vr_frame->GetCurSelVol()->GetName():
			"");
	}
}

//pre-draw processings
void VRenderVulkanView::PreDraw()
{
	//skip if not done with loop
	if (TextureRenderer::get_mem_swap())
	{
		if (m_pre_draw)
			m_pre_draw = false;
		else
			return;
	}

	if (m_manip)
	{
		if (m_manip_cur_num <= m_manip_end_frame)
		{
			SetManipParams(m_manip_cur_num);

			m_manip_cur_num++;
		}
		else
		{
			SetManipParams(m_manip_end_frame);
			EndManipulation();
		}
	}
}

void VRenderVulkanView::PostDraw()
{
	//skip if not done with loop
	if (!m_postdraw) {
		if (TextureRenderer::get_mem_swap() &&
			TextureRenderer::get_start_update_loop() &&
			!TextureRenderer::get_done_update_loop())
			return;
	}
    
    vkQueueWaitIdle(m_vulkan->vulkanDevice->queue);

	//output animations
	if (m_capture && !m_cap_file.IsEmpty())
	{
		wxString outputfilename = m_cap_file;
		
		//capture
		int x, y, w, h;
        x = 0;
        y = 0;
        w = m_nx;
        h = m_ny;

		vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;

		int chann = 4;
		int dst_chann = 3;
		unsigned char* image = new unsigned char[w * h * chann];
		if (m_tile_rendering && m_tiled_image != NULL) {
			prim_dev->DownloadTexture(m_tex_tile, image);
			size_t stx = (m_current_tileid - 1) % m_tile_xnum * m_tile_w;
			size_t sty = (m_current_tileid - 1) / m_tile_xnum * m_tile_h;
			size_t edx = stx + m_tile_w;
			size_t edy = sty + m_tile_h;
			if (edx > m_capture_resx) edx = m_capture_resx;
			if (edy > m_capture_resy) edy = m_capture_resy;
			for (size_t y = sty; y < edy; y++) {
				for (size_t x = stx; x < edx; x++) {
					for (int c = 0; c < dst_chann; c++) {
						m_tiled_image[(y * m_capture_resx + x) * dst_chann + c] = image[((y - sty) * m_tile_w + (x - stx)) * chann + c];
					}
				}
			}
		}
		else {
			vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
			prim_dev->DownloadTexture(current_fbo->attachments[0], image);

			bool colorSwizzleBGR = false;
			std::vector<VkFormat> formatsBGR = { VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SNORM };
			colorSwizzleBGR = (std::find(formatsBGR.begin(), formatsBGR.end(), current_fbo->attachments[0]->format) != formatsBGR.end());

			unsigned char* rgb_image = new unsigned char[w * h * dst_chann];

			if (colorSwizzleBGR)
			{
				for (size_t y = 0; y < h; y++) {
					for (size_t x = 0; x < w; x++) {
						rgb_image[(y * w + x) * dst_chann + 2] = image[(y * w + x) * chann + 0];
						rgb_image[(y * w + x) * dst_chann + 1] = image[(y * w + x) * chann + 1];
						rgb_image[(y * w + x) * dst_chann + 0] = image[(y * w + x) * chann + 2];
					}
				}
			}
			else
			{
				for (size_t y = 0; y < h; y++) {
					for (size_t x = 0; x < w; x++) {
						for (int c = 0; c < dst_chann; c++) {
							rgb_image[(y * w + x) * dst_chann + c] = image[(y * w + x) * chann + c];
						}
					}
				}
			}
            
            if (m_draw_frame)
            {
                x = m_frame_x;
                y = m_frame_y;
                w = m_frame_w;
                h = m_frame_h;
            }
			
			string str_fn = outputfilename.ToStdString();
			TIFF* out = TIFFOpen(str_fn.c_str(), "wb");
			if (!out)
				return;
			TIFFSetField(out, TIFFTAG_IMAGEWIDTH, w);
			TIFFSetField(out, TIFFTAG_IMAGELENGTH, h);
			TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, dst_chann);
			TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 8);
			TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
			TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
			TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
			if (VRenderFrame::GetCompression())
				TIFFSetField(out, TIFFTAG_COMPRESSION, COMPRESSION_LZW);

			tsize_t linebytes = dst_chann * w;
            tsize_t ypitch = dst_chann * m_nx;
			unsigned char* buf = NULL;
			buf = (unsigned char*)_TIFFmalloc(linebytes);
			TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(out, 0));
			for (uint32 row = y; row < (uint32)h; row++)
			{
				memcpy(buf, &rgb_image[row*ypitch + x*dst_chann], linebytes);
				if (TIFFWriteScanline(out, buf, row-y, 0) < 0)
					break;
			}
			TIFFClose(out);
			if (buf)
				_TIFFfree(buf);

			delete[] rgb_image;
		}
		
		if (image)
			delete[] image;

		m_capture = false;
		
		if (m_tile_rendering) {
			if (m_current_tileid < m_tile_xnum*m_tile_ynum)
				m_capture = true;
			else if (m_tiled_image){


				double scalefac = GetSize().x > GetSize().y ? 
					(double)m_capture_resy / (double)GetSize().y :
					(double)m_capture_resx / (double)GetSize().x;

				size_t cap_ox = m_draw_frame ? (size_t)(m_frame_x*scalefac) : 0;
				size_t cap_oy = m_draw_frame ? (size_t)(m_frame_y*scalefac) : 0;
				size_t cap_w  = m_draw_frame ? (size_t)(m_frame_w*scalefac) : m_capture_resx;
				size_t cap_h  = m_draw_frame ? (size_t)(m_frame_h*scalefac) : m_capture_resy;

				if (cap_ox <= m_capture_resx && cap_oy <= m_capture_resy) {
					if (cap_ox + cap_w > m_capture_resx)
						cap_w = m_capture_resx - cap_ox;
					if (cap_oy + cap_h > m_capture_resy)
						cap_h = m_capture_resy - cap_oy;

					size_t imsize = (size_t)cap_w * (size_t)cap_h;
					string str_fn = outputfilename.ToStdString();
					TIFF* out = TIFFOpen(str_fn.c_str(), imsize <= 3758096384ULL ? "wb" : "wb8");
					if (out) {
						TIFFSetField(out, TIFFTAG_IMAGEWIDTH, cap_w);
						TIFFSetField(out, TIFFTAG_IMAGELENGTH, cap_h);
						TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, dst_chann);
						TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 8);
						TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
						TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
						TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
						if (VRenderFrame::GetCompression())
							TIFFSetField(out, TIFFTAG_COMPRESSION, COMPRESSION_LZW);

						tsize_t linebytes = dst_chann * cap_w;
						tsize_t pitchX = dst_chann * m_capture_resx;
						unsigned char* buf = NULL;
						buf = (unsigned char*)_TIFFmalloc(linebytes);
						TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(out, 0));
						for (uint32 row = 0; row < (uint32)cap_h; row++)
						{
							memcpy(buf, &m_tiled_image[(row + cap_oy) * pitchX + cap_ox * dst_chann], linebytes);
							if (TIFFWriteScanline(out, buf, row, 0) < 0)
								break;
						}
						TIFFClose(out);
						if (buf)
							_TIFFfree(buf);
					}
				}

				delete [] m_tiled_image;
				m_tiled_image = NULL;
			}
		}
	}

	m_postdraw = false;
}

//run 4d script
void VRenderVulkanView::Run4DScript(wxString &scriptname, VolumeData* vd)
{
	if (m_run_script && wxFileExists(m_script_file))
	{
		m_selector.SetVolume(vd);
		m_calculator.SetVolumeA(vd);
		m_cur_vol = vd;
	}
	else
		return;

	wxFileInputStream is(scriptname);
	if (!is.IsOk())
		return;
	wxFileConfig fconfig(is);

	int i;
	wxString str;

	//tasks
	if (fconfig.Exists("/tasks"))
	{
		fconfig.SetPath("/tasks");
		int tasknum = fconfig.Read("tasknum", 0l);
		for (i=0; i<tasknum; i++)
		{
			str = wxString::Format("/tasks/task%d", i);
			if (fconfig.Exists(str))
			{
				fconfig.SetPath(str);
				fconfig.Read("type", &str, "");
				if (str == "noise_reduction")
					RunNoiseReduction(fconfig);
				else if (str == "selection_tracking")
					RunSelectionTracking(fconfig);
				else if (str == "random_colors")
					RunRandomColors(fconfig);
				else if (str == "separate_channels")
					RunSeparateChannels(fconfig);
				else if (str == "external_exe")
					RunExternalExe(fconfig);
				else if (str == "fetch_mask")
					RunFetchMask(fconfig);
			}
		}
	}
}

void VRenderVulkanView::RunNoiseReduction(wxFileConfig &fconfig)
{
	wxString str;
	wxString pathname;
	double thresh, size;
	if (!m_cur_vol || !m_cur_vol->GetReader())
		return;
	fconfig.Read("threshold", &thresh, 0.0);
	thresh /= m_cur_vol->GetMaxValue();
	fconfig.Read("voxelsize", &size, 0.0);
	int mode;
	fconfig.Read("format", &mode, 0);
	bool bake;
	fconfig.Read("bake", &bake, false);
	bool compression;
	fconfig.Read("compress", &compression, false);
	fconfig.Read("savepath", &pathname, "");
	str = pathname;
	int64_t pos = 0;
	do
	{
		pos = pathname.find(GETSLASH(), pos);
		if (pos == wxNOT_FOUND)
			break;
		pos++;
		str = pathname.Left(pos);
		if (!wxDirExists(str))
			wxMkdir(str);
	} while (true);

	CompAnalysis(0.0, size, thresh, false, false, (int)size+1);
	Calculate(6, "", false);
	VolumeData* vd = m_calculator.GetResult();
	/*	m_selector.NoiseRemoval(size, thresh, 1);
	vector<VolumeData*> *vol_list = m_selector.GetResultVols();
	if(!vol_list || vol_list->empty()) return;
	VolumeData* vd = (*vol_list)[0];
	*/	if (vd)
	{
		int time_num = m_cur_vol->GetReader()->GetTimeNum();
		wxString format = wxString::Format("%d", time_num);
		m_fr_length = format.Length();
		format = wxString::Format("_T%%0%dd", m_fr_length);
		str = pathname + vd->GetName() +
			wxString::Format(format, m_tseq_cur_num) + (mode == 2 ? ".nrrd" : ".tif");
		vd->Save(str, mode, bake, compression, false, false);
		delete vd;
	}
	m_cur_vol->DeleteMask();
	m_cur_vol->DeleteLabel();
}

void VRenderVulkanView::RunSelectionTracking(wxFileConfig &fconfig)
{
	//read the size threshold
	int slimit;
	fconfig.Read("size_limit", &slimit, 0);
	//current state:
	//new data has been loaded in system memory
	//old data is in graphics memory
	//old mask in system memory is obsolete
	//new mask is in graphics memory
	//old label is in system memory
	//no label in graphics memory
	//steps:
	int ii, jj, kk;
	int nx, ny, nz;
	//return current mask (into system memory)
	if (!m_cur_vol)
		return;
	m_cur_vol->GetVR()->return_mask();
	m_cur_vol->GetResolution(nx, ny, nz);
	//find labels in the old that are selected by the current mask
	auto mask_nrrd = m_cur_vol->GetMask(true);
	if (!mask_nrrd || !mask_nrrd->getNrrd())
	{
		m_cur_vol->AddEmptyMask();
		mask_nrrd = m_cur_vol->GetMask(false);
	}
	auto label_nrrd = m_cur_vol->GetLabel(false);
	if (!label_nrrd || !label_nrrd->getNrrd())
		return;
	unsigned char* mask_data = (unsigned char*)(mask_nrrd->getNrrd()->data);
	if (!mask_data)
		return;
	unsigned int* label_data = (unsigned int*)(label_nrrd->getNrrd()->data);
	if (!label_data)
		return;
	FL::CellList sel_labels;
	FL::CellListIter label_iter;
	for (ii = 0; ii<nx; ii++)
		for (jj = 0; jj<ny; jj++)
			for (kk = 0; kk<nz; kk++)
			{
				int index = nx*ny*kk + nx*jj + ii;
				unsigned int label_value = label_data[index];
				if (mask_data[index] && label_value)
				{
					label_iter = sel_labels.find(label_value);
					if (label_iter == sel_labels.end())
					{
						FL::pCell cell(new FL::Cell(label_value));
						cell->SetSizeUi(1);
						sel_labels.insert(pair<unsigned int, FL::pCell>
							(label_value, cell));
					}
					else
						label_iter->second->Inc();
				}
			}
			//clean label list according to the size limit
			label_iter = sel_labels.begin();
			while (label_iter != sel_labels.end())
			{
				if (label_iter->second->GetSizeUi() < (unsigned int)slimit)
					label_iter = sel_labels.erase(label_iter);
				else
					++label_iter;
			}
			if (m_trace_group &&
				m_trace_group->GetTrackMap().GetFrameNum())
			{
				//create new id list
				m_trace_group->SetCurTime(m_tseq_cur_num);
				m_trace_group->SetPrvTime(m_tseq_prv_num);
				m_trace_group->UpdateCellList(sel_labels);
			}
			//load and replace the label
			BaseReader* reader = m_cur_vol->GetReader();
			if (!reader)
				return;
			wxString data_name = reader->GetCurName(m_tseq_cur_num, m_cur_vol->GetCurChannel());
			wxString label_name = data_name.Left(data_name.find_last_of('.')) + ".lbl";
			LBLReader lbl_reader;
			wstring lblname = label_name.ToStdWstring();
			lbl_reader.SetFile(lblname);
			auto label_nrrd_new = lbl_reader.Convert(m_tseq_cur_num, m_cur_vol->GetCurChannel(), true);
			if (!label_nrrd_new || !label_nrrd_new->getNrrd())
			{
				m_cur_vol->AddEmptyLabel();
				label_nrrd_new = m_cur_vol->GetLabel(false);
			}
			else
				m_cur_vol->LoadLabel(label_nrrd_new);
			label_data = (unsigned int*)(label_nrrd_new->getNrrd()->data);
			if (!label_data)
				return;
			//update the mask according to the new label
			memset((void*)mask_data, 0, sizeof(uint8)*nx*ny*nz);
			for (ii = 0; ii<nx; ii++)
				for (jj = 0; jj<ny; jj++)
					for (kk = 0; kk<nz; kk++)
					{
						int index = nx*ny*kk + nx*jj + ii;
						unsigned int label_value = label_data[index];
						if (m_trace_group &&
							m_trace_group->GetTrackMap().GetFrameNum())
						{
							if (m_trace_group->FindCell(label_value))
								mask_data[index] = 255;
						}
						else
						{
							label_iter = sel_labels.find(label_value);
							if (label_iter != sel_labels.end())
								mask_data[index] = 255;
						}
					}

					//add traces to trace dialog
					VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
					if (m_vrv && vr_frame && vr_frame->GetTraceDlg())
						vr_frame->GetTraceDlg()->GetSettings(m_vrv);
}

void VRenderVulkanView::RunRandomColors(wxFileConfig &fconfig)
{
	int hmode;
	fconfig.Read("huemode", &hmode, 1);
	wxString str, pathname;
	fconfig.Read("savepath", &pathname, "");
	str = pathname;
	int64_t pos = 0;
	do
	{
		pos = pathname.find(GETSLASH(), pos);
		if (pos == wxNOT_FOUND)
			break;
		pos++;
		str = pathname.Left(pos);
		if (!wxDirExists(str))
			wxMkdir(str);
	} while (true);

	//current state: see selection_tracking
	//steps:
	//load and replace the label
	if (!m_cur_vol)
		return;
	BaseReader* reader = m_cur_vol->GetReader();
	if (!reader)
		return;
	wxString data_name = reader->GetCurName(m_tseq_cur_num, m_cur_vol->GetCurChannel());
	wxString label_name = data_name.Left(data_name.find_last_of('.')) + ".lbl";
	LBLReader lbl_reader;
	wstring lblname = label_name.ToStdWstring();
	lbl_reader.SetFile(lblname);
	auto label_nrrd_new = lbl_reader.Convert(m_tseq_cur_num, m_cur_vol->GetCurChannel(), true);
	if (!label_nrrd_new)
		return;
	m_cur_vol->LoadLabel(label_nrrd_new);
	int time_num = m_cur_vol->GetReader()->GetTimeNum();
	//generate RGB volumes
	m_selector.CompExportRandomColor(hmode, 0, 0, 0, false, false);
	vector<VolumeData*> *vol_list = m_selector.GetResultVols();
	for (int ii = 0; ii<(int)vol_list->size(); ii++)
	{
		VolumeData* vd = (*vol_list)[ii];
		if (vd)
		{
			wxString format = wxString::Format("%d", time_num);
			m_fr_length = format.Length();
			format = wxString::Format("_T%%0%dd", m_fr_length);
			str = pathname + vd->GetName() +
				wxString::Format(format, m_tseq_cur_num) +
				wxString::Format("_COMP%d", ii + 1) + ".tif";
			vd->Save(str);
			delete vd;
		}
	}
}

void VRenderVulkanView::RunSeparateChannels(wxFileConfig &fconfig)
{
	wxString str, pathname;
	int mode;
	fconfig.Read("format", &mode, 0);
	bool bake;
	fconfig.Read("bake", &bake, false);
	bool compression;
	fconfig.Read("compress", &compression, false);
	fconfig.Read("savepath", &pathname, "");
	str = pathname;
	int64_t pos = 0;
	do
	{
		pos = pathname.find(GETSLASH(), pos);
		if (pos == wxNOT_FOUND)
			break;
		pos++;
		str = pathname.Left(pos);
		if (!wxDirExists(str))
			wxMkdir(str);
	} while (true);

	if (wxDirExists(pathname))
	{
		VolumeData* vd = m_selector.GetVolume();
		if (vd)
		{
			int time_num = vd->GetReader()->GetTimeNum();
			wxString format = wxString::Format("%d", time_num);
			m_fr_length = format.Length();
			format = wxString::Format("_T%%0%dd", m_fr_length);
			str = pathname + vd->GetName() +
				wxString::Format(format, m_tseq_cur_num) + (mode == 2 ? ".nrrd" : ".tif");;
			vd->Save(str, mode, bake, compression);
		}
	}
}

void VRenderVulkanView::RunExternalExe(wxFileConfig &fconfig)
{
	/*	wxString pathname;
	fconfig.Read("exepath", &pathname);
	if (!wxFileExists(pathname))
	return;
	VolumeData* vd = m_cur_vol;
	if (!vd)
	return;
	BaseReader* reader = vd->GetReader();
	if (!reader)
	return;
	wxString data_name = reader->GetCurName(m_tseq_cur_num, vd->GetCurChannel());

	vector<string> args;
	args.push_back(pathname.ToStdString());
	args.push_back(data_name.ToStdString());
	boost::process::context ctx;
	ctx.stdout_behavior = boost::process::silence_stream();
	boost::process::child c =
	boost::process::launch(pathname.ToStdString(),
	args, ctx);
	c.wait();
	*/
}

void VRenderVulkanView::RunFetchMask(wxFileConfig &fconfig)
{
	//load and replace the mask
	if (!m_cur_vol)
		return;
	BaseReader* reader = m_cur_vol->GetReader();
	if (!reader)
		return;
	wxString data_name = reader->GetCurName(m_tseq_cur_num, m_cur_vol->GetCurChannel());
	wxString mask_name = data_name.Left(data_name.find_last_of('.')) + ".msk";
	MSKReader msk_reader;
	wstring mskname = mask_name.ToStdWstring();
	msk_reader.SetFile(mskname);
	auto mask_nrrd_new = msk_reader.Convert(m_tseq_cur_num, m_cur_vol->GetCurChannel(), true);
	if (mask_nrrd_new)
		m_cur_vol->LoadMask(mask_nrrd_new);

	//load and replace the label
	wxString label_name = data_name.Left(data_name.find_last_of('.')) + ".lbl";
	LBLReader lbl_reader;
	wstring lblname = label_name.ToStdWstring();
	lbl_reader.SetFile(lblname);
	auto label_nrrd_new = lbl_reader.Convert(m_tseq_cur_num, m_cur_vol->GetCurChannel(), true);
	if (label_nrrd_new)
		m_cur_vol->LoadLabel(label_nrrd_new);
}

//draw
void VRenderVulkanView::OnDraw(wxPaintEvent& event)
{
    UpdateScreen();
}

void VRenderVulkanView::UpdateScreen()
{
	m_nx = GetSize().x;
	m_ny = GetSize().y;
	if (m_tile_rendering) {
		m_nx = m_tile_w;
		m_ny = m_tile_h;
	}

	int nx = m_nx;
	int ny = m_ny;

	if (nx <= 0 || ny <= 0)
	{
		return;
	}
	
	if(!m_vulkan)
	{
		InitVulkan();
		return;
	}

	if (m_abort)
	{
		m_abort = false;
		return;
	}

	Init();
	//wxPaintDC dc(this);
    
    SetEvtHandlerEnabled(false);

	if (m_recording)
		m_recording_frame = true;
	
	m_vulkan->ResetRenderSemaphores();
	m_vulkan->prepareFrame();
	m_frame_clear = true;

	PopMeshList();

	m_draw_type = 1;

	PreDraw();

	Draw();

	if (m_draw_camctr)
		DrawCamCtr();

	if (m_draw_legend)
		DrawLegend();

	if (m_disp_scale_bar)
		DrawScaleBar();

	DrawColormap();

	PostDraw();

	//draw frame after capture
	if (m_draw_frame)
		DrawFrame();

	//draw info
	if (m_draw_info)
		DrawInfo(nx, ny);

	if (m_int_mode == 2 ||
		m_int_mode == 7)  //painting mode
	{
		if (m_draw_brush)
		{
			DrawBrush();
		}
		if (m_paint_enable || m_clear_paint)
		{
			//paiting mode
			//for volume segmentation
			PaintStroke();
		}
		if (m_paint_enable && m_paint_display)
		{
			//show the paint strokes
			DisplayStroke();
		}
	}
	else if (m_clear_paint)
	{
		PaintStroke();
	}
    
    if ( m_recording &&
		(VolumeRenderer::get_done_update_loop() || !VolumeRenderer::get_mem_swap()) )
    {
        vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
        vks::VulkanDevice* prim_dev = m_vulkan->devices[0];
        
        if (m_fbo_record && (m_fbo_record->w != nx || m_fbo_record->h != ny))
        {
            m_fbo_record.reset();
            m_tex_record.reset();
        }
        if (!m_fbo_record)
        {
            m_fbo_record = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
            m_fbo_record->w = m_nx;
            m_fbo_record->h = m_ny;
            m_fbo_record->device = prim_dev;
            
            m_tex_record = prim_dev->GenTexture2D(current_fbo->attachments[0]->format, VK_FILTER_LINEAR, m_nx, m_ny);
            m_fbo_record->addAttachment(m_tex_record);
        }
        
        Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
        params.pipeline =
        m_v2drender->preparePipeline(
                                     IMG_SHADER_TEXTURE_LOOKUP,
                                     V2DRENDER_BLEND_DISABLE,
                                     m_fbo_record->attachments[0]->format,
                                     m_fbo_record->attachments.size(),
                                     0,
                                     m_fbo_record->attachments[0]->is_swapchain_images);
        params.tex[0] = current_fbo->attachments[0].get();
        params.clear = true;
        
        if (!m_fbo_record->renderPass)
            m_fbo_record->replaceRenderPass(params.pipeline.pass);
        
        m_v2drender->render(m_fbo_record, params);
    }
	
	m_vulkan->submitFrame();

	if ( m_recording &&
		(VolumeRenderer::get_done_update_loop() || !VolumeRenderer::get_mem_swap()) )
	{
		m_recording_frame = false;
	}


	if (m_tile_rendering && m_current_tileid >= m_tile_xnum * m_tile_ynum) {
		EndTileRendering();
	}

	if (m_int_mode == 4)
		m_int_mode = 2;
	if (m_int_mode == 8)
		m_int_mode = 7;

	goTimer->sample();

	if (m_resize)
		m_resize = false;

	SetEvtHandlerEnabled(true);
}

void VRenderVulkanView::DownloadRecordedFrame(void *image, VkFormat &format)
{
    if (!m_recording)
        return;
    
    vkQueueWaitIdle(m_vulkan->vulkanDevice->queue);
    m_vulkan->vulkanDevice->DownloadTexture(m_tex_record, image);
    format = m_tex_record->format;
}

void VRenderVulkanView::SetRadius(double r)
{
	m_radius = r;
}

void VRenderVulkanView::SetCenter()
{
	InitView(INIT_BOUNDS|INIT_CENTER|INIT_OBJ_TRANSL);

	VolumeData *vd = 0;
    BBox bounds;
    
	if (m_cur_vol)
    {
		vd = m_cur_vol;
        if (vd)
        {
            if (!vd->GetTexture() || !vd->GetVR())
                return;
            Transform *tform = vd->GetTexture()->transform();
            if (!tform)
                return;
            vector<Plane*> *planes = vd->GetVR()->get_planes();
            if (!planes || planes->size() != 6)
                return;
            
            double params[6];
            for (int j = 0; j < 6; j++)
                params[j] = (*planes)[j]->GetParam();
            
            Transform tform_copy;
            double mvmat[16];
            tform->get_trans(mvmat);
            swap(mvmat[3], mvmat[12]);
            swap(mvmat[7], mvmat[13]);
            swap(mvmat[11], mvmat[14]);
            tform_copy.set(mvmat);
            
            FLIVR::Point p[8] = {
                FLIVR::Point(params[0],       params[2],       params[4]),
                FLIVR::Point(1.0 - params[1], params[2],       params[4]),
                FLIVR::Point(1.0 - params[1], 1.0 - params[3], params[4]),
                FLIVR::Point(params[0],       1.0 - params[3], params[4]),
                FLIVR::Point(params[0],       params[2],       1.0 - params[5]),
                FLIVR::Point(1.0 - params[1], params[2],       1.0 - params[5]),
                FLIVR::Point(1.0 - params[1], 1.0 - params[3], 1.0 - params[5]),
                FLIVR::Point(params[0],       1.0 - params[3], 1.0 - params[5])
            };
            
            double rotx, roty, rotz;
            Quaternion q_cl;
            m_q_cl.ToEuler(rotx, roty, rotz);
            q_cl.FromEuler(-rotx, -roty, rotz);
            
            for (int j = 0; j < 8; j++)
            {
                p[j] = tform_copy.project(p[j]);
                Quaternion p2(p[j].x() - m_obj_ctrx, p[j].y() - m_obj_ctry, p[j].z() - m_obj_ctrz, 0.0);
                Quaternion p3 = q_cl * p2 * (-q_cl);
                p[j] = Point(p3.x + m_obj_ctrx, p3.y + m_obj_ctry, p3.z + m_obj_ctrz);
                bounds.extend(p[j]);
            }
            
            m_obj_transx += (bounds.min().x() + bounds.max().x()) / 2.0 - m_obj_ctrx;
            m_obj_transy += (bounds.min().y() + bounds.max().y()) / 2.0 - m_obj_ctry;
            m_obj_transz -= (bounds.min().z() + bounds.max().z()) / 2.0 - m_obj_ctrz;
            
            //SetSortBricks();
            
            RefreshGL();
        }
    }
	else
    {
        for (auto vd : m_vd_pop_list)
        {
            if (vd)
            {
                if (!vd->GetTexture() || !vd->GetVR())
                    return;
                Transform *tform = vd->GetTexture()->transform();
                if (!tform)
                    return;
                vector<Plane*> *planes = vd->GetVR()->get_planes();
                if (!planes || planes->size() != 6)
                    return;
                
                double params[6];
                for (int j = 0; j < 6; j++)
                    params[j] = (*planes)[j]->GetParam();
                
                Transform tform_copy;
                double mvmat[16];
                tform->get_trans(mvmat);
                swap(mvmat[3], mvmat[12]);
                swap(mvmat[7], mvmat[13]);
                swap(mvmat[11], mvmat[14]);
                tform_copy.set(mvmat);
                
                FLIVR::Point p[8] = {
                    FLIVR::Point(params[0],       params[2],       params[4]),
                    FLIVR::Point(1.0 - params[1], params[2],       params[4]),
                    FLIVR::Point(1.0 - params[1], 1.0 - params[3], params[4]),
                    FLIVR::Point(params[0],       1.0 - params[3], params[4]),
                    FLIVR::Point(params[0],       params[2],       1.0 - params[5]),
                    FLIVR::Point(1.0 - params[1], params[2],       1.0 - params[5]),
                    FLIVR::Point(1.0 - params[1], 1.0 - params[3], 1.0 - params[5]),
                    FLIVR::Point(params[0],       1.0 - params[3], 1.0 - params[5])
                };
                
                double rotx, roty, rotz;
                Quaternion q_cl;
                m_q_cl.ToEuler(rotx, roty, rotz);
                q_cl.FromEuler(-rotx, -roty, rotz);
                
                for (int j = 0; j < 8; j++)
                {
                    p[j] = tform_copy.project(p[j]);
                    Quaternion p2(p[j].x() - m_obj_ctrx, p[j].y() - m_obj_ctry, p[j].z() - m_obj_ctrz, 0.0);
                    Quaternion p3 = q_cl * p2 * (-q_cl);
                    p[j] = Point(p3.x + m_obj_ctrx, p3.y + m_obj_ctry, p3.z + m_obj_ctrz);
                    bounds.extend(p[j]);
                }
            }
        }
        for (auto md : m_md_pop_list)
            bounds.extend(md->GetBounds());
        
        m_obj_transx += (bounds.min().x() + bounds.max().x()) / 2.0 - m_obj_ctrx;
        m_obj_transy += (bounds.min().y() + bounds.max().y()) / 2.0 - m_obj_ctry;
        m_obj_transz -= (bounds.min().z() + bounds.max().z()) / 2.0 - m_obj_ctrz;
        
        //SetSortBricks();
        
        RefreshGL();
    }
}

void VRenderVulkanView::SetScale121()
{
	//SetCenter();

	int nx = GetSize().x;
	int ny = GetSize().y;
	double aspect = (double)nx / (double)ny;

	double spc_x = 1.0;
	double spc_y = 1.0;
	double spc_z = 1.0;
	VolumeData *vd = 0;
	if (m_cur_vol)
		vd = m_cur_vol;
	else if (m_vd_pop_list.size())
		vd = m_vd_pop_list[0];
	if (vd)
		vd->GetSpacings(spc_x, spc_y, spc_z);
	spc_x = spc_x<EPS?1.0:spc_x;
	spc_y = spc_y<EPS?1.0:spc_y;

	if (aspect > 1.0)
		m_scale_factor = 2.0*m_radius/spc_y/double(ny);
	else
		m_scale_factor = 2.0*m_radius/spc_x/double(nx);

	wxString str = wxString::Format("%.0f", m_scale_factor*100.0);
	m_vrv->m_scale_factor_sldr->SetValue(m_scale_factor*100);
	m_vrv->m_scale_factor_text->ChangeValue(str);

	//SetSortBricks();

	RefreshGL();
}

void VRenderVulkanView::SetPersp(bool persp)
{
	m_persp = persp;
	if (m_free && !m_persp)
	{
		m_free = false;

		//restore camera translation
		m_transx = m_transx_saved;
		m_transy = m_transy_saved;
		m_transz = m_transz_saved;
		m_ctrx = m_ctrx_saved;
		m_ctry = m_ctry_saved;
		m_ctrz = m_ctrz_saved;
		//restore camera rotation
		m_rotx = m_rotx_saved;
		m_roty = m_roty_saved;
		m_rotz = m_rotz_saved;
		//restore object translation
		m_obj_transx = m_obj_transx_saved;
		m_obj_transy = m_obj_transy_saved;
		m_obj_transz = m_obj_transz_saved;
		//restore scale factor
		m_scale_factor = m_scale_factor_saved;
		wxString str = wxString::Format("%.0f", m_scale_factor*100.0);
		m_vrv->m_scale_factor_sldr->SetValue(m_scale_factor*100);
		m_vrv->m_scale_factor_text->ChangeValue(str);
		m_vrv->m_free_chk->SetValue(false);

		SetRotations(m_rotx, m_roty, m_rotz);
	}
	//SetSortBricks();
}

void VRenderVulkanView::SetFree(bool free)
{
	m_free = free;
	if (m_vrv->m_free_chk->GetValue() != m_free)
		m_vrv->m_free_chk->SetValue(m_free);
	if (free)
	{
		m_persp = true;
		Vector pos(m_transx, m_transy, m_transz);
		Vector d = pos;
		d.normalize();
		Vector ctr;
		ctr = pos - m_radius*0.01*d;
		m_ctrx = ctr.x();
		m_ctry = ctr.y();
		m_ctrz = ctr.z();

		//save camera translation
		m_transx_saved = m_transx;
		m_transy_saved = m_transy;
		m_transz_saved = m_transz;
		m_ctrx_saved = m_ctrx;
		m_ctry_saved = m_ctry;
		m_ctrz_saved = m_ctrz;
		//save camera rotation
		m_rotx_saved = m_rotx;
		m_roty_saved = m_roty;
		m_rotz_saved = m_rotz;
		//save object translateion
		m_obj_transx_saved = m_obj_transx;
		m_obj_transy_saved = m_obj_transy;
		m_obj_transz_saved = m_obj_transz;
		//save scale factor
		m_scale_factor_saved = m_scale_factor;
		m_vrv->m_aov_text->ChangeValue(wxString::Format("%d",int(m_aov)));
		m_vrv->m_aov_sldr->SetValue(m_aov);
	}
	else
	{
		m_ctrx = m_ctry = m_ctrz = 0.0;

		//restore camera translation
		m_transx = m_transx_saved;
		m_transy = m_transy_saved;
		m_transz = m_transz_saved;
		m_ctrx = m_ctrx_saved;
		m_ctry = m_ctry_saved;
		m_ctrz = m_ctrz_saved;
		//restore camera rotation
		m_rotx = m_rotx_saved;
		m_roty = m_roty_saved;
		m_rotz = m_rotz_saved;
		//restore object translation
		m_obj_transx = m_obj_transx_saved;
		m_obj_transy = m_obj_transy_saved;
		m_obj_transz = m_obj_transz_saved;
		//restore scale factor
		m_scale_factor = m_scale_factor_saved;
		wxString str = wxString::Format("%.0f", m_scale_factor*100.0);
		m_vrv->m_scale_factor_sldr->SetValue(m_scale_factor*100);
		m_vrv->m_scale_factor_text->ChangeValue(str);

		SetRotations(m_rotx, m_roty, m_rotz);
	}
	//SetSortBricks();
}

void VRenderVulkanView::SetAov(double aov)
{
	//view has been changed, sort bricks
	//SetSortBricks();

	m_aov = aov;
	if (m_persp)
	{
		m_vrv->m_aov_text->ChangeValue(wxString::Format("%d", int(m_aov)));
		m_vrv->m_aov_sldr->SetValue(int(m_aov));
	}
}

void VRenderVulkanView::SetMinPPI(double ppi)
{
	m_min_ppi = ppi;
	m_vrv->m_ppi_text->ChangeValue(wxString::Format("%d", int(m_min_ppi)));
	m_vrv->m_ppi_sldr->SetValue(int(m_min_ppi));
}

void VRenderVulkanView::SetVolMethod(int method)
{
	//get the volume list m_vd_pop_list
	PopVolumeList();

	m_vol_method = method;

	//ui
	m_vrv->m_volume_seq_rd->SetValue(false);
	m_vrv->m_volume_multi_rd->SetValue(false);
	m_vrv->m_volume_comp_rd->SetValue(false);
	switch (m_vol_method)
	{
	case VOL_METHOD_SEQ:
		m_vrv->m_volume_seq_rd->SetValue(true);
		break;
	case VOL_METHOD_MULTI:
		m_vrv->m_volume_multi_rd->SetValue(true);
		break;
	case VOL_METHOD_COMP:
		m_vrv->m_volume_comp_rd->SetValue(true);
		break;
	}
}

VolumeData* VRenderVulkanView::GetAllVolumeData(int index)
{
	int cnt = 0;
	int i, j;
	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 2:  //volume data
			if (cnt == index)
				return (VolumeData*)m_layer_list[i];
			cnt++;
			break;
		case 5:  //group
			{
				DataGroup* group = (DataGroup*)m_layer_list[i];
				if (!group)
					break;
				for (j=0; j<group->GetVolumeNum(); j++)
				{
					if (cnt == index)
						return group->GetVolumeData(j);
					cnt++;
				}
			}
			break;
		}
	}
	return 0;
}

MeshData* VRenderVulkanView::GetAllMeshData(int index)
{
    int cnt = 0;
    int i, j;
    for (i=0; i<(int)m_layer_list.size(); i++)
    {
        if (!m_layer_list[i])
            continue;
        switch (m_layer_list[i]->IsA())
        {
            case 3:  //mesh data
                if (cnt == index)
                    return (MeshData*)m_layer_list[i];
                cnt++;
                break;
            case 6:  //mesh group
            {
                MeshGroup* group = (MeshGroup*)m_layer_list[i];
                if (!group)
                    break;
                for (j=0; j<group->GetMeshNum(); j++)
                {
                    if (cnt == index)
                        return group->GetMeshData(j);
                    cnt++;
                }
            }
                break;
        }
    }
    return 0;
}

VolumeData* VRenderVulkanView::GetDispVolumeData(int index)
{
	if (GetDispVolumeNum()<=0)
		return 0;

	//get the volume list m_vd_pop_list
	PopVolumeList();

	if (index>=0 && index<(int)m_vd_pop_list.size())
		return m_vd_pop_list[index];
	else
		return 0;
}

MeshData* VRenderVulkanView::GetMeshData(int index)
{
	if (GetMeshNum()<=0)
		return 0;

	PopMeshList();

	if (index>=0 && index<(int)m_md_pop_list.size())
		return m_md_pop_list[index];
	else
		return 0;
}

VolumeData* VRenderVulkanView::GetVolumeData(wxString &name)
{
	int i, j;

	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 2://volume data
			{
				VolumeData* vd = (VolumeData*)m_layer_list[i];
				if (vd && vd->GetName() == name)
					return vd;
			}
			break;
		case 5://group
			{
				DataGroup* group = (DataGroup*)m_layer_list[i];
				if (!group)
					break;
				for (j=0; j<group->GetVolumeNum(); j++)
				{
					VolumeData* vd = group->GetVolumeData(j);
					if (vd && vd->GetName() == name)
						return vd;
				}
			}
			break;
		}
	}

	return 0;
}

MeshData* VRenderVulkanView::GetMeshData(wxString &name)
{
	int i, j;

	for (i=0 ; i<(int)m_layer_list.size() ; i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 3://mesh data
			{
				MeshData* md = (MeshData*)m_layer_list[i];
				if (md && md->GetName()== name)
					return md;
			}
			break;
		case 6://mesh group
			{
				MeshGroup* group = (MeshGroup*)m_layer_list[i];
				if (!group) continue;
				for (j=0; j<group->GetMeshNum(); j++)
				{
					MeshData* md = group->GetMeshData(j);
					if (md && md->GetName()==name)
						return md;
				}
			}
			break;
		}
	}
	return 0;
}

Annotations* VRenderVulkanView::GetAnnotations(wxString &name)
{
	int i;

	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 4://annotations
			{
				Annotations* ann = (Annotations*)m_layer_list[i];
				if (ann && ann->GetName()==name)
					return ann;
			}
		}
	}
	return 0;
}

DataGroup* VRenderVulkanView::GetGroup(wxString &name)
{
	int i;

	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 5://group
			{
				DataGroup* group = (DataGroup*)m_layer_list[i];
				if (group && group->GetName()==name)
					return group;
			}
		}
	}
	return 0;
}

DataGroup* VRenderVulkanView::GetParentGroup(wxString& name)
{
	int i, j;

	for (i = 0; i < (int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 5://group
		{
			DataGroup* group = (DataGroup*)m_layer_list[i];
			if (!group)
				break;
			for (j = 0; j < group->GetVolumeNum(); j++)
			{
				VolumeData* vd = group->GetVolumeData(j);
				if (vd && vd->GetName() == name)
					return group;
			}
		}
		break;
		}
	}

	return 0;
}

MeshGroup* VRenderVulkanView::GetParentMGroup(wxString& name)
{
    int i, j;

    for (i = 0; i < (int)m_layer_list.size(); i++)
    {
        if (!m_layer_list[i])
            continue;
        switch (m_layer_list[i]->IsA())
        {
        case 6://group
        {
            MeshGroup* group = (MeshGroup*)m_layer_list[i];
            if (!group)
                break;
            for (j = 0; j < group->GetMeshNum(); j++)
            {
                MeshData* md = group->GetMeshData(j);
                if (md && md->GetName() == name)
                    return group;
            }
        }
        break;
        }
    }

    return 0;
}

int VRenderVulkanView::GetAny()
{
	PopVolumeList();
	PopMeshList();
	return m_vd_pop_list.size() + m_md_pop_list.size();
}

int VRenderVulkanView::GetDispVolumeNum()
{
	//get the volume list m_vd_pop_list
	PopVolumeList();

	return m_vd_pop_list.size();
}

int VRenderVulkanView::GetAllVolumeNum()
{
	int num = 0;
	for (int i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 2:  //volume data
			num++;
			break;
		case 5:  //group
			{
				DataGroup* group = (DataGroup*)m_layer_list[i];
				num += group->GetVolumeNum();
			}
			break;
		}
	}
	return num;
}

int VRenderVulkanView::GetAllMeshNum()
{
    int num = 0;
    for (int i=0; i<(int)m_layer_list.size(); i++)
    {
        if (!m_layer_list[i])
            continue;
        switch (m_layer_list[i]->IsA())
        {
            case 3:  //mesh data
                num++;
                break;
            case 6:  //mesh group
            {
                MeshGroup* group = (MeshGroup*)m_layer_list[i];
                num += group->GetMeshNum();
            }
                break;
        }
    }
    return num;
}

int VRenderVulkanView::GetMeshNum()
{
	PopMeshList();
	return m_md_pop_list.size();
}

DataGroup* VRenderVulkanView::GetCurrentVolGroup()
{
	int i;
	DataGroup* group = 0;
	DataGroup* cur_group = 0;

	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		TreeLayer* layer = m_layer_list[i];
		if (layer && layer->IsA() == 5)
		{
			//layer is group
			group = (DataGroup*) layer;
			if (group)
			{
				for (int j = 0; j < group->GetVolumeNum(); j++)
				{
					if (m_cur_vol && m_cur_vol == group->GetVolumeData(j))
						cur_group = group;
				}
			}
		}
	}

	return cur_group != NULL ? cur_group : group;
}

DataGroup* VRenderVulkanView::AddVolumeData(VolumeData* vd, wxString group_name)
{
	//m_layer_list.push_back(vd);
	int i;
	DataGroup* group = 0;
	DataGroup* group_temp = 0;
	DataGroup* cur_group = 0;

	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		TreeLayer* layer = m_layer_list[i];
		if (layer && layer->IsA() == 5)
		{
			//layer is group
			group_temp = (DataGroup*) layer;
			if (group_temp && group_temp->GetName()==group_name)
			{
				group = group_temp;
				break;
			}
			if (group_temp)
			{
				for (int j = 0; j < group_temp->GetVolumeNum(); j++)
				{
					if (m_cur_vol && m_cur_vol == group_temp->GetVolumeData(j))
						cur_group = group_temp;
				}
			}
		}
	}

	if (!group && cur_group)
		group = cur_group;

	if (!group && group_temp)
		group = group_temp;

	if (!group)
	{
		wxString group_name = AddGroup("");
		group = GetGroup(group_name);
		if (!group)
			return 0;
	}
	/*
	for (i=0; i<1; i++)
	{
	VolumeData* vol_data = group->GetVolumeData(i);
	if (vol_data)
	{
	double spcx, spcy, spcz;
	vol_data->GetSpacings(spcx, spcy, spcz);
	vd->SetSpacings(spcx, spcy, spcz);
	}
	}
	*/
	group->InsertVolumeData(group->GetVolumeNum()-1, vd);

	if (group && vd)
	{
		Color gamma = group->GetGamma();
		vd->SetGamma(gamma);
		Color brightness = group->GetBrightness();
		vd->SetBrightness(brightness);
		Color hdr = group->GetHdr();
		vd->SetHdr(hdr);
        Color level = group->GetLevels();
        vd->SetLevels(level);
		bool sync_r = group->GetSyncR();
		vd->SetSyncR(sync_r);
		bool sync_g = group->GetSyncG();
		vd->SetSyncG(sync_g);
		bool sync_b = group->GetSyncB();
		vd->SetSyncB(sync_b);

		if ((group->GetVolumeSyncSpc() || group->GetVolumeSyncProp()) && group->GetVolumeNum() > 0)
		{
			double spcx=1.0, spcy=1.0, spcz=1.0;
			group->GetVolumeData(0)->GetSpacings(spcx, spcy, spcz, 0);
			vd->SetSpacings(spcx, spcy, spcz);
		}

		if (group->GetVolumeSyncProp() && group->GetVolumeData(0) != vd)
		{
			VolumeData *srcvd = group->GetVolumeData(0);
			double dval = 0.0;
			double dval2 = 0.0;
			vd->Set3DGamma(srcvd->Get3DGamma());
			vd->SetBoundary(srcvd->GetBoundary());
			vd->SetOffset(srcvd->GetOffset());
			vd->SetLeftThresh(srcvd->GetLeftThresh());
			vd->SetRightThresh(srcvd->GetRightThresh());
			vd->SetLuminance(srcvd->GetLuminance());
			vd->SetShadow(srcvd->GetShadow());
			srcvd->GetShadowParams(dval);
			vd->SetShadowParams(dval);
			vd->SetShading(srcvd->GetShading());
			double amb, diff, spec, shine;
			srcvd->GetMaterial(amb, diff, spec, shine);
			vd->SetMaterial(amb, diff, spec, shine);
			vd->SetAlpha(srcvd->GetAlpha());
			vd->SetSampleRate(srcvd->GetSampleRate());
			vd->SetShading(srcvd->GetShading());
			vd->SetColormap(srcvd->GetColormap());
			srcvd->GetColormapValues(dval, dval2);
			vd->SetColormapValues(dval, dval2);
			vd->SetInvert(srcvd->GetInvert());
			vd->SetMode(srcvd->GetMode());
			vd->SetNR(srcvd->GetNR());
		}

		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			vr_frame->GetAdjustView()->SetVolumeData(vd);
			vr_frame->GetAdjustView()->SetGroupLink(group);

			VolumeData* cvd = vr_frame->GetClippingView()->GetVolumeData();
			MeshData* cmd = vr_frame->GetClippingView()->GetMeshData();
			vector<Plane*> *src_planes = 0;
			if (cmd && cmd->GetMR())
				src_planes = cmd->GetMR()->get_planes();
			else if (cvd && cvd->GetVR())
				src_planes = cvd->GetVR()->get_planes();

			if (vr_frame->GetClippingView()->GetChannLink() && group->GetVolumeData(0) != vd && src_planes)
			{
                CalcAndSetCombinedClippingPlanes();
                A2Q();
			}
		}
	}

	m_vd_pop_dirty = true;

	if (vd->isBrxml())
	{
		m_loader.SetMemoryLimitByte((long long)TextureRenderer::mainmem_buf_size_*1024LL*1024LL);
		m_loader.PreloadLevel(vd, vd->GetMaskLv(), true);
	}

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (m_frame)
	{
		AdjustView* adjust_view = vr_frame->GetAdjustView();
		if (adjust_view)
		{
			adjust_view->SetGroupLink(group);
			adjust_view->UpdateSync();
		}
	}

	InitView(INIT_BOUNDS | INIT_CENTER);

	return group;
}

void VRenderVulkanView::AddMeshData(MeshData* md)
{
	if (!md)
		return;

	m_layer_list.push_back(md);
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame && vr_frame->GetClippingView())
	{
		VolumeData* cvd = vr_frame->GetClippingView()->GetVolumeData();
		MeshData* cmd = vr_frame->GetClippingView()->GetMeshData();
		vector<Plane*> *src_planes = 0;
		if (cmd && cmd->GetMR())
			src_planes = cmd->GetMR()->get_planes();
		else if (cvd && cvd->GetVR())
			src_planes = cvd->GetVR()->get_planes();

		if (src_planes && vr_frame->GetClippingView()->GetChannLink())
		{
			CalcAndSetCombinedClippingPlanes();
            A2Q();
		}
	}
	m_md_pop_dirty = true;

	InitView(INIT_BOUNDS | INIT_CENTER);
}

void VRenderVulkanView::AddAnnotations(Annotations* ann)
{
	bool exist = false;
	for (auto layer : m_layer_list)
		if (layer->IsA() == 4 && ann == (Annotations*)layer)
			exist = true;
	if (!exist)
    {
        m_layer_list.push_back(ann);
        VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
        if (vr_frame && vr_frame->GetClippingView())
        {
            VolumeData* cvd = vr_frame->GetClippingView()->GetVolumeData();
            MeshData* cmd = vr_frame->GetClippingView()->GetMeshData();
            vector<Plane*> *src_planes = 0;
            if (cmd && cmd->GetMR())
                src_planes = cmd->GetMR()->get_planes();
            else if (cvd && cvd->GetVR())
                src_planes = cvd->GetVR()->get_planes();

            if (src_planes && vr_frame->GetClippingView()->GetChannLink())
            {
                CalcAndSetCombinedClippingPlanes();
                A2Q();
            }
        }
    }
}

void VRenderVulkanView::ReplaceVolumeData(wxString &name, VolumeData *dst)
{
	int i, j;

	bool found = false;
	DataGroup* group = 0;
	
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager *dm = vr_frame->GetDataManager();
	if (!dm) return;

	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 2://volume data
			{
				VolumeData* vd = (VolumeData*)m_layer_list[i];
				if (vd && vd->GetName() == name)
				{
					if (m_cur_vol == vd) m_cur_vol = dst;
					m_loader.RemoveDataVD(vd);
					vd->GetVR()->clear_tex_current();
					m_layer_list[i] = dst;
					m_vd_pop_dirty = true;
					found = true;
                    wxString vname = dst->GetName();
                    int vid = dm->GetVolumeIndex(vname);
                    if (vid >= 0)
                        dm->ReplaceVolumeData(vid, dst);
					break;
				}
			}
			break;
		case 5://group
			{
				DataGroup* tmpgroup = (DataGroup*)m_layer_list[i];
				for (j=0; j<tmpgroup->GetVolumeNum(); j++)
				{
					VolumeData* vd = tmpgroup->GetVolumeData(j);
					if (vd && vd->GetName() == name)
					{
						if (m_cur_vol == vd) m_cur_vol = dst;
						m_loader.RemoveDataVD(vd);
						vd->GetVR()->clear_tex_current();
						tmpgroup->ReplaceVolumeData(j, dst);
						m_vd_pop_dirty = true;
						found = true;
						group = tmpgroup;
                        wxString vname = dst->GetName();
                        int vid = dm->GetVolumeIndex(vname);
                        if (vid >= 0)
                            dm->ReplaceVolumeData(vid, dst);
						break;
					}
				}
			}
			break;
		}
	}

	if (found)
	{
		if (dst->isBrxml())
		{
			m_loader.SetMemoryLimitByte((long long)TextureRenderer::mainmem_buf_size_*1024LL*1024LL);
			m_loader.PreloadLevel(dst, dst->GetMaskLv(), true);
		}
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			AdjustView* adjust_view = vr_frame->GetAdjustView();
			if (adjust_view)
			{
				adjust_view->SetVolumeData(dst);
				if (!group) adjust_view->SetGroupLink(group);
				adjust_view->UpdateSync();
			}
		}
	}

	InitView(INIT_BOUNDS | INIT_CENTER);
}

void VRenderVulkanView::RemoveVolumeData(wxString &name)
{
	int i, j;
	
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager *dm = vr_frame->GetDataManager();
	if (!dm) return;

	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 2://volume data
			{
				VolumeData* vd = (VolumeData*)m_layer_list[i];
				if (vd && vd->GetName() == name)
				{
					m_layer_list.erase(m_layer_list.begin()+i);
					m_vd_pop_dirty = true;
					m_cur_vol = NULL;
					m_loader.RemoveDataVD(vd);
					vd->GetVR()->clear_tex_current();
					dm->RemoveVolumeData(name);
					InitView(INIT_BOUNDS | INIT_CENTER);
					return;
				}
			}
			break;
		case 5://group
			{
				DataGroup* group = (DataGroup*)m_layer_list[i];
				for (j=0; j<group->GetVolumeNum(); j++)
				{
					VolumeData* vd = group->GetVolumeData(j);
					if (vd && vd->GetName() == name)
					{
						group->RemoveVolumeData(j);
						m_vd_pop_dirty = true;
						m_cur_vol = NULL;
						m_loader.RemoveDataVD(vd);
						vd->GetVR()->clear_tex_current();
						dm->RemoveVolumeData(name);
						InitView(INIT_BOUNDS | INIT_CENTER);
						return;
					}
				}
			}
			break;
		}
	}
}

void VRenderVulkanView::RemoveVolumeDataset(BaseReader *reader, int channel)
{
	int i, j;

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager *dm = vr_frame->GetDataManager();
	if (!dm) return;

	for (i=(int)m_layer_list.size()-1; i>=0; i--)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 2://volume data
			{
				VolumeData* vd = (VolumeData*)m_layer_list[i];
				if (vd && vd->GetReader() == reader && vd->GetCurChannel() == channel)
				{
					m_layer_list.erase(m_layer_list.begin()+i);
					m_vd_pop_dirty = true;
					if (vd == m_cur_vol) m_cur_vol = NULL;
					m_loader.RemoveDataVD(vd);
					vd->GetVR()->clear_tex_current();
					dm->RemoveVolumeData(vd->GetName());
				}
			}
			break;
		case 5://group
			{
				DataGroup* group = (DataGroup*)m_layer_list[i];
				for (j=group->GetVolumeNum()-1; j >= 0; j--)
				{
					VolumeData* vd = group->GetVolumeData(j);
					if (vd && vd->GetReader() == reader && vd->GetCurChannel() == channel)
					{
						group->RemoveVolumeData(j);
						m_vd_pop_dirty = true;
						if (vd == m_cur_vol) m_cur_vol = NULL;
						m_loader.RemoveDataVD(vd);
						vd->GetVR()->clear_tex_current();
						dm->RemoveVolumeData(vd->GetName());
					}
				}
			}
			break;
		}
	}
	InitView(INIT_BOUNDS | INIT_CENTER);
}

void VRenderVulkanView::RemoveMeshData(wxString &name)
{
	int i, j;

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager *dm = vr_frame->GetDataManager();
	if (!dm) return;

	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 3://mesh data
			{
				MeshData* md = (MeshData*)m_layer_list[i];
				if (md && md->GetName() == name)
				{
					m_layer_list.erase(m_layer_list.begin()+i);
					m_md_pop_dirty = true;
					dm->RemoveMeshData(name);
					InitView(INIT_BOUNDS | INIT_CENTER);
					return;
				}
			}
			break;
		case 6://mesh group
			{
				MeshGroup* group = (MeshGroup*)m_layer_list[i];
				if (!group) continue;
				for (j=0; j<group->GetMeshNum(); j++)
				{
					MeshData* md = group->GetMeshData(j);
					if (md && md->GetName()==name)
					{
						group->RemoveMeshData(j);
						m_md_pop_dirty = true;
						dm->RemoveMeshData(name);
						InitView(INIT_BOUNDS | INIT_CENTER);
						return;
					}
				}
			}
			break;
		}
	}
}

void VRenderVulkanView::RemoveAnnotations(wxString &name)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager *dm = vr_frame->GetDataManager();
	if (!dm) return;

	for (int i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		if (m_layer_list[i]->IsA() == 4)
		{
			Annotations* ann = (Annotations*)m_layer_list[i];
			if (ann && ann->GetName() == name)
			{
				m_layer_list.erase(m_layer_list.begin()+i);
				dm->RemoveAnnotations(name);
			}
		}
	}
}

void VRenderVulkanView::RemoveGroup(wxString &name)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	DataManager *dm = vr_frame->GetDataManager();
	if (!dm) return;

	int i, j;
	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 5://group
			{
				DataGroup* group = (DataGroup*)m_layer_list[i];
				if (group && group->GetName() == name)
				{
					for (j=group->GetVolumeNum()-1; j>=0; j--)
					{
						VolumeData* vd = group->GetVolumeData(j);
						if (vd)
						{
							if (m_cur_vol == vd)
								m_cur_vol = NULL;
							group->RemoveVolumeData(j);
							m_loader.RemoveDataVD(vd);
							vd->GetVR()->clear_tex_current();
							dm->RemoveVolumeData(vd->GetName());
						}
					}
					m_layer_list.erase(m_layer_list.begin()+i);
					delete group;
					m_vd_pop_dirty = true;
				}
			}
			break;
		case 6://mesh group
			{
				MeshGroup* group = (MeshGroup*)m_layer_list[i];
				if (group && group->GetName() == name)
				{
					for (j=group->GetMeshNum()-1; j>=0; j--)
					{
						MeshData* md = group->GetMeshData(j);
						if (md)
						{
							group->RemoveMeshData(j);
							dm->RemoveMeshData(md->GetName());
						}
					}
					m_layer_list.erase(m_layer_list.begin()+i);
					delete group;
					m_md_pop_dirty = true;
				}
			}
			break;
		}
	}
	InitView(INIT_BOUNDS | INIT_CENTER);
}

//isolate
void VRenderVulkanView::Isolate(int type, wxString name)
{
	for (int i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i]) continue;

		switch (m_layer_list[i]->IsA())
		{
		case 2://volume
			{
				VolumeData* vd = (VolumeData*)m_layer_list[i];
				if (vd)
				{
					if (type==2 &&
						vd->GetName() == name)
						vd->SetDisp(true);
					else
						vd->SetDisp(false);
				}
			}
			break;
		case 3://mesh
			{
				MeshData* md = (MeshData*)m_layer_list[i];
				if (md)
				{
					if (type==3 &&
						md->GetName() == name)
						md->SetDisp(false);
					else
						md->SetDisp(false);
				}
			}
			break;
		case 4://annotation
			{
				Annotations* ann = (Annotations*)m_layer_list[i];
				if (ann)
				{
					if (type==4 &&
						ann->GetName() == name)
						ann->SetDisp(true);
					else
						ann->SetDisp(false);
				}
			}
			break;
		case 5://volume group
			{
				DataGroup* group = (DataGroup*)m_layer_list[i];
				if (group)
				{
					for (int i=0; i<(int)group->GetVolumeNum(); i++)
					{
						VolumeData* vd = group->GetVolumeData(i);
						if (vd)
						{
							if (type == 2 &&
								vd->GetName() == name)
								vd->SetDisp(true);
							else
								vd->SetDisp(false);
						}
					}
				}
			}
			break;
		case 6://mesh group
			{
				MeshGroup* group = (MeshGroup*)m_layer_list[i];
				if (group)
				{
					for (int i=0; i<(int)group->GetMeshNum(); i++)
					{
						MeshData* md = group->GetMeshData(i);
						if (md)
						{
							if (type == 3 &&
								md->GetName() == name)
								md->SetDisp(true);
							else
								md->SetDisp(false);
						}
					}
				}
			}
			break;
		}
	}
	m_vd_pop_dirty = true;
	m_md_pop_dirty = true;
}

//move layer of the same level within this view
//source is after the destination
void VRenderVulkanView::MoveLayerinView(wxString &src_name, wxString &dst_name)
{
	int i, src_index;
	TreeLayer* src = 0;
	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (m_layer_list[i] && m_layer_list[i]->GetName() == src_name)
		{
			src = m_layer_list[i];
			src_index = i;
			m_layer_list.erase(m_layer_list.begin()+i);
			break;
		}
	}
	if (!src)
		return;
	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (m_layer_list[i] && m_layer_list[i]->GetName() == dst_name)
		{
			if (i>=src_index)
				m_layer_list.insert(m_layer_list.begin()+i+1, src);
			else
				m_layer_list.insert(m_layer_list.begin()+i, src);
			break;
		}
	}
	m_vd_pop_dirty = true;
	m_md_pop_dirty = true;
}

void VRenderVulkanView::ShowAll()
{
	for (unsigned int i=0; i<m_layer_list.size(); ++i)
	{
		if (!m_layer_list[i]) continue;

		switch (m_layer_list[i]->IsA())
		{
		case 2://volume
			{
				VolumeData* vd = (VolumeData*)m_layer_list[i];
				if (vd)
					vd->SetDisp(true);
			}
			break;
		case 3://mesh
			{
				MeshData* md = (MeshData*)m_layer_list[i];
				if (md)
					md->SetDisp(true);
			}
			break;
		case 4://annotation
			{
				Annotations* ann = (Annotations*)m_layer_list[i];
				if (ann)
					ann->SetDisp(true);
			}
			break;
		case 5:
			{
				DataGroup* group = (DataGroup*)m_layer_list[i];
				if (group)
				{
					for (int j=0; j<group->GetVolumeNum(); ++j)
					{
						VolumeData* vd = group->GetVolumeData(j);
						if (vd)
							vd->SetDisp(true);
					}
				}
			}
			break;
		case 6://mesh group
			{
				MeshGroup* group = (MeshGroup*)m_layer_list[i];
				if (group)
				{
					for (int j=0; j<group->GetMeshNum(); ++j)
					{
						MeshData* md = group->GetMeshData(j);
						if (md)
							md->SetDisp(true);
					}
				}
			}
			break;
		}
	}
	m_vd_pop_dirty = true;
	m_md_pop_dirty = true;
}

//move layer (volume) of the same level within the given group
//source is after the destination
void VRenderVulkanView::MoveLayerinGroup(wxString &group_name, wxString &src_name, wxString &dst_name, int insert_mode)
{
	DataGroup* group = GetGroup(group_name);
	if (!group)
		return;

	VolumeData* src_vd = 0;
	int i, src_index;
	for (i=0; i<group->GetVolumeNum(); i++)
	{
		wxString name = group->GetVolumeData(i)->GetName();
		if (name == src_name)
		{
			src_index = i;
			src_vd = group->GetVolumeData(i);
			group->RemoveVolumeData(i);
			break;
		}
	}
	if (!src_vd)
		return;
	for (i=0; i<group->GetVolumeNum(); i++)
	{
		wxString name = group->GetVolumeData(i)->GetName();
		if (name == dst_name)
		{
			int insert_offset = (insert_mode == 0) ? -1 : 0; 
			group->InsertVolumeData(i+insert_offset, src_vd);
			break;
		}
	}
	m_vd_pop_dirty = true;
	m_md_pop_dirty = true;
}

//move layer (volume) from the given group up one level to this view
//source is after the destination
void VRenderVulkanView::MoveLayertoView(wxString &group_name, wxString &src_name, wxString &dst_name)
{
	DataGroup* group = GetGroup(group_name);
	if (!group)
		return;

	VolumeData* src_vd = 0;
	int i;
	for (i=0; i<group->GetVolumeNum(); i++)
	{
		wxString name = group->GetVolumeData(i)->GetName();
		if (name == src_name)
		{
			src_vd = group->GetVolumeData(i);
			group->RemoveVolumeData(i);
			break;
		}
	}
	if (!src_vd)
		return;
	if (dst_name == "")
	{
		m_layer_list.push_back(src_vd);
	}
	else
	{
		for (i=0; i<(int)m_layer_list.size(); i++)
		{
			wxString name = m_layer_list[i]->GetName();
			if (name == dst_name)
			{
				m_layer_list.insert(m_layer_list.begin()+i+1, src_vd);
				break;
			}
		}
	}
	m_vd_pop_dirty = true;
	m_md_pop_dirty = true;
}

//move layer (volume) one level down to the given group
//source is after the destination
void VRenderVulkanView::MoveLayertoGroup(wxString &group_name, wxString &src_name, wxString &dst_name)
{
	VolumeData* src_vd = 0;
	int i;

	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		wxString name = m_layer_list[i]->GetName();
		if (name == src_name && m_layer_list[i]->IsA() == 2)//is volume data
		{
			src_vd = (VolumeData*)m_layer_list[i];
			m_layer_list.erase(m_layer_list.begin()+i);
			break;
		}
	}
	DataGroup* group = GetGroup(group_name);
	if (!group || !src_vd)
		return;
	if (group->GetVolumeNum()==0 || dst_name == "")
	{
		group->InsertVolumeData(0, src_vd);
	}
	else
	{
		for (i=0; i<group->GetVolumeNum(); i++)
		{
			wxString name = group->GetVolumeData(i)->GetName();
			if (name == dst_name)
			{
				group->InsertVolumeData(i, src_vd);
				break;
			}
		}
	}

	//set the 2d adjustment settings of the volume the same as the group
	Color gamma = group->GetGamma();
	src_vd->SetGamma(gamma);
	Color brightness = group->GetBrightness();
	src_vd->SetBrightness(brightness);
	Color hdr = group->GetHdr();
	src_vd->SetHdr(hdr);
    Color level = group->GetLevels();
    src_vd->SetLevels(level);
	bool sync_r = group->GetSyncR();
	src_vd->SetSyncR(sync_r);
	bool sync_g = group->GetSyncG();
	src_vd->SetSyncG(sync_g);
	bool sync_b = group->GetSyncB();
	src_vd->SetSyncB(sync_b);

	if ((group->GetVolumeSyncSpc() || group->GetVolumeSyncProp()) && group->GetVolumeNum() > 0)
	{
		double spcx=1.0, spcy=1.0, spcz=1.0;
		group->GetVolumeData(0)->GetSpacings(spcx, spcy, spcz, 0);
		src_vd->SetSpacings(spcx, spcy, spcz);
	}

	if (group->GetVolumeSyncProp() && group->GetVolumeData(0) != src_vd)
	{
		VolumeData *gvd = group->GetVolumeData(0);
		double dval = 0.0;
		double dval2 = 0.0;
		src_vd->Set3DGamma(gvd->Get3DGamma());
		src_vd->SetBoundary(gvd->GetBoundary());
		src_vd->SetOffset(gvd->GetOffset());
		src_vd->SetLeftThresh(gvd->GetLeftThresh());
		src_vd->SetRightThresh(gvd->GetRightThresh());
		src_vd->SetLuminance(gvd->GetLuminance());
		src_vd->SetShadow(gvd->GetShadow());
		gvd->GetShadowParams(dval);
		src_vd->SetShadowParams(dval);
		src_vd->SetShading(gvd->GetShading());
		double amb, diff, spec, shine;
		gvd->GetMaterial(amb, diff, spec, shine);
		src_vd->SetMaterial(amb, diff, spec, shine);
		src_vd->SetAlpha(gvd->GetAlpha());
		src_vd->SetSampleRate(gvd->GetSampleRate());
		src_vd->SetShading(gvd->GetShading());
		src_vd->SetColormap(gvd->GetColormap());
		gvd->GetColormapValues(dval, dval2);
		src_vd->SetColormapValues(dval, dval2);
		src_vd->SetInvert(gvd->GetInvert());
		src_vd->SetMode(gvd->GetMode());
		src_vd->SetNR(gvd->GetNR());
	}

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		vr_frame->GetAdjustView()->SetVolumeData(src_vd);
		vr_frame->GetAdjustView()->SetGroupLink(group);
		if (vr_frame->GetClippingView()->GetChannLink() && group->GetVolumeData(0) != src_vd)
		{
			CalcAndSetCombinedClippingPlanes();
            A2Q();
		}
	}

	m_vd_pop_dirty = true;
	m_md_pop_dirty = true;
}

//move layer (volume from one group to another different group
//sourece is after the destination
void VRenderVulkanView::MoveLayerfromtoGroup(wxString &src_group_name, wxString &dst_group_name, wxString &src_name, wxString &dst_name, int insert_mode)
{
	DataGroup* src_group = GetGroup(src_group_name);
	if (!src_group)
		return;
	int i;
	VolumeData* src_vd = 0;
	for (i=0; i<src_group->GetVolumeNum(); i++)
	{
		wxString name = src_group->GetVolumeData(i)->GetName();
		if (name == src_name)
		{
			src_vd = src_group->GetVolumeData(i);
			src_group->RemoveVolumeData(i);
			break;
		}
	}
	DataGroup* dst_group = GetGroup(dst_group_name);
	if (!dst_group || !src_vd)
		return;
	if (dst_group->GetVolumeNum()==0 || dst_name == "")
	{
		dst_group->InsertVolumeData(0, src_vd);
	}
	else
	{
		for (i=0; i<dst_group->GetVolumeNum(); i++)
		{
			wxString name = dst_group->GetVolumeData(i)->GetName();
			if (name == dst_name)
			{
				int insert_offset = (insert_mode == 0) ? -1 : 0; 
				dst_group->InsertVolumeData(i+insert_offset, src_vd);
				break;
			}
		}
	}

	//reset the sync of the source group
	//src_group->ResetSync();

	//set the 2d adjustment settings of the volume the same as the group
	Color gamma = dst_group->GetGamma();
	src_vd->SetGamma(gamma);
	Color brightness = dst_group->GetBrightness();
	src_vd->SetBrightness(brightness);
	Color hdr = dst_group->GetHdr();
	src_vd->SetHdr(hdr);
    Color level = dst_group->GetLevels();
    src_vd->SetLevels(level);
	bool sync_r = dst_group->GetSyncR();
	src_vd->SetSyncR(sync_r);
	bool sync_g = dst_group->GetSyncG();
	src_vd->SetSyncG(sync_g);
	bool sync_b = dst_group->GetSyncB();
	src_vd->SetSyncB(sync_b);

	if ((dst_group->GetVolumeSyncSpc() || dst_group->GetVolumeSyncProp()) && dst_group->GetVolumeNum() > 0)
	{
		double spcx=1.0, spcy=1.0, spcz=1.0;
		dst_group->GetVolumeData(0)->GetSpacings(spcx, spcy, spcz, 0);
		src_vd->SetSpacings(spcx, spcy, spcz);
	}

	if (dst_group->GetVolumeSyncProp() && dst_group->GetVolumeData(0) != src_vd)
	{
		VolumeData *gvd = dst_group->GetVolumeData(0);
		double dval = 0.0;
		double dval2 = 0.0;
		src_vd->Set3DGamma(gvd->Get3DGamma());
		src_vd->SetBoundary(gvd->GetBoundary());
		src_vd->SetOffset(gvd->GetOffset());
		src_vd->SetLeftThresh(gvd->GetLeftThresh());
		src_vd->SetRightThresh(gvd->GetRightThresh());
		src_vd->SetLuminance(gvd->GetLuminance());
		src_vd->SetShadow(gvd->GetShadow());
		gvd->GetShadowParams(dval);
		src_vd->SetShadowParams(dval);
		src_vd->SetShading(gvd->GetShading());
		double amb, diff, spec, shine;
		gvd->GetMaterial(amb, diff, spec, shine);
		src_vd->SetMaterial(amb, diff, spec, shine);
		src_vd->SetAlpha(gvd->GetAlpha());
		src_vd->SetSampleRate(gvd->GetSampleRate());
		src_vd->SetShading(gvd->GetShading());
		src_vd->SetColormap(gvd->GetColormap());
		gvd->GetColormapValues(dval, dval2);
		src_vd->SetColormapValues(dval, dval2);
		src_vd->SetInvert(gvd->GetInvert());
		src_vd->SetMode(gvd->GetMode());
		src_vd->SetNR(gvd->GetNR());
	}

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		vr_frame->GetAdjustView()->SetVolumeData(src_vd);
		vr_frame->GetAdjustView()->SetGroupLink(dst_group);
		if (vr_frame->GetClippingView()->GetChannLink() && dst_group->GetVolumeData(0) != src_vd)
		{
			CalcAndSetCombinedClippingPlanes();
            A2Q();
		}
	}

	m_vd_pop_dirty = true;
	m_md_pop_dirty = true;

	if (m_frame)
	{
		AdjustView* adjust_view = vr_frame->GetAdjustView();
		if (adjust_view)
		{
			adjust_view->SetVolumeData(src_vd);
			adjust_view->SetGroupLink(dst_group);
			adjust_view->UpdateSync();
		}
	}
}

//move mesh within a group
void VRenderVulkanView::MoveMeshinGroup(wxString &group_name, wxString &src_name, wxString &dst_name, int insert_mode)
{
	MeshGroup* group = GetMGroup(group_name);
	if (!group)
		return;

	MeshData* src_md = 0;
	int i, src_index;
	for (i=0; i<group->GetMeshNum(); i++)
	{
		wxString name = group->GetMeshData(i)->GetName();
		if (name == src_name)
		{
			src_index = i;
			src_md = group->GetMeshData(i);
			group->RemoveMeshData(i);
			break;
		}
	}
	if (!src_md)
		return;
	for (i=0; i<group->GetMeshNum(); i++)
	{
		wxString name = group->GetMeshData(i)->GetName();
		if (name == dst_name)
		{
			int insert_offset = (insert_mode == 0) ? -1 : 0; 
			group->InsertMeshData(i+insert_offset, src_md);
			break;
		}
	}
    
    InitView(INIT_BOUNDS|INIT_CENTER);

	m_vd_pop_dirty = true;
	m_md_pop_dirty = true;
}

//move mesh out of a group
void VRenderVulkanView::MoveMeshtoView(wxString &group_name, wxString &src_name, wxString &dst_name)
{
	MeshGroup* group = GetMGroup(group_name);
	if (!group)
		return;

	MeshData* src_md = 0;
	int i;
	for (i=0; i<group->GetMeshNum(); i++)
	{
		wxString name = group->GetMeshData(i)->GetName();
		if (name == src_name)
		{
			src_md = group->GetMeshData(i);
			group->RemoveMeshData(i);
			break;
		}
	}
	if (!src_md)
		return;
	if (dst_name == "")
		m_layer_list.push_back(src_md);
	else
	{
		for (i=0; i<(int)m_layer_list.size(); i++)
		{
			wxString name = m_layer_list[i]->GetName();
			if (name == dst_name)
			{
				m_layer_list.insert(m_layer_list.begin()+i+1, src_md);
				break;
			}
		}
	}
	m_vd_pop_dirty = true;
	m_md_pop_dirty = true;
}

//move mesh into a group
void VRenderVulkanView::MoveMeshtoGroup(wxString &group_name, wxString &src_name, wxString &dst_name)
{
	MeshData* src_md = 0;
	int i;

	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		wxString name = m_layer_list[i]->GetName();
		if (name==src_name && m_layer_list[i]->IsA()==3)
		{
			src_md = (MeshData*)m_layer_list[i];
			m_layer_list.erase(m_layer_list.begin()+i);
			break;
		}
	}
	MeshGroup* group = GetMGroup(group_name);
	if (!group || !src_md)
		return;
	if (group->GetMeshNum()==0 || dst_name=="")
		group->InsertMeshData(0, src_md);
	else
	{
		for (i=0; i<group->GetMeshNum(); i++)
		{
			wxString name = group->GetMeshData(i)->GetName();
			if (name == dst_name)
			{
				group->InsertMeshData(i, src_md);
				break;
			}
		}
	}
    InitView(INIT_BOUNDS|INIT_CENTER);
	m_vd_pop_dirty = true;
	m_md_pop_dirty = true;
}

//move mesh out of then into a group
void VRenderVulkanView::MoveMeshfromtoGroup(wxString &src_group_name, wxString &dst_group_name, wxString &src_name, wxString &dst_name, int insert_mode)
{
	MeshGroup* src_group = GetMGroup(src_group_name);
	if (!src_group)
		return;
	int i;
	MeshData* src_md = 0;
	for (i=0; i<src_group->GetMeshNum(); i++)
	{
		wxString name = src_group->GetMeshData(i)->GetName();
		if (name == src_name)
		{
			src_md = src_group->GetMeshData(i);
			src_group->RemoveMeshData(i);
			break;
		}
	}
	MeshGroup* dst_group = GetMGroup(dst_group_name);
	if (!dst_group || !src_md)
		return;
	if (dst_group->GetMeshNum()==0 ||dst_name=="")
		dst_group->InsertMeshData(0, src_md);
	else
	{
		for (i=0; i<dst_group->GetMeshNum(); i++)
		{
			wxString name = dst_group->GetMeshData(i)->GetName();
			if (name == dst_name)
			{
				int insert_offset = (insert_mode == 0) ? -1 : 0; 
				dst_group->InsertMeshData(i+insert_offset, src_md);
				break;
			}
		}
	}
    InitView(INIT_BOUNDS|INIT_CENTER);
	m_vd_pop_dirty = true;
	m_md_pop_dirty = true;
}

//layer control
int VRenderVulkanView::GetGroupNum()
{
	int group_num = 0;

	for (int i=0; i<(int)m_layer_list.size(); i++)
	{
		TreeLayer *layer = m_layer_list[i];
		if (layer && layer->IsA() == 5)
			group_num++;
	}
	return group_num;
}

int VRenderVulkanView::GetLayerNum()
{
	return m_layer_list.size();
}

TreeLayer* VRenderVulkanView::GetLayer(int index)
{
	if (index>=0 && index<(int)m_layer_list.size())
		return m_layer_list[index];
	else
		return 0;
}

wxString VRenderVulkanView::CheckNewGroupName(const wxString &name, int type)
{
	wxString result = name;

	int i;
	for (i=1; CheckGroupNames(result, type); i++)
		result = name+wxString::Format("_%d", i);

	return result;
}

bool VRenderVulkanView::CheckGroupNames(const wxString &name, int type)
{
	bool result = false;
	for (unsigned int i=0; i<m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		if (m_layer_list[i]->IsA() == type && m_layer_list[i]->GetName() == name)
		{
			result = true;
			break;
		}
	}
	return result;
}

wxString VRenderVulkanView::AddGroup(wxString str, wxString prev_group)
{
	DataGroup* group = new DataGroup();
	if (group && str != "")
		group->SetName(str);

	bool found_prev = false;
	for (int i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 5://group
			{
				DataGroup* group_temp = (DataGroup*)m_layer_list[i];
				if (group_temp && group_temp->GetName()==prev_group)
				{
					m_layer_list.insert(m_layer_list.begin()+i+1, group);
					found_prev = true;
				}
			}
			break;
		}
	}
	if (!found_prev)
		m_layer_list.push_back(group);

	//set default settings
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		AdjustView* adjust_view = vr_frame->GetAdjustView();
		if (adjust_view && group)
		{
			Color gamma, brightness, hdr, level;
			bool sync_r, sync_g, sync_b;
			adjust_view->GetDefaults(gamma, brightness, hdr, level, sync_r, sync_g, sync_b);
			group->SetGamma(gamma);
			group->SetBrightness(brightness);
			group->SetHdr(hdr);
            group->SetLevels(level);
			group->SetSyncR(sync_r);
			group->SetSyncG(sync_g);
			group->SetSyncB(sync_b);
		}
	}

	if (group)
		return group->GetName();
	else
		return "";
}

wxString VRenderVulkanView::AddMGroup(wxString str)
{
	MeshGroup* group = new MeshGroup();
	if (group && str != "")
		group->SetName(str);
	m_layer_list.push_back(group);

	if (group)
		return group->GetName();
	else
		return "";
}

MeshGroup* VRenderVulkanView::GetMGroup(wxString str)
{
	int i;

	for (i=0; i<(int)m_layer_list.size(); i++)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 6://mesh group
			{
				MeshGroup* group = (MeshGroup*)m_layer_list[i];
				if (group && group->GetName()==str)
					return group;
			}
		}
	}
	return 0;
}

//init
void VRenderVulkanView::InitView(unsigned int type)
{
	int i;

	if (type&INIT_BOUNDS)
	{
		m_bounds.reset();
		PopVolumeList();
		PopMeshList();
        
        for (i = 0; i < (int)m_vd_pop_list.size(); i++)
        {
            VolumeData* vd = m_vd_pop_list[i];
            if (vd)
            {
                if (!vd->GetTexture())
                    continue;
                Transform *tform = vd->GetTexture()->transform();
                if (!tform)
                    continue;
                Transform tform_copy;
                double mvmat[16];
                tform->get_trans(mvmat);
                swap(mvmat[3], mvmat[12]);
                swap(mvmat[7], mvmat[13]);
                swap(mvmat[11], mvmat[14]);
                tform_copy.set(mvmat);
                
                FLIVR::Point p[8] = {
                    FLIVR::Point(0.0f, 0.0f, 0.0f),
                    FLIVR::Point(1.0f, 0.0f, 0.0f),
                    FLIVR::Point(1.0f, 1.0f, 0.0f),
                    FLIVR::Point(0.0f, 1.0f, 0.0f),
                    FLIVR::Point(0.0f, 0.0f, 1.0f),
                    FLIVR::Point(1.0f, 0.0f, 1.0f),
                    FLIVR::Point(1.0f, 1.0f, 1.0f),
                    FLIVR::Point(0.0f, 1.0f, 1.0f)
                };
                
                for (int j = 0; j < 8; j++)
                {
                    p[j] = tform_copy.project(p[j]);
                    m_bounds.extend(p[j]);
                }
            }
        }
        for (i = 0; i < (int)m_md_pop_list.size(); i++)
            m_bounds.extend(m_md_pop_list[i]->GetBounds());

		if (m_bounds.valid())
		{
			Vector diag = m_bounds.diagonal();
			m_radius = sqrt(diag.x()*diag.x()+diag.y()*diag.y()) / 2.0;
//			if (m_radius<0.1)
//				m_radius = 5.0;
			m_near_clip = m_radius / 1000.0;
			m_far_clip = m_radius * 100.0;
			for (i=0 ; i<(int)m_md_pop_list.size() ; i++)
				m_md_pop_list[i]->SetBounds(m_bounds);
		}
	}

	if (type&INIT_CENTER)
	{
		if (m_bounds.valid())
		{
			m_obj_ctrx = (m_bounds.min().x() + m_bounds.max().x())/2.0;
			m_obj_ctry = (m_bounds.min().y() + m_bounds.max().y())/2.0;
			m_obj_ctrz = (m_bounds.min().z() + m_bounds.max().z())/2.0;
		}
	}

	if (type&INIT_TRANSL/*||!m_init_view*/)
	{
		m_distance = m_radius/tan(d2r(m_aov/2.0));
		m_init_dist = m_distance;
		m_transx = 0.0;
		m_transy = 0.0;
		m_transz = m_distance;
		if (!m_vrv->m_use_dft_settings)
			m_scale_factor = 1.0;
	}

	if (type&INIT_OBJ_TRANSL)
	{
		m_obj_transx = 0.0;
		m_obj_transy = 0.0;
		m_obj_transz = 0.0;
	}

	if (type&INIT_ROTATE||!m_init_view)
	{
		if (!m_vrv->m_use_dft_settings)
		{
			m_q = Quaternion(0,0,0,1);
			m_q.ToEuler(m_rotx, m_roty, m_rotz);
		}
	}
    
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    if (vr_frame && vr_frame->GetClippingView())
    {
        for (i = 0; i < (int)m_md_pop_list.size(); i++)
            m_md_pop_list[i]->RecalcBounds();
        if (vr_frame->GetClippingView()->GetChannLink())
        {
            CalcAndSetCombinedClippingPlanes();
            A2Q();
        }
    }

	m_init_view = true;

}

void VRenderVulkanView::DrawBounds()
{
	/*glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	vector<float> vertex;
	vertex.reserve(16*3);

	vertex.push_back(m_bounds.min().x()); vertex.push_back(m_bounds.min().y()); vertex.push_back(m_bounds.min().z());
	vertex.push_back(m_bounds.max().x()); vertex.push_back(m_bounds.min().y()); vertex.push_back(m_bounds.min().z());
	vertex.push_back(m_bounds.max().x()); vertex.push_back(m_bounds.max().y()); vertex.push_back(m_bounds.min().z());
	vertex.push_back(m_bounds.min().x()); vertex.push_back(m_bounds.max().y()); vertex.push_back(m_bounds.min().z());

	vertex.push_back(m_bounds.min().x()); vertex.push_back(m_bounds.min().y()); vertex.push_back(m_bounds.max().z());
	vertex.push_back(m_bounds.max().x()); vertex.push_back(m_bounds.min().y()); vertex.push_back(m_bounds.max().z());
	vertex.push_back(m_bounds.max().x()); vertex.push_back(m_bounds.max().y()); vertex.push_back(m_bounds.max().z());
	vertex.push_back(m_bounds.min().x()); vertex.push_back(m_bounds.max().y()); vertex.push_back(m_bounds.max().z());

	vertex.push_back(m_bounds.min().x()); vertex.push_back(m_bounds.min().y()); vertex.push_back(m_bounds.min().z());
	vertex.push_back(m_bounds.min().x()); vertex.push_back(m_bounds.min().y()); vertex.push_back(m_bounds.max().z());
	vertex.push_back(m_bounds.max().x()); vertex.push_back(m_bounds.min().y()); vertex.push_back(m_bounds.min().z());
	vertex.push_back(m_bounds.max().x()); vertex.push_back(m_bounds.min().y()); vertex.push_back(m_bounds.max().z());
	vertex.push_back(m_bounds.max().x()); vertex.push_back(m_bounds.max().y()); vertex.push_back(m_bounds.min().z());
	vertex.push_back(m_bounds.max().x()); vertex.push_back(m_bounds.max().y()); vertex.push_back(m_bounds.max().z());
	vertex.push_back(m_bounds.min().x()); vertex.push_back(m_bounds.max().y()); vertex.push_back(m_bounds.min().z());
	vertex.push_back(m_bounds.min().x()); vertex.push_back(m_bounds.max().y()); vertex.push_back(m_bounds.max().z());

	ShaderProgram* shader =
		m_img_shader_factory.shader(IMG_SHDR_DRAW_GEOMETRY);
	if (shader)
	{
		if (!shader->valid())
			shader->create();
		shader->bind();
	}
	shader->setLocalParam(0, 1.0, 1.0, 1.0, 1.0);
	glm::mat4 matrix = m_proj_mat * m_mv_mat;
	shader->setLocalParamMatrix(0, glm::value_ptr(matrix));

	glBindBuffer(GL_ARRAY_BUFFER, m_misc_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float)*vertex.size(), &vertex[0], GL_DYNAMIC_DRAW);
	glBindVertexArray(m_misc_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_misc_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (const GLvoid*)0);
	glDrawArrays(GL_LINE_LOOP, 0, 4);
	glDrawArrays(GL_LINE_LOOP, 4, 4);
	glDrawArrays(GL_LINES, 8, 8);
	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	if (shader && shader->valid())
		shader->release();

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);*/
}

double VRenderVulkanView::CalcCameraDistance()
{
	glm::mat4 mv_temp = m_mv_mat;
	//translate object
	mv_temp = glm::translate(mv_temp, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
	//rotate object
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
	//center object
	mv_temp = glm::translate(mv_temp, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

	double mvmat[16] =
		{ mv_temp[0][0], mv_temp[0][1], mv_temp[0][2], mv_temp[0][3],
		  mv_temp[1][0], mv_temp[1][1], mv_temp[1][2], mv_temp[1][3],
		  mv_temp[2][0], mv_temp[2][1], mv_temp[2][2], mv_temp[2][3],
		  mv_temp[3][0], mv_temp[3][1], mv_temp[3][2], mv_temp[3][3]};

	
	Transform mv;
	mv.set_trans(mvmat);

	vector<Vector> points;
	points.reserve(8);

	points.push_back(Vector(m_bounds.min().x(), m_bounds.min().y(), m_bounds.min().z()));
	points.push_back(Vector(m_bounds.max().x(), m_bounds.min().y(), m_bounds.min().z()));
	points.push_back(Vector(m_bounds.min().x(), m_bounds.max().y(), m_bounds.min().z()));
	points.push_back(Vector(m_bounds.max().x(), m_bounds.max().y(), m_bounds.min().z()));
	points.push_back(Vector(m_bounds.min().x(), m_bounds.min().y(), m_bounds.max().z()));
	points.push_back(Vector(m_bounds.max().x(), m_bounds.min().y(), m_bounds.max().z()));
	points.push_back(Vector(m_bounds.min().x(), m_bounds.max().y(), m_bounds.max().z()));
	points.push_back(Vector(m_bounds.max().x(), m_bounds.max().y(), m_bounds.max().z()));

	static int edges[12*2] = {0, 1,  1, 3,  3, 2,  2, 0,
							  0, 4,  1, 5,  3, 7,  2, 6,
							  4, 5,  5, 7,  7, 6,  6, 4};

	static int planes[3*6] = {0, 1, 2,  0, 1, 4,  1, 3, 5,  3, 2, 7,  2, 0, 6,  4, 5, 6};
	
	for (int i = 0; i < 8; i++)
		points[i] = mv.project(points[i].asPoint()).asVector();

	double dmin = DBL_MAX;
	
	//point
	for (int i = 0; i < 8; i++)
	{
		double d = points[i].length2();
		if (d < dmin) dmin = d;
	}
	
	//edge
	for (int i = 0; i < 12; i++)
	{
		Vector p = points[edges[i*2]];
		Vector q = points[edges[i*2+1]];

		if (Dot(p,p-q) <= 0 || Dot(q,q-p) <= 0) continue;

		Vector e = q - p;
		e.safe_normalize();
		double d = Dot(p, e);
		d = p.length2() - d*d;

		if (d < dmin) dmin = d;
	}

	//planes
	for (int i = 0; i < 6; i++)
	{
		Vector o = points[planes[i*3]];
		Vector p = points[planes[i*3+1]] - points[planes[i*3]];
		Vector q = points[planes[i*3+2]] - points[planes[i*3]];
		double plen = p.length();
		double qlen = q.length();
		p.safe_normalize();
		q.safe_normalize();
		Vector c = Cross(p, q);
		c.safe_normalize();

		double d = Dot(points[planes[i*3+1]], c);
		Vector v = c*d - o;

		double s = Dot(v, p) / plen;
		double t = Dot(v, q) / qlen;

		d = d*d;

		if (s > 0 && s < 1 && t > 0 && t < 1 && d < dmin) 
			dmin = d;
	}

	//inside?
	bool inside = false;
	//double padding = 0.0;
	{
		Vector ex = points[1] - points[0];
		Vector ey = points[2] - points[0];
		Vector ez = points[4] - points[0];
		double xlen = ex.length();
		double ylen = ey.length();
		double zlen = ez.length();
		ex.safe_normalize();
		ey.safe_normalize();
		ez.safe_normalize();

		Vector v = Vector(0)-points[0];

		double x = Dot(v, ex) / xlen;
		double y = Dot(v, ey) / ylen;
		double z = Dot(v, ez) / zlen;
		if (x > 0 && x < 1 && y > 0 && y < 1 && z > 0 && z < 1) 
			inside = true;

		//padding = Min(Min(xlen, ylen), zlen) / 2.0;
	}
	
	dmin = inside ? 0 : sqrt(dmin);

	return dmin;
}

void VRenderVulkanView::DrawClippingPlanes(bool border, int face_winding)
{
	int i;
	bool link = false;
	int plane_mode = PM_NORMAL;
	int draw_type = 2;
	VolumeData *cur_vd = NULL;
	MeshData *cur_md = NULL;

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame && vr_frame->GetClippingView())
	{
		link = vr_frame->GetClippingView()->GetChannLink();
		plane_mode = vr_frame->GetClippingView()->GetPlaneMode();
		draw_type = vr_frame->GetClippingView()->GetSelType();
		if (draw_type == 2)
		{
			cur_vd = vr_frame->GetClippingView()->GetVolumeData();
			if (!cur_vd) return;
		}
		if (draw_type == 3)
		{
			cur_md = vr_frame->GetClippingView()->GetMeshData();
			if (!cur_md) return;
		}
	}

	VkCullModeFlags cullmode = VK_CULL_MODE_NONE;
	bool draw_plane = plane_mode != PM_FRAME;
	if ((plane_mode == PM_LOWTRANSBACK ||
		plane_mode == PM_NORMALBACK) &&
		m_clip_mask == -1)
	{
		cullmode = VK_CULL_MODE_FRONT_BIT;
		if (face_winding == BACK_FACE)
			face_winding = FRONT_FACE;
		else
			draw_plane = false;
	}
	else
		cullmode = VK_CULL_MODE_BACK_BIT;

	if (!border && plane_mode == PM_FRAME)
		return;

	bool cull = false;
	VkFrontFace frontface = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	if (face_winding == FRONT_FACE)
	{
		cull = true;
		frontface = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	}
	else if (face_winding == BACK_FACE)
	{
		cull = true;
		frontface = VK_FRONT_FACE_CLOCKWISE;
	}

	//ShaderProgram* shader =
	//	m_img_shader_factory.shader(IMG_SHDR_DRAW_GEOMETRY);
	//if (shader)
	//{
	//	if (!shader->valid())
	//		shader->create();
	//	shader->bind();
	//}

	if (draw_type == 2)
	{
		for (i=0; i<GetDispVolumeNum(); i++)
		{
			VolumeData* vd = GetDispVolumeData(i);
			if (!vd)
				continue;

			if (vd!=cur_vd)
				continue;

			VolumeRenderer *vr = vd->GetVR();
			if (!vr)
				continue;

			vector<Plane*> *planes = vr->get_planes();
			if (planes->size() != 6)
				continue;

			//calculating planes
			//get six planes
			Plane* px1 = (*planes)[0];
			Plane* px2 = (*planes)[1];
			Plane* py1 = (*planes)[2];
			Plane* py2 = (*planes)[3];
			Plane* pz1 = (*planes)[4];
			Plane* pz2 = (*planes)[5];

			//calculate 4 lines
			Vector lv_x1z1, lv_x1z2, lv_x2z1, lv_x2z2;
			Point lp_x1z1, lp_x1z2, lp_x2z1, lp_x2z2;
			//x1z1
			if (!px1->Intersect(*pz1, lp_x1z1, lv_x1z1))
				continue;
			//x1z2
			if (!px1->Intersect(*pz2, lp_x1z2, lv_x1z2))
				continue;
			//x2z1
			if (!px2->Intersect(*pz1, lp_x2z1, lv_x2z1))
				continue;
			//x2z2
			if (!px2->Intersect(*pz2, lp_x2z2, lv_x2z2))
				continue;

			//calculate 8 points
			Point pp[8];
			//p0 = l_x1z1 * py1
			if (!py1->Intersect(lp_x1z1, lv_x1z1, pp[0]))
				continue;
			//p1 = l_x1z2 * py1
			if (!py1->Intersect(lp_x1z2, lv_x1z2, pp[1]))
				continue;
			//p2 = l_x2z1 *py1
			if (!py1->Intersect(lp_x2z1, lv_x2z1, pp[2]))
				continue;
			//p3 = l_x2z2 * py1
			if (!py1->Intersect(lp_x2z2, lv_x2z2, pp[3]))
				continue;
			//p4 = l_x1z1 * py2
			if (!py2->Intersect(lp_x1z1, lv_x1z1, pp[4]))
				continue;
			//p5 = l_x1z2 * py2
			if (!py2->Intersect(lp_x1z2, lv_x1z2, pp[5]))
				continue;
			//p6 = l_x2z1 * py2
			if (!py2->Intersect(lp_x2z1, lv_x2z1, pp[6]))
				continue;
			//p7 = l_x2z2 * py2
			if (!py2->Intersect(lp_x2z2, lv_x2z2, pp[7]))
				continue;

			//draw the six planes out of the eight points
			//get color
			Color color(1.0, 1.0, 1.0);
			double plane_trans = 0.0;
			if (face_winding == BACK_FACE &&
				(m_clip_mask == 3 ||
				m_clip_mask == 12 ||
				m_clip_mask == 48 ||
				m_clip_mask == 1 ||
				m_clip_mask == 2 ||
				m_clip_mask == 4 ||
				m_clip_mask == 8 ||
				m_clip_mask == 16 ||
				m_clip_mask == 32 ||
				m_clip_mask == 64)
				)
				plane_trans = plane_mode == PM_LOWTRANS ||
				plane_mode == PM_LOWTRANSBACK ? 0.1 : 0.3;

			if (face_winding == FRONT_FACE)
			{
				plane_trans = plane_mode == PM_LOWTRANS ||
					plane_mode == PM_LOWTRANSBACK ? 0.1 : 0.3;
			}

			if (plane_mode == PM_NORMAL ||
				plane_mode == PM_NORMALBACK)
			{
				if (!link)
					color = vd->GetColor();
			}
			else
				color = GetTextColor();

			//transform
			if (!vd->GetTexture())
				continue;
			Transform *tform = vd->GetTexture()->transform();
			if (!tform)
				continue;
			double mvmat[16];
			tform->get_trans(mvmat);
			double sclx, scly, sclz;
			vd->GetScalings(sclx, scly, sclz);
			glm::mat4 mv_mat = glm::scale(m_mv_mat,
				glm::vec3(float(sclx), float(scly), float(sclz)));
			glm::mat4 mv_mat2 = glm::mat4(
				mvmat[0], mvmat[4], mvmat[8], mvmat[3],
				mvmat[1], mvmat[5], mvmat[9], mvmat[7],
				mvmat[2], mvmat[6], mvmat[10], mvmat[11],
				mvmat[12], mvmat[13], mvmat[14], mvmat[15]);
			mv_mat = mv_mat * mv_mat2;
			glm::mat4 matrix = m_proj_mat * mv_mat;

			vector<Vulkan2dRender::Vertex> vertex;
			vector<uint32_t> index;
			vertex.reserve(8);
			index.reserve(6 * 4 + 6 * 5);

			//vertices
			for (size_t pi = 0; pi < 8; ++pi)
			{
				vertex.push_back(Vulkan2dRender::Vertex{
					{ (float)pp[pi].x(), (float)pp[pi].y(), (float)pp[pi].z() },
					{ 0.0f, 0.0f, 0.0f }
				});
			}

			if (m_clip_vobj.vertBuf.buffer == VK_NULL_HANDLE)
			{
				//indices
				index.push_back(4); index.push_back(0); index.push_back(5); index.push_back(1);
				index.push_back(7); index.push_back(3); index.push_back(6); index.push_back(2);
				index.push_back(1); index.push_back(0); index.push_back(3); index.push_back(2);
				index.push_back(4); index.push_back(5); index.push_back(6); index.push_back(7);
				index.push_back(0); index.push_back(4); index.push_back(2); index.push_back(6);
				index.push_back(5); index.push_back(1); index.push_back(7); index.push_back(3);
				index.push_back(4); index.push_back(0); index.push_back(1); index.push_back(5); index.push_back(4);
				index.push_back(7); index.push_back(3); index.push_back(2); index.push_back(6); index.push_back(7);
				index.push_back(1); index.push_back(0); index.push_back(2); index.push_back(3); index.push_back(1);
				index.push_back(4); index.push_back(5); index.push_back(7); index.push_back(6); index.push_back(4);
				index.push_back(0); index.push_back(4); index.push_back(6); index.push_back(2); index.push_back(0);
				index.push_back(5); index.push_back(1); index.push_back(3); index.push_back(7); index.push_back(5);

				VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					&m_clip_vobj.vertBuf,
					vertex.size() * sizeof(Vulkan2dRender::Vertex),
					vertex.data()));

				VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
					VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					&m_clip_vobj.idxBuf,
					index.size() * sizeof(uint32_t),
					index.data()));

				m_clip_vobj.idxCount = index.size();
				m_clip_vobj.idxOffset = 0;
				m_clip_vobj.vertCount = vertex.size();
				m_clip_vobj.vertOffset = 0;

				m_clip_vobj.idxBuf.map();
				m_clip_vobj.idxBuf.copyTo(index.data(), index.size() * sizeof(uint32_t));
				m_clip_vobj.idxBuf.unmap();
			}
			m_clip_vobj.vertBuf.map();
			m_clip_vobj.vertBuf.copyTo(vertex.data(), vertex.size() * sizeof(Vulkan2dRender::Vertex));
			m_clip_vobj.vertBuf.unmap();

			vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
			Vulkan2dRender::V2dPipeline pipeline_line =
				m_v2drender->preparePipeline(
					IMG_SHDR_DRAW_GEOMETRY,
					V2DRENDER_BLEND_OVER_UI,
					current_fbo->attachments[0]->format,
					current_fbo->attachments.size(),
					0,
					current_fbo->attachments[0]->is_swapchain_images,
					VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
					VK_POLYGON_MODE_FILL,
					cull ? cullmode : VK_CULL_MODE_NONE,
					frontface);
			Vulkan2dRender::V2dPipeline pipeline_tri =
				m_v2drender->preparePipeline(
					IMG_SHDR_DRAW_GEOMETRY,
					V2DRENDER_BLEND_OVER_UI,
					current_fbo->attachments[0]->format,
					current_fbo->attachments.size(),
					0,
					current_fbo->attachments[0]->is_swapchain_images,
					VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
					VK_POLYGON_MODE_FILL,
					cull ? cullmode : VK_CULL_MODE_NONE,
					frontface);
			if (!current_fbo->renderPass)
				current_fbo->replaceRenderPass(pipeline_line.pass);
			
			std::vector<Vulkan2dRender::V2DRenderParams> params_list;
			Vulkan2dRender::V2DRenderParams params;
			params.matrix[0] = matrix;
			params.clear = m_frame_clear;
			params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 1.0f };
			m_frame_clear = false;
			params.obj = &m_clip_vobj;

			//draw
			//x1 = (p4, p0, p1, p5)
			if (m_clip_mask & 1)
			{
				if (draw_plane)
				{
					if (plane_mode == PM_NORMAL ||
						plane_mode == PM_NORMALBACK)
						params.loc[0] = glm::vec4(1.0f, 0.5f, 0.5f, plane_trans);
					else
						params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_tri;
					params.render_idxCount = 4;
					params.render_idxBase = 4 * 0;
					params_list.push_back(params);
				}
				if (border)
				{
					params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_line;
					params.render_idxCount = 5;
					params.render_idxBase = 4 * 6 + 5 * 0;
					params_list.push_back(params);
				}
			}
			//x2 = (p7, p3, p2, p6)
			if (m_clip_mask & 2)
			{
				if (draw_plane)
				{
					if (plane_mode == PM_NORMAL ||
						plane_mode == PM_NORMALBACK)
						params.loc[0] = glm::vec4(1.0f, 0.5f, 1.0f, plane_trans);
					else
						params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_tri;
					params.render_idxCount = 4;
					params.render_idxBase = 4 * 1;
					params_list.push_back(params);
				}
				if (border)
				{
					params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_line;
					params.render_idxCount = 5;
					params.render_idxBase = 4 * 6 + 5 * 1;
					params_list.push_back(params);
				}
			}
			//y1 = (p1, p0, p2, p3)
			if (m_clip_mask & 4)
			{
				if (draw_plane)
				{
					if (plane_mode == PM_NORMAL ||
						plane_mode == PM_NORMALBACK)
						params.loc[0] = glm::vec4(0.5f, 1.0f, 0.5f, plane_trans);
					else
						params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_tri;
					params.render_idxCount = 4;
					params.render_idxBase = 4 * 2;
					params_list.push_back(params);
				}
				if (border)
				{
					params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_line;
					params.render_idxCount = 5;
					params.render_idxBase = 4 * 6 + 5 * 2;
					params_list.push_back(params);
				}
			}
			//y2 = (p4, p5, p7, p6)
			if (m_clip_mask & 8)
			{
				if (draw_plane)
				{
					if (plane_mode == PM_NORMAL ||
						plane_mode == PM_NORMALBACK)
						params.loc[0] = glm::vec4(1.0f, 1.0f, 0.5f, plane_trans);
					else
						params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_tri;
					params.render_idxCount = 4;
					params.render_idxBase = 4 * 3;
					params_list.push_back(params);
				}
				if (border)
				{
					params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_line;
					params.render_idxCount = 5;
					params.render_idxBase = 4 * 6 + 5 * 3;
					params_list.push_back(params);
				}
			}
			//z1 = (p0, p4, p6, p2)
			if (m_clip_mask & 16)
			{
				if (draw_plane)
				{
					if (plane_mode == PM_NORMAL ||
						plane_mode == PM_NORMALBACK)
						params.loc[0] = glm::vec4(0.5f, 0.5f, 1.0f, plane_trans);
					else
						params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_tri;
					params.render_idxCount = 4;
					params.render_idxBase = 4 * 4;
					params_list.push_back(params);
				}
				if (border)
				{
					params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_line;
					params.render_idxCount = 5;
					params.render_idxBase = 4 * 6 + 5 * 4;
					params_list.push_back(params);
				}
			}
			//z2 = (p5, p1, p3, p7)
			if (m_clip_mask & 32)
			{
				if (draw_plane)
				{
					if (plane_mode == PM_NORMAL ||
						plane_mode == PM_NORMALBACK)
						params.loc[0] = glm::vec4(0.5f, 1.0f, 1.0f, plane_trans);
					else
						params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_tri;
					params.render_idxCount = 4;
					params.render_idxBase = 4 * 5;
					params_list.push_back(params);
				}
				if (border)
				{
					params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_line;
					params.render_idxCount = 5;
					params.render_idxBase = 4 * 6 + 5 * 5;
					params_list.push_back(params);
				}
			}

			if (params_list.size() > 0)
			{
				m_v2drender->GetNextV2dRenderSemaphoreSettings(params_list[0]);
				m_v2drender->seq_render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params_list.data(), params_list.size());
			}
		}
	}

	if (draw_type == 3)
	{
        bool disp = false;
        for (i=0; i<GetMeshNum(); i++)
        {
            MeshData* md = GetMeshData(i);
            if (md==cur_md)
            {
                disp = true;
                break;
            }
        }
        for (i=0; i<(int)m_layer_list.size(); i++)
        {
            if (!m_layer_list[i])
                continue;
            switch (m_layer_list[i]->IsA())
            {
            case 4://annotations
                {
                    Annotations* ann = (Annotations*)m_layer_list[i];
                    if (ann && ann->GetDisp() && ann->GetMesh() && ann->GetMesh() == cur_md)
                    {
                        disp = true;
                        break;
                    }
                }
            }
        }
		if (disp)
		{
			MeshData* md = cur_md;
			if (!md)
				return;

			if (md!=cur_md)
                return;

			MeshRenderer *mr = md->GetMR();
			if (!mr)
                return;

			vector<Plane*> *planes = mr->get_planes();
			if (planes->size() != 6)
                return;

			//calculating planes
			//get six planes
			Plane* px1 = (*planes)[0];
			Plane* px2 = (*planes)[1];
			Plane* py1 = (*planes)[2];
			Plane* py2 = (*planes)[3];
			Plane* pz1 = (*planes)[4];
			Plane* pz2 = (*planes)[5];

			//calculate 4 lines
			Vector lv_x1z1, lv_x1z2, lv_x2z1, lv_x2z2;
			Point lp_x1z1, lp_x1z2, lp_x2z1, lp_x2z2;
			//x1z1
			if (!px1->Intersect(*pz1, lp_x1z1, lv_x1z1))
                return;
			//x1z2
			if (!px1->Intersect(*pz2, lp_x1z2, lv_x1z2))
                return;
			//x2z1
			if (!px2->Intersect(*pz1, lp_x2z1, lv_x2z1))
                return;
			//x2z2
			if (!px2->Intersect(*pz2, lp_x2z2, lv_x2z2))
                return;

			//calculate 8 points
			Point pp[8];
			//p0 = l_x1z1 * py1
			if (!py1->Intersect(lp_x1z1, lv_x1z1, pp[0]))
                return;
			//p1 = l_x1z2 * py1
			if (!py1->Intersect(lp_x1z2, lv_x1z2, pp[1]))
                return;
			//p2 = l_x2z1 *py1
			if (!py1->Intersect(lp_x2z1, lv_x2z1, pp[2]))
                return;
			//p3 = l_x2z2 * py1
			if (!py1->Intersect(lp_x2z2, lv_x2z2, pp[3]))
                return;
			//p4 = l_x1z1 * py2
			if (!py2->Intersect(lp_x1z1, lv_x1z1, pp[4]))
                return;
			//p5 = l_x1z2 * py2
			if (!py2->Intersect(lp_x1z2, lv_x1z2, pp[5]))
                return;
			//p6 = l_x2z1 * py2
			if (!py2->Intersect(lp_x2z1, lv_x2z1, pp[6]))
                return;
			//p7 = l_x2z2 * py2
			if (!py2->Intersect(lp_x2z2, lv_x2z2, pp[7]))
                return;

			//draw the six planes out of the eight points
			//get color
			Color color(1.0, 1.0, 1.0);
			double plane_trans = 0.0;
			if (face_winding == BACK_FACE &&
				(m_clip_mask == 3 ||
				m_clip_mask == 12 ||
				m_clip_mask == 48 ||
				m_clip_mask == 1 ||
				m_clip_mask == 2 ||
				m_clip_mask == 4 ||
				m_clip_mask == 8 ||
				m_clip_mask == 16 ||
				m_clip_mask == 32 ||
				m_clip_mask == 64)
				)
				plane_trans = plane_mode == PM_LOWTRANS ||
				plane_mode == PM_LOWTRANSBACK ? 0.1 : 0.3;

			if (face_winding == FRONT_FACE)
			{
				plane_trans = plane_mode == PM_LOWTRANS ||
					plane_mode == PM_LOWTRANSBACK ? 0.1 : 0.3;
			}

			if (plane_mode == PM_NORMAL ||
				plane_mode == PM_NORMALBACK)
			{
				if (!link)
				{
					Color amb, diff, spec;
					double shine, alpha;
					md->GetMaterial(amb, diff, spec, shine, alpha);
					color = diff;
				}
			}
			else
				color = GetTextColor();

			//transform
			BBox dbox = md->GetBounds();
			glm::mat4 mvmat = glm::mat4(float(dbox.max().x()-dbox.min().x()), 0.0f, 0.0f, 0.0f,
										0.0f, float(dbox.max().y()-dbox.min().y()), 0.0f, 0.0f,
										0.0f, 0.0f, -float(dbox.max().z()-dbox.min().z()), 0.0f,
										float(dbox.min().x()), float(dbox.min().y()), float(dbox.max().z()), 1.0f);
			glm::mat4 matrix = m_proj_mat * m_mv_mat * mvmat;

			vector<Vulkan2dRender::Vertex> vertex;
			vector<uint32_t> index;
			vertex.reserve(8);
			index.reserve(6 * 4 + 6 * 5);

			//vertices
			for (size_t pi = 0; pi < 8; ++pi)
			{
				vertex.push_back(Vulkan2dRender::Vertex{
					{ (float)pp[pi].x(), (float)pp[pi].y(), (float)pp[pi].z() },
					{ 0.0f, 0.0f, 0.0f }
					});
			}

			if (m_clip_vobj.vertBuf.buffer == VK_NULL_HANDLE)
			{
				//indices
				index.push_back(4); index.push_back(0); index.push_back(5); index.push_back(1);
				index.push_back(7); index.push_back(3); index.push_back(6); index.push_back(2);
				index.push_back(1); index.push_back(0); index.push_back(3); index.push_back(2);
				index.push_back(4); index.push_back(5); index.push_back(6); index.push_back(7);
				index.push_back(0); index.push_back(4); index.push_back(2); index.push_back(6);
				index.push_back(5); index.push_back(1); index.push_back(7); index.push_back(3);
				index.push_back(4); index.push_back(0); index.push_back(1); index.push_back(5); index.push_back(4);
				index.push_back(7); index.push_back(3); index.push_back(2); index.push_back(6); index.push_back(7);
				index.push_back(1); index.push_back(0); index.push_back(2); index.push_back(3); index.push_back(1);
				index.push_back(4); index.push_back(5); index.push_back(7); index.push_back(6); index.push_back(4);
				index.push_back(0); index.push_back(4); index.push_back(6); index.push_back(2); index.push_back(0);
				index.push_back(5); index.push_back(1); index.push_back(3); index.push_back(7); index.push_back(5);

				VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					&m_clip_vobj.vertBuf,
					vertex.size() * sizeof(Vulkan2dRender::Vertex),
					vertex.data()));

				VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
					VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					&m_clip_vobj.idxBuf,
					index.size() * sizeof(uint32_t),
					index.data()));

				m_clip_vobj.idxCount = index.size();
				m_clip_vobj.idxOffset = 0;
				m_clip_vobj.vertCount = vertex.size();
				m_clip_vobj.vertOffset = 0;

				m_clip_vobj.idxBuf.map();
				m_clip_vobj.idxBuf.copyTo(index.data(), index.size() * sizeof(uint32_t));
				m_clip_vobj.idxBuf.unmap();
			}
			m_clip_vobj.vertBuf.map();
			m_clip_vobj.vertBuf.copyTo(vertex.data(), vertex.size() * sizeof(Vulkan2dRender::Vertex));
			m_clip_vobj.vertBuf.unmap();

			vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
			Vulkan2dRender::V2dPipeline pipeline_line =
				m_v2drender->preparePipeline(
					IMG_SHDR_DRAW_GEOMETRY,
					V2DRENDER_BLEND_OVER_UI,
					current_fbo->attachments[0]->format,
					current_fbo->attachments.size(),
					0,
					current_fbo->attachments[0]->is_swapchain_images,
					VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
					VK_POLYGON_MODE_FILL,
					cull ? cullmode : VK_CULL_MODE_NONE,
					frontface);
			Vulkan2dRender::V2dPipeline pipeline_tri =
				m_v2drender->preparePipeline(
					IMG_SHDR_DRAW_GEOMETRY,
					V2DRENDER_BLEND_OVER_UI,
					current_fbo->attachments[0]->format,
					current_fbo->attachments.size(),
					0,
					current_fbo->attachments[0]->is_swapchain_images,
					VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
					VK_POLYGON_MODE_FILL,
					cull ? cullmode : VK_CULL_MODE_NONE,
					frontface);
			if (!current_fbo->renderPass)
				current_fbo->replaceRenderPass(pipeline_line.pass);

			std::vector<Vulkan2dRender::V2DRenderParams> params_list;
			Vulkan2dRender::V2DRenderParams params;
			params.matrix[0] = matrix;
			params.clear = m_frame_clear;
			params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 1.0f };
			m_frame_clear = false;
			params.obj = &m_clip_vobj;

			//draw
			//x1 = (p4, p0, p1, p5)
			if (m_clip_mask & 1)
			{
				if (draw_plane)
				{
					if (plane_mode == PM_NORMAL ||
						plane_mode == PM_NORMALBACK)
						params.loc[0] = glm::vec4(1.0f, 0.5f, 0.5f, plane_trans);
					else
						params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_tri;
					params.render_idxCount = 4;
					params.render_idxBase = 4 * 0;
					params_list.push_back(params);
				}
				if (border)
				{
					params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_line;
					params.render_idxCount = 5;
					params.render_idxBase = 4 * 6 + 5 * 0;
					params_list.push_back(params);
				}
			}
			//x2 = (p7, p3, p2, p6)
			if (m_clip_mask & 2)
			{
				if (draw_plane)
				{
					if (plane_mode == PM_NORMAL ||
						plane_mode == PM_NORMALBACK)
						params.loc[0] = glm::vec4(1.0f, 0.5f, 1.0f, plane_trans);
					else
						params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_tri;
					params.render_idxCount = 4;
					params.render_idxBase = 4 * 1;
					params_list.push_back(params);
				}
				if (border)
				{
					params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_line;
					params.render_idxCount = 5;
					params.render_idxBase = 4 * 6 + 5 * 1;
					params_list.push_back(params);
				}
			}
			//y1 = (p1, p0, p2, p3)
			if (m_clip_mask & 4)
			{
				if (draw_plane)
				{
					if (plane_mode == PM_NORMAL ||
						plane_mode == PM_NORMALBACK)
						params.loc[0] = glm::vec4(0.5f, 1.0f, 0.5f, plane_trans);
					else
						params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_tri;
					params.render_idxCount = 4;
					params.render_idxBase = 4 * 2;
					params_list.push_back(params);
				}
				if (border)
				{
					params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_line;
					params.render_idxCount = 5;
					params.render_idxBase = 4 * 6 + 5 * 2;
					params_list.push_back(params);
				}
			}
			//y2 = (p4, p5, p7, p6)
			if (m_clip_mask & 8)
			{
				if (draw_plane)
				{
					if (plane_mode == PM_NORMAL ||
						plane_mode == PM_NORMALBACK)
						params.loc[0] = glm::vec4(1.0f, 1.0f, 0.5f, plane_trans);
					else
						params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_tri;
					params.render_idxCount = 4;
					params.render_idxBase = 4 * 3;
					params_list.push_back(params);
				}
				if (border)
				{
					params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_line;
					params.render_idxCount = 5;
					params.render_idxBase = 4 * 6 + 5 * 3;
					params_list.push_back(params);
				}
			}
			//z1 = (p0, p4, p6, p2)
			if (m_clip_mask & 16)
			{
				if (draw_plane)
				{
					if (plane_mode == PM_NORMAL ||
						plane_mode == PM_NORMALBACK)
						params.loc[0] = glm::vec4(0.5f, 0.5f, 1.0f, plane_trans);
					else
						params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_tri;
					params.render_idxCount = 4;
					params.render_idxBase = 4 * 4;
					params_list.push_back(params);
				}
				if (border)
				{
					params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_line;
					params.render_idxCount = 5;
					params.render_idxBase = 4 * 6 + 5 * 4;
					params_list.push_back(params);
				}
			}
			//z2 = (p5, p1, p3, p7)
			if (m_clip_mask & 32)
			{
				if (draw_plane)
				{
					if (plane_mode == PM_NORMAL ||
						plane_mode == PM_NORMALBACK)
						params.loc[0] = glm::vec4(0.5f, 1.0f, 1.0f, plane_trans);
					else
						params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_tri;
					params.render_idxCount = 4;
					params.render_idxBase = 4 * 5;
					params_list.push_back(params);
				}
				if (border)
				{
					params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
					params.pipeline = pipeline_line;
					params.render_idxCount = 5;
					params.render_idxBase = 4 * 6 + 5 * 5;
					params_list.push_back(params);
				}
			}

			if (params_list.size() > 0)
			{
				m_v2drender->GetNextV2dRenderSemaphoreSettings(params_list[0]);
				m_v2drender->seq_render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params_list.data(), params_list.size());
			}
		}
	}

}

void VRenderVulkanView::DrawGrid()
{
	size_t grid_num = 5;
	size_t line_num = grid_num*2 + 1;
	double gap = m_distance / grid_num;
	size_t i;
	vector<Vulkan2dRender::Vertex> vertex;
	vector<uint32_t> index;
	vertex.reserve(line_num * 2 * 2);
	index.reserve(line_num * 2 * 2);

	for (i = 0; i < line_num; ++i)
	{
		vertex.push_back(
			Vulkan2dRender::Vertex{
				{float(-m_distance + gap*i), 0.0f, float(-m_distance)},
				{0.0f, 0.0f, 0.0f}
			}
		);
		vertex.push_back(
			Vulkan2dRender::Vertex{
				{float(-m_distance + gap*i), 0.0f, float(m_distance)},
				{0.0f, 0.0f, 0.0f}
			}
		);
		index.push_back(2*i);
		index.push_back(2*i + 1);
	}
	for (i = 0; i < line_num; ++i)
	{
		vertex.push_back(
			Vulkan2dRender::Vertex{
				{float(-m_distance), 0.0f, float(-m_distance + gap * i)},
				{0.0f, 0.0f, 0.0f}
			}
		);
		vertex.push_back(
			Vulkan2dRender::Vertex{
				{float(m_distance), 0.0f, float(-m_distance + gap * i)},
				{0.0f, 0.0f, 0.0f}
			}
		);
		index.push_back(2*line_num + 2*i);
		index.push_back(2*line_num + 2*i + 1);
	}

	if (m_grid_vobj.vertBuf.buffer == VK_NULL_HANDLE)
	{
		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_grid_vobj.vertBuf,
			vertex.size() * sizeof(Vulkan2dRender::Vertex),
			vertex.data()));

		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_grid_vobj.idxBuf,
			index.size() * sizeof(uint32_t),
			index.data()));

		m_grid_vobj.idxCount = index.size();
		m_grid_vobj.idxOffset = 0;
		m_grid_vobj.vertCount = vertex.size();
		m_grid_vobj.vertOffset = 0;
	}
	m_grid_vobj.vertBuf.map();
	m_grid_vobj.idxBuf.map();
	m_grid_vobj.vertBuf.copyTo(vertex.data(), vertex.size() * sizeof(Vulkan2dRender::Vertex));
	m_grid_vobj.idxBuf.copyTo(index.data(), index.size() * sizeof(uint32_t));
	m_grid_vobj.vertBuf.unmap();
	m_grid_vobj.idxBuf.unmap();


	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
	Color text_color = GetTextColor();
	params.loc[0] = glm::vec4((float)text_color.r(), (float)text_color.g(), (float)text_color.b(), 1.0f);
	params.matrix[0] = m_proj_mat * m_mv_mat;
	params.clear = m_frame_clear;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b() };
	m_frame_clear = false;

	params.obj = &m_grid_vobj;

	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);

	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
}

void VRenderVulkanView::DrawCamCtr()
{
	float len;
	if (m_camctr_size > 0.0)
		len = m_distance * tan(d2r(m_aov / 2.0)) * m_camctr_size / 10.0;
	else
		len = fabs(m_camctr_size);

	vector<Vulkan2dRender::Vertex> vertex;
	vector<uint32_t> index = { 0,1,2,3,4,5 };
	vertex.reserve(6);

	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {len,  0.0f, 0.0f}, {1.0f, 0.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, len,  0.0f}, {0.0f, 1.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.0f, len }, {0.0f, 0.0f, 1.0f} });
	
	if (m_camctr_vobj.vertBuf.buffer == VK_NULL_HANDLE)
	{
		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_camctr_vobj.vertBuf,
			vertex.size() * sizeof(Vulkan2dRender::Vertex),
			vertex.data()));

		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_camctr_vobj.idxBuf,
			index.size() * sizeof(uint32_t),
			index.data()));

		m_camctr_vobj.idxCount = index.size();
		m_camctr_vobj.idxOffset = 0;
		m_camctr_vobj.vertCount = vertex.size();
		m_camctr_vobj.vertOffset = 0;
	}
	m_camctr_vobj.vertBuf.map();
	m_camctr_vobj.idxBuf.map();
	m_camctr_vobj.vertBuf.copyTo(vertex.data(), vertex.size() * sizeof(Vulkan2dRender::Vertex));
	m_camctr_vobj.idxBuf.copyTo(index.data(), index.size() * sizeof(uint32_t));
	m_camctr_vobj.vertBuf.unmap();
	m_camctr_vobj.idxBuf.unmap();


	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY_COLOR3,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_LINE_LIST
		);
	params.matrix[0] = m_proj_mat * m_mv_mat;
	params.clear = m_frame_clear;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b() };
	m_frame_clear = false;

	params.obj = &m_camctr_vobj;

	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);

	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
}

void VRenderVulkanView::DrawFrame()
{
	int nx, ny;
	nx = GetSize().x;
	ny = GetSize().y;
	glm::mat4 proj_mat = glm::ortho(float(0), float(nx), float(0), float(ny));

	float line_width = 1.0f;

	vector<Vulkan2dRender::Vertex> vertex;
	vector<uint32_t> index = { 0,1,2,3,0 };
	vertex.reserve(4);

	vertex.push_back(Vulkan2dRender::Vertex{ {(float)(m_frame_x - 1), (float)(m_frame_y - 1), 0.0f}, {0.0f, 0.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {(float)(m_frame_x + m_frame_w + 1), (float)(m_frame_y - 1), 0.0f}, {0.0f, 0.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {(float)(m_frame_x + m_frame_w + 1), (float)(m_frame_y + m_frame_h + 1), 0.0f}, {0.0f, 0.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {(float)(m_frame_x - 1), (float)(m_frame_y + m_frame_h + 1), 0.0f}, {0.0f, 0.0f, 0.0f} });

	if (m_frame_vobj.vertBuf.buffer == VK_NULL_HANDLE)
	{
		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_frame_vobj.vertBuf,
			vertex.size() * sizeof(Vulkan2dRender::Vertex),
			vertex.data()));

		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_frame_vobj.idxBuf,
			index.size() * sizeof(uint32_t),
			index.data()));

		m_frame_vobj.idxCount = index.size();
		m_frame_vobj.idxOffset = 0;
		m_frame_vobj.vertCount = vertex.size();
		m_frame_vobj.vertOffset = 0;
	}
	m_frame_vobj.vertBuf.map();
	m_frame_vobj.idxBuf.map();
	m_frame_vobj.vertBuf.copyTo(vertex.data(), vertex.size() * sizeof(Vulkan2dRender::Vertex));
	m_frame_vobj.idxBuf.copyTo(index.data(), index.size() * sizeof(uint32_t));
	m_frame_vobj.vertBuf.unmap();
	m_frame_vobj.idxBuf.unmap();


	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_LINE_STRIP
		);
	params.loc[0] = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
	params.matrix[0] = proj_mat;
	params.clear = m_frame_clear;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b() };
	m_frame_clear = false;

	params.obj = &m_frame_vobj;

	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);

	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
}

void VRenderVulkanView::FixScaleBarLen(bool fix, double len)
{ 
	int nx = GetSize().x;
	int ny = GetSize().y;

	m_fix_sclbar = fix;
	if (len > 0.0) m_sb_length = len;
	m_fixed_sclbar_fac = m_sb_length*m_scale_factor*min(nx,ny);
}

void VRenderVulkanView::GetScaleBarFixed(bool &fix, double &len, int &unitid)
{
	int nx = GetSize().x;
	int ny = GetSize().y;

	fix = m_fix_sclbar;
	
	if (m_fix_sclbar)
		m_sb_length = m_fixed_sclbar_fac/(m_scale_factor*min(nx,ny));
	
	double len_txt = m_sb_length;
	int unit_id = m_sb_unit;
	wxString unit_txt = m_sb_text;

	wxString num_txt = (len_txt==(int)len_txt) ? wxString::Format(wxT("%i "), (int)len_txt) :
												 wxString::Format( ((int)(len_txt*100.0))%10==0 ? wxT("%.1f ") : wxT("%.2f "), len_txt);
/*	if (m_fix_sclbar)
	{

		if (log10(len_txt) < 0.0)
		{
			while (log10(len_txt) < 0.0 && unit_id > 0)
			{
				len_txt *= 1000.0;
				unit_id--;
			}
		}
		else if (log10(len_txt) >= 3.0)
		{
			while (log10(len_txt) >= 3.0 && unit_id < 2)
			{
				len_txt *= 0.001;
				unit_id++;
			}
		}

		switch (unit_id)
		{
		case 0:
			unit_txt = "nm";
			break;
		case 1:
		default:
			unit_txt = wxString::Format("%c%c", 181, 'm');
			break;
		case 2:
			unit_txt = "mm";
			break;
		}

		if (log10(len_txt) >= 2.0)
			num_txt = wxString::Format(wxT("%i "), (int)len_txt);
		else if (len_txt < 1.0)
			num_txt = wxString::Format(wxT("%.3f "), len_txt);
		else
		{
			int pr = 2.0 - (int)log10(len_txt);
			wxString f = wxT("%.") + wxString::Format(wxT("%i"), pr) + wxT("f ");
			num_txt = wxString::Format(f, len_txt);
		}
	}
*/	
	len = len_txt;
	unitid = unit_id;
}

void VRenderVulkanView::DrawScaleBar()
{
	double offset = 0.0;
	if (m_draw_legend)
		offset = m_sb_height;

	int nx = GetSize().x;
	int ny = GetSize().y;
	float sx, sy;
	sx = 2.0/nx;
	sy = 2.0/ny;
	float px, py, ph;

	glm::mat4 proj_mat = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f);
	
	if (m_fix_sclbar)
		m_sb_length = m_fixed_sclbar_fac/(m_scale_factor*min(nx,ny));
	float len = m_sb_length / (m_ortho_right-m_ortho_left);

	double len_txt = m_sb_length;
	int unit_id = m_sb_unit;
	wxString unit_txt = m_sb_text;

	wxString num_txt;
	
	if (m_sclbar_digit == 0)
		num_txt = wxString::Format(wxT("%i "), (int)len_txt);
	else if (m_sclbar_digit > 0)
	{
		wxString f = wxT("%.9f");
		num_txt = wxString::Format(f, len_txt);
		wxString intstr = num_txt.BeforeFirst(wxT('.'));
		wxString fracstr = num_txt.AfterFirst(wxT('.'));
		int fractextlen = m_sclbar_digit;
		if (fractextlen > fracstr.Length()) fractextlen = fracstr.Length();
		num_txt = intstr + wxT(".") + fracstr.Mid(0, fractextlen) + wxT(" ");
	}

/*
	if (m_sclbar_digit == 0)
		num_txt = wxString::Format(wxT("%i "), (int)len_txt);
	else if (m_sclbar_digit > 0)
	{
		wxString f = wxT("%.15f");
		num_txt = wxString::Format(f, len_txt);
		wxString intstr = num_txt.BeforeFirst(wxT('.'));
		if (intstr.Length() >= m_sclbar_digit)
			num_txt = intstr;
		else if (intstr.GetChar(0) != wxT('0'))
		{
			int textlen = m_sclbar_digit+1;
			if (textlen >= num_txt.Length()) textlen = num_txt.Length();
			num_txt = num_txt.Mid(0, m_sclbar_digit+1);
		}
		else
		{
			int count;
			bool zero = true;
			for(count = 0; count < num_txt.Length(); count++)
			{
				if (num_txt.GetChar(count) != wxT('0') && num_txt.GetChar(count) != wxT('.'))
				{
					zero = false;
					break;
				}
			}
			if (!zero)
			{
				count += m_sclbar_digit;
				if (count >= num_txt.Length()) count = num_txt.Length()-1;
			}
			else
				count = 1;
			num_txt = num_txt.Mid(0, count);
		}
		num_txt += wxT(" ");
	}
*/

/*	wxString num_txt = (len_txt==(int)len_txt) ? wxString::Format(wxT("%i "), (int)len_txt) :
												 wxString::Format( ((int)(len_txt*100.0))%10==0 ? wxT("%.1f ") : wxT("%.2f "), len_txt);
	if (m_fix_sclbar)
	{
		if (log10(len_txt) < 0.0)
		{
			while (log10(len_txt) < 0.0 && unit_id > 0)
			{
				len_txt *= 1000.0;
				unit_id--;
			}
		}
		else if (log10(len_txt) >= 3.0)
		{
			while (log10(len_txt) >= 3.0 && unit_id < 2)
			{
				len_txt *= 0.001;
				unit_id++;
			}
		}

		switch (unit_id)
		{
		case 0:
			unit_txt = "nm";
			break;
		case 1:
		default:
			unit_txt = wxString::Format("%c%c", 181, 'm');
			break;
		case 2:
			unit_txt = "mm";
			break;
		}

		if (log10(len_txt) >= 2.0)
			num_txt = wxString::Format(wxT("%i "), (int)len_txt);
		else if (len_txt < 1.0)
			num_txt = wxString::Format(wxT("%.3f "), len_txt);
		else
		{
			int pr = 2.0 - (int)log10(len_txt);
			wxString f = wxT("%.") + wxString::Format(wxT("%i"), pr) + wxT("f ");
			num_txt = wxString::Format(f, len_txt);
		}
	}
*/
	wstring wsb_text;
	wsb_text = num_txt + unit_txt.ToStdWstring();
	
	float textlen = m_text_renderer?
		m_text_renderer->RenderTextDims(wsb_text).width : 0.0f;
	Color text_color = GetTextColor();

	vector<Vulkan2dRender::Vertex> vertex;
	vector<uint32_t> index = { 0,1,2, 2,3,0 };
	vertex.reserve(4);

	if (m_draw_frame)
	{
		px = (0.95 * m_frame_w + m_frame_x) / nx;
		py = (0.95 * m_frame_h + m_frame_y - offset) / ny;
		ph = 5.0 / ny;
		vertex.push_back(Vulkan2dRender::Vertex{ {px, py, 0}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {px - len, py, 0}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {px - len, py + ph, 0}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {px, py + ph, 0}, {0.0f, 0.0f, 0.0f} });

		if (m_disp_scale_bar_text)
		{
			px = 0.95 * m_frame_w + m_frame_x - (len * nx + textlen + nx) / 2.0;
			py = 0.95 * m_frame_h + m_frame_y - 15.0f - offset - ny / 2.0;
			if (m_text_renderer)
				m_text_renderer->RenderText(
					m_vulkan->frameBuffers[m_vulkan->currentBuffer],
					wsb_text, text_color,
					px * sx, py * sy);
		}
	}
	else
	{
		px = 0.95;
		py = 0.95 - offset / ny;
		ph = 5.0 / ny;
		vertex.push_back(Vulkan2dRender::Vertex{ {px, py, 0}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {px - len, py, 0}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {px - len, py + ph, 0}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {px, py + ph, 0}, {0.0f, 0.0f, 0.0f} });

		if (m_disp_scale_bar_text)
		{
			px = 0.95 * nx - (len * nx + textlen + nx) / 2.0;
			py = 0.95 * ny - 15.0f - offset - ny / 2.0;
			if (m_text_renderer)
				m_text_renderer->RenderText(
					m_vulkan->frameBuffers[m_vulkan->currentBuffer],
					wsb_text, text_color,
					px * sx, py * sy);
		}
	}

	if (m_scbar_vobj.vertBuf.buffer == VK_NULL_HANDLE)
	{
		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_scbar_vobj.vertBuf,
			vertex.size() * sizeof(Vulkan2dRender::Vertex),
			vertex.data()));

		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_scbar_vobj.idxBuf,
			index.size() * sizeof(uint32_t),
			index.data()));

		m_scbar_vobj.idxCount = index.size();
		m_scbar_vobj.idxOffset = 0;
		m_scbar_vobj.vertCount = vertex.size();
		m_scbar_vobj.vertOffset = 0;
	}
	m_scbar_vobj.vertBuf.map();
	m_scbar_vobj.idxBuf.map();
	m_scbar_vobj.vertBuf.copyTo(vertex.data(), vertex.size() * sizeof(Vulkan2dRender::Vertex));
	m_scbar_vobj.idxBuf.copyTo(index.data(), index.size() * sizeof(uint32_t));
	m_scbar_vobj.vertBuf.unmap();
	m_scbar_vobj.idxBuf.unmap();


	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images
		);
	params.loc[0] = glm::vec4((float)text_color.r(), (float)text_color.g(), (float)text_color.b(), 1.0f);
	params.matrix[0] = proj_mat;
	params.clear = m_frame_clear;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b() };
	m_frame_clear = false;

	params.obj = &m_scbar_vobj;

	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);

	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
}

void VRenderVulkanView::DrawLegend()
{
	if (!m_text_renderer)
		return;
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;

	double font_height = m_text_renderer->GetSize() + 3.0;

	int nx = GetSize().x;
	int ny = GetSize().y;

	float xoffset = 10.0;
	float yoffset = 10.0;
	if (m_draw_frame)
	{
		xoffset = 10.0+m_frame_x;
		yoffset = 10.0+m_frame_y;
	}

	wxString wxstr;
	wstring wstr;
	double length = 0.0;
	double name_len = 0.0;
	double gap_width = font_height*1.5;
	int lines = 0;
	int i;
	//first pass
	for (i=0; i<(int)m_vd_pop_list.size(); i++)
	{
		if (m_vd_pop_list[i] && m_vd_pop_list[i]->GetLegend())
		{
			wxstr = m_vd_pop_list[i]->GetName();
			wstr = wxstr.ToStdWstring();
			name_len = m_text_renderer->RenderTextDims(wstr).width+font_height;
			length += name_len;
			if (length < double(m_draw_frame?m_frame_w:nx)-gap_width)
			{
				length += gap_width;
			}
			else
			{
				length = name_len + gap_width;
				lines++;
			}
		}
	}
	for (i=0; i<(int)m_md_pop_list.size(); i++)
	{
		if (m_md_pop_list[i] && m_md_pop_list[i]->GetLegend())
		{
			wxstr = m_md_pop_list[i]->GetName();
			wstr = wxstr.ToStdWstring();
			name_len = m_text_renderer->RenderTextDims(wstr).width+font_height;
			length += name_len;
			if (length < double(m_draw_frame?m_frame_w:nx)-gap_width)
			{
				length += gap_width;
			}
			else
			{
				length = name_len + gap_width;
				lines++;
			}
		}
	}

	//second pass
	int cur_line = 0;
	double xpos;
	length = 0.0;
	for (i=0; i<(int)m_vd_pop_list.size(); i++)
	{
		if (m_vd_pop_list[i] && m_vd_pop_list[i]->GetLegend())
		{
			wxstr = m_vd_pop_list[i]->GetName();
			xpos = length;
			wstr = wxstr.ToStdWstring();
			name_len = m_text_renderer->RenderTextDims(wstr).width+font_height;
			length += name_len;
			if (length < double(m_draw_frame?m_frame_w:nx)-gap_width)
			{
				length += gap_width;
			}
			else
			{
				length = name_len + gap_width;
				xpos = 0.0;
				cur_line++;
			}
			bool highlighted = false;
			if (vr_frame->GetCurSelType() == 2 &&
				vr_frame->GetCurSelVol() &&
				vr_frame->GetCurSelVol()->GetName() == wxstr &&
                !m_capture)
				highlighted = true;
			DrawName(xpos+xoffset, ny-(lines-cur_line+0.1)*font_height-yoffset,
				nx, ny, wxstr, m_vd_pop_list[i]->GetColor(),
				font_height, highlighted);
		}
	}
	for (i=0; i<(int)m_md_pop_list.size(); i++)
	{
		if (m_md_pop_list[i] && m_md_pop_list[i]->GetLegend())
		{
			wxstr = m_md_pop_list[i]->GetName();
			xpos = length;
			wstr = wxstr.ToStdWstring();
			name_len = m_text_renderer->RenderTextDims(wstr).width+font_height;
			length += name_len;
			if (length < double(m_draw_frame?m_frame_w:nx)-gap_width)
			{
				length += gap_width;
			}
			else
			{
				length = name_len + gap_width;
				xpos = 0.0;
				cur_line++;
			}
			Color amb, diff, spec;
			double shine, alpha;
			m_md_pop_list[i]->GetMaterial(amb, diff, spec, shine, alpha);
			Color c(diff.r(), diff.g(), diff.b());
			bool highlighted = false;
			if (vr_frame->GetCurSelType() == 3 &&
				vr_frame->GetCurSelMesh() &&
				vr_frame->GetCurSelMesh()->GetName() == wxstr &&
                !m_capture)
				highlighted = true;
			DrawName(xpos+xoffset, ny-(lines-cur_line+0.1)*font_height-yoffset,
				nx, ny, wxstr, c, font_height, highlighted);
		}
	}

	m_sb_height = (lines+1)*font_height;
}

void VRenderVulkanView::DrawName(
	double x, double y, int nx, int ny,
	wxString name, Color color,
	double font_height,
	bool highlighted)
{
	float sx, sy;
	sx = 2.0 / nx;
	sy = 2.0 / ny;

	wstring wstr;

	glm::mat4 proj_mat = glm::ortho(0.0f, float(nx), 0.0f, float(ny));
	vector<float> vertex;
	vertex.reserve(8 * 3);

	glm::vec3 trans1 = glm::vec3((float)(x + 0.2 * font_height - 1.0), (float)(y - 0.2 * font_height + 1.0), 0.0f);
	glm::vec3 scale1 = glm::vec3((float)(0.6 * font_height + 2.0), (float)(0.6 * font_height + 2.0), 1.0f);

	glm::vec3 trans2 = glm::vec3((float)(x + 0.2 * font_height), (float)(y - 0.2 * font_height), 0.0f);
	glm::vec3 scale2 = glm::vec3((float)(0.6 * font_height), (float)(0.6 * font_height), 1.0f);

	if (m_name_vobj.vertBuf.buffer == VK_NULL_HANDLE)
	{
		vector<Vulkan2dRender::Vertex> vertex;
		vector<uint32_t> index = { 0,1,2,2,3,0 };
		vertex.reserve(4);

		vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 0.0f} });

		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_name_vobj.vertBuf,
			vertex.size() * sizeof(Vulkan2dRender::Vertex),
			vertex.data()));

		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_name_vobj.idxBuf,
			index.size() * sizeof(uint32_t),
			index.data()));

		m_name_vobj.idxCount = index.size();
		m_name_vobj.idxOffset = 0;
		m_name_vobj.vertCount = vertex.size();
		m_name_vobj.vertOffset = 0;

		m_name_vobj.vertBuf.map();
		m_name_vobj.idxBuf.map();
		m_name_vobj.vertBuf.copyTo(vertex.data(), vertex.size() * sizeof(Vulkan2dRender::Vertex));
		m_name_vobj.idxBuf.copyTo(index.data(), index.size() * sizeof(uint32_t));
		m_name_vobj.vertBuf.unmap();
		m_name_vobj.idxBuf.unmap();
	}

	std::vector<Vulkan2dRender::V2DRenderParams> v2drender_params;

	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	Vulkan2dRender::V2dPipeline pl =
		m_v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY,
			V2DRENDER_BLEND_DISABLE,
			current_fbo->attachments[0]->format,
			current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images
		);

	Color text_color = GetTextColor();
	Vulkan2dRender::V2DRenderParams params1 = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	params1.pipeline = pl;
	params1.loc[0] = glm::vec4((float)text_color.r(), (float)text_color.g(), (float)text_color.b(), 1.0f);
	params1.matrix[0] = proj_mat * glm::translate(trans1) * glm::scale(scale1);
	params1.clear = m_frame_clear;
	params1.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b() };
	m_frame_clear = false;
	params1.obj = &m_name_vobj;

	Vulkan2dRender::V2DRenderParams params2;
	params2.pipeline = pl;
	params2.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), 1.0f);
	params2.matrix[0] = proj_mat * glm::translate(trans2) * glm::scale(scale2);
	params2.clear = false;
	params2.obj = &m_name_vobj;

	v2drender_params.push_back(params1);
	v2drender_params.push_back(params2);

	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(pl.pass);

	m_v2drender->seq_render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], v2drender_params.data(), v2drender_params.size());
	
	float px1 = x + font_height - nx / 2;
	float py1 = y - 0.25 * font_height - ny / 2;
	wstr = name.ToStdWstring();
	m_text_renderer->RenderText(
		m_vulkan->frameBuffers[m_vulkan->currentBuffer],
		wstr, text_color,
		px1 * sx, py1 * sy);
	if (highlighted)
	{
		px1 -= 0.5;
		py1 -= 0.5;
		m_text_renderer->RenderText(
			m_vulkan->frameBuffers[m_vulkan->currentBuffer],
			wstr, color,
			px1 * sx, py1 * sy);
	}

}

void VRenderVulkanView::DrawGradBg()
{
	glm::mat4 proj_mat = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f);

	Color color1, color2;
	HSVColor hsv_color1(m_bg_color);
	if (hsv_color1.val() > 0.5)
	{
		if (hsv_color1.sat() > 0.3)
		{
			color1 = Color(HSVColor(hsv_color1.hue(),
				hsv_color1.sat() * 0.1,
				Min(hsv_color1.val() + 0.3, 1.0)));
			color2 = Color(HSVColor(hsv_color1.hue(),
				hsv_color1.sat() * 0.3,
				Min(hsv_color1.val() + 0.1, 1.0)));
		}
		else
		{
			color1 = Color(HSVColor(hsv_color1.hue(),
				hsv_color1.sat() * 0.1,
				Max(hsv_color1.val() - 0.5, 0.0)));
			color2 = Color(HSVColor(hsv_color1.hue(),
				hsv_color1.sat() * 0.3,
				Max(hsv_color1.val() - 0.3, 0.0)));
		}
	}
	else
	{
		color1 = Color(HSVColor(hsv_color1.hue(),
			hsv_color1.sat() * 0.1,
			Min(hsv_color1.val() + 0.7, 1.0)));
		color2 = Color(HSVColor(hsv_color1.hue(),
			hsv_color1.sat() * 0.3,
			Min(hsv_color1.val() + 0.5, 1.0)));
	}

	vector<Vulkan2dRender::Vertex> vertex;
	vertex.reserve(8);

	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 1.0f, 0.0f}, {(float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b()} });
	vertex.push_back(Vulkan2dRender::Vertex{ {1.0f, 1.0f, 0.0f}, {(float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b()} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.7f, 0.0f}, {(float)color1.r(), (float)color1.g(), (float)color1.b()} });
	vertex.push_back(Vulkan2dRender::Vertex{ {1.0f, 0.7f, 0.0f}, {(float)color1.r(), (float)color1.g(), (float)color1.b()} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.5f, 0.0f}, {(float)color2.r(), (float)color2.g(), (float)color2.b()} });
	vertex.push_back(Vulkan2dRender::Vertex{ {1.0f, 0.5f, 0.0f}, {(float)color2.r(), (float)color2.g(), (float)color2.b()} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.0f, 0.0f}, {(float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b()} });
	vertex.push_back(Vulkan2dRender::Vertex{ {1.0f, 0.0f, 0.0f}, {(float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b()} });

	if (m_grad_vobj.vertBuf.buffer == VK_NULL_HANDLE)
	{
		vector<uint32_t> index = { 0,1,2,3,4,5,6,7 };
		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_grad_vobj.vertBuf,
			vertex.size() * sizeof(Vulkan2dRender::Vertex),
			vertex.data()));

		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_grad_vobj.idxBuf,
			index.size() * sizeof(uint32_t),
			index.data()));

		m_grad_vobj.idxCount = index.size();
		m_grad_vobj.idxOffset = 0;
		m_grad_vobj.vertCount = vertex.size();
		m_grad_vobj.vertOffset = 0;

		m_grad_vobj.idxBuf.map();
		m_grad_vobj.idxBuf.copyTo(index.data(), index.size() * sizeof(uint32_t));
		m_grad_vobj.idxBuf.unmap();
	}
	m_grad_vobj.vertBuf.map();
	m_grad_vobj.vertBuf.copyTo(vertex.data(), vertex.size() * sizeof(Vulkan2dRender::Vertex));
	m_grad_vobj.vertBuf.unmap();
	
	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY_COLOR3,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
		);
	params.matrix[0] = proj_mat;
	params.clear = m_frame_clear;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 1.0f };
	m_frame_clear = false;

	params.obj = &m_grad_vobj;

	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);

	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);

}

void VRenderVulkanView::SetColormapColors(int colormap)
{
	switch (colormap)
	{
	case 0://rainbow
		m_color_1 = Color(0.0, 0.0, 1.0);
		m_color_2 = Color(0.0, 0.0, 1.0);
		m_color_3 = Color(0.0, 1.0, 1.0);
		m_color_4 = Color(0.0, 1.0, 0.0);
		m_color_5 = Color(1.0, 1.0, 0.0);
		m_color_6 = Color(1.0, 0.0, 0.0);
		m_color_7 = Color(1.0, 0.0, 0.0);
		break;
	case 1://reverse rainbow
		m_color_1 = Color(1.0, 0.0, 0.0);
		m_color_2 = Color(1.0, 0.0, 0.0);
		m_color_3 = Color(1.0, 1.0, 0.0);
		m_color_4 = Color(0.0, 1.0, 0.0);
		m_color_5 = Color(0.0, 1.0, 1.0);
		m_color_6 = Color(0.0, 0.0, 1.0);
		m_color_7 = Color(0.0, 0.0, 1.0);
		break;
	case 2://hot
		m_color_1 = Color(0.0, 0.0, 0.0);
		m_color_2 = Color(0.0, 0.0, 0.0);
		m_color_3 = Color(0.5, 0.0, 0.0);
		m_color_4 = Color(1.0, 0.0, 0.0);
		m_color_5 = Color(1.0, 1.0, 0.0);
		m_color_6 = Color(1.0, 1.0, 1.0);
		m_color_7 = Color(1.0, 1.0, 1.0);
		break;
	case 3://cool
		m_color_1 = Color(0.0, 1.0, 1.0);
		m_color_2 = Color(0.0, 1.0, 1.0);
		m_color_3 = Color(0.25, 0.75, 1.0);
		m_color_4 = Color(0.5, 0.5, 1.0);
		m_color_5 = Color(0.75, 0.25, 1.0);
		m_color_6 = Color(1.0, 0.0, 1.0);
		m_color_7 = Color(1.0, 0.0, 1.0);
		break;
	case 4://blue-red
		m_color_1 = Color(0.25, 0.3, 0.75);
		m_color_2 = Color(0.25, 0.3, 0.75);
		m_color_3 = Color(0.475, 0.5, 0.725);
		m_color_4 = Color(0.7, 0.7, 0.7);
		m_color_5 = Color(0.7, 0.35, 0.425);
		m_color_6 = Color(0.7, 0.0, 0.15);
		m_color_7 = Color(0.7, 0.0, 0.15);
		break;
	}
}

void VRenderVulkanView::DrawColormap()
{
	bool draw = false;

	int num = 0;
	int vd_index;
	double max_val = 255.0;
	bool enable_alpha = false;

	for (int i=0; i<GetDispVolumeNum(); i++)
	{
		VolumeData* vd = GetDispVolumeData(i);
		if (vd && vd->GetColormapMode() == 1 && vd->GetDisp())
		{
			num++;
			vd_index = i;
		}
	}

	if (num == 0)
		return;
	else if (num == 1)
	{
		VolumeData* vd_view = GetDispVolumeData(vd_index);
		if (vd_view)
		{
			draw = true;
			double low, high;
			vd_view->GetColormapValues(low, high);
			m_value_2 = low;
			m_value_6 = high;
			m_value_4 = (low+high)/2.0;
			m_value_3 = (low+m_value_4)/2.0;
			m_value_5 = (m_value_4+high)/2.0;
			max_val = vd_view->GetMaxValue();
			enable_alpha = vd_view->GetEnableAlpha();
			SetColormapColors(vd_view->GetColormap());
		}
	}
	else if (num > 1)
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			VolumeData* vd = vr_frame->GetCurSelVol();
			if (vd && vd->GetDisp())
			{
				wxString str = vd->GetName();
				VolumeData* vd_view = GetVolumeData(str);
				if (vd_view && vd_view->GetColormapDisp())
				{
					draw = true;
					double low, high;
					vd_view->GetColormapValues(low, high);
					m_value_2 = low;
					m_value_6 = high;
					m_value_4 = (low+high)/2.0;
					m_value_3 = (low+m_value_4)/2.0;
					m_value_5 = (m_value_4+high)/2.0;
					max_val = vd_view->GetMaxValue();
					enable_alpha = vd_view->GetEnableAlpha();
					SetColormapColors(vd_view->GetColormap());
				}
			}
		}
	}

	if (!draw)
		return;

	float offset = 0.0f;
	if (m_draw_legend)
		offset = m_sb_height;

	int nx = GetSize().x;
	int ny = GetSize().y;
	float sx, sy;
	sx = 2.0/nx;
	sy = 2.0/ny;

	glm::mat4 proj_mat = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f);

	vector<Vulkan2dRender::Vertex34> vertex;
	vertex.reserve(8);

	float px, py;
	float fx, fy, fw, fh;
	if (m_draw_frame)
	{
		fx = m_frame_x;
		fy = m_frame_y;
		fw = m_frame_w;
		fh = m_frame_h;
	}
	else
	{
		fx = 0.0f;
		fy = 0.0f;
		fw = nx;
		fh = ny;
	}

	px = (0.01 * fw + fx) / nx;
	py = (0.05 * fw + fx) / nx;
	vertex.push_back(Vulkan2dRender::Vertex34{
		{px, (fh - (0.1f * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_1.r(), (float)m_color_1.g(), (float)m_color_1.b(), enable_alpha ? 0.0f : 1.0f}
		});
	vertex.push_back(Vulkan2dRender::Vertex34{
		{py, (fh - (0.1f * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_1.r(), (float)m_color_1.g(), (float)m_color_1.b(), enable_alpha ? 0.0f : 1.0f}
		});
	vertex.push_back(Vulkan2dRender::Vertex34{
		{px, (fh - ((0.1f + 0.4f * m_value_2) * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_2.r(), (float)m_color_2.g(), (float)m_color_2.b(), enable_alpha ? m_value_2 : 1.0f}
		});
	vertex.push_back(Vulkan2dRender::Vertex34{
		{py, (fh - ((0.1f + 0.4f * m_value_2) * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_2.r(), (float)m_color_2.g(), (float)m_color_2.b(), enable_alpha ? m_value_2 : 1.0f}
		});
	vertex.push_back(Vulkan2dRender::Vertex34{
		{px, (fh - ((0.1f + 0.4f * m_value_3) * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_3.r(), (float)m_color_3.g(), (float)m_color_3.b(), enable_alpha ? m_value_3 : 1.0f}
		});
	vertex.push_back(Vulkan2dRender::Vertex34{
		{py, (fh - ((0.1f + 0.4f * m_value_3) * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_3.r(), (float)m_color_3.g(), (float)m_color_3.b(), enable_alpha ? m_value_3 : 1.0f}
		});
	vertex.push_back(Vulkan2dRender::Vertex34{
		{px, (fh - ((0.1f + 0.4f * m_value_4) * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_4.r(), (float)m_color_4.g(), (float)m_color_4.b(), enable_alpha ? m_value_4 : 1.0f}
		});
	vertex.push_back(Vulkan2dRender::Vertex34{
		{py, (fh - ((0.1f + 0.4f * m_value_4) * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_4.r(), (float)m_color_4.g(), (float)m_color_4.b(), enable_alpha ? m_value_4 : 1.0f}
		});
	vertex.push_back(Vulkan2dRender::Vertex34{
		{px, (fh - ((0.1f + 0.4f * m_value_5) * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_5.r(), (float)m_color_5.g(), (float)m_color_5.b(), enable_alpha ? m_value_5 : 1.0f}
		});
	vertex.push_back(Vulkan2dRender::Vertex34{
		{py, (fh - ((0.1f + 0.4f * m_value_5) * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_5.r(), (float)m_color_5.g(), (float)m_color_5.b(), enable_alpha ? m_value_5 : 1.0f}
		});
	vertex.push_back(Vulkan2dRender::Vertex34{
		{px, (fh - ((0.1f + 0.4f * m_value_6) * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_6.r(), (float)m_color_6.g(), (float)m_color_6.b(), enable_alpha ? m_value_6 : 1.0f}
		});
	vertex.push_back(Vulkan2dRender::Vertex34{
		{py, (fh - ((0.1f + 0.4f * m_value_6) * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_6.r(), (float)m_color_6.g(), (float)m_color_6.b(), enable_alpha ? m_value_6 : 1.0f}
		});
	vertex.push_back(Vulkan2dRender::Vertex34{
		{px, (fh - (0.5f * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_7.r(), (float)m_color_7.g(), (float)m_color_7.b(), 1.0f}
		});
	vertex.push_back(Vulkan2dRender::Vertex34{
		{py, (fh - (0.5f * fh + fy + offset)) / ny, 0.0f},
		{(float)m_color_7.r(), (float)m_color_7.g(), (float)m_color_7.b(), 1.0f}
		});

	if (m_cmap_vobj.vertBuf.buffer == VK_NULL_HANDLE)
	{
		vector<uint32_t> index = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13 };
		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_cmap_vobj.vertBuf,
			vertex.size() * sizeof(Vulkan2dRender::Vertex34),
			vertex.data()));

		VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			&m_cmap_vobj.idxBuf,
			index.size() * sizeof(uint32_t),
			index.data()));

		m_cmap_vobj.idxCount = index.size();
		m_cmap_vobj.idxOffset = 0;
		m_cmap_vobj.vertCount = vertex.size();
		m_cmap_vobj.vertOffset = 0;

		m_cmap_vobj.idxBuf.map();
		m_cmap_vobj.idxBuf.copyTo(index.data(), index.size() * sizeof(uint32_t));
		m_cmap_vobj.idxBuf.unmap();
	}
	m_cmap_vobj.vertBuf.map();
	m_cmap_vobj.vertBuf.copyTo(vertex.data(), vertex.size() * sizeof(Vulkan2dRender::Vertex34));
	m_cmap_vobj.vertBuf.unmap();

	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY_COLOR4,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
		);
	params.matrix[0] = proj_mat;
	params.clear = m_frame_clear;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b() };
	m_frame_clear = false;

	params.obj = &m_cmap_vobj;

	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);

	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);

	if (m_text_renderer)
	{
		wxString str;
		wstring wstr;

		Color text_color = GetTextColor();

		//value 1
		px = 0.052 * fw + fx - nx / 2.0;
		py = fh - (0.1 * fh + fy + offset) - ny / 2.0;
		str = wxString::Format("%d", 0);
		wstr = str.ToStdWstring();
		m_text_renderer->RenderText(
			m_vulkan->frameBuffers[m_vulkan->currentBuffer],
			wstr, text_color,
			px * sx, py * sy);
		//value 2
		px = 0.052 * fw + fx - nx / 2.0;
		py = fh - ((0.1 + 0.4 * m_value_2) * fh + fy + offset) - ny / 2.0;
		str = wxString::Format("%d", int(m_value_2 * max_val));
		wstr = str.ToStdWstring();
		m_text_renderer->RenderText(
			m_vulkan->frameBuffers[m_vulkan->currentBuffer],
			wstr, text_color,
			px * sx, py * sy);
		//value 4
		px = 0.052 * fw + fx - nx / 2.0;
		py = fh - ((0.1 + 0.4 * m_value_4) * fh + fy + offset) - ny / 2.0;
		str = wxString::Format("%d", int(m_value_4 * max_val));
		wstr = str.ToStdWstring();
		m_text_renderer->RenderText(
			m_vulkan->frameBuffers[m_vulkan->currentBuffer],
			wstr, text_color,
			px * sx, py * sy);
		//value 6
		px = 0.052 * fw + fx - nx / 2.0;
		py = fh - ((0.1 + 0.4 * m_value_6) * fh + fy + offset) - ny / 2.0;
		str = wxString::Format("%d", int(m_value_6 * max_val));
		wstr = str.ToStdWstring();
		m_text_renderer->RenderText(
			m_vulkan->frameBuffers[m_vulkan->currentBuffer],
			wstr, text_color,
			px * sx, py * sy);
		//value 7
		px = 0.052 * fw + fx - nx / 2.0;
		py = fh - (0.5 * fh + fy + offset) - ny / 2.0;
		str = wxString::Format("%d", int(max_val));
		wstr = str.ToStdWstring();
		m_text_renderer->RenderText(
			m_vulkan->frameBuffers[m_vulkan->currentBuffer],
			wstr, text_color,
			px * sx, py * sy);
	}
}

void VRenderVulkanView::DrawInfo(int nx, int ny)
{
	//if (!m_text_renderer)
	//	return;

	float sx, sy;
	sx = 2.0/nx;
	sy = 2.0/ny;
	float px, py;
	float gapw = m_text_renderer->GetSize();
	float gaph = gapw;

	double fps_ = 1.0/goTimer->average();
	wxString str;
	Color text_color = GetTextColor();
	if (TextureRenderer::get_streaming())
	{
		/*str = wxString::Format(
			"FPS: %.2f, Bricks: %d, CurBrk: %d",
			fps_>=0.0&&fps_<300.0?fps_:0.0,
			TextureRenderer::get_total_brick_num(),
			TextureRenderer::get_cur_brick_num());
        */

		str = wxString::Format(
			"FPS: %.2f, Bricks: %d, Time: %lu",
			fps_ >= 0.0 && fps_ < 300.0 ? fps_ : 0.0,
			TextureRenderer::get_total_brick_num(),
			TextureRenderer::get_cor_up_time());
        
        /*str = wxString::Format(
                               "FPS: %.2f, Bricks: %d, Quota: %d, Int: %s, Time: %lu, Dist: %f",
                               fps_>=0.0&&fps_<300.0?fps_:0.0,
                               TextureRenderer::get_total_brick_num(),
                               TextureRenderer::get_quota_bricks(),
                               m_interactive?"Yes":"No",
                               TextureRenderer::get_cor_up_time(),
                               CalcCameraDistance());*/
        
		////budget_test
		//if (m_interactive)
		//  tos <<
		//  TextureRenderer::get_quota_bricks()
		//  << "\t" <<
		//  TextureRenderer::get_finished_bricks()
		//  << "\t" <<
		//  TextureRenderer::get_queue_last()
		//  << "\t" <<
		//  int(TextureRenderer::get_finished_bricks()*
		//    TextureRenderer::get_up_time()/
		//    TextureRenderer::get_consumed_time())
		//  << "\n";
	}
	else
		str = wxString::Format("FPS: %.2f", fps_>=0.0&&fps_<300.0?fps_:0.0);

	if (m_cur_vol)
	{
		int resx=0, resy=0, resz=0;
		if (m_cur_vol->GetTexture())
		{
			resx = m_cur_vol->GetTexture()->nx();
			resy = m_cur_vol->GetTexture()->ny();
			resz = m_cur_vol->GetTexture()->nz();
		}
		if (m_cur_vol->isBrxml())
			str += wxString::Format(" VVD_Level: %d/%d W:%d H:%d D:%d,", m_cur_vol->GetLevel()+1, m_cur_vol->GetLevelNum(), resx, resy, resz);
		long long used_mem;
		int dtnum, qnum, dqnum;
		m_loader.GetPalams(used_mem, dtnum, qnum, dqnum);
		str += wxString::Format(" Mem:%lld(MB) Th: %d Q: %d DQ: %d,", used_mem/(1024LL*1024LL), dtnum, qnum, dqnum);
	}

	wstring wstr_temp = str.ToStdWstring();
	px = gapw - nx/2.0f;
	py = gaph - ny/2.0f;
	m_text_renderer->RenderText(
		m_vulkan->frameBuffers[m_vulkan->currentBuffer],
		wstr_temp, text_color,
		px*sx, py*sy);

	gaph += m_text_renderer->GetSize();

	if (m_draw_coord)
	{
		Point p;
		wxPoint mouse_pos = ScreenToClient(wxGetMousePosition());
		if ((m_cur_vol && GetPointVolumeBox(p, mouse_pos.x, mouse_pos.y, m_cur_vol)>0.0) ||
			GetPointPlane(p, mouse_pos.x, mouse_pos.y)>0.0)
		{
			str = wxString::Format("T: %d  X: %.2f  Y: %.2f  Z: %.2f",
				m_tseq_cur_num, p.x(), p.y(), p.z());
			wstr_temp = str.ToStdWstring();
			px = gapw-nx/2;
			py = gaph-ny/2;
			m_text_renderer->RenderText(
				m_vulkan->frameBuffers[m_vulkan->currentBuffer],
				wstr_temp, text_color,
				px*sx, py*sy);
		}
	}
	else
	{
		str = wxString::Format("T: %d", m_tseq_cur_num);
		wstr_temp = str.ToStdWstring();
		px = gapw - nx / 2;
		py = gaph - ny / 2;
		m_text_renderer->RenderText(
			m_vulkan->frameBuffers[m_vulkan->currentBuffer],
			wstr_temp, text_color,
			px*sx, py*sy);
	}

    if (/*m_test_wiref*/1)
    {
        if (m_vol_method == VOL_METHOD_MULTI && m_mvr)
        {
            str = wxString::Format("SLICES: %d", m_mvr->get_slice_num());
            wstr_temp = str.ToStdWstring();
            px = gapw-nx/2;
            py = ny/2-gaph*1.5;
            m_text_renderer->RenderText(
                m_vulkan->frameBuffers[m_vulkan->currentBuffer],
                wstr_temp, text_color,
                px*sx, py*sy);
        }
        else
        {
            for (int i=0; i<(int)m_vd_pop_list.size(); i++)
            {
                VolumeData* vd = m_vd_pop_list[i];
                if (vd && vd->GetVR())
                {
                    str = wxString::Format("SLICES_%d: %d", i+1, vd->GetVR()->get_slice_num());
                    wstr_temp = str.ToStdWstring();
                    px = gapw-nx/2;
                    py = ny/2-gaph*(3+i)/2;
                    if (m_text_renderer)
                        m_text_renderer->RenderText(
                        m_vulkan->frameBuffers[m_vulkan->currentBuffer],
                        wstr_temp, text_color,
                        px*sx, py*sy);
                }
            }
        }
    }

	//wxString dbgstr = wxString::Format("fps: %.2f\n", fps_ >= 0.0 && fps_ < 300.0 ? fps_ : 0.0);
	//OutputDebugStringA(dbgstr.ToStdString().c_str());
}

Quaternion VRenderVulkanView::Trackball(int p1x, int p1y, int p2x, int p2y)
{
	Quaternion q;
	Vector a; /* Axis of rotation */
	double phi;  /* how much to rotate about axis */

	if (p1x == p2x && p1y == p2y)
	{
		/* Zero rotation */
		return q;
	}

	if (m_rot_lock)
	{
		if (abs(p2x-p1x)<50 &&
			abs(p2y-p1y)<50)
			return q;
	}

	a = Vector(p2y-p1y, p2x-p1x, 0.0);
	phi = a.length()/3.0;
	a.normalize();
	Quaternion q_a(a);
	//rotate back to local
	Quaternion q_a2 = (-m_q) * q_a * m_q;
	a = Vector(q_a2.x, q_a2.y, q_a2.z);
	a.normalize();

	q = Quaternion(phi, a);
	q.Normalize();

	if (m_rot_lock)
	{
		double rotx, roty, rotz;
		q.ToEuler(rotx, roty, rotz);
		rotx = int(rotx/45.0)*45.0;
		roty = int(roty/45.0)*45.0;
		rotz = int(rotz/45.0)*45.0;
		q.FromEuler(rotx, roty, rotz);
	}

	return q;
}

Quaternion VRenderVulkanView::TrackballClip(int p1x, int p1y, int p2x, int p2y)
{
	Quaternion q;
	Vector a; /* Axis of rotation */
	double phi;  /* how much to rotate about axis */

	if (p1x == p2x && p1y == p2y)
	{
		/* Zero rotation */
		return q;
	}

	a = Vector(p1y-p2y, p2x-p1x, 0.0);
	phi = a.length()/3.0;
	a.normalize();
	Quaternion q_a(a);
	//rotate back to local
	Quaternion q2;
	q2.FromEuler(-m_rotx, m_roty, m_rotz);
	q_a = (q2) * q_a * (-q2);
	q_a = (m_q_cl) * q_a * (-m_q_cl);
	a = Vector(q_a.x, q_a.y, q_a.z);
	a.normalize();

	q = Quaternion(phi, a);
	q.Normalize();

	return q;
}

void VRenderVulkanView::Q2A()
{
	//view changed, re-sort bricks
	//SetSortBricks();

	m_q.ToEuler(m_rotx, m_roty, m_rotz);
    //make rotation directions the same as the OpenGL version.
    //m_rotx = -m_rotx;
    //m_rotz = -m_rotz;

	if (m_roty>360.0)
		m_roty -= 360.0;
	if (m_roty<0.0)
		m_roty += 360.0;
	if (m_rotx>360.0)
		m_rotx -= 360.0;
	if (m_rotx<0.0)
		m_rotx += 360.0;
	if (m_rotz>360.0)
		m_rotz -= 360.0;
	if (m_rotz<0.0)
		m_rotz += 360.0;

	if (m_clip_mode)
	{
		if (m_clip_mode == 1)
			m_q_cl.FromEuler(-m_rotx, -m_roty, m_rotz);
		else if (m_clip_mode == 3)
		{
			m_q_cl_zero.FromEuler(-m_rotx, -m_roty, m_rotz);
			m_q_cl = m_q_fix * m_q_cl_zero;
			/*
            m_q_cl.ToEuler(m_rotx_cl, m_roty_cl, m_rotz_cl);
			if (m_rotx_cl > 180.0) m_rotx_cl -= 360.0;
			if (m_roty_cl > 180.0) m_roty_cl -= 360.0;
			if (m_rotz_cl > 180.0) m_rotz_cl -= 360.0;
			VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
			if (vr_frame)
			{
				ClippingView* clip_view = vr_frame->GetClippingView();
				if (clip_view)
					clip_view->SetClippingPlaneRotations(
						m_rotx_cl <= 180.0 ? m_rotx_cl : 360.0 - m_rotx_cl,
						m_roty_cl <= 180.0 ? m_roty_cl : 360.0 - m_roty_cl,
						m_rotz_cl <= 180.0 ? m_rotz_cl : 360.0 - m_rotz_cl,
						true);
			}
             */
		}

		vector<Plane*> *planes = 0;
        Vector n;
        Point p;
		for (int i=0; i<(int)m_vd_pop_list.size(); i++)
		{
			if (!m_vd_pop_list[i])
				continue;

			double spcx, spcy, spcz;
			int resx, resy, resz;
			m_vd_pop_list[i]->GetSpacings(spcx, spcy, spcz);
			m_vd_pop_list[i]->GetResolution(resx, resy, resz);
			Vector scale, scale2, scale2inv;
            if (spcx>0.0 && spcy>0.0 && spcz>0.0)
            {
                scale = Vector(1.0/resx/spcx, 1.0/resy/spcy, 1.0/resz/spcz);
                scale.safe_normalize();
                scale2 = Vector(resx*spcx, resy*spcy, resz*spcz);
                //scale2.safe_normalize();
                scale2inv = Vector(1.0/(resx*spcx), 1.0/(resy*spcy), 1.0/(resz*spcz));
                //scale2inv.safe_normalize();
            }
            else
            {
                scale = Vector(1.0, 1.0, 1.0);
                scale2 = Vector(1.0, 1.0, 1.0);
                scale2inv = Vector(1.0, 1.0, 1.0);
            }
            
            if (!m_vd_pop_list[i]->GetTexture())
                continue;
            Transform *tform = m_vd_pop_list[i]->GetTexture()->transform();
            if (!tform)
                continue;
            Transform tform_copy;
            double mvmat[16];
            tform->get_trans(mvmat);
            swap(mvmat[3], mvmat[12]);
            swap(mvmat[7], mvmat[13]);
            swap(mvmat[11], mvmat[14]);
            tform_copy.set(mvmat);
            
            double rotx, roty, rotz;
            Quaternion q_cl, q_cl_fix;
            m_q_cl.ToEuler(rotx, roty, rotz);
            q_cl.FromEuler(-rotx, -roty, rotz);
            m_q_cl_fix.ToEuler(rotx, roty, rotz);
            q_cl_fix.FromEuler(-rotx, -roty, rotz);

			if (m_vd_pop_list[i]->GetVR())
				planes = m_vd_pop_list[i]->GetVR()->get_planes();
			if (planes && planes->size()==6)
			{
				m_vd_pop_list[i]->GetVR()->set_clip_quaternion(m_q_cl);
				double x1, x2, y1, y2, z1, z2;
				double abcd[4];
				(*planes)[0]->get_copy(abcd);
				x1 = fabs(abcd[3]);
				(*planes)[1]->get_copy(abcd);
				x2 = fabs(abcd[3]);
				(*planes)[2]->get_copy(abcd);
				y1 = fabs(abcd[3]);
				(*planes)[3]->get_copy(abcd);
				y2 = fabs(abcd[3]);
				(*planes)[4]->get_copy(abcd);
				z1 = fabs(abcd[3]);
				(*planes)[5]->get_copy(abcd);
				z2 = fabs(abcd[3]);
                
                Vector trans1(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz);
                Vector trans2(m_obj_ctrx, m_obj_ctry, m_obj_ctrz);
                
                if (m_clip_mode == 3)
                {
                    Vector obj_trans1 = m_trans_fix;
                    Vector obj_trans2 = -obj_trans1;
                    Vector obj_trans3 = obj_trans1 - Vector(m_obj_transx, m_obj_transy, m_obj_transz);
                    
                    for (int i = 0; i < 6; i++)
                    {
                        (*planes)[i]->Restore();
                        
                        p = (*planes)[i]->get_point();
                        n = (*planes)[i]->normal();
                        p = tform_copy.project(p);
                        n = tform->unproject(n);
                        n.safe_normalize();
                        (*planes)[i]->ChangePlaneTemp(p, n);
                        
                        (*planes)[i]->Translate(trans1);
                        (*planes)[i]->Rotate(q_cl_fix);
                        (*planes)[i]->Translate(obj_trans1);
                        (*planes)[i]->Rotate(q_cl);
                        (*planes)[i]->Translate(obj_trans2);
                        (*planes)[i]->Translate(obj_trans3);
                        (*planes)[i]->Translate(trans2);
                        
                        p = (*planes)[i]->get_point();
                        n = (*planes)[i]->normal();
                        p = tform_copy.unproject(p);
                        n = tform->project(n);
                        n.safe_normalize();
                        (*planes)[i]->ChangePlaneTemp(p, n);
                    }
                }
                else
                {
                    for (int i = 0; i < 6; i++)
                    {
                        (*planes)[i]->Restore();
                        
                        p = (*planes)[i]->get_point();
                        n = (*planes)[i]->normal();
                        p = tform_copy.project(p);
                        n = tform->unproject(n);
                        n.safe_normalize();
                        (*planes)[i]->ChangePlaneTemp(p, n);
                        
                        (*planes)[i]->Translate(trans1);
                        (*planes)[i]->Rotate(q_cl);
                        (*planes)[i]->Translate(trans2);
                        
                        p = (*planes)[i]->get_point();
                        n = (*planes)[i]->normal();
                        p = tform_copy.unproject(p);
                        n = tform->project(n);
                        n.safe_normalize();
                        (*planes)[i]->ChangePlaneTemp(p, n);
                    }
                }
			}
		}
        
        vector<MeshData*> md_list(m_md_pop_list);
        for (size_t i=0; i<m_layer_list.size(); i++)
        {
            if (!m_layer_list[i])
                continue;
            if (m_layer_list[i]->IsA() == 4)
            {
                Annotations* ann = (Annotations*)m_layer_list[i];
                if (!ann) continue;
                if (ann->GetDisp() && ann->GetMesh())
                {
                    ann->GetMesh()->RecalcBounds();
                    md_list.push_back(ann->GetMesh());
                }
            }
        }
        for (int i=0; i<(int)md_list.size(); i++)
        {
            if (!md_list[i])
                continue;
            
            vector<Plane*> *planes = 0;
            
            Vector sz = md_list[i]->GetBounds().diagonal();
            Vector scale, scale2, scale2inv;
            scale = Vector(1.0/sz.x(), 1.0/sz.y(), 1.0/sz.z());
            scale.safe_normalize();
            scale2 = Vector(sz.x(), sz.y(), sz.z());
            //scale2.safe_normalize();
            scale2inv = Vector(1.0/sz.x(), 1.0/sz.y(), 1.0/sz.z());
            //scale2inv.safe_normalize();
            
            if (md_list[i]->GetMR())
                planes = md_list[i]->GetMR()->get_planes();
            if (planes && planes->size()==6)
            {
                double x1, x2, y1, y2, z1, z2;
                double abcd[4];
                
                (*planes)[0]->get_copy(abcd);
                x1 = fabs(abcd[3]);
                (*planes)[1]->get_copy(abcd);
                x2 = fabs(abcd[3]);
                (*planes)[2]->get_copy(abcd);
                y1 = fabs(abcd[3]);
                (*planes)[3]->get_copy(abcd);
                y2 = fabs(abcd[3]);
                (*planes)[4]->get_copy(abcd);
                z1 = fabs(abcd[3]);
                (*planes)[5]->get_copy(abcd);
                z2 = fabs(abcd[3]);
                
                Vector trans1(-0.5, -0.5, -0.5);
                Vector trans2(0.5, 0.5, 0.5);
                
                if (m_clip_mode == 3)
                {
                    Vector obj_trans1 = m_trans_fix;
                    Vector obj_trans2 = -obj_trans1;
                    Vector obj_trans3 = obj_trans1 - Vector(m_obj_transx, m_obj_transy, -m_obj_transz);
                    
                    for (int i = 0; i < 6; i++)
                    {
                        (*planes)[i]->Restore();
                        (*planes)[i]->Translate(trans1);
                        (*planes)[i]->Scale2(scale2);
                        (*planes)[i]->Rotate(m_q_cl_fix);
                        (*planes)[i]->Translate(obj_trans1);
                        (*planes)[i]->Rotate(m_q_cl);
                        (*planes)[i]->Translate(obj_trans2);
                        (*planes)[i]->Translate(obj_trans3);
                        (*planes)[i]->Scale2(scale2inv);
                        (*planes)[i]->Translate(trans2);
                    }
                }
                else
                {
                    (*planes)[0]->Restore();
                    (*planes)[0]->Translate(trans1);
                    (*planes)[0]->Scale2(scale2);
                    (*planes)[0]->Rotate(m_q_cl);
                    (*planes)[0]->Scale2(scale2inv);
                    (*planes)[0]->Translate(trans2);
                    
                    (*planes)[1]->Restore();
                    (*planes)[1]->Translate(trans1);
                    (*planes)[1]->Scale2(scale2);
                    (*planes)[1]->Rotate(m_q_cl);
                    (*planes)[1]->Scale2(scale2inv);
                    (*planes)[1]->Translate(trans2);
                    
                    (*planes)[2]->Restore();
                    (*planes)[2]->Translate(trans1);
                    (*planes)[2]->Scale2(scale2);
                    (*planes)[2]->Rotate(m_q_cl);
                    (*planes)[2]->Scale2(scale2inv);
                    (*planes)[2]->Translate(trans2);
                    
                    (*planes)[3]->Restore();
                    (*planes)[3]->Translate(trans1);
                    (*planes)[3]->Scale2(scale2);
                    (*planes)[3]->Rotate(m_q_cl);
                    (*planes)[3]->Scale2(scale2inv);
                    (*planes)[3]->Translate(trans2);
                    
                    (*planes)[4]->Restore();
                    (*planes)[4]->Translate(trans1);
                    (*planes)[4]->Scale2(scale2);
                    (*planes)[4]->Rotate(m_q_cl);
                    (*planes)[4]->Scale2(scale2inv);
                    (*planes)[4]->Translate(trans2);
                    
                    (*planes)[5]->Restore();
                    (*planes)[5]->Translate(trans1);
                    (*planes)[5]->Scale2(scale2);
                    (*planes)[5]->Rotate(m_q_cl);
                    (*planes)[5]->Scale2(scale2inv);
                    (*planes)[5]->Translate(trans2);
                }
            }
        }
	}
}

void VRenderVulkanView::A2Q()
{
	//view changed, re-sort bricks
	//SetSortBricks();
    
    //make rotation directions the same as the OpenGL version.
	//m_q.FromEuler(-m_rotx, m_roty, -m_rotz);

    m_q.FromEuler(m_rotx, m_roty, m_rotz);
    
	if (m_clip_mode)
	{
		if (m_clip_mode == 1)
			m_q_cl.FromEuler(-m_rotx, -m_roty, m_rotz);
		else if (m_clip_mode == 3)
		{
            m_q_cl_zero.FromEuler(-m_rotx, -m_roty, m_rotz);
            m_q_cl = m_q_fix * m_q_cl_zero;
            /*
			m_q_cl.ToEuler(m_rotx_cl, m_roty_cl, m_rotz_cl);
			if (m_rotx_cl > 180.0) m_rotx_cl -= 360.0;
			if (m_roty_cl > 180.0) m_roty_cl -= 360.0;
			if (m_rotz_cl > 180.0) m_rotz_cl -= 360.0;
			VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
			if (vr_frame)
			{
				ClippingView* clip_view = vr_frame->GetClippingView();
				if (clip_view)
					clip_view->SetClippingPlaneRotations(
						m_rotx_cl <= 180.0 ? m_rotx_cl : 360.0 - m_rotx_cl,
						m_roty_cl <= 180.0 ? m_roty_cl : 360.0 - m_roty_cl,
						m_rotz_cl <= 180.0 ? m_rotz_cl : 360.0 - m_rotz_cl,
						true);
			}
             */
		}

		for (int i=0; i<(int)m_vd_pop_list.size(); i++)
		{
			if (!m_vd_pop_list[i])
				continue;

			vector<Plane*> *planes = 0;
            Vector n;
            Point p;
			double spcx, spcy, spcz;
			int resx, resy, resz;
			m_vd_pop_list[i]->GetSpacings(spcx, spcy, spcz);
			m_vd_pop_list[i]->GetResolution(resx, resy, resz);
			Vector scale, scale2, scale2inv;
			if (spcx>0.0 && spcy>0.0 && spcz>0.0)
			{
				scale = Vector(1.0/resx/spcx, 1.0/resy/spcy, 1.0/resz/spcz);
				scale.safe_normalize();
                scale2 = Vector(resx*spcx, resy*spcy, resz*spcz);
                //scale2.safe_normalize();
                scale2inv = Vector(1.0/(resx*spcx), 1.0/(resy*spcy), 1.0/(resz*spcz));
                //scale2inv.safe_normalize();
			}
			else
            {
				scale = Vector(1.0, 1.0, 1.0);
                scale2 = Vector(1.0, 1.0, 1.0);
                scale2inv = Vector(1.0, 1.0, 1.0);
            }
            
            if (!m_vd_pop_list[i]->GetTexture())
                continue;
            Transform *tform = m_vd_pop_list[i]->GetTexture()->transform();
            if (!tform)
                continue;
            Transform tform_copy;
            double mvmat[16];
            tform->get_trans(mvmat);
            swap(mvmat[3], mvmat[12]);
            swap(mvmat[7], mvmat[13]);
            swap(mvmat[11], mvmat[14]);
            tform_copy.set(mvmat);
            
            double rotx, roty, rotz;
            Quaternion q_cl, q_cl_fix;
            m_q_cl.ToEuler(rotx, roty, rotz);
            q_cl.FromEuler(-rotx, -roty, rotz);
            m_q_cl_fix.ToEuler(rotx, roty, rotz);
            q_cl_fix.FromEuler(-rotx, -roty, rotz);

			if (m_vd_pop_list[i]->GetVR())
				planes = m_vd_pop_list[i]->GetVR()->get_planes();
			if (planes && planes->size()==6)
			{
				m_vd_pop_list[i]->GetVR()->set_clip_quaternion(m_q_cl);
				double x1, x2, y1, y2, z1, z2;
				double abcd[4];

				(*planes)[0]->get_copy(abcd);
				x1 = fabs(abcd[3]);
				(*planes)[1]->get_copy(abcd);
				x2 = fabs(abcd[3]);
				(*planes)[2]->get_copy(abcd);
				y1 = fabs(abcd[3]);
				(*planes)[3]->get_copy(abcd);
				y2 = fabs(abcd[3]);
				(*planes)[4]->get_copy(abcd);
				z1 = fabs(abcd[3]);
				(*planes)[5]->get_copy(abcd);
				z2 = fabs(abcd[3]);

                Vector trans1(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz);
                Vector trans2(m_obj_ctrx, m_obj_ctry, m_obj_ctrz);
                
                if (m_clip_mode == 3)
                {
                    Vector obj_trans1 = m_trans_fix;
                    Vector obj_trans2 = -obj_trans1;
                    Vector obj_trans3 = obj_trans1 - Vector(m_obj_transx, m_obj_transy, m_obj_transz);
                    
                    for (int i = 0; i < 6; i++)
                    {
                        (*planes)[i]->Restore();
                        
                        p = (*planes)[i]->get_point();
                        n = (*planes)[i]->normal();
                        p = tform_copy.project(p);
                        n = tform->unproject(n);
                        n.safe_normalize();
                        (*planes)[i]->ChangePlaneTemp(p, n);
                        
                        (*planes)[i]->Translate(trans1);
                        (*planes)[i]->Rotate(q_cl_fix);
                        (*planes)[i]->Translate(obj_trans1);
                        (*planes)[i]->Rotate(q_cl);
                        (*planes)[i]->Translate(obj_trans2);
                        (*planes)[i]->Translate(obj_trans3);
                        (*planes)[i]->Translate(trans2);
                        
                        p = (*planes)[i]->get_point();
                        n = (*planes)[i]->normal();
                        p = tform_copy.unproject(p);
                        n = tform->project(n);
                        n.safe_normalize();
                        (*planes)[i]->ChangePlaneTemp(p, n);
                    }
                }
                else
                {
                    for (int i = 0; i < 6; i++)
                    {
                        (*planes)[i]->Restore();
                        
                        p = (*planes)[i]->get_point();
                        n = (*planes)[i]->normal();
                        p = tform_copy.project(p);
                        n = tform->unproject(n);
                        n.safe_normalize();
                        (*planes)[i]->ChangePlaneTemp(p, n);
                        
                        (*planes)[i]->Translate(trans1);
                        (*planes)[i]->Rotate(q_cl);
                        (*planes)[i]->Translate(trans2);
                        
                        p = (*planes)[i]->get_point();
                        n = (*planes)[i]->normal();
                        p = tform_copy.unproject(p);
                        n = tform->project(n);
                        n.safe_normalize();
                        (*planes)[i]->ChangePlaneTemp(p, n);
                    }
                }
			}
		}
        
        vector<MeshData*> md_list(m_md_pop_list);
        for (size_t i=0; i<m_layer_list.size(); i++)
        {
            if (!m_layer_list[i])
                continue;
            if (m_layer_list[i]->IsA() == 4)
            {
                Annotations* ann = (Annotations*)m_layer_list[i];
                if (!ann) continue;
                if (ann->GetDisp() && ann->GetMesh())
                {
                    ann->GetMesh()->RecalcBounds();
                    md_list.push_back(ann->GetMesh());
                }
            }
        }
		for (int i=0; i<(int)md_list.size(); i++)
		{
			if (!md_list[i])
				continue;

			vector<Plane*> *planes = 0;

			Vector sz = md_list[i]->GetBounds().diagonal();
			Vector scale, scale2, scale2inv;
			scale = Vector(1.0/sz.x(), 1.0/sz.y(), 1.0/sz.z());
			scale.safe_normalize();
            scale2 = Vector(sz.x(), sz.y(), sz.z());
            //scale2.safe_normalize();
            scale2inv = Vector(1.0/sz.x(), 1.0/sz.y(), 1.0/sz.z());
            //scale2inv.safe_normalize();
			
			if (md_list[i]->GetMR())
				planes = md_list[i]->GetMR()->get_planes();
			if (planes && planes->size()==6)
			{
				double x1, x2, y1, y2, z1, z2;
				double abcd[4];

				(*planes)[0]->get_copy(abcd);
				x1 = fabs(abcd[3]);
				(*planes)[1]->get_copy(abcd);
				x2 = fabs(abcd[3]);
				(*planes)[2]->get_copy(abcd);
				y1 = fabs(abcd[3]);
				(*planes)[3]->get_copy(abcd);
				y2 = fabs(abcd[3]);
				(*planes)[4]->get_copy(abcd);
				z1 = fabs(abcd[3]);
				(*planes)[5]->get_copy(abcd);
				z2 = fabs(abcd[3]);

                Vector trans1(-0.5, -0.5, -0.5);
                Vector trans2(0.5, 0.5, 0.5);
                
                if (m_clip_mode == 3)
                {
                    Vector obj_trans1 = m_trans_fix;
                    Vector obj_trans2 = -obj_trans1;
                    Vector obj_trans3 = obj_trans1 - Vector(m_obj_transx, m_obj_transy, -m_obj_transz);
                    
                    for (int i = 0; i < 6; i++)
                    {
                        (*planes)[i]->Restore();
                        (*planes)[i]->Translate(trans1);
                        (*planes)[i]->Scale2(scale2);
                        (*planes)[i]->Rotate(m_q_cl_fix);
                        (*planes)[i]->Translate(obj_trans1);
                        (*planes)[i]->Rotate(m_q_cl);
                        (*planes)[i]->Translate(obj_trans2);
                        (*planes)[i]->Translate(obj_trans3);
                        (*planes)[i]->Scale2(scale2inv);
                        (*planes)[i]->Translate(trans2);
                    }
                }
                else
                {
                    (*planes)[0]->Restore();
                    (*planes)[0]->Translate(trans1);
                    (*planes)[0]->Scale2(scale2);
                    (*planes)[0]->Rotate(m_q_cl);
                    (*planes)[0]->Scale2(scale2inv);
                    (*planes)[0]->Translate(trans2);
                    
                    (*planes)[1]->Restore();
                    (*planes)[1]->Translate(trans1);
                    (*planes)[1]->Scale2(scale2);
                    (*planes)[1]->Rotate(m_q_cl);
                    (*planes)[1]->Scale2(scale2inv);
                    (*planes)[1]->Translate(trans2);
                    
                    (*planes)[2]->Restore();
                    (*planes)[2]->Translate(trans1);
                    (*planes)[2]->Scale2(scale2);
                    (*planes)[2]->Rotate(m_q_cl);
                    (*planes)[2]->Scale2(scale2inv);
                    (*planes)[2]->Translate(trans2);
                    
                    (*planes)[3]->Restore();
                    (*planes)[3]->Translate(trans1);
                    (*planes)[3]->Scale2(scale2);
                    (*planes)[3]->Rotate(m_q_cl);
                    (*planes)[3]->Scale2(scale2inv);
                    (*planes)[3]->Translate(trans2);
                    
                    (*planes)[4]->Restore();
                    (*planes)[4]->Translate(trans1);
                    (*planes)[4]->Scale2(scale2);
                    (*planes)[4]->Rotate(m_q_cl);
                    (*planes)[4]->Scale2(scale2inv);
                    (*planes)[4]->Translate(trans2);
                    
                    (*planes)[5]->Restore();
                    (*planes)[5]->Translate(trans1);
                    (*planes)[5]->Scale2(scale2);
                    (*planes)[5]->Rotate(m_q_cl);
                    (*planes)[5]->Scale2(scale2inv);
                    (*planes)[5]->Translate(trans2);
                }
			}
		}
	}
}

//sort bricks after view changes
void VRenderVulkanView::SetSortBricks()
{
	PopVolumeList();

	for (int i=0; i<(int)m_vd_pop_list.size(); i++)
	{
		VolumeData* vd = m_vd_pop_list[i];
		if (vd && vd->GetTexture())
			vd->GetTexture()->set_sort_bricks();
	}
}

void VRenderVulkanView::SetClipMode(int mode)
{
	switch (mode)
	{
	case 0:
		m_clip_mode = 0;
		RestorePlanes();
		m_rotx_cl = 0;
		m_roty_cl = 0;
		m_rotz_cl = 0;
		break;
	case 1:
		m_clip_mode = 1;
		SetRotations(m_rotx, m_roty, m_rotz);
		break;
	case 2:
		m_clip_mode = 2;
		m_q_cl_zero.FromEuler(-m_rotx, -m_roty, m_rotz);
		m_q_cl = m_q_cl_zero;
		m_q_cl.ToEuler(m_rotx_cl, m_roty_cl, m_rotz_cl);
		if (m_rotx_cl > 180.0) m_rotx_cl -= 360.0;
		if (m_roty_cl > 180.0) m_roty_cl -= 360.0;
		if (m_rotz_cl > 180.0) m_rotz_cl -= 360.0;
		SetRotations(m_rotx, m_roty, m_rotz);
		break;
	case 3:
		m_clip_mode = 3;
        m_rotx_cl_fix = m_rotx_cl;
        m_roty_cl_fix = m_roty_cl;
        m_rotz_cl_fix = m_rotz_cl;
        m_rotx_fix = m_rotx;
        m_roty_fix = m_roty;
        m_rotz_fix = m_rotz;
		m_q_cl_fix.FromEuler(m_rotx_cl, m_roty_cl, m_rotz_cl);
		m_q_fix.FromEuler(m_rotx, m_roty, m_rotz);
		m_q_fix.z *= -1;
		m_q_cl_zero.FromEuler(-m_rotx, -m_roty, m_rotz);
        m_trans_fix = Vector(m_obj_transx, m_obj_transy, -m_obj_transz);
		break;
	case 4:
		m_clip_mode = 2;
        m_rotx_cl = m_rotx_cl_fix;
        m_roty_cl = m_roty_cl_fix;
        m_rotz_cl = m_rotz_cl_fix;
        m_rotx = m_rotx_fix;
        m_roty = m_roty_fix;
        m_rotz = m_rotz_fix;
        m_q_cl.FromEuler(m_rotx_cl, m_roty_cl, m_rotz_cl);
        m_q_cl.Normalize();
		SetRotations(m_rotx, m_roty, m_rotz);
		break;
	}
}

void VRenderVulkanView::RestorePlanes()
{
	vector<Plane*> *planes = 0;
	for (int i=0; i<(int)m_vd_pop_list.size(); i++)
	{
		if (!m_vd_pop_list[i])
			continue;

		planes = 0;
		if (m_vd_pop_list[i]->GetVR())
			planes = m_vd_pop_list[i]->GetVR()->get_planes();
		if (planes && planes->size()==6)
		{
			(*planes)[0]->Restore();
			(*planes)[1]->Restore();
			(*planes)[2]->Restore();
			(*planes)[3]->Restore();
			(*planes)[4]->Restore();
			(*planes)[5]->Restore();
		}
	}
}

void VRenderVulkanView::SetClippingPlaneRotations(double rotx, double roty, double rotz)
{
	m_rotx_cl = rotx;
	m_roty_cl = roty;
	m_rotz_cl = -rotz;

	m_q_cl.FromEuler(m_rotx_cl, m_roty_cl, m_rotz_cl);
	m_q_cl.Normalize();

	SetRotations(m_rotx, m_roty, m_rotz);
}

void VRenderVulkanView::GetClippingPlaneRotations(double &rotx, double &roty, double &rotz)
{
	rotx = m_rotx_cl;
	roty = m_roty_cl;
	rotz = -m_rotz_cl;
}

//interpolation
void VRenderVulkanView::SetIntp(bool mode)
{
	m_intp = mode;
}

bool VRenderVulkanView::GetIntp()
{
	return m_intp;
}

void VRenderVulkanView::Run4DScript()
{
	for (int i = 0; i < (int)m_vd_pop_list.size(); ++i)
	{
		VolumeData* vd = m_vd_pop_list[i];
		if (vd)
			Run4DScript(m_script_file, vd);
	}
}

//start loop update
void VRenderVulkanView::StartLoopUpdate(bool reset_peeling_layer)
{
	////this is for debug_ds, comment when done
	//if (TextureRenderer::get_mem_swap() &&
	//  TextureRenderer::get_start_update_loop() &&
	//  !TextureRenderer::get_done_update_loop())
	//  return;

	//st_time = milliseconds_now();

	if (reset_peeling_layer) {
		m_finished_peeling_layer = 0;
		m_peeling_layer_progress = 0;
		m_mesh_rendering_progress = 0;
	}

	SetSortBricks();

	m_nx = GetSize().x;
	m_ny = GetSize().y;
	if (m_tile_rendering) {
		m_nx = m_tile_w;
		m_ny = m_tile_h;
	}

	int nx = m_nx;
	int ny = m_ny;

	if (m_fix_sclbar)
		m_sb_length = m_fixed_sclbar_fac/(m_scale_factor*min(nx,ny));

	//projection
	if (m_tile_rendering) HandleProjection(m_capture_resx, m_capture_resy);
	else HandleProjection(nx, ny);
	//Transformation
	HandleCamera();

	glm::mat4 mv_temp = m_mv_mat;
	//translate object
	m_mv_mat = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
	//rotate object
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
	//center object
	m_mv_mat = glm::translate(m_mv_mat, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

	PopMeshList();
	SetBufferScale(m_res_mode);

	int i, j, k;
	m_dpeel = false;
	m_mdtrans = false;
	for (i=0; i<m_md_pop_list.size(); i++)
	{
		MeshData *md = m_md_pop_list[i];
		if (!md) continue;
		Color amb, diff, spec;
		double shine, alpha;
		md->GetMaterial(amb, diff, spec, shine, alpha);
		if (alpha < 1.0) m_mdtrans = true;
	}

	//	if (TextureRenderer::get_mem_swap())
	{
		PopVolumeList();
		int total_num = 0;
		int num_chan;
		//reset drawn status for all bricks

		vector<VolumeData*> displist;
        vector<EmptyBlockDetectorQueue> ebd_queues;
		for (i=(int)m_layer_list.size()-1; i>=0; i--)
		{
			if (!m_layer_list[i])
				continue;
			switch (m_layer_list[i]->IsA())
			{
			case 5://group
				{
					DataGroup* group = (DataGroup*)m_layer_list[i];
					if (!group->GetDisp() || group->GetBlendMode() != VOL_METHOD_MULTI)
						continue;

					for (j=group->GetVolumeNum()-1; j>=0; j--)
					{
						VolumeData* vd = group->GetVolumeData(j);
						if (!vd || !vd->GetDisp())
							continue;
						Texture* tex = vd->GetTexture();
						if (!tex)
							continue;
						Ray view_ray = vd->GetVR()->compute_view();
						vector<TextureBrick*> *bricks = tex->get_sorted_bricks(view_ray, !m_persp);
						if (!bricks || bricks->size()==0)
							continue;
						displist.push_back(vd);
					}
				}
				break;
			}
		}
		m_dpeel = !(displist.empty() && m_vol_method != VOL_METHOD_MULTI);
		if (m_md_pop_list.size() == 0)
			m_dpeel = false;
//		if (reset_peeling_layer || m_vol_method == VOL_METHOD_MULTI)
			displist = m_vd_pop_list;
        
		for (i=0; i<displist.size(); i++)
		{
			VolumeData* vd = displist[i];
			if (vd && vd->GetDisp())
			{
#ifdef _DARWIN
                bool slice_mode = false;
                VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
                if ( vr_frame && vr_frame->GetClippingView() && (displist.size() == 1 || vr_frame->GetClippingView()->GetChannLink()) &&
                    (vr_frame->GetClippingView()->GetXdist() <= 3 || vr_frame->GetClippingView()->GetYdist() <= 3 || vr_frame->GetClippingView()->GetZdist() <= 3) )
                    slice_mode = true;
                if(slice_mode && vd->GetVR())
                    vd->GetVR()->set_slice_mode(true);
                else
                    vd->GetVR()->set_slice_mode(false);
#endif
                
				switchLevel(vd);
				vd->SetInterpolate(m_intp);
				if (!TextureRenderer::get_mem_swap()) continue;

				vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);

				num_chan = 0;
				Texture* tex = vd->GetTexture();
				if (tex && vd->GetVR())
				{
                    shared_ptr<VL_Nrrd> msk_nrrd;
					if (m_draw_mask && tex->isBrxml() && (vd->GetTexture()->nmask() >= 0 || !vd->GetSharedMaskName().IsEmpty()))
					{
						int curlv = vd->GetLevel();
						vd->SetLevel(vd->GetMaskLv());
                        msk_nrrd = tex->get_nrrd(tex->nmask());
						Transform *tform = tex->transform();
						double mvmat2[16];
						tform->get_trans(mvmat2);
						vd->GetVR()->m_mv_mat2 = glm::mat4(
							mvmat2[0], mvmat2[4], mvmat2[8], mvmat2[3],
							mvmat2[1], mvmat2[5], mvmat2[9], mvmat2[7],
							mvmat2[2], mvmat2[6], mvmat2[10], mvmat2[11],
							mvmat2[12], mvmat2[13], mvmat2[14], mvmat2[15]);
						vd->GetVR()->m_mv_mat2 = vd->GetVR()->m_mv_mat * vd->GetVR()->m_mv_mat2;

						Ray view_ray = vd->GetVR()->compute_view();

						vector<TextureBrick*> *bricks2 = tex->get_sorted_bricks(view_ray, !m_persp);
						if (!bricks2 || bricks2->size()==0)
							continue;
						for (j=0; j<bricks2->size(); j++)
						{
							(*bricks2)[j]->set_drawn(false);
							if ((*bricks2)[j]->get_priority()>0 ||
								!vd->GetVR()->test_against_view_clip((*bricks2)[j]->bbox(), (*bricks2)[j]->tbox(), (*bricks2)[j]->dbox(), m_persp) ||
                                !tex->GetFileName((*bricks2)[j]->getID()) ||
                                (!tex->GetFileName((*bricks2)[j]->getID())->isvalid && !tex->GetFileName((*bricks2)[j]->getID())->isurl))
							{
								(*bricks2)[j]->set_disp(false);
								continue;
							}
							else {
								(*bricks2)[j]->set_disp(true);
								if (tex->GetFileName((*bricks2)[j]->getID()) && !tex->GetFileName((*bricks2)[j]->getID())->isvalid && tex->GetFileName((*bricks2)[j]->getID())->isurl)
									tex->GetFileName((*bricks2)[j]->getID())->isvalid = true;
							}
							if (m_draw_mask && tex->nmask() != -1 && vd->GetMaskHideMode() == VOL_MASK_HIDE_NONE)
                            {
                                total_num++;
                                num_chan++;
                                if ((*bricks2)[j]->modified(tex->nmask()))
                                {
                                    EmptyBlockDetectorQueue eq((*bricks2)[j], tex->nmask(), tex->get_nrrd(tex->nmask()));
                                    if (curlv != vd->GetMaskLv())
                                        eq.msknrrd = (*bricks2)[j]->get_nrrd(tex->nmask());
                                    ebd_queues.push_back(eq);
                                }
                            }
							if (m_draw_label && tex->nlabel() != -1)
							{
								total_num++;
								num_chan++;
							}
						}
						vd->SetLevel(curlv);
					}

					Transform *tform = tex->transform();
					double mvmat[16];
					tform->get_trans(mvmat);
					vd->GetVR()->m_mv_mat2 = glm::mat4(
                                                       mvmat[0], mvmat[4], mvmat[8], mvmat[3],
                                                       mvmat[1], mvmat[5], mvmat[9], mvmat[7],
                                                       mvmat[2], mvmat[6], mvmat[10], mvmat[11],
                                                       mvmat[12], mvmat[13], mvmat[14], mvmat[15]);
					vd->GetVR()->m_mv_mat2 = vd->GetVR()->m_mv_mat * vd->GetVR()->m_mv_mat2;

					Ray view_ray = vd->GetVR()->compute_view();

					vector<TextureBrick*> *bricks = tex->get_sorted_bricks(view_ray, !m_persp);
					if (!bricks || bricks->size()==0)
						continue;
					for (j=0; j<bricks->size(); j++)
					{
						(*bricks)[j]->set_drawn(false);
						if ((*bricks)[j]->get_priority()>0 ||
							!vd->GetVR()->test_against_view_clip((*bricks)[j]->bbox(), (*bricks)[j]->tbox(), (*bricks)[j]->dbox(), m_persp) ||
                            (tex->isBrxml() && (!tex->GetFileName((*bricks)[j]->getID()) || (!tex->GetFileName((*bricks)[j]->getID())->isvalid && !tex->GetFileName((*bricks)[j]->getID())->isurl))) )
						{
							(*bricks)[j]->set_disp(false);
							continue;
						}
						else
						{
							(*bricks)[j]->set_disp(true);
							if (tex->isBrxml() && tex->GetFileName((*bricks)[j]->getID()) && !tex->GetFileName((*bricks)[j]->getID())->isvalid && tex->GetFileName((*bricks)[j]->getID())->isurl)
								tex->GetFileName((*bricks)[j]->getID())->isvalid = true;
						}
						total_num++;
						num_chan++;
						if (vd->GetShadow())
						{
							total_num++;
							num_chan++;
						}
						if (m_draw_mask && (tex->nmask() != -1 || !vd->GetSharedMaskName().IsEmpty()))
						{
                            if (!tex->isBrxml() && vd->GetMaskHideMode() == VOL_MASK_HIDE_NONE)
                            {
                                total_num++;
                                num_chan++;
                            }
                            if ((*bricks)[j]->modified(tex->nmask()))
                            {
                                EmptyBlockDetectorQueue eq((*bricks)[j], tex->nmask(), tex->get_nrrd(0), msk_nrrd);
                                ebd_queues.push_back(eq);
                            }
						}
						if (m_draw_label && tex->nlabel() != -1 && !tex->isBrxml())
						{
							total_num++;
							num_chan++;
						}
					}
				}
				vd->SetBrickNum(num_chan);
				if (vd->GetVR())
					vd->GetVR()->set_done_loop(false);
			}
		}

		vector<VolumeLoaderData> queues;
		if (m_vol_method == VOL_METHOD_MULTI)
		{
			vector<VolumeData*> list;
			for (i=0; i<displist.size(); i++)
			{
				VolumeData* vd = displist[i];
				if (!vd || !vd->GetDisp() || !vd->isBrxml())
					continue;
				Texture* tex = vd->GetTexture();
				if (!tex)
					continue;
				vector<TextureBrick*> *bricks = tex->get_bricks();
				if (!bricks || bricks->size()==0)
					continue;
				list.push_back(vd);
			}

			vector<VolumeLoaderData> tmp_shadow;
			for (i = 0; i < list.size(); i++)
			{
				VolumeData* vd = list[i];
				Texture* tex = vd->GetTexture();
				Ray view_ray = vd->GetVR()->compute_view();
				tex->set_sort_bricks();
				vector<TextureBrick*> *bricks = tex->get_sorted_bricks_dir(view_ray); // distance from view plane
				int mode = vd->GetMode() == 1 ? 1 : 0;
				bool shadow = vd->GetShadow();
				for (j = 0; j < bricks->size(); j++)
				{
					VolumeLoaderData d = {};
					TextureBrick* b = (*bricks)[j];
					if (b->get_disp())
					{
						d.brick = b;
						d.finfo = tex->GetFileName(b->getID());
						d.vd = vd;
						if (!b->drawn(mode))
						{
							d.mode = mode;
							queues.push_back(d);
						}
						if (shadow && !b->drawn(3))
						{
							d.mode = 3;
							tmp_shadow.push_back(d);
						}
					}
				}
			}
			if (TextureRenderer::get_update_order() == 1)
				std::sort(queues.begin(), queues.end(), VolumeLoader::sort_data_dsc);
			else if (TextureRenderer::get_update_order() == 0)
				std::sort(queues.begin(), queues.end(), VolumeLoader::sort_data_asc);

			if (!tmp_shadow.empty())
			{
				if (TextureRenderer::get_update_order() == 1)
				{
					int order = TextureRenderer::get_update_order();
					TextureRenderer::set_update_order(0);
					for (i = 0; i < list.size(); i++)
					{
						Ray view_ray = list[i]->GetVR()->compute_view();
						list[i]->GetTexture()->set_sort_bricks();
						list[i]->GetTexture()->get_sorted_bricks_dir(view_ray); // distance from view plane
						list[i]->GetTexture()->set_sort_bricks();
					}
					TextureRenderer::set_update_order(order);
					std::sort(tmp_shadow.begin(), tmp_shadow.end(), VolumeLoader::sort_data_asc);
				}
				else if (TextureRenderer::get_update_order() == 0)
					std::sort(tmp_shadow.begin(), tmp_shadow.end(), VolumeLoader::sort_data_asc);
				queues.insert(queues.end(), tmp_shadow.begin(), tmp_shadow.end());
			}

			//for mask
			if (m_draw_mask)
			{
				for (i = 0; i < list.size(); i++)
				{
					VolumeData* vd = list[i];
					if (vd->GetTexture()->nmask() < 0 && vd->GetSharedMaskName().IsEmpty()) continue;
					int curlevel = vd->GetLevel();
					vd->SetLevel(vd->GetMaskLv());
					Texture* tex = vd->GetTexture();
					if (tex->nmask() < 0) continue;
					Ray view_ray = vd->GetVR()->compute_view();
					vector<TextureBrick*> *bricks = tex->get_sorted_bricks(view_ray, !m_persp);
					int mode = TEXTURE_RENDER_MODE_MASK;
					for (j = 0; j < bricks->size(); j++)
					{
						VolumeLoaderData d = {};
						TextureBrick* b = (*bricks)[j];
						if (b->get_disp())
						{
							d.brick = b;
							d.finfo = tex->GetFileName(b->getID());
							d.vd = vd;
							if (!b->drawn(mode))
							{
								d.mode = mode;
								queues.push_back(d);
							}
						}
					}
					vd->SetLevel(curlevel);
				}
			}

		}
		else if (m_layer_list.size() > 0)
		{
			for (i=(int)m_layer_list.size()-1; i>=0; i--)
			{
				if (!m_layer_list[i])
					continue;
				switch (m_layer_list[i]->IsA())
				{
				case 5://group
					{
						vector<VolumeData*> list;
						DataGroup* group = (DataGroup*)m_layer_list[i];
						if (!group->GetDisp())
							continue;
						for (j=group->GetVolumeNum()-1; j>=0; j--)
						{
							VolumeData* vd = group->GetVolumeData(j);
							if (!vd || !vd->GetDisp() || !vd->isBrxml())
								continue;
							Texture* tex = vd->GetTexture();
							if (!tex)
								continue;
							Ray view_ray = vd->GetVR()->compute_view();
							vector<TextureBrick*> *bricks = tex->get_sorted_bricks(view_ray, !m_persp);
							if (!bricks || bricks->size()==0)
								continue;
							list.push_back(vd);
						}
						if (list.empty())
							continue;

						vector<VolumeLoaderData> tmp_q;
						vector<VolumeLoaderData> tmp_shadow;
						if (group->GetBlendMode() == VOL_METHOD_MULTI)
						{
							for (k = 0; k < list.size(); k++)
							{
								VolumeData* vd = list[k];
								Texture* tex = vd->GetTexture();
								Ray view_ray = vd->GetVR()->compute_view();
								tex->set_sort_bricks();
								vector<TextureBrick*> *bricks = tex->get_sorted_bricks_dir(view_ray); // distance from view plane
								int mode = vd->GetMode() == 1 ? 1 : 0;
								bool shadow = vd->GetShadow();
								for (j = 0; j < bricks->size(); j++)
								{
									VolumeLoaderData d = {};
									TextureBrick* b = (*bricks)[j];
									if (b->get_disp())
									{
										d.brick = b;
										d.finfo = tex->GetFileName(b->getID());
										d.vd = vd;
										if (!b->drawn(mode))
										{
											d.mode = mode;
											tmp_q.push_back(d);
										}
										if (shadow && !b->drawn(3))
										{
											d.mode = 3;
											tmp_shadow.push_back(d);
										}
									}
								}
							}
							if (!tmp_q.empty())
							{
								if (TextureRenderer::get_update_order() == 1)
									std::sort(tmp_q.begin(), tmp_q.end(), VolumeLoader::sort_data_dsc);
								else if (TextureRenderer::get_update_order() == 0)
									std::sort(tmp_q.begin(), tmp_q.end(), VolumeLoader::sort_data_asc);
								queues.insert(queues.end(), tmp_q.begin(), tmp_q.end());
							}
							if (!tmp_shadow.empty())
							{
								if (TextureRenderer::get_update_order() == 1)
								{
									int order = TextureRenderer::get_update_order();
									TextureRenderer::set_update_order(0);
									for (k = 0; k < list.size(); k++)
									{
										Ray view_ray = list[k]->GetVR()->compute_view();
										list[i]->GetTexture()->set_sort_bricks();
										list[i]->GetTexture()->get_sorted_bricks_dir(view_ray); // distance from view plane
										list[i]->GetTexture()->set_sort_bricks();
									}
									TextureRenderer::set_update_order(order);
									std::sort(tmp_shadow.begin(), tmp_shadow.end(), VolumeLoader::sort_data_asc);
								}
								else if (TextureRenderer::get_update_order() == 0)
									std::sort(tmp_shadow.begin(), tmp_shadow.end(), VolumeLoader::sort_data_asc);
								queues.insert(queues.end(), tmp_shadow.begin(), tmp_shadow.end());
							}
						}
						else
						{
							if (!reset_peeling_layer)
								continue;
							for (j = 0; j < list.size(); j++)
							{
								VolumeData* vd = list[j];
								Texture* tex = vd->GetTexture();
								Ray view_ray = vd->GetVR()->compute_view();
								tex->set_sort_bricks();
								vector<TextureBrick*> *bricks = tex->get_sorted_bricks(view_ray, !m_persp);
								int mode = vd->GetMode() == 1 ? 1 : 0;
								bool shadow = vd->GetShadow();
								for (k=0; k<bricks->size(); k++)
								{
									VolumeLoaderData d = {};
									TextureBrick* b = (*bricks)[k];
									if (b->get_disp())
									{
										d.brick = b;
										d.finfo = tex->GetFileName(b->getID());
										d.vd = vd;
										if (!b->drawn(mode))
										{
											d.mode = mode;
											queues.push_back(d);
										}
										if (shadow && !b->drawn(3))
										{
											d.mode = 3;
											tmp_shadow.push_back(d);
										}
									}
								}
								if (!tmp_shadow.empty())
								{
									if (TextureRenderer::get_update_order() == 1)
									{
										/*
										int order = TextureRenderer::get_update_order();
										TextureRenderer::set_update_order(0);
										Ray view_ray = vd->GetVR()->compute_view();
										tex->set_sort_bricks();
										tex->get_sorted_bricks(view_ray, !m_persp); //recalculate brick.d_
										tex->set_sort_bricks();
										TextureRenderer::set_update_order(order);
										*/
										std::sort(tmp_shadow.begin(), tmp_shadow.end(), VolumeLoader::sort_data_asc);
									}
									queues.insert(queues.end(), tmp_shadow.begin(), tmp_shadow.end());
								}
							}
						}

						//for mask
						if (m_draw_mask)
						{
							for (j = 0; j < list.size(); j++)
							{
								VolumeData* vd = list[j];
								if (vd->GetTexture()->nmask() < 0 && vd->GetSharedMaskName().IsEmpty()) continue;
								int curlevel = vd->GetLevel();
								vd->SetLevel(vd->GetMaskLv());
								Texture* tex = vd->GetTexture();
								Ray view_ray = vd->GetVR()->compute_view();
								vector<TextureBrick*> *bricks = tex->get_sorted_bricks(view_ray, !m_persp);
								int mode = TEXTURE_RENDER_MODE_MASK;
								for (k=0; k<bricks->size(); k++)
								{
									VolumeLoaderData d = {};
									TextureBrick* b = (*bricks)[k];
									if (b->get_disp())
									{
										d.brick = b;
										d.finfo = tex->GetFileName(b->getID());
										d.vd = vd;
										if (!b->drawn(mode))
										{
											d.mode = mode;
											queues.push_back(d);
										}
									}
								}
								vd->SetLevel(curlevel);
							}
						}

					}
					break;
				}
			}
		}

		if (1)
		{
			long long avmem = m_loader.GetAvailableMemory();

			int maxtime = 0;
			int maxcurtime = 0;
			vector<VolumeData*> vd_4d_list;
			for (i = 0; i < displist.size(); i++)
			{
				VolumeData* vd = displist[i];
				if (vd && vd->GetDisp() && !vd->isBrxml() && vd->GetReader() && 
					vd->GetReader()->GetCurTime() != vd->GetReader()->GetTimeNum())
				{
					int tnum = vd->GetReader()->GetTimeNum();
					if (tnum > maxtime)
						maxtime = tnum;
					vd_4d_list.push_back(vd);
				}
			}
			
			int toffset = 1;
			while (1)
			{
				bool remain = false;
				for (i = 0; i < vd_4d_list.size(); i++)
				{
					VolumeData* vd = vd_4d_list[i];
					if (vd->GetCurTime() + toffset < vd->GetReader()->GetTimeNum())
					{
						VolumeLoaderData d = {};
						d.chid = vd->GetCurChannel();
						d.frameid = vd->GetCurTime() + toffset;
						d.vd = vd;
						d.datasize = vd->GetDataSize();
						d.brick = NULL;
						queues.push_back(d);
						remain = true;
					}
				}
				if (!remain) break;
				toffset++;
			}
		}

		if (!m_ebd_run)
			ebd_queues.clear();
        
        bool runvl = false;
		if (queues.size() > 0 && !m_interactive)
		{
            for (auto q : queues)
            {
                if ( (q.brick && !q.brick->isLoaded() && !q.brick->isLoading()) ||
					 (q.vd && !q.vd->isBrxml()) )
                {
                    runvl = true;
                    break;
                }
            }
            if (runvl)
            {
                m_loader.Set(queues);
                m_loader.SetMemoryLimitByte((long long)TextureRenderer::mainmem_buf_size_*1024LL*1024LL);
                TextureRenderer::set_load_on_main_thread(false);
                
            }
		}
        if (ebd_queues.size() > 0)
        {
            m_ebd.Set(ebd_queues);
            if (runvl)
                m_ebd.SetMaxThreadNum((wxThread::GetCPUCount()-1)/2);
            else
                m_ebd.SetMaxThreadNum(wxThread::GetCPUCount()-1);
            m_ebd.Run();
            //m_ebd.Join();
        }
        if (runvl)
        {
            if (ebd_queues.size() > 0)
                m_loader.SetMaxThreadNum((wxThread::GetCPUCount()-1) - m_ebd.GetMaxThreadNum());
            else
                m_loader.SetMaxThreadNum(wxThread::GetCPUCount()-1);
            m_loader.Run();
        }

		if (total_num > 0)
		{
			TextureRenderer::set_update_loop();
			TextureRenderer::set_total_brick_num(total_num);
			TextureRenderer::reset_done_current_chan();
		}
		else if (m_md_pop_list.size() > 0)
		{
			TextureRenderer::set_update_loop();
			TextureRenderer::set_total_brick_num(0);
		}
		else
			TextureRenderer::set_total_brick_num(0);

		TextureRenderer::reset_save_final_buffer();
		TextureRenderer::invalidate_temp_final_buffer();
	}
}

//halt loop update
void VRenderVulkanView::HaltLoopUpdate()
{
	if (TextureRenderer::get_mem_swap())
	{
		TextureRenderer::reset_update_loop();
	}
}

//new function to refresh
void VRenderVulkanView::RefreshGL(bool erase, bool start_loop)
{
	m_draw_overlays_only = false;
	m_updating = true;
	if (start_loop)
		StartLoopUpdate();
	SetSortBricks();
	Refresh(erase);

	if (m_vulkan && (m_vulkan->width != m_vulkan->destWidth || m_vulkan->height != m_vulkan->destHeight))
	{
		m_vulkan->setSize(GetSize().GetWidth(), GetSize().GetHeight());
		m_refresh = true;
	}
}

//new function to refresh
void VRenderVulkanView::RefreshGLOverlays(bool erase)
{
	if (m_updating) return;
	if (TextureRenderer::get_mem_swap() && !TextureRenderer::get_done_update_loop()) return;

	m_draw_overlays_only = true;
	SetSortBricks();
	Refresh(erase);
	
	if (m_vulkan && (m_vulkan->width != m_vulkan->destWidth || m_vulkan->height != m_vulkan->destHeight))
	{
		m_vulkan->setSize(GetSize().GetWidth(), GetSize().GetHeight());
		m_refresh = true;
	}
}

//#ifdef __WXMAC__
void VRenderVulkanView::Refresh( bool eraseBackground, const wxRect *rect)
{
    UpdateScreen();
}
//#endif

double VRenderVulkanView::GetPointVolume(Point& mp, int mx, int my,
									 VolumeData* vd, int mode, bool use_transf, double thresh)
{
	if (!vd)
		return -1.0;
	Texture* tex = vd->GetTexture();
	if (!tex) return -1.0;
	Nrrd* nrrd = tex->get_nrrd_raw(0);
	if (!nrrd) return -1.0;
	void* data = nrrd->data;
	if (!data && !tex->isBrxml()) return -1.0;

	int nx = GetSize().x;
	int ny = GetSize().y;

	if (nx <= 0 || ny <= 0)
		return -1.0;

	glm::mat4 cur_mv_mat = m_mv_mat;
	glm::mat4 cur_proj_mat = m_proj_mat;

	//projection
	HandleProjection(nx, ny);
	//Transformation
	HandleCamera();
	glm::mat4 mv_temp;
	//translate object
	mv_temp = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
	//rotate object
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
	//center object
	mv_temp = glm::translate(mv_temp, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	p.invert();
	mv.invert();
	//transform mp1 and mp2 to object space
	Point mp1(x, y, 0.0);
	mp1 = p.transform(mp1);
	mp1 = mv.transform(mp1);
	Point mp2(x, y, 1.0);
	mp2 = p.transform(mp2);
	mp2 = mv.transform(mp2);

	//volume res
	int xx = -1;
	int yy = -1;
	int zz = -1;
	int tmp_xx, tmp_yy, tmp_zz;
	double spcx, spcy, spcz;
	vd->GetSpacings(spcx, spcy, spcz);
	int resx, resy, resz;
	vd->GetResolution(resx, resy, resz);
	//volume bounding box
	BBox bbox = vd->GetBounds();
	Vector vv = mp2 - mp1;
	vv.normalize();
	Point hit;
	double max_int = 0.0;
	double alpha = 0.0;
	double value = 0.0;
	vector<Plane*> *planes = 0;
	double mspc = 1.0;
	double return_val = -1.0;
	if (vd->GetSampleRate() > 0.0)
		mspc = sqrt(1.0 / (resx * resx) + 1.0 / (resy * resy) + 1.0 / (resz * resz)) / 2.0;
	if (vd->GetVR())
		planes = vd->GetVR()->get_planes();
	if (bbox.intersect(mp1, vv, hit))
	{
		Transform *textrans = tex->transform();
		Vector vv2 = textrans->unproject(vv);
		vv2.normalize();
		Point hp1 = textrans->unproject(hit);
		while (true)
		{
			tmp_xx = int(hp1.x() * resx);
			tmp_yy = int(hp1.y() * resy);
			tmp_zz = int(hp1.z() * resz);
			if (mode==1 &&
				tmp_xx==xx && tmp_yy==yy && tmp_zz==zz)
			{
				//same, skip
				hp1 += vv2 * mspc;
				continue;
			}
			else
			{
				xx = tmp_xx;
				yy = tmp_yy;
				zz = tmp_zz;
			}
			//out of bound, stop
			if (xx<0 || xx>resx ||
				yy<0 || yy>resy ||
				zz<0 || zz>resz)
				break;

			bool inside = true;
			if (planes)
			{
				for (int i = 0; i < 6; i++)
				{
					if ((*planes)[i] &&
						(*planes)[i]->eval_point(hp1) < 0.0)
					{
						inside = false;
						break;
					}
				}
			}
			if (inside)
			{
				xx = xx == resx ? resx - 1 : xx;
				yy = yy == resy ? resy - 1 : yy;
				zz = zz == resz ? resz - 1 : zz;

				if (use_transf)
					value = vd->GetTransferedValue(xx, yy, zz);
				else
					value = vd->GetOriginalValue(xx, yy, zz);

				if (mode == 1)
				{
					if (value > max_int)
					{
						mp = Point((xx + 0.5) / resx, (yy + 0.5) / resy, (zz + 0.5) / resz);
						mp = textrans->project(mp);
						max_int = value;
					}
				}
				else if (mode == 2)
				{
					//accumulate
					if (value > 0.0)
					{
						alpha = 1.0 - pow(Clamp(1.0 - value, 0.0, 1.0), vd->GetSampleRate());
						max_int += alpha * (1.0 - max_int);
						mp = Point((xx + 0.5) / resx, (yy + 0.5) / resy, (zz + 0.5) / resz);
						mp = textrans->project(mp);
					}
					if (max_int >= thresh)
						break;
				}
			}
			hp1 += vv2 * mspc;
		}
	}

	if (mode==1)
	{
		if (max_int > 0.0)
			return_val = (mp-mp1).length();
	}
	else if (mode==2)
	{
		if (max_int >= thresh)
			return_val = (mp-mp1).length();
	}

	m_mv_mat = cur_mv_mat;
	m_proj_mat = cur_proj_mat;
	return return_val;
}

double VRenderVulkanView::GetPointAndLabelMax(Point& mp, int& lblval, int mx, int my, VolumeData* vd, vector<VolumeData*> ref)
{
    if (!vd)
        return -1.0;
    Texture* tex = vd->GetTexture();
    if (!tex) return -1.0;
    Nrrd* nrrd = tex->get_nrrd_raw(tex->nlabel());
    if (!nrrd) return -1.0;
    void* data = nrrd->data;
    if (!data && !tex->isBrxml()) return -1.0;
    
    int nx = GetSize().x;
    int ny = GetSize().y;
    
    if (nx <= 0 || ny <= 0)
        return -1.0;
    
    glm::mat4 cur_mv_mat = m_mv_mat;
    glm::mat4 cur_proj_mat = m_proj_mat;
    
    //projection
    HandleProjection(nx, ny);
    //Transformation
    HandleCamera();
    glm::mat4 mv_temp;
    //translate object
    mv_temp = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
    //rotate object
    mv_temp = glm::rotate(mv_temp, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
    mv_temp = glm::rotate(mv_temp, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
    mv_temp = glm::rotate(mv_temp, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
    //center object
    mv_temp = glm::translate(mv_temp, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));
    
    Transform mv;
    Transform p;
    mv.set(glm::value_ptr(mv_temp));
    p.set(glm::value_ptr(m_proj_mat));
    
    double x, y;
    x = double(mx) * 2.0 / double(nx) - 1.0;
    y = double(my) * 2.0 / double(ny) - 1.0;
    p.invert();
    mv.invert();
    //transform mp1 and mp2 to object space
    Point mp1(x, y, 0.0);
    mp1 = p.transform(mp1);
    mp1 = mv.transform(mp1);
    Point mp2(x, y, 1.0);
    mp2 = p.transform(mp2);
    mp2 = mv.transform(mp2);
    
    //volume res
    int xx = -1;
    int yy = -1;
    int zz = -1;
    int tmp_xx, tmp_yy, tmp_zz;
    double spcx, spcy, spcz;
    vd->GetSpacings(spcx, spcy, spcz);
    int resx, resy, resz;
    vd->GetResolution(resx, resy, resz);
    //volume bounding box
    BBox bbox = vd->GetBounds();
    Vector vv = mp2 - mp1;
    vv.normalize();
    Point hit;
    int p_int = 0;
    double max_int = 0.0;
    int colvalue = 0;
    double alpha = 0.0;
    int value = 0;
    vector<Plane*>* planes = 0;
    double mspc = 1.0;
    double return_val = -1.0;
    if (vd->GetSampleRate() > 0.0)
        mspc = sqrt(1.0 / (resx * resx) + 1.0 / (resy * resy) + 1.0 / (resz * resz)) / 2.0;
    if (vd->GetVR())
        planes = vd->GetVR()->get_planes();
    if (bbox.intersect(mp1, vv, hit))
    {
        Transform* textrans = tex->transform();
        Vector vv2 = textrans->unproject(vv);
        vv2.normalize();
        Point hp1 = textrans->unproject(hit);
        while (true)
        {
            tmp_xx = int(hp1.x() * resx);
            tmp_yy = int(hp1.y() * resy);
            tmp_zz = int(hp1.z() * resz);
            if (tmp_xx == xx && tmp_yy == yy && tmp_zz == zz)
            {
                //same, skip
                hp1 += vv2 * mspc;
                continue;
            }
            else
            {
                xx = tmp_xx;
                yy = tmp_yy;
                zz = tmp_zz;
            }
            //out of bound, stop
            if (xx<0 || xx>resx ||
                yy<0 || yy>resy ||
                zz<0 || zz>resz)
                break;
            
            bool inside = true;
            if (planes)
            {
                for (int i = 0; i < 6; i++)
                {
                    if ((*planes)[i] &&
                        (*planes)[i]->eval_point(hp1) < 0.0)
                    {
                        inside = false;
                        break;
                    }
                }
            }
            if (inside)
            {
                xx = xx == resx ? resx - 1 : xx;
                yy = yy == resy ? resy - 1 : yy;
                zz = zz == resz ? resz - 1 : zz;
                
                value = vd->GetLabellValue(xx, yy, zz);
                
                if (value > 0 && vd->GetSegmentMask(value) > 0)
                {
                    bool ismax = false;
                    for (auto v : ref)
                    {
                        double dval = v->GetOriginalValue(xx, yy, zz);
                        if (dval > max_int)
                        {
                            max_int = dval;
                            ismax = true;
                        }
                    }
                    if (ismax)
                    {
                        mp = Point((xx + 0.5) / resx, (yy + 0.5) / resy, (zz + 0.5) / resz);
                        mp = textrans->project(mp);
                        p_int = value;
                    }
                }
            }
            hp1 += vv2 * mspc;
        }
    }
    
    if (p_int > 0)
        return_val = (mp - mp1).length();
    
    lblval = p_int;
    
    m_mv_mat = cur_mv_mat;
    m_proj_mat = cur_proj_mat;
    return return_val;
}

double VRenderVulkanView::GetPointAndLabel(Point& mp, int& lblval, int mx, int my, VolumeData* vd)
{
	if (!vd)
		return -1.0;
	Texture* tex = vd->GetTexture();
	if (!tex) return -1.0;
	Nrrd* nrrd = tex->get_nrrd_raw(tex->nlabel());
	if (!nrrd) return -1.0;
	void* data = nrrd->data;
	if (!data && !tex->isBrxml()) return -1.0;

	int nx = GetSize().x;
	int ny = GetSize().y;

	if (nx <= 0 || ny <= 0)
		return -1.0;

	glm::mat4 cur_mv_mat = m_mv_mat;
	glm::mat4 cur_proj_mat = m_proj_mat;

	//projection
	HandleProjection(nx, ny);
	//Transformation
	HandleCamera();
	glm::mat4 mv_temp;
	//translate object
	mv_temp = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
	//rotate object
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
	//center object
	mv_temp = glm::translate(mv_temp, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	p.invert();
	mv.invert();
	//transform mp1 and mp2 to object space
	Point mp1(x, y, 0.0);
	mp1 = p.transform(mp1);
	mp1 = mv.transform(mp1);
	Point mp2(x, y, 1.0);
	mp2 = p.transform(mp2);
	mp2 = mv.transform(mp2);

	//volume res
	int xx = -1;
	int yy = -1;
	int zz = -1;
	int tmp_xx, tmp_yy, tmp_zz;
	double spcx, spcy, spcz;
	vd->GetSpacings(spcx, spcy, spcz);
	int resx, resy, resz;
	vd->GetResolution(resx, resy, resz);
	//volume bounding box
	BBox bbox = vd->GetBounds();
	Vector vv = mp2 - mp1;
	vv.normalize();
	Point hit;
	int p_int = 0;
	double alpha = 0.0;
	int value = 0;
	vector<Plane*>* planes = 0;
	double mspc = 1.0;
	double return_val = -1.0;
	if (vd->GetSampleRate() > 0.0)
		mspc = sqrt(1.0 / (resx * resx) + 1.0 / (resy * resy) + 1.0 / (resz * resz)) / 2.0;
	if (vd->GetVR())
		planes = vd->GetVR()->get_planes();
	if (bbox.intersect(mp1, vv, hit))
	{
		Transform* textrans = tex->transform();
		Vector vv2 = textrans->unproject(vv);
		vv2.normalize();
		Point hp1 = textrans->unproject(hit);
		while (true)
		{
			tmp_xx = int(hp1.x() * resx);
			tmp_yy = int(hp1.y() * resy);
			tmp_zz = int(hp1.z() * resz);
			if (tmp_xx == xx && tmp_yy == yy && tmp_zz == zz)
			{
				//same, skip
				hp1 += vv2 * mspc;
				continue;
			}
			else
			{
				xx = tmp_xx;
				yy = tmp_yy;
				zz = tmp_zz;
			}
			//out of bound, stop
			if (xx<0 || xx>resx ||
				yy<0 || yy>resy ||
				zz<0 || zz>resz)
				break;

			bool inside = true;
			if (planes)
			{
				for (int i = 0; i < 6; i++)
				{
					if ((*planes)[i] &&
						(*planes)[i]->eval_point(hp1) < 0.0)
					{
						inside = false;
						break;
					}
				}
			}
			if (inside)
			{
				xx = xx == resx ? resx - 1 : xx;
				yy = yy == resy ? resy - 1 : yy;
				zz = zz == resz ? resz - 1 : zz;

				value = vd->GetLabellValue(xx, yy, zz);

				if (value > 0 && vd->GetSegmentMask(value) > 0)
				{
					mp = Point((xx + 0.5) / resx, (yy + 0.5) / resy, (zz + 0.5) / resz);
					mp = textrans->project(mp);
					p_int = value;
					break;
				}
			}
			hp1 += vv2 * mspc;
		}
	}

	if (p_int > 0)
		return_val = (mp - mp1).length();

	lblval = p_int;

	m_mv_mat = cur_mv_mat;
	m_proj_mat = cur_proj_mat;
	return return_val;
}

double VRenderVulkanView::GetPointAndIntVolume(Point& mp, double &intensity, bool normalize, int mx, int my, VolumeData* vd, double thresh)
{
	if (!vd)
		return -1.0;
	Texture* tex = vd->GetTexture();
	if (!tex) return -1.0;
	Nrrd* nrrd = tex->get_nrrd_raw(0);
	if (!nrrd) return -1.0;
	void* data = nrrd->data;
	if (!data && !tex->isBrxml()) return -1.0;

	int nx = GetSize().x;
	int ny = GetSize().y;

	if (nx <= 0 || ny <= 0)
		return -1.0;

	glm::mat4 cur_mv_mat = m_mv_mat;
	glm::mat4 cur_proj_mat = m_proj_mat;

	//projection
	HandleProjection(nx, ny);
	//Transformation
	HandleCamera();
	glm::mat4 mv_temp;
	//translate object
	mv_temp = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
	//rotate object
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
	//center object
	mv_temp = glm::translate(mv_temp, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	p.invert();
	mv.invert();
	//transform mp1 and mp2 to object space
	Point mp1(x, y, 0.0);
	mp1 = p.transform(mp1);
	mp1 = mv.transform(mp1);
	Point mp2(x, y, 1.0);
	mp2 = p.transform(mp2);
	mp2 = mv.transform(mp2);

	//volume res
	int xx = -1;
	int yy = -1;
	int zz = -1;
	int tmp_xx, tmp_yy, tmp_zz;
	double spcx, spcy, spcz;
	vd->GetSpacings(spcx, spcy, spcz);
	int resx, resy, resz;
	vd->GetResolution(resx, resy, resz);
	//volume bounding box
	BBox bbox = vd->GetBounds();
	Vector vv = mp2 - mp1;
	vv.normalize();
	Point hit;
	double p_int = 0.0;
	double alpha = 0.0;
	double value = 0.0;
	vector<Plane*> *planes = 0;
	double mspc = 1.0;
	double return_val = -1.0;

	double scx, scy, scz;
	scx = scy = scz = 1.0;
	if (vd->isBrxml())
	{
		int64_t vnx = (int64_t)(nrrd->axis[0].size);
		int64_t vny = (int64_t)(nrrd->axis[1].size);
		int64_t vnz = (int64_t)(nrrd->axis[2].size);
		scx = (double)vnx / resx; scy = (double)vny / resy; scz = (double)vnz / resz;
	}

	if (vd->GetSampleRate() > 0.0)
		mspc = sqrt(1.0 / (resx * scx * resx * scx) + 1.0 / (resy * scy * resy * scy) + 1.0 / (resz * scz * resz * scz)) / 2.0;
	if (vd->GetVR())
		planes = vd->GetVR()->get_planes();
	if (bbox.intersect(mp1, vv, hit))
	{
		Transform* textrans = tex->transform();
		Vector vv2 = textrans->unproject(vv);
		vv2.normalize();
		Point hp1 = textrans->unproject(hit);
		while (true)
		{
			tmp_xx = int(hp1.x() * resx);
			tmp_yy = int(hp1.y() * resy);
			tmp_zz = int(hp1.z() * resz);
			if ( (!vd->isBrxml() && tmp_xx==xx && tmp_yy==yy && tmp_zz==zz) || 
				(vd->isBrxml() && (int)(tmp_xx*scx) == (int)(xx*scx) && (int)(tmp_yy*scy) == (int)(yy*scy) && (int)(tmp_zz*scz) == (int)(zz*scz)) )
			{
				//same, skip
				hp1 += vv2 * mspc;
				continue;
			}
			else
			{
				xx = tmp_xx;
				yy = tmp_yy;
				zz = tmp_zz;
			}
			//out of bound, stop
			if (xx<0 || xx>resx ||
				yy<0 || yy>resy ||
				zz<0 || zz>resz)
				break;

			bool inside = true;
			if (planes)
			{
				for (int i = 0; i < 6; i++)
				{
					if ((*planes)[i] &&
						(*planes)[i]->eval_point(hp1) < 0.0)
					{
						inside = false;
						break;
					}
				}
			}
			if (inside)
			{
				xx = xx==resx?resx-1:xx;
				yy = yy==resy?resy-1:yy;
				zz = zz==resz?resz-1:zz;

				value = vd->GetOriginalValue(xx, yy, zz, normalize);

				if (vd->GetColormapMode() == 3)
				{
					unsigned char r=0, g=0, b=0;
					vd->GetRenderedIDColor(r, g, b, (int)value);
					if (r == 0 && g == 0 && b == 0)
					{
						hp1 += vv2 * mspc;
						continue;
					}
				}

				if (value >= thresh)
				{
					mp = Point((xx + 0.5) / resx, (yy + 0.5) / resy, (zz + 0.5) / resz);
					mp = textrans->project(mp);
					p_int = value;
					break;
				}
			}
			hp1 += vv2 * mspc;
		}
	}

	if (p_int >= thresh)
		return_val = (mp-mp1).length();

	intensity = p_int;

	m_mv_mat = cur_mv_mat;
	m_proj_mat = cur_proj_mat;
	return return_val;
}

double VRenderVulkanView::GetPointVolumeBox(Point &mp, int mx, int my, VolumeData* vd, bool calc_mats)
{
	if (!vd)
		return -1.0;
	int nx = GetSize().x;
	int ny = GetSize().y;
	if (nx <= 0 || ny <= 0)
		return -1.0;
	vector<Plane*> *planes = vd->GetVR()->get_planes();
	if (planes->size() != 6)
		return -1.0;

	Transform mv;
	Transform p;
	glm::mat4 mv_temp = m_mv_mat;
	Transform *tform = vd->GetTexture()->transform();
	double mvmat[16];
	tform->get_trans(mvmat);

	glm::mat4 cur_mv_mat;
	glm::mat4 cur_proj_mat;

	if (calc_mats)
	{
		cur_mv_mat = m_mv_mat;
		cur_proj_mat = m_proj_mat;

		//projection
		HandleProjection(nx, ny);
		//Transformation
		HandleCamera();
		//translate object
		mv_temp = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
		//rotate object
		mv_temp = glm::rotate(mv_temp, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
		mv_temp = glm::rotate(mv_temp, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
		mv_temp = glm::rotate(mv_temp, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
		//center object
		mv_temp = glm::translate(mv_temp, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));
	}
	else
		mv_temp = m_mv_mat;

	glm::mat4 mv_mat2 = glm::mat4(
		mvmat[0], mvmat[4], mvmat[8], mvmat[12],
		mvmat[1], mvmat[5], mvmat[9], mvmat[13],
		mvmat[2], mvmat[6], mvmat[10], mvmat[14],
		mvmat[3], mvmat[7], mvmat[11], mvmat[15]);
	mv_temp = mv_temp * mv_mat2;

	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	p.invert();
	mv.invert();
	//transform mp1 and mp2 to object space
	Point mp1(x, y, 0.0);
	mp1 = p.transform(mp1);
	mp1 = mv.transform(mp1);
	Point mp2(x, y, 1.0);
	mp2 = p.transform(mp2);
	mp2 = mv.transform(mp2);
	Vector ray_d = mp1-mp2;
	ray_d.normalize();
	Ray ray(mp1, ray_d);
	double mint = -1.0;
	double t;
	//for each plane, calculate the intersection point
	Plane* plane = 0;
	Point pp;//a point on plane
	int i, j;
	bool pp_out;
	for (i=0; i<6; i++)
	{
		plane = (*planes)[i];
		FLIVR::Vector vec = plane->normal();
		FLIVR::Point pnt = plane->get_point();
		if (ray.planeIntersectParameter(vec,pnt, t))
		{
			pp = ray.parameter(t);

			pp_out = false;
			//determine if the point is inside the box
			for (j=0; j<6; j++)
			{
				if (j == i)
					continue;
				if ((*planes)[j]->eval_point(pp) < 0)
				{
					pp_out = true;
					break;
				}
			}

			if (!pp_out)
			{
				if (t > mint)
				{
					mp = pp;
					mint = t;
				}
			}
		}
	}

	mp = tform->transform(mp);

	if (calc_mats)
	{
		m_mv_mat = cur_mv_mat;
		m_proj_mat = cur_proj_mat;
	}

	return mint;
}

double VRenderVulkanView::GetPointVolumeBox2(Point &p1, Point &p2, int mx, int my, VolumeData* vd)
{
	if (!vd)
		return -1.0;
	int nx = GetSize().x;
	int ny = GetSize().y;
	if (nx <= 0 || ny <= 0)
		return -1.0;
	vector<Plane*> *planes = vd->GetVR()->get_planes();
	if (planes->size() != 6)
		return -1.0;

	glm::mat4 cur_mv_mat = m_mv_mat;
	glm::mat4 cur_proj_mat = m_proj_mat;

	//projection
	HandleProjection(nx, ny);
	//Transformation
	HandleCamera();
	glm::mat4 mv_temp;
	//translate object
	mv_temp = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
	//rotate object
	mv_temp = glm::rotate(mv_temp, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
	//center object
	mv_temp = glm::translate(mv_temp, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));
	Transform *tform = vd->GetTexture()->transform();
	double mvmat[16];
	tform->get_trans(mvmat);
	glm::mat4 mv_mat2 = glm::mat4(
		mvmat[0], mvmat[4], mvmat[8], mvmat[12],
		mvmat[1], mvmat[5], mvmat[9], mvmat[13],
		mvmat[2], mvmat[6], mvmat[10], mvmat[14],
		mvmat[3], mvmat[7], mvmat[11], mvmat[15]);
	mv_temp = mv_temp * mv_mat2;

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	p.invert();
	mv.invert();
	//transform mp1 and mp2 to object space
	Point mp1(x, y, 0.0);
	mp1 = p.transform(mp1);
	mp1 = mv.transform(mp1);
	Point mp2(x, y, 1.0);
	mp2 = p.transform(mp2);
	mp2 = mv.transform(mp2);
	Vector ray_d = mp1-mp2;
	ray_d.normalize();
	Ray ray(mp1, ray_d);
	double mint = -1.0;
	double maxt = std::numeric_limits<double>::max();
	double t;
	//for each plane, calculate the intersection point
	Plane* plane = 0;
	Point pp;//a point on plane
	int i, j;
	bool pp_out;
	for (i=0; i<6; i++)
	{
		plane = (*planes)[i];
		FLIVR::Vector vec = plane->normal();
		FLIVR::Point pnt = plane->get_point();
		if (ray.planeIntersectParameter(vec,pnt, t))
		{
			pp = ray.parameter(t);

			pp_out = false;
			//determine if the point is inside the box
			for (j=0; j<6; j++)
			{
				if (j == i)
					continue;
				if ((*planes)[j]->eval_point(pp) < 0)
				{
					pp_out = true;
					break;
				}
			}

			if (!pp_out)
			{
				if (t > mint)
				{
					p1 = pp;
					mint = t;
				}
				if (t < maxt)
				{
					p2 = pp;
					maxt = t;
				}
			}
		}
	}

	p1 = tform->transform(p1);
	p2 = tform->transform(p2);

	m_mv_mat = cur_mv_mat;
	m_proj_mat = cur_proj_mat;

	return mint;
}

double VRenderVulkanView::GetPointPlane(Point &mp, int mx, int my, Point* planep, bool calc_mats)
{
	int nx = GetSize().x;
	int ny = GetSize().y;

	if (nx <= 0 || ny <= 0)
		return -1.0;

	glm::mat4 mv_temp;
	glm::mat4 cur_mv_mat;
	glm::mat4 cur_proj_mat;

	if (calc_mats)
	{
		cur_mv_mat = m_mv_mat;
		cur_proj_mat = m_proj_mat;

		//projection
		HandleProjection(nx, ny);
		//Transformation
		HandleCamera();
		//translate object
		mv_temp = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
		//rotate object
		mv_temp = glm::rotate(mv_temp, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
		mv_temp = glm::rotate(mv_temp, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
		mv_temp = glm::rotate(mv_temp, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
		//center object
		mv_temp = glm::translate(mv_temp, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));
	}
	else
		mv_temp = m_mv_mat;

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	Vector n(0.0, 0.0, 1.0);
	Point center(0.0, 0.0, -m_distance);
	if (planep)
	{
		center = *planep;
		center = mv.transform(center);
	}
	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	p.invert();
	mv.invert();
	//transform mp1 and mp2 to eye space
	Point mp1(x, y, 0.0);
	mp1 = p.transform(mp1);
	Point mp2(x, y, 1.0);
	mp2 = p.transform(mp2);
	FLIVR::Vector vec = mp2-mp1;
	Ray ray(mp1, vec);
	double t = 0.0;
	if (ray.planeIntersectParameter(n, center, t))
		mp = ray.parameter(t);
	//transform mp to world space
	mp = mv.transform(mp);

	if (calc_mats)
	{
		m_mv_mat = cur_mv_mat;
		m_proj_mat = cur_proj_mat;
	}

	return (mp-mp1).length();
}

Point* VRenderVulkanView::GetEditingRulerPoint(int mx, int my)
{
	Point* point = 0;

	int nx = GetSize().x;
	int ny = GetSize().y;

	if (nx <= 0 || ny <= 0)
		return 0;

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	double aspect = (double)nx / (double)ny;

	glm::mat4 cur_mv_mat = m_mv_mat;
	glm::mat4 cur_proj_mat = m_proj_mat;

	//projection
	HandleProjection(nx, ny);
	//Transformation
	HandleCamera();
	glm::mat4 mv_temp;
	//translate object
	mv_temp = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
	//rotate object
	mv_temp = glm::rotate(mv_temp, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
	//center object
	mv_temp = glm::translate(mv_temp, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	int i, j;
	Point ptemp;
	bool found = false;
	for (i=0; i<(int)m_ruler_list.size() && !found; i++)
	{
		Ruler* ruler = m_ruler_list[i];
		if (!ruler) continue;
        
        bool skip = true;
        if (m_cur_vol)
        {
            for (size_t j=0; j<ruler->GetNumPoint(); ++j)
            {
                if (m_cur_vol->InsideClippingPlanes(*(ruler->GetPoint(j))))
                {
                    skip = false;
                    break;
                }
            }
        }
        else
            skip = false;
        
        if (skip)
            continue;

		for (j=0; j<ruler->GetNumPoint(); j++)
		{
			point = ruler->GetPoint(j);
			if (!point) continue;
			if (!m_cur_vol->InsideClippingPlanes(*point)) continue;
			ptemp = *point;
			ptemp = mv.transform(ptemp);
			ptemp = p.transform(ptemp);
			if (ptemp.z()<=0.0 || ptemp.z()>=1.0)
				continue;
			if (x<ptemp.x()+0.02/aspect &&
				x>ptemp.x()-0.02/aspect &&
				y<ptemp.y()+0.02 &&
				y>ptemp.y()-0.02)
			{
				found = true;
				break;
			}
		}
	}

	if(!found && m_draw_landmarks)
	{
		for (i=0; i<(int)m_landmarks.size() && !found; i++)
		{
			Ruler* ruler = m_landmarks[i];
			if (!ruler) continue;

			for (j=0; j<ruler->GetNumPoint(); j++)
			{
				point = ruler->GetPoint(j);
				if (!point) continue;
				ptemp = *point;
				ptemp = mv.transform(ptemp);
				ptemp = p.transform(ptemp);
				if (ptemp.z()<=0.0 || ptemp.z()>=1.0)
					continue;
				if (x<ptemp.x()+0.02/aspect &&
					x>ptemp.x()-0.02/aspect &&
					y<ptemp.y()+0.02 &&
					y>ptemp.y()-0.02)
				{
					found = true;
					break;
				}
			}
		}
	}

	m_mv_mat = cur_mv_mat;
	m_proj_mat = cur_proj_mat;

	if (found)
		return point;
	else
		return 0;
}

//added by takashi
bool VRenderVulkanView::SwitchRulerBalloonVisibility_Point(int mx, int my)
{
	Point* point = 0;

	int nx = GetSize().x;
	int ny = GetSize().y;

	if (nx <= 0 || ny <= 0)
		return 0;

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	double aspect = (double)nx / (double)ny;

	glm::mat4 cur_mv_mat = m_mv_mat;
	glm::mat4 cur_proj_mat = m_proj_mat;

	//projection
	HandleProjection(nx, ny);
	//Transformation
	HandleCamera();
	glm::mat4 mv_temp;
	//translate object
	mv_temp = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
	//rotate object
	mv_temp = glm::rotate(mv_temp, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
	//center object
	mv_temp = glm::translate(mv_temp, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	int i, j;
	Point ptemp;
	bool found = false;
	for (i=0; i<(int)m_ruler_list.size() && !found; i++)
	{
		Ruler* ruler = m_ruler_list[i];
		if (!ruler) continue;

		for (j=0; j<ruler->GetNumPoint(); j++)
		{
			point = ruler->GetPoint(j);
			if (!point) continue;
			ptemp = *point;
			ptemp = mv.transform(ptemp);
			ptemp = p.transform(ptemp);
			if (ptemp.z()<=0.0 || ptemp.z()>=1.0)
				continue;
			if (x<ptemp.x()+0.02/aspect &&
				x>ptemp.x()-0.02/aspect &&
				y<ptemp.y()+0.02 &&
				y>ptemp.y()-0.02)
			{
				ruler->SetBalloonVisibility(j, !ruler->GetBalloonVisibility(j));
				found = true;
				break;
			}
		}
	}

	if(!found)
	{
		for (i=0; i<(int)m_landmarks.size() && !found; i++)
		{
			Ruler* ruler = m_landmarks[i];
			if (!ruler) continue;

			for (j=0; j<ruler->GetNumPoint(); j++)
			{
				point = ruler->GetPoint(j);
				if (!point) continue;
				ptemp = *point;
				ptemp = mv.transform(ptemp);
				ptemp = p.transform(ptemp);
				if (ptemp.z()<=0.0 || ptemp.z()>=1.0)
					continue;
				if (x<ptemp.x()+0.02/aspect &&
					x>ptemp.x()-0.02/aspect &&
					y<ptemp.y()+0.02 &&
					y>ptemp.y()-0.02)
				{
					ruler->SetBalloonVisibility(j, !ruler->GetBalloonVisibility(j));
					found = true;
					break;
				}
			}
		}
	}

	m_mv_mat = cur_mv_mat;
	m_proj_mat = cur_proj_mat;

	if (found)
		return true;
	else
		return false;
}

int VRenderVulkanView::GetRulerType()
{
	return m_ruler_type;
}

void VRenderVulkanView::SetRulerType(int type)
{
	m_ruler_type = type;
}

void VRenderVulkanView::FinishRuler()
{
	size_t size = m_ruler_list.size();
	if (!size) return;
	if (m_ruler_list[size-1] &&
		m_ruler_list[size-1]->GetRulerType() == 1)
		m_ruler_list[size-1]->SetFinished();
}

bool VRenderVulkanView::GetRulerFinished()
{
	size_t size = m_ruler_list.size();
	if (!size) return true;
	if (m_ruler_list[size-1])
		return m_ruler_list[size-1]->GetFinished();
	else
		return true;
}

void VRenderVulkanView::AddRulerPoint(int mx, int my)
{
	if (m_ruler_type == 3)
	{
		Point p1, p2;
		Ruler* ruler = new Ruler();
		ruler->SetRulerType(m_ruler_type);
		GetPointVolumeBox2(p1, p2, mx, my, m_cur_vol);
		ruler->AddPoint(p1);
		ruler->AddPoint(p2);
		ruler->SetTimeDep(m_ruler_time_dep);
		ruler->SetTime(m_tseq_cur_num);
		m_ruler_list.push_back(ruler);
	}
	else
	{
		Point p;
		if (m_point_volume_mode)
		{
			double t = GetPointVolume(p, mx, my, m_cur_vol,
				m_point_volume_mode, m_ruler_use_transf);
			if (t <= 0.0)
			{
				t = GetPointPlane(p, mx, my);
				if (t <= 0.0)
					return;
			}
		}
		else
		{
			double t = GetPointPlane(p, mx, my);
			if (t <= 0.0)
				return;
		}

		bool new_ruler = true;
		if (m_ruler_list.size())
		{
			Ruler* ruler = m_ruler_list[m_ruler_list.size()-1];
			if (ruler && !ruler->GetFinished())
			{
				ruler->AddPoint(p);
				new_ruler = false;
			}
		}
		if (new_ruler)
		{
			Ruler* ruler = new Ruler();
			ruler->SetRulerType(m_ruler_type);
			ruler->AddPoint(p);
			ruler->SetTimeDep(m_ruler_time_dep);
			ruler->SetTime(m_tseq_cur_num);
			m_ruler_list.push_back(ruler);
		}
	}

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (m_vrv && vr_frame && vr_frame->GetMeasureDlg())
		vr_frame->GetMeasureDlg()->GetSettings(m_vrv);
}

void VRenderVulkanView::AddPaintRulerPoint()
{
	if (m_selector.ProcessSel(0.01))
	{
		wxString str;
		Point center;
		double size;
		m_selector.GetCenter(center);
		m_selector.GetSize(size);

		bool new_ruler = true;
		if (m_ruler_list.size())
		{
			Ruler* ruler = m_ruler_list[m_ruler_list.size()-1];
			if (ruler && !ruler->GetFinished())
			{
				ruler->AddPoint(center);
				str = wxString::Format("\tv%d", ruler->GetNumPoint()-1);
				ruler->AddInfoNames(str);
				str = wxString::Format("\t%.0f", size);
				ruler->AddInfoValues(str);
				new_ruler = false;
			}
		}
		if (new_ruler)
		{
			Ruler* ruler = new Ruler();
			ruler->SetRulerType(m_ruler_type);
			ruler->AddPoint(center);
			ruler->SetTimeDep(m_ruler_time_dep);
			ruler->SetTime(m_tseq_cur_num);
			str = "v0";
			ruler->AddInfoNames(str);
			str = wxString::Format("%.0f", size);
			ruler->AddInfoValues(str);
			m_ruler_list.push_back(ruler);
		}

		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (m_vrv && vr_frame && vr_frame->GetMeasureDlg())
			vr_frame->GetMeasureDlg()->GetSettings(m_vrv);
	}
}

void VRenderVulkanView::DrawRulers()
{
	if (!m_text_renderer)
		return;

	int nx = GetSize().x;
	int ny = GetSize().y;
	float sx, sy;
	sx = 2.0/nx;
	sy = 2.0/ny;
	float w = m_text_renderer->GetSize()/4.0f;
	float px, py, p2x, p2y;

	vector<Vulkan2dRender::Vertex> verts;
	vector<uint32_t> index;
	int vert_num = 0;
	for (size_t i=0; i<m_ruler_list.size(); ++i)
		if (m_ruler_list[i])
			vert_num += m_ruler_list[i]->GetNumPoint();
	verts.reserve(vert_num*4);
	index.reserve(vert_num*8);

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(m_mv_mat));
	p.set(glm::value_ptr(m_proj_mat));
	Point p1, p2;
	Color color;
	Color text_color = GetTextColor();

	double spcx = 1.0;
	double spcy = 1.0;
	double spcz = 1.0;

	int fsize = m_text_renderer->GetSize();

	if(m_cur_vol){
		Texture *vtex = m_cur_vol->GetTexture();
		if (vtex && vtex->isBrxml())
		{
			BRKXMLReader *br = (BRKXMLReader *)m_cur_vol->GetReader();
			br->SetLevel(0);
			spcx = br->GetXSpc();
			spcy = br->GetYSpc();
			spcz = br->GetZSpc();
		}
	}

	uint32_t vnum = 0;
	for (size_t i=0; i<m_ruler_list.size(); i++)
	{
		Ruler* ruler = m_ruler_list[i];
		if (!ruler) continue;
		if (!ruler->GetTimeDep() ||
			(ruler->GetTimeDep() &&
			ruler->GetTime() == m_tseq_cur_num))
		{
            bool skip = true;
            if (m_cur_vol)
            {
                for (size_t j=0; j<ruler->GetNumPoint(); ++j)
                {
                    p2 = *(ruler->GetPoint(j));
                    if (m_cur_vol->InsideClippingPlanes(p2))
                    {
                        skip = false;
                        break;
                    }
                }
            }
            else
                skip = false;
            
            if (skip)
                continue;
            
			if (ruler->GetUseColor())
				color = ruler->GetColor();
			else
				color = text_color;
			for (size_t j=0; j<ruler->GetNumPoint(); ++j)
			{
				p2 = *(ruler->GetPoint(j));
				p2 = mv.transform(p2);
				p2 = p.transform(p2);
				if (p2.z() <= 0.0 || p2.z() >= 1.0)
					continue;
				px = (p2.x()+1.0)*nx/2.0;
				py = (p2.y()+1.0)*ny/2.0;
				verts.push_back(Vulkan2dRender::Vertex{ {px - w, py - w, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
				verts.push_back(Vulkan2dRender::Vertex{ {px + w, py - w, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
				verts.push_back(Vulkan2dRender::Vertex{ {px + w, py + w, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
				verts.push_back(Vulkan2dRender::Vertex{ {px - w, py + w, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
				uint32_t tmp[] = { vnum+0, vnum+1, vnum+1, vnum+2, vnum+2, vnum+3, vnum+3, vnum+0 };
				index.insert(index.end(), tmp, tmp + 8);
				vnum += 4;

				if (j+1 == ruler->GetNumPoint())
				{
					p2x = p2.x()*nx/2.0;
					p2y = p2.y()*ny/2.0;
					m_text_renderer->RenderText(
						m_vulkan->frameBuffers[m_vulkan->currentBuffer],
						ruler->GetNameDisp().ToStdWstring(),
						color,
						(p2x+w)*sx, (p2y-w)*sy);
				}
				if (j > 0)
				{
					p1 = *(ruler->GetPoint(j-1));
					p1 = mv.transform(p1);
					p1 = p.transform(p1);
					if (p1.z() <= 0.0 || p1.z() >= 1.0)
						continue;
					verts.push_back(Vulkan2dRender::Vertex{ {px, py, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
					p2x = (p1.x()+1.0)*nx/2.0;
					p2y = (p1.y()+1.0)*ny/2.0;
					verts.push_back(Vulkan2dRender::Vertex{ {p2x, p2y, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
					index.push_back(vnum); index.push_back(vnum+1);
                    int vid = vnum;
					vnum += 2;
                    
                    float vx = p2x - px;
                    float vy = p2y - py;
                    p2x = 0.8660254f * vx - 0.5f * vy;
                    p2y = 0.5f * vx + 0.8660254f * vy;
                    float d = 15.0f / sqrt(p2x*p2x + p2y*p2y);
                    verts.push_back(Vulkan2dRender::Vertex{ {p2x*d+px, p2y*d+py, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
                    index.push_back(vid); index.push_back(vnum);
                    vnum += 1;
                    
                    p2x = 0.8660254f * vx + 0.5f * vy;
                    p2y = -0.5f * vx + 0.8660254f * vy;
                    verts.push_back(Vulkan2dRender::Vertex{ {p2x*d+px, p2y*d+py, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
                    index.push_back(vid); index.push_back(vnum);
                    vnum += 1;
				}

				if(ruler->GetBalloonVisibility(j))
				{
					wxArrayString ann = ruler->GetAnnotations(j,  m_cur_vol ? m_cur_vol->GetAnnotation() : vector<AnnotationDB>(), spcx, spcy, spcz);

					int lnum = ann.size();

					if(lnum > 0)
					{
						p2x = p2.x()*nx/2.0;
						p2y = p2.y()*ny/2.0;

						double asp = (double)nx / (double)ny;;
						double margin_x = 5+w;
						double margin_top = w;
						double margin_bottom = w;
						double line_spc = 2;
						double maxlw, lw, lh;

						lh = m_text_renderer->GetSize();
						margin_bottom += lh/2;
						maxlw = 0.0;

						for(int i = 0; i < lnum; i++)
						{
							wstring wstr_temp = ann.Item(i).ToStdWstring();
							m_text_renderer->RenderText(m_vulkan->frameBuffers[m_vulkan->currentBuffer],
								wstr_temp, color,
								p2x*sx+margin_x*sx, p2y*sy+(lh*(i+1)+line_spc*i+margin_top)*sy);
							lw = m_text_renderer->RenderTextDims(wstr_temp).width;
							if (lw > maxlw) maxlw = lw;
						}

						float bw = (margin_x*2 + maxlw);
						float bh = (margin_top + margin_bottom + lh*lnum + line_spc*(lnum-1));

						px = (p2.x()+1.0)*nx/2.0;
						py = (p2.y()+1.0)*ny/2.0;
						verts.push_back(Vulkan2dRender::Vertex{ {px,      py,      0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
						verts.push_back(Vulkan2dRender::Vertex{ {px + bw, py,      0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
						verts.push_back(Vulkan2dRender::Vertex{ {px + bw, py + bh, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
						verts.push_back(Vulkan2dRender::Vertex{ {px,      py + bh, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
						uint32_t tmp[] = { vnum+0, vnum+1, vnum+1, vnum+2, vnum+2, vnum+3, vnum+3, vnum+0 };
						index.insert(index.end(), tmp, tmp + 8);
						vnum += 4;
					}
				}
			}
			if (ruler->GetRulerType() == 4 &&
				ruler->GetNumPoint() >= 3)
			{
				Point center = *(ruler->GetPoint(1));
				Vector v1 = *(ruler->GetPoint(0)) - center;
				Vector v2 = *(ruler->GetPoint(2)) - center;
				double len = Min(v1.length(), v2.length());
				if (len > w)
				{
					v1.normalize();
					v2.normalize();
					p1 = center + v1*w;
					p1 = mv.transform(p1);
					p1 = p.transform(p1);
					px = (p1.x()+1.0)*nx/2.0;
					py = (p1.y()+1.0)*ny/2.0;
					verts.push_back(Vulkan2dRender::Vertex{ {px, py, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
					p1 = center + v2*w;
					p1 = mv.transform(p1);
					p1 = p.transform(p1);
					px = (p1.x()+1.0)*nx/2.0;
					py = (p1.y()+1.0)*ny/2.0;
					verts.push_back(Vulkan2dRender::Vertex{ {px, py, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
					index.push_back(vnum); index.push_back(vnum + 1);
					vnum += 2;
				}
			}
		}
	}

	//draw landmarks
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	vector<VolumeData *> displist;
	for (int i=(int)m_layer_list.size()-1; i>=0; i--)
	{
		if (!m_layer_list[i])
			continue;
		switch (m_layer_list[i]->IsA())
		{
		case 2://volume data (this won't happen now)
			{
				VolumeData* vd = (VolumeData*)m_layer_list[i];
				if (vd && vd->GetDisp())
				{
					displist.push_back(vd);
				}
			}
			break;
		case 5://group
			{
				DataGroup* group = (DataGroup*)m_layer_list[i];
				if (!group->GetDisp())
					continue;
				for (int j=group->GetVolumeNum()-1; j>=0; j--)
				{
					VolumeData* vd = group->GetVolumeData(j);
					if (vd && vd->GetDisp())
					{
						displist.push_back(vd);
					}
				}
			}
			break;
		}
	}
	std::sort(displist.begin(), displist.end());
	std::sort(m_lm_vdlist.begin(), m_lm_vdlist.end());

	if (m_lm_vdlist != displist)
	{
		for(int i = 0; i < m_landmarks.size(); i++)
		{
			delete m_landmarks[i];
			m_landmarks[i] = NULL;
		}
		vector<Ruler *>().swap(m_landmarks);

		m_lm_vdlist = displist;

		if(m_lm_vdlist.size() > 0)
		{
			int id1;
			wstring lmid;
			for(id1 = 1; id1 < m_lm_vdlist.size(); id1++)
			{
				if (m_lm_vdlist[id1]->GetLandmarkNum() > 0)
				{
					m_lm_vdlist[id1]->GetMetadataID(lmid);
					break;
				}
			}

			bool multi = false;
			for(int i = id1; i < m_lm_vdlist.size(); i++)
			{
				wstring lmid2;
				m_lm_vdlist[i]->GetMetadataID(lmid2);
				if (lmid != lmid2 && m_lm_vdlist[i]->GetLandmarkNum() > 0)
				{
					multi = true;
					break;
				}
			}

			map<wstring, VD_Landmark> lmmap;
			vector<pair<wstring, wstring>> redundant_first;
			for(int i = 0; i < m_lm_vdlist.size(); i++)
			{
				int lmnum = m_lm_vdlist[i]->GetLandmarkNum();
				for(int j = 0; j < lmnum; j++)
				{
					VD_Landmark lm;
					m_lm_vdlist[i]->GetLandmark(j, lm);
					if (multi) 
					{
						wstring name = lm.name;
						m_lm_vdlist[i]->GetMetadataID(lmid);
						if(!lmid.empty())
						{
							name += L" (";
							name += lmid;
							name += L")";
							lm.name = name;
						}
					}

					if (lmmap.find(lm.name) == lmmap.end())
					{
						lmmap[lm.name] = lm;
						Ruler* ruler = new Ruler();
						ruler->SetNameDisp(lm.name);
						ruler->SetRulerType(2);
						ruler->AddPoint(Point(lm.x, lm.y, lm.z));
						ruler->SetTimeDep(false);
						m_landmarks.push_back(ruler);
					}
				}
			}
		}
	}

	if(m_draw_landmarks)
	{
		for (int i=0; i<(int)m_landmarks.size(); i++)
		{
			Ruler* ruler = m_landmarks[i];
			if (!ruler) continue;
			if (!ruler->GetTimeDep() ||
				(ruler->GetTimeDep() &&
				ruler->GetTime() == m_tseq_cur_num))
			{
				if (ruler->GetUseColor())
					color = ruler->GetColor();
				else
					color = text_color;
				for (size_t j=0; j<ruler->GetNumPoint(); ++j)
				{
					p2 = *(ruler->GetPoint(j));
					p2 = mv.transform(p2);
					p2 = p.transform(p2);
					if (p2.z() <= 0.0 || p2.z() >= 1.0)
						continue;
					px = (p2.x()+1.0)*nx/2.0;
					py = (p2.y()+1.0)*ny/2.0;
					verts.push_back(Vulkan2dRender::Vertex{ {px - w, py - w, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
					verts.push_back(Vulkan2dRender::Vertex{ {px + w, py - w, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
					verts.push_back(Vulkan2dRender::Vertex{ {px + w, py + w, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
					verts.push_back(Vulkan2dRender::Vertex{ {px - w, py + w, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
					uint32_t tmp[] = { vnum+0, vnum+1, vnum+1, vnum+2, vnum+2, vnum+3, vnum+3, vnum+0 };
					index.insert(index.end(), tmp, tmp + 8);
					vnum += 4;

					if (j+1 == ruler->GetNumPoint())
					{
						p2x = p2.x()*nx/2.0;
						p2y = p2.y()*ny/2.0;
						m_text_renderer->RenderText(
							m_vulkan->frameBuffers[m_vulkan->currentBuffer],
							ruler->GetNameDisp().ToStdWstring(),
							color,
							(p2x+w)*sx, (p2y-w)*sy);
					}
					if (j > 0)
					{
						p1 = *(ruler->GetPoint(j-1));
						p1 = mv.transform(p1);
						p1 = p.transform(p1);
						if (p1.z() <= 0.0 || p1.z() >= 1.0)
							continue;
						verts.push_back(Vulkan2dRender::Vertex{ {px, py, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
						px = (p1.x() + 1.0) * nx / 2.0;
						py = (p1.y() + 1.0) * ny / 2.0;
						verts.push_back(Vulkan2dRender::Vertex{ {px, py, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
						index.push_back(vnum); index.push_back(vnum + 1);
						vnum += 2;
					}

					if(ruler->GetBalloonVisibility(j))
					{
						wxArrayString ann = ruler->GetAnnotations(j,  m_cur_vol ? m_cur_vol->GetAnnotation() : vector<AnnotationDB>(), spcx, spcy, spcz);

						int lnum = ann.size();

						if(lnum > 0)
						{
							p2x = p2.x()*nx/2.0;
							p2y = p2.y()*ny/2.0;

							double asp = (double)nx / (double)ny;;
							double margin_x = 5+w;
							double margin_top = w;
							double margin_bottom = w;
							double line_spc = 2;
							double maxlw, lw, lh;

							lh = m_text_renderer->GetSize();
							margin_bottom += lh/2;
							maxlw = 0.0;

							for(int i = 0; i < lnum; i++)
							{
								wstring wstr_temp = ann.Item(i).ToStdWstring();
								m_text_renderer->RenderText(m_vulkan->frameBuffers[m_vulkan->currentBuffer],
									wstr_temp, color,
									p2x*sx+margin_x*sx, p2y*sy+(lh*(i+1)+line_spc*i+margin_top)*sy);
								lw = m_text_renderer->RenderTextDims(wstr_temp).width;
								if (lw > maxlw) maxlw = lw;
							}

							float bw = (margin_x*2 + maxlw);
							float bh = (margin_top + margin_bottom + lh*lnum + line_spc*(lnum-1));

							px = (p2.x() + 1.0) * nx / 2.0;
							py = (p2.y() + 1.0) * ny / 2.0;
							verts.push_back(Vulkan2dRender::Vertex{ {px,      py,      0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
							verts.push_back(Vulkan2dRender::Vertex{ {px + bw, py,      0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
							verts.push_back(Vulkan2dRender::Vertex{ {px + bw, py + bh, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
							verts.push_back(Vulkan2dRender::Vertex{ {px,      py + bh, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
							uint32_t tmp[] = { vnum + 0, vnum + 1, vnum + 1, vnum + 2, vnum + 2, vnum + 3, vnum + 3, vnum + 0 };
							index.insert(index.end(), tmp, tmp + 8);
							vnum += 4;
						}
					}
				}
				if (ruler->GetRulerType() == 4 &&
					ruler->GetNumPoint() >= 3)
				{
					Point center = *(ruler->GetPoint(1));
					Vector v1 = *(ruler->GetPoint(0)) - center;
					Vector v2 = *(ruler->GetPoint(2)) - center;
					double len = Min(v1.length(), v2.length());
					if (len > w)
					{
						v1.normalize();
						v2.normalize();
						p1 = center + v1*w;
						p1 = mv.transform(p1);
						p1 = p.transform(p1);
						px = (p1.x()+1.0)*nx/2.0;
						py = (p1.y()+1.0)*ny/2.0;
						verts.push_back(Vulkan2dRender::Vertex{ {px, py, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
						p1 = center + v2*w;
						p1 = mv.transform(p1);
						p1 = p.transform(p1);
						px = (p1.x()+1.0)*nx/2.0;
						py = (p1.y()+1.0)*ny/2.0;
						verts.push_back(Vulkan2dRender::Vertex{ {px, py, 0.0f}, {(float)color.r(), (float)color.g(), (float)color.b()} });
						index.push_back(vnum); index.push_back(vnum + 1);
						vnum += 2;
					}
				}
			}
		}
	}
	if (!verts.empty() && !index.empty())
	{
		if (m_ruler_vobj.vertCount != verts.size())
		{
			if (m_ruler_vobj.vertBuf.buffer != VK_NULL_HANDLE)
				m_ruler_vobj.vertBuf.destroy();
			VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				&m_ruler_vobj.vertBuf,
				verts.size() * sizeof(Vulkan2dRender::Vertex),
				verts.data()));
			m_ruler_vobj.vertCount = verts.size();
			m_ruler_vobj.vertOffset = 0;
		}
		if (m_ruler_vobj.idxCount != index.size())
		{
			if (m_ruler_vobj.idxBuf.buffer != VK_NULL_HANDLE)
				m_ruler_vobj.idxBuf.destroy();
			VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				&m_ruler_vobj.idxBuf,
				index.size() * sizeof(uint32_t),
				index.data()));

			m_ruler_vobj.idxCount = index.size();
			m_ruler_vobj.idxOffset = 0;
		}
		m_ruler_vobj.vertBuf.map();
		m_ruler_vobj.idxBuf.map();
		m_ruler_vobj.vertBuf.copyTo(verts.data(), verts.size() * sizeof(Vulkan2dRender::Vertex));
		m_ruler_vobj.idxBuf.copyTo(index.data(), index.size() * sizeof(uint32_t));
		m_ruler_vobj.vertBuf.unmap();
		m_ruler_vobj.idxBuf.unmap();

		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHDR_DRAW_GEOMETRY_COLOR3,
				V2DRENDER_BLEND_OVER_UI,
				current_fbo->attachments[0]->format,
				current_fbo->attachments.size(),
				0,
				current_fbo->attachments[0]->is_swapchain_images,
				VK_PRIMITIVE_TOPOLOGY_LINE_LIST
			);
		params.matrix[0] = glm::ortho(float(0), float(nx), float(0), float(ny));
		params.clear = m_frame_clear;
		params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b() };
		m_frame_clear = false;

		params.obj = &m_ruler_vobj;

		if (!current_fbo->renderPass)
			current_fbo->replaceRenderPass(params.pipeline.pass);

		m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
	}
}

vector<Ruler*>* VRenderVulkanView::GetRulerList()
{
	return &m_ruler_list;
}

//traces
TraceGroup* VRenderVulkanView::GetTraceGroup()
{
	return m_trace_group;
}

void VRenderVulkanView::CreateTraceGroup()
{
	if (m_trace_group)
		delete m_trace_group;

	m_trace_group = new TraceGroup;
}

int VRenderVulkanView::LoadTraceGroup(wxString filename)
{
	if (m_trace_group)
		delete m_trace_group;

	m_trace_group = new TraceGroup;
	return m_trace_group->Load(filename);
}

int VRenderVulkanView::SaveTraceGroup(wxString filename)
{
	if (m_trace_group)
		return m_trace_group->Save(filename);
	else
		return 0;
}

void VRenderVulkanView::ExportTrace(wxString filename, unsigned int id)
{
	if (!m_trace_group)
		return;
}

void VRenderVulkanView::DrawTraces()
{
	/*if (m_cur_vol && m_trace_group && m_text_renderer)
	{
		vector<float> verts;
		unsigned int num = m_trace_group->Draw(verts);

		if (!verts.empty())
		{
			double width = m_text_renderer->GetSize()/3.0;
			glEnable(GL_LINE_SMOOTH);
			glLineWidth(float(width));
			glEnable(GL_DEPTH_TEST);
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

			double spcx, spcy, spcz;
			m_cur_vol->GetSpacings(spcx, spcy, spcz);
			glm::mat4 matrix = glm::scale(m_mv_mat,
				glm::vec3(float(spcx), float(spcy), float(spcz)));
			matrix = m_proj_mat*matrix;

			ShaderProgram* shader =
				m_img_shader_factory.shader(IMG_SHDR_DRAW_GEOMETRY_COLOR3);
			if (shader)
			{
				if (!shader->valid())
					shader->create();
				shader->bind();
			}
			shader->setLocalParamMatrix(0, glm::value_ptr(matrix));

			glBindBuffer(GL_ARRAY_BUFFER, m_misc_vbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(float)*verts.size(), &verts[0], GL_DYNAMIC_DRAW);
			glBindVertexArray(m_misc_vao);
			glBindBuffer(GL_ARRAY_BUFFER, m_misc_vbo);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (const GLvoid*)0);
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (const GLvoid*)12);

			glDrawArrays(GL_LINES, 0, num);

			glDisableVertexAttribArray(0);
			glDisableVertexAttribArray(1);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindVertexArray(0);

			if (shader && shader->valid())
				shader->release();

			glDisable(GL_LINE_SMOOTH);
			glLineWidth(1.0);
		}
	}*/
}

void VRenderVulkanView::GetTraces()
{
	if (!m_trace_group)
		return;

	int ii, jj, kk;
	int nx, ny, nz;
	//return current mask (into system memory)
	if (!m_cur_vol) return;
	m_cur_vol->GetVR()->return_mask();
	m_cur_vol->GetResolution(nx, ny, nz);
	//find labels in the old that are selected by the current mask
	auto mask_nrrd = m_cur_vol->GetMask(true);
	if (!mask_nrrd || !mask_nrrd->getNrrd()) return;
	auto label_nrrd = m_cur_vol->GetLabel(false);
	if(!label_nrrd || !label_nrrd->getNrrd()) return;
	unsigned char* mask_data = (unsigned char*)(mask_nrrd->getNrrd()->data);
	if (!mask_data) return;
	unsigned int* label_data = (unsigned int*)(label_nrrd->getNrrd()->data);
	if (!label_data) return;
	FL::CellList sel_labels;
	FL::CellListIter label_iter;
	for (ii=0; ii<nx; ii++)
		for (jj=0; jj<ny; jj++)
			for (kk=0; kk<nz; kk++)
			{
				int index = nx*ny*kk + nx*jj + ii;
				unsigned int label_value = label_data[index];
				if (mask_data[index] && label_value)
				{
					label_iter = sel_labels.find(label_value);
					if (label_iter == sel_labels.end())
					{
						FL::pCell cell(new FL::Cell(label_value));
						cell->Inc(ii, jj, kk, 1.0f);
						sel_labels.insert(pair<unsigned int, FL::pCell>
							(label_value, cell));
					}
					else
					{
						label_iter->second->Inc(ii, jj, kk, 1.0f);
					}
				}
			}

			//create id list
			m_trace_group->SetCurTime(m_tseq_cur_num);
			m_trace_group->SetPrvTime(m_tseq_cur_num);
			m_trace_group->UpdateCellList(sel_labels);

			//add traces to trace dialog
			VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
			if (m_vrv && vr_frame && vr_frame->GetTraceDlg())
				vr_frame->GetTraceDlg()->GetSettings(m_vrv);
}

/*WXLRESULT VRenderVulkanView::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam)
{
PACKET pkt;

if (message == WT_PACKET)
{
if (gpWTPacket((HCTX)lParam, wParam, &pkt))
{
unsigned int prsNew = pkt.pkNormalPressure;
wxSize size = GetClientSize();
long pkx = (size.GetX() * pkt.pkX) / m_lc.lcOutExtX;
long pky = size.GetY() - (size.GetY() * pkt.pkY) / m_lc.lcOutExtY;

m_paint_enable = false;

if (HIWORD(pkt.pkButtons)==TBN_DOWN)
{
if (m_int_mode == 2)
{
old_mouse_X = pkx;
old_mouse_Y = pky;
prv_mouse_X = old_mouse_X;
prv_mouse_Y = old_mouse_Y;
m_paint_enable = true;
m_clear_paint = true;
m_on_press = true;
RefreshGL();
}
}

if (HIWORD(pkt.pkButtons)==TBN_UP)
{
if (m_int_mode == 2)
{
m_paint_enable = true;
Segment();
m_int_mode = 1;
m_on_press = false;
RefreshGL();
}
}

//update mouse position
if (old_mouse_X >= 0 && old_mouse_Y >=0)
{
prv_mouse_X = old_mouse_X;
prv_mouse_Y = old_mouse_Y;
old_mouse_X = pkx;
old_mouse_Y = pky;
}
else
{
old_mouse_X = pkx;
old_mouse_Y = pky;
prv_mouse_X = old_mouse_X;
prv_mouse_Y = old_mouse_Y;
}

if (m_int_mode == 2 && m_on_press)
{
m_paint_enable = true;
RefreshGL();
}

if (m_on_press)
m_pressure = double(prsNew)/512.0;
else
m_pressure = 1.0;

if (m_draw_brush)
RefreshGL();
}
}

return wxWindow::MSWWindowProc(message, wParam, lParam);
}*/

void VRenderVulkanView::OnMouse(wxMouseEvent& event)
{
#ifdef _DEBUG
	cout << "OnMouse" << endl;
#endif

	wxWindow *window = wxWindow::FindFocus();
	//	if (window &&
	//		window->GetClassInfo()->IsKindOf(CLASSINFO(wxTextCtrl)))
	//		SetFocus();
	//mouse interactive flag
	//m_interactive = false; //deleted by takashi
	
	wxPoint mouse_pos = wxGetMousePosition();
	wxRect view_reg = GetScreenRect();
	if (view_reg.Contains(mouse_pos))
		UpdateBrushState();

	m_paint_enable = false;
	m_drawing_coord = false;

	extern CURLM * _g_curlm;
	int handle_count;
	curl_multi_perform(_g_curlm, &handle_count);

	if(event.LeftDClick())
	{
		Point *p = GetEditingRulerPoint(event.GetX(), event.GetY());
		if(p)
		{
			double cx, cy, cz;
			GetObjCenters(cx, cy, cz);
			Point trans = Point(cx-p->x(), -(cy-p->y()), -(cz-p->z()));
			StartManipulation(NULL, NULL, NULL, &trans, NULL);
		}
		else
		{
			bool na_mode = false;
            vector<VolumeData*> ref;
			for (int i = 0; i < m_vd_pop_list.size(); i++)
			{
				if (m_vd_pop_list[i]->GetDisp() && m_vd_pop_list[i]->GetNAMode())
				{
					na_mode = true;
                    ref.push_back(m_vd_pop_list[i]);
				}
			}
			if (na_mode)
				SelLabelSegVolumeMax(0, ref);
			else
				SelSegVolume();
		}

		RefreshGL();
		return;
	}

	//mouse button down operations
	if (event.LeftDown())
	{
		//m_searcher->KillFocus();
		SetKeyLock(false);
		m_dummy->SetFocus();

		if (m_int_mode == 6)
			m_editing_ruler_point = GetEditingRulerPoint(event.GetX(), event.GetY());

		old_mouse_X = event.GetX();
		old_mouse_Y = event.GetY();
		prv_mouse_X = old_mouse_X;
		prv_mouse_Y = old_mouse_Y;

		if (m_int_mode == 1 ||
			(m_int_mode==5 &&
			event.AltDown()) ||
			(m_int_mode==6 &&
			m_editing_ruler_point==0))
		{
			m_pick = true;
		}
		else if (m_int_mode == 2 || m_int_mode == 7)
		{
            if (m_int_mode == 7)
            {
                int int_mode = GetIntMode();
                int paint_mode = GetPaintMode();
                SetPaintMode(6);
                Segment();
                SetPaintMode(paint_mode);
                SetIntMode(int_mode);
            }
			m_paint_enable = true;
			m_clear_paint = true;
			RefreshGLOverlays();
		}
		return;
	}
	if (event.RightDown())
	{
		//m_searcher->KillFocus();
		SetKeyLock(false);
		m_dummy->SetFocus();

		old_mouse_X = event.GetX();
		old_mouse_Y = event.GetY();
		return;
	}
	if (event.MiddleDown())
	{
		//m_searcher->KillFocus();
		SetKeyLock(false);
		m_dummy->SetFocus();

		old_mouse_X = event.GetX();
		old_mouse_Y = event.GetY();
		return;
	}

	//mouse button up operations
	if (event.LeftUp())
	{
		if (m_int_mode == 1)
		{
			bool na_mode = false;
			for (int i = 0; i < m_vd_pop_list.size(); i++)
			{
				if (m_vd_pop_list[i]->GetDisp() && m_vd_pop_list[i]->GetNAMode())
				{
					na_mode = true;
					break;
				}
			}
			//pick polygon models
			if (m_pick && !na_mode)
				Pick();
		}
		else if (m_int_mode == 2)
		{
			//segment volumes
            SetForceHideMask(false);
			Segment();
			m_int_mode = 4;
			m_force_clear = true;
			m_clear_paint = true;
		}
		else if (m_int_mode == 5 &&
			!event.AltDown())
		{
			//add one point to a ruler
			AddRulerPoint(event.GetX(), event.GetY());
			RefreshGLOverlays();
			return;
		}
		else if (m_int_mode == 6)
		{
			m_editing_ruler_point = 0;
		}
		else if (m_int_mode == 7)
		{
			//segment volume, calculate center, add ruler point
            SetForceHideMask(false);
			Segment();
			if (m_ruler_type == 3)
				AddRulerPoint(event.GetX(), event.GetY());
			else
				AddPaintRulerPoint();
			m_int_mode = 8;
			m_force_clear = true;
			m_clear_paint = true;
		}

		RefreshGL();
		return;
	}
	if (event.MiddleUp())
	{
		//SetSortBricks();
		RefreshGL();
		return;
	}
	if (event.RightUp())
	{
		if (m_int_mode == 5 &&
			!event.AltDown())
		{
			if (GetRulerFinished())
			{
				SetIntMode(1);
				VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
				if (m_vrv && vr_frame && vr_frame->GetMeasureDlg())
					vr_frame->GetMeasureDlg()->GetSettings(m_vrv);
			}
			else
			{
				AddRulerPoint(event.GetX(), event.GetY());
				FinishRuler();
			}

			RefreshGL();
		}
		else
		{
			wxContextMenuEvent e;
			e.SetPosition(mouse_pos);
			OnContextMenu(e);
		}

		//added by takashi
		//SwitchRulerBalloonVisibility_Point(event.GetX(), event.GetY());
		
		return;
	}

	//mouse dragging
	if (event.Dragging())
	{
		TextureRenderer::set_cor_up_time(
			int(sqrt(double(old_mouse_X-event.GetX())*
			double(old_mouse_X-event.GetX())+
			double(old_mouse_Y-event.GetY())*
			double(old_mouse_Y-event.GetY()))));

		bool hold_old = false;
		if (m_int_mode == 1 ||
			(m_int_mode==5 &&
			event.AltDown()) ||
			(m_int_mode==6 &&
			m_editing_ruler_point==0))
		{
			//disable picking
			m_pick = false;

			if (old_mouse_X!=-1&&
				old_mouse_Y!=-1&&
				abs(old_mouse_X-event.GetX())+
				abs(old_mouse_Y-event.GetY())<200)
			{
				if (event.LeftIsDown() && !event.ControlDown())
				{
					Quaternion q_delta = Trackball(old_mouse_X, event.GetY(), event.GetX(), old_mouse_Y);
					if (m_rot_lock && q_delta.IsIdentity())
						hold_old = true;
					m_q *= q_delta;
					m_q.Normalize();
					Quaternion cam_pos(0.0, 0.0, m_distance, 0.0);
					Quaternion cam_pos2 = (-m_q) * cam_pos * m_q;
					m_transx = cam_pos2.x;
					m_transy = cam_pos2.y;
					m_transz = cam_pos2.z;

					Quaternion up(0.0, 1.0, 0.0, 0.0);
					Quaternion up2 = (-m_q) * up * m_q;
					m_up = Vector(up2.x, up2.y, up2.z);

					Q2A();

					wxString str = wxString::Format("%.1f", 360.0-m_rotx);
					m_vrv->m_x_rot_text->ChangeValue(str);
					if (m_rot_lock)
						m_vrv->m_x_rot_sldr->SetValue(int((360.0-m_rotx)/45.0));
					else
						m_vrv->m_x_rot_sldr->SetValue(int(360.0-m_rotx));
					str = wxString::Format("%.1f", m_roty);
					m_vrv->m_y_rot_text->ChangeValue(str);
					if (m_rot_lock)
						m_vrv->m_y_rot_sldr->SetValue(int(m_roty/45.0));
					else
						m_vrv->m_y_rot_sldr->SetValue(int(m_roty));
					str = wxString::Format("%.1f", m_rotz);
					m_vrv->m_z_rot_text->ChangeValue(str);
					if (m_rot_lock)
						m_vrv->m_z_rot_sldr->SetValue(int(m_rotz/45.0));
					else
						m_vrv->m_z_rot_sldr->SetValue(int(m_rotz));

					m_interactive = m_adaptive;
					m_int_res = m_adaptive_res;

					if (m_linked_rot)
					{
						VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
						if (vr_frame)
						{
							for (int i=0; i<vr_frame->GetViewNum(); i++)
							{
								VRenderView* view = vr_frame->GetView(i);
								if (view && view->m_glview)
								{
									view->m_glview->SetRotations(
										m_rotx, m_roty, m_rotz, true, false);
									view->m_glview->UpdateScreen();
								}
							}
						}
					}
					if (!hold_old)
						RefreshGL();
				}
				if (event.MiddleIsDown() || (event.ControlDown() && event.LeftIsDown()))
				{
					long dx = event.GetX() - old_mouse_X;
					long dy = event.GetY() - old_mouse_Y;

					m_head = Vector(-m_transx, -m_transy, -m_transz);
					m_head.normalize();
					Vector side = Cross(m_up, m_head);
					Vector trans = -(
						side*(double(dx)*(m_ortho_right-m_ortho_left)/double(GetSize().x))+
						m_up*(double(-dy)*(m_ortho_top-m_ortho_bottom)/double(GetSize().y)));
					m_obj_transx += trans.x();
					m_obj_transy += trans.y();
					m_obj_transz += trans.z();

					m_interactive = m_adaptive;
					m_int_res = m_adaptive_res;
                    
                    Q2A();

					//SetSortBricks();
					RefreshGL();
				}
				if (event.RightIsDown())
				{
					long dx = event.GetX() - old_mouse_X;
					long dy = event.GetY() - old_mouse_Y;
					double delta = abs(dx) > abs(dy) ?
									(double)dx / (double)GetSize().x :
									(double)-dy / (double)GetSize().y;

					if (m_free)
					{
						Vector pos(m_transx, m_transy, m_transz);
						pos.normalize();
						Vector ctr(m_ctrx, m_ctry, m_ctrz);
						ctr -= delta * pos * m_radius * 10;//1000;
						m_ctrx = ctr.x();
						m_ctry = ctr.y();
						m_ctrz = ctr.z();
						m_scale_factor = m_radius/tan(d2r(m_aov/2.0)) / ( CalcCameraDistance() + (m_radius/tan(d2r(m_aov/2.0))/12.0) );
					}
					else
						m_scale_factor += m_scale_factor*delta;

					wxString str = wxString::Format("%.0f", m_scale_factor*100.0);
					m_vrv->m_scale_factor_sldr->SetValue(m_scale_factor*100);
					m_vrv->m_scale_factor_text->ChangeValue(str);

					m_interactive = m_adaptive;
					m_int_res = m_adaptive_res;

					//SetSortBricks();
					RefreshGL();
				}
			}
		}
		else if (m_int_mode == 2 || m_int_mode == 7)
		{
			m_paint_enable = true;
			m_pressure = 1.0;
			RefreshGLOverlays();
		}
		else if (m_int_mode ==3)
		{
			if (old_mouse_X!=-1&&
				old_mouse_Y!=-1&&
				abs(old_mouse_X-event.GetX())+
				abs(old_mouse_Y-event.GetY())<200)
			{
				if (event.LeftIsDown())
				{
					Quaternion q_delta = TrackballClip(old_mouse_X, event.GetY(), event.GetX(), old_mouse_Y);
					m_q_cl = q_delta * m_q_cl;
					m_q_cl.Normalize();
					SetRotations(m_rotx, m_roty, m_rotz);
					RefreshGL();
				}
			}
		}
		else if (m_int_mode == 6)
		{
			if (event.LeftIsDown() &&
				m_editing_ruler_point)
			{
				Point point;
				bool failed = false;
				if (m_point_volume_mode)
				{
					double t = GetPointVolume(point,
						event.GetX(), event.GetY(),
						m_cur_vol, m_point_volume_mode,
						m_ruler_use_transf);
					if (t <= 0.0)
						t = GetPointPlane(point,
						event.GetX(), event.GetY(),
						m_editing_ruler_point);
					if (t <= 0.0)
						failed = true;
				}
				else
				{
					double t = GetPointPlane(point,
						event.GetX(), event.GetY(),
						m_editing_ruler_point);
					if (t <= 0.0)
						failed = true;
				}
                
				if (!failed)
				{
					m_editing_ruler_point->x(point.x());
					m_editing_ruler_point->y(point.y());
					m_editing_ruler_point->z(point.z());
					RefreshGLOverlays();
					VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
					if (m_vrv && vr_frame && vr_frame->GetMeasureDlg())
						vr_frame->GetMeasureDlg()->GetSettings(m_vrv);
				}
			}
		}

		//update mouse position
		if (old_mouse_X >= 0 && old_mouse_Y >=0)
		{
			prv_mouse_X = old_mouse_X;
			prv_mouse_Y = old_mouse_Y;
			if (!hold_old)
			{
				old_mouse_X = event.GetX();
				old_mouse_Y = event.GetY();
			}
		}
		else
		{
			old_mouse_X = event.GetX();
			old_mouse_Y = event.GetY();
			prv_mouse_X = old_mouse_X;
			prv_mouse_Y = old_mouse_Y;
		}
		return;
	}

	//wheel operations
	int wheel = event.GetWheelRotation();
	if (wheel)  //if rotation
	{
		if (m_int_mode == 2 || m_int_mode == 7)
		{
			if (m_use_brush_radius2 && m_selector.GetMode()!=8)
			{
				double delta = wheel / 100.0;
				m_brush_radius2 += delta;
				m_brush_radius2 = max(1.0, m_brush_radius2);
				m_brush_radius1 = min(m_brush_radius2, m_brush_radius1);
			}
			else
			{
				m_brush_radius1 += wheel / 100.0;
				m_brush_radius1 = max(m_brush_radius1, 1.0);
				m_brush_radius2 = m_brush_radius1;
			}

			VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
			if (vr_frame && vr_frame->GetBrushToolDlg())
				vr_frame->GetBrushToolDlg()->GetSettings(m_vrv);
		}
		else
		{
            double delta = wheel/1000.0;
            if (m_free)
            {
                Vector pos(m_transx, m_transy, m_transz);
                pos.normalize();
                Vector ctr(m_ctrx, m_ctry, m_ctrz);
                ctr -= delta * pos * m_radius * 10;//1000;
                m_ctrx = ctr.x();
                m_ctry = ctr.y();
                m_ctrz = ctr.z();
                m_scale_factor = m_radius/tan(d2r(m_aov/2.0)) / ( CalcCameraDistance() + (m_radius/tan(d2r(m_aov/2.0))/12.0) );
            }
            else
                m_scale_factor += delta;

			//m_scale_factor += wheel/1000.0;
			if (m_scale_factor < 0.01)
				m_scale_factor = 0.01;
			wxString str = wxString::Format("%.0f", m_scale_factor*100.0);
			m_vrv->m_scale_factor_sldr->SetValue(m_scale_factor*100);
			m_vrv->m_scale_factor_text->ChangeValue(str);
		}

		RefreshGL();
		return;
	}

	// draw the strokes into a framebuffer texture
	//not actually for displaying it
	if (m_draw_brush)
	{
		old_mouse_X = event.GetX();
		old_mouse_Y = event.GetY();
		RefreshGLOverlays();
		return;
	}

	if (m_draw_coord)
	{
		m_drawing_coord = true;
		RefreshGL(false, false);
		return;
	}
}

void VRenderVulkanView::SetDraw(bool draw)
{
	m_draw_all = draw;
}

void VRenderVulkanView::ToggleDraw()
{
	m_draw_all = !m_draw_all;
}

bool VRenderVulkanView::GetDraw()
{
	return m_draw_all;
}

Color VRenderVulkanView::GetBackgroundColor()
{
	return m_bg_color;
}

Color VRenderVulkanView::GetTextColor()
{
	VRenderFrame* frame = (VRenderFrame*)m_frame;
	if (!frame || !frame->GetSettingDlg())
		return m_bg_color_inv;
	switch (frame->GetSettingDlg()->GetTextColor())
	{
	case 0://background inverted
		return m_bg_color_inv;
	case 1://background
		return m_bg_color;
	case 2://secondary color of current volume
		if (m_cur_vol)
			return m_cur_vol->GetMaskColor();
		else
			return m_bg_color_inv;
	}
	return m_bg_color_inv;
}

void VRenderVulkanView::SetBackgroundColor(Color &color)
{
	m_bg_color = color;
	HSVColor bg_color(m_bg_color);
	double hue, sat, val;
	if (bg_color.val()>0.7 && bg_color.sat()>0.7)
	{
		hue = bg_color.hue() + 180.0;
		sat = 1.0;
		val = 1.0;
		m_bg_color_inv = Color(HSVColor(hue, sat, val));
	}
	else if (bg_color.val()>0.7)
	{
		m_bg_color_inv = Color(0.0, 0.0, 0.0);
	}
	else
	{
		m_bg_color_inv = Color(1.0, 1.0, 1.0);
	}
}

void VRenderVulkanView::SetGradBg(bool val)
{
	m_grad_bg = val;
}

void VRenderVulkanView::SetRotations(double rotx, double roty, double rotz, bool ui_update, bool link_update)
{
	m_rotx = rotx;
	m_roty = roty;
	m_rotz = rotz;

	if (m_roty>360.0)
		m_roty -= 360.0;
	if (m_roty<0.0)
		m_roty += 360.0;
	if (m_rotx>360.0)
		m_rotx -= 360.0;
	if (m_rotx<0.0)
		m_rotx += 360.0;
	if (m_rotz>360.0)
		m_rotz -= 360.0;
	if (m_rotz<0.0)
		m_rotz += 360.0;

	A2Q();

	Quaternion cam_pos(0.0, 0.0, m_distance, 0.0);
	Quaternion cam_pos2 = (-m_q) * cam_pos * m_q;
	m_transx = cam_pos2.x;
	m_transy = cam_pos2.y;
	m_transz = cam_pos2.z;

	Quaternion up(0.0, 1.0, 0.0, 0.0);
	Quaternion up2 = (-m_q) * up * m_q;
	m_up = Vector(up2.x, up2.y, up2.z);

	if (m_rot_lock)
	{
		m_vrv->m_x_rot_sldr->SetValue(int((360.0-m_rotx)/45.0));
		m_vrv->m_y_rot_sldr->SetValue(int(m_roty/45.0));
		m_vrv->m_z_rot_sldr->SetValue(int(m_rotz/45.0));
	}
	else
	{
		m_vrv->m_x_rot_sldr->SetValue(int(360.0-m_rotx));
		m_vrv->m_y_rot_sldr->SetValue(int(m_roty));
		m_vrv->m_z_rot_sldr->SetValue(int(m_rotz));
	}

	if (ui_update)
	{
		wxString str = wxString::Format("%.1f", 360.0-m_rotx);
		m_vrv->m_x_rot_text->ChangeValue(str);
		str = wxString::Format("%.1f", m_roty);
		m_vrv->m_y_rot_text->ChangeValue(str);
		str = wxString::Format("%.1f", m_rotz);
		m_vrv->m_z_rot_text->ChangeValue(str);
	}

	if (m_linked_rot && link_update)
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			for (int i=0; i<vr_frame->GetViewNum(); i++)
			{
				VRenderView* view = vr_frame->GetView(i);
				if (view && view->m_glview &&
					view->m_glview!= this &&
					view->m_glview->m_linked_rot)
				{
					view->m_glview->SetRotations(m_rotx, m_roty, m_rotz, true, false);
					view->RefreshGL();
				}
			}
		}
	}
}

char* VRenderVulkanView::wxStringToChar(wxString input)
{
#if (wxUSE_UNICODE)
	size_t size = input.size() + 1;
	char *buffer = new char[size];//No need to multiply by 4, converting to 1 byte char only.
	memset(buffer, 0, size); //Good Practice, Can use buffer[0] = '&#65533;' also.
	wxEncodingConverter wxec;
	wxec.Init(wxFONTENCODING_ISO8859_1, wxFONTENCODING_ISO8859_1, wxCONVERT_SUBSTITUTE);
	wxec.Convert(input.mb_str(), buffer);
	return buffer; //To free this buffer memory is user responsibility.
#else
	return (char *)(input.c_str());
#endif
}

void VRenderVulkanView::GetFrame(int &x, int &y, int &w, int &h)
{
	x = m_frame_x;
	y = m_frame_y;
	w = m_frame_w;
	h = m_frame_h;
}

void VRenderVulkanView::CalcFrame()
{
	int w, h;
	w = GetSize().x;
	h = GetSize().y;
	if (m_tile_rendering) {
		w = m_capture_resx;
		h = m_capture_resy;
	}
	/*
	if (m_cur_vol)
	{
		//projection
		HandleProjection(w, h);
		//Transformation
		HandleCamera();

		glm::mat4 mv_temp;
		//translate object
		mv_temp = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
		//rotate object
		mv_temp = glm::rotate(mv_temp, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
		mv_temp = glm::rotate(mv_temp, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
		mv_temp = glm::rotate(mv_temp, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
		//center object
		mv_temp = glm::translate(mv_temp, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

		Transform mv;
		Transform pr;
		mv.set(glm::value_ptr(mv_temp));
		pr.set(glm::value_ptr(m_proj_mat));

		double minx, maxx, miny, maxy;
		minx = 1.0;
		maxx = -1.0;
		miny = 1.0;
		maxy = -1.0;
		vector<Point> points;
		BBox bbox = m_cur_vol->GetBounds();
		points.push_back(Point(bbox.min().x(), bbox.min().y(), bbox.min().z()));
		points.push_back(Point(bbox.min().x(), bbox.min().y(), bbox.max().z()));
		points.push_back(Point(bbox.min().x(), bbox.max().y(), bbox.min().z()));
		points.push_back(Point(bbox.min().x(), bbox.max().y(), bbox.max().z()));
		points.push_back(Point(bbox.max().x(), bbox.min().y(), bbox.min().z()));
		points.push_back(Point(bbox.max().x(), bbox.min().y(), bbox.max().z()));
		points.push_back(Point(bbox.max().x(), bbox.max().y(), bbox.min().z()));
		points.push_back(Point(bbox.max().x(), bbox.max().y(), bbox.max().z()));

		Point p;
		for (unsigned int i=0; i<points.size(); ++i)
		{
			p = mv.transform(points[i]);
			p = pr.transform(p);
			minx = p.x()<minx?p.x():minx;
			maxx = p.x()>maxx?p.x():maxx;
			miny = p.y()<miny?p.y():miny;
			maxy = p.y()>maxy?p.y():maxy;
		}

		minx = Clamp(minx, -1.0, 1.0);
		maxx = Clamp(maxx, -1.0, 1.0);
		miny = Clamp(miny, -1.0, 1.0);
		maxy = Clamp(maxy, -1.0, 1.0);

		m_frame_x = int((minx+1.0)*w/2.0+1.0);
		m_frame_y = int((miny+1.0)*h/2.0+1.0);
		m_frame_w = int((maxx-minx)*w/2.0-1.5);
		m_frame_h = int((maxy-miny)*h/2.0-1.5);

	}
	else*/
	{
		int size;
		if (w > h)
		{
			size = h;
			m_frame_x = int((w-h)/2.0);
			m_frame_y = 0;
		}
		else
		{
			size = w;
			m_frame_x = 0;
			m_frame_y = int((h-w)/2.0);
		}
		m_frame_w = m_frame_h = size;
	}
}

void VRenderVulkanView::StartManipulation(const Point *view_trans, const Point *view_center, const Point *view_rot, const Point *obj_trans, const double *scale)
{
	if(m_capture) return;

	if(!m_vrv)
		return;

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;

	DataManager* mgr = vr_frame->GetDataManager();
	if (!mgr)
		return;

	m_saved_uptime = TextureRenderer::get_up_time();
	TextureRenderer::set_up_time(m_manip_time);

	m_manip_interpolator.Clear();

	SetManipKey(0, 1, NULL, NULL, NULL, NULL, NULL); 

	double t = m_manip_time * m_manip_fps * 0.001;

	SetManipKey(t, 1, view_trans, view_center, view_rot, obj_trans, scale); 

	m_manip = true;
	m_manip_cur_num = 0;
	m_manip_end_frame = t;
}

void VRenderVulkanView::EndManipulation()
{
	TextureRenderer::set_up_time(m_saved_uptime);
	m_manip_interpolator.Clear();

	m_manip = false;
	m_manip_cur_num = 0;
	TextureRenderer::set_done_update_loop(true);
}

void VRenderVulkanView::SetManipKey(double t, int interpolation, const Point *view_trans, const Point *view_center, const Point *view_rot, const Point *obj_trans, const double *scale)
{
	if(!m_vrv)
		return;

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;

	DataManager* mgr = vr_frame->GetDataManager();
	if (!mgr)
		return;

	ClippingView* clip_view = vr_frame->GetClippingView();

	FLKeyCode keycode;
	FlKeyDouble* flkey = 0;
	FlKeyQuaternion* flkeyQ = 0;
	FlKeyBoolean* flkeyB = 0;

	m_manip_interpolator.Begin(t);

	//for all volumes
	for (int i=0; i<mgr->GetVolumeNum() ; i++)
	{
		VolumeData* vd = mgr->GetVolumeData(i);
		keycode.l0 = 1;
		keycode.l0_name = m_vrv->GetName();
		keycode.l1 = 2;
		keycode.l1_name = vd->GetName();
		//display
		keycode.l2 = 0;
		keycode.l2_name = "display";
		flkeyB = new FlKeyBoolean(keycode, vd->GetDisp());
		m_manip_interpolator.AddKey(flkeyB);
		//clipping planes
		vector<Plane*> * planes = vd->GetVR()->get_planes();
		if (!planes)
			continue;
		if (planes->size() != 6)
			continue;
		Plane* plane = 0;
		double param;
		//x1
		plane = (*planes)[0];
		param = plane->GetParam();
		keycode.l2 = 0;
		keycode.l2_name = "x1_val";
		flkey = new FlKeyDouble(keycode, param);
		m_manip_interpolator.AddKey(flkey);
		//x2
		plane = (*planes)[1];
        param = plane->GetParam();
		keycode.l2 = 0;
		keycode.l2_name = "x2_val";
		flkey = new FlKeyDouble(keycode, param);
		m_manip_interpolator.AddKey(flkey);
		//y1
		plane = (*planes)[2];
        param = plane->GetParam();
		keycode.l2 = 0;
		keycode.l2_name = "y1_val";
		flkey = new FlKeyDouble(keycode, param);
		m_manip_interpolator.AddKey(flkey);
		//y2
		plane = (*planes)[3];
        param = plane->GetParam();
		keycode.l2 = 0;
		keycode.l2_name = "y2_val";
		flkey = new FlKeyDouble(keycode, param);
		m_manip_interpolator.AddKey(flkey);
		//z1
		plane = (*planes)[4];
        param = plane->GetParam();
		keycode.l2 = 0;
		keycode.l2_name = "z1_val";
		flkey = new FlKeyDouble(keycode, param);
		m_manip_interpolator.AddKey(flkey);
		//z2
		plane = (*planes)[5];
        param = plane->GetParam();
		keycode.l2 = 0;
		keycode.l2_name = "z2_val";
		flkey = new FlKeyDouble(keycode, param);
		m_manip_interpolator.AddKey(flkey);
	}
	//for the view
	keycode.l0 = 1;
	keycode.l0_name = m_vrv->GetName();
	keycode.l1 = 1;
	keycode.l1_name = m_vrv->GetName();
    //link_planes
    if (clip_view) {
        keycode.l2 = 0;
        keycode.l2_name = "link_planes";
        flkeyB = new FlKeyBoolean(keycode, clip_view->GetChannLink());
        m_manip_interpolator.AddKey(flkeyB);
    }
	//rotation
	keycode.l2 = 0;
	keycode.l2_name = "rotation";
	double rotx, roty, rotz;
	if (!view_rot) GetRotations(rotx, roty, rotz);
	else { rotx = view_rot->x(); roty = view_rot->y(); rotz = view_rot->z(); }
	Quaternion q;
	q.FromEuler(rotx, roty, rotz);
	flkeyQ = new FlKeyQuaternion(keycode, q);
	m_manip_interpolator.AddKey(flkeyQ);
	//translation
	double tx, ty, tz;
	if (!view_trans) GetTranslations(tx, ty, tz);
	else { tx = view_trans->x(); ty = view_trans->y(); tz = view_trans->z(); }
	//x
	keycode.l2_name = "translation_x";
	flkey = new FlKeyDouble(keycode, tx);
	m_manip_interpolator.AddKey(flkey);
	//y
	keycode.l2_name = "translation_y";
	flkey = new FlKeyDouble(keycode, ty);
	m_manip_interpolator.AddKey(flkey);
	//z
	keycode.l2_name = "translation_z";
	flkey = new FlKeyDouble(keycode, tz);
	m_manip_interpolator.AddKey(flkey);
	//centers
	if (!view_center) GetCenters(tx, ty, tz);
	else { tx = view_center->x(); ty = view_center->y(); tz = view_center->z(); }
	//x
	keycode.l2_name = "center_x";
	flkey = new FlKeyDouble(keycode, tx);
	m_manip_interpolator.AddKey(flkey);
	//y
	keycode.l2_name = "center_y";
	flkey = new FlKeyDouble(keycode, ty);
	m_manip_interpolator.AddKey(flkey);
	//z
	keycode.l2_name = "center_z";
	flkey = new FlKeyDouble(keycode, tz);
	m_manip_interpolator.AddKey(flkey);
	//obj traslation
	if (!obj_trans) GetObjTrans(tx, ty, tz);
	else { tx = obj_trans->x(); ty = obj_trans->y(); tz = obj_trans->z(); }
	//x
	keycode.l2_name = "obj_trans_x";
	flkey = new FlKeyDouble(keycode, tx);
	m_manip_interpolator.AddKey(flkey);
	//y
	keycode.l2_name = "obj_trans_y";
	flkey = new FlKeyDouble(keycode, ty);
	m_manip_interpolator.AddKey(flkey);
	//z
	keycode.l2_name = "obj_trans_z";
	flkey = new FlKeyDouble(keycode, tz);
	m_manip_interpolator.AddKey(flkey);
	//scale
	double scale_factor = scale ? *scale : m_scale_factor;
	keycode.l2_name = "scale";
	flkey = new FlKeyDouble(keycode, scale_factor);
	m_manip_interpolator.AddKey(flkey);

	m_manip_interpolator.End();

	FlKeyGroup* group = m_manip_interpolator.GetKeyGroup(m_manip_interpolator.GetLastIndex());
	if (group)
		group->type = interpolation;

}

void VRenderVulkanView::SetManipParams(double t)
{
	if (!m_vrv)
		return;
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	ClippingView* clip_view = vr_frame->GetClippingView();
	FLKeyCode keycode;
	keycode.l0 = 1;
	keycode.l0_name = m_vrv->GetName();
    
    //for the view
    keycode.l1 = 1;
    keycode.l1_name = m_vrv->GetName();
    keycode.l2 = 0;
    keycode.l2_name = "link_planes";
    bool bval;
    if (m_manip_interpolator.GetBoolean(keycode, t, bval))
        clip_view->SetChannLink(bval);

	for (int i=0; i<GetAllVolumeNum(); i++)
	{
		VolumeData* vd = GetAllVolumeData(i);
		if (!vd) continue;

		keycode.l1 = 2;
		keycode.l1_name = vd->GetName();

		//display
		keycode.l2 = 0;
		keycode.l2_name = "display";
		bool bval;
		if (m_manip_interpolator.GetBoolean(keycode, t, bval))
			vd->SetDisp(bval);

		//clipping planes
		vector<Plane*> *planes = vd->GetVR()->get_planes();
		if (!planes) continue;
		if(planes->size() != 6) continue;
		Plane *plane = 0;
		double val = 0;
		//x1
		plane = (*planes)[0];
		keycode.l2 = 0;
		keycode.l2_name = "x1_val";
        if (m_manip_interpolator.GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedX1Param(val);
        }
		//x2
		plane = (*planes)[1];
		keycode.l2 = 0;
		keycode.l2_name = "x2_val";
        if (m_manip_interpolator.GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedX2Param(val);
        }
		//y1
		plane = (*planes)[2];
		keycode.l2 = 0;
		keycode.l2_name = "y1_val";
        if (m_manip_interpolator.GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedY1Param(val);
        }
		//y2
		plane = (*planes)[3];
		keycode.l2 = 0;
		keycode.l2_name = "y2_val";
        if (m_manip_interpolator.GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedY2Param(val);
        }
		//z1
		plane = (*planes)[4];
		keycode.l2 = 0;
		keycode.l2_name = "z1_val";
        if (m_manip_interpolator.GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedZ1Param(val);
        }
		//z2
		plane = (*planes)[5];
		keycode.l2 = 0;
		keycode.l2_name = "z2_val";
        if (m_manip_interpolator.GetDouble(keycode, t, val)) {
            plane->SetParam(val);
            if (clip_view && clip_view->GetChannLink())
                clip_view->SetLinkedZ2Param(val);
        }
	}

	bool bx, by, bz;
	//for the view
	keycode.l1 = 1;
	keycode.l1_name = m_vrv->GetName();
	//translation
	double tx, ty, tz;
	keycode.l2 = 0;
	keycode.l2_name = "translation_x";
	bx = m_manip_interpolator.GetDouble(keycode, t, tx);
	keycode.l2_name = "translation_y";
	by = m_manip_interpolator.GetDouble(keycode, t, ty);
	keycode.l2_name = "translation_z";
	bz = m_manip_interpolator.GetDouble(keycode, t, tz);
	if (bx && by && bz)
		SetTranslations(tx, ty, tz);
	//centers
	keycode.l2_name = "center_x";
	bx = m_manip_interpolator.GetDouble(keycode, t, tx);
	keycode.l2_name = "center_y";
	by = m_manip_interpolator.GetDouble(keycode, t, ty);
	keycode.l2_name = "center_z";
	bz = m_manip_interpolator.GetDouble(keycode, t, tz);
	if (bx && by && bz)
		SetCenters(tx, ty, tz);
	//obj translation
	keycode.l2_name = "obj_trans_x";
	bx = m_manip_interpolator.GetDouble(keycode, t, tx);
	keycode.l2_name = "obj_trans_y";
	by = m_manip_interpolator.GetDouble(keycode, t, ty);
	keycode.l2_name = "obj_trans_z";
	bz = m_manip_interpolator.GetDouble(keycode, t, tz);
	if (bx && by && bz)
		SetObjTrans(tx, ty, tz);
	//scale
	double scale;
	keycode.l2_name = "scale";
	if (m_manip_interpolator.GetDouble(keycode, t, scale))
		m_vrv->SetScaleFactor(scale);
	//rotation
	keycode.l2 = 0;
	keycode.l2_name = "rotation";
	Quaternion q;
	if (m_manip_interpolator.GetQuaternion(keycode, t, q))
	{
		double rotx, roty, rotz;
		q.ToEuler(rotx, roty, rotz);
		SetRotations(rotx, roty, rotz, true);
	}

	if (clip_view)
		clip_view->SetVolumeData(vr_frame->GetCurSelVol());
	if (vr_frame)
	{
		vr_frame->UpdateTree();
		int index = m_manip_interpolator.GetKeyIndexFromTime(t);
		vr_frame->GetRecorderDlg()->SetSelection(index);
	}
	SetVolPopDirty();
}

void VRenderVulkanView::DrawViewQuad()
{
	/*glEnable(GL_TEXTURE_2D);
	if (!glIsVertexArray(m_quad_vao))
	{
		float points[] = {
			-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
			1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
			-1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
			1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f};

		glBindBuffer(GL_ARRAY_BUFFER, m_quad_vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float)*24, points, GL_STATIC_DRAW);

		glGenVertexArrays(1, &m_quad_vao);
		glBindVertexArray(m_quad_vao);

		glBindBuffer(GL_ARRAY_BUFFER, m_quad_vbo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (const GLvoid*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (const GLvoid*)12);
	} else
		glBindBuffer(GL_ARRAY_BUFFER, m_quad_vbo);
	
	glBindVertexArray(m_quad_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);*/
}

void VRenderVulkanView::GetTiledViewQuadVerts(int tileid, vector<Vulkan2dRender::Vertex> &verts)
{
	if (!verts.empty())
		verts.clear();

	int w = GetSize().x;
	int h = GetSize().y;

	double win_aspect = (double)w/(double)h;
	double ren_aspect = (double)m_capture_resx/(double)m_capture_resy;
	double stpos_x = -1.0;
	double stpos_y = -1.0;
	double edpos_x = 1.0;
	double edpos_y = 1.0;
	double tilew = 1.0;
	double tileh = 1.0;

	double xbound = 1.0;
	double ybound = -1.0;
	
	if (win_aspect > ren_aspect) {
		tilew = 2.0 * m_tile_w * h/m_capture_resy / w;
		tileh = 2.0 * m_tile_h * h/m_capture_resy / h;
		stpos_x = -1.0 + 2.0 * (w - h*ren_aspect) / 2.0 / w;
		stpos_y = 1.0;
		xbound = stpos_x + 2.0 * h*ren_aspect / w;
		ybound = -1.0;

		stpos_x = stpos_x + tilew * (tileid % m_tile_xnum);
		stpos_y = stpos_y - tileh * (tileid / m_tile_xnum + 1);
		edpos_x = stpos_x + tilew;
		edpos_y = stpos_y + tileh;
	} else {
		tilew = 2.0 * m_tile_w * w/m_capture_resx / w;
		tileh = 2.0 * m_tile_h * w/m_capture_resx / h;
		stpos_x = -1.0;
		stpos_y = 1.0 - 2.0*(h - w/ren_aspect) / 2.0 / h;
		xbound = 1.0;
		ybound = stpos_y - 2.0 * w/ren_aspect / h;

		stpos_x = stpos_x + tilew * (tileid % m_tile_xnum);
		stpos_y = stpos_y - tileh * (tileid / m_tile_xnum + 1);
		edpos_x = stpos_x + tilew;
		edpos_y = stpos_y + tileh;
	}

	double tex_x = 1.0;
	double tex_y = 0.0;
	if (edpos_x > xbound) {
		tex_x = (xbound - stpos_x) / tilew;
		edpos_x = xbound;
	}
	if (stpos_y < ybound) {
		tex_y = 1.0 - (edpos_y - ybound) / tileh;
		stpos_y = ybound;
	}

	verts.push_back(Vulkan2dRender::Vertex{ {(float)edpos_x, (float)edpos_y, 0.0f}, {(float)tex_x, 1.0f,         0.0f} });
	verts.push_back(Vulkan2dRender::Vertex{ {(float)stpos_x, (float)edpos_y, 0.0f}, {0.0f,         1.0f,         0.0f} });
	verts.push_back(Vulkan2dRender::Vertex{ {(float)stpos_x, (float)stpos_y, 0.0f}, {0.0f,         (float)tex_y, 0.0f} });
	verts.push_back(Vulkan2dRender::Vertex{ {(float)edpos_x, (float)stpos_y, 0.0f}, {(float)tex_x, (float)tex_y, 0.0f} });
	
}

void VRenderVulkanView::CalcAndSetCombinedClippingPlanes()
{
    int i;
    
    m_bounds.reset();
    PopVolumeList();
    PopMeshList();
    
    BBox bounds;
    
    for (i = 0; i < (int)m_vd_pop_list.size(); i++)
    {
        VolumeData* vd = m_vd_pop_list[i];
        if (vd)
        {
            if (!vd->GetTexture())
                continue;
            Transform *tform = vd->GetTexture()->transform();
            if (!tform)
                continue;
            Transform tform_copy;
            double mvmat[16];
            tform->get_trans(mvmat);
            swap(mvmat[3], mvmat[12]);
            swap(mvmat[7], mvmat[13]);
            swap(mvmat[11], mvmat[14]);
            tform_copy.set(mvmat);
            
            FLIVR::Point p[8] = {
                FLIVR::Point(0.0f, 0.0f, 0.0f),
                FLIVR::Point(1.0f, 0.0f, 0.0f),
                FLIVR::Point(1.0f, 1.0f, 0.0f),
                FLIVR::Point(0.0f, 1.0f, 0.0f),
                FLIVR::Point(0.0f, 0.0f, 1.0f),
                FLIVR::Point(1.0f, 0.0f, 1.0f),
                FLIVR::Point(1.0f, 1.0f, 1.0f),
                FLIVR::Point(0.0f, 1.0f, 1.0f)
            };
            
            for (int j = 0; j < 8; j++)
            {
                p[j] = tform_copy.project(p[j]);
                bounds.extend(p[j]);
            }
        }
    }
    
    
    for (i = 0; i < (int)m_md_pop_list.size(); i++)
    {
        m_md_pop_list[i]->RecalcBounds();
        bounds.extend(m_md_pop_list[i]->GetBounds());
    }
    
    for (size_t i=0; i<m_layer_list.size(); i++)
    {
        if (!m_layer_list[i])
            continue;
        if (m_layer_list[i]->IsA() == 4)
        {
            Annotations* ann = (Annotations*)m_layer_list[i];
            if (!ann) continue;
            if (ann->GetDisp() && ann->GetMesh())
            {
                ann->GetMesh()->RecalcBounds();
                bounds.extend(ann->GetMesh()->GetBounds());
            }
        }
    }
    
    for (i = 0; i < (int)m_vd_pop_list.size(); i++)
    {
        
        VolumeData* vd = m_vd_pop_list[i];
        if (vd)
        {
            FLIVR::Point pp[6] = {
                FLIVR::Point(bounds.min().x(), 0.0, 0.0),
                FLIVR::Point(bounds.max().x(), 0.0, 0.0),
                FLIVR::Point(0.0, bounds.min().y(), 0.0),
                FLIVR::Point(0.0, bounds.max().y(), 0.0),
                FLIVR::Point(0.0, 0.0, bounds.max().z()),
                FLIVR::Point(0.0, 0.0, bounds.min().z())
            };
            
            FLIVR::Vector pn[6] = {
                FLIVR::Vector(1.0, 0.0, 0.0),
                FLIVR::Vector(-1.0, 0.0, 0.0),
                FLIVR::Vector(0.0, 1.0, 0.0),
                FLIVR::Vector(0.0, -1.0, 0.0),
                FLIVR::Vector(0.0, 0.0, -1.0),
                FLIVR::Vector(0.0, 0.0, 1.0)
            };
            
            if (!vd->GetVR())
                continue;
            if (!vd->GetTexture())
                continue;
            Transform *tform = vd->GetTexture()->transform();
            if (!tform)
                continue;
            Transform tform_copy;
            double mvmat[16];
            tform->get_trans(mvmat);
            swap(mvmat[3], mvmat[12]);
            swap(mvmat[7], mvmat[13]);
            swap(mvmat[11], mvmat[14]);
            tform_copy.set(mvmat);
            
            vector<Plane*> *planes = vd->GetVR()->get_planes();
            
            for (int j = 0; j < 6; j++)
            {
                pp[j] = tform_copy.unproject(pp[j]);
                pn[j] = tform->project(pn[j]);
                pn[j].safe_normalize();
                
                (*planes)[j]->RememberParam();
                (*planes)[j]->ChangePlane(pp[j], pn[j]);
            }
            
            (*planes)[0]->SetRange((*planes)[0]->get_point(), (*planes)[0]->normal(), (*planes)[1]->get_point(), (*planes)[1]->normal());
            (*planes)[1]->SetRange((*planes)[1]->get_point(), (*planes)[1]->normal(), (*planes)[0]->get_point(), (*planes)[0]->normal());
            (*planes)[2]->SetRange((*planes)[2]->get_point(), (*planes)[2]->normal(), (*planes)[3]->get_point(), (*planes)[3]->normal());
            (*planes)[3]->SetRange((*planes)[3]->get_point(), (*planes)[3]->normal(), (*planes)[2]->get_point(), (*planes)[2]->normal());
            (*planes)[4]->SetRange((*planes)[4]->get_point(), (*planes)[4]->normal(), (*planes)[5]->get_point(), (*planes)[5]->normal());
            (*planes)[5]->SetRange((*planes)[5]->get_point(), (*planes)[5]->normal(), (*planes)[4]->get_point(), (*planes)[4]->normal());
            
            for (int j = 0; j < 6; j++)
                (*planes)[j]->RestoreParam();
        }
    }
    
    for (i = 0; i < (int)m_md_pop_list.size(); i++)
        m_md_pop_list[i]->SetBounds(bounds);
    
    for (size_t i=0; i<m_layer_list.size(); i++)
    {
        if (!m_layer_list[i])
            continue;
        if (m_layer_list[i]->IsA() == 4)
        {
            Annotations* ann = (Annotations*)m_layer_list[i];
            if (!ann) continue;
            if (ann->GetDisp() && ann->GetMesh())
                ann->GetMesh()->SetBounds(bounds);
        }
    }
}

void VRenderVulkanView::ImportBigWarpCSV(wxString inputpath)
{
    wxTextFile csv(inputpath);
    if (!csv.Exists())
        return;
    if (!csv.Open())
        return;
    
    for (int i=m_ruler_list.size()-1; i>=0; i--)
    {
        Ruler* ruler = m_ruler_list[i];
        if (ruler)
            delete ruler;
    }
    m_ruler_list.clear();
    Ruler::ResetID();
    
    wxString line = csv.GetFirstLine();
    while(!csv.Eof())
    {
        wxStringTokenizer tkz(line, wxT(","));
        wxArrayString elems;
        while(tkz.HasMoreTokens())
        {
            wxString token = tkz.GetNextToken();
            token.Replace("\"", "");
            elems.Add(token);
        }
        if (elems.Count() == 8)
        {
            Point p1, p2;
            double dval;
            elems[2].ToDouble(&dval);
            p1.x(dval);
            elems[3].ToDouble(&dval);
            p1.y(dval);
            elems[4].ToDouble(&dval);
            p1.z(-dval);
            elems[5].ToDouble(&dval);
            p2.x(dval);
            elems[6].ToDouble(&dval);
            p2.y(dval);
            elems[7].ToDouble(&dval);
            p2.z(-dval);
            
            Ruler* ruler = new Ruler();
            ruler->SetRulerType(m_ruler_type);
            ruler->AddPoint(p1);
            ruler->SetTimeDep(m_ruler_time_dep);
            ruler->SetTime(m_tseq_cur_num);
            if (p1 != p2) // 2 points
                ruler->AddPoint(p2);
            m_ruler_list.push_back(ruler);
        }
        line = csv.GetNextLine();
    }
    csv.Close();

    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    if (m_vrv && vr_frame && vr_frame->GetMeasureDlg())
        vr_frame->GetMeasureDlg()->GetSettings(m_vrv);
    
    RefreshGL();
}

void VRenderVulkanView::ExportBigWarpCSV(wxString outpath)
{
    wxArrayString lines;
    int count = 0;
    for (size_t i = 0; i < m_ruler_list.size(); ++i)
    {
        if (m_ruler_list[i])
        {
            wxString str;
            if (m_ruler_list[i]->GetRulerType() == 2) //locator (single point)
            {
                Point* p = m_ruler_list[i]->GetPoint(0);
                if (p)
                    str = wxString::Format("\"Pt-%d\",\"true\",\"%.14f\",\"%.14f\",\"%.14f\",\"%.14f\",\"%.14f\",\"%.14f\"", count, p->x(), p->y(), p->z()*-1.0, p->x(), p->y(), p->z()*-1.0);
            }
            else if (m_ruler_list[i]->GetRulerType() == 0) //2 points
            {
                Point* p1 = m_ruler_list[i]->GetPoint(0);
                Point* p2 = m_ruler_list[i]->GetPoint(1);
                if (p1 && p2)
                    str = wxString::Format("\"Pt-%d\",\"true\",\"%.14f\",\"%.14f\",\"%.14f\",\"%.14f\",\"%.14f\",\"%.14f\"", count, p1->x(), p1->y(), p1->z()*-1.0, p2->x(), p2->y(), p2->z()*-1.0);
            }
            
            if (!str.IsEmpty())
            {
                lines.Add(str);
                count++;
            }
        }
    }
    
    if (lines.Count() == 0)
        return;
    
    wxTextFile csv(outpath);
    if (csv.Exists())
    {
        if (!csv.Open())
            return;
        csv.Clear();
    }
    else
    {
        if (!csv.Create())
            return;
    }
    for(int j = 0; j < lines.Count(); j++)
        csv.AddLine(lines[j]);
    csv.Write();
    csv.Close();
}

void VRenderVulkanView::WarpCurrentVolume()
{
    wxString temp_csv_path = wxStandardPaths::Get().GetTempDir() + wxFileName::GetPathSeparator() + "warp.csv";
    ExportBigWarpCSV(temp_csv_path);
    
    wxString fi_name = _("Fiji Interface");

    VRenderFrame *vframe = (VRenderFrame *)m_frame;
    if (!vframe) return;

    if (!vframe->PluginExists(fi_name))
    {
        wxMessageBox("ERROR: Could not found Fiji interface", "Warping");
        return;
    }

    if (!vframe->IsCreatedPluginWindow(fi_name))
    {
        vframe->CreatePluginWindow(fi_name);
        vframe->ToggleVisibilityPluginWindow(fi_name, false);
    }
    
    if (m_cur_vol && !m_cur_vol->isBrxml())
    {
        int resx, resy, resz;
        double spcx, spcy, spcz;
        double offsetx, offsety, offsetz;
        m_cur_vol->GetResolution(resx, resy, resz);
        m_cur_vol->GetSpacings(spcx, spcy, spcz);
        
        bool crop = true;
        BBox bbox;
        if (crop)
        {
            //Cropping
            vector<Plane*> *planes = m_cur_vol->GetVR()->get_planes();
            Plane* px1 = (*planes)[0];
            Plane* px2 = (*planes)[1];
            Plane* py1 = (*planes)[2];
            Plane* py2 = (*planes)[3];
            Plane* pz1 = (*planes)[4];
            Plane* pz2 = (*planes)[5];
            
            //calculate 4 lines
            Vector lv_x1z1, lv_x1z2, lv_x2z1, lv_x2z2;
            Point lp_x1z1, lp_x1z2, lp_x2z1, lp_x2z2;
            //x1z1
            if (!px1->Intersect(*pz1, lp_x1z1, lv_x1z1))
                crop = false;
            //x1z2
            if (!px1->Intersect(*pz2, lp_x1z2, lv_x1z2))
                crop = false;
            //x2z1
            if (!px2->Intersect(*pz1, lp_x2z1, lv_x2z1))
                crop = false;
            //x2z2
            if (!px2->Intersect(*pz2, lp_x2z2, lv_x2z2))
                crop = false;
            
            //calculate 8 points
            Point pp[8];
            //p0 = l_x1z1 * py1
            if (!py1->Intersect(lp_x1z1, lv_x1z1, pp[0]))
                crop = false;
            //p1 = l_x1z2 * py1
            if (!py1->Intersect(lp_x1z2, lv_x1z2, pp[1]))
                crop = false;
            //p2 = l_x2z1 *py1
            if (!py1->Intersect(lp_x2z1, lv_x2z1, pp[2]))
                crop = false;
            //p3 = l_x2z2 * py1
            if (!py1->Intersect(lp_x2z2, lv_x2z2, pp[3]))
                crop = false;
            //p4 = l_x1z1 * py2
            if (!py2->Intersect(lp_x1z1, lv_x1z1, pp[4]))
                crop = false;
            //p5 = l_x1z2 * py2
            if (!py2->Intersect(lp_x1z2, lv_x1z2, pp[5]))
                crop = false;
            //p6 = l_x2z1 * py2
            if (!py2->Intersect(lp_x2z1, lv_x2z1, pp[6]))
                crop = false;
            //p7 = l_x2z2 * py2
            if (!py2->Intersect(lp_x2z2, lv_x2z2, pp[7]))
                crop = false;
            
            for (int i = 0; i < 8; i++)
                bbox.extend(pp[i]);
        }
        
        if (!crop)
        {
            bbox.extend(Point(0.0, 0.0, 0.0));
            bbox.extend(Point(1.0, 1.0, 1.0));
        }

        //save data
        auto vlnrrd = m_cur_vol->GetTexture()->get_nrrd(0);
        if (vlnrrd && vlnrrd->getNrrd())
        {
            Nrrd* data = vlnrrd->getNrrd();
                
            int w = int(data->axis[0].size);
            int h = int(data->axis[1].size);
            int d = int(data->axis[2].size);
                
            int sx = (int)(bbox.min().x() * w + 0.5);
            int sy = (int)(bbox.min().y() * h + 0.5);
            int sz = (int)(bbox.min().z() * d + 0.5);
            int nx = (int)(bbox.max().x() * w + 0.5) - sx;
            int ny = (int)(bbox.max().y() * h + 0.5) - sy;
            int nz = (int)(bbox.max().z() * d + 0.5) - sz;
            
            resx = nx;
            resy = ny;
            resz = nz;
            offsetx = sx;
            offsety = sy;
            offsetz = sz;
            
            wxString command = wxString::Format("Apply Bigwarp Filter, landmark_file=%s x_size=%d y_size=%d z_size=%d x_spacing=%f y_spacing=%f z_spacing=%f x_offset=%f y_offset=%f z_offset=%f transform_type=[Thin Plate Spline] interpolation=Linear threads=8;false;false", temp_csv_path, resx, resy, resz, spcx, spcy, spcz, offsetx, offsety, offsetz);
            vframe->RunPlugin(fi_name, command);
        }
    }
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(LegendListCtrl, wxListCtrl)
EVT_LIST_ITEM_SELECTED(wxID_ANY, LegendListCtrl::OnItemSelected)
EVT_LIST_ITEM_CHECKED(wxID_ANY, LegendListCtrl::OnItemChecked)
EVT_LIST_ITEM_UNCHECKED(wxID_ANY, LegendListCtrl::OnItemUnchecked)
EVT_LEFT_DOWN(LegendListCtrl::OnLeftDown)
EVT_SCROLLWIN(LegendListCtrl::OnScroll)
EVT_MOUSEWHEEL(LegendListCtrl::OnScroll)
END_EVENT_TABLE()

LegendListCtrl::LegendListCtrl(wxWindow* parent,
                               VRenderView* view,
                               wxWindowID id,
                               const wxPoint& pos,
                               const wxSize& size,
                               long style) :
wxListCtrl(parent, id, pos, size, style)
{
    m_view = view;
    
    SetEvtHandlerEnabled(false);
    Freeze();
    
    wxListItem itemCol;
    itemCol.SetText("Name");
    InsertColumn(0, itemCol);
    itemCol.SetText("Type");
    InsertColumn(1, itemCol);
    EnableCheckBoxes(true);
    
    UpdateContents();
    
    Thaw();
    SetEvtHandlerEnabled(true);
}

LegendListCtrl::~LegendListCtrl()
{
    
}

wxString LegendListCtrl::GetText(long item, int col)
{
    wxListItem info;
    info.SetId(item);
    info.SetColumn(col);
    info.SetMask(wxLIST_MASK_TEXT);
    GetItem(info);
    return info.GetText();
}

void LegendListCtrl::UpdateContents()
{
    if (m_view)
    {
        SetEvtHandlerEnabled(false);
        Freeze();
        
        DeleteAllItems();
        
        vector<wxString> choices;
        vector<wxString> types;
        vector<bool> disp_states;
        for (int i = 0; i < m_view->GetDispVolumeNum(); i++)
        {
            VolumeData* vd = m_view->GetDispVolumeData(i);
            if (vd)
            {
                choices.push_back(vd->GetName());
                types.push_back("Volume");
                disp_states.push_back(vd->GetLegend());
            }
        }
        for (int i = 0; i < m_view->GetMeshNum(); i++)
        {
            MeshData* md = m_view->GetMeshData(i);
            if (md)
            {
                choices.push_back(md->GetName());
                types.push_back("Mesh");
                disp_states.push_back(md->GetLegend());
            }
        }
        
        for (int i = 0; i < choices.size(); i++)
        {
            InsertItem(i, choices[i]);
            SetItem(i, 1, types[i]);
            CheckItem(i, disp_states[i]);
        }
        SetColumnWidth(0, wxLIST_AUTOSIZE);
        SetColumnWidth(1, 0);
        
        Thaw();
        SetEvtHandlerEnabled(true);
    }
}

void LegendListCtrl::OnItemSelected(wxListEvent& event)
{
    SetItemState(event.GetIndex(), 0, wxLIST_STATE_SELECTED|wxLIST_STATE_FOCUSED);
}

void LegendListCtrl::OnItemChecked(wxListEvent& event)
{
    if (m_view)
    {
        wxString name = GetItemText(event.GetIndex(), 0);
        wxString type = GetItemText(event.GetIndex(), 1);
        if (type == "Volume")
        {
            for (int i = 0; i < m_view->GetDispVolumeNum(); i++)
            {
                VolumeData* vd = m_view->GetDispVolumeData(i);
                if (vd && name == vd->GetName())
                {
                    vd->SetLegend(true);
                }
            }
        }
        else if (type == "Mesh")
        {
            for (int i = 0; i < m_view->GetMeshNum(); i++)
            {
                MeshData* md = m_view->GetMeshData(i);
                if (md && name == md->GetName())
                {
                    md->SetLegend(true);
                }
            }
        }
        m_view->RefreshGL();
    }
}

void LegendListCtrl::OnItemUnchecked(wxListEvent& event)
{
    if (m_view)
    {
        wxString name = GetItemText(event.GetIndex(), 0);
        wxString type = GetItemText(event.GetIndex(), 1);
        if (type == "Volume")
        {
            for (int i = 0; i < m_view->GetDispVolumeNum(); i++)
            {
                VolumeData* vd = m_view->GetDispVolumeData(i);
                if (vd && name == vd->GetName())
                {
                    vd->SetLegend(false);
                }
            }
        }
        else if (type == "Mesh")
        {
            for (int i = 0; i < m_view->GetMeshNum(); i++)
            {
                MeshData* md = m_view->GetMeshData(i);
                if (md && name == md->GetName())
                {
                    md->SetLegend(false);
                }
            }
        }
        m_view->RefreshGL();
    }
}

void LegendListCtrl::OnLeftDown(wxMouseEvent& event)
{
    wxPoint pos = event.GetPosition();
    int flags = wxLIST_HITTEST_ONITEM;
    long item = HitTest(pos, flags, NULL);
    if (item != -1)
    {
        wxString name = GetText(item, 0);
        if (IsItemChecked(item))
            CheckItem(item, false);
        else
            CheckItem(item, true);
    }
    event.Skip(false);
}

void LegendListCtrl::OnScroll(wxScrollWinEvent& event)
{
    event.Skip(true);
}

void LegendListCtrl::OnScroll(wxMouseEvent& event)
{
    event.Skip(true);
}


LegendPanel::LegendPanel(wxWindow* frame, VRenderView* view)
: wxPanel(frame, wxID_ANY, wxDefaultPosition, wxSize(100, 100), 0, "Legend")
{
    m_frame = frame;
    m_view = view;
    
    SetEvtHandlerEnabled(false);
    Freeze();
    
    m_list = new LegendListCtrl(this, m_view, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    
    //all controls
    wxBoxSizer *sizerV = new wxBoxSizer(wxVERTICAL);
    sizerV->Add(m_list, 1, wxEXPAND);
    SetSizer(sizerV);
    Layout();
    
    Thaw();
    SetEvtHandlerEnabled(true);
}

LegendPanel::~LegendPanel()
{
    
}

void LegendPanel::Reload()
{
    m_list->UpdateContents();
}

wxSize LegendPanel::GetListSize()
{
    m_list->SetColumnWidth(0, wxLIST_AUTOSIZE);
    int w = m_list->GetColumnWidth(0) + 30;
    m_list->SetColumnWidth(0, w);
    
    long height = 0L;
    for (long i = 0; i < m_list->GetItemCount(); i++)
    {
        wxRect rect;
        m_list->GetItemRect(i, rect);
        height += rect.height;
    }
    
    m_list->SetSize(wxSize(w, height));
    
    return wxSize(w, height);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(VRenderView, wxPanel)
	//bar top
	EVT_RADIOBUTTON(ID_VolumeSeqRd, VRenderView::OnVolumeMethodCheck)
	EVT_RADIOBUTTON(ID_VolumeMultiRd, VRenderView::OnVolumeMethodCheck)
	EVT_RADIOBUTTON(ID_VolumeCompRd, VRenderView::OnVolumeMethodCheck)
	EVT_BUTTON(ID_CaptureBtn, VRenderView::OnCapture)
	EVT_COLOURPICKER_CHANGED(ID_BgColorPicker, VRenderView::OnBgColorChange)
	EVT_CHECKBOX(ID_CamCtrChk, VRenderView::OnCamCtrCheck)
	EVT_CHECKBOX(ID_FpsChk, VRenderView::OnFpsCheck)
	EVT_CHECKBOX(ID_LegendChk, VRenderView::OnLegendCheck)
	EVT_CHECKBOX(ID_IntpChk, VRenderView::OnIntpCheck)
	EVT_CHECKBOX(ID_SearchChk, VRenderView::OnSearchCheck)
	EVT_COMMAND_SCROLL(ID_AovSldr, VRenderView::OnAovChange)
	EVT_TEXT(ID_AovText, VRenderView::OnAovText)
	EVT_CHECKBOX(ID_FreeChk, VRenderView::OnFreeChk)
	EVT_COMMAND_SCROLL(ID_PPISldr, VRenderView::OnPPIChange)
	EVT_TEXT(ID_PPIText, VRenderView::OnPPIEdit)
	EVT_COMBOBOX(ID_ResCombo, VRenderView::OnResModesCombo)
	//bar left
	EVT_CHECKBOX(ID_DepthAttenChk, VRenderView::OnDepthAttenCheck)
	EVT_COMMAND_SCROLL(ID_DepthAttenFactorSldr, VRenderView::OnDepthAttenFactorChange)
	EVT_TEXT(ID_DepthAttenFactorText, VRenderView::OnDepthAttenFactorEdit)
	EVT_BUTTON(ID_DepthAttenResetBtn, VRenderView::OnDepthAttenReset)
	//bar right
	EVT_BUTTON(ID_CenterBtn, VRenderView::OnCenter)
	EVT_BUTTON(ID_Scale121Btn, VRenderView::OnScale121)
	EVT_COMMAND_SCROLL(ID_ScaleFactorSldr, VRenderView::OnScaleFactorChange)
	EVT_TEXT(ID_ScaleFactorText, VRenderView::OnScaleFactorEdit)
	EVT_BUTTON(ID_ScaleResetBtn, VRenderView::OnScaleReset)
#if defined(__WXGTK__)
	EVT_SPIN_UP(ID_ScaleFactorSpin, VRenderView::OnScaleFactorSpinUp)
	EVT_SPIN_DOWN(ID_ScaleFactorSpin, VRenderView::OnScaleFactorSpinDown)
#else
	EVT_SPIN_UP(ID_ScaleFactorSpin, VRenderView::OnScaleFactorSpinDown)
	EVT_SPIN_DOWN(ID_ScaleFactorSpin, VRenderView::OnScaleFactorSpinUp)
#endif
	//bar bottom
	EVT_CHECKBOX(ID_RotLinkChk, VRenderView::OnRotLink)
	EVT_BUTTON(ID_RotResetBtn, VRenderView::OnRotReset)
	EVT_TEXT(ID_XRotText, VRenderView::OnValueEdit)
	EVT_TEXT(ID_YRotText, VRenderView::OnValueEdit)
	EVT_TEXT(ID_ZRotText, VRenderView::OnValueEdit)
	EVT_COMMAND_SCROLL(ID_XRotSldr, VRenderView::OnXRotScroll)
	EVT_COMMAND_SCROLL(ID_YRotSldr, VRenderView::OnYRotScroll)
	EVT_COMMAND_SCROLL(ID_ZRotSldr, VRenderView::OnZRotScroll)
	EVT_CHECKBOX(ID_RotLockChk, VRenderView::OnRotLockCheck)
	//spin buttons
#if defined(__WXGTK__)
	EVT_SPIN_UP(ID_XRotSpin, VRenderView::OnXRotSpinDown)
	EVT_SPIN_DOWN(ID_XRotSpin, VRenderView::OnXRotSpinUp)
	EVT_SPIN_UP(ID_YRotSpin, VRenderView::OnYRotSpinDown)
	EVT_SPIN_DOWN(ID_YRotSpin, VRenderView::OnYRotSpinUp)
	EVT_SPIN_UP(ID_ZRotSpin, VRenderView::OnZRotSpinDown)
	EVT_SPIN_DOWN(ID_ZRotSpin, VRenderView::OnZRotSpinUp)
#else
	EVT_SPIN_UP(ID_XRotSpin, VRenderView::OnXRotSpinUp)
	EVT_SPIN_DOWN(ID_XRotSpin, VRenderView::OnXRotSpinDown)
	EVT_SPIN_UP(ID_YRotSpin, VRenderView::OnYRotSpinUp)
	EVT_SPIN_DOWN(ID_YRotSpin, VRenderView::OnYRotSpinDown)
	EVT_SPIN_UP(ID_ZRotSpin, VRenderView::OnZRotSpinUp)
	EVT_SPIN_DOWN(ID_ZRotSpin, VRenderView::OnZRotSpinDown)
#endif
	//reset
	EVT_BUTTON(ID_DefaultBtn, VRenderView::OnSaveDefault)
	EVT_TIMER(ID_Timer, VRenderView::OnAovSldrIdle)

    EVT_BUTTON(ID_LegendBtn, VRenderView::OnLegendButton)

	EVT_KEY_DOWN(VRenderView::OnKeyDown)
	END_EVENT_TABLE()

int VRenderView::m_cap_resx = -1;
int VRenderView::m_cap_resy = -1;
int VRenderView::m_cap_orgresx = -1;
int VRenderView::m_cap_orgresy = -1;
int VRenderView::m_cap_dispresx = -1;
int VRenderView::m_cap_dispresy = -1;
wxTextCtrl* VRenderView::m_cap_w_txt = NULL;
wxTextCtrl* VRenderView::m_cap_h_txt = NULL;

VRenderView::VRenderView(wxWindow* frame,
	wxWindow* parent,
	wxWindowID id,
	const std::shared_ptr<VVulkan> &sharedContext,
	const wxPoint& pos,
	const wxSize& size,
	long style) :
wxPanel(parent, id, pos, size, style),
	m_default_saved(false),
	m_frame(frame),
	m_draw_clip(false),
	m_use_dft_settings(false),
	m_res_mode(0)
{
	SetEvtHandlerEnabled(false);
	Freeze();

	//full frame
	m_full_frame = new wxFrame((wxFrame*)NULL, wxID_ANY, "FluoRender");
	m_view_sizer = new wxBoxSizer(wxVERTICAL);

	wxString name = wxString::Format("Render View:%d", m_id++);
	this->SetName(name);

	m_glview = new VRenderVulkanView(frame, this, wxID_ANY);
	m_glview->SetCanFocus(false);

	CreateBar();
	if (m_glview) {
		m_glview->SetSBText("µm");
		m_glview->SetScaleBarLen(50.);
	}
	LoadSettings();

	Thaw();
	SetEvtHandlerEnabled(true);

	m_idleTimer = new wxTimer(this, ID_Timer);
	m_idleTimer->Start(100);
}

VRenderView::~VRenderView()
{
	m_idleTimer->Stop();
	wxDELETE(m_idleTimer);
	if (m_glview)
		delete m_glview;
	if (m_full_frame)
		delete m_full_frame;
}

void VRenderView::CreateBar()
{
	//validator: floating point 1
	wxFloatingPointValidator<double> vald_fp1(1);
	vald_fp1.SetRange(0.0, 360.0);
	//validator: floating point 2
	wxFloatingPointValidator<double> vald_fp2(2);
	vald_fp2.SetRange(0.0, 1.0);
	//validator: integer
	wxIntegerValidator<unsigned int> vald_int;
	vald_int.SetMin(1);

	wxBoxSizer* sizer_v = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* sizer_m = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText *st1, *st2, *st3, *st4;

#if defined(__WXGTK__)
	int top_h = 32;
	int spin_right_w = 70;
	int spin_w = 70;
	int combo_w = 140;
#else
	int top_h = 24;
	int spin_right_w = 40;
	int spin_w = 20;
	int combo_w = 100;
#endif

	//bar top///////////////////////////////////////////////////
	wxBoxSizer* sizer_h_1 = new wxBoxSizer(wxHORIZONTAL);
	m_volume_seq_rd = new wxRadioButton(this, ID_VolumeSeqRd, "Layered",
		wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	m_volume_multi_rd = new wxRadioButton(this, ID_VolumeMultiRd, "Depth",
		wxDefaultPosition, wxDefaultSize);
	m_volume_comp_rd = new wxRadioButton(this, ID_VolumeCompRd, "Composite",
		wxDefaultPosition, wxDefaultSize);
	m_volume_seq_rd->SetValue(false);
	m_volume_multi_rd->SetValue(false);
	m_volume_comp_rd->SetValue(false);
	switch (m_glview->GetVolMethod())
	{
	case VOL_METHOD_SEQ:
		m_volume_seq_rd->SetValue(true);
		break;
	case VOL_METHOD_MULTI:
		m_volume_multi_rd->SetValue(true);
		break;
	case VOL_METHOD_COMP:
		m_volume_comp_rd->SetValue(true);
		break;
	}
	m_capture_btn = new wxButton(this, ID_CaptureBtn, "Capture",
		wxDefaultPosition, wxSize(80, top_h));
	st1 = new wxStaticText(this, 0, "Background:");
	m_bg_color_picker = new wxColourPickerCtrl(this, ID_BgColorPicker, *wxBLACK,
		wxDefaultPosition, wxDefaultSize/*wxSize(40, 20)*/);
	m_cam_ctr_chk = new wxCheckBox(this, ID_CamCtrChk, "Center",
		wxDefaultPosition, wxSize(-1, top_h));
	m_cam_ctr_chk->SetValue(false);
	m_fps_chk = new wxCheckBox(this, ID_FpsChk, "Info.",
		wxDefaultPosition, wxSize(-1, top_h));
	m_fps_chk->SetValue(false);
    m_legend_chk = new wxCheckBox(this, ID_LegendChk, "",
                                  wxDefaultPosition, wxSize(-1, top_h));
    m_legend_chk->SetValue(true);
    m_legend_chk->Hide();
    m_legend_btn = new wxButton(this, ID_LegendBtn, "Legend",
                                 wxDefaultPosition, wxSize(60, top_h));
    m_legend_list = NULL;
	m_intp_chk = new wxCheckBox(this, ID_IntpChk, "Intrp.",
		wxDefaultPosition, wxSize(-1, top_h));
	m_intp_chk->SetValue(true);
	m_search_chk = new wxCheckBox(this, ID_SearchChk, "Search",
		wxDefaultPosition, wxSize(-1, top_h));
	m_search_chk->SetValue(false);
	m_search_chk->Hide();
#ifndef WITH_DATABASE
	m_search_chk->Hide();
#endif // !WITH_DATABASE
	//angle of view
	st2 = new wxStaticText(this, 0, "V. AOV:");
	m_aov_sldr = new wxSlider(this, ID_AovSldr, 45, 10, 100,
		wxDefaultPosition, wxSize(120, top_h), wxSL_HORIZONTAL);
	m_aov_sldr->SetValue(GetPersp()?GetAov():10);
	m_aov_text = new wxTextCtrl(this, ID_AovText, "",
		wxDefaultPosition, wxSize(60, top_h), 0, vald_int);
	m_aov_text->ChangeValue(GetPersp()?wxString::Format("%d", int(GetAov())):"Ortho");
	m_free_chk = new wxCheckBox(this, ID_FreeChk, "FreeFly",
		wxDefaultPosition, wxSize(-1, top_h));
	if (GetFree())
		m_free_chk->SetValue(true);
	else
		m_free_chk->SetValue(false);

	m_ppi_sldr = new wxSlider(this, ID_PPISldr, 20, 5, 100,
		wxDefaultPosition, wxSize(120, top_h), wxSL_HORIZONTAL);
	m_ppi_sldr->Hide();
	m_ppi_text = new wxTextCtrl(this, ID_PPIText, "20",
		wxDefaultPosition, wxSize(40, top_h), 0, vald_int);
	m_ppi_text->Hide();

    m_res_mode_text = new wxStaticText(this, 0, "Quality:");
	m_res_mode_combo = new wxComboBox(this, ID_ResCombo, "",
		wxDefaultPosition, wxSize(combo_w, top_h), 0, NULL, wxCB_READONLY);
	vector<string>mode_list;
	mode_list.push_back("Max");
	mode_list.push_back("Best(x1)");
	mode_list.push_back("Fine(x1.5)");
	mode_list.push_back("Standard(x2)");
	mode_list.push_back("Coarse(x3)");
	mode_list.push_back("Min");
	for (size_t i=0; i<mode_list.size(); ++i)
		m_res_mode_combo->Append(mode_list[i]);
	m_res_mode_combo->SetSelection(m_res_mode);

	//
	sizer_h_1->Add(10, 5, 0);
	sizer_h_1->Add(m_volume_seq_rd, 0, wxALIGN_CENTER, 0);
	sizer_h_1->Add(m_volume_multi_rd, 0, wxALIGN_CENTER, 0);
	sizer_h_1->Add(m_volume_comp_rd, 0, wxALIGN_CENTER, 0);
	sizer_h_1->Add(10, 5, 0);
	sizer_h_1->Add(m_capture_btn, 0, wxALIGN_CENTER, 0);
	sizer_h_1->Add(10, 5, 0);
	sizer_h_1->Add(st1, 0, wxALIGN_CENTER, 0);
	sizer_h_1->Add(m_bg_color_picker, 0, wxALIGN_CENTER, 0);
	sizer_h_1->Add(10, 5, 0);
	sizer_h_1->Add(m_cam_ctr_chk, 0, wxALIGN_CENTER);
	sizer_h_1->Add(5, 5, 0);
	sizer_h_1->Add(m_fps_chk, 0, wxALIGN_CENTER);
	sizer_h_1->Add(5, 5, 0);
    sizer_h_1->Add(m_legend_chk, 0, wxALIGN_CENTER);
	sizer_h_1->Add(m_legend_btn, 0, wxALIGN_CENTER);
    //m_legend_btn->Disable();
	sizer_h_1->Add(10, 5, 0);
	sizer_h_1->Add(m_intp_chk, 0, wxALIGN_CENTER);
	sizer_h_1->Add(5, 5, 0);
	//	sizer_h_1->Add(m_search_chk, 0, wxALIGN_CENTER);
	//	sizer_h_1->Add(5, 5, 0);
	sizer_h_1->Add(m_res_mode_text, 0, wxALIGN_CENTER, 0);
	sizer_h_1->Add(m_res_mode_combo, 0, wxALIGN_CENTER);
	sizer_h_1->Add(10, 5, 0);
	sizer_h_1->Add(st2, 0, wxALIGN_CENTER, 0);
	sizer_h_1->Add(m_aov_sldr, 0, wxALIGN_CENTER);
	sizer_h_1->Add(m_aov_text, 0, wxALIGN_CENTER);
	sizer_h_1->Add(5, 5, 0);
	sizer_h_1->Add(m_free_chk, 0, wxALIGN_CENTER);
	sizer_h_1->Add(5, 5, 0);
	sizer_h_1->Add(m_ppi_sldr, 0, wxALIGN_CENTER);
	sizer_h_1->Add(m_ppi_text, 0, wxALIGN_CENTER);

	//bar left///////////////////////////////////////////////////
	wxBoxSizer* sizer_v_3 = new wxBoxSizer(wxVERTICAL);
	st1 = new wxStaticText(this, 0, "Depth\nInt:\n");
	m_depth_atten_chk = new wxCheckBox(this, ID_DepthAttenChk, "");
	m_depth_atten_chk->SetValue(true);
	m_depth_atten_factor_sldr = new wxSlider(this, ID_DepthAttenFactorSldr, 0, 0, 100,
		wxDefaultPosition, wxDefaultSize, wxSL_VERTICAL|wxSL_INVERSE);
	m_depth_atten_reset_btn = new wxButton(this, ID_DepthAttenResetBtn, "Reset\nDepth\nInt",
		wxDefaultPosition, wxSize(60, 60));
	m_depth_atten_factor_text = new wxTextCtrl(this, ID_DepthAttenFactorText, "0.0",
		wxDefaultPosition, wxSize(40, 20), 0, vald_fp2);
	sizer_v_3->Add(5, 10, 0);
	sizer_v_3->Add(st1, 0, wxALIGN_CENTER);
	sizer_v_3->Add(m_depth_atten_chk, 0, wxALIGN_CENTER);
	sizer_v_3->Add(m_depth_atten_factor_sldr, 1, wxALIGN_CENTER);
	sizer_v_3->Add(m_depth_atten_reset_btn, 0, wxALIGN_CENTER);
	sizer_v_3->Add(m_depth_atten_factor_text, 0, wxEXPAND);

	//bar right///////////////////////////////////////////////////
	wxBoxSizer* sizer_v_4 = new wxBoxSizer(wxVERTICAL);
	st1 = new wxStaticText(this, 0, "Zoom:\n");
	m_scale_121_btn = new wxButton(this, ID_Scale121Btn, "1:1",
		wxDefaultPosition, wxSize(40, 20));
	m_scale_factor_sldr = new wxSlider(this, ID_ScaleFactorSldr, 100, 50, 999,
		wxDefaultPosition, wxDefaultSize, wxSL_VERTICAL);
	m_scale_reset_btn = new wxButton(this, ID_ScaleResetBtn, "Reset\nZoom",
		wxDefaultPosition, wxSize(60, 40));
	m_scale_factor_text = new wxTextCtrl(this, ID_ScaleFactorText, "100",
		wxDefaultPosition, wxSize(40, 20), 0, vald_int);
	m_scale_factor_spin = new wxSpinButton(this, ID_ScaleFactorSpin,
		wxDefaultPosition, wxSize(spin_right_w, 25), wxSP_VERTICAL|wxSP_ARROW_KEYS);
    m_scale_factor_spin->SetRange(-INT_MAX, INT_MAX);
    m_scale_factor_spin->SetValue(100);
	sizer_v_4->Add(5, 10, 0);
	sizer_v_4->Add(st1, 0, wxALIGN_CENTER);
	sizer_v_4->Add(m_scale_121_btn, 0, wxALIGN_CENTER);
	sizer_v_4->Add(m_scale_factor_sldr, 1, wxALIGN_CENTER);
	sizer_v_4->Add(m_scale_factor_spin, 0, wxALIGN_CENTER);
	sizer_v_4->Add(m_scale_reset_btn, 0, wxALIGN_CENTER);
	sizer_v_4->Add(m_scale_factor_text, 0, wxEXPAND);

	//middle sizer
	sizer_m->Add(sizer_v_3, 0, wxEXPAND);
	sizer_m->Add(m_glview, 1, wxEXPAND);
	sizer_m->Add(sizer_v_4, 0, wxEXPAND);

	//bar bottom///////////////////////////////////////////////////
	wxBoxSizer* sizer_h_2 = new wxBoxSizer(wxHORIZONTAL);
	m_rot_link_chk = new wxCheckBox(this, ID_RotLinkChk, "Link");
	m_rot_reset_btn = new wxButton(this, ID_RotResetBtn, "Reset Rot.",
		wxDefaultPosition, wxSize(85, 20));
	st1 = new wxStaticText(this, 0, "X:");
	m_x_rot_sldr = new wxSlider(this, ID_XRotSldr, 0, 0, 360,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_x_rot_text = new wxTextCtrl(this, ID_XRotText, "0.0",
		wxDefaultPosition, wxSize(60,20), 0, vald_fp1);
	m_x_rot_spin = new wxSpinButton(this, ID_XRotSpin,
		wxDefaultPosition, wxSize(spin_w, 20), wxSP_HORIZONTAL);
    m_x_rot_spin->SetRange(-INT_MAX, INT_MAX);
    m_x_rot_spin->SetValue(180);
	st2 = new wxStaticText(this, 0, "Y:");
	m_y_rot_sldr = new wxSlider(this, ID_YRotSldr, 0, 0, 360,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_y_rot_text = new wxTextCtrl(this, ID_YRotText, "0.0",
		wxDefaultPosition, wxSize(60,20), 0, vald_fp1);
	m_y_rot_spin = new wxSpinButton(this, ID_YRotSpin,
		wxDefaultPosition, wxSize(spin_w, 20), wxSP_HORIZONTAL);
    m_y_rot_spin->SetRange(-INT_MAX, INT_MAX);
    m_y_rot_spin->SetValue(180);
	st3 = new wxStaticText(this, 0, "Z:");
	m_z_rot_sldr = new wxSlider(this, ID_ZRotSldr, 0, 0, 360,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_z_rot_text = new wxTextCtrl(this, ID_ZRotText, "0.0",
		wxDefaultPosition, wxSize(60,20), 0, vald_fp1);
	m_z_rot_spin = new wxSpinButton(this, ID_ZRotSpin,
		wxDefaultPosition, wxSize(spin_w, 20), wxSP_HORIZONTAL);
    m_z_rot_spin->SetRange(-INT_MAX, INT_MAX);
    m_z_rot_spin->SetValue(180);
	m_rot_lock_chk = new wxCheckBox(this, ID_RotLockChk, "45 Increments");
    m_center_btn = new wxButton(this, ID_CenterBtn, "Reset Transl.",
        wxDefaultPosition, wxSize(100, 20));
	m_default_btn = new wxButton(this, ID_DefaultBtn, "Save as Default",
		wxDefaultPosition, wxSize(115, 20));
    sizer_h_2->Add(m_rot_link_chk, 0, wxALIGN_CENTER);
	sizer_h_2->Add(5, 5, 0);
    sizer_h_2->Add(m_rot_reset_btn, 0, wxALIGN_CENTER);
    sizer_h_2->Add(5, 5, 0);
	sizer_h_2->Add(st1, 0, wxALIGN_CENTER, 0);
	sizer_h_2->Add(m_x_rot_sldr, 1, wxEXPAND, 0);
	sizer_h_2->Add(m_x_rot_spin, 0, wxALIGN_CENTER, 0);
	sizer_h_2->Add(m_x_rot_text, 0, wxALIGN_CENTER, 0);
	sizer_h_2->Add(5, 5, 0);
	sizer_h_2->Add(st2, 0, wxALIGN_CENTER, 0);
	sizer_h_2->Add(m_y_rot_sldr, 1, wxEXPAND, 0);
	sizer_h_2->Add(m_y_rot_spin, 0, wxALIGN_CENTER, 0);
	sizer_h_2->Add(m_y_rot_text, 0, wxALIGN_CENTER, 0);
	sizer_h_2->Add(5, 5, 0);
	sizer_h_2->Add(st3, 0, wxALIGN_CENTER, 0);
	sizer_h_2->Add(m_z_rot_sldr, 1, wxEXPAND, 0);
	sizer_h_2->Add(m_z_rot_spin, 0, wxALIGN_CENTER, 0);
	sizer_h_2->Add(m_z_rot_text, 0, wxALIGN_CENTER, 0);
	sizer_h_2->Add(5, 5, 0);
	sizer_h_2->Add(m_rot_lock_chk, 0, wxALIGN_CENTER, 0);
	sizer_h_2->Add(5, 5, 0);
    sizer_h_2->Add(m_center_btn, 0, wxALIGN_CENTER);
    sizer_h_2->Add(5, 5, 0);
	sizer_h_2->Add(m_default_btn, 0, wxALIGN_CENTER);

	sizer_v->Add(sizer_h_1, 0, wxEXPAND);
	sizer_v->Add(sizer_m, 1, wxEXPAND);
	sizer_v->Add(sizer_h_2, 0, wxEXPAND);

	SetSizer(sizer_v);
	Layout();
    
    m_res_mode_combo->Hide();
    m_res_mode_text->Hide();
}

//recalculate view according to object bounds
void VRenderView::InitView(unsigned int type)
{
	if (m_glview)
		m_glview->InitView(type);
	if (m_use_dft_settings)
		UpdateView();
}

void VRenderView::Clear()
{
	if (m_glview)
		m_glview->Clear();
	ClearVolList();
	ClearMeshList();
}

//data management
int VRenderView::GetDispVolumeNum()
{
	if (m_glview)
		return m_glview->GetDispVolumeNum();
	else
		return 0;
}

int VRenderView::GetAny()
{
	if (m_glview)
		return m_glview->GetAny();
	else
		return 0;
}

int VRenderView::GetAllVolumeNum()
{
	if (m_glview)
		return m_glview->GetAllVolumeNum();
	else return 0;
}

int VRenderView::GetMeshNum()
{
	if (m_glview)
		return m_glview->GetMeshNum();
	else
		return 0;
}

int VRenderView::GetGroupNum()
{
	if (m_glview)
		return m_glview->GetGroupNum();
	else
		return 0;
}

int VRenderView::GetLayerNum()
{
	if (m_glview)
		return m_glview->GetLayerNum();
	else
		return 0;
}

VolumeData* VRenderView::GetAllVolumeData(int index)
{
	if (m_glview)
		return m_glview->GetAllVolumeData(index);
	else
		return 0;
}

VolumeData* VRenderView::GetDispVolumeData(int index)
{
	if (m_glview)
		return m_glview->GetDispVolumeData(index);
	else
		return 0;
}

MeshData* VRenderView::GetMeshData(int index)
{
	if (m_glview)
		return m_glview->GetMeshData(index);
	else
		return 0;
}

TreeLayer* VRenderView::GetLayer(int index)
{
	if (m_glview)
		return m_glview->GetLayer(index);
	else
		return 0;
}

MultiVolumeRenderer* VRenderView::GetMultiVolumeData()
{
	if (m_glview)
		return m_glview->GetMultiVolumeData();
	else
		return 0;
}

VolumeData* VRenderView::GetVolumeData(wxString &name)
{
	if (m_glview)
		return m_glview->GetVolumeData(name);
	else
		return 0;
}

MeshData* VRenderView::GetMeshData(wxString &name)
{
	if (m_glview)
		return m_glview->GetMeshData(name);
	else
		return 0;
}

Annotations* VRenderView::GetAnnotations(wxString &name)
{
	if (m_glview)
		return m_glview->GetAnnotations(name);
	else
		return 0;
}

DataGroup* VRenderView::GetGroup(wxString &name)
{
	if (m_glview)
		return m_glview->GetGroup(name);
	else
		return 0;
}

DataGroup* VRenderView::GetParentGroup(wxString& name)
{
	if (m_glview)
		return m_glview->GetParentGroup(name);
	else
		return 0;
}

MeshGroup* VRenderView::GetParentMGroup(wxString& name)
{
    if (m_glview)
        return m_glview->GetParentMGroup(name);
    else
        return 0;
}

DataGroup* VRenderView::AddVolumeData(VolumeData* vd, wxString group_name)
{
	if (m_glview) {
		double val = 50.;
		//		m_scale_text->GetValue().ToDouble(&val);
		//		m_glview->SetScaleBarLen(val);
		return m_glview->AddVolumeData(vd, group_name);
	}
	else
		return 0;
}

void VRenderView::AddMeshData(MeshData* md)
{
	if (m_glview)
		m_glview->AddMeshData(md);
}

void VRenderView::AddAnnotations(Annotations* ann)
{
	if (m_glview)
		m_glview->AddAnnotations(ann);
}

wxString VRenderView::AddGroup(wxString str, wxString prev_group)
{
	if (m_glview)
		return m_glview->AddGroup(str);
	else
		return "";
}

wxString VRenderView::AddMGroup(wxString str)
{
	if (m_glview)
		return m_glview->AddMGroup(str);
	else
		return "";
}

MeshGroup* VRenderView::GetMGroup(wxString &str)
{
	if (m_glview)
		return m_glview->GetMGroup(str);
	else return 0;
}

void VRenderView::RemoveVolumeData(wxString &name)
{
	if (m_glview)
		m_glview->RemoveVolumeData(name);
}

void VRenderView::RemoveVolumeDataset(BaseReader *reader, int channel)
{
	if (m_glview)
		m_glview->RemoveVolumeDataset(reader, channel);
}

void VRenderView::RemoveMeshData(wxString &name)
{
	if (m_glview)
		m_glview->RemoveMeshData(name);
}

void VRenderView::RemoveAnnotations(wxString &name)
{
	if (m_glview)
		m_glview->RemoveAnnotations(name);
}

void VRenderView::RemoveGroup(wxString &name)
{
	if (m_glview)
		m_glview->RemoveGroup(name);
}

void VRenderView::Isolate(int type, wxString name)
{
	if (m_glview)
		m_glview->Isolate(type, name);
}

void VRenderView::ShowAll()
{
	if (m_glview)
		m_glview->ShowAll();
}

//move
void VRenderView::MoveLayerinView(wxString &src_name, wxString &dst_name)
{
	if (m_glview)
		m_glview->MoveLayerinView(src_name, dst_name);
}

//move volume
void VRenderView::MoveLayerinGroup(wxString &group_name, wxString &src_name, wxString &dst_name, int insert_mode)
{
	if (m_glview)
		m_glview->MoveLayerinGroup(group_name, src_name, dst_name, insert_mode);
}

void VRenderView::MoveLayertoView(wxString &group_name, wxString &src_name, wxString &dst_name)
{
	if (m_glview)
		m_glview->MoveLayertoView(group_name, src_name, dst_name);
}

void VRenderView::MoveLayertoGroup(wxString &group_name, wxString &src_name, wxString &dst_name)
{
	if (m_glview)
		m_glview->MoveLayertoGroup(group_name, src_name, dst_name);
}

void VRenderView::MoveLayerfromtoGroup(wxString &src_group_name,
									   wxString &dst_group_name,
									   wxString &src_name, wxString &dst_name,
									   int insert_mode)
{
	if (m_glview)
		m_glview->MoveLayerfromtoGroup(src_group_name,
		dst_group_name, src_name, dst_name, insert_mode);
}

//move mesh
void VRenderView::MoveMeshinGroup(wxString &group_name, wxString &src_name, wxString &dst_name, int insert_mode)
{
	if (m_glview)
		m_glview->MoveMeshinGroup(group_name, src_name, dst_name, insert_mode);
}

void VRenderView::MoveMeshtoView(wxString &group_name, wxString &src_name, wxString &dst_name)
{
	if (m_glview)
		m_glview->MoveMeshtoView(group_name, src_name, dst_name);
}

void VRenderView::MoveMeshtoGroup(wxString &group_name, wxString &src_name, wxString &dst_name)
{
	if (m_glview)
		m_glview->MoveMeshtoGroup(group_name, src_name, dst_name);
}

void VRenderView::MoveMeshfromtoGroup(wxString &src_group_name,
									  wxString &dst_group_name,
									  wxString &src_name, wxString &dst_name
									  , int insert_mode)
{
	if (m_glview)
		m_glview->MoveMeshfromtoGroup(src_group_name,
		dst_group_name, src_name, dst_name, insert_mode);
}

//reorganize layers in view
void VRenderView::OrganizeLayers()
{
	if (m_glview)
		m_glview->OrganizeLayers();
}

//randomize color
void VRenderView::RandomizeColor()
{
	if (m_glview)
		m_glview->RandomizeColor();
}

//toggle hiding/displaying
void VRenderView::SetDraw(bool draw)
{
	if (m_glview)
		m_glview->SetDraw(draw);
}

void VRenderView::ToggleDraw()
{
	if (m_glview)
		m_glview->ToggleDraw();
}

bool VRenderView::GetDraw()
{
	if (m_glview)
		return m_glview->GetDraw();
	else
		return false;
}

//camera operations
void VRenderView::GetTranslations(double &transx, double &transy, double &transz)
{
	if (m_glview)
		m_glview->GetTranslations(transx, transy, transz);
}

void VRenderView::SetTranslations(double transx, double transy, double transz)
{
	if (m_glview)
		m_glview->SetTranslations(transx, transy, transz);
}

void VRenderView::GetRotations(double &rotx, double &roty, double &rotz)
{
	if (m_glview)
		m_glview->GetRotations(rotx, roty, rotz);
}

void VRenderView::SetRotations(double rotx, double roty, double rotz, bool ui_update)
{
	if (m_glview)
		m_glview->SetRotations(360.0-rotx, roty, rotz, ui_update);

}

void VRenderView::GetCenters(double &ctrx, double &ctry, double &ctrz)
{
	if (m_glview)
		m_glview->GetCenters(ctrx, ctry, ctrz);
}

void VRenderView::SetCenters(double ctrx, double ctry, double ctrz)
{
	if (m_glview)
		m_glview->SetCenters(ctrx, ctry, ctrz);
}

double VRenderView::GetCenterEyeDist()
{
	if (m_glview)
		return m_glview->GetCenterEyeDist();
	else
		return 0.0;
}

void VRenderView::SetCenterEyeDist(double dist)
{
	if (m_glview)
		m_glview->SetCenterEyeDist(dist);
}

double VRenderView::GetRadius()
{
	if (m_glview)
		return m_glview->GetRadius();
	else
		return 0.0;
}

void VRenderView::SetRadius(double r)
{
	if (m_glview)
		m_glview->SetRadius(r);
}

//object operations
void VRenderView::GetObjCenters(double &ctrx, double &ctry, double &ctrz)
{
	if (m_glview)
		m_glview->GetObjCenters(ctrx, ctry, ctrz);
}

void VRenderView::SetObjCenters(double ctrx, double ctry, double ctrz)
{
	if (m_glview)
		m_glview->SetObjCenters(ctrx, ctry, ctrz);
}

void VRenderView::GetObjTrans(double &transx, double &transy, double &transz)
{
	if (m_glview)
		m_glview->GetObjTrans(transx, transy, transz);
}

void VRenderView::SetObjTrans(double transx, double transy, double transz)
{
	if (m_glview)
		m_glview->SetObjTrans(transx, transy, transz);
}

void VRenderView::GetObjRot(double &rotx, double &roty, double &rotz)
{
	if (m_glview)
		m_glview->GetObjRot(rotx, roty, rotz);
}

void VRenderView::SetObjRot(double rotx, double roty, double rotz)
{
	if (m_glview)
		m_glview->SetObjRot(rotx, roty, rotz);
}

//camera properties
double VRenderView::GetAov()
{
	if (m_glview)
		return m_glview->GetAov();
	else
		return 0.0;
}

void VRenderView::SetAov(double aov)
{
	if (m_glview)
		m_glview->SetAov(aov);
}

double VRenderView::GetMinPPI()
{
	if (m_glview)
		return m_glview->GetMinPPI();
	else
		return 20.0;
}
void VRenderView::SetMinPPI(double ppi)
{
	if (m_glview)
		m_glview->SetMinPPI(ppi);
}

double VRenderView::GetNearClip()
{
	if (m_glview)
		return m_glview->GetNearClip();
	else
		return 0.0;
}

void VRenderView::SetNearClip(double nc)
{
	if (m_glview)
		m_glview->SetNearClip(nc);
}

double VRenderView::GetFarClip()
{
	if (m_glview)
		return m_glview->GetFarClip();
	else
		return 0.0;
}

void VRenderView::SetFarClip(double fc)
{
	if (m_glview)
		m_glview->SetFarClip(fc);
}

//background color
Color VRenderView::GetBackgroundColor()
{
	if (m_glview)
		return m_glview->GetBackgroundColor();
	else
		return Color(0, 0, 0);
}

void VRenderView::SetBackgroundColor(Color &color)
{
	if (m_glview)
		m_glview->SetBackgroundColor(color);
	wxColor c(int(color.r()*255.0), int(color.g()*255.0), int(color.b()*255.0));
	m_bg_color_picker->SetColour(c);
}

void VRenderView::SetGradBg(bool val)
{
	if (m_glview)
		m_glview->SetGradBg(val);
}

//point volume mode
void VRenderView::SetPointVolumeMode(int mode)
{
	if (m_glview)
		m_glview->m_point_volume_mode = mode;
}

int VRenderView::GetPointVolumeMode()
{
	if (m_glview)
		return m_glview->m_point_volume_mode;
	else
		return 0;
}

//ruler uses trnasfer function
void VRenderView::SetRulerUseTransf(bool val)
{
	if (m_glview)
		m_glview->m_ruler_use_transf = val;
}

bool VRenderView::GetRulerUseTransf()
{
	if (m_glview)
		return m_glview->m_ruler_use_transf;
	else
		return false;
}

//ruler time dependent
void VRenderView::SetRulerTimeDep(bool val)
{
	if (m_glview)
		m_glview->m_ruler_time_dep = val;
}

bool VRenderView::GetRulerTimeDep()
{
	if (m_glview)
		return m_glview->m_ruler_time_dep;
	else
		return true;
}

//disply modes
int VRenderView::GetDrawType()
{
	if (m_glview)
		return m_glview->GetDrawType();
	else
		return 0;
}

void VRenderView::SetVolMethod(int method)
{
	if (m_glview)
		m_glview->SetVolMethod(method);
}

int VRenderView::GetVolMethod()
{
	if (m_glview)
		return m_glview->GetVolMethod();
	else
		return 0;
}

//other properties
void VRenderView::SetPeelingLayers(int n)
{
	if (m_glview)
		m_glview->SetPeelingLayers(n);
}

int VRenderView::GetPeelingLayers()
{
	if (m_glview)
		return m_glview->GetPeelingLayers();
	else
		return 0;
}

void VRenderView::SetBlendSlices(bool val)
{
	if (m_glview)
		m_glview->SetBlendSlices(val);
}

bool VRenderView::GetBlendSlices()
{
	if (m_glview)
		return m_glview->GetBlendSlices();
	else
		return false;
}

void VRenderView::SetAdaptive(bool val)
{
	if (m_glview)
		m_glview->SetAdaptive(val);
}

bool VRenderView::GetAdaptive()
{
	if (m_glview)
		return m_glview->GetAdaptive();
	else
		return false;
}

void VRenderView::SetFog(bool b)
{
	if (m_glview)
		m_glview->SetFog(b);
	if (m_depth_atten_chk)
		m_depth_atten_chk->SetValue(b);
}

bool VRenderView::GetFog()
{
	if (m_glview)
		return m_glview->GetFog();
	else
		return false;
}

void VRenderView::SetFogIntensity(double i)
{
	if (m_glview)
		m_glview->SetFogIntensity(i);
}

double VRenderView::GetFogIntensity()
{
	if (m_glview)
		return m_glview->GetFogIntensity();
	else
		return 0.0;
}

//reset counter
void VRenderView::ResetID()
{
	m_id = 1;
}

//get rendering context
std::shared_ptr<VVulkan> VRenderView::GetContext()
{
	if (m_glview)
		return m_glview->m_vulkan;
	else
		return nullptr;
}

void VRenderView::RefreshGL(bool interactive, bool start_loop)
{
	if (m_glview)
	{
		m_glview->m_force_clear = true;
		if (m_glview->m_adaptive) m_glview->m_interactive = interactive;
		if (m_glview->m_adaptive_res) m_glview->m_int_res = interactive;
		m_glview->RefreshGL(false, start_loop);
	}
}

void VRenderView::RefreshGLOverlays()
{
	if (m_glview)
	{
		m_glview->RefreshGLOverlays();
	}
}

void VRenderView::ToggleQualityDropDownBox(bool val)
{
    if (!val)
    {
        m_res_mode_combo->Hide();
        m_res_mode_text->Hide();
    }
    else
    {
        m_res_mode_combo->Show();
        m_res_mode_text->Show();
    }
}

//bar top
void VRenderView::OnVolumeMethodCheck(wxCommandEvent& event)
{
	//mode switch type
	//0 - didn't change
	//1 - to depth mode
	//2 - from depth mode
	int mode_switch_type = 0;
	int old_mode = GetVolMethod();

	int sender_id = event.GetId();
	switch (sender_id)
	{
	case ID_VolumeSeqRd:
		SetVolMethod(VOL_METHOD_SEQ);
		break;
	case ID_VolumeMultiRd:
		SetVolMethod(VOL_METHOD_MULTI);
		break;
	case ID_VolumeCompRd:
		SetVolMethod(VOL_METHOD_COMP);
		break;
	}

	int new_mode = GetVolMethod();

	if (new_mode == VOL_METHOD_MULTI &&
		(old_mode == VOL_METHOD_SEQ || old_mode == VOL_METHOD_COMP))
		mode_switch_type = 1;
	else if ((new_mode == VOL_METHOD_SEQ || new_mode == VOL_METHOD_COMP) &&
		old_mode == VOL_METHOD_MULTI)
		mode_switch_type = 2;

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		if (mode_switch_type == 1)
		{
			int cnt = 0;
			bool r = m_glview->GetSyncR();
			bool g = m_glview->GetSyncG();
			bool b = m_glview->GetSyncB();
			Color gamma = m_glview->GetGamma();
			Color brightness = m_glview->GetBrightness();
			Color hdr = m_glview->GetHdr();
            Color level = m_glview->GetLevels();
			for (int i=0; i<GetLayerNum(); i++)
			{
				TreeLayer* layer = GetLayer(i);
				if (layer && layer->IsA() == 5)
				{
					DataGroup* group = (DataGroup*)layer;

					if (group->GetVolumeNum() == 0)
						continue;

					r = r||group->GetSyncR();
					g = g||group->GetSyncG();
					b = b||group->GetSyncB();

					if (cnt == 0)
					{
						gamma = group->GetGamma();
						brightness = group->GetBrightness();
						hdr = group->GetHdr();
                        level = group->GetLevels();
					}
				}
				cnt++;
			}

			if (r && g && b)
			{
				gamma[1] = gamma[2] = gamma[0];
				brightness[1] = brightness[2] = brightness[0];
				hdr[1] = hdr[2] = hdr[0];
                level[1] = level[2] = level[0];
			}
			else
			{
				if (r && g)
				{
					gamma[1] = gamma[0];
					brightness[1] = brightness[0];
					hdr[1] = hdr[0];
                    level[1] = level[0];
				}
				else if (r & b)
				{
					gamma[2] = gamma[0];
					brightness[2] = brightness[0];
					hdr[2] = hdr[0];
                    level[2] = level[0];
				}
				else if (g & b)
				{
					gamma[2] = gamma[1];
					brightness[2] = brightness[1];
					hdr[2] = hdr[1];
                    level[2] = level[1];
				}
			}

			m_glview->SetGamma(gamma);
			m_glview->SetBrightness(brightness);
			m_glview->SetHdr(hdr);
            m_glview->SetLevels(level);
			m_glview->SetSyncR(r);
			m_glview->SetSyncG(g);
			m_glview->SetSyncB(b);

			//sync properties of the selcted volume
			VolumeData* svd = vr_frame->GetCurSelVol();
			if (!svd)
				svd = GetAllVolumeData(0);
			if (svd)
			{
				for (int i=0; i<GetAllVolumeNum(); i++)
				{
					VolumeData* vd = GetAllVolumeData(i);
					if (vd)
					{
						vd->SetNR(svd->GetNR());
						vd->SetSampleRate(svd->GetSampleRate());
						vd->SetShadow(svd->GetShadow());
						double sp;
						svd->GetShadowParams(sp);
						vd->SetShadowParams(sp);
					}
				}
			}
		}
		else if (mode_switch_type == 2)
		{
			/*			if (GetGroupNum() == 1)
			{
			for (int i=0; i<GetLayerNum(); i++)
			{
			TreeLayer* layer = GetLayer(i);
			if (layer && layer->IsA() == 5)
			{
			DataGroup* group = (DataGroup*)layer;
			FLIVR::Color col = m_glview->GetGamma();
			group->SetGammaAll(col);
			col = m_glview->GetBrightness();
			group->SetBrightnessAll(col);
			col = m_glview->GetHdr();
			group->SetHdrAll(col);
			group->SetSyncRAll(m_glview->GetSyncR());
			group->SetSyncGAll(m_glview->GetSyncG());
			group->SetSyncBAll(m_glview->GetSyncB());
			break;
			}
			}
			}
			*/		}

		vr_frame->GetTree()->UpdateSelection();
	}

	RefreshGL();
}

//ch1
void VRenderView::OnCh1Check(wxCommandEvent &event)
{
	wxCheckBox* ch1 = (wxCheckBox*)event.GetEventObject();
	if (ch1)
		VRenderFrame::SetCompression(ch1->GetValue());
}

//embde project
void VRenderView::OnChEmbedCheck(wxCommandEvent &event)
{
	wxCheckBox* ch_embed = (wxCheckBox*)event.GetEventObject();
	if (ch_embed)
		VRenderFrame::SetEmbedProject(ch_embed->GetValue());
}

wxWindow* VRenderView::CreateExtraCaptureControl(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent, 0, wxDefaultPosition, wxSize(400, 90));

	wxBoxSizer *group1 = new wxStaticBoxSizer(
		new wxStaticBox(panel, wxID_ANY, "Additional Options"), wxVERTICAL);

	//compressed
	wxCheckBox* ch1 = new wxCheckBox(panel, wxID_HIGHEST+3004,
		"Lempel-Ziv-Welch Compression");
	ch1->Connect(ch1->GetId(), wxEVT_COMMAND_CHECKBOX_CLICKED,
		wxCommandEventHandler(VRenderView::OnCh1Check), NULL, panel);
	if (ch1)
		ch1->SetValue(VRenderFrame::GetCompression());

	wxIntegerValidator<unsigned int> vald_int;

	wxBoxSizer* sizer1 = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText* st = new wxStaticText(panel, 0, "width:");
	m_cap_w_txt = new wxTextCtrl(panel, wxID_HIGHEST+3006,
		"", wxDefaultPosition, wxSize(50, 20), wxALIGN_RIGHT, vald_int);
	m_cap_w_txt->SetValue(wxString::Format(wxT("%i"),m_cap_resx));
	m_cap_w_txt->Connect(m_cap_w_txt->GetId(), wxEVT_COMMAND_TEXT_UPDATED,
		wxCommandEventHandler(VRenderView::OnCapWidthTextChange), NULL, panel);
	sizer1->Add(st);
	sizer1->Add(10, 10);
	sizer1->Add(m_cap_w_txt);
	sizer1->Add(20, 10);

	st = new wxStaticText(panel, 0, "height:");
	m_cap_h_txt = new wxTextCtrl(panel, wxID_HIGHEST+3007,
		"", wxDefaultPosition, wxSize(50, 20), wxALIGN_RIGHT, vald_int);
	m_cap_h_txt->SetValue(wxString::Format(wxT("%i"),m_cap_resy));
	m_cap_h_txt->Connect(m_cap_h_txt->GetId(), wxEVT_COMMAND_TEXT_UPDATED,
		wxCommandEventHandler(VRenderView::OnCapHeightTextChange), NULL, panel);
	sizer1->Add(st);
	sizer1->Add(10, 10);
	sizer1->Add(m_cap_h_txt);
	sizer1->Add(20, 10);
	wxButton* orgresbtn = new wxButton(panel, wxID_HIGHEST+3008, "1:1", wxDefaultPosition, wxSize(40,20));
	orgresbtn->Connect(orgresbtn->GetId(), wxEVT_COMMAND_BUTTON_CLICKED,
		wxCommandEventHandler(VRenderView::OnSetOrgResButton), NULL, panel);
	sizer1->Add(orgresbtn);
	sizer1->Add(5, 10);
	wxButton* dispresbtn = new wxButton(panel, wxID_HIGHEST+3009, "Disp", wxDefaultPosition, wxSize(40,20));
	dispresbtn->Connect(dispresbtn->GetId(), wxEVT_COMMAND_BUTTON_CLICKED,
		wxCommandEventHandler(VRenderView::OnSetDispResButton), NULL, panel);
	sizer1->Add(dispresbtn);

	//copy all files check box
	wxCheckBox* ch_embed = 0;
	if (VRenderFrame::GetSaveProject())
	{
		ch_embed = new wxCheckBox(panel, wxID_HIGHEST+3005,
			"Embed all files in the project folder");
		ch_embed->Connect(ch_embed->GetId(), wxEVT_COMMAND_CHECKBOX_CLICKED,
			wxCommandEventHandler(VRenderView::OnChEmbedCheck), NULL, panel);
		ch_embed->SetValue(VRenderFrame::GetEmbedProject());
	}

	//group
	group1->Add(10, 10);
	group1->Add(ch1);
	group1->Add(10, 10);
	group1->Add(sizer1);
	group1->Add(10, 10);
	if (VRenderFrame::GetSaveProject() &&
		ch_embed)
	{
		group1->Add(ch_embed);
		group1->Add(10, 10);
	}

	panel->SetSizer(group1);
	panel->Layout();

	return panel;
}

void VRenderView::OnCapWidthTextChange(wxCommandEvent &event)
{
	wxTextCtrl* txt1 = (wxTextCtrl*)event.GetEventObject();
	wxString num_text = txt1->GetValue();
	long w;
	num_text.ToLong(&w);
	m_cap_resx = w;
}

void VRenderView::OnCapHeightTextChange(wxCommandEvent &event)
{
	wxTextCtrl* txt1 = (wxTextCtrl*)event.GetEventObject();
	wxString num_text = txt1->GetValue();
	long h;
	num_text.ToLong(&h);
	m_cap_resy = h;
}

void VRenderView::OnSetOrgResButton(wxCommandEvent &event)
{
	if (m_cap_w_txt) m_cap_w_txt->SetValue(wxString::Format(wxT("%i"),m_cap_orgresx));
	if (m_cap_h_txt) m_cap_h_txt->SetValue(wxString::Format(wxT("%i"),m_cap_orgresy));
}

void VRenderView::OnSetDispResButton(wxCommandEvent &event)
{
	if (m_cap_w_txt) m_cap_w_txt->SetValue(wxString::Format(wxT("%i"),m_cap_dispresx));
	if (m_cap_h_txt) m_cap_h_txt->SetValue(wxString::Format(wxT("%i"),m_cap_dispresy));
}

void VRenderView::OnCapture(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

	if (vr_frame && vr_frame->GetSettingDlg())
		VRenderFrame::SetSaveProject(vr_frame->GetSettingDlg()->GetProjSave());

	m_cap_resx = m_glview->m_capture_resx;
	if (m_cap_resx <= 0) m_cap_resx = m_glview->GetSize().x;
	m_cap_resy = m_glview->m_capture_resy;
	if (m_cap_resy <= 0) m_cap_resy = m_glview->GetSize().y;
	
	m_glview->Get1x1DispSize(m_cap_orgresx, m_cap_orgresy);
	if (m_cap_orgresx <= 0) m_cap_orgresx = m_glview->GetSize().x;
	if (m_cap_orgresy <= 0) m_cap_orgresy = m_glview->GetSize().y;

	m_cap_dispresx = m_glview->GetSize().x;
	m_cap_dispresy = m_glview->GetSize().y;

	wxFileDialog file_dlg(m_frame, "Save captured image", "", "", "*.tif", wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
	file_dlg.SetExtraControlCreator(CreateExtraCaptureControl);
	int rval = file_dlg.ShowModal();
	if (rval == wxID_OK)
	{
		m_glview->m_cap_file = file_dlg.GetDirectory() + "/" + file_dlg.GetFilename();
		m_glview->m_capture = true;
		
		if (m_cap_resx > 4000 && m_cap_resy > 4000 && m_cap_resx != m_cap_dispresx && m_cap_resy != m_cap_dispresy)
		{
			int tilew = m_cap_resx / (m_cap_resx / 2000 + 1) + (m_cap_resx / 2000 + 1);
			int tileh = m_cap_resy / (m_cap_resy / 2000 + 1) + (m_cap_resy / 2000 + 1);
			m_glview->StartTileRendering(m_cap_resx, m_cap_resy, tilew, tileh);
		}
		else
        {
            m_glview->Resize();
		//	m_glview->StartTileRendering(m_cap_resx, m_cap_resy, m_cap_resx, m_cap_resy);
        }

		if (vr_frame && vr_frame->GetSettingDlg() &&
			vr_frame->GetSettingDlg()->GetProjSave())
		{
			wxString new_folder;
			new_folder = m_glview->m_cap_file + "_project";
			CREATE_DIR(new_folder.fn_str());
			wxString prop_file = new_folder + "/" + file_dlg.GetFilename() + "_project.vrp";
			vr_frame->SaveProject(prop_file);
		}
	}
}

void VRenderView::OnResModesCombo(wxCommandEvent &event)
{
	SetResMode(m_res_mode_combo->GetCurrentSelection());

	RefreshGL();
}

//bar left
void VRenderView::OnDepthAttenCheck(wxCommandEvent& event)
{
	if (m_depth_atten_chk->GetValue())
	{
		SetFog(true);
		m_depth_atten_factor_sldr->Enable();
		m_depth_atten_factor_text->Enable();
        m_depth_atten_reset_btn->Enable();
	}
	else
	{
		SetFog(false);
		m_depth_atten_factor_sldr->Disable();
		m_depth_atten_factor_text->Disable();
        m_depth_atten_reset_btn->Disable();
	}

	RefreshGL();
}

void VRenderView::OnDepthAttenFactorChange(wxScrollEvent& event)
{
	double atten_factor = m_depth_atten_factor_sldr->GetValue()/100.0;
	wxString str = wxString::Format("%.2f", atten_factor);
	m_depth_atten_factor_text->SetValue(str);
}

void VRenderView::OnDepthAttenFactorEdit(wxCommandEvent& event)
{
	wxString str = m_depth_atten_factor_text->GetValue();
	double val;
	str.ToDouble(&val);
	SetFogIntensity(val);
	m_depth_atten_factor_sldr->SetValue(int(val*100.0));
	RefreshGL(true);
}

void VRenderView::OnDepthAttenReset(wxCommandEvent &event)
{
	if (m_use_dft_settings)
	{
		wxString str = wxString::Format("%.2f", m_dft_depth_atten_factor);
		m_depth_atten_factor_text->SetValue(str);
	}
	else
	{
		wxString str = wxString::Format("%.2f", 0.0);
		m_depth_atten_factor_text->SetValue(str);
	}
}

//bar right
void VRenderView::OnCenter(wxCommandEvent &event)
{
	if (m_glview)
	{
		m_glview->SetCenter();
		RefreshGL();
	}
}

void VRenderView::OnScale121(wxCommandEvent &event)
{
	if (m_glview)
	{
		m_glview->SetScale121();
		RefreshGL();
		if (m_glview->m_mouse_focus)
			m_glview->SetFocus();
	}
}

void VRenderView::OnScaleFactorChange(wxScrollEvent& event)
{
	int scale_factor = m_scale_factor_sldr->GetValue();
	m_glview->m_scale_factor = scale_factor/100.0;
	wxString str = wxString::Format("%d", scale_factor);
	m_scale_factor_text->SetValue(str);
}

void VRenderView::OnScaleFactorEdit(wxCommandEvent& event)
{
	wxString str = m_scale_factor_text->GetValue();
	long val;
	str.ToLong(&val);
	if (val>0)
	{
		m_scale_factor_sldr->SetValue(val);
		m_glview->m_scale_factor = val/100.0;
		//m_glview->SetSortBricks();
		RefreshGL(true);
	}
}

void VRenderView::OnScaleFactorSpinUp(wxSpinEvent& event)
{
	wxString str_val = m_scale_factor_text->GetValue();
	long val;
	str_val.ToLong(&val);
	val++;
	str_val = wxString::Format("%ld", val);
	m_scale_factor_text->SetValue(str_val);
    //m_scale_factor_spin->SetValue(100);
}

void VRenderView::OnScaleFactorSpinDown(wxSpinEvent& event)
{
	wxString str_val = m_scale_factor_text->GetValue();
	long val;
	str_val.ToLong(&val);
	if (val > 1) val--;
	
	str_val = wxString::Format("%ld", val);
	m_scale_factor_text->SetValue(str_val);
    //m_scale_factor_spin->SetValue(100);
}

void VRenderView::OnScaleReset(wxCommandEvent &event)
{
	if (m_use_dft_settings)
	{
		wxString str = wxString::Format("%d", int(m_dft_scale_factor));
		m_scale_factor_text->SetValue(str);
	}
	else
		m_scale_factor_text->SetValue("100");
	if (m_glview && m_glview->m_mouse_focus)
		m_glview->SetFocus();
}

//bar bottom
void VRenderView::UpdateView(bool ui_update)
{
	double rotx, roty, rotz;
	wxString str_val = m_x_rot_text->GetValue();
	rotx = STOD(str_val.fn_str());
	str_val = m_y_rot_text->GetValue();
	roty = STOD(str_val.fn_str());
	str_val = m_z_rot_text->GetValue();
	rotz = STOD(str_val.fn_str());
	SetRotations(rotx, roty, rotz, ui_update);
	RefreshGL(true);
}

void VRenderView::OnValueEdit(wxCommandEvent& event)
{
	UpdateView(false);
}

void VRenderView::OnRotLink(wxCommandEvent& event)
{
	if (m_rot_link_chk->GetValue())
	{
		m_glview->m_linked_rot = true;
		double rotx, roty, rotz;
		m_glview->GetRotations(rotx, roty, rotz);
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			for (int i=0; i<vr_frame->GetViewNum(); i++)
			{
				VRenderView* view = vr_frame->GetView(i);
				if (view)
				{
					view->m_glview->m_linked_rot = true;
					view->m_rot_link_chk->SetValue(true);
					view->m_glview->SetRotations(rotx, roty, rotz);
				}
			}
		}
	}
	else
		m_glview->m_linked_rot = false;
}

void VRenderView::OnRotLink(bool b)
{
	m_glview->m_linked_rot = true;
	double rotx, roty, rotz;
	m_glview->GetRotations(rotx, roty, rotz);
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		for (int i=0; i<vr_frame->GetViewNum(); i++)
		{
			VRenderView* view = vr_frame->GetView(i);
			if (view)
			{
				view->m_glview->m_linked_rot = b;
				view->m_glview->SetRotations(rotx, roty, rotz);
				view->m_glview->RefreshGL();
			}
		}
	}
}

void VRenderView::OnRotReset(wxCommandEvent &event)
{
	m_x_rot_sldr->SetValue(0);
	m_y_rot_sldr->SetValue(0);
	m_z_rot_sldr->SetValue(0);
	m_x_rot_text->ChangeValue("0.0");
	m_y_rot_text->ChangeValue("0.0");
	m_z_rot_text->ChangeValue("0.0");
	SetRotations(0.0, 0.0, 0.0);
    m_glview->InitView(INIT_BOUNDS|INIT_CENTER|INIT_ROTATE);
    //m_glview->SetCenter();
	RefreshGL();
	if (m_glview->m_mouse_focus)
		m_glview->SetFocus();
}

void VRenderView::OnXRotScroll(wxScrollEvent& event)
{
	int val = event.GetPosition();
	double dval = m_glview->GetRotLock()?val*45.0:val;
	wxString str = wxString::Format("%.1f", dval);
	m_x_rot_text->SetValue(str);
}

void VRenderView::OnYRotScroll(wxScrollEvent& event)
{
	int val = event.GetPosition();
	double dval = m_glview->GetRotLock()?val*45.0:val;
	wxString str = wxString::Format("%.1f", dval);
	m_y_rot_text->SetValue(str);
}

void VRenderView::OnZRotScroll(wxScrollEvent& event)
{
	int val = event.GetPosition();
	double dval = m_glview->GetRotLock()?val*45.0:val;
	wxString str = wxString::Format("%.1f", dval);
	m_z_rot_text->SetValue(str);
}

void VRenderView::OnRotLockCheck(wxCommandEvent& event)
{
	if (m_rot_lock_chk->GetValue())
	{
		m_glview->SetRotLock(true);
		//sliders
		m_x_rot_sldr->SetRange(0, 8);
		m_y_rot_sldr->SetRange(0, 8);
		m_z_rot_sldr->SetRange(0, 8);
		double rotx, roty, rotz;
		m_glview->GetRotations(rotx, roty, rotz);
		m_x_rot_sldr->SetValue(int(rotx/45.0));
		m_y_rot_sldr->SetValue(int(roty/45.0));
		m_z_rot_sldr->SetValue(int(rotz/45.0));
	}
	else
	{
		m_glview->SetRotLock(false);
		//sliders
		m_x_rot_sldr->SetRange(0, 360);
		m_y_rot_sldr->SetRange(0, 360);
		m_z_rot_sldr->SetRange(0, 360);
		double rotx, roty, rotz;
		m_glview->GetRotations(rotx, roty, rotz);
		m_x_rot_sldr->SetValue(int(rotx));
		m_y_rot_sldr->SetValue(int(roty));
		m_z_rot_sldr->SetValue(int(rotz));
	}
}

void VRenderView::OnXRotSpinDown(wxSpinEvent& event)
{
	wxString str_val = m_x_rot_text->GetValue();
	int value = STOI(str_val.fn_str())+1;
	value = value>360?value-360:value;
	wxString str = wxString::Format("%.1f", double(value));
	m_x_rot_text->SetValue(str);
    //m_x_rot_spin->SetValue(180);
}

void VRenderView::OnXRotSpinUp(wxSpinEvent& event)
{
	wxString str_val = m_x_rot_text->GetValue();
	int value = STOI(str_val.fn_str())-1;
	value = value<0?value+360:value;
	wxString str = wxString::Format("%.1f", double(value));
	m_x_rot_text->SetValue(str);
    //m_x_rot_spin->SetValue(180);
}

void VRenderView::OnYRotSpinDown(wxSpinEvent& event)
{
	wxString str_val = m_y_rot_text->GetValue();
	int value = STOI(str_val.fn_str())+1;
	value = value>360?value-360:value;
	wxString str = wxString::Format("%.1f", double(value));
	m_y_rot_text->SetValue(str);
    //m_y_rot_spin->SetValue(180);
}

void VRenderView::OnYRotSpinUp(wxSpinEvent& event)
{
	wxString str_val = m_y_rot_text->GetValue();
	int value = STOI(str_val.fn_str())-1;
	value = value<0?value+360:value;
	wxString str = wxString::Format("%.1f", double(value));
	m_y_rot_text->SetValue(str);
    //m_y_rot_spin->SetValue(180);
}

void VRenderView::OnZRotSpinDown(wxSpinEvent& event)
{
	wxString str_val = m_z_rot_text->GetValue();
	int value = STOI(str_val.fn_str())+1;
	value = value>360?value-360:value;
	wxString str = wxString::Format("%.1f", double(value));
	m_z_rot_text->SetValue(str);
    //m_z_rot_spin->SetValue(180);
}

void VRenderView::OnZRotSpinUp(wxSpinEvent& event)
{
	wxString str_val = m_z_rot_text->GetValue();
	int value = STOI(str_val.fn_str())-1;
	value = value<0?value+360:value;
	wxString str = wxString::Format("%.1f", double(value));
	m_z_rot_text->SetValue(str);
    //m_z_rot_spin->SetValue(180);
}

//top
void VRenderView::OnBgColorChange(wxColourPickerEvent& event)
{
	wxColor c = event.GetColour();
	Color color(c.Red()/255.0, c.Green()/255.0, c.Blue()/255.0);
	SetBackgroundColor(color);
	RefreshGL();
}

void VRenderView::OnCamCtrCheck(wxCommandEvent& event)
{
	m_glview->m_draw_camctr = m_cam_ctr_chk->GetValue();
	RefreshGL();
}

void VRenderView::OnFpsCheck(wxCommandEvent& event)
{
	m_glview->m_draw_info = m_fps_chk->GetValue();
	//m_glview->m_draw_coord = m_glview->m_draw_info;
	RefreshGL();
}

void VRenderView::OnLegendCheck(wxCommandEvent& event)
{
    if (!m_legend_chk->GetValue() && m_legend_list)
    {
        delete m_legend_list;
        m_legend_list = NULL;
    }
    
    if (m_legend_chk->GetValue())
        m_legend_btn->Enable();
    else
    {
        m_legend_btn->Disable();
        if (m_glview && m_frame)
        {
            VRenderFrame* vframe = (VRenderFrame*) m_frame;
            LegendPanel* lpanel = vframe->GetLegendPanel();
            if (lpanel)
            {
                if (lpanel->GetView() == this && vframe->IsShownLegendPanel())
                    vframe->HideLegendPanel();
            }
        }
    }
    
	m_glview->m_draw_legend = m_legend_chk->GetValue();
	RefreshGL();
}

void VRenderView::OnIntpCheck(wxCommandEvent& event)
{
	m_glview->SetIntp(m_intp_chk->GetValue());
	RefreshGL();
}

void VRenderView::OnSearchCheck(wxCommandEvent& event)
{
	m_glview->SetSearcherVisibility(m_search_chk->GetValue());
}

void VRenderView::OnAovSldrIdle(wxTimerEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;
	ClippingView *cp_view = vr_frame->GetClippingView();
	if (!cp_view) return;

	if (cp_view->GetHoldPlanes())
		return;
	if (m_glview->m_capture)
		return;

	wxPoint pos = wxGetMousePosition();
	wxRect reg = m_aov_sldr->GetScreenRect();
	wxWindow *window = wxWindow::FindFocus();

	int plane_mode = cp_view->GetPlaneMode();

	if (reg.Contains(pos))
	{
		if (!m_draw_clip)
		{
			m_glview->m_draw_clip = true;
			m_glview->m_clip_mask = -1;
			if (plane_mode == PM_LOWTRANSBACK || plane_mode == PM_NORMALBACK)
				RefreshGL();
			else 
				RefreshGLOverlays();
			m_draw_clip = true;
		}
	}
	else
	{
		if (m_draw_clip)
		{
			m_glview->m_draw_clip = false;
			if (plane_mode == PM_LOWTRANSBACK || plane_mode == PM_NORMALBACK)
				RefreshGL();
			else 
				RefreshGLOverlays();
			m_draw_clip = false;
		}
	}
}

void VRenderView::OnAovChange(wxScrollEvent& event)
{
	int val = m_aov_sldr->GetValue();
	m_aov_text->SetValue(wxString::Format("%d", val));
}

void VRenderView::OnAovText(wxCommandEvent& event)
{
	wxString str = m_aov_text->GetValue();
	if (str == "Ortho")
	{
		SetPersp(false);
		m_aov_sldr->SetValue(10);
		RefreshGL(true);
		return;
	}
	long val;
	if (!str.ToLong(&val))
		return;
	if (val ==0 || val == 10)
	{
		SetPersp(false);
		m_aov_text->ChangeValue("Ortho");
		m_aov_sldr->SetValue(10);
	}
	else if (val < 10)
	{
		return;
	}
	else
	{
		if (val > 100)
		{
			val = 100;
			m_aov_text->ChangeValue("100");
			m_aov_sldr->SetValue(100);
		}
		SetPersp(true);
		SetAov(val);
		m_aov_sldr->SetValue(val);
	}
	RefreshGL(true);
}

void VRenderView::OnFreeChk(wxCommandEvent& event)
{
	if (m_free_chk->GetValue())
		SetFree(true);
	else
	{
		SetFree(false);
		int val = m_aov_sldr->GetValue();
		if (val == 10)
			SetPersp(false);
		else
			SetPersp(true);
	}
	RefreshGL();
}

void VRenderView::OnPPIChange(wxScrollEvent& event)
{
	int val = m_ppi_sldr->GetValue();
	m_ppi_text->SetValue(wxString::Format("%d", val));
}

void VRenderView::OnPPIEdit(wxCommandEvent &event)
{
	wxString str = m_ppi_text->GetValue();
	long val;
	str.ToLong(&val);
	if (val>0)
	{
		m_ppi_sldr->SetValue(val);
		m_glview->m_min_ppi = val;
		RefreshGL(true);
	}
}

void VRenderView::OnLegendButton(wxCommandEvent &event)
{
    if (m_glview && m_frame)
    {
        VRenderFrame* vframe = (VRenderFrame*) m_frame;
        LegendPanel* lpanel = vframe->GetLegendPanel();
        
        if (lpanel)
        {
            if (lpanel->GetView() == this && vframe->IsShownLegendPanel())
                vframe->HideLegendPanel();
            else
            {
                wxDisplay display(wxDisplay::GetFromWindow(m_legend_btn));
                wxRect screen = display.GetClientArea();
                
                lpanel->SetViewAndReload(this);
                wxSize panel_size = lpanel->GetListSize();
                
                if (panel_size.y > 0)
                {
                    panel_size.x += 20;
                    panel_size.y += 20;
#ifdef _WIN32
                    panel_size.y += 25;
#endif
                    wxPoint pos = m_legend_btn->GetPosition();
                    pos.y += 20;
                    pos = ClientToScreen(pos);
                    if (panel_size.x > screen.GetWidth())
                        panel_size.x = screen.GetWidth();
                    if (pos.x + panel_size.x > screen.GetWidth() )
                        pos.x = screen.GetWidth() - panel_size.x;
                    if (pos.y + panel_size.y > screen.GetHeight() )
                        panel_size.y = screen.GetHeight() - pos.y;
                    vframe->ShowLegendPanel(this, pos, panel_size);
                }
            }
        }
    }
}

void VRenderView::OnLegendList(wxCommandEvent &event)
{
    
}

void VRenderView::SaveDefault(unsigned int mask)
{
	wxFileConfig fconfig("FluoRender default view settings");
	wxString str;

	//render modes
	bool bVal;
	bVal = m_volume_seq_rd->GetValue();
	fconfig.Write("volume_seq_rd", bVal);
	bVal = m_volume_multi_rd->GetValue();
	fconfig.Write("volume_multi_rd", bVal);
	bVal = m_volume_comp_rd->GetValue();
	fconfig.Write("volume_comp_rd", bVal);
	//background color
	wxColor cVal;
	cVal = m_bg_color_picker->GetColour();
	str = wxString::Format("%d %d %d", cVal.Red(), cVal.Green(), cVal.Blue());
	fconfig.Write("bg_color_picker", str);
	//camera center
	bVal = m_cam_ctr_chk->GetValue();
	fconfig.Write("cam_ctr_chk", bVal);
	//camctr size
	fconfig.Write("camctr_size", m_glview->m_camctr_size);
	//fps
	bVal = m_fps_chk->GetValue();
	fconfig.Write("fps_chk", bVal);
	//interpolation
	bVal = m_intp_chk->GetValue();
	fconfig.Write("intp_chk", bVal);
	//selection
	bVal = m_glview->m_draw_legend;
	fconfig.Write("legend_chk", bVal);
	//mouse focus
	bVal = m_glview->m_mouse_focus;
	fconfig.Write("mouse_focus", bVal);
	//search
	bVal = m_search_chk->GetValue();
	fconfig.Write("search_chk", bVal);
	//ortho/persp
	fconfig.Write("persp", m_glview->m_persp);
	fconfig.Write("aov", m_glview->m_aov);
	bVal = m_free_chk->GetValue();
	fconfig.Write("free_rd", bVal);
	//min dpi
	fconfig.Write("min_dpi", m_glview->m_min_ppi);
	fconfig.Write("res_mode", m_glview->m_res_mode);
	//rotations
	str = m_x_rot_text->GetValue();
	fconfig.Write("x_rot", str);
	str = m_y_rot_text->GetValue();
	fconfig.Write("y_rot", str);
	str = m_z_rot_text->GetValue();
	fconfig.Write("z_rot", str);
	fconfig.Write("rot_lock", m_glview->GetRotLock());
	//depth atten
	bVal = m_depth_atten_chk->GetValue();
	fconfig.Write("depth_atten_chk", bVal);
	str = m_depth_atten_factor_text->GetValue();
	fconfig.Write("depth_atten_factor_text", str);
	str.ToDouble(&m_dft_depth_atten_factor);
	//scale factor
	str = m_scale_factor_text->GetValue();
	fconfig.Write("scale_factor_text", str);
	str.ToDouble(&m_dft_scale_factor);
	//camera center
	double x, y, z;
	GetCenters(x, y, z);
	str = wxString::Format("%f %f %f", x, y, z);
	fconfig.Write("center", str);

	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(),NULL);
#ifdef _DARWIN
	wxString dft = expath + "/../Resources/default_view_settings.dft";
#else
	wxString dft = expath + GETSLASHS() + "default_view_settings.dft";
	wxString dft2 = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "default_view_settings.dft";
	if (!wxFileExists(dft) && wxFileExists(dft2))
		dft = dft2;
#endif
	wxFileOutputStream os(dft);
	fconfig.Save(os);

	m_default_saved = true;
}

void VRenderView::OnSaveDefault(wxCommandEvent &event)
{
	SaveDefault();
}

void VRenderView::LoadSettings()
{
	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(),NULL);
#ifdef _DARWIN
	wxString dft = expath + "/../Resources/default_view_settings.dft";
#else
	wxString dft = expath + GETSLASHS() + "default_view_settings.dft";
	if (!wxFileExists(dft))
		dft = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "default_view_settings.dft";
#endif

	wxFileInputStream is(dft);

	if (!is.IsOk()) {
		UpdateView();
		return;
	}

	wxFileConfig fconfig(is);

	bool bVal;
	double dVal;
	int iVal;
	if (fconfig.Read("volume_seq_rd", &bVal))
	{
		m_volume_seq_rd->SetValue(bVal);
		if (bVal)
			SetVolMethod(VOL_METHOD_SEQ);
	}
	if (fconfig.Read("volume_multi_rd", &bVal))
	{
		m_volume_multi_rd->SetValue(bVal);
		if (bVal)
			SetVolMethod(VOL_METHOD_MULTI);
	}
	if (fconfig.Read("volume_comp_rd", &bVal))
	{
		m_volume_comp_rd->SetValue(bVal);
		if (bVal)
			SetVolMethod(VOL_METHOD_COMP);
	}
	wxString str;
	if (fconfig.Read("bg_color_picker", &str))
	{
		int r, g, b;
		SSCANF(str.c_str(), "%d%d%d", &r, &g, &b);
		wxColor cVal(r, g, b);
		m_bg_color_picker->SetColour(cVal);
		Color c(r/255.0, g/255.0, b/255.0);
		SetBackgroundColor(c);
	}
	if (fconfig.Read("cam_ctr_chk", &bVal))
	{
		m_cam_ctr_chk->SetValue(bVal);
		m_glview->m_draw_camctr = bVal;
	}
	if (fconfig.Read("camctr_size", &dVal))
	{
		m_glview->m_camctr_size = dVal;
	}
	if (fconfig.Read("fps_chk", &bVal))
	{
		m_fps_chk->SetValue(bVal);
		m_glview->m_draw_info = bVal;
		m_glview->m_draw_coord = bVal;
	}
	if (fconfig.Read("intp_chk", &bVal))
	{
		m_intp_chk->SetValue(bVal);
		m_glview->SetIntp(bVal);
	}
	if (fconfig.Read("legend_chk", &bVal))
	{
		m_legend_chk->SetValue(bVal);
		m_glview->m_draw_legend = true;
	}
	if (fconfig.Read("search_chk", &bVal))
	{
		//m_search_chk->SetValue(bVal);
		//if (bVal) m_glview->m_searcher->Show();
		//else m_glview->m_searcher->Hide();
	}
	if (fconfig.Read("mouse_focus", &bVal))
	{
		m_glview->m_mouse_focus = bVal;
	}
	if (fconfig.Read("persp", &bVal))
	{
		if (bVal)
			SetPersp(true);
		else
			SetPersp(false);
	}
	if (fconfig.Read("aov", &dVal))
	{
		SetAov(dVal);
	}
	if (fconfig.Read("min_dpi", &dVal))
	{
		SetMinPPI(dVal);
	}
	if (fconfig.Read("res_mode", &iVal))
	{
		SetResMode(iVal);
	}
	else
		SetResMode(0);
	if (fconfig.Read("free_rd", &bVal))
	{
		m_free_chk->SetValue(bVal);
		if (bVal)
			SetFree(true);
	}
	if (fconfig.Read("x_rot", &str))
	{
		m_x_rot_text->ChangeValue(str);
		str.ToDouble(&m_dft_x_rot);
	}
	if (fconfig.Read("y_rot", &str))
	{
		m_y_rot_text->ChangeValue(str);
		str.ToDouble(&m_dft_y_rot);
	}
	if (fconfig.Read("z_rot", &str))
	{
		m_z_rot_text->ChangeValue(str);
		str.ToDouble(&m_dft_z_rot);
	}
	if (fconfig.Read("rot_lock", &bVal))
	{
		m_rot_lock_chk->SetValue(bVal);
		m_glview->SetRotLock(bVal);
	}
	UpdateView();  //for rotations
	if (fconfig.Read("scale_factor_text", &str))
	{
		m_scale_factor_text->ChangeValue(str);
		str.ToDouble(&dVal);
		if (dVal <= 1.0)
			dVal = 100.0;
		m_scale_factor_sldr->SetValue(dVal);
		m_glview->m_scale_factor = dVal/100.0;
		m_dft_scale_factor = dVal;
	}
	if (fconfig.Read("depth_atten_chk", &bVal))
	{
		m_depth_atten_chk->SetValue(bVal);
		SetFog(bVal);
		if (bVal)
		{
			m_depth_atten_factor_sldr->Enable();
			m_depth_atten_factor_text->Enable();
            m_depth_atten_reset_btn->Enable();
		}
		else
		{
			m_depth_atten_factor_sldr->Disable();
			m_depth_atten_factor_text->Disable();
            m_depth_atten_reset_btn->Disable();
		}
	}
	if (fconfig.Read("depth_atten_factor_text", &str))
	{
		m_depth_atten_factor_text->ChangeValue(str);
		str.ToDouble(&dVal);
		m_depth_atten_factor_sldr->SetValue(int(dVal*100));
		SetFogIntensity(dVal);
		m_dft_depth_atten_factor = dVal;
	}
	if (fconfig.Read("center", &str))
	{
		float x, y, z;
		SSCANF(str.c_str(), "%f%f%f", &x, &y, &z);
		SetCenters(x, y, z);
	}

	m_use_dft_settings = true;
	RefreshGL();
}

void VRenderView::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

