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
//#include <GL/glew.h>
#include "compatibility.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string.h>
#include <tiffio.h>
#include "FLIVR/BBox.h"
#include "FLIVR/Color.h"
#include "FLIVR/Point.h"
#include "FLIVR/MeshRenderer.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/TextureBrick.h"
#include <wx/wfstream.h>
#include <wx/fileconf.h>
#include "Formats/base_reader.h"
#include "Formats/oib_reader.h"
#include "Formats/oif_reader.h"
#include "Formats/nrrd_reader.h"
#include "Formats/tif_reader.h"
#include "Formats/nrrd_writer.h"
#include "Formats/nd2_reader.h"
#include "Formats/tif_writer.h"
#include "Formats/msk_reader.h"
#include "Formats/msk_writer.h"
#include "Formats/lsm_reader.h"
#include "Formats/czi_reader.h"
#include "Formats/lbl_reader.h"
#include "Formats/pvxml_reader.h"
#include "Formats/brkxml_reader.h"
#include "Formats/h5j_reader.h"
#include "Formats/swc_reader.h"
#include "Formats/ply_reader.h"
#include "Formats/v3dpbd_reader.h"

#include "Tracking/TrackMap.h"
#include "DatabaseDlg.h"

#include <curl/curl.h>
#include <boost/property_tree/ptree.hpp>

#include "DLLExport.h"

#ifndef _DATAMANAGER_H_
#define _DATAMANAGER_H_

using namespace std;
using namespace FLIVR;

#define DATA_VOLUME			1
#define DATA_MESH			2
#define DATA_ANNOTATIONS	3

#define LOAD_TYPE_NRRD		1
#define LOAD_TYPE_TIFF		2
#define LOAD_TYPE_OIB		3
#define LOAD_TYPE_OIF		4
#define LOAD_TYPE_LSM		5
#define LOAD_TYPE_PVXML		6
#define LOAD_TYPE_BRKXML	7
#define LOAD_TYPE_H5J		8
#define LOAD_TYPE_V3DPBD	9
#define LOAD_TYPE_IDI       10
#define LOAD_TYPE_CZI       11
#define LOAD_TYPE_ND2       12


class EXPORT_API TreeLayer
{
public:
	TreeLayer();
	~TreeLayer();

	int IsA()
	{
		return type;
	}
	wxString GetName()
	{
		return m_name;
	}
	void SetName(wxString name)
	{
		m_name = name;
	}
	unsigned int Id()
	{
		return m_id;
	}
	void Id(unsigned int id)
	{
		m_id = id;
	}

	//layer adjustment
	//gamma
	const Color GetGamma()
	{return m_gamma;}
	void SetGamma(Color gamma)
	{m_gamma = gamma;}
	//brightness
	const Color GetBrightness()
	{return m_brightness;}
	void SetBrightness(Color brightness)
	{m_brightness = brightness;}
	//hdr settings
	const Color GetHdr()
	{return m_hdr;}
	void SetHdr(Color hdr)
	{m_hdr = hdr;}
    //levels settings
    const Color GetLevels()
    {return m_levels;}
    void SetLevels(Color levels)
    {m_levels = levels;}
	//sync values
	bool GetSyncR()
	{return m_sync_r;}
	void SetSyncR(bool sync_r)
	{m_sync_r = sync_r;}
	bool GetSyncG()
	{return m_sync_g;}
	void SetSyncG(bool sync_g)
	{m_sync_g = sync_g;}
	bool GetSyncB()
	{return m_sync_b;}
	void SetSyncB(bool sync_b)
	{m_sync_b = sync_b;}
    
    wxString GetMetadataPath()
    {return m_metadata;}
    void SetMetadataPath(wxString mpath)
    {m_metadata = mpath;}

	//randomize color
	virtual void RandomizeColor() {}

	//soft threshold
	static void SetSoftThreshsold(double val)
	{m_sw = val;}

	//associated layer
	TreeLayer* GetAssociated()
	{return m_associated;}
	void SetAssociated(TreeLayer* layer)
	{m_associated = layer;}

protected:
	int type;//-1:invalid, 2:volume, 3:mesh, 4:annotations, 5:group, 6:mesh group, 7:ruler, 8:traces
	wxString m_name;
	unsigned int m_id;
    
    wxString m_metadata;

	//layer adjustment
	Color m_gamma;
	Color m_brightness;
	Color m_hdr;
    Color m_levels;
	bool m_sync_r;
	bool m_sync_g;
	bool m_sync_b;

	//soft threshold
	static double m_sw;

	//associated layer
	TreeLayer* m_associated;
};

struct VD_Landmark
{
	wstring name;
	double x, y, z;
	double spcx, spcy, spcz;
};

class DataManager;
class VolumeLoader;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define MESH_COLOR_AMB	1
#define MESH_COLOR_DIFF	2
#define MESH_COLOR_SPEC	3
#define MESH_FLOAT_SHN	4
#define MESH_FLOAT_ALPHA	5

class Annotations;
class EXPORT_API MeshData : public TreeLayer
{
public:
	MeshData();
	virtual ~MeshData();

	wxString GetPath();
	BBox GetBounds();
	void SetBounds(BBox b);
	GLMmodel* GetMesh();
	void SetDisp(bool disp);
	void ToggleDisp();
	bool GetDisp();
	void SetDrawBounds(bool draw);
	void ToggleDrawBounds();
	bool GetDrawBounds();

	//data management
	int Load(wxString &filename);
	int Load(GLMmodel* mesh);
	void Save(wxString &filename);

	static MeshData* DeepCopy(MeshData &copy, bool use_default_settings=false, DataManager *d_manager=NULL);

	//MR
	MeshRenderer* GetMR();

	//draw
	void SetMatrices(glm::mat4 &mv_mat, glm::mat4 &proj_mat);
	void Draw(const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf, int peel);
	void DrawBounds(const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf);
	void DrawInt(unsigned int name, const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf, VkRect2D scissor = {0,0,0,0}, bool segment = false);

	//lighting
	void SetLighting(bool bVal);
	bool GetLighting();
	void SetFog(bool bVal, double fog_intensity, double fog_start, double fog_end, Color fog_col);
	bool GetFog();
	void SetMaterial(Color& amb, Color& diff, Color& spec, 
		double shine = 30.0, double alpha = 1.0);
	void SetColor(Color &color, int type);
	void SetFloat(double &value, int type);
	void GetMaterial(Color& amb, Color& diff, Color& spec,
		double& shine, double& alpha);
	bool IsTransp();
	//shadow
	void SetShadow(bool bVal);
	bool GetShadow();
	void SetShadowParams(double val);
	void GetShadowParams(double &val);
    
    void SetThreshold(float th)
    {
        if (m_mr) m_mr->set_threshold(th);
    }
    float GetThreshold()
    {
        return m_mr ? m_mr->get_threshold() : 0.0f;
    }

	void SetTranslation(double x, double y, double z);
	void GetTranslation(double &x, double &y, double &z);
	void SetRotation(double x, double y, double z);
	void GetRotation(double &x, double &y, double &z);
	void SetScaling(double x, double y, double z);
	void GetScaling(double &x, double &y, double &z);

	//randomize color
	void RandomizeColor();

	//shown in legend
	void SetLegend(bool val);
	bool GetLegend();

	//size limiter
	void SetLimit(bool bVal);
	bool GetLimit();
	void SetLimitNumer(int val);
	int GetLimitNumber();

	wstring GetInfo(){ return m_info; };

	bool isSWC(){ return m_swc; }
	void SetRadScale(double rs)
	{
		if (m_r_scale != rs)
		{
			m_r_scale = rs;
			UpdateModelSWC();
		}
	}
	bool UpdateModelSWC();
	double GetRadScale(){ return m_r_scale; }

	void SetClipDistance(int distx, int disty, int distz);
	void GetClipDistance(int &distx, int &disty, int &distz);

	void SetDepthTex(const std::shared_ptr<vks::VTexture>& depth_tex)
	{
		if (m_mr) m_mr->set_depth_tex(depth_tex);
	}
	std::shared_ptr<vks::VTexture> GetDepthTex()
	{
		return m_mr ? m_mr->m_depth_tex : std::shared_ptr<vks::VTexture>();
	}
	void SetDevice(vks::VulkanDevice* device)
	{
		if (m_mr) m_mr->set_device(device);
	}
	vks::VulkanDevice* GetDevice()
	{
		return m_mr ? m_mr->get_device() : nullptr;
	}

	void RecalcBounds();
    
    void SetExtraVertexData(float* data)
    {
        if (m_mr) m_mr->set_extra_vertex_data(data);
        m_extra_vertex_data = data;
    }
    
    void SetExtraVertexData(std::vector<float>* data_vec)
    {
        if (data_vec && data_vec->size() > 0)
        {
            float* data = new float[data_vec->size()];
            memcpy(data, data_vec->data(), sizeof(float)*data_vec->size());
            if (m_mr) m_mr->set_extra_vertex_data(data);
            m_extra_vertex_data = data;
        }
    }
    
    void SetSWCSubdivLevel(int lv)
    {
        if (m_subdiv >= 0)
            m_subdiv = lv;
    }
    int GetSWCSubdivLevel()
    {
        return m_subdiv;
    }
    
    void SetAnnotations(Annotations *anno)
    {
        m_anno = anno;
    }
    Annotations* GetAnnotations()
    {
        return m_anno;
    }
    
