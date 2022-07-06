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
#include "VolumeSelector.h"
#include "VRenderFrame.h"
#include "utility.h"
#include <wx/wx.h>
#include <algorithm>
#include <queue>

VolumeSelector::VolumeSelector() :
	m_vd(0),
	m_2d_mask(0),
	m_2d_weight1(0),
	m_2d_weight2(0),
	m_iter_num(20),
	m_mode(0),
	m_use2d(false),
	m_size_map(false),
	m_ini_thresh(0.0),
	m_gm_falloff(1.0),
	m_scl_falloff(0.0),
	m_scl_translate(0.0),
	m_select_multi(0),
	m_use_dslt(false),
	m_dslt_r(14),
	m_dslt_q(3),
	m_dslt_c(0.0),
	m_use_brush_size2(false),
	m_edge_detect(false),
	m_hidden_removal(false),
	m_ortho(true),
	m_w2d(0.0),
	m_iter_label(1),
	m_label_thresh(0.0),
	m_label_falloff(1.0),
	m_min_voxels(0.0),
	m_max_voxels(0.0),
	m_annotations(0),
	m_prog_diag(0),
	m_progress(0),
	m_total_pr(0),
	m_ca_comps(0),
	m_ca_volume(0),
	m_randv(113),
	m_ps(false),
	m_estimate_threshold(false),
    m_use_absolute_value(false)
{
}

VolumeSelector::~VolumeSelector()
{
}

void VolumeSelector::SetVolume(VolumeData *vd)
{
	m_vd = vd;
}

VolumeData* VolumeSelector::GetVolume()
{
	return m_vd;
}

void VolumeSelector::Set2DMask(const std::shared_ptr<vks::VTexture> &mask)
{
	m_2d_mask = mask;
}

void VolumeSelector::Set2DWeight(const std::shared_ptr<vks::VTexture> &weight1, const std::shared_ptr<vks::VTexture> &weight2)
{
	m_2d_weight1 = weight1;
	m_2d_weight2 = weight2;
}

void VolumeSelector::SetProjection(double *mvmat, double *prjmat)
{
	memcpy(m_mvmat, mvmat, 16*sizeof(double));
	memcpy(m_prjmat, prjmat, 16*sizeof(double));
}

void VolumeSelector::Select(double radius)
{
	if (!m_vd)
		return;

	//insert the mask volume into m_vd
    Texture* ext_msk = NULL;
    if (!m_vd->GetSharedMaskName().IsEmpty() && m_dm)
    {
        VolumeData* msk = m_dm->GetVolumeData(m_vd->GetSharedMaskName());
        if (msk)
            ext_msk = msk->GetTexture();
        else
            m_vd->AddEmptyMask();
    }
    else
        m_vd->AddEmptyMask();
    
	m_vd->Set2dMask(m_2d_mask);
	if (m_use2d && m_2d_weight1 && m_2d_weight2)
		m_vd->Set2DWeight(m_2d_weight1, m_2d_weight2);
	else
	{
		auto blank = std::shared_ptr<vks::VTexture>();
		m_vd->Set2DWeight(blank, blank);
	}

	//segment the volume with 2d mask
	//result in 3d mask
	//clear if the select mode
	double ini_thresh, gm_falloff, scl_falloff;
	if (m_use_brush_size2)
	{
		if (m_ini_thresh > 0.0)
			ini_thresh = m_ini_thresh;
		else
			ini_thresh = sqrt(m_scl_translate);
		if (m_scl_falloff > 0.0)
			scl_falloff = m_scl_falloff;
		else
			scl_falloff = 0.008;
	}
	else
	{
		ini_thresh = m_scl_translate;
		if (m_vd->GetLeftThresh() > ini_thresh)
			ini_thresh = m_vd->GetLeftThresh();
		scl_falloff = 0.0;
	}
	if (m_edge_detect)
		gm_falloff = m_gm_falloff;
	else
		gm_falloff = 1.0;

	if (Texture::mask_undo_num_>0 &&
		m_vd->GetTexture())
		m_vd->GetTexture()->push_mask();

	//initialization
	int hr_mode = m_hidden_removal?(m_ortho?1:2):0;
/*	if ((m_mode==1 || m_mode==2) && m_estimate_threshold)
	{
		m_vd->DrawMask(0, m_mode, hr_mode, 0.0, gm_falloff, scl_falloff, 0.0, m_w2d, 0.0, false, true);
		m_vd->DrawMask(0, 6, 0, ini_thresh, gm_falloff, scl_falloff, m_scl_translate, m_w2d, 0.0);
		ini_thresh = m_vd->GetEstThresh() * m_vd->GetScalarScale();
		if (m_iter_num>BRUSH_TOOL_ITER_WEAK)
			ini_thresh /= 2.0;
		m_scl_translate = ini_thresh;
	}*/
    
	if (m_use_dslt && (m_mode == 1 || m_mode == 2))
		m_vd->DrawMaskDSLT(0, m_mode, hr_mode, ini_thresh, gm_falloff, scl_falloff, m_scl_translate, m_w2d, 0.0, m_dslt_r, m_dslt_q, m_dslt_c);
	else
		m_vd->DrawMask(0, m_mode, hr_mode, ini_thresh, gm_falloff, scl_falloff, m_scl_translate, m_w2d, 0.0, false, ext_msk);

	//grow the selection when paint mode is select, append, erase, or invert
	if (/*m_mode==1 ||
		m_mode==2 ||
		m_mode==3 ||*/
		m_mode==4)
	{
        auto st_time = GET_TICK_COUNT();
        wxProgressDialog* prog_diag = nullptr;
        
		//loop for growing
		int iter = m_iter_num*(radius/20.0>1.0?radius/20.0:1.0);
        int bnum = m_vd->GetTexture()->get_brick_num();
		for (int i=0; i<iter; i++)
        {
            bool clear_cache = false;
            
            if (m_vd->isBrxml() && bnum > 1)
            {
                if (i % 5 == 4 || i == iter-1)
                {
                    clear_cache = true;
                    if (m_vd->GetVR())
                        m_vd->GetVR()->return_mask();
                }
            }
            m_vd->DrawMask(1, m_mode, 0, ini_thresh, gm_falloff, scl_falloff, m_scl_translate, m_w2d, 0.0, false, ext_msk, clear_cache);
            
            auto rn_time = GET_TICK_COUNT();
            if (!prog_diag && rn_time - st_time > 3000)
            {
                prog_diag = new wxProgressDialog(
                                "VVDViewer: Diffuse brush",
                                 "Processing... Please wait.",
                                 100, 0,
                                 wxPD_SMOOTH|wxPD_ELAPSED_TIME|wxPD_AUTO_HIDE|wxPD_APP_MODAL|wxPD_CAN_ABORT);
            }
            if (prog_diag)
            {
                if (!prog_diag->Update(95*(i+1)/iter))
                    break;
            }
        }
		/*if (m_vd->GetVR() && m_vd->GetBrickNum() > 1) {
			m_vd->GetVR()->return_mask();
			m_vd->GetVR()->clear_tex_current_mask();
			for (int i = 0; i < iter; i++)
				m_vd->DrawMask(1, m_mode, 0, ini_thresh, gm_falloff, scl_falloff, m_scl_translate, m_w2d, 0.0);
		}*/
        if (prog_diag)
        {
            prog_diag->Update(100);
            delete prog_diag;
        }
	}

	if (m_mode == 6)
		m_vd->SetUseMaskThreshold(false);
    
    if (m_vd->GetVR())
        m_vd->GetVR()->return_mask();

	if (Texture::mask_undo_num_>0 &&
		m_vd->GetVR())
	{
		m_vd->GetVR()->clear_tex_current_mask();
	}
}

