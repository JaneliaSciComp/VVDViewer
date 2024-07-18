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
#include "DLLExport.h"
#include "DataManager.h"
#include "utility.h"
#include "VolumeSelector.h"
#include "VolumeCalculator.h"
#include "Animator/Interpolator.h"
#include "TextRenderer.h"
#include "KernelExecutor.h"

#include "FLIVR/Color.h"
#include "FLIVR/ShaderProgram.h"
#include "FLIVR/BBox.h"
#include "FLIVR/MultiVolumeRenderer.h"
#include "FLIVR/Quaternion.h"
#include "FLIVR/ImgShader.h"
#include "FLIVR/PaintShader.h"
#include "compatibility.h"

#include <wx/wx.h>
#include <wx/clrpicker.h>
#include <wx/spinbutt.h>
#include <wx/event.h>
#include <wx/srchctrl.h>
#include <wx/thread.h>
#include <wx/checklst.h>

#include <vector>
#include <stdarg.h>
#include <unordered_map>
#include <memory>
#include "NV/Timer.h"

#include <glm/glm.hpp>

#include "VVulkan.h"

#ifndef _VRENDERVIEW_H_
#define _VRENDERVIEW_H_

#define VOL_METHOD_SEQ    1
#define VOL_METHOD_MULTI  2
#define VOL_METHOD_COMP    3

#define INIT_BOUNDS  1
#define INIT_CENTER  2
#define INIT_TRANSL  4
#define INIT_ROTATE  8
#define INIT_OBJ_TRANSL  16

//clipping plane mask
#define CLIP_X1  1
#define CLIP_X2  2
#define CLIP_Y1  4
#define CLIP_Y2  8
#define CLIP_Z1  16
#define CLIP_Z2  32
//clipping plane winding
#define CULL_OFF  0
#define FRONT_FACE  1
#define BACK_FACE  2

#define LAYER_DATAGROUP 5
#define LAYER_MESHGROUP 6 

using namespace std;

class VRenderView;
class VRenderVulkanView;
class LMSeacher;

//tree item data
class EXPORT_API LMItemInfo : public wxTreeItemData
{
public:
	LMItemInfo()
	{
		type = -1;
	}
	LMItemInfo(const LMItemInfo &obj)
	{
		type = obj.type;
		p = obj.p;
	}
	int type;	//0-root; 1-data;
	Point p;
};

class EXPORT_API LMTreeCtrl : public wxTreeCtrl
{
	enum
	{
		ID_Expand = wxID_HIGHEST+2101
	};

public:
	LMTreeCtrl(VRenderVulkanView *glview,
		wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style=wxTR_HAS_BUTTONS|
		wxTR_TWIST_BUTTONS|
		wxTR_LINES_AT_ROOT|
		wxTR_NO_LINES|
		wxTR_FULL_ROW_HIGHLIGHT|
		wxTR_HIDE_ROOT|
		wxTR_SINGLE);
	~LMTreeCtrl();

	void Append(wxString name, Point p);

	void ShowOnlyMachedItems(wxString str);

	void SetSearcher(LMSeacher *searcher) { m_schtxtctrl = searcher; }

	void CopyToSchTextCtrl();

	void StartManip(wxString str);

private:
	VRenderVulkanView* m_glview;

	LMSeacher *m_schtxtctrl;

private:
	void OnSetFocus(wxFocusEvent& event);
	void OnAct(wxTreeEvent &event);
	void OnExpand(wxCommandEvent &event);
	void OnSelectChanged(wxTreeEvent &event);

	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);

	void OnMouse(wxMouseEvent& event);

	DECLARE_EVENT_TABLE()
protected: //Possible TODO
	wxSize GetSizeAvailableForScrollTarget(const wxSize& size) {
		return size - GetEffectiveMinSize();
	}
};


class EXPORT_API LMSeacher : public wxTextCtrl
{
public:
	LMSeacher(VRenderVulkanView* glview,
		wxWindow* parent,
		wxWindowID id,
		const wxString& text = wxT(""),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style=wxTE_PROCESS_ENTER);
	~LMSeacher();

	void KillFocus();

private:
	VRenderVulkanView *m_glview;
	wxButton *m_dummy;

	LMTreeCtrl *m_list;

private:
	void OnSetChildFocus(wxChildFocusEvent& event);
	void OnSetFocus(wxFocusEvent& event);
	void OnKillFocus(wxFocusEvent& event);

	void OnSearchText(wxCommandEvent& event);
	void OnEnterInSearcher(wxCommandEvent& event);

	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);

	void OnIdle(wxIdleEvent &event);

	void OnMouse(wxMouseEvent& event);

	DECLARE_EVENT_TABLE()
};

class EXPORT_API VRenderVulkanView: public wxWindow
{
	enum
	{
		ID_Searcher = wxID_HIGHEST+6001,
		ID_Timer,
		ID_CTXMENU_SHOW_ALL,
		ID_CTXMENU_HIDE_OTHER_VOLS,
		ID_CTXMENU_HIDE_THIS_VOL,
		ID_CTXMENU_SHOW_ALL_FRAGMENTS,
        ID_CTXMENU_DESELECT_ALL_FRAGMENTS,
		ID_CTXMENU_HIDE_OTHER_FRAGMENTS,
		ID_CTXMENU_HIDE_SELECTED_FLAGMENTS,
		ID_CTXMENU_UNDO_VISIBILITY_SETTING_CHANGES,
		ID_CTXMENU_REDO_VISIBILITY_SETTING_CHANGES,
		ID_CTXMENU_JOIN_SEGMENTS,
		ID_CTXMENU_SPLIT_SEGMENTS,
	};

public:
	VRenderVulkanView(wxWindow* frame,
		wxWindow *parent,
        wxWindowID id,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = 0,
        const wxString& name = "VulkanCanvasName");
	~VRenderVulkanView();

	double GetAvailableGraphicsMemory(int device_id);

	//initialization
	void Init();

	void InitVulkan();

	//Clear all layers
	void Clear();

	//recalculate view according to object bounds
	void InitView(unsigned int type=INIT_BOUNDS);

	//data management
	int GetAny();
	int GetDispVolumeNum();
	int GetAllVolumeNum();
	int GetMeshNum();
    int GetAllMeshNum();
	int GetGroupNum();
	int GetLayerNum();
	VolumeData* GetAllVolumeData(int index);
	VolumeData* GetDispVolumeData(int index);
    MeshData* GetAllMeshData(int index);
	MeshData* GetMeshData(int index);
	TreeLayer* GetLayer(int index);
	MultiVolumeRenderer* GetMultiVolumeData() {return m_mvr;};
	VolumeData* GetVolumeData(wxString &name);
	MeshData* GetMeshData(wxString &name);
	Annotations* GetAnnotations(wxString &name);
	DataGroup* GetGroup(wxString &name);
	DataGroup* GetParentGroup(wxString& name);
	MeshGroup* GetMGroup(wxString str);
    MeshGroup* GetParentMGroup(wxString& name);
	//add
	DataGroup* AddVolumeData(VolumeData* vd, wxString group_name="");
	void AddMeshData(MeshData* md);
	void AddAnnotations(Annotations* ann);
	wxString AddGroup(wxString str, wxString prev_group="");
	wxString CheckNewGroupName(const wxString &name, int type);
	bool CheckGroupNames(const wxString &name, int type);
	wxString AddMGroup(wxString str);
	//remove
	void RemoveVolumeData(wxString &name);
	void RemoveVolumeDataset(BaseReader *reader, int channel);
	void ReplaceVolumeData(wxString &name, VolumeData *dst);
	void RemoveMeshData(wxString &name);
	void RemoveAnnotations(wxString &name);
	void RemoveGroup(wxString &name);
	//isolate
	void Isolate(int type, wxString name);
	void ShowAll();
	//move
	void MoveLayerinView(wxString &src_name, wxString &dst_name);
	//move volume
	void MoveLayerinGroup(wxString &group_name, wxString &src_name, wxString &dst_name, int insert_mode=0);
	void MoveLayertoView(wxString &group_name, wxString &src_name, wxString &dst_name);
	void MoveLayertoGroup(wxString &group_name, wxString &src_name, wxString &dst_name);
	void MoveLayerfromtoGroup(wxString &src_group_name, wxString &dst_group_name, wxString &src_name, wxString &dst_name, int insert_mode=0);
	//move mesh
	void MoveMeshinGroup(wxString &group_name, wxString &src_name, wxString &dst_name, int insert_mode=0);
	void MoveMeshtoView(wxString &group_name, wxString &src_name, wxString &dst_name);
	void MoveMeshtoGroup(wxString &group_name, wxString &src_name, wxString &dst_name);
	void MoveMeshfromtoGroup(wxString &src_group_name, wxString &dst_group_name, wxString &src_name, wxString &dst_name, int insert_mode=0);
	//
	void PopVolumeList();
	void PopMeshList();
	//reorganize layers in view
	void OrganizeLayers();
	//randomize color
	void RandomizeColor();

	//trim volume data with meshes
	//void TrimData(int type=SEG_MTHD_BOTH);

	//toggle hiding/displaying
	void SetDraw(bool draw);
	void ToggleDraw();
	bool GetDraw();

	//camera operations
	void GetTranslations(double &transx, double &transy, double &transz)
	{transx = m_transx; transy = m_transy; transz = m_transz;}
	void SetTranslations(double transx, double transy, double transz)
	{
		m_transx = transx; m_transy = transy; m_transz = transz;
		m_distance = sqrt(m_transx*m_transx + m_transy*m_transy + m_transz*m_transz);
	}
	void GetRotations(double &rotx, double &roty, double &rotz)
	{
		rotx = 360.0 - m_rotx;
		roty = m_roty;
		rotz = m_rotz;
	}
	void SetRotations(double rotx, double roty, double rotz, bool ui_update=true, bool link_update=true);
	void GetCenters(double &ctrx, double &ctry, double &ctrz)
	{
		ctrx = m_ctrx; ctry = m_ctry; ctrz = m_ctrz;
	}
	void SetCenters(double ctrx, double ctry, double ctrz)
	{
		m_ctrx = ctrx; m_ctry = ctry; m_ctrz = ctrz;
	}
	double GetCenterEyeDist()
	{
		return m_distance;
	}
	void SetCenterEyeDist(double dist)
	{
		m_distance = dist;
	}
	double GetInitDist()
	{
		return m_init_dist;
	}
	void SetInitDist(double dist)
	{
		m_init_dist = dist;
	}
	double GetRadius()
	{
		return m_radius;
	}
	void SetRadius(double r);
	void SetCenter();
	void SetScale121();