    void SetLabelVisibility(bool show)
    {
        m_show_labels = show;
    }
    bool GetLabelVisibility()
    {
        return m_show_labels;
    }

    bool InsideClippingPlanes(Point pos);
    
    wstring GetROIPath(int id)
    {
        if (m_mr)
        {
            if (auto ret = m_mr->get_roi_path(id))
                return *ret;
        }
        return wstring();
    }
    bool GetROIVisibility(int id) { return m_mr ? m_mr->get_roi_visibility(id) : false; }
    bool GetSubmeshLabelState(int id) { return m_mr ? m_mr->get_submesh_label_state(id) : false; }
    void SetSelectedSubmeshID(int id) { if (m_mr) m_mr->set_selected_submesh_id(id); }
    int GetSelectedSubmeshID() { return m_mr ? m_mr->get_selected_submesh_id() : -INT_MAX; }
    void SetSubmeshLabel(int id, const MeshRenderer::SubMeshLabel &label_data) { if (m_mr) m_mr->set_submesh_label(id, label_data); }
    void SetSubmeshLabelState(int id, bool state) { if (m_mr) m_mr->set_submesh_label_state(id, state); }
    void SetSubmeshLabelStateSiblings(int id, bool state) { if (m_mr) m_mr->set_submesh_label_state_siblings(id, state); }
    void SetSubmeshLabelStateByName(wstring name, bool state) { if (m_mr) m_mr->set_submesh_label_state_by_name(name, state); }
    map<int, MeshRenderer::SubMeshLabel>* GetSubmeshLabels() { return m_mr ? m_mr->get_submesh_labels() : NULL; }
    unordered_set<int>* GetActiveLabelSet() { return m_mr ? m_mr->get_active_label_set() : NULL; }
    void UpdateIDPalette() { if (m_mr) m_mr->update_palette(L""); }
    void SelectSegment(int id=-INT_MAX) { if (m_mr) m_mr->select_segment(id); }
    bool isTree() { return m_mr ? m_mr->is_tree() : false; }
    void InitROIGroup() { if (m_mr) m_mr->init_group_ids(); }
    void SetROIStateTraverse(bool state=true, int id=-INT_MAX, int exclude=-INT_MAX) { if (m_mr) m_mr->set_roi_state_traverse(id, state, exclude); }
    void SetROIState(int id, bool state) { if (m_mr) m_mr->set_roi_state(id, state); }
    void SetROIStateSiblings(int id, bool state) { if (m_mr) m_mr->set_roi_state_siblings(id, state); }
    void SetRoiStateByName(int id, bool state) { if (m_mr) m_mr->set_roi_state_by_name(id, state); }
    void ToggleROIState(int id) { if (m_mr) m_mr->toggle_roi_state(id); }
    bool GetROIState(int id)  { return m_mr ? m_mr->get_roi_state(id) : false; }
    void PutROINode(wstring path, wstring name=L""){ if (m_mr) m_mr->put_node(path, name); }
    void PutROINode(wstring path, int id = -1){ if (m_mr) m_mr->put_node(path, id); }
    void SetROIName(wstring name, int id=-1, wstring parent_name=L""){ if (m_mr) m_mr->set_roi_name(name, id, parent_name); }
    int AddROIGroup(wstring parent_name=L"", wstring name=L""){ return m_mr ? m_mr->add_roi_group_node(parent_name, name) : -1; }
    int GetNextSiblingROI(int id){ return m_mr ? m_mr->get_next_sibling_roi(id) : -1; }
    //insert_mode: 0-before dst; 1-after dst; 2-into group
    void MoveROINode(int src_id, int dst_id, int insert_mode=0){ if (m_mr) m_mr->move_roi_node(src_id, dst_id, insert_mode); }
    void EraseROITreeNode(int id=-1){ if (m_mr) m_mr->erase_node(id); }
    void EraseROITreeNode(wstring name){ if (m_mr) m_mr->erase_node(name); }
    wstring GetROIName(int id=-1){ return m_mr ? m_mr->get_roi_name(id) : wstring(); }
    int GetROIid(const wstring &path){ return m_mr ? m_mr->get_roi_id(path) : -INT_MAX; }
    void SetROISel(wstring name, bool select, bool traverse=false){ if (m_mr) m_mr->set_roi_select(name, select, traverse); }
    void SetROISelChildren(wstring name, bool select, bool traverse=false){ if (m_mr) m_mr->set_roi_select_children(name, select, traverse); }
    void SelectAllNamedROI(){ if (m_mr) m_mr->select_all_roi_tree(); }
    void DeselectAllNamedROI(){ if (m_mr) m_mr->deselect_all_roi_tree(); }
    void DeselectAllROI(){ if (m_mr) m_mr->deselect_all_roi(); }
    void ClearROIs(){ if (m_mr) m_mr->clear_roi(); }
    void SetIDColor(unsigned char r, unsigned char g, unsigned char b, bool update_palette=true, const wstring &path = L"")
    {
        if (m_mr) m_mr->set_id_color(r, g, b, update_palette, path);
    }
    void SetIDColor(unsigned char r, unsigned char g, unsigned char b, bool update_palette=true, int id=-1)
    {
        if (m_mr) m_mr->set_id_color(r, g, b, update_palette, id);
    }
    void GetIDColor(unsigned char &r, unsigned char &g, unsigned char &b, const wstring &path)
    {
        if (m_mr) m_mr->get_id_color(r, g, b, path);
    }
    void GetIDColor(unsigned char &r, unsigned char &g, unsigned char &b, int id=-1)
    {
        if (m_mr) m_mr->get_id_color(r, g, b, id);
    }
    void GetRenderedIDColor(unsigned char &r, unsigned char &g, unsigned char &b, int id=-1)
    {
        if (m_mr) m_mr->get_rendered_id_color(r, g, b, id);
    }
    bool isSelID(int id){ return m_mr ? m_mr->is_sel_id(id) : false; }
    void AddSelID(int id){ if (m_mr) m_mr->add_sel_id(id); }
    void DelSelID(int id){ if (m_mr) m_mr->del_sel_id(id); }
    int GetEditSelID(){ return m_mr ? m_mr->get_edit_sel_id() : -1; }
    void SetEditSelID(int id){ if (m_mr) m_mr->set_edit_sel_id(id); }
    void ClearSelIDs(){ if (m_mr) m_mr->clear_sel_ids(); }
    void SetIDColDispMode(int mode){ if (m_mr) m_mr->update_palette(mode); }
    int GetIDColDispMode(){ return m_mr ? m_mr->get_roi_disp_mode() : 0; }
    boost::property_tree::wptree *getROITree(int id=-INT_MAX){ return m_mr ? m_mr->get_roi_tree(id) : NULL; };
    wstring ExportROITree(){ return m_mr ? m_mr->export_roi_tree() : wstring(); }
    string ExportSelIDs(){ return m_mr ? m_mr->exprot_selected_roi_ids() : string(); }
    void ImportROITree(const wstring &tree){ if (m_mr) m_mr->import_roi_tree(tree); }
    void ImportROITreeXML(const wstring &filepath){ if (m_mr) m_mr->import_roi_tree_xml(filepath); }
    void ImportSelIDs(const string &sel_ids_str){ if (m_mr) m_mr->import_selected_ids(sel_ids_str); }


private:
	//wxString m_name;
	wxString m_data_path;
	GLMmodel* m_data;
    float* m_extra_vertex_data;
	MeshRenderer *m_mr;
	BBox m_bounds;
	Point m_center;

	bool m_disp;
	bool m_draw_bounds;

	//lighting
	bool m_light;
	bool m_fog;
	Color m_mat_amb;
	Color m_mat_diff;
	Color m_mat_spec;
	double m_mat_shine;
	double m_mat_alpha;
	//shadow
	bool m_shadow;
	double m_shadow_darkness;
	//size limiter
	bool m_enable_limit;
	int m_limit;

	double m_trans[3];
	double m_rot[3];
	double m_scale[3];

	//legend
	bool m_legend;

	bool m_swc;
	double m_r_scale;
	double m_def_r;
	int m_subdiv;
	SWCReader *m_swc_reader;
    PLYReader *m_ply_reader;
    
    Annotations *m_anno;
    bool m_show_labels;

	wstring m_info;

	int m_clip_dist_x;
	int m_clip_dist_y;
	int m_clip_dist_z;
};

/////////////////////////////////////////////////////////////////////
class EXPORT_API VolumeData : public TreeLayer
{
public:
	VolumeData();
	VolumeData(VolumeData& copy);
	virtual ~VolumeData();

	static VolumeData* DeepCopy(VolumeData& copy, bool use_default_settings = false, DataManager* d_manager = NULL);

	//duplication
	bool GetDup();
	//increase duplicate counter
	void IncDupCounter();

