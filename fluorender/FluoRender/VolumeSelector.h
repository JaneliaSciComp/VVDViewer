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
#include "DataManager.h"
#include "Core/UICallbacks.h"
//#include <boost/unordered_map.hpp>
#include "DLLExport.h"

#ifndef _VOLUMESELECTOR_H_
#define _VOLUMESELECTOR_H_

//using namespace stdext;

class EXPORT_API VolumeSelector
{
public:
	VolumeSelector();
	~VolumeSelector();

	void SetVolume(VolumeData *vd);
	VolumeData* GetVolume();
	void Set2DMask(const std::shared_ptr<vks::VTexture> &mask);
	void Set2DWeight(const std::shared_ptr<vks::VTexture> &weight1, const std::shared_ptr<vks::VTexture> &weight2);
	void SetProjection(double* mvmat, double *prjmat);
	void SetBrushIteration(int num) {m_iter_num = num;}
	int GetBrushIteration() {return m_iter_num;}
	//set/get brush properties
	void SetBrushIniThresh(double val) {m_ini_thresh = val;}
	double GetBrushIniThresh() {return m_ini_thresh;}
	void SetBrushGmFalloff(double val) {m_gm_falloff = val;}
	double GetBrushGmFalloff() {return m_gm_falloff;}
	void SetBrushSclFalloff(double val) {m_scl_falloff = val;}
	double GetBrushSclFalloff() {return m_scl_falloff;}
	void SetBrushSclTranslate(double val) {m_scl_translate = val;}
	double GetBrushSclTranslate() {return m_scl_translate;}
	void SetUseBrushSize2(bool val) {m_use_brush_size2 = val;}
	
	void SetUseDSLTBrush(bool value) {m_use_dslt = value;}
	bool GetUseDSLTBrush() {return m_use_dslt;}
	void SetBrushDSLT_R(int rad) {m_dslt_r = rad;}
	int GetBrushDSLT_R() {return m_dslt_r;}
	void SetBrushDSLT_Q(int quality) {m_dslt_q = quality;}
	int GetBrushDSLT_Q() {return m_dslt_q;}
	void SetBrushDSLT_C(double c) {m_dslt_c = c;}
	double GetBrushDSLT_C() {return m_dslt_c;}

	//w2d
	void SetW2d(double val) {m_w2d = val;}
	double GetW2d() {return m_w2d;}
	//edge detect
	void SetEdgeDetect(bool val) {m_edge_detect = val;}
	bool GetEdgeDetect() {return m_edge_detect;}
	//hidden removal
	void SetHiddenRemoval(bool val) {m_hidden_removal = val;}
	bool GetHiddenRemoval() {return m_hidden_removal;}
	//ortho
	void SetOrthographic(bool val) {m_ortho = val;}
	bool GetOrthographic() {return m_ortho;}
	//select group
	void SetSelectGroup(bool value) {m_select_multi = value?1:0;}
	bool GetSelectGroup() {return m_select_multi==1;}
	//select both
	void SetSelectBoth(bool value) {m_select_multi = value?2:0;}
	bool GetSelectBoth() {return m_select_multi==2;}
	//size map
	void SetSizeMap(bool size_map) {m_size_map = size_map;}
	bool GetSizeMap() {return m_size_map;}
    //use absolute value
    void SetUseAbsoluteValue(bool val) {m_use_absolute_value = val;}
    bool GetUseAbsoluteValue() {return m_use_absolute_value;}

	//modes
	void SetMode(int mode) {m_mode = mode;}
	int GetMode() {return m_mode;}
	//set use 2d rendering results
	void SetPaintUse2d(bool use2d) {m_use2d = use2d;}
	bool GetPaintUse2d() {return m_use2d;}

	void Select(double radius);
	//mode: 0-nomral; 1-posterized
	void Label(int mode=0);
	int CompAnalysis(double min_voxels, double max_voxels, double thresh, double falloff, bool select, bool gen_ann, int iter_limit = -1);
	int SetLabelBySize();
	int NoiseAnalysis(double min_voxels, double max_voxels, double bins, double thresh);
	int CompIslandCount(double min_voxels, double max_voxels);
	void CompExportMultiChann(bool select);
	void CompExportRandomColor(int hmode, VolumeData* vd_r, VolumeData* vd_g, VolumeData* vd_b, bool select, bool hide=true);
	//mode: 0-no duplicate; 1-duplicate
	void NoiseRemoval(int iter, double thresh, int mode=0);
	vector<VolumeData*>* GetResultVols();
	//EVE
	void EVEAnalysis(int min_radius, int max_radius, double thresh, int target_lv = -1);
	void EVECount(int min_radius, int max_radius, double thresh, int target_lv = -1);
	//process current selection
	int ProcessSel(double thresh);
	int GetCenter(Point& p);
	int GetSize(double& s);