	//object operations
	void GetObjCenters(double &ctrx, double &ctry, double &ctrz)
	{
		ctrx = m_obj_ctrx;
		ctry = m_obj_ctry;
		ctrz = m_obj_ctrz;
	}
	void SetObjCenters(double ctrx, double ctry, double ctrz)
	{
		m_obj_ctrx = ctrx;
		m_obj_ctry = ctry;
		m_obj_ctrz = ctrz;
	}
	void GetObjTrans(double &transx, double &transy, double &transz)
	{
		transx = m_obj_transx;
		transy = m_obj_transy;
		transz = m_obj_transz;
	}
	void SetObjTrans(double transx, double transy, double transz)
	{
		m_obj_transx = transx;
		m_obj_transy = transy;
		m_obj_transz = transz;
	}
	void GetObjRot(double &rotx, double &roty, double &rotz)
	{
		rotx = m_obj_rotx;
		roty = m_obj_roty;
		rotz = m_obj_rotz;
	}
	void SetObjRot(double rotx, double roty, double rotz)
	{
		m_obj_rotx = rotx;
		m_obj_roty = roty;
		m_obj_rotz = rotz;
	}
	void SetRotLock(bool mode)
	{
		m_rot_lock = mode;
	}
	bool GetRotLock()
	{
		return m_rot_lock;
	}

	//camera properties
	bool GetPersp() {return m_persp;}
	void SetPersp(bool persp=true);
	bool GetFree() {return m_free;}
	void SetFree(bool free=true);
	double GetAov() {return m_aov;}
	void SetAov(double aov);
	double GetNearClip() {return m_near_clip;}
	void SetNearClip(double nc) {m_near_clip = nc;}
	double GetFarClip() {return m_far_clip;}
	void SetFarClip(double fc) {m_far_clip = fc;}

	//background color
	Color GetBackgroundColor();
	Color GetTextColor();
	void SetBackgroundColor(Color &color);
	void SetGradBg(bool val);

	//disply modes
	int GetDrawType() {return m_draw_type;}
	void SetVolMethod(int method);
	int GetVolMethod() {return m_vol_method;}
	void SetFog(bool b=true) {m_use_fog = b;}
	bool GetFog() {return m_use_fog;}
	void SetFogIntensity(double i) {m_fog_intensity = i;}
	double GetFogIntensity() {return m_fog_intensity;}
	void SetPeelingLayers(int n) {m_peeling_layers = n;}
	int GetPeelingLayers() {return m_peeling_layers;}
	void SetBlendSlices(bool val) {m_blend_slices = val;}
	bool GetBlendSlices() {return m_blend_slices;}
	void SetAdaptive(bool val) {m_adaptive = val;}
	bool GetAdaptive() {return m_adaptive;}

	//movie capture
	void Set3DRotCapture(double start_angle,
		double end_angle,
		double step,
		int frames,
		int rot_axis,
		wxString &cap_file,
		bool rewind,
		int len);
	//time sequence data
	void Set4DSeqCapture(wxString &cap_file, int begin_frame, int end_frame, bool rewind);
	//batch files
	void Set3DBatCapture(wxString &cap_file, int begin_frame, int end_frame);
	//parameter recording/capture
	void SetParamCapture(wxString &cap_file, int begin_frame, int end_frame, bool rewind);
	//set parameters
	void SetParams(double t);
	//reset and stop
	void ResetMovieAngle();
	void StopMovie();
	//4d movie frame calculation
	void Get4DSeqFrames(int &start_frame, int &end_frame, int &cur_frame);
	void Set4DSeqFrame(int frame, bool run_script);
	//3d batch file calculation
	void Get3DBatFrames(int &start_frame, int &end_frame, int &cur_frame);
	void Set3DBatFrame(int offset);

	//frame for capturing
	void EnableFrame() {m_draw_frame = true;}
	void DisableFrame() {m_draw_frame = false;};
	void SetFrame(int x, int y, int w, int h) {m_frame_x = x;
	m_frame_y = y; m_frame_w = w; m_frame_h = h;}
	void GetFrame(int &x, int &y, int &w, int &h);
	void CalcFrame();

	//scale bar
	void EnableScaleBar() {m_disp_scale_bar = true;}
	void DisableScaleBar() {m_disp_scale_bar = false;}
	void EnableSBText() {m_disp_scale_bar_text = true;}
	void DisableSBText() {m_disp_scale_bar_text = false;}
	void SetScaleBarLen(double len) {m_sb_length = len;}
	double GetScaleBarLen() { return m_sb_length; }
	void SetSBText(wxString text) {m_sb_text = text;}
	wxString GetSBText() { return m_sb_text;}

	//gamma settings
	Color GetGamma() {return m_gamma;}
	void SetGamma(Color gamma) {m_gamma = gamma;}
	//brightness adjustment
	Color GetBrightness() {return m_brightness;}
	void SetBrightness(Color brightness) {m_brightness = brightness;}
	//hdr settings
	Color GetHdr() {return m_hdr;}
	void SetHdr(Color hdr) {m_hdr = hdr;}
    //levels settings
    Color GetLevels() {return m_levels;}
    void SetLevels(Color levels) {m_levels = levels;}
	//sync values
	bool GetSyncR() {return m_sync_r;}
	void SetSyncR(bool sync_r) {m_sync_r = sync_r;}
	bool GetSyncG() {return m_sync_g;}
	void SetSyncG(bool sync_g) {m_sync_g = sync_g;}
	bool GetSyncB() {return m_sync_b;}
	void SetSyncB(bool sync_b) {m_sync_b = sync_b;}

	//reload volume list
	void SetVolPopDirty() {m_vd_pop_dirty = true;}
	//reload mesh list
	void SetMeshPopDirty() {m_md_pop_dirty = true;}
	//clear
	void ClearVolList();
	void ClearMeshList();

	//inteactive mode selection
	int GetIntMode();
	void SetIntMode(int mode);

	//set use 2d rendering results
	void SetPaintUse2d(bool use2d);
	bool GetPaintUse2d();

	//segmentation mode selection
	void SetPaintMode(int mode);
	int GetPaintMode();

	//calculations
	void SetVolumeA(VolumeData* vd);
	void SetVolumeB(VolumeData* vd);
	void SetVolumeC(VolumeData* vd);
	VolumeData* GetVolumeA() { return m_calculator.GetVolumeA(); }
	VolumeData* GetVolumeB() { return m_calculator.GetVolumeB(); }
	VolumeData* GetVolumeC() { return m_calculator.GetVolumeC(); }
	//1-sub;2-add;3-div;4-and;5-new;6-new inv;7-clear
	void Calculate(int type, wxString prev_group="", bool add=true);
	void CalculateSingle(int type, wxString prev_group, bool add);

	//set 2d weights
	void Set2dWeights();
	//segment volumes in current view
	void Segment();
	//label volumes in current view
	void Label();
	//noise removal
	int CompAnalysis(double min_voxels, double max_voxels, double thresh, bool select, bool gen_ann, int iter_limit=-1);
	void CompExport(int mode, bool select);//mode: 0-multi channels; 1-random colors
	void ShowAnnotations(bool select_annotations = false);
	int NoiseAnalysis(double min_voxels, double max_voxels, double thresh);
	void NoiseRemoval(int iter, double thresh);
	string EVEAnalysis(int min_radius, int max_radius, double thresh, int target_lv = -1);

	//brush properties
	//load default;
	void LoadDefaultBrushSettings();
	//use pressure
	void SetBrushUsePres(bool pres);
	bool GetBrushUsePres();
	//set brush size
	void SetUseBrushSize2(bool val);
	bool GetBrushSize2Link();
	void SetBrushSize(double size1, double size2);
	double GetBrushSize1();
	double GetBrushSize2();
	//set brush spacing
	void SetBrushSpacing(double spacing);
	double GetBrushSpacing();
	//set iteration number
	void SetBrushIteration(int num);
	int GetBrushIteration();
	//set translate
	void SetBrushSclTranslate(double val);
	double GetBrushSclTranslate();
	//set gm falloff
	void SetBrushGmFalloff(double val);
	double GetBrushGmFalloff();
	//w2d
	void SetW2d(double val);
	double GetW2d();
	//edge detect
	void SetEdgeDetect(bool value);
	bool GetEdgeDetect();
	//hidden removal
	void SetHiddenRemoval(bool value);
	bool GetHiddenRemoval();
	//select group
	void SetSelectGroup(bool value);
	bool GetSelectGroup();
    //use absolute value
    void SetUseAbsoluteValue(bool value);
    bool GetUseAbsoluteValue();
	//estimate threshold
	void SetEstimateThresh(bool value);
	bool GetEstimateThresh();
	//select both
	void SetSelectBoth(bool value);
	bool GetSelectBoth();
	
	void SetUseDSLTBrush(bool value);
	bool GetUseDSLTBrush();
	void SetBrushDSLT_R(int rad);
	int GetBrushDSLT_R();
	void SetBrushDSLT_Q(int quality);
	int GetBrushDSLT_Q();
	void SetBrushDSLT_C(double c);
	double GetBrushDSLT_C();

	//set clip mode
	void SetClipMode(int mode);
	int GetClipMode() {return m_clip_mode;}
	//restore clipping planes
	void RestorePlanes();
	//clipping plane rotations
	void SetClippingPlaneRotations(double rotx, double roty, double rotz);
	void GetClippingPlaneRotations(double &rotx, double &roty, double &rotz);

	//interpolation
	void SetIntp(bool mode);
	bool GetIntp();

	//get volume selector
	VolumeSelector* GetVolumeSelector() {return &m_selector;}
	//get volume calculator
	VolumeCalculator* GetVolumeCalculator() {return &m_calculator;}

	//force draw
	void ForceDraw() {UpdateScreen();}

	//run 4d script
	void SetRun4DScript(bool runscript) {m_run_script = runscript;}
	bool GetRun4DScript() {return m_run_script;}
	void Run4DScript();

	//start loop update
	void StartLoopUpdate(bool reset_peeling_layer=true);
	void HaltLoopUpdate();
	void RefreshGL(bool erase=false, bool start_loop=true);
	void RefreshGLOverlays(bool erase=false);
//#ifdef __WXMAC__
    virtual void Refresh( bool eraseBackground = true,
                         const wxRect *rect = NULL ) wxOVERRIDE;
//#endif