	//data related
	//reader
	void SetReader(BaseReader* reader) { m_reader = reader; }
	BaseReader* GetReader() { return m_reader; }
	//compression
	void SetCompression(bool compression);
	bool GetCompression();
	//skip brick
	void SetSkipBrick(bool skip);
	bool GetSkipBrick();
	//load
	int Load(const std::shared_ptr<VL_Nrrd>& data, const wxString& name, const wxString& path, BRKXMLReader* breader = NULL);
	int Replace(const std::shared_ptr<VL_Nrrd>& data, bool del_tex);
	int Replace(VolumeData* data);
	std::shared_ptr<VL_Nrrd> GetVolume(bool ret);
	//empty data
	void AddEmptyData(int bits,
		int nx, int ny, int nz,
		double spcx, double spcy, double spcz);
	//load mask
	bool LoadMask(const std::shared_ptr<VL_Nrrd>& mask);
	void DeleteMask();
	std::shared_ptr<VL_Nrrd> GetMask(bool ret);
	//empty mask
	void AddEmptyMask();
	//load label
	void LoadLabel(const std::shared_ptr<VL_Nrrd>& label);
	void DeleteLabel();
	std::shared_ptr<VL_Nrrd> GetLabel(bool ret);
	//empty label
	//load stroke
	void LoadStroke(const std::shared_ptr<VL_Nrrd>& stroke);
	void DeleteStroke();
	std::shared_ptr<VL_Nrrd> GetStroke(bool ret);
	void AddEmptyStroke();
	//mode: 0-zeros;1-ordered; 2-shuffled
	void AddEmptyLabel(int mode = 0);
	bool SearchLabel(unsigned int label);

	//save
	double GetOriginalValue(int i, int j, int k, bool normalize = true);
	double GetTransferedValue(int i, int j, int k);
	int GetLabellValue(int i, int j, int k);
	void Save(wxString& filename, int mode = 0, bool bake = false, bool compress = false, bool save_msk = true, bool save_label = true, VolumeLoader* vl = NULL, bool crop = false, int lv = 0);
	void ExportMask(wxString& filename);
	void ImportMask(wxString& filename);
	void ExportEachSegment(wxString dir, const std::shared_ptr<VL_Nrrd>& label_nrrd = nullptr, int mode = 2, bool compress = true);

	//volumerenderer
	VolumeRenderer* GetVR();
	//texture
	Texture* GetTexture();
	void SetTexture();

	//bounding box
	BBox GetBounds();
	//path
	void SetPath(wxString path);
	wxString GetPath();
	//multi-channel
	void SetCurChannel(int chan);
	int GetCurChannel();
	//time sequence
	void SetCurTime(int time);
	int GetCurTime();