	//results
	int GetCompNum() {return m_ca_comps;}
	int GetVolumeNum() {return m_ca_volume;}

	//annotations
	void GenerateAnnotations(bool use_sel);
	Annotations* GetAnnotations();
	//the last analysis' annotations belong to DataManager once shown: forget
	//them when they are deleted (nullptr: whichever they are, e.g. ClearAll),
	//so Show Annotations does not hand the freed object back
	void ForgetAnnotations(Annotations* ann = nullptr)
	{
		if (!ann || m_annotations == ann)
			m_annotations = 0;
	}

	//estimate threshold
	void SetEstimateThreshold(bool value)
	{m_estimate_threshold = value;}
	bool GetEstimateThreshold()
	{return m_estimate_threshold;}

	//Test functions
	void Test();
    
    void SetDataManager(DataManager* dm)
    {
        m_dm = dm;
    }

private:
	VolumeData *m_vd;	//volume data for segmentation
	std::shared_ptr<vks::VTexture> m_2d_mask;	//2d mask from painting
	std::shared_ptr<vks::VTexture> m_2d_weight1;//2d weight map (after tone mapping)
	std::shared_ptr<vks::VTexture> m_2d_weight2;//2d weight map	(before tone mapping)
	double m_mvmat[16];	//modelview matrix
	double m_prjmat[16];//projection matrix
	int m_iter_num;		//iteration number for growing
	int m_mode;			//segmentation modes
						//1-select; 2-append; 3-erase; 4-diffuse; 5-flood; 6-clear; 7-all; 8-solid;
						//image processing modes
						//11-posterize
	bool m_use2d;
	bool m_size_map;

	//brush properties
	double m_ini_thresh;
	double m_gm_falloff;
	double m_scl_falloff;
	double m_scl_translate;
	int m_select_multi;	//0-only current; 1-select group; 2-select a and b
	bool m_use_brush_size2;
	bool m_edge_detect;
	bool m_hidden_removal;
	bool m_ortho;
	bool m_use_dslt;
	int m_dslt_r;
	int m_dslt_q;
	double m_dslt_c;
	//w2d
	double m_w2d;
	//iteration for labeling
	int m_iter_label;
	//label thresh
	double m_label_thresh;
	double m_label_falloff;
    bool m_use_absolute_value;

	//define structure
	struct Component
	{
		unsigned int id;
		unsigned long long counter;
		Vector acc_pos;
		double acc_int;
		std::unordered_set<unsigned int > links;
		bool marker;
	};
    
    struct LMaxima
    {
        int r;
        int zr;
        unsigned long long pos;
        float score;
    };

	std::unordered_map <unsigned int, Component> m_comps;
	double m_min_voxels, m_max_voxels;
    
    std::unordered_map <unsigned long long, LMaxima> m_maxima;

	//exported volumes
	vector<VolumeData*> m_result_vols;
	//annotations
	Annotations* m_annotations;

	//make progress
	vvd::Progress *m_prog_diag;
	int m_progress;
	int m_total_pr;

	//results
	int m_ca_comps;
	int m_ca_volume;

	//a random variable
	int m_randv;

	//process selection
	bool m_ps;
	Point m_ps_center;
	double m_ps_size;

	bool m_estimate_threshold;
    
    DataManager* m_dm;

private:
	bool SearchComponentList(unsigned int cval, Vector &pos, double intensity);
	double HueCalculation(int mode, unsigned int label);
	unsigned int getMinIndexOfLinkedComponents(unsigned int lval);
	unsigned int getMinIndexOfLinkedComponents_r(unsigned int lval);
	void SetIdLinkedComponents(unsigned int lval, unsigned int new_lval);
	void SetIdLinkedComponents_r(unsigned int lval, unsigned int new_lval);
	Component GetCombinedComponent(unsigned int root);
	void GetCombinedComponent_r(unsigned int lval, Component &output);
};

#endif//_VOLUMESELECTOR_H_