	//rulers
	int GetRulerType();
	void SetRulerType(int type);
	void FinishRuler();
	bool GetRulerFinished();
	void AddRulerPoint(int mx, int my);
	void AddPaintRulerPoint();
	void DrawRulers();
	vector<Ruler*>* GetRulerList();
	
	//public mouse

	void OnMouse(wxMouseEvent& event);

	//traces
	TraceGroup* GetTraceGroup();
	void CreateTraceGroup();
	int LoadTraceGroup(wxString filename);
	int SaveTraceGroup(wxString filename);
	void ExportTrace(wxString filename, unsigned int id);
	void DrawTraces();
	void GetTraces();

	//text renderer
	void SetTextRenderer(TextRenderer* text_renderer)
	{ m_text_renderer = text_renderer; }
	TextRenderer* GetTextRenderer()
	{ return m_text_renderer; }

	void SetPreDraw(bool pre_draw) { m_pre_draw = pre_draw; }//added by takashi

	void SetKeyLock(bool key_lock) { m_key_lock = key_lock; }//added by takashi

	//get manip_interpolator
	Interpolator* GetManipInterpolator(){ return &m_manip_interpolator; }//added by takashi
	void SetManipFPS(double fps){ m_manip_fps = fps; }
	double GetManipFPS(){ return m_manip_fps; }
	void SetManipTime(unsigned long time){ m_manip_time = time; }
	unsigned long GetManipTime(){ return m_manip_time; }
	void StartManipulation(const Point *view_trans, const Point *view_center, const Point *view_rot, const Point *obj_trans, const double *scale);
	void EndManipulation();
	void SetManipKey(double t, int interpolation, const Point *view_trans, const Point *view_center, const Point *view_rot, const Point *obj_trans, const double *scale);
	void SetManipParams(double t);

	void SetSearcherVisibility(bool visibility);

	vector<Ruler*>* GetLandmarks(){return &m_landmarks;}
	void SetLandmarkVisibility(bool visibility){m_draw_landmarks = visibility;}

	void SetEnhanceSelection(bool enhance) {m_enhance_sel = enhance;}
	bool GetEnhanceSelection() {return m_enhance_sel;}

	void SetAdaptiveRes(bool adapt_res) {m_adaptive_res = adapt_res;}
	bool GetAdaptiveRes() {return m_adaptive_res;}

	void SetMinPPI(double ppi);
	double GetMinPPI() {return m_min_ppi;};

	VolumeData *CopyLevel(VolumeData *src, int lv=-1);

	void SetBufferScale(int mode);

	void FixScaleBarLen(bool fix, double len=-1.0);
	void GetScaleBarFixed(bool &fix, double &len, int &unitid);
	void SetScaleBarDigit(int digit){ m_sclbar_digit = (digit >= 0 && digit <= 8) ? digit : 3; }
	int GetScaleBarDigit(){ return m_sclbar_digit; }

	VolumeData* GetCurrentVolume() {return m_cur_vol;}
	void SetCurrentVolume(VolumeData *vd) {m_cur_vol = vd;}
	DataGroup* GetCurrentVolGroup();

	double CalcCameraDistance();

	void OnErase(wxEraseEvent& event);
    
    void SetRecording(bool val) { m_recording = val; }
    bool GetRecording() { return m_recording; }
	bool IsRecordedFrameReady() { return !m_recording_frame; }
    void DownloadRecordedFrame(void *image, VkFormat &format);
    
    void SetForceHideMask(bool val) { m_force_hide_mask = val; m_draw_mask = !val; }
    bool GetForceHideMask() { return m_force_hide_mask; }

	void AbortRendering() { m_abort = true; }
    
    bool GetEasy2DAdjustMode() { return m_easy_2d_adjust; }
    void SetEasy2DAdjustMode(bool mode) { m_easy_2d_adjust = mode; }

	void SetEmptyBlockDetectorActive(bool val) { m_ebd_run = val; }

	static void setCriticalSection(wxCriticalSection* crtsec) 
	{
		ms_pThreadCS = crtsec;
		VolumeLoader::setCriticalSection(crtsec);
        EmptyBlockDetector::setCriticalSection(crtsec);
	}
	static wxCriticalSection* ms_pThreadCS;
    
    void CalcAndSetCombinedClippingPlanes();
    
    bool GetUseFogMesh() { return m_use_fog_mesh; }
    void SetUseFogMesh(bool val) { m_use_fog_mesh = val; }
    
    void ImportBigWarpCSV(wxString inputpath);
    void ExportBigWarpCSV(wxString outpath);
    void WarpCurrentVolume();

public:
	//script run
	bool m_run_script;
	wxString m_script_file;
	//capture modes
	bool m_capture;
	bool m_capture_rotat;
	bool m_capture_rotate_over;
	bool m_capture_tsequ;
	bool m_capture_bat;
	bool m_capture_param;
	bool m_capture_change_res;
	int m_capture_resx;
	int m_capture_resy;
	bool m_tile_rendering;
	int m_current_tileid;
	int m_tile_xnum;
	int m_tile_ynum;
	int m_tile_w;
	int m_tile_h;
	unsigned char *m_tiled_image;
	int m_nx;
	int m_ny;
	bool m_postdraw;
	int m_tmp_res_mode;
	std::map<VolumeData*,double> m_tmp_sample_rates;
	//begin and end frame
	int m_begin_frame;
	int m_end_frame;
	//counters
	int m_tseq_cur_num;
	int m_tseq_prv_num;
	int m_param_cur_num;
	int m_total_frames;
	//file name for capturing
	wxString m_cap_file;
	//folder name for 3d batch
	wxString m_bat_folder;
	//hud
	bool m_updating;
	bool m_draw_annotations;
	bool m_draw_camctr;
	double m_camctr_size;
	bool m_draw_info;
	bool m_draw_coord;
	bool m_drawing_coord;
	bool m_draw_frame;
	bool m_test_speed;
	bool m_draw_clip;
	bool m_draw_legend;
	bool m_mouse_focus;
	bool m_test_wiref;
	bool m_draw_bounds;
	bool m_draw_grid;
	bool m_draw_rulers;
	//current volume
	VolumeData *m_cur_vol;
	//clipping settings
	int m_clip_mask;
	int m_clip_mode;//0-normal; 1-ortho planes; 2-rot difference
	//scale bar
	bool m_disp_scale_bar;
	bool m_disp_scale_bar_text;
	double m_sb_length;
	int m_sb_unit;
	wxString m_sb_text;
	wxString m_sb_num;
	double m_sb_height;
	//ortho size
	double m_ortho_left;
	double m_ortho_right;
	double m_ortho_bottom;
	double m_ortho_top;
	//scale factor
	double m_scale_factor;
	double m_scale_factor_saved;
	//mode in determining depth of volume
	int m_point_volume_mode;  //0: use view plane; 1: use max value; 2: use accumulated value
	//ruler use volume transfer function
	bool m_ruler_use_transf;
	//ruler time dependent
	bool m_ruler_time_dep;
	//linked rotation
	bool m_linked_rot;
    //use absolute value for segmentation
    bool m_use_absolute_value;

	shared_ptr<VVulkan> m_vulkan;
	std::shared_ptr<Vulkan2dRender> m_v2drender;

#if defined(VK_USE_PLATFORM_WAYLAND_KHR) || defined(VK_USE_PLATFORM_XCB_KHR)
	bool m_readyToDraw;
	wl_compositor *m_wlCompositor;
    wl_subcompositor *m_wlSubcompositor;
	wl_surface *m_wlSurface;
    wl_region *m_wlRegion;
    wl_subsurface *m_wlSubsurface;
	wl_callback *m_wlFrameCallbackHandler;
	friend void VVVUpdatePosition(VRenderVulkanView* win);
#endif

private:
	wxWindow* m_frame;
	VRenderView* m_vrv;
	//populated lists of data
	bool m_vd_pop_dirty;
	vector <VolumeData*> m_vd_pop_list;
	bool m_md_pop_dirty;
	vector <MeshData*> m_md_pop_list;
	//real data list
	vector <TreeLayer*> m_layer_list;
	//ruler list
	int m_ruler_type;//0: 2point ruler; 1:multi-point ruler; 2:locator
	vector <Ruler*> m_ruler_list;
	Point* m_editing_ruler_point;
	//traces
	TraceGroup* m_trace_group;
	//multivolume
	MultiVolumeRenderer* m_mvr;
	//fisrt volume data in the depth groups
	//VolumeData* m_first_depth_vd;
	//initializaion
	bool m_initialized;
	bool m_init_view;
	//bg color
	Color m_bg_color;
	Color m_bg_color_inv;
	bool m_grad_bg;
	//frustrum
	double m_aov;
	double m_near_clip;
	double m_far_clip;
	//interpolation
	bool m_intp;
	//previous focus before brush
	wxWindow* m_prev_focus;

	//interactive modes
	int m_int_mode;  //interactive mode
	//1-normal viewing
	//2-painting
	//3-rotate clipping planes
	//4-one-time rendering update in painting mode
	//5-ruler mode
	//6-edit ruler
	//7-paint ruler mode
	//8-same as 4, but for paint ruler mode
	bool m_force_clear;
	bool m_interactive;
	bool m_clear_buffer;
	bool m_adaptive;
	bool m_adaptive_res;
	int m_brush_state;  //sets the button state of the tree panel
	//0-not set
	//2-append
	//3-erase
	//4-diffuse
	//8-solid