	//draw volume
	void SetMatrices(glm::mat4& mv_mat, glm::mat4& proj_mat, glm::mat4& tex_mat);
	void Draw(
		std::unique_ptr<vks::VFrameBuffer>& framebuf,
		bool clear_framebuf,
		bool otho = false,
		bool intactive = false,
		double zoom = 1.0,
		double sampling_frq_fac = -1.0,
		VkClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f },
		Texture* ext_msk = NULL,
		Texture* ext_lbl = NULL
	);
	void DrawBounds();
	//draw mask (create the mask)
	//type: 0-initial; 1-diffusion-based growing
	//paint_mode: 1-select; 2-append; 3-erase; 4-diffuse; 5-flood; 6-clear
	//hr_mode (hidden removal): 0-none; 1-ortho; 2-persp
	void DrawMask(int type, int paint_mode, int hr_mode,
		double ini_thresh, double gm_falloff, double scl_falloff, double scl_translate,
		double w2d, double bins, bool ortho = false, Texture* ext_msk = NULL, bool clear_msk_cache = true, bool use_absolute_value = false, bool save_stroke = true);
	void DrawMaskThreshold(float th, bool ortho = false, bool use_absolute_value = false);
	void DrawMaskDSLT(int type, int paint_mode, int hr_mode,
		double ini_thresh, double gm_falloff, double scl_falloff, double scl_translate,
		double w2d, double bins, int dslt_r, int dslt_q, double dslt_c, bool ortho = false, bool use_absolute_value = false);
	//draw label (create the label)
	//type: 0-initialize; 1-maximum intensity filtering
	//mode: 0-normal; 1-posterized, 2-copy values
	void DrawLabel(int type, int mode, double thresh, double gm_falloff);

	//calculation
	void Calculate(int type, VolumeData* vd_a, VolumeData* vd_b, VolumeData* vd_c = NULL);

	//set 2d mask for segmentation
	void Set2dMask(std::shared_ptr<vks::VTexture>& mask);
	//set 2d weight map for segmentation
	void Set2DWeight(std::shared_ptr<vks::VTexture>& weight1, std::shared_ptr<vks::VTexture>& weight2);
	//set 2d depth map for rendering shadows
	void Set2dDmap(std::shared_ptr<vks::VTexture>& dmap);

	//properties
	//transfer function
	void Set3DGamma(double dVal);
	double Get3DGamma();
	void SetBoundary(double dVal);
	double GetBoundary();
	void SetOffset(double dVal);
	double GetOffset();
	void SetLeftThresh(double dVal);
	double GetLeftThresh();
	void SetRightThresh(double dVal);
	double GetRightThresh();
	void SetColor(const Color& color, bool update_hsv = true);
	Color GetColor();
	void SetMaskColor(Color& color, bool set = true);
	Color GetMaskColor();
	bool GetMaskColorSet();
	void ResetMaskColorSet();
	void SetMaskAlpha(double alpha);
	double GetMaskAlpha();
	Color SetLuminance(double dVal);
	double GetLuminance();
	void SetAlpha(double alpha);
	double GetAlpha();
	void SetEnableAlpha(bool mode);
	bool GetEnableAlpha();
	void SetHSV(double hue = -1, double sat = -1, double val = -1);
	void GetHSV(double& hue, double& sat, double& val);

	//mask threshold
	void SetMaskThreshold(double thresh);
	void SetUseMaskThreshold(bool mode);

	//shading
	void SetShading(bool bVal);
	bool GetShading();
	void SetMaterial(double amb, double diff, double spec, double shine);
	void GetMaterial(double& amb, double& diff, double& spec, double& shine);
	double GetLowShading();
	void SetLowShading(double dVal);
	void SetHiShading(double dVal);
	//shadow
	void SetShadow(bool bVal);
	bool GetShadow();
	void SetShadowParams(double val);
	void GetShadowParams(double& val);
	//sample rate
	void SetSampleRate(double rate);
	double GetSampleRate();

	//colormap mode
	void SetColormapMode(int mode);
	int GetColormapMode();
	void SetColormapDisp(bool disp);
	bool GetColormapDisp();
	void SetColormapValues(double low, double high);
	void GetColormapValues(double& low, double& high);
	void SetColormap(int value);
	void SetColormapProj(int value);
	int GetColormap();
	int GetColormapProj();

	//resolution  scaling and spacing
	void GetResolution(int& res_x, int& res_y, int& res_z);
	void SetScalings(double sclx, double scly, double sclz);
	void GetScalings(double& sclx, double& scly, double& sclz);
	void SetSpacings(double spcx, double spcy, double spcz);
	void GetSpacings(double& spcx, double& spcy, double& spcz, int lv = -1);
	void SetBaseSpacings(double spcx, double spcy, double spcz);
	void GetBaseSpacings(double& spcx, double& spcy, double& spcz);
	void SetSpacingScales(double s_spcx, double s_spcy, double s_spcz);
	void GetSpacingScales(double& s_spcx, double& s_spcy, double& s_spcz);
	void SetLevel(int lv);
	int GetLevel();
	int GetLevelNum();
	void GetFileSpacings(double& spcx, double& spcy, double& spcz);
	//read resolutions from file
	void SetSpcFromFile(bool val = true) { m_spc_from_file = val; }
	bool GetSpcFromFile() { return m_spc_from_file; }

	//display controls
	void SetDisp(bool disp);
	bool GetDisp();
	void ToggleDisp();
	//bounding box
	void SetDrawBounds(bool draw);
	bool GetDrawBounds();
	void ToggleDrawBounds();
	//wirefraem mode
	void SetWireframe(bool val);

	//MIP & normal modes
	void SetMode(int mode);
	int GetMode();
	void RestoreMode();
	//stream modes
	void SetStreamMode(int mode) { m_stream_mode = mode; }
	int GetStreamMode() { return m_stream_mode; }

	//invert
	void SetInvert(bool mode);
	bool GetInvert();

	//mask mode
	void SetMaskMode(int mode);
	int GetMaskMode();
	bool isActiveMask();
	bool isActiveLabel();

	//noise reduction
	void SetNR(bool val);
	bool GetNR();

	//blend mode
	void SetBlendMode(int mode);
	int GetBlendMode();

	//scalar value info
	double GetScalarScale() { return m_scalar_scale; }
	void SetScalarScale(double val) { m_scalar_scale = val; if (m_vr) m_vr->set_scalar_scale(val); }
	double GetGMScale() { return m_gm_scale; }
	void SetGMScale(double val) { m_gm_scale = val; if (m_vr) m_vr->set_gm_scale(val); }
	double GetMaxValue() { return m_max_value; }
	void SetMaxValue(double val) { m_max_value = val; }

	//clip distance
	void SetClipDistance(int distx, int disty, int distz);
	void GetClipDistance(int& distx, int& disty, int& distz);

	//randomize color
	void RandomizeColor();
	//legend
	void SetLegend(bool val);
	bool GetLegend();
	//interpolate
	void SetInterpolate(bool val);
	bool GetInterpolate();

	//number of valid bricks
	void SetBrickNum(int num) { m_brick_num = num; }
	int GetBrickNum() { return m_brick_num; }

	//added by takashi
	vector<AnnotationDB> GetAnnotation() { return m_annotation; }
	void SetAnnotation(vector<AnnotationDB> ann) { m_annotation = ann; }

	void GetLandmark(int index, VD_Landmark& vdl) { if (0 <= index && index < m_landmarks.size()) vdl = m_landmarks[index]; }
	int GetLandmarkNum() { return m_landmarks.size(); }
	void GetMetadataID(wstring& mid) { mid = m_metadata_id; }

	VolumeData* CopyLevel(int lv = -1);
	bool isBrxml();

	void SetFog(bool use_fog, double fog_intensity, double fog_start, double fog_end, Color fog_col);

	void GenAllROINames() { if (m_vr) m_vr->gen_all_roi_names(); }
	std::unordered_set<int> GetSelIDs() { return m_vr ? m_vr->get_sel_ids() : std::unordered_set<int>(); }
	void SetSelIDs(const std::unordered_set<int>& ids) { if (m_vr) m_vr->set_sel_ids(ids); }
	void SetROIName(wstring name, int id = -1, wstring parent_name = L"") { if (m_vr) m_vr->set_roi_name(name, id, parent_name); }
	int AddROIGroup(wstring parent_name = L"", wstring name = L"") { return m_vr ? m_vr->add_roi_group_node(parent_name, name) : -1; }
	int GetNextSiblingROI(int id) { return m_vr ? m_vr->get_next_sibling_roi(id) : -1; }
	//insert_mode: 0-before dst; 1-after dst; 2-into group
	void MoveROINode(int src_id, int dst_id, int insert_mode = 0) { if (m_vr) m_vr->move_roi_node(src_id, dst_id, insert_mode); }
	void EraseROITreeNode(int id = -1) { if (m_vr) m_vr->erase_node(id); }
	void EraseROITreeNode(wstring name) { if (m_vr) m_vr->erase_node(name); }
	wstring GetROIName(int id = -1) { return m_vr ? m_vr->get_roi_name(id) : wstring(); }
	int GetROIid(wstring name) { return m_vr ? m_vr->get_roi_id(name) : -1; }
	void SetROISel(wstring name, bool select, bool traverse = false) { if (m_vr) m_vr->set_roi_select(name, select, traverse); }
	void SetROISelChildren(wstring name, bool select, bool traverse = false) { if (m_vr) m_vr->set_roi_select_children(name, select, traverse); }
	void SelectAllNamedROI() { if (m_vr) m_vr->select_all_roi_tree(); }
	void DeselectAllNamedROI() { if (m_vr) m_vr->deselect_all_roi_tree(); }
	void DeselectAllROI() { if (m_vr) m_vr->deselect_all_roi(); }
	void ClearROIs() { if (m_vr) m_vr->clear_roi(); }
	void SetIDColor(unsigned char r, unsigned char g, unsigned char b, bool update_palette = true, int id = -1)
	{
		if (m_vr) m_vr->set_id_color(r, g, b, update_palette, id);
	}
	void GetIDColor(unsigned char& r, unsigned char& g, unsigned char& b, int id = -1)
	{
		if (m_vr) m_vr->get_id_color(r, g, b, id);
	}
	void GetRenderedIDColor(unsigned char& r, unsigned char& g, unsigned char& b, int id = -1)
	{
		if (m_vr) m_vr->get_rendered_id_color(r, g, b, id);
	}
	bool isSelID(int id) { return m_vr ? m_vr->is_sel_id(id) : false; }
	void AddSelID(int id) { if (m_vr) m_vr->add_sel_id(id); }
	void DelSelID(int id) { if (m_vr) m_vr->del_sel_id(id); }
	int GetEditSelID() { return m_vr ? m_vr->get_edit_sel_id() : -1; }
	void SetEditSelID(int id) { if (m_vr) m_vr->set_edit_sel_id(id); }
	void ClearSelIDs() { if (m_vr) m_vr->clear_sel_ids(); }
	void SetIDColDispMode(int mode) { if (m_vr) m_vr->update_palette(mode); }
	int GetIDColDispMode() { return m_vr ? m_vr->get_roi_disp_mode() : 0; }
	boost::property_tree::wptree* getROITree() { return m_vr ? m_vr->get_roi_tree() : NULL; }
	wstring ExportROITree() { return m_vr ? m_vr->export_roi_tree() : wstring(); }
	string ExportSelIDs() { return m_vr ? m_vr->exprot_selected_roi_ids() : string(); }
	string ExportCombinedROIs() { return m_vr ? m_vr->exprot_combined_roi_ids() : string(); }
	void ImportROITree(const wstring& tree) { if (m_vr) m_vr->import_roi_tree(tree); }
	void ImportROITreeXML(const wstring& filepath) { if (m_vr) m_vr->import_roi_tree_xml(filepath); }
	void ImportSelIDs(const string& sel_ids_str) { if (m_vr) m_vr->import_selected_ids(sel_ids_str); }
	void ImportCombinedROIs(const string& combined_ids_str) { if (m_vr) m_vr->import_combined_ids(combined_ids_str); }
	void CombineSelectedROIs() { if (m_vr) m_vr->combine_selected_rois(); }
	void SplitSelectedROIs() { if (m_vr) m_vr->split_selected_rois(); }
	int IsROICombined(int id) { return m_vr ? m_vr->is_roi_combined(id) : -1; }
	std::map<int, vector<int>>* GetCombinedROIs() { return m_vr ? m_vr->get_combined_rois() : NULL; }

	void FlipHorizontally();
	void FlipVertically();

	VolumeData *GetBrkxmlMask() { return m_brkxml_mask; }
	void SetBrkxmlMask(VolumeData *vd)
	{
		if (m_brkxml_mask) delete m_brkxml_mask;
		m_brkxml_mask = vd;
	}
	int GetLevelBySize(size_t size);

	MeshData *ExportMeshMask();

	int GetMaskLv()
	{
		if (!m_tex) return -1;
		return m_tex->GetMaskLv();
	}
	void SetMaskLv(int lv)
	{
		if (!m_tex) return;
		m_tex->SetMaskLv(lv);
	}

	void SetMaskHideMode(int mode) { if (m_vr) m_vr->set_mask_hide_mode(mode); }
	int GetMaskHideMode() { return m_vr ? m_vr->get_mask_hide_mode() : VOL_MASK_HIDE_NONE; }
    
    void SetSegmentMask(int id, int val) { if (m_vr) m_vr->set_seg_mask(id, val); }
	int GetSegmentMask(int id) { return m_vr ? m_vr->get_seg_mask(id) : false; }
	std::set<int>* GetActiveSegIDs() { return m_vr ? m_vr->get_active_seg_ids() : nullptr; }
    void SetNAMode(bool val) { if (m_vr) m_vr->set_na_mode(val); }
    bool GetNAMode() { return m_vr ? m_vr->get_na_mode() : false; }

	wxString GetSharedLabelName() { return m_shared_lbl_name; }
	void SetSharedLabelName(wxString name) { m_shared_lbl_name = name; }
    
    wxString GetSharedMaskName() { return m_shared_msk_name; }
    void SetSharedMaskName(wxString name) { m_shared_msk_name = name; }

	static Nrrd* NrrdScale(Nrrd* src, size_t dst_datasize, bool interpolation = true);
	static Nrrd* NrrdScale(Nrrd* src, size_t nx, size_t ny, size_t nz, bool interpolation = true);

	size_t GetDataSize()
	{
		size_t ret = 0;
		size_t w, h, d, bd;
		if (m_tex)
		{
			m_tex->get_dimensions(w, h, d);
			bd = m_tex->nb(0);
			ret = w * h * d * bd;
		}

		return ret;
	}
    
    bool InsideClippingPlanes(Point pos);
    
    void SetHighlightingMode(bool val) { if (m_vr) m_vr->set_highlight_mode(val); }
    bool GetHighlightingMode() { return m_vr ? m_vr->get_highlight_mode() : false; }
    void SetHighlightingThreshold(double val) { if (m_vr) m_vr->set_highlight_th(val); }
    double GetHighlightingThreshold() { return m_vr ? m_vr->get_highlight_th() : 0.0; }