//mode: 0-normal; 1-posterized; 2-noraml,copy; 3-poster, copy
void VolumeSelector::Label(int mode)
{
	if (!m_vd)
		return;

	int label_mode = 0;
	if (mode==2||mode==3)
		label_mode = 2;
	//insert the label volume to m_vd
	m_vd->AddEmptyLabel(label_mode);

	//apply ids to the label volume
	m_vd->DrawLabel(0, mode, m_label_thresh, m_label_falloff);

	//filter the label volume by maximum intensity filtering
	for (int i=0; i<m_iter_label; i++)
	{
		m_vd->DrawLabel(1, mode, m_label_thresh, m_label_falloff);
		if (m_prog_diag)
		{
			m_progress++;
			m_prog_diag->Update(95*(m_progress+1)/m_total_pr);
		}
	}
}

int VolumeSelector::CompAnalysis(double min_voxels, double max_voxels, double thresh, double falloff, bool select, bool gen_ann, int iter_limit)
{
	int return_val = 0;
	m_label_thresh = thresh;
	m_label_falloff = falloff;
	if (!m_vd/* || m_vd->isBrxml()*/)
		return return_val;

	bool use_sel = false;
	if (select && m_vd->GetTexture() && m_vd->GetTexture()->nmask()!=-1)
		use_sel = true;

	m_prog_diag = new wxProgressDialog(
		"FluoRender: Component Analysis...",
		"Analyzing... Please wait.",
		100, 0,
		wxPD_SMOOTH|wxPD_ELAPSED_TIME|wxPD_AUTO_HIDE);
	m_progress = 0;

	if (use_sel)
	{
		//calculate on selection only
		int nx, ny, nz;
		m_vd->GetResolution(nx, ny, nz);
		m_iter_label = Max(nx, Max(ny, nz));
		m_total_pr = m_iter_label+nx*2;
	}
	else
	{
		//calculate on the whole volume
		int nx, ny, nz;
		m_vd->GetResolution(nx, ny, nz);
		m_iter_label = Max(nx, Max(ny, nz));
		if (iter_limit > 0) m_iter_label = Min(iter_limit, m_iter_label);
		m_total_pr = m_iter_label+nx*2;
		//first, grow in the whole volume
		m_vd->AddEmptyMask();
		if (m_use2d && m_2d_weight1 && m_2d_weight2)
			m_vd->Set2DWeight(m_2d_weight1, m_2d_weight2);
		else
		{
			auto blank = std::shared_ptr<vks::VTexture>();
			m_vd->Set2DWeight(blank, blank);
		}

		m_vd->DrawMask(0, 5, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
		//next do the same as when it's selected by brush
	}/*
	if (m_vd->GetBrickNum() == 1) {
		Label(0);
		m_vd->GetVR()->return_label();
	} else*/
		m_vd->GetVR()->return_mask();
	return_val = CompIslandCount(min_voxels, max_voxels);

	if (gen_ann)
		GenerateAnnotations(use_sel);
	else
		m_annotations = 0;

	if (m_size_map)
		SetLabelBySize();

	if (m_prog_diag)
	{
		m_prog_diag->Update(100);
		delete m_prog_diag;
		m_prog_diag = 0;
	}

	return return_val;
}
int VolumeSelector::SetLabelBySize()
{
	int return_val = 0;
	if (!m_vd)
		return 0;
	//get label data
	Texture* tex = m_vd->GetTexture();
	if (!tex)
		return 0;
	Nrrd* nrrd_label = tex->get_nrrd_raw(tex->nlabel());
	if (!nrrd_label)
		return 0;
	unsigned int* data_label = (unsigned int*)(nrrd_label->data);
	if (!data_label)
		return 0;

	//determine range first
	unsigned int min_size = 0;
	unsigned int max_size = 0;
	unsigned int counter;
	unordered_map<unsigned int, Component>::iterator comp_iter;
	for (comp_iter=m_comps.begin();
		comp_iter!=m_comps.end();
		++comp_iter)
	{
		counter = comp_iter->second.counter;
		if (counter<m_min_voxels ||
			(m_max_voxels<0.0?false:
			(counter>m_max_voxels)))
			continue;
		if (comp_iter == m_comps.begin())
		{
			min_size = counter;
			max_size = counter;
		}
		else
		{
			min_size = counter<min_size?counter:min_size;
			max_size = counter>max_size?counter:max_size;
		}
	}

	//parse label data and change values
	int nx, ny, nz;
	m_vd->GetResolution(nx, ny, nz);

	int i, j, k;
	int index;
	unsigned int id;
	for (i=0; i<nx; ++i)
		for (j=0; j<ny; ++j)
			for (k=0; k<nz; ++k)
			{
				index = nx*ny*k + nx*j + i;
				id = data_label[index];
				if (id > 0)
				{
					comp_iter = m_comps.find(id);
					if (comp_iter != m_comps.end())
					{
						counter = comp_iter->second.counter;
						if (counter >= min_size &&
							counter <= max_size)
						{
							//calculate color
							if (max_size > min_size)
								data_label[index] = 
								(unsigned int)(240.0-
								(double)(counter-min_size)/
								(double)(max_size-min_size)*
								239.0);
							else
								data_label[index] = 1;
							continue;
						}
					}

					data_label[index] = 0;
				}
			}

			return return_val;
}

int VolumeSelector::CompIslandCount(double min_voxels, double max_voxels)
{
	m_min_voxels = min_voxels;
	m_max_voxels = max_voxels;
	m_ca_comps = 0;
	m_ca_volume = 0;
	if (!m_vd/* || m_vd->isBrxml()*/)
		return 0;
	Texture* tex = m_vd->GetTexture();
	if (!tex)
		return 0;

	std::shared_ptr<VL_Nrrd> orig_nrrd;
    if (tex->isBrxml())
    {
        int msklv = tex->GetMaskLv();
        orig_nrrd = make_shared<VL_Nrrd>(tex->loadData(msklv));
    }
    else
        orig_nrrd = tex->get_nrrd(0);
    
	if (!orig_nrrd)
		return 0;
	void* orig_data = orig_nrrd->getNrrd()->data;
	if (!orig_data)
		return 0;
	
	//resolution
	int nx, ny, nz;
	m_vd->GetResolution(nx, ny, nz);
    if (tex->isBrxml())
        tex->GetDimensionLv(tex->GetMaskLv(), nx, ny, nz);

	m_comps.clear();
	
	std::unordered_map <unsigned int, Component> ::iterator comp_iter;
	size_t i, j, k;

	/*if (m_vd->GetBrickNum() > 1) {*/
		
		m_vd->AddEmptyLabel(0);

		std::shared_ptr<VL_Nrrd> label_nrrd = tex->get_nrrd(tex->nlabel());
		if (!label_nrrd)
			return 0;
		unsigned int* label_data = (unsigned int*)(label_nrrd->getNrrd()->data);
		if (!label_data)
			return 0;

		std::shared_ptr<VL_Nrrd> mask_nrrd = tex->get_nrrd(tex->nmask());
		if (!mask_nrrd)
			return 0;
		unsigned char* mask_data = (unsigned char*)(mask_nrrd->getNrrd()->data);
		if (!mask_data)
			return 0;

		size_t voxelnum = (size_t)nx*(size_t)ny*(size_t)nz;
		unsigned int *tmplabel = new (std::nothrow) unsigned int[voxelnum];
		memset(tmplabel, 0, voxelnum * sizeof(unsigned int));

		m_total_pr = nz;
		m_progress = 0;

		int label_th_i = orig_nrrd->getNrrd()->type == nrrdTypeUChar ? (unsigned int)(m_label_thresh * 255.0) : (unsigned int)(m_label_thresh * 65535.0);

		unsigned int segid = 1;
		unsigned int finalid = 1;
		unsigned int x, y, z;
		vector<size_t> pts;
		for (z = 0; z < nz; z++) {
			for (y = 0; y < ny; y++) {
				for (x = 0; x < nx; x++) {
					size_t id = (size_t)nx*(size_t)ny*z + (size_t)nx*y + x;
					unsigned int val = orig_nrrd->getNrrd()->type == nrrdTypeUChar ? ((unsigned char*)orig_data)[id] : ((unsigned short*)orig_data)[id];
					unsigned char mask_val = mask_data[id];
					unsigned int segval = tmplabel[id];
					if (mask_val == 0 || val <= label_th_i) {
						tmplabel[id] = 0;
						mask_data[id] = 0;
					}
					else if (segval == 0) {
						std::queue<long long> que;
						unsigned int cx, cy, cz;
						long long tmp;

						pts.clear();

						Component comp;
						comp.counter = 0;
						comp.id = segid;
						comp.acc_pos = Vector(0.0);
						comp.acc_int = 0;

						tmplabel[id] = segid;
						que.push(((long long)x << 16 | (long long)y) << 16 | (long long)z);

						while (que.size()) {
							tmp = que.front();
							que.pop();

							cx = tmp >> 32;
							cy = (tmp >> 16) & 0xFFFF;
							cz = tmp & 0xFFFF;

							int cid = (size_t)nx*(size_t)ny*cz + (size_t)nx*cy + cx;
							pts.push_back(cid);
							comp.counter++;
							comp.acc_pos += Vector(cx, cy, cz);
							comp.acc_int += orig_nrrd->getNrrd()->type == nrrdTypeUChar ? double(((unsigned char*)orig_data)[cid]) / 255.0 : double(((unsigned short*)orig_data)[cid]) / 65535.0;

							int stx = -1, sty = -1, stz = -1, edx = 1, edy = 1, edz = 1;
							if (cx == 0) stx = 0;
							else if (cx == nx - 1) edx = 0;
							if (cy == 0) sty = 0;
							else if (cy == ny - 1) edy = 0;
							if (cz == 0) stz = 0;
							else if (cz == nz - 1) edz = 0;

							for (int dz = stz; dz <= edz; dz++)
								for (int dy = sty; dy <= edy; dy++)
									for (int dx = stx; dx <= edx; dx++) {
										size_t did = (size_t)nx*(size_t)ny*(cz + dz) + (size_t)nx*(cy + dy) + cx + dx;
										unsigned int next_val = orig_nrrd->getNrrd()->type == nrrdTypeUChar ? ((unsigned char*)orig_data)[did] : ((unsigned short*)orig_data)[did];
										if (mask_data[did] > 0 && tmplabel[did] == 0 && next_val > label_th_i) {
											tmplabel[did] = segid;
											que.push(((long long)(cx + dx) << 16 | (long long)(cy + dy)) << 16 | (long long)(cz + dz));
										}
									}
						}
						if (comp.counter < min_voxels || (max_voxels < 0.0 ? false : (comp.counter > max_voxels))) {
							for (size_t e : pts)
								mask_data[e] = 0;
						}
						else {
							comp.id = finalid;
							m_comps.insert(pair<unsigned int, Component>(finalid, comp));
							for (size_t e : pts)
								label_data[e] = finalid;
							finalid++;
						}
						segid++;
					}
				}
			}
			if (m_prog_diag)
			{
				m_progress++;
				m_prog_diag->Update(95 * (m_progress + 1) / m_total_pr);
			}
		}

		delete[] tmplabel;
	/*}
	else {
		Nrrd* label_nrrd = tex->get_nrrd(tex->nlabel());
		if (!label_nrrd)
			return 0;
		unsigned int* label_data = (unsigned int*)(label_nrrd->data);
		if (!label_data)
			return 0;

		//first pass: generate the component list
		for (i = 0; i < nx; i++)
		{
			for (j = 0; j < ny; j++)
				for (k = 0; k < nz; k++)
				{
					size_t index = (size_t)nx*(size_t)ny*k + (size_t)nx*j + i;
					unsigned int value = label_data[index];
					if (value > 0)
					{
						Vector pos = Vector(i, j, k);
						double intensity = 0.0;
						if (orig_nrrd->type == nrrdTypeUChar)
							intensity = double(((unsigned char*)orig_data)[index]) / 255.0;
						else if (orig_nrrd->type == nrrdTypeUShort)
							intensity = double(((unsigned short*)orig_data)[index]) / 65535.0;
						bool found = SearchComponentList(value, pos, intensity);
						if (!found)
						{
							Component comp;
							comp.counter = 1ULL;
							comp.id = value;
							comp.acc_pos = pos;
							comp.acc_int = intensity;
							m_comps.insert(pair<unsigned int, Component>(value, comp));
						}
					}
				}
			if (m_prog_diag)
			{
				m_progress++;
				m_prog_diag->Update(95 * (m_progress + 1) / m_total_pr);
			}
		}

		//second pass: combine components and remove islands
		//update mask
		Nrrd* mask_nrrd = m_vd->GetMask(true);
		if (!mask_nrrd)
			return 0;
		unsigned char* mask_data = (unsigned char*)(mask_nrrd->data);
		if (!mask_data)
			return 0;
		for (i = 0; i < nx; i++)
		{
			for (j = 0; j < ny; j++)
				for (k = 0; k < nz; k++)
				{
					size_t index = (size_t)nx * (size_t)ny*k + (size_t)nx * j + i;
					unsigned int label_value = label_data[index];
					if (label_value > 0)
					{
						comp_iter = m_comps.find(label_value);
						if (comp_iter != m_comps.end())
						{
							if (comp_iter->second.counter < min_voxels ||
								(max_voxels < 0.0 ? false : (comp_iter->second.counter > max_voxels)))
								mask_data[index] = 0;
						}
					}
					else
						mask_data[index] = 0;
				}
			if (m_prog_diag)
			{
				m_progress++;
				m_prog_diag->Update(95 * (m_progress + 1) / m_total_pr);
			}
		}
	}*/

	if (m_vd->GetVR())
		m_vd->GetVR()->clear_tex_current();

	//count
	for (comp_iter = m_comps.begin(); comp_iter != m_comps.end(); comp_iter++)
	{
		if (comp_iter->second.counter >= min_voxels &&
			(max_voxels < 0.0 ? true : (comp_iter->second.counter <= max_voxels)))
		{
			m_ca_comps++;
			m_ca_volume += comp_iter->second.counter;
		}
	}

	return m_ca_comps;
}

unsigned int VolumeSelector::getMinIndexOfLinkedComponents(unsigned int lval)
{
	for (auto& elem : m_comps)
		elem.second.marker = false;

	return getMinIndexOfLinkedComponents_r(lval);
}

unsigned int VolumeSelector::getMinIndexOfLinkedComponents_r(unsigned int lval)
{
	unordered_map <unsigned int, Component> ::iterator comp_iter;
	comp_iter = m_comps.find(lval);
	unsigned int minval = UINT_MAX;
	if (comp_iter != m_comps.end()) {
		if (!comp_iter->second.marker) {
			minval = lval;
			comp_iter->second.marker = true;
			for (const auto& elem : comp_iter->second.links) {
				unsigned int val = getMinIndexOfLinkedComponents_r(elem);
				if (minval > val) minval = val;
			}
		}
	}

	return minval;
}

void VolumeSelector::SetIdLinkedComponents(unsigned int lval, unsigned int new_lval)
{
	for (auto& elem : m_comps)
		elem.second.marker = false;

	SetIdLinkedComponents_r(lval, new_lval);
}

void VolumeSelector::SetIdLinkedComponents_r(unsigned int lval, unsigned int new_lval)
{
	unordered_map <unsigned int, Component> ::iterator comp_iter;
	comp_iter = m_comps.find(lval);
	if (comp_iter != m_comps.end()) {
		if (!comp_iter->second.marker) {
			comp_iter->second.id = new_lval;
			comp_iter->second.marker = true;
			for (const auto& elem : comp_iter->second.links)
				SetIdLinkedComponents_r(elem, new_lval);
		}
	}

	return;
}

VolumeSelector::Component VolumeSelector::GetCombinedComponent(unsigned int root)
{
	for (auto& elem : m_comps)
		elem.second.marker = false;

	Component output;
	output.counter = 0;
	output.id = 0;
	output.acc_pos = 0;
	output.acc_int = 0;

	unordered_map <unsigned int, Component> ::iterator comp_iter;
	comp_iter = m_comps.find(root);
	if (comp_iter != m_comps.end()) {
		output = comp_iter->second;
		for (const auto& elem : comp_iter->second.links) {
			GetCombinedComponent_r(elem, output);
		}
	}

	return output;
}
void VolumeSelector::GetCombinedComponent_r(unsigned int lval, Component &output)
{	
	unordered_map <unsigned int, Component> ::iterator comp_iter;
	comp_iter = m_comps.find(lval);
	if (comp_iter != m_comps.end()) {
		if (!comp_iter->second.marker) {
			output.counter += comp_iter->second.counter;
			output.acc_pos += comp_iter->second.acc_pos;
			output.acc_int += comp_iter->second.acc_int;
			comp_iter->second.marker = true;
			for (const auto& elem : comp_iter->second.links) {
				GetCombinedComponent_r(elem, output);
			}
		}
	}
}

bool VolumeSelector::SearchComponentList(unsigned int cval, Vector &pos, double intensity)
{
	unordered_map <unsigned int, Component> :: iterator comp_iter;
	comp_iter = m_comps.find(cval);
	if (comp_iter != m_comps.end()) {
		comp_iter->second.counter++;
		comp_iter->second.acc_pos += pos;
		comp_iter->second.acc_int += intensity;
		return true;
	}
	else
		return false;

	return false;
}

double VolumeSelector::HueCalculation(int mode, unsigned int label)
{
	double hue = 0.0;
	switch (mode)
	{
	case 0:
		hue = double(label % 360);
		break;
	case 1:
		hue = double((label*43) % 360);//double(label % m_randv) / double(m_randv) * 360.0;
		break;
	case 2:
		hue = double((bit_reverse(label)*43) % 360);//double(bit_reverse(label) % m_randv) / double(m_randv) * 360.0;
		break;
	}
	return hue;
}

void VolumeSelector::CompExportMultiChann(bool select)
{
	if (!m_vd ||
		!m_vd->GetTexture() ||
		(select&&m_vd->GetTexture()->nmask()==-1) ||
		m_vd->GetTexture()->nlabel()==-1)
		return;

	m_result_vols.clear();

	if (select)
		m_vd->GetVR()->return_mask();

	int i;
	//get all the data from original volume
	Texture* tex_mvd = m_vd->GetTexture();
	if (!tex_mvd) return;
	Nrrd* nrrd_mvd = tex_mvd->get_nrrd_raw(0);
	if (!nrrd_mvd) return;
	Nrrd* nrrd_mvd_mask = tex_mvd->get_nrrd_raw(tex_mvd->nmask());
	if (select && !nrrd_mvd_mask) return;
	Nrrd* nrrd_mvd_label = tex_mvd->get_nrrd_raw(tex_mvd->nlabel());
	if (!nrrd_mvd_label) return;
	void* data_mvd = nrrd_mvd->data;
	unsigned char* data_mvd_mask = (unsigned char*)nrrd_mvd_mask->data;
	unsigned int* data_mvd_label = (unsigned int*)nrrd_mvd_label->data;
	if (!data_mvd || (select&&!data_mvd_mask) || !data_mvd_label) return;

	i = 1;
	unordered_map <unsigned int, Component> :: const_iterator comp_iter;
	for (comp_iter=m_comps.begin(); comp_iter!=m_comps.end(); comp_iter++)
	{
		if (comp_iter->second.counter<m_min_voxels ||
			(m_max_voxels<0.0?false:(comp_iter->second.counter>m_max_voxels)))
			continue;

		//create a new volume
		int res_x, res_y, res_z;
		double spc_x, spc_y, spc_z;
		int bits = 8;
		m_vd->GetResolution(res_x, res_y, res_z);
		m_vd->GetSpacings(spc_x, spc_y, spc_z);
		double amb, diff, spec, shine;
		m_vd->GetMaterial(amb, diff, spec, shine);

		VolumeData* vd = new VolumeData();
		vd->AddEmptyData(bits,
			res_x, res_y, res_z,
			spc_x, spc_y, spc_z);
		vd->SetSpcFromFile(true);
		vd->SetName(m_vd->GetName() +
			wxString::Format("_COMP%d_SIZE%llu", i++, comp_iter->second.counter));

		//populate the volume
		//the actual data
		Texture* tex_vd = vd->GetTexture();
		if (!tex_vd) continue;
		Nrrd* nrrd_vd = tex_vd->get_nrrd_raw(0);
		if (!nrrd_vd) continue;
		unsigned char* data_vd = (unsigned char*)nrrd_vd->data;
		if (!data_vd) continue;

		int ii, jj, kk;
		for (ii=0; ii<res_x; ii++)
			for (jj=0; jj<res_y; jj++)
				for (kk=0; kk<res_z; kk++)
				{
					int index = res_x*res_y*kk + res_x*jj + ii;
					unsigned int value_label = data_mvd_label[index];
					if (value_label > 0 && value_label==comp_iter->second.id)
					{
						unsigned char value = 0;
						if (nrrd_mvd->type == nrrdTypeUChar)
						{
							if (select)
								value = (unsigned char)((double)(((unsigned char*)data_mvd)[index]) *
								double(data_mvd_mask[index]) / 255.0);
							else
								value = ((unsigned char*)data_mvd)[index];
						}
						else if (nrrd_mvd->type == nrrdTypeUShort)
						{
							if (select)
								value = (unsigned char)((double)(((unsigned short*)data_mvd)[index]) *
								m_vd->GetScalarScale() * double(data_mvd_mask[index]) / 65535.0);
							else
								value = (unsigned char)((double)(((unsigned short*)data_mvd)[index]) *
								m_vd->GetScalarScale() / 255.0);
						}
						data_vd[index] = value;
					}
				}
				int randv = 0;
				while (randv < 100) randv = rand();
				unsigned int rev_value_label = bit_reverse(comp_iter->second.id);
				double hue = double(rev_value_label % randv) / double(randv) * 360.0;
				Color color(HSVColor(hue, 1.0, 1.0));
				vd->SetColor(color);

				vd->SetEnableAlpha(m_vd->GetEnableAlpha());
				vd->SetShading(m_vd->GetShading());
				vd->SetShadow(false);
				//other settings
				vd->Set3DGamma(m_vd->Get3DGamma());
				vd->SetBoundary(m_vd->GetBoundary());
				vd->SetOffset(m_vd->GetOffset());
				vd->SetLeftThresh(m_vd->GetLeftThresh());
				vd->SetRightThresh(m_vd->GetRightThresh());
				vd->SetAlpha(m_vd->GetAlpha());
				vd->SetSampleRate(m_vd->GetSampleRate());
				vd->SetMaterial(amb, diff, spec, shine);

				m_result_vols.push_back(vd);
	}
}

void VolumeSelector::CompExportRandomColor(int hmode, VolumeData* vd_r,
	VolumeData* vd_g, VolumeData* vd_b, bool select, bool hide)
{
	if (!m_vd ||
		!m_vd->GetTexture() ||
		(select&&m_vd->GetTexture()->nmask()==-1) ||
		m_vd->GetTexture()->nlabel()==-1)
		return;

	if (select)
		m_vd->GetVR()->return_mask();

	m_result_vols.clear();

	//get all the data from original volume
	Texture* tex_mvd = m_vd->GetTexture();
	if (!tex_mvd) return;
	Nrrd* nrrd_mvd = tex_mvd->get_nrrd_raw(0);
	if (!nrrd_mvd) return;
	Nrrd* nrrd_mvd_mask = tex_mvd->get_nrrd_raw(tex_mvd->nmask());
	if (select && !nrrd_mvd_mask) return;
	Nrrd* nrrd_mvd_label = tex_mvd->get_nrrd_raw(tex_mvd->nlabel());
	if (!nrrd_mvd_label) return;
	void* data_mvd = nrrd_mvd->data;
	unsigned char* data_mvd_mask = (unsigned char*)nrrd_mvd_mask->data;
	unsigned int* data_mvd_label = (unsigned int*)nrrd_mvd_label->data;
	if (!data_mvd || (select&&!data_mvd_mask) || !data_mvd_label) return;

	//create new volumes
	int res_x, res_y, res_z;
	double spc_x, spc_y, spc_z;
	int bits = 8;
	m_vd->GetResolution(res_x, res_y, res_z);
	m_vd->GetSpacings(spc_x, spc_y, spc_z);
	bool push_new = true;
	//red volume
	if (!vd_r)
		vd_r = new VolumeData();
	else
		push_new = false;
	vd_r->AddEmptyData(bits,
		res_x, res_y, res_z,
		spc_x, spc_y, spc_z);
	vd_r->SetSpcFromFile(true);
	vd_r->SetName(m_vd->GetName() +
		wxString::Format("_COMP1"));
	//green volume
	if (!vd_g)
		vd_g = new VolumeData();
	vd_g->AddEmptyData(bits,
		res_x, res_y, res_z,
		spc_x, spc_y, spc_z);
	vd_g->SetSpcFromFile(true);
	vd_g->SetName(m_vd->GetName() +
		wxString::Format("_COMP2"));
	//blue volume
	if (!vd_b)
		vd_b = new VolumeData();
	vd_b->AddEmptyData(bits,
		res_x, res_y, res_z,
		spc_x, spc_y, spc_z);
	vd_b->SetSpcFromFile(true);
	vd_b->SetName(m_vd->GetName() +
		wxString::Format("_COMP3"));

	//get new data
	//red volume
	Texture* tex_vd_r = vd_r->GetTexture();
	if (!tex_vd_r) return;
	Nrrd* nrrd_vd_r = tex_vd_r->get_nrrd_raw(0);
	if (!nrrd_vd_r) return;
	unsigned char* data_vd_r = (unsigned char*)nrrd_vd_r->data;
	if (!data_vd_r) return;
	//green volume
	Texture* tex_vd_g = vd_g->GetTexture();
	if (!tex_vd_g) return;
	Nrrd* nrrd_vd_g = tex_vd_g->get_nrrd_raw(0);
	if (!nrrd_vd_g) return;
	unsigned char* data_vd_g = (unsigned char*)nrrd_vd_g->data;
	if (!data_vd_g) return;
	//blue volume
	Texture* tex_vd_b = vd_b->GetTexture();
	if (!tex_vd_b) return;
	Nrrd* nrrd_vd_b = tex_vd_b->get_nrrd_raw(0);
	if (!nrrd_vd_b) return;
	unsigned char* data_vd_b = (unsigned char*)nrrd_vd_b->data;
	if (!data_vd_b) return;

	if (hide)
		m_randv = int((double)rand()/(RAND_MAX)*900+100);
	//populate the data
	int ii, jj, kk;
	for (ii=0; ii<res_x; ii++)
		for (jj=0; jj<res_y; jj++)
			for (kk=0; kk<res_z; kk++)
			{
				int index = res_x*res_y*kk + res_x*jj + ii;
				unsigned int value_label = data_mvd_label[index];
				if (value_label > 0)
				{
					//intensity value
					double value = 0.0;
					if (nrrd_mvd->type == nrrdTypeUChar)
					{
						if (select)
							value = double(((unsigned char*)data_mvd)[index]) *
							double(data_mvd_mask[index]) / 65025.0;
						else
							value = double(((unsigned char*)data_mvd)[index]) / 255.0;
					}
					else if (nrrd_mvd->type == nrrdTypeUShort)
					{
						if (select)
							value = double(((unsigned short*)data_mvd)[index]) *
							m_vd->GetScalarScale() *
							double(data_mvd_mask[index]) / 16581375.0;
						else
							value = double(((unsigned short*)data_mvd)[index]) *
							m_vd->GetScalarScale() / 65535.0;
					}
					double hue = HueCalculation(hmode, value_label);
					Color color(HSVColor(hue, 1.0, 1.0));
					//color
					value = value>1.0?1.0:value;
					data_vd_r[index] = (unsigned char)(color.r()*255.0*value);
					data_vd_g[index] = (unsigned char)(color.g()*255.0*value);
					data_vd_b[index] = (unsigned char)(color.b()*255.0*value);
				}
			}

			FLIVR::Color red    = Color(1.0,0.0,0.0);
			FLIVR::Color green  = Color(0.0,1.0,0.0);
			FLIVR::Color blue   = Color(0.0,0.0,1.0);
			vd_r->SetColor(red);
			vd_g->SetColor(green);
			vd_b->SetColor(blue);
			bool bval = m_vd->GetEnableAlpha();
			vd_r->SetEnableAlpha(bval);
			vd_g->SetEnableAlpha(bval);
			vd_b->SetEnableAlpha(bval);
			bval = m_vd->GetShading();
			vd_r->SetShading(bval);
			vd_g->SetShading(bval);
			vd_b->SetShading(bval);
			vd_r->SetShadow(false);
			vd_g->SetShadow(false);
			vd_b->SetShadow(false);
			//other settings
			double amb, diff, spec, shine;
			m_vd->GetMaterial(amb, diff, spec, shine);
			vd_r->Set3DGamma(m_vd->Get3DGamma());
			vd_r->SetBoundary(m_vd->GetBoundary());
			vd_r->SetOffset(m_vd->GetOffset());
			vd_r->SetLeftThresh(m_vd->GetLeftThresh());
			vd_r->SetRightThresh(m_vd->GetRightThresh());
			vd_r->SetAlpha(m_vd->GetAlpha());
			vd_r->SetSampleRate(m_vd->GetSampleRate());
			vd_r->SetMaterial(amb, diff, spec, shine);
			vd_g->Set3DGamma(m_vd->Get3DGamma());
			vd_g->SetBoundary(m_vd->GetBoundary());
			vd_g->SetOffset(m_vd->GetOffset());
			vd_g->SetLeftThresh(m_vd->GetLeftThresh());
			vd_g->SetRightThresh(m_vd->GetRightThresh());
			vd_g->SetAlpha(m_vd->GetAlpha());
			vd_g->SetSampleRate(m_vd->GetSampleRate());
			vd_g->SetMaterial(amb, diff, spec, shine);
			vd_b->Set3DGamma(m_vd->Get3DGamma());
			vd_b->SetBoundary(m_vd->GetBoundary());
			vd_b->SetOffset(m_vd->GetOffset());
			vd_b->SetLeftThresh(m_vd->GetLeftThresh());
			vd_b->SetRightThresh(m_vd->GetRightThresh());
			vd_b->SetAlpha(m_vd->GetAlpha());
			vd_b->SetSampleRate(m_vd->GetSampleRate());
			vd_b->SetMaterial(amb, diff, spec, shine);

			if (push_new)
			{
				m_result_vols.push_back(vd_r);
				m_result_vols.push_back(vd_g);
				m_result_vols.push_back(vd_b);
			}

			//turn off m_vd
			if (hide)
				m_vd->SetDisp(false);
}

vector<VolumeData*>* VolumeSelector::GetResultVols()
{
	return &m_result_vols;
}

//process current selection
int VolumeSelector::ProcessSel(double thresh)
{
	m_ps = false;

	if (!m_vd ||
		!m_vd->GetTexture() ||
		m_vd->GetTexture()->nmask()==-1)
		return 0;

	m_vd->GetVR()->return_mask();
    //m_vd->GetVR()->return_stroke();

	//get all the data from original volume
	Texture* tex_mvd = m_vd->GetTexture();
	if (!tex_mvd) return 0;
	Nrrd* nrrd_mvd = tex_mvd->get_nrrd_raw(0);
	if (!nrrd_mvd) return 0;
	Nrrd* nrrd_mvd_mask = tex_mvd->get_nrrd_raw(tex_mvd->nmask());
	if (!nrrd_mvd_mask) return 0;
	void* data_mvd = nrrd_mvd->data;
	unsigned char* data_mvd_mask = (unsigned char*)nrrd_mvd_mask->data;
	if (!data_mvd || (!data_mvd_mask)) return 0;

	//find center
	int res_x, res_y, res_z;
	double spc_x, spc_y, spc_z;
	m_vd->GetResolution(res_x, res_y, res_z);
	m_vd->GetSpacings(spc_x, spc_y, spc_z);

	m_ps_size = 0.0;
	double nw = 0.0;
	double w;
	Point sump(0.0, 0.0, 0.0);
	double value;
	int ii, jj, kk;
	int index;
	for (ii=0; ii<res_x; ii++)
		for (jj=0; jj<res_y; jj++)
			for (kk=0; kk<res_z; kk++)
			{
				index = res_x*res_y*kk + res_x*jj + ii;
				if (nrrd_mvd->type == nrrdTypeUChar)
					value = double(((unsigned char*)data_mvd)[index]) *
					double(data_mvd_mask[index]) / 65025.0;
				else if (nrrd_mvd->type == nrrdTypeUShort)
					value = double(((unsigned short*)data_mvd)[index]) *
					m_vd->GetScalarScale() *
					double(data_mvd_mask[index]) / 16581375.0;
				if (value > thresh)
				{
					w = value>0.5?1.0:-16.0*value*value*value + 12.0*value*value;
					sump += Point(ii, jj, kk)*w;
					nw += w;
					m_ps_size += 1.0;
				}
			}

			//clear data_mvd_mask
			size_t set_num = res_x*res_y*res_z;
			memset(data_mvd_mask, 0, set_num);
            m_vd->GetVR()->clear_tex_current_mask();

			if (nw > 0.0)
			{
				m_ps_center = Point(sump.x()*spc_x, sump.y()*spc_y, -sump.z()*spc_z) / nw +
					Vector(0.5*spc_x, 0.5*spc_y, -0.5*spc_z);
				m_ps_size *= spc_x*spc_y*spc_z;
				m_ps = true;
				return 1;
			}
			else
				return 0;
}

//get center
int VolumeSelector::GetCenter(Point& p)
{
	p = m_ps_center;
	return m_ps;
}

//get volume
int VolumeSelector::GetSize(double &s)
{
	s = m_ps_size;
	return m_ps;
}

void VolumeSelector::GenerateAnnotations(bool use_sel)
{
	if (!m_vd ||
		!m_vd->GetMask(false) ||
		!m_vd->GetLabel(false) ||
		m_comps.size()==0)
	{
		m_annotations = 0;
		return;
	}
    
    int curLv = m_vd->GetLevel();
    if (m_vd->isBrxml())
        m_vd->SetLevel(m_vd->GetMaskLv());

    Texture* tex = m_vd->GetTexture();
    if (!tex)
        return;

	m_annotations = new Annotations();
	size_t nx, ny, nz;
	tex->get_dimensions(nx, ny, nz);
	double spcx, spcy, spcz;
    tex->get_spacings(spcx, spcy, spcz);

	double mul = 255.0;
	if (m_vd->GetTexture()->get_nrrd_raw(0)->type == nrrdTypeUChar)
		mul = 255.0;
	else if (m_vd->GetTexture()->get_nrrd_raw(0)->type == nrrdTypeUShort)
		mul = 65535.0;
	double total_int = 0.0;

	unordered_map <unsigned int, Component> :: const_iterator comp_iter;
	vector<Component> comps;
	for (comp_iter=m_comps.begin(); comp_iter!=m_comps.end(); comp_iter++)
		comps.push_back(comp_iter->second);
	sort(comps.begin(), comps.end(), [](const Component& a, const Component& b){return a.id < b.id;} );

	for (Component c : comps)
	{
		wxString str_id = wxString::Format("%d", c.id);
		Vector pos = c.acc_pos/c.counter;
		pos *= Vector(nx==0?0.0:1.0/nx,
			ny==0?0.0:1.0/ny,
			nz==0?0.0:1.0/nz);
		double intensity = mul * c.acc_int / c.counter;
		total_int += intensity;
		wxString str_info = wxString::Format("%llu\t%f\t%d",
			c.counter, double(c.counter)*(spcx*spcy*spcz), int(intensity+0.5));
		m_annotations->AddText(str_id.ToStdString(), Point(pos), str_info.ToStdString());
	}

	m_annotations->SetVolume(m_vd);
	m_annotations->SetTransform(m_vd->GetTexture()->transform());
	wxString info_meaning = "VOX_SIZE\tVOLUME\tAVG_VALUE";
	m_annotations->SetInfoMeaning(info_meaning);

	//memo
	wxString memo;
	memo += "Volume: " + m_vd->GetName() + "\n";
	memo += "Components: " + wxString::Format("%d", m_ca_comps) + "\n";
	memo += "Total volume: " + wxString::Format("%d", m_ca_volume) + "\n";
	memo += "Average value: " + wxString::Format("%d", int(total_int/m_ca_comps+0.5)) + "\n";
	memo += "\nSettings:\n";
	double threshold = m_label_thresh * m_vd->GetMaxValue();
	memo += "Threshold: " + wxString::Format("%f", threshold) + "\n";
	double falloff = m_label_falloff * m_vd->GetMaxValue();
	memo += "Falloff: " + wxString::Format("%f", falloff) + "\n";
	wxString str;
	if (use_sel)
		str = "Yes";
	else
		str = "No";
	memo += "Selected only: " + str + "\n";
	memo += "Minimum component size in voxel: " + wxString::Format("%d", int(m_min_voxels)) + "\n";
	if (m_max_voxels > 0.0)
		memo += "Maximum component size in voxel: " + wxString::Format("%d", int(m_max_voxels)) + "\n";
	else
		memo += "Maximum component size ignored\n";
	memo += "Iterations: " + wxString::Format("%d", m_iter_label);

	std::string str1 = memo.ToStdString();
	m_annotations->SetMemo(str1);
	m_annotations->SetMemoRO(true);

	Nrrd* label_nrrd = tex->get_nrrd_raw(tex->nlabel());
	if (!label_nrrd) return;
	unsigned int* label_data = (unsigned int*)(label_nrrd->data);
	if (!label_data) return;

	size_t voxelnum = (size_t)nx*(size_t)ny*(size_t)nz;

	Nrrd *copy = nrrdNew();
	unsigned int *val32 = new (std::nothrow) unsigned int[voxelnum];
	if (!val32)
	{
		wxMessageBox("Not enough memory. Please save project and restart.");
		return;
	}

	tex->get_spacings(spcx, spcy, spcz);
	nrrdWrap(copy, val32, nrrdTypeUInt, 3, (size_t)nx, (size_t)ny, (size_t)nz);
	nrrdAxisInfoSet(copy, nrrdAxisInfoSpacing, spcx, spcy, spcz);
	nrrdAxisInfoSet(copy, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
	nrrdAxisInfoSet(copy, nrrdAxisInfoMax, spcx*nx, spcy*ny, spcz*nz);
	nrrdAxisInfoSet(copy, nrrdAxisInfoSize, (size_t)nx, (size_t)ny, (size_t)nz);

	memcpy(val32, label_data, voxelnum*sizeof(unsigned int));
	auto sptr = std::make_shared<VL_Nrrd>(copy);
	m_annotations->SetLabel(sptr);
    
    if (m_vd->isBrxml())
        m_vd->SetLevel(curLv);
}

Annotations* VolumeSelector::GetAnnotations()
{
	return m_annotations;
}

int VolumeSelector::NoiseAnalysis(double min_voxels, double max_voxels, double bins, double thresh)
{
	if (!m_vd || m_vd->isBrxml())
		return 0;

	m_prog_diag = new wxProgressDialog(
		"FluoRender: Noise Analysis...",
		"Analyzing... Please wait.",
		100, 0,
		wxPD_SMOOTH|wxPD_ELAPSED_TIME|wxPD_AUTO_HIDE);
	m_progress = 0;

	int nx, ny, nz;
	m_vd->GetResolution(nx, ny, nz);
	m_iter_label = Max(nx, Max(ny, nz))/10;
	m_total_pr = m_iter_label+nx*3+1;

	if (m_prog_diag)
	{
		m_progress++;
		m_prog_diag->Update(95*(m_progress+1)/m_total_pr);
	}

	//first posterize the volume and put it into the mask
	m_vd->AddEmptyMask();
	if (m_use2d && m_2d_weight1 && m_2d_weight2)
		m_vd->Set2DWeight(m_2d_weight1, m_2d_weight2);
	else
	{
		auto blank = std::shared_ptr<vks::VTexture>();
		m_vd->Set2DWeight(blank, blank);
	}
	double ini_thresh, gm_falloff, scl_falloff;
	if (m_ini_thresh > 0.0)
		ini_thresh = m_ini_thresh;
	else
		ini_thresh = sqrt(thresh);
	gm_falloff = m_gm_falloff;
	if (m_scl_falloff > 0.0)
		scl_falloff = m_scl_falloff;
	else
		scl_falloff = 0.01;
	m_vd->DrawMask(0, 11, 0, ini_thresh, gm_falloff, scl_falloff, thresh, m_w2d, bins);

	//then label
	Label(1);
	m_vd->GetVR()->return_label();
	int return_val = CompIslandCount(min_voxels, max_voxels);

	delete m_prog_diag;

	return return_val;
}

void VolumeSelector::NoiseRemoval(int iter, double thresh, int mode)
{
	if (!m_vd || !m_vd->GetMask(false))
		return;

	m_prog_diag = new wxProgressDialog(
		"FluoRender: Component Analysis...",
		"Analyzing... Please wait.",
		100, 0,
		wxPD_SMOOTH|wxPD_ELAPSED_TIME|wxPD_AUTO_HIDE);
	m_progress = 0;
	m_total_pr = iter+1;

	double ini_thresh, gm_falloff, scl_falloff;
	if (m_ini_thresh > 0.0)
		ini_thresh = m_ini_thresh;
	else
		ini_thresh = sqrt(thresh);
	gm_falloff = m_gm_falloff;
	if (m_scl_falloff > 0.0)
		scl_falloff = m_scl_falloff;
	else
		scl_falloff = 0.01;

	if (mode == 0)
	{
		for (int i=0; i<iter; i++)
		{
			m_vd->DrawMask(2, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
			if (m_prog_diag)
			{
				m_progress++;
				m_prog_diag->Update(95*(m_progress+1)/m_total_pr);
			}
		}
		m_vd->DrawMask(0, 6, 0, ini_thresh, gm_falloff, scl_falloff, thresh, m_w2d, 0.0);
		m_vd->GetVR()->return_volume();
	}
	else if (mode == 1)
	{
		m_result_vols.clear();
		//get data and mask from the old volume
		VolumeData *vd_new = VolumeData::DeepCopy(*m_vd);
		if (!vd_new) return;
		//name etc
		vd_new->SetName(m_vd->GetName() + "_NR");
		
		Texture *tex = vd_new->GetTexture();
		if (!tex) return;
		Nrrd *nrrd_new = tex->get_nrrd_raw(0);
		int res_x, res_y, res_z;
		vd_new->GetResolution(res_x, res_y, res_z);
		int bytes = 1;
		if (nrrd_new->type == nrrdTypeUShort) bytes = 2;
		unsigned long long mem_size = (unsigned long long)res_x*
			(unsigned long long)res_y*(unsigned long long)res_z*(unsigned long long)bytes;

		for (int i=0; i<iter; i++)
		{
			vd_new->DrawMask(2, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
			if (m_prog_diag)
			{
				m_progress++;
				m_prog_diag->Update(95*(m_progress+1)/m_total_pr);
			}
		}
		vd_new->DrawMask(0, 6, 0, ini_thresh, gm_falloff, scl_falloff, thresh, m_w2d, 0.0);
		vd_new->GetVR()->return_volume();

		if (nrrd_new)
		{
			uint8 *val8nr = (uint8*)nrrd_new->data;
			int max_val = 255;
			if (nrrd_new->type == nrrdTypeUChar)
				max_val = *std::max_element(val8nr, val8nr+mem_size);
			else if (nrrd_new->type == nrrdTypeUShort)
				max_val = *std::max_element((uint16*)val8nr, (uint16*)val8nr+mem_size/2);
			vd_new->SetMaxValue(max_val);
		}

		m_result_vols.push_back(vd_new);
	}
	delete m_prog_diag;
}

void VolumeSelector::Test()
{
	/*  if (!m_vd)
	return;

	int nx, ny, nz;
	m_vd->GetResolution(nx, ny, nz);
	m_iter_label = Max(nx, Max(ny, nz))/10;

	int bins = 100;
	int value = 0;

	FILE* pfile = fopen("stats.bins", "wb");

	value = bins+1;
	fwrite(&value, sizeof(int), 1, pfile);

	for (int i=0; i<bins+1; i++)
	{
	m_label_thresh = double(i)/double(bins);
	Label(0);
	m_vd->GetVR()->return_label();
	CompIslandCount(0.0, -1.0);

	value = m_comps.size();
	fwrite(&value, sizeof(int), 1, pfile);

	unordered_map <unsigned int, Component> :: const_iterator comp_iter;
	for (comp_iter=m_comps.begin(); comp_iter!=m_comps.end(); comp_iter++)
	{
	value = comp_iter->second.counter;
	fwrite(&value, sizeof(int), 1, pfile);
	}
	}

	fclose(pfile);*/
}