	//resizing
	bool m_resize;
	bool m_resize_ol1;
	bool m_resize_ol2;
	bool m_resize_paint;
	//brush tools
	bool m_draw_brush;
	bool m_paint_enable;
	bool m_paint_display;
	//2d frame buffers
	std::unique_ptr<vks::VFrameBuffer> m_fbo;
	std::shared_ptr<vks::VTexture> m_tex;
	std::unique_ptr<vks::VFrameBuffer> m_fbo_wt2;
	std::shared_ptr<vks::VTexture> m_tex_wt2;  //use this texture instead of m_tex when the volume for segmentation is rendered
	std::unique_ptr<vks::VFrameBuffer> m_fbo_final;
	std::shared_ptr<vks::VTexture> m_tex_final;
	bool m_clear_final_buffer;
	//temp buffer for large data compositing
	std::unique_ptr<vks::VFrameBuffer> m_fbo_temp;
	std::shared_ptr<vks::VTexture> m_tex_temp;
	std::unique_ptr<vks::VFrameBuffer> m_fbo_temp_restore;
	//shading (shadow) overlay
	std::unique_ptr<vks::VFrameBuffer> m_fbo_ol1;
	std::unique_ptr<vks::VFrameBuffer> m_fbo_ol2;
	std::shared_ptr<vks::VTexture> m_tex_ol1;
	std::shared_ptr<vks::VTexture> m_tex_ol2;
	//paint buffer
	std::unique_ptr<vks::VFrameBuffer> m_fbo_paint;
	std::shared_ptr<vks::VTexture> m_tex_paint;
	bool m_clear_paint;
	//depth peeling buffers
	vector<std::unique_ptr<vks::VFrameBuffer>> m_dp_fbo_list;
	vector<std::shared_ptr<vks::VTexture>> m_dp_tex_list;
	vector<std::shared_ptr<vks::VTexture>> m_dp_ctex_list;
	std::unique_ptr<vks::VFrameBuffer> m_dp_buf_fbo;
	std::shared_ptr<vks::VTexture> m_dp_buf_tex;
    //annotation buffers
    std::unique_ptr<vks::VFrameBuffer> m_anno_fbo;
    std::shared_ptr<vks::VTexture> m_anno_tex;
    std::shared_ptr<vks::VTexture> m_anno_dp_tex;

	//depth front tex for volume renderer (tex14)
	std::shared_ptr<vks::VTexture> m_vol_dp_fr_tex;
	//depth back tex for volume renderer (tex15)
	std::shared_ptr<vks::VTexture> m_vol_dp_bk_tex;
	//depth front tex for mesh renderer (tex15)
	std::shared_ptr<vks::VTexture> m_msh_dp_fr_tex;

	//vert buffer
	//GLuint m_quad_vbo, m_quad_vao;
	//GLuint m_quad_tile_vbo, m_quad_tile_vao;
	//GLuint m_misc_vbo, m_misc_ibo, m_misc_vao;
	//mesh picking buffer
	std::unique_ptr<vks::VFrameBuffer> m_fbo_pick;
	std::shared_ptr<vks::VTexture> m_tex_pick, m_tex_pick_depth;
	//tile buffer
	std::unique_ptr<vks::VFrameBuffer> m_fbo_tile;
	std::shared_ptr<vks::VTexture> m_tex_tile;
	std::unique_ptr<vks::VFrameBuffer> m_fbo_tiled_tmp;
	std::shared_ptr<vks::VTexture> m_tex_tiled_tmp;
    
    std::unique_ptr<vks::VFrameBuffer> m_fbo_record;
    std::shared_ptr<vks::VTexture> m_tex_record;
    bool m_recording;
	bool m_recording_frame;

	//camera controls
	bool m_persp;
	bool m_free;
	//camera distance
	double m_distance;
	double m_init_dist;
	//camera translation
	double m_transx, m_transy, m_transz;
	//saved camera trans
	double m_transx_saved, m_transy_saved, m_transz_saved;
	//camera rotation
	double m_rotx, m_roty, m_rotz;
	//saved camera rotation
	double m_rotx_saved, m_roty_saved, m_rotz_saved;
	//camera center
	double m_ctrx, m_ctry, m_ctrz;
	//saved camera center
	double m_ctrx_saved, m_ctry_saved, m_ctrz_saved;
	Quaternion m_q;
	Vector m_up;
	Vector m_head;

	//object center
	double m_obj_ctrx, m_obj_ctry, m_obj_ctrz;
	//object translation
	double m_obj_transx, m_obj_transy, m_obj_transz;
	//saved translation for free flight
	double m_obj_transx_saved, m_obj_transy_saved, m_obj_transz_saved;
	//object rotation
	double m_obj_rotx, m_obj_roty, m_obj_rotz;
	//rotation lock
	bool m_rot_lock;

	//object bounding box
	BBox m_bounds;
	double m_radius;

	//mouse position
	long old_mouse_X, old_mouse_Y;
	long prv_mouse_X, prv_mouse_Y;

	//draw controls
	bool m_draw_all;
	int m_draw_type;
	int m_vol_method;
	int m_peeling_layers;
	bool m_blend_slices;

	//fog
	bool m_use_fog;
	double m_fog_intensity;
	double m_fog_start;
	double m_fog_end;

	//movie properties
	double m_init_angle;
	double m_start_angle;
	double m_end_angle;
	double m_cur_angle;
	double m_step;
	int m_rot_axis; //1-X; 2-Y; 3-Z;
	int m_movie_seq;
	bool m_rewind;
	int m_fr_length;
	int m_stages; //0-moving to start angle; 1-moving to end; 2-rewind
	bool m_4d_rewind;

	//movie frame properties
	int m_frame_x;
	int m_frame_y;
	int m_frame_w;
	int m_frame_h;

	//post image processing
	static ImgShaderFactory m_img_shader_factory;
	Color m_gamma;
	Color m_brightness;
	Color m_hdr;
    Color m_levels;
	bool m_sync_r;
	bool m_sync_g;
	bool m_sync_b;

	//volume color map
	//double m_value_1;
	Color m_color_1;
	float m_value_2;
	Color m_color_2;
	float m_value_3;
	Color m_color_3;
	float m_value_4;
	Color m_color_4;
	float m_value_5;
	Color m_color_5;
	float m_value_6;
	Color m_color_6;
	//double m_value_7;
	Color m_color_7;

	//paint brush use pressure
	bool m_use_pres;
	bool m_on_press;
	//paint stroke radius
	double m_brush_radius1;
	double m_brush_radius2;
	bool m_use_brush_radius2;
	//paint stroke spacing
	double m_brush_spacing;

	//clipping plane rotations
	Quaternion m_q_cl;
	Quaternion m_q_cl_zero;
	Quaternion m_q_cl_fix;
	Quaternion m_q_fix;
	double m_rotx_cl, m_roty_cl, m_rotz_cl;
    double m_rotx_cl_fix, m_roty_cl_fix, m_rotz_cl_fix;
    double m_rotx_fix, m_roty_fix, m_rotz_fix;
    Vector m_trans_fix;

	bool m_dpeel;
	bool m_mdtrans;

	//volume selector for segmentation
	VolumeSelector m_selector;

	//calculator
	VolumeCalculator m_calculator;

	//timer
	nv::Timer *goTimer;
    
    bool m_refresh;
	bool m_refresh_start_loop;

	//wacom support
#ifdef _WIN32
	HCTX m_hTab;
	LOGCONTEXTA m_lc;
#endif
	double m_pressure;

	//for selection
	bool m_pick;

	//draw mask
	bool m_draw_mask;
	bool m_clear_mask;
	bool m_draw_label;
    
    bool m_force_hide_mask;

	//move view
	bool m_move_left;
	bool m_move_right;
	bool m_move_up;
	bool m_move_down;
	//move time
	bool m_tseq_forward;
	bool m_tseq_backward;
	//move clip
	bool m_clip_up;
	bool m_clip_down;
	//full cell
	bool m_cell_full;
	//link cell
	bool m_cell_link;
	//new cell id
	bool m_cell_new_id;

	//predraw in streaming mode
	bool m_pre_draw;

	bool m_key_lock;//add by takashi

	//add by takashi
	bool m_manip;
	Interpolator m_manip_interpolator;
	double m_manip_fps;
	unsigned long m_manip_time;//msec
	unsigned long m_saved_uptime;
	int m_manip_cur_num;
	int m_manip_end_frame;

	int m_min_ppi;
	int m_res_mode;
	double m_res_scale;

	LMSeacher *m_searcher;
	vector<VolumeData *> m_lm_vdlist;
	vector<Ruler *> m_landmarks;

	bool m_draw_landmarks;

	bool m_draw_overlays_only;

	bool m_enhance_sel;

	bool m_int_res;

	//glm matrices
	glm::mat4 m_mv_mat;
	glm::mat4 m_proj_mat;
	glm::mat4 m_tex_mat;

	//text renderer
	TextRenderer* m_text_renderer;

	VolumeLoader m_loader;
	bool m_load_in_main_thread;
	bool m_loader_run;
    
    EmptyBlockDetector m_ebd;
	bool m_ebd_run;

	wxTimer *m_idleTimer;

	int m_finished_peeling_layer;

	bool m_fix_sclbar;
	double m_fixed_sclbar_len;
	double m_fixed_sclbar_fac;
	int m_sclbar_digit;

	Vulkan2dRender::V2dObject m_tile_vobj;
	Vulkan2dRender::V2dObject m_brush_vobj;
	Vulkan2dRender::V2dObject m_grid_vobj;
	Vulkan2dRender::V2dObject m_scbar_vobj;
	Vulkan2dRender::V2dObject m_frame_vobj;
	Vulkan2dRender::V2dObject m_camctr_vobj;
	Vulkan2dRender::V2dObject m_name_vobj;
	Vulkan2dRender::V2dObject m_clip_vobj;
	Vulkan2dRender::V2dObject m_grad_vobj;
	Vulkan2dRender::V2dObject m_cmap_vobj;
	Vulkan2dRender::V2dObject m_ruler_vobj;

	bool m_frame_clear;

	bool m_abort;
    
    bool m_easy_2d_adjust;
    
    bool m_undo_keydown;
    bool m_redo_keydown;
    
    bool m_use_fog_mesh;

	wxButton *m_dummy;

private:
#ifdef _WIN32
	//wacom tablet
	HCTX TabletInit(HWND hWnd);
#endif

	void DrawBounds();
	void DrawGrid();
	void DrawCamCtr();
	void DrawScaleBar();
	void DrawLegend();
	void DrawName(double x, double y, int nx, int ny,
		wxString name, Color color,
		double font_height, bool hilighted=false);
	char* wxStringToChar(wxString input);
	void DrawFrame();
	void DrawClippingPlanes(bool border, int face_winding);
	void SetColormapColors(int colormap);
	void DrawColormap();
	void DrawGradBg();
	void DrawInfo(int nx, int ny);