private:
	//duplication indicator and counter
	bool m_dup;
	int m_dup_counter;

	wxString m_tex_path;
	BBox m_bounds;
	VolumeRenderer *m_vr;
	Texture *m_tex;

	int m_chan;	//channel index of the original file
	int m_time;	//time index of the original file

	//modes (MIP & normal)
	int m_mode;	//0-normal; 1-MIP; 2-white shading; 3-white mip
	//modes for streaming
	int m_stream_mode;	//0-normal; 1-MIP; 2-shading; 3-shadow

	//mask mode
	int m_mask_mode;	//0-normal, 1-render with mask, 2-render with mask excluded,
						//3-random color with label, 4-random color with label+mask
	bool m_use_mask_threshold;// use mask threshold

	//volume properties
	double m_scalar_scale;
	double m_gm_scale;
	double m_max_value;
	//gamma
	double m_gamma3d;
	double m_gm_thresh;
	double m_offset;
	double m_lo_thresh;
	double m_hi_thresh;
	Color m_color;
	HSVColor m_hsv;
	double m_alpha;
	double m_sample_rate;
	double m_mat_amb;
	double m_mat_diff;
	double m_mat_spec;
	double m_mat_shine;
	//noise reduction
	bool m_noise_rd;
	//shading
	bool m_shading;
	//shadow
	bool m_shadow;
	double m_shadow_darkness;

	//resolution, scaling, spacing
	int m_res_x, m_res_y, m_res_z;
	double m_sclx, m_scly, m_sclz;
	double m_spcx, m_spcy, m_spcz;
	bool m_spc_from_file;

	//display control
	bool m_disp;
	bool m_draw_bounds;
	bool m_test_wiref;

	//color map mode
	int m_colormap_mode;	//0-normal; 1-rainbow; 3-index
	bool m_colormap_disp;	//true/false
	double m_colormap_low_value;
	double m_colormap_hi_value;
	int m_colormap;//index to a colormap
	int m_colormap_proj;//index to a way of projection
	int m_id_col_disp_mode;

	//save the mode for restoring
	int m_saved_mode;

	//blend mode
	int m_blend_mode;	//0: ignore; 1: layered; 2: depth; 3: composite

	//2d mask texture for segmentation
	std::shared_ptr<vks::VTexture> m_2d_mask;
	//2d weight map for segmentation
	std::shared_ptr<vks::VTexture> m_2d_weight1;	//after tone mapping
	std::shared_ptr<vks::VTexture> m_2d_weight2;	//before tone mapping

	//2d depth map texture for rendering shadows
	std::shared_ptr<vks::VTexture> m_2d_dmap;

	//reader
	BaseReader *m_reader;

	//clip distance
	int m_clip_dist_x;
	int m_clip_dist_y;
	int m_clip_dist_z;

	//compression
	bool m_compression;

	//brick skipping
	bool m_skip_brick;

	//shown in legend
	bool m_legend;
	//interpolate
	bool m_interpolate;

	//valid brick number
	int m_brick_num;

	//added by takashi
	vector<AnnotationDB> m_annotation;
	
	vector<VD_Landmark> m_landmarks;
	wstring m_metadata_id;

	VolumeData *m_brkxml_mask;
	int m_mask_lv;

	wxString m_shared_lbl_name;
    wxString m_shared_msk_name;

private:
	//label functions
	void SetOrderedID(unsigned int* val);
	void SetReverseID(unsigned int* val);
	void SetShuffledID(unsigned int* val);
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class EXPORT_API AText
{
public:
	AText();
	AText(const string &str, const Point &pos);
	~AText();

	string GetText();
	Point GetPos();
	void SetText(string str);
	void SetPos(Point pos);
	void SetInfo(string str);

	friend class Annotations;

private:
	string m_txt;
	Point m_pos;
	string m_info;
};

class NrrdScaler
{
public:
	NrrdScaler(Nrrd* src, Nrrd* dst, bool interpolation);
	~NrrdScaler();

	void Run();

	wxCriticalSection m_pThreadCS;
	int m_running_nrrd_scale_th;

private:
	Nrrd* m_src;
	Nrrd* m_dst;
	bool m_interpolation;
};

class NrrdScaleThread : public wxThread
{
public:
	NrrdScaleThread(NrrdScaler* scaler, Nrrd* src, Nrrd* dst, int zst, int zend, bool interpolation);
	~NrrdScaleThread();
protected:
	virtual ExitCode Entry();
	Nrrd* m_src;
	Nrrd* m_dst;
	int m_zst;
	int m_zed;
	bool m_interpolation;
	NrrdScaler* m_scaler;
};


class DataManager;
class EXPORT_API Annotations : public TreeLayer
{
public:
	Annotations();
	virtual ~Annotations();

	//reset counter
	static void ResetID()
	{
		m_num = 0;
	}
	static void SetID(int id)
	{
		m_num = id;
	}
	static int GetID()
	{
		return m_num;
	}

	int GetTextNum();
	string GetTextText(int index);
	Point GetTextPos(int index);
	Point GetTextTransformedPos(int index);
	string GetTextInfo(int index);
	void AddText(std::string str, Point pos, std::string info);
	void SetTransform(Transform *tform);
	void SetVolume(VolumeData* vd);
	VolumeData* GetVolume();
    void SetMesh(MeshData* md);
    MeshData* GetMesh();

	void Clear();

	//display functions
	void SetDisp(bool disp)
	{
		m_disp = disp;
	}
	void ToggleDisp()
	{
		m_disp = !m_disp;
	}
	bool GetDisp()
	{
		return m_disp;
	}

	//memo
	void SetMemo(string &memo);
	string &GetMemo();
	void SetMemoRO(bool ro);
	bool GetMemoRO();

	//save/load
	wxString GetPath();
	int Load(wxString &filename, DataManager* mgr);
	void Save(wxString &filename);

	//info meaning
	wxString GetInfoMeaning();
	void SetInfoMeaning(wxString &str);

	bool InsideClippingPlanes(Point &pos);

	void SetLabel(const std::shared_ptr<VL_Nrrd>& label) {m_label = label;}
	std::shared_ptr<VL_Nrrd> GetLabel() {return m_label;}
    
    void SetAlpha(double alpha) { m_alpha = alpha; }
    double GetAlpha() { return m_alpha; }
    
    void SetMaxScore(double max_score) { m_max_score = max_score; }
    double GetMaxScore() { return m_max_score; }
    
    void SetThreshold(double th)
    {
        if (m_md)
            m_md->SetThreshold(th);
    }
    double GetThreshold() { return m_md ? m_md->GetThreshold() : 0.0; }

private:
	static int m_num;
	vector<AText*> m_alist;
	Transform *m_tform;
	VolumeData* m_vd;
	std::shared_ptr<VL_Nrrd> m_label;
    MeshData* m_md;
    double m_alpha;
    double m_max_score;

	bool m_disp;

	//memo
	string m_memo;
	bool m_memo_ro;//read only

	//on disk
	wxString m_data_path;

	//atext info meaning
	wxString m_info_meaning;

private:
	AText* GetAText(wxString str);
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class EXPORT_API RulerBalloon
{
public:
	
	RulerBalloon(bool visibility, Point point, wxArrayString annotations);

	~RulerBalloon();

	void SetVisibility(bool visibility)
	{
		m_visibility = visibility;
	}
	void SetPoint(Point p)
	{
		m_point = p;
	}
	void SetAnnotations(wxArrayString annotations)
	{
		m_annotations = annotations;
	}

	bool GetVisibility(){return m_visibility;}
	Point GetPoint(){return m_point;}
    wxArrayString GetAnnotations();
	vector<AnnotationDB> GetAnnotationDB(){return m_annotationdb;}

	void SetAnnotationsFromDatabase(vector<AnnotationDB> ann, Point new_p, double spcx = 1.0, double spcy = 1.0, double spcz = 1.0);

private:

	bool m_visibility;
	Point m_point;
	vector<AnnotationDB> m_annotationdb;
	wxArrayString m_annotations;
	vector<CURL *> m_curl;
	vector<string> m_bufs;
};

class EXPORT_API ProfileBin
{
public:
	ProfileBin():
	m_pixels(0), m_accum(0.0) {}
	~ProfileBin() {}
	int m_pixels;
	double m_accum;
};

class EXPORT_API Ruler : public TreeLayer
{
public:
	Ruler();
	virtual ~Ruler();

	//reset counter
	static void ResetID()
	{
		m_num = 0;
	}
	static void SetID(int id)
	{
		m_num = id;
	}
	static int GetID()
	{
		return m_num;
	}

	//data
	int GetNumPoint();
	Point* GetPoint(int index);
	int GetRulerType();
	void SetRulerType(int type);
	bool GetFinished();
	void SetFinished();
	double GetLength();
	double GetLengthObject(double spcx, double spcy, double spcz);
	double GetAngle();
	void Scale(double spcx, double spcy, double spcz);
    
    wxArrayString GetAnnotations(int index, vector<AnnotationDB> annotationdb, double spcx, double spcy, double spcz);

	bool AddPoint(const Point &point);
	void SetTransform(Transform *tform);

	void Clear();
	
	//display functions
	void SetDisp(bool disp)
	{
		m_disp = disp;
	}
	void ToggleDisp()
	{
		m_disp = !m_disp;
	}
	bool GetDisp()
	{
		return m_disp;
	}

	//time-dependent
	void SetTimeDep(bool time_dep)
	{
		m_time_dep = time_dep;
	}
	bool GetTimeDep()
	{
		return m_time_dep;
	}
	void SetTime(int time)
	{
		m_time = time;
	}
	int GetTime()
	{
		return m_time;
	}


	wxString GetNameDisp()
	{
		return m_name_disp;
	}
	void SetNameDisp(wxString name_disp)
	{
		m_name_disp = name_disp;
	}
	wxString GetDesc()
	{
		return m_desc;
	}
	void SetDesc(wxString desc)
	{
		m_desc = desc;
	}


	//extra info
	void AddInfoNames(wxString &str)
	{
		m_info_names += str;
	}
	void SetInfoNames(wxString &str)
	{
		m_info_names = str;
	}
	wxString &GetInfoNames()
	{
		return m_info_names;
	}
	void AddInfoValues(wxString &str)
	{
		m_info_values += str;
	}
	void SetInfoValues(wxString &str)
	{
		m_info_values = str;
	}
	wxString &GetInfoValues()
	{
		return m_info_values;
	}
	wxString GetDelInfoValues(wxString del=",");

	//profile
	void SetInfoProfile(wxString &str)
	{
		m_info_profile = str;
	}
	wxString &GetInfoProfile()
	{
		return m_info_profile;
	}
	vector<ProfileBin> *GetProfile()
	{
		return &m_profile;
	}
	void SaveProfile(wxString &filename);

	//color
	void SetColor(Color& color)
	{ m_color = color; m_use_color = true;}
	bool GetUseColor()
	{ return m_use_color; }
	Color &GetColor()
	{ return m_color; }

	//added by takashi
	void SetBalloonVisibility(int p, bool visibility)
	{
		if((p < m_balloons.size() && p >= 0))m_balloons[p].SetVisibility(visibility);
	}
	bool GetBalloonVisibility(int p)
	{
		return (p < m_balloons.size() && p >= 0) ? m_balloons[p].GetVisibility() : false;
	}

private:
	static int m_num;
	int m_ruler_type;	//0: 2 point; 1: multi point; 2:locator; 3: probe; 4: protractor
	bool m_finished;
	vector<Point> m_ruler;
	bool m_disp;
	Transform *m_tform;
	//a profile
	wxString m_info_profile;
	vector<ProfileBin> m_profile;
	//color
	bool m_use_color;
	Color m_color;

	//time-dependent
	bool m_time_dep;
	int m_time;

	wxString m_name_disp;//add by takashi
	wxString m_desc;//add by takashi

	//extra info
	wxString m_info_names;
	wxString m_info_values;