	//depth
	double CalcZ(double z);
	void CalcFogRange();
	//different modes
	void Draw();//draw volumes only
	void DrawDP();//draw volumes and meshes with depth peeling
	//mesh and volume
	void DrawMeshes(const std::unique_ptr<
		vks::VFrameBuffer>& framebuf,
		bool clear_framebuf,
		int peel=0,
		const std::shared_ptr<vks::VTexture> depth_tex=std::shared_ptr<vks::VTexture>()
	);
	//0: no dp
	//1: draw depth after 15 (15)
	//2: draw mesh after 14 (14, 15)
	//3: draw mesh after 13 and before 15 (13, 14, 15)
	//4: draw mesh before 15 (at 14) (14, 15)
	//5: draw mesh at 15 (15)
	void DrawVolumes(int peel=0);//0: no dep
	//1: draw volume befoew 15 (15)
	//2: draw volume after 15 (14, 15)
	//3: draw volume after 14 and before 15 (13, 14, 15)
	//4: same as 3 (14, 15)
	//5: same as 2 (15)
	void DrawVolumesDP();
	//annotation layer
	void DrawAnnotations();
	//draw out the framebuffer after composition
	void PrepFinalBuffer();
	void ClearFinalBuffer();
	void DrawFinalBuffer(bool clear);
	//different volume drawing modes
	void DrawVolumesMulti(vector<VolumeData*> &list, int peel=0);
	void DrawVolumesComp(vector<VolumeData*> &list, bool mask=false, int peel=0);
	void DrawMIP(VolumeData* vd, std::unique_ptr<vks::VFrameBuffer> &fb, int peel=0, double sampling_frq_fac = -1.0);
	void DrawOVER(VolumeData* vd, std::unique_ptr<vks::VFrameBuffer>& fb, int peel=0, double sampling_frq_fac = -1.0);
	//overlay passes
	void DrawOLShading(VolumeData* vd, std::unique_ptr<vks::VFrameBuffer>& fb);
	void DrawOLShadows(vector<VolumeData*> &vlist, std::unique_ptr<vks::VFrameBuffer>& fb);
	void DrawOLShadowsMesh(const std::shared_ptr<vks::VTexture>& tex_depth, double darkness);
	
	void StartTileRendering(int w, int h, int tilew, int tileh);
	void EndTileRendering();
	void DrawTile();
	void GetTiledViewQuadVerts(int tileid, vector<Vulkan2dRender::Vertex> &verts);
	void Get1x1DispSize(int &w, int &h);

	//get mesh shadow
	bool GetMeshShadow(double &val);

	//painting
	void DrawCircle(double cx, double cy,
		double radius, Color &color, glm::mat4 &matrix);
	void DrawBrush();
	void PaintStroke();
	void DisplayStroke();

	//handle camera
	void HandleProjection(int nx, int ny);
	void HandleCamera();
	Quaternion Trackball(int p1x, int p1y, int p2x, int p2y);
	Quaternion TrackballClip(int p1x, int p1y, int p2x, int p2y);
	void Q2A();
	void A2Q();
	//sort bricks after the view has been changed
	void SetSortBricks();

	//pre draw and post draw
	void PreDraw();
	void PostDraw();

	//run 4d script
	void Run4DScript(wxString &scriptname, VolumeData* vd);
	void RunNoiseReduction(wxFileConfig &fconfig);
	void RunSelectionTracking(wxFileConfig &fconfig);
	void RunRandomColors(wxFileConfig &fconfig);
	void RunSeparateChannels(wxFileConfig &fconfig);
	void RunExternalExe(wxFileConfig &fconfig);
	void RunFetchMask(wxFileConfig &fconfig);

	//brush states update
	void SetBrush(int mode);
	void UpdateBrushState();

	//selection
	void Pick();
	void PickMesh();
	void PickVolume();
	//mode: 0-add selection; 1-single selection; 2-subtract selection, 3-switch an editing segment
	bool SelSegVolume(int mode=0);
	//mode: 0-add selection; 1-single selection; 2-subtract selection, 3-switch an editing segment
	bool SelLabelSegVolumeMax(int mode, vector<VolumeData*> ref);

	//get mouse point in 3D
	//mode: 0-maximum with original value; 1-maximum with transfered value; 2-accumulated with original value; 3-accumulated with transfered value
	double GetPointVolume(Point &mp, int mx, int my, VolumeData* vd, int mode, bool use_transf, double thresh = 0.5);
	double GetPointAndLabel(Point& mp, int& lblval, int mx, int my, VolumeData* vd);
    double GetPointAndLabelMax(Point& mp, int& lblval, int mx, int my, VolumeData* vd, vector<VolumeData*> ref);
	double GetPointAndIntVolume(Point& mp, double &intensity, bool normalize, int mx, int my, VolumeData* vd, double thresh = 0.5);
	double GetPointVolumeBox(Point &mp, int mx, int my, VolumeData* vd, bool calc_mats=true);
	double GetPointVolumeBox2(Point &p1, Point &p2, int mx, int my, VolumeData* vd);
	double GetPointPlane(Point &mp, int mx, int my, Point *planep=0, bool calc_mats=true);
	Point* GetEditingRulerPoint(int mx, int my);
	//added by takashi
	bool SwitchRulerBalloonVisibility_Point(int mx, int my);

    void UpdateScreen();
    
	//system call
	void OnDraw(wxPaintEvent& event);
	void OnResize(wxSizeEvent& event);
	void Resize(bool refresh=true);
#if defined(__WXGTK__)
	void OnIdle(wxIdleEvent& event);
#else
	void OnIdle(wxTimerEvent& event);
#endif
	void OnKeyDown(wxKeyEvent& event);
	void OnContextMenu(wxContextMenuEvent& event);
	
	void OnShowAllDatasets(wxCommandEvent& event);
	void OnHideOtherDatasets(wxCommandEvent& event);
	void OnHideSelectedDataset(wxCommandEvent& event);
	void OnShowAllFragments(wxCommandEvent& event);
    void OnDeselectAllFragments(wxCommandEvent& event);
	void OnHideOtherFragments(wxCommandEvent& event);
	void OnHideSelectedFragments(wxCommandEvent& event);
	void OnJoinSegments(wxCommandEvent& event);
	void OnSplitSegments(wxCommandEvent& event);
	void OnUndoVisibilitySettings(wxCommandEvent& event);
	void OnRedoVisibilitySettings(wxCommandEvent& event);

	//WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam);

	void switchLevel(VolumeData *vd);

	VolumeLoader* GetVolumeLoader() { return &m_loader; }

	void DrawViewQuad();

	DECLARE_EVENT_TABLE();

	friend class VRenderView;
};


class LegendListCtrl : public wxListCtrl
{
    
public:
    LegendListCtrl(wxWindow* parent,
                   VRenderView* view,
                   wxWindowID id,
                   const wxPoint& pos=wxDefaultPosition,
                   const wxSize& size=wxDefaultSize,
                   long style=wxLC_REPORT|wxLC_SINGLE_SEL|wxLC_NO_HEADER);
    ~LegendListCtrl();
    
    wxString GetText(long item, int col);
    void UpdateContents();
    
    void SetView(VRenderView* view)
    {
        m_view = view;
        UpdateContents();
    }
    
private:
    VRenderView* m_view;
    
private:
    void OnItemSelected(wxListEvent& event);
    void OnItemChecked(wxListEvent& event);
    void OnItemUnchecked(wxListEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnScroll(wxScrollWinEvent& event);
    void OnScroll(wxMouseEvent& event);
    
    DECLARE_EVENT_TABLE()
protected:
    wxSize GetSizeAvailableForScrollTarget(const wxSize& size) {
        return size - GetEffectiveMinSize();
    }
};

class EXPORT_API LegendPanel : public wxPanel
{
public:
    enum
    {
        ID_SearchTextCtrl,
        ID_DownloadBtn,
        ID_DBTextCtrl,
        ID_BrowseBtn,
        ID_AddDBBtn
    };
    
    LegendPanel(wxWindow* frame,
                VRenderView* view);
    ~LegendPanel();
    
    void SetViewAndReload(VRenderView* view)
    {
        m_view = view;
        m_list->SetView(m_view);
    }
    VRenderView* GetView()
    {
        return m_view;
    }
    void Reload();
    
    wxSize GetListSize();
    
private:
    wxWindow* m_frame;
    //current view
    VRenderView* m_view;
    