	//added by takashi
	vector<RulerBalloon> m_balloons;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class EXPORT_API TraceGroup : public TreeLayer
{
public:
	TraceGroup();
	virtual ~TraceGroup();

	//reset counter
	static void ResetID()
	{
		m_num = 0;
	}
	static void SetID(int id)
	{
		m_num = id;
	}
	static int GetID()
	{
		return m_num;
	}

	FL::TrackMap &GetTrackMap()
	{
		return m_track_map;
	}

	wxString GetPath() {return m_data_path;}
	void SetCurTime(int time);
	int GetCurTime();
	void SetPrvTime(int time);
	int GetPrvTime();
	//ghost num
	void SetGhostNum(int num) {m_ghost_num = num;}
	int GetGhostNum() {return m_ghost_num;}
	void SetDrawTail(bool draw) {m_draw_tail = draw;}
	bool GetDrawTail() {return m_draw_tail;}
	void SetDrawLead(bool draw) {m_draw_lead = draw;}
	bool GetDrawLead() {return m_draw_lead;}
	//cells size filter
	void SetCellSize(int size) {m_cell_size = size;}
	int GetSizeSize() {return m_cell_size;}

	//get information
	void GetLinkLists(size_t frame,
		FL::VertexList &in_orphan_list,
		FL::VertexList &out_orphan_list,
		FL::VertexList &in_multi_list,
		FL::VertexList &out_multi_list);

	//for selective drawing
	void ClearCellList();
	void UpdateCellList(FL::CellList &cur_sel_list);
	FL::CellList &GetCellList();
	bool FindCell(unsigned int id);

	//modifications
	bool AddCell(FL::pCell &cell, size_t frame);
	bool LinkCells(FL::CellList &list1, FL::CellList &list2,
		size_t frame1, size_t frame2, bool exclusive);
	bool IsolateCells(FL::CellList &list, size_t frame);
	bool UnlinkCells(FL::CellList &list1, FL::CellList &list2,
		size_t frame1, size_t frame2);
	bool CombineCells(FL::pCell &cell, FL::CellList &list,
		size_t frame);
	bool DivideCells(FL::CellList &list, size_t frame);
	bool ReplaceCellID(unsigned int old_id, unsigned int new_id, size_t frame);

	//rulers
	bool GetMappedRulers(FL::RulerList &rulers);

	//i/o
	bool Load(wxString &filename);
	bool Save(wxString &filename);

	//draw
	unsigned int Draw(vector<float> &verts);

	//pattern search
/*	typedef struct
	{
		int div;
		int conv;
	} Patterns;
	//type: 1-diamond; 2-branching
	bool FindPattern(int type, unsigned int id, int time);*/

private:
	static int m_num;
	wxString m_data_path;
	//for selective drawing
	int m_cur_time;
	int m_prv_time;
	int m_ghost_num;
	bool m_draw_tail;
	bool m_draw_lead;
	int m_cell_size;

	FL::TrackMap m_track_map;
	FL::CellList m_cell_list;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class EXPORT_API DataGroup : public TreeLayer
{
public:
	DataGroup();
	virtual ~DataGroup();

	//reset counter
	static void ResetID()
	{
		m_num = 0;
	}
	static void SetID(int id)
	{
		m_num = id;
	}
	static int GetID()
	{
		return m_num;
	}

	int GetVolumeNum()
	{
		return m_vd_list.size();
	}
	VolumeData* GetVolumeData(int index)
	{
		if (index>=0 && index<(int)m_vd_list.size())
			return m_vd_list[index];
		else return 0;
	}
	void InsertVolumeData(int index, VolumeData* vd)
	{
		if (!m_vd_list.empty())
		{
			if (index>-1 && index<(int)m_vd_list.size())
				m_vd_list.insert(m_vd_list.begin()+(index+1), vd);
			else if (index == -1)
				m_vd_list.insert(m_vd_list.begin()+0, vd);
			else
				m_vd_list.push_back(vd);
		}
		else
		{
			m_vd_list.push_back(vd);
		}
	}

	void ReplaceVolumeData(int index, VolumeData *vd)
	{
		if (index>=0 && index<(int)m_vd_list.size())
			m_vd_list[index] = vd;
		ResetSync();
	}

	void RemoveVolumeData(int index)
	{
		if (index>=0 && index<(int)m_vd_list.size())
			m_vd_list.erase(m_vd_list.begin()+index);
		ResetSync();
	}

	//display functions
	void SetDisp(bool disp)
	{
		m_disp = disp;
	}
	void ToggleDisp()
	{
		m_disp = !m_disp;
	}
	bool GetDisp()
	{
		return m_disp;
	}

	//group blend mode
	int GetBlendMode();

	//set gamma to all
	void SetGammaAll(Color &gamma);
	//set brightness to all
	void SetBrightnessAll(Color &brightness);
	//set hdr to all
	void SetHdrAll(Color &hdr);
    //set levels to all
    void SetLevelsAll(Color &levels);
	//set sync to all
	void SetSyncRAll(bool sync_r);
	void SetSyncGAll(bool sync_g);
	void SetSyncBAll(bool sync_b);
	//reset sync
	void ResetSync();

	//volume properties
	void SetEnableAlpha(bool mode);
	void SetAlpha(double dVal);
	void SetSampleRate(double dVal);
	void SetBoundary(double dVal);
	void Set3DGamma(double dVal);
	void SetOffset(double dVal);
	void SetLeftThresh(double dVal);
	void SetRightThresh(double dVal);
	void SetLowShading(double dVal);
	void SetHiShading(double dVal);
	void SetLuminance(double dVal);
	void SetColormapMode(int mode);
	void SetColormapDisp(bool disp);
	void SetColormapValues(double low, double high);
	void SetColormap(int value);
	void SetColormapProj(int value);
	void SetShading(bool shading);
	void SetShadow(bool shadow);
	void SetShadowParams(double val);
	void SetMode(int mode);
	void SetNR(bool val);
    void SetInterpolate(bool mode);
	void SetInvert(bool mode);
    void SetMaskHideMode(int mode);

	//blend mode
	void SetBlendMode(int mode);

	//sync prop
	void SetVolumeSyncProp(bool bVal)
	{
		m_sync_volume_prop = bVal;
	}
	bool GetVolumeSyncProp()
	{
		return m_sync_volume_prop;
	}

	//sync spc
	void SetVolumeSyncSpc(bool bVal)
	{
		m_sync_volume_spc = bVal;
	}
	bool GetVolumeSyncSpc()
	{
		return m_sync_volume_spc;
	}

	//randomize color
	void RandomizeColor();

private:
	static int m_num;
	//wxString m_name;
	vector <VolumeData*> m_vd_list;
	bool m_sync_volume_prop;
	bool m_sync_volume_spc;

	bool m_disp;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class EXPORT_API MeshGroup : public TreeLayer
{
public:
	MeshGroup();
	virtual ~MeshGroup();

	//counter
	static void ResetID()
	{
		m_num = 0;
	}
	static void SetID(int id)
	{
		m_num = id;
	}
	static int GetID()
	{
		return m_num;
	}

	//data
	int GetMeshNum()
	{
		return (int)m_md_list.size();
	}
	MeshData* GetMeshData(int index)
	{
		if (index>=0 && index<(int)m_md_list.size())
			return m_md_list[index];
		else return 0;
	}
	void InsertMeshData(int index, MeshData* md)
	{
		if (m_md_list.size() > 0)
		{
			if (index>-1 && index<(int)m_md_list.size())
				m_md_list.insert(m_md_list.begin()+(index+1), md);
			else if (index == -1)
				m_md_list.insert(m_md_list.begin()+0, md);
		}
		else
		{
			m_md_list.push_back(md);
		}
	}
	void RemoveMeshData(int index)
	{
		if (index>=0 && index<(int)m_md_list.size())
			m_md_list.erase(m_md_list.begin()+index);
	}

	//display functions
	void SetDisp(bool disp)
	{
		m_disp = disp;
	}
	void ToggleDisp()
	{
		m_disp = !m_disp;
	}
	bool GetDisp()
	{
		return m_disp;
	}

	//sync prop
	void SetMeshSyncProp(bool bVal)
	{
		m_sync_mesh_prop = bVal;
	}
	bool GetMeshSyncProp()
	{
		return m_sync_mesh_prop;
	}

	//randomize color
	void RandomizeColor();

private:
	static int m_num;
	vector<MeshData*> m_md_list;
	bool m_sync_mesh_prop;
	bool m_disp;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class EXPORT_API DataManager
{
public:
	DataManager();
	~DataManager();

	void ClearAll();

	//set project path
	//when data and project are moved, use project file's path
	//if data's directory doesn't exist
	void SetProjectPath(wxString path);
	wxString SearchProjectPath(wxString &filename);

	bool DownloadToCurrentDir(wxString &filename);
	//load volume
	int LoadVolumeData(wxString &filename, int type, int ch_num=-1, int t_num=-1, size_t datasize = 0, wxString prefix = wxT(""), wxString metadata = wxT(""));
	//set default
	void SetVolumeDefault(VolumeData* vd);
	//load volume options
	void SetSliceSequence(bool ss) {m_sliceSequence = ss;}
	void SetTimeSequence(bool ts) {m_timeSequence = ts;}
	void SetCompression(bool compression) {m_compression = compression;}
	bool GetCompression(){ return m_compression; }
	void SetSkipBrick(bool skip) {m_skip_brick = skip;}
	void SetTimeId(wxString str) {m_timeId = str;}
	void SetLoadMask(bool load_mask) {m_load_mask = load_mask;}
	void AddVolumeData(VolumeData* vd);
	void AddEmptyVolumeData(wxString name, int bits, int nx, int ny, int nz, double spcx, double spcy, double spcz);
	void AddMeshData(MeshData* md);
	VolumeData* DuplicateVolumeData(VolumeData* vd, bool use_default_settings=false);
	MeshData* DuplicateMeshData(MeshData* vd, bool use_default_settings=false);
	void ReplaceVolumeData(int index, VolumeData *vd);
	void RemoveVolumeData(int index);
	void RemoveVolumeData(const wxString &name);
	void RemoveVolumeDataset(BaseReader *reader, int channel);
	int GetVolumeNum();
	VolumeData* GetVolumeData(int index);
	VolumeData* GetVolumeData(const wxString &name);
	int GetVolumeIndex(wxString &name);
	VolumeData* GetLastVolumeData()
	{
		int num = m_vd_list.size();
		if (num)
			return m_vd_list[num-1];
		else
			return 0;
	};
	int GetLatestVolumeChannelNum() { return m_latest_vols.size(); }
	VolumeData* GetLatestVolumeDataset(int ch)
	{
		for (int i = 0; i < m_latest_vols.size(); i++)
		{
			if (m_latest_vols[i]->GetCurChannel() == ch)
				return m_latest_vols[i];
		}

		return NULL;
	}

	//load mesh
    int LoadMeshData(wxString &filename, wxString prefix = wxT(""));
	int LoadMeshData(GLMmodel* mesh);
	int GetMeshNum();
	MeshData* GetMeshData(int index);
	MeshData* GetMeshData(wxString &name);
	int GetMeshIndex(wxString &name);
	MeshData* GetLastMeshData()
	{
		int num = m_md_list.size();
		if (num)
			return m_md_list[num-1];
		else
			return 0;
	};
	void RemoveMeshData(int index);
	void RemoveMeshData(const wxString &name);

	//annotations
	Annotations* LoadAnnotations(wxString &filename);
	void AddAnnotations(Annotations* ann);
	void RemoveAnnotations(int index);
	void RemoveAnnotations(const wxString &name);
	int GetAnnotationNum();
	Annotations* GetAnnotations(int index);
	Annotations* GetAnnotations(wxString &name);
	int GetAnnotationIndex(wxString &name);
	Annotations* GetLastAnnotations()
	{
		int num = m_annotation_list.size();
		if (num)
			return m_annotation_list[num-1];
		else
			return 0;
	}

	bool CheckNames(const wxString &str);
	bool CheckNames(const wxString &str, int type);
	wxString CheckNewName(const wxString &name, int type);

	//wavelength to color
	void SetWavelengthColor(int c1, int c2, int c3, int c4);
	Color GetWavelengthColor(double wavelength);

	//override voxel size
	void SetOverrideVox(bool val)
	{ m_override_vox = val; }
	bool GetOverrideVox()
	{ return m_override_vox; }

	//flags for pvxml flipping
	void SetPvxmlFlipX(bool flip) {m_pvxml_flip_x = flip;}
	bool GetPvxmlFlipX() {return m_pvxml_flip_x;}
	void SetPvxmlFlipY(bool flip) {m_pvxml_flip_y = flip;}
	bool GetPvxmlFlipY() {return m_pvxml_flip_y;}
    
    void PushReader(BaseReader* reader) { m_reader_list.push_back(reader); }

public:
	//default values
	//volume
	double m_vol_exb;	//extract_boundary
	double m_vol_gam;	//gamma
	double m_vol_of1;	//offset1
	double m_vol_of2;	//offset2
	double m_vol_lth;	//low_thresholding
	double m_vol_hth;	//high_thresholding
	double m_vol_lsh;	//low_shading
	double m_vol_hsh;	//high_shading
	double m_vol_alf;	//alpha
	double m_vol_spr;	//sample_rate
	double m_vol_xsp;	//x_spacing
	double m_vol_ysp;	//y_spacing
	double m_vol_zsp;	//z_spacing
	double m_vol_lum;	//luminance
	int m_vol_cmp;		//colormap
	double m_vol_lcm;	//colormap low value
	double m_vol_hcm;	//colormap high value
	bool m_vol_eap;		//enable alpha
	bool m_vol_esh;		//enable_shading
	bool m_vol_interp;	//enable interpolation
	bool m_vol_inv;		//enable inversion
	bool m_vol_mip;		//enable_mip
	bool m_vol_nrd;		//noise reduction
	bool m_vol_shw;		//enable shadow
	double m_vol_swi;	//shadow intensity

	bool m_vol_test_wiref;		//wireframe mode

	//wavelength to color table
	Color m_vol_wav[4];

	vector<VolumeData*> m_latest_vols;

private:
	vector <VolumeData*> m_vd_list;
	vector <MeshData*> m_md_list;
	vector <BaseReader*> m_reader_list;
	vector <Annotations*> m_annotation_list;

	bool m_use_defaults;

	//slice sequence
	bool m_sliceSequence;
	bool m_timeSequence;
	//compression
	bool m_compression;
	//skip brick
	bool m_skip_brick;
	//time sequence identifier
	wxString m_timeId;
	//load volume mask
	bool m_load_mask;
	//project path
	wxString m_prj_path;
	//override voxel size
	bool m_override_vox;
	//flgs for pvxml flipping
	bool m_pvxml_flip_x;
	bool m_pvxml_flip_y;
};



struct VolumeLoaderData
{
	FileLocInfo *finfo;
	TextureBrick *brick;
	VolumeData *vd;
	unsigned long long datasize;
	int mode;

	int chid;
	int frameid;
};

struct VolumeLoaderImage
{
	VolumeData* vd;
	int chid;
	int frameid;
	std::shared_ptr<VL_Nrrd> vlnrrd;
};

struct VolumeLoaderImageKey
{
	int frameid;
	wstring key;
};

struct VolumeDecompressorData
{
	char *in_data;
	size_t in_size;
	TextureBrick *b;
	FileLocInfo *finfo;
	VolumeData *vd;
	unsigned long long datasize;
	int mode;

	int chid;
	int frameid;
};

class VolumeLoader;

class EXPORT_API VolumeDecompressorThread : public wxThread
{
public:
	VolumeDecompressorThread(VolumeLoader* vl);
	~VolumeDecompressorThread();
protected:
	virtual ExitCode Entry();
	VolumeLoader* m_vl;
};

class EXPORT_API VolumeLoaderThread : public wxThread
{
    public:
		VolumeLoaderThread(VolumeLoader *vl);
		~VolumeLoaderThread();
    protected:
		virtual ExitCode Entry();
        VolumeLoader* m_vl;
};

class EXPORT_API VolumeLoader
{
	public:
		VolumeLoader();
		~VolumeLoader();
		void Queue(VolumeLoaderData brick);
		void ClearQueues();
		void Set(vector<VolumeLoaderData> vld);
		void Abort();
		void StopAll();
		void Join();
		bool Run();
		void SetMaxThreadNum(int num) {m_max_decomp_th = num;}
		void SetFrameReaderMaxThreadNum(int num) { m_frame_reader_max_decomp_th = num; }
		void SetMemoryLimitByte(long long limit) {m_memory_limit = limit;}
		void CleanupLoadedBrick();
		void TryToFreeMemory(long long req=-1);
		void CheckMemoryCache();
		void RemoveAllLoadedBrick();
		void RemoveAllLoadedData();
		void RemoveDataVD(VolumeData *vd);
		void GetPalams(long long &used_mem, int &running_decomp_th, int &queue_num, int &decomp_queue_num);
		void PreloadLevel(VolumeData *vd, int lv, bool lock=false);
		std::shared_ptr<VL_Nrrd> GetLoadedNrrd(VolumeData* vd, int ch, int frame);
		bool IsNrrdLoaded(VolumeData* vd, int ch, int frame);
		bool IsNrrdLoading(VolumeData* vd, int ch, int frame);
		std::wstring GetTimeDataKeyString(VolumeData* vd, int ch, int frame);
		void AddLoadedNrrd(const std::shared_ptr<VL_Nrrd> &nrrd, VolumeData* vd, int ch, int frame);
		//void DeleteLoadedNrrd(Nrrd* nrrd, VolumeData* vd, int ch, int frame);

		int GetMaxThreadNum() { return m_max_decomp_th; }
		int GetFrameReaderMaxThreadNum() { return m_frame_reader_max_decomp_th; }

		long long GetAvailableMemory() { return m_memory_limit - m_used_memory; }
		long long GetMemoryLimitByte() { return m_memory_limit; }
		long long GetUsedMemory() { return m_used_memory; }

		static bool sort_data_dsc(const VolumeLoaderData b1, const VolumeLoaderData b2)
		{ return b2.brick->get_d() > b1.brick->get_d(); }
		static bool sort_data_asc(const VolumeLoaderData b1, const VolumeLoaderData b2)
		{ return b2.brick->get_d() < b1.brick->get_d(); }

		static void setCriticalSection(wxCriticalSection* crtsec) { ms_pThreadCS = crtsec; }

	protected:
		VolumeLoaderThread *m_thread;
		vector<VolumeLoaderData> m_queues;
		vector<VolumeLoaderData> m_queued;
		vector<VolumeDecompressorData> m_decomp_queues;
		vector<VolumeDecompressorThread *> m_decomp_threads;
		unordered_map<TextureBrick*, VolumeLoaderData> m_loaded;
		unordered_map<wstring, std::shared_ptr<VL_Array>> m_memcached_data;
		unordered_set<wstring> m_loading_files;
		unordered_map<wstring, VolumeLoaderImage> m_loaded_files;
		int m_running_decomp_th;
		int m_max_decomp_th;
		int m_frame_reader_max_decomp_th;
		bool m_valid;

		long long m_memory_limit;
		long long m_used_memory;

		static wxCriticalSection* ms_pThreadCS;

		inline void AddLoadedBrick(const VolumeLoaderData &lbd)
		{
			m_loaded[lbd.brick] = lbd;
			m_memcached_data[lbd.finfo->id_string] = lbd.brick->getBrickDataSP();
			m_used_memory += lbd.datasize;
		}

		static bool less_vld_frame(const VolumeLoaderImageKey&d1, const VolumeLoaderImageKey&d2) { return d1.frameid < d2.frameid; }

		friend class VolumeLoaderThread;
		friend class VolumeDecompressorThread;
};


class ProjectDataLoader;

class ProjectDataLoaderQueue
{
public:
    wxString path;
    wxString name;
    wxString metadata;
    int ch;
    int t;
    bool compression;
    bool skip_brick;
    bool slice_seq;
    bool time_seq;
    wxString time_id;
    bool load_mask;
    wxString mskpath;
    wxString lblpath;
    int type;
    
    ProjectDataLoaderQueue(const wxString &_path, int _ch, int _t, wxString _name=wxEmptyString , bool _compression=false, bool _skip_brick=false, bool _slice_seq=false, bool _time_seq=false, wxString _time_id=wxEmptyString, bool _load_mask=true, wxString _mskpath=wxEmptyString, wxString _lblpath=wxEmptyString, wxString _metadata=wxEmptyString)
    {
        path = _path;
        name = _name;
        ch = _ch;
        t = _t;
        compression = _compression;
        skip_brick = _skip_brick;
        slice_seq = _slice_seq;
        time_seq = _time_seq;
        time_id = _time_id;
        load_mask = _load_mask;
        mskpath = _mskpath;
        lblpath = _lblpath;
        metadata = _metadata;
        
        wxString suffix = path.Mid(path.Find('.', true)).MakeLower();
        if (suffix == ".nrrd")
            type = LOAD_TYPE_NRRD;
        else if (suffix == ".tif"||suffix == ".tiff"||suffix == ".zip")
            type = LOAD_TYPE_TIFF;
        else if (suffix == ".oib")
            type = LOAD_TYPE_OIB;
        else if (suffix == ".oif")
            type = LOAD_TYPE_OIF;
        else if (suffix == ".lsm")
            type = LOAD_TYPE_LSM;
        else if (suffix == ".CZI")
            type = LOAD_TYPE_CZI;
        else if (suffix == ".nd2")
            type = LOAD_TYPE_ND2;
       // else if (suffix == ".xml")
       //     type = LOAD_TYPE_PVXML;
        else if (suffix==".vvd" || suffix==".n5" || suffix==".json" || suffix==".n5fs_ch" || suffix==".xml")
            type = LOAD_TYPE_BRKXML;
        else if (suffix == ".h5j")
            type = LOAD_TYPE_H5J;
        else if (suffix == ".v3dpbd")
            type = LOAD_TYPE_V3DPBD;
        else if (suffix == ".idi")
            type = LOAD_TYPE_IDI;
    }
    
    ProjectDataLoaderQueue(const ProjectDataLoaderQueue &copy)
    {
        path = copy.path;
        name = copy.name;
        metadata = copy.metadata;
        ch = copy.ch;
        t = copy.t;
        compression = copy.compression;
        skip_brick = copy.skip_brick;
        slice_seq = copy.slice_seq;
        time_seq = copy.time_seq;
        time_id = copy.time_id;
        load_mask = copy.load_mask;
        mskpath = copy.mskpath;
        lblpath = copy.lblpath;
        type = copy.type;
    }
    
};

class EXPORT_API ProjectDataLoaderThread : public wxThread
{
public:
    ProjectDataLoaderThread(ProjectDataLoader *pdl);
    ~ProjectDataLoaderThread();
    
protected:
    virtual ExitCode Entry();
    ProjectDataLoader *m_pdl;
};

class EXPORT_API ProjectDataLoader
{
public:
    ProjectDataLoader();
    ~ProjectDataLoader();
    void Queue(ProjectDataLoaderQueue path);
    void ClearQueues();
    void Set(vector<ProjectDataLoaderQueue> &queues);
    void Join();
    bool Run();
    void GetPalams(long long &used_mem, int &running_decomp_th, int &queue_num, int &decomp_queue_num);
    void SetMaxThreadNum(int num) {m_max_th = num;}
    int GetMaxThreadNum() { return m_max_th;}
    int GetProgress() { return m_progress; }
    void SetDataManager(DataManager *dm) { m_dm = dm; }
    bool IsRunning() { return m_running_th > 0; }
    
    static void setCriticalSection(wxCriticalSection* crtsec) { ms_pThreadCS = crtsec; }
    
protected:
    vector<ProjectDataLoaderQueue> m_queues;
    vector<ProjectDataLoaderQueue> m_queued;
    DataManager *m_dm;
    int m_running_th;
    int m_max_th;
    int m_queue_count;
    int m_progress;
    
    static wxCriticalSection* ms_pThreadCS;
    
    friend class ProjectDataLoaderThread;
};


class EmptyBlockDetector;

class EmptyBlockDetectorQueue
{
public:
    TextureBrick* b;
    int c;
    shared_ptr<VL_Nrrd> nrrd;
    shared_ptr<VL_Nrrd> msknrrd;
    
    EmptyBlockDetectorQueue(TextureBrick* b_,int c_, const shared_ptr<VL_Nrrd> &nrrd_)
    {
        b = b_;
        c = c_;
        nrrd = nrrd_;
    }
    EmptyBlockDetectorQueue(TextureBrick* b_, int c_, const shared_ptr<VL_Nrrd> &nrrd_, const shared_ptr<VL_Nrrd> &msknrrd_)
    {
        b = b_;
        c = c_;
        nrrd = nrrd_;
        msknrrd = msknrrd_;
    }
    
    EmptyBlockDetectorQueue(const EmptyBlockDetectorQueue &copy)
    {
        b = copy.b;
        c = copy.c;
        nrrd = copy.nrrd;
        msknrrd = copy.msknrrd;
    }
    
};

class EXPORT_API EmptyBlockDetectorThread : public wxThread
{
public:
    EmptyBlockDetectorThread(EmptyBlockDetector *vl);
    ~EmptyBlockDetectorThread();
protected:
    virtual ExitCode Entry();
    EmptyBlockDetector* m_ebd;
};

class EXPORT_API EmptyBlockDetector
{
public:
    EmptyBlockDetector();
    ~EmptyBlockDetector();
    void Queue(EmptyBlockDetectorQueue queue);
    void ClearQueues();
    void Set(vector<EmptyBlockDetectorQueue> &queues);
    void Join();
    bool Run();
    void SetMaxThreadNum(int num) {m_max_th = num;}
    int GetMaxThreadNum() { return m_max_th;}
    int GetProgress() { return m_progress; }
    bool IsRunning() { return m_running_th > 0; }
    
    static void setCriticalSection(wxCriticalSection* crtsec) { ms_pThreadCS = crtsec; }
    
protected:
    vector<EmptyBlockDetectorQueue> m_queues;
    vector<EmptyBlockDetectorQueue> m_queued;
    int m_running_th;
    int m_max_th;
    int m_queue_count;
    int m_progress;
    
    static wxCriticalSection* ms_pThreadCS;
    
    friend class EmptyBlockDetectorThread;
};



#endif//_DATAMANAGER_H_