    LegendListCtrl *m_list;
    
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class EXPORT_API VRenderView: public wxPanel
{
public:
	enum
	{
		ID_VolumeSeqRd = wxID_HIGHEST+1001,
		ID_VolumeMultiRd,
		ID_VolumeCompRd,
		ID_CaptureBtn,
		ID_BgColorPicker,
		ID_RotLinkChk,
		ID_RotResetBtn,
		ID_XRotText,
		ID_YRotText,
		ID_ZRotText,
		ID_XRotSldr,
		ID_YRotSldr,
		ID_ZRotSldr,
		ID_XRotSpin,
		ID_YRotSpin,
		ID_ZRotSpin,
		ID_RotLockChk,
		ID_DepthAttenChk,
		ID_DepthAttenFactorSldr,
		ID_DepthAttenResetBtn,
		ID_DepthAttenFactorText,
		ID_CenterBtn,
		ID_Scale121Btn,
		ID_ScaleFactorSldr,
		ID_ScaleFactorText,
		ID_ScaleFactorSpin,
		ID_ScaleResetBtn,
		ID_CamCtrChk,
		ID_FpsChk,
		ID_LegendChk,
		ID_IntpChk,
		ID_DefaultBtn,
		ID_AovSldr,
		ID_AovText,
		ID_FreeChk,
		ID_ResCombo,
		ID_PPISldr,
		ID_PPIText,
		ID_Searcher,
		ID_SearchChk,
		ID_Timer,
        ID_LegendBtn,
        ID_LegendList
	};

	VRenderView(wxWindow* frame,
		wxWindow* parent,
		wxWindowID id,
		const std::shared_ptr<VVulkan> &sharedContext = std::shared_ptr<VVulkan>(),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0);
	~VRenderView();

	double GetAvailableGraphicsMemory(int device_id)
	{
		return m_glview ? m_glview->GetAvailableGraphicsMemory(device_id) : 0.0;
	}

	//recalculate view according to object bounds
	void InitView(unsigned int type=INIT_BOUNDS);

	//clear layers
	void Clear();

	//data management
	int GetAny();
	int GetDispVolumeNum();
	int GetAllVolumeNum();
	int GetMeshNum();
	int GetGroupNum();
	int GetLayerNum();
	VolumeData* GetAllVolumeData(int index);
	VolumeData* GetDispVolumeData(int index);
	MeshData* GetMeshData(int index);
	TreeLayer* GetLayer(int index);
	MultiVolumeRenderer* GetMultiVolumeData();
	VolumeData* GetVolumeData(wxString &name);
	MeshData* GetMeshData(wxString &name);
	Annotations* GetAnnotations(wxString &name);
	DataGroup* GetGroup(wxString &name);
	DataGroup* GetParentGroup(wxString& name);
    MeshGroup* GetParentMGroup(wxString& name);
	DataGroup* AddVolumeData(VolumeData* vd, wxString group_name="");
	void AddMeshData(MeshData* md);
	void AddAnnotations(Annotations* ann);
	wxString AddGroup(wxString str = "", wxString prev_group="");
	wxString CheckNewGroupName(const wxString &name, int type)
	{
		if (m_glview)
			return m_glview->CheckNewGroupName(name, type);
		return wxString();
	}
	wxString AddMGroup(wxString str = "");
	MeshGroup* GetMGroup(wxString &name);
	void RemoveVolumeData(wxString &name);
	void RemoveVolumeDataset(BaseReader *reader, int channel);
	void RemoveMeshData(wxString &name);
	void RemoveAnnotations(wxString &name);
	void RemoveGroup(wxString &name);
	void Isolate(int type, wxString name);
	void ShowAll();
	//move
	void MoveLayerinView(wxString &src_name, wxString &dst_name);
	//move volume
	void MoveLayerinGroup(wxString &group_name, wxString &src_name, wxString &dst_name, int insert_mode=0);
	void MoveLayertoView(wxString &group_name, wxString &src_name, wxString &dst_name);
	void MoveLayertoGroup(wxString &group_name, wxString &src_name, wxString &dst_name);
	void MoveLayerfromtoGroup(wxString &src_group_name, wxString &dst_group_name, wxString &src_name, wxString &dst_name, int insert_mode=0);
	//move mesh
	void MoveMeshinGroup(wxString &group_name, wxString &src_name, wxString &dst_name, int insert_mode=0);
	void MoveMeshtoView(wxString &group_name, wxString &src_name, wxString &dst_name);
	void MoveMeshtoGroup(wxString &group_name, wxString &src_name, wxString &dst_name);
	void MoveMeshfromtoGroup(wxString &src_group_name, wxString &dst_group_name, wxString &src_name, wxString &dst_name, int insert_mode=0);
	//reorganize layers in view
	void OrganizeLayers();
	//randomize color
	void RandomizeColor();
	//rotation linking
	void OnRotLink(bool b);

	//trim volume data with meshes
	//void TrimData(int type=SEG_MTHD_BOTH);

	//toggle hiding/displaying
	void SetDraw(bool draw);
	void ToggleDraw();
	bool GetDraw();

	//camera operations
	void GetTranslations(double &transx, double &transy, double &transz);
	void SetTranslations(double transx, double transy, double transz);
	void GetRotations(double &rotx, double &roty, double &rotz);
	void SetRotations(double rotx, double roty, double rotz, bool ui_update=true);
	void GetCenters(double &ctrx, double &ctry, double &ctrz);
	void SetCenters(double ctrx, double ctry, double ctrz);
	double GetCenterEyeDist();
	void SetCenterEyeDist(double dist);
	double GetRadius();
	void SetRadius(double r);
	void SetScaleFactor(double s)
	{
		m_glview->m_scale_factor = s;
		int val = s*100;
		m_scale_factor_sldr->SetValue(val);
		wxString str = wxString::Format("%d", val);
		m_scale_factor_text->SetValue(str);
	}

	//object operations
	void GetObjCenters(double &ctrx, double &ctry, double &ctrz);
	void SetObjCenters(double ctrx, double ctry, double ctrz);
	void GetObjTrans(double &transx, double &transy, double &transz);
	void SetObjTrans(double transx, double transy, double transz);
	void GetObjRot(double &rotx, double &roty, double &rotz);
	void SetObjRot(double rotx, double roty, double rotz);

	//camera properties
	bool GetPersp() {if (m_glview) return m_glview->GetPersp(); else return true;}
	void SetPersp(bool persp) {if (m_glview) m_glview->SetPersp(persp);}
	bool GetFree() {if (m_glview) return m_glview->GetFree(); else return false;}
	void SetFree(bool free) {if (m_glview) m_glview->SetFree(free);}
	double GetAov();
	void SetAov(double aov);
	double GetNearClip();
	void SetNearClip(double nc);
	double GetFarClip();
	void SetFarClip(double fc);

	//background color
	Color GetBackgroundColor();
	Color GetTextColor();
	void SetBackgroundColor(Color &color);
	void SetGradBg(bool val);

	//point volume mode
	void SetPointVolumeMode(int mode);
	int GetPointVolumeMode();

	//ruler uses transfer function
	void SetRulerUseTransf(bool val);
	bool GetRulerUseTransf();

	//ruler time dependent
	void SetRulerTimeDep(bool val);
	bool GetRulerTimeDep();

	//disply modes
	int GetDrawType();
	void SetVolMethod(int method);
	int GetVolMethod();

	//other properties
	void SetFog(bool b=true);
	bool GetFog();
	void SetFogIntensity(double i);
	double GetFogIntensity();
	void SetPeelingLayers(int n);
	int GetPeelingLayers();
	void SetBlendSlices(bool val);
	bool GetBlendSlices();
	void SetAdaptive(bool val);
	bool GetAdaptive();

	//reset counter
	static void ResetID();

	//get rendering context
	std::shared_ptr<VVulkan> GetContext();

	//refresh glview
	void RefreshGL(bool intactive=false, bool start_loop=true);
	void RefreshGLOverlays();

	//movie export
	//get frame info
	//4d sequence
	void Get4DSeqFrames(int &start_frame, int &end_frame, int &cur_frame)
	{
		if (m_glview)
			m_glview->Get4DSeqFrames(start_frame, end_frame, cur_frame);
	}
	void Set4DSeqFrame(int frame, bool run_script)
	{
		if (m_glview)
			m_glview->Set4DSeqFrame(frame, run_script);
	}
	//3d batch
	void Get3DBatFrames(int &start_frame, int &end_frame, int &cur_frame)
	{
		if (m_glview)
			m_glview->Get3DBatFrames(start_frame, end_frame, cur_frame);
	}
	void Set3DBatFrame(int offset)
	{
		if (m_glview)
			m_glview->Set3DBatFrame(offset);
	}
	//set movie export
	void Set3DRotCapture(double start_angle,
		double end_angle,
		double step,
		int frames,
		int rot_axis,
		wxString &cap_file,
		bool rewind,
		int len = 4)
	{
		if (m_glview)
			m_glview->Set3DRotCapture(start_angle, end_angle, step, frames, rot_axis, cap_file, rewind, len);
	}
	void Set4DSeqCapture(wxString &cap_file, int begin_frame, int end_frame, bool rewind)
	{
		if (m_glview)
			m_glview->Set4DSeqCapture(cap_file, begin_frame, end_frame, rewind);
	}
	void Set3DBatCapture(wxString &cap_file, int begin_frame, int end_frame)
	{
		if (m_glview)
			m_glview->Set3DBatCapture(cap_file, begin_frame, end_frame);
	}
	void SetParamCapture(wxString &cap_file, int begin_frame, int end_frame, bool rewind)
	{
		if (m_glview)
			m_glview->SetParamCapture(cap_file, begin_frame, end_frame, rewind);
	}
	void SetParams(double p) {
		if (m_glview)
			m_glview->SetParams(p);
	}
	//reset & stop
	void ResetMovieAngle()
	{
		if (m_glview)
			m_glview->ResetMovieAngle();
	}
	void StopMovie()
	{
		if (m_glview)
			m_glview->StopMovie();
	}

	//movie frame
	void EnableFrame()
	{
		if (m_glview) m_glview->EnableFrame();
	}
	void DisableFrame()
	{
		if (m_glview) m_glview->DisableFrame();
	}
	void SetFrame(int x, int y, int w, int h)
	{
		if (m_glview) m_glview->SetFrame(x, y, w, h);
	}
	void GetFrame(int &x, int &y, int &w, int &h)
	{
		if (m_glview) m_glview->GetFrame(x, y, w, h);
	}
	void CalcFrame()
	{
		if (m_glview) m_glview->CalcFrame();
	}
	bool GetFrameEnabled()
	{
		if (m_glview) return m_glview->m_draw_frame;
		return false;
	}
	wxSize GetGLSize()
	{
		if (m_glview) return m_glview->GetSize();
		else return wxSize(0, 0);
	}
	//scale bar
	void EnableScaleBar() {if (m_glview) m_glview->EnableScaleBar();}
	void DisableScaleBar() {if (m_glview) m_glview->DisableScaleBar();}
	void EnableSBText() {if (m_glview) m_glview->EnableSBText();}
	void DisableSBText() {if (m_glview) m_glview->DisableSBText();}
	void SetScaleBarLen(double len)
	{if (m_glview) m_glview->SetScaleBarLen(len);}
	double GetScaleBarLen()
	{if (m_glview) return m_glview->GetScaleBarLen(); else return 0.0; }
	void SetSBText(wxString text)
	{if (m_glview) m_glview->SetSBText(text);}
	wxString GetSBText()
	{if (m_glview) return m_glview->GetSBText(); else return wxString();}
	void SetSbNumText(wxString text)
	{if (m_glview) m_glview->m_sb_num = text;}
	void SetSbUnitSel(int sel)
	{if (m_glview) m_glview->m_sb_unit = sel;}

	//set dirty
	void SetVolPopDirty()
	{if (m_glview) m_glview->SetVolPopDirty();}
	void SetMeshPopDirty()
	{if (m_glview) m_glview->SetMeshPopDirty();}
	//clear
	void ClearVolList()
	{if (m_glview) m_glview->ClearVolList();}
	void ClearMeshList()
	{if (m_glview) m_glview->ClearMeshList();}

	//inteactive mode selection
	int GetIntMode()
	{if (m_glview) return m_glview->GetIntMode(); else return 1;}
	void SetIntMode(int mode = 1)
	{if (m_glview) m_glview->SetIntMode(mode);}

	//set paint use 2d results
	void SetPaintUse2d(bool use2d)
	{if (m_glview) m_glview->SetPaintUse2d(use2d);}
	bool GetPaintUse2d()
	{if (m_glview) return m_glview->GetPaintUse2d(); else return false;}

	//segmentation mode selection
	void SetPaintMode(int mode)
	{if (m_glview) m_glview->SetPaintMode(mode);}
	int GetPaintMode()
	{if (m_glview) return m_glview->GetPaintMode(); else return 0;}

	//segment volumes in current view
	void Segment()
	{if (m_glview) m_glview->Segment();}
	//label the volume in current view
	void Label()
	{if (m_glview) m_glview->Label();}
	//remove noise

	int CompAnalysis(double min_voxels, double max_voxels, double thresh, bool select, bool gen_ann, int iter_limit=-1)
	{
		if (m_glview) 
			return m_glview->CompAnalysis(min_voxels, max_voxels, thresh, select, gen_ann, iter_limit); 
		else 
			return 0;
	}
	void CompExport(int mode, bool select)
	{if (m_glview) m_glview->CompExport(mode, select);}
	void ShowAnnotations()
	{if (m_glview) m_glview->ShowAnnotations();}
	int NoiseAnalysis(double min_voxels, double max_voxels, double thresh)
	{if (m_glview) return m_glview->NoiseAnalysis(min_voxels, max_voxels, thresh); else return 0;}
	void NoiseRemoval(int iter, double thresh)
	{if (m_glview) m_glview->NoiseRemoval(iter, thresh);}

	//calculations
	void SetVolumeA(VolumeData* vd)
	{if (m_glview) m_glview->SetVolumeA(vd);}
	void SetVolumeB(VolumeData* vd)
	{if (m_glview) m_glview->SetVolumeB(vd);}
	void SetVolumeC(VolumeData* vd)
	{if (m_glview) m_glview->SetVolumeC(vd);}
	VolumeData* GetVolumeA()
	{if (m_glview) return m_glview->GetVolumeA(); else return 0;}
	VolumeData* GetVolumeB()
	{if (m_glview) return m_glview->GetVolumeB(); else return 0;}
	VolumeData* GetVolumeC()
	{if (m_glview) return m_glview->GetVolumeC(); else return 0;}
	void Calculate(int type, wxString prev_group="")
	{if (m_glview) return m_glview->Calculate(type, prev_group);}

	//brush properties
	//use pressure
	void SetBrushUsePres(bool pres)
	{ if (m_glview) m_glview->SetBrushUsePres(pres); }
	bool GetBrushUsePres()
	{ if (m_glview) return m_glview->GetBrushUsePres(); else return false;}
	//set brush size
	void SetUseBrushSize2(bool val)
	{ if (m_glview) m_glview->SetUseBrushSize2(val); }
	bool GetBrushSize2Link()
	{ if (m_glview) return m_glview->GetBrushSize2Link(); else return false; }
	void SetBrushSize(double size1, double size2)
	{ if (m_glview) m_glview->SetBrushSize(size1, size2); }
	double GetBrushSize1()
	{ if (m_glview) return m_glview->GetBrushSize1(); else return 0.0; }
	double GetBrushSize2()
	{ if (m_glview) return m_glview->GetBrushSize2(); else return 0.0; }
	//set brush spacing
	void SetBrushSpacing(double spacing)
	{ if (m_glview) m_glview->SetBrushSpacing(spacing); }
	double GetBrushSpacing()
	{ if (m_glview) return m_glview->GetBrushSpacing(); else return 1.0; }
	//set iteration number
	void SetBrushIteration(int num)
	{ if (m_glview) m_glview->SetBrushIteration(num); }
	int GetBrushIteration()
	{ if (m_glview) return m_glview->GetBrushIteration(); else return 0; }
	//scalar translate
	void SetBrushSclTranslate(double val)
	{ if (m_glview) m_glview->SetBrushSclTranslate(val); }
	double GetBrushSclTranslate()
	{ if (m_glview) return m_glview->GetBrushSclTranslate(); else return 0.0; }
	//gm falloff
	void SetBrushGmFalloff(double val)
	{ if (m_glview) m_glview->SetBrushGmFalloff(val); }
	double GetBrushGmFalloff()
	{ if (m_glview) return m_glview->GetBrushGmFalloff(); else return 0.0; }
	//w2d
	void SetW2d(double val)
	{ if (m_glview) m_glview->SetW2d(val); }
	double GetW2d()
	{ if (m_glview) return m_glview->GetW2d(); else return 0.0; }
	//edge detect
	void SetEdgeDetect(bool value)
	{ if (m_glview) m_glview->SetEdgeDetect(value); }
	bool GetEdgeDetect()
	{ if (m_glview) return m_glview->GetEdgeDetect(); else return false;}
	//hidden removal
	void SetHiddenRemoval(bool value)
	{ if (m_glview) m_glview->SetHiddenRemoval(value); }
	bool GetHiddenRemoval()
	{ if (m_glview) return m_glview->GetHiddenRemoval(); else return false;}
	//select group
	void SetSelectGroup(bool value)
	{ if (m_glview) m_glview->SetSelectGroup(value); }
	bool GetSelectGroup()
	{ if (m_glview) return m_glview->GetSelectGroup(); else return false;}
    //use absolute value
    void SetUseAbsoluteValue(bool value)
    { if (m_glview) m_glview->SetUseAbsoluteValue(value); }
    bool GetUseAbsoluteValue()
    { if (m_glview) return m_glview->GetUseAbsoluteValue(); else return false;}
	//estimate threshold
	void SetEstimateThresh(bool value)
	{ if (m_glview) m_glview->SetEstimateThresh(value);}
	bool GetEstimateThresh()
	{ if (m_glview) return m_glview->GetEstimateThresh(); else return false;}
	//select a and b
	void SetSelectBoth(bool value)
	{ if (m_glview) m_glview->SetSelectBoth(value); }
	bool GetSelectBoth()
	{ if (m_glview) return m_glview->GetSelectBoth(); else return false;}
	
	void SetUseDSLTBrush(bool value)
	{ if (m_glview) m_glview->SetUseDSLTBrush(value); }
	bool GetUseDSLTBrush()
	{ if (m_glview) return m_glview->GetUseDSLTBrush(); else return false;}
	void SetBrushDSLT_R(int rad)
	{ if (m_glview) m_glview->SetBrushDSLT_R(rad); }
	int GetBrushDSLT_R()
	{ if (m_glview) return m_glview->GetBrushDSLT_R(); else return 0; }
	void SetBrushDSLT_Q(int quality)
	{ if (m_glview) m_glview->SetBrushDSLT_Q(quality); }
	int GetBrushDSLT_Q()
	{ if (m_glview) return m_glview->GetBrushDSLT_Q(); else return 0; }
	void SetBrushDSLT_C(double c)
	{ if (m_glview) m_glview->SetBrushDSLT_C(c); }
	double GetBrushDSLT_C()
	{ if (m_glview) return m_glview->GetBrushDSLT_C(); else return 0.0; }

	//set clip mode
	void SetClipMode(int mode)
	{
		if (m_glview) m_glview->SetClipMode(mode);
	}
	int GetClipMode()
	{
		if (m_glview) return m_glview->GetClipMode();
		return 0;
	}
	//restore clipping planes
	void RestorePlanes()
	{
		if (m_glview) m_glview->RestorePlanes();
	}
	void SetClippingPlaneRotations(double rotx, double roty, double rotz)
	{
		if (m_glview) m_glview->SetClippingPlaneRotations(360.0 - rotx, roty, rotz);
	}
	void GetClippingPlaneRotations(double &rotx, double &roty, double &rotz)
	{
		if (m_glview)
        {
            m_glview->GetClippingPlaneRotations(rotx, roty, rotz);
            rotx = 360.0 - rotx;
        }
	}

	//get volume selector
	VolumeSelector* GetVolumeSelector()
	{
		if (m_glview) return m_glview->GetVolumeSelector(); else return 0;
	}
	//get volume calculator
	VolumeCalculator* GetVolumeCalculator()
	{
		if (m_glview) return m_glview->GetVolumeCalculator(); else return 0;
	}

	//set ruler type
	int GetRulerType()
	{
		if (m_glview) return m_glview->GetRulerType();
		else return 0;
	}
	void SetRulerType(int type)
	{
		if (m_glview) m_glview->SetRulerType(type);
	}
	void FinishRuler()
	{
		if (m_glview) m_glview->FinishRuler();
	}
	vector<Ruler*>* GetRulerList()
	{
		if (m_glview) return m_glview->GetRulerList(); else return 0;
	}

	//traces
	void CreateTraceGroup()
	{
		if (m_glview) m_glview->CreateTraceGroup();
	}
	TraceGroup* GetTraceGroup()
	{
		if (m_glview) return m_glview->GetTraceGroup(); else return 0;
	}
	int LoadTraceGroup(wxString filename)
	{
		if (m_glview) return m_glview->LoadTraceGroup(filename); else return 0;
	}
	int SaveTraceGroup(wxString filename)
	{
		if (m_glview) return m_glview->SaveTraceGroup(filename); else return 0;
	}
	void ExportTrace(wxString filename, unsigned int id)
	{
		if (m_glview) m_glview->ExportTrace(filename, id);
	}

	//bit mask for items to save
	bool m_default_saved;
	void SaveDefault(unsigned int mask = 0xffffffff);

	//text renderer
	void SetTextRenderer(TextRenderer* text_renderer)
	{ if (m_glview) m_glview->SetTextRenderer(text_renderer); }


	void SetPreDraw(bool pre_draw)//added by takashi
	{
		if (m_glview) m_glview->SetPreDraw(pre_draw);
	}

	void SetKeyLock(bool key_lock)//added by takashi
	{
		if (m_glview) m_glview->SetKeyLock(key_lock);
	}

	Interpolator* GetManipInterpolator()//added by takashi
	{
		if (m_glview) return m_glview->GetManipInterpolator();
		else return 0;
	}

	void SetEnhanceSelection(bool enhance)
	{
		if (m_glview) m_glview->SetEnhanceSelection(enhance);
	}
	bool GetEnhanceSelection()
	{
		if (m_glview) return m_glview->GetEnhanceSelection(); else return 0;
	}

	void SetAdaptiveRes(bool adapt_res)
	{
		if (m_glview) m_glview->SetAdaptiveRes(adapt_res);
	}
	bool GetAdaptiveRes()
	{
		if (m_glview) return m_glview->GetAdaptiveRes(); else return 0;
	}
    
    void SetRecording(bool recording)
    {
        if (m_glview) m_glview->SetRecording(recording);
    }
    bool GetRecording()
    {
        if (m_glview) return m_glview->GetRecording(); else return false;
    }
	bool IsRecordedFrameReady()
	{
		if (m_glview) return m_glview->IsRecordedFrameReady(); else return false;
	}
    void DownloadRecordedFrame(void *image, VkFormat &format)
    {
        if (m_glview) m_glview->DownloadRecordedFrame(image, format);
    }

	void SetMinPPI(double ppi);
	double GetMinPPI();

	void SetResMode(int mode)
	{
		int elem_num = m_res_mode_combo->GetStrings().size();
		if (mode < 0 || mode >= elem_num) return;
		
		m_res_mode = mode;
		if (m_glview) m_glview->m_res_mode = mode;
		m_res_mode_combo->SetSelection(mode);
	}
	int GetResMode() {if (m_glview) return m_glview->m_res_mode; else return 0;}

	void FixScaleBarLen(bool fix, double len=-1.0) {if (m_glview) m_glview->FixScaleBarLen(fix, len);}
	void GetScaleBarFixed(bool &fix, double &len, int &unitid) {if (m_glview) m_glview->GetScaleBarFixed(fix, len, unitid);}
	void SetScaleBarDigit(int digit) {if (m_glview) m_glview->SetScaleBarDigit(digit);}
	int GetScaleBarDigit() { return m_glview ? m_glview->GetScaleBarDigit() : 3; }

	VolumeData* GetCurrentVolume() { if (m_glview) return m_glview->GetCurrentVolume(); else return NULL; }
	void SetCurrentVolume(VolumeData *vd) { if (m_glview) m_glview->SetCurrentVolume(vd); }
	DataGroup* GetCurrentVolGroup() { if (m_glview) return m_glview->GetCurrentVolGroup(); else return NULL; }

	VolumeLoader* GetVolumeLoader() { if (m_glview) return m_glview->GetVolumeLoader(); else return NULL; }
	 
	TextRenderer* GetTextRenderer() { if (m_glview) return m_glview->GetTextRenderer(); else return nullptr; }
    
    void SetForceHideMask(bool val) { if (m_glview) m_glview->SetForceHideMask(val); }
	bool GetForceHideMask() { if (m_glview) return m_glview->GetForceHideMask(); else return false; }

	void AbortRendering() { if (m_glview) m_glview->AbortRendering(); }
    
    void SetEasy2DAdjustMode(bool val) { if (m_glview) m_glview->SetEasy2DAdjustMode(val); }
    bool GetEasy2DAdjustMode() { if (m_glview) return m_glview->GetEasy2DAdjustMode(); else return false; }

	void SetEmptyBlockDetectorActive(bool val) { if (m_glview) m_glview->SetEmptyBlockDetectorActive(val); }
    
    void CalcAndSetCombinedClippingPlanes() { if (m_glview) m_glview->CalcAndSetCombinedClippingPlanes(); }
    
    void SetUseFogMesh(bool val)
    {
        if (m_glview) m_glview->SetUseFogMesh(val);
    }
    bool GetUseFogMesh()
    {
        if (m_glview) return m_glview->GetUseFogMesh(); else return false;
    }

	string EVEAnalysis(int min_radius, int max_radius, double thresh, int target_lv = -1)
	{
		if (m_glview) return m_glview->EVEAnalysis(min_radius, max_radius, thresh, target_lv); else return string();
	}
    
    void ToggleQualityDropDownBox(bool val);
    
    void ReplaceVolumeData(wxString &name, VolumeData *dst)
    {
        if (m_glview) m_glview->ReplaceVolumeData(name, dst);
    }
    
    void ImportBigWarpCSV(wxString inputpath)
    {
        if (m_glview) m_glview->ImportBigWarpCSV(inputpath);
    }
    
    void ExportBigWarpCSV(wxString outpath)
    {
        if (m_glview) m_glview->ExportBigWarpCSV(outpath);
    }
    
    void WarpCurrentVolume()
    {
        if (m_glview) m_glview->WarpCurrentVolume();
    }

public:
	wxWindow* m_frame;
	static int m_id;

	//render view///////////////////////////////////////////////
	VRenderVulkanView *m_glview;
	wxFrame* m_full_frame;
	wxBoxSizer* m_view_sizer;

	//top bar///////////////////////////////////////////////////
	wxPanel* m_panel_1;
	wxRadioButton *m_volume_seq_rd;
	wxRadioButton *m_volume_multi_rd;
	wxRadioButton *m_volume_comp_rd;
	wxButton *m_capture_btn;
	wxColourPickerCtrl *m_bg_color_picker;
	wxCheckBox *m_cam_ctr_chk;
	wxCheckBox *m_fps_chk;
	wxCheckBox *m_legend_chk;
	wxCheckBox *m_intp_chk;
	wxCheckBox *m_search_chk;
	wxSlider* m_aov_sldr;
	wxTextCtrl* m_aov_text;
	wxCheckBox* m_free_chk;

	wxSlider* m_ppi_sldr;
	wxTextCtrl* m_ppi_text;
	int m_res_mode;
    wxStaticText *m_res_mode_text;
	wxComboBox *m_res_mode_combo;

	//bottom bar///////////////////////////////////////////////////
	wxPanel* m_panel_2;
	wxCheckBox *m_rot_link_chk;
	wxButton *m_rot_reset_btn;
	wxSpinButton* m_x_rot_spin;
	wxSlider *m_x_rot_sldr;
	wxTextCtrl *m_x_rot_text;
	wxSlider *m_y_rot_sldr;
	wxTextCtrl *m_y_rot_text;
	wxSpinButton* m_y_rot_spin;
	wxSlider *m_z_rot_sldr;
	wxTextCtrl *m_z_rot_text;
	wxSpinButton* m_z_rot_spin;
	wxCheckBox* m_rot_lock_chk;
	wxButton *m_default_btn;

	//left bar///////////////////////////////////////////////////
	wxPanel* m_panel_3;
	wxCheckBox *m_depth_atten_chk;
	wxSlider *m_depth_atten_factor_sldr;
	wxButton *m_depth_atten_reset_btn;
	wxTextCtrl *m_depth_atten_factor_text;

	//right bar///////////////////////////////////////////////////
	wxPanel* m_panel_4;
	wxButton *m_center_btn;
	wxButton *m_scale_121_btn;
	wxSlider *m_scale_factor_sldr;
	wxTextCtrl *m_scale_factor_text;
	wxButton *m_scale_reset_btn;
	wxSpinButton* m_scale_factor_spin;

	//capture////////////////////////////////////////////////////
	static int m_cap_resx;
	static int m_cap_resy;
	static int m_cap_orgresx;
	static int m_cap_orgresy;
	static int m_cap_dispresx;
	static int m_cap_dispresy;
	static wxTextCtrl *m_cap_w_txt;
	static wxTextCtrl *m_cap_h_txt;

	//draw clip
	bool m_draw_clip;

	bool m_use_dft_settings;
	double m_dft_x_rot;
	double m_dft_y_rot;
	double m_dft_z_rot;
	double m_dft_depth_atten_factor;
	double m_dft_scale_factor;

	wxTimer *m_idleTimer;
    
    wxButton *m_legend_btn;
    LegendListCtrl *m_legend_list;

private:
	//called when updated from bars
	void CreateBar();
	//load default settings from file
	void LoadSettings();

	//update
	void UpdateView(bool ui_update=true);

	//bar top
	void OnVolumeMethodCheck(wxCommandEvent& event);
	void OnCh1Check(wxCommandEvent &event);
	void OnChEmbedCheck(wxCommandEvent &event);
	static wxWindow* CreateExtraCaptureControl(wxWindow* parent);
	void OnCapture(wxCommandEvent& event);
	void OnBgColorChange(wxColourPickerEvent& event);
	void OnCamCtrCheck(wxCommandEvent& event);
	void OnFpsCheck(wxCommandEvent& event);
	void OnLegendCheck(wxCommandEvent& event);
	void OnIntpCheck(wxCommandEvent& event);
	void OnSearchCheck(wxCommandEvent& event);
	void OnAovSldrIdle(wxTimerEvent& event);
	void OnAovChange(wxScrollEvent& event);
	void OnAovText(wxCommandEvent &event);
	void OnFreeChk(wxCommandEvent& event);
	void OnPPIChange(wxScrollEvent& event);
	void OnPPIEdit(wxCommandEvent &event);
	void OnResModesCombo(wxCommandEvent &event);
	//bar left
	void OnDepthAttenCheck(wxCommandEvent& event);
	void OnDepthAttenFactorChange(wxScrollEvent& event);
	void OnDepthAttenFactorEdit(wxCommandEvent& event);
	void OnDepthAttenReset(wxCommandEvent &event);
	//bar right
	void OnCenter(wxCommandEvent &event);
	void OnScale121(wxCommandEvent &event);
	void OnScaleFactorChange(wxScrollEvent& event);
	void OnScaleFactorEdit(wxCommandEvent& event);
	void OnScaleReset(wxCommandEvent &event);
	void OnScaleFactorSpinUp(wxSpinEvent& event);
	void OnScaleFactorSpinDown(wxSpinEvent& event);
	//bar bottom
	void OnRotLink(wxCommandEvent &event);
	void OnRotReset(wxCommandEvent &event);
	void OnValueEdit(wxCommandEvent& event);
	void OnXRotScroll(wxScrollEvent &event);
	void OnYRotScroll(wxScrollEvent &event);
	void OnZRotScroll(wxScrollEvent &event);
	void OnRotLockCheck(wxCommandEvent& event);
	//spin buttons
	void OnXRotSpinUp(wxSpinEvent& event);
	void OnXRotSpinDown(wxSpinEvent& event);
	void OnYRotSpinUp(wxSpinEvent& event);
	void OnYRotSpinDown(wxSpinEvent& event);
	void OnZRotSpinUp(wxSpinEvent& event);
	void OnZRotSpinDown(wxSpinEvent& event);

	//capture
	void OnCapWidthTextChange(wxCommandEvent &event);
	void OnCapHeightTextChange(wxCommandEvent &event);
	void OnSetOrgResButton(wxCommandEvent &event);
	void OnSetDispResButton(wxCommandEvent &event);
    
    void OnLegendButton(wxCommandEvent &event);
    void OnLegendList(wxCommandEvent &event);

	void OnSaveDefault(wxCommandEvent &event);

	void OnKeyDown(wxKeyEvent& event);

	DECLARE_EVENT_TABLE();

};

#endif//_VRENDERVIEW_H_
