#include "DataManager.h"
#include "teem/Nrrd/nrrd.h"
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <wx/zipstrm.h>
#include <wx/url.h>
#include <wx/file.h>
#include <wx/stdpaths.h>
#include "utility.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <set>
#include <thread>
#include <mutex>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef _WIN32
//
//#include <vtkVersion.h>
//#include <vtkSmartPointer.h>
//#include <vtkImageImport.h>
//#include <vtkFlyingEdges3D.h>
//#include <vtkImageData.h>
//#include <vtkFloatArray.h>
//#include <vtkDoubleArray.h>
//#include <vtkPoints.h>
//#include <vtkPolyData.h>
//#include <vtkPolyDataNormals.h>
//#include <vtkPointData.h>
//#include <vtkCellData.h>

#include <omp.h>
#  undef min
#  undef max
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
double TreeLayer::m_sw = 0.0;

TreeLayer::TreeLayer()
{
	type = -1;
	m_gamma = Color(1.0, 1.0, 1.0);
	m_brightness = Color(1.0, 1.0, 1.0);
	m_hdr = Color(0.0, 0.0, 0.0);
    m_levels = Color(1.0, 1.0, 1.0);
	m_sync_r = m_sync_g = m_sync_b = false;
}

TreeLayer::~TreeLayer()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
VolumeData::VolumeData()
{
	m_reader = 0;

	m_dup = false;
	m_dup_counter = 0;

	type = 2;//volume

	//volume renderer and texture
	m_vr = 0;
	m_tex = 0;

	//current channel index
	m_chan = -1;
	m_time = 0;

	//mdoes
	m_mode = 0;
	//stream modes
	m_stream_mode = 0;

	//mask mode
	m_mask_mode = 0;
	m_use_mask_threshold = false;

	//volume properties
	m_scalar_scale = 1.0;
	m_gm_scale = 1.0;
	m_max_value = 255.0;
	//gamma
	m_gamma3d = 1.0;
	m_gm_thresh = 0.0;
	m_offset = 1.0;
	m_lo_thresh = 0.0;
	m_hi_thresh = 1.0;
	m_color = Color(1.0, 1.0, 1.0);
	SetHSV();
	m_alpha = 1.0;
	m_sample_rate = 1.0;
	m_mat_amb = 1.0;
	m_mat_diff = 1.0;
	m_mat_spec = 1.0;
	m_mat_shine = 10;
	//noise reduction
	m_noise_rd = false;
	//shading
	m_shading = false;
	//shadow
	m_shadow = false;
	m_shadow_darkness = 0.0;

	//resolution, scaling, spacing
	m_res_x = 0;	m_res_y = 0;	m_res_z = 0;
	m_sclx = 1.0;	m_scly = 1.0;	m_sclz = 1.0;
	m_spcx = 1.0;	m_spcy = 1.0;	m_spcz = 1.0;
	m_spc_from_file = false;

	//display control
	m_disp = true;
	m_draw_bounds = false;
	m_test_wiref = false;

	//colormap mode
	m_colormap_mode = 0;
	m_colormap_disp = false;
	m_colormap_low_value = 0.0;
	m_colormap_hi_value = 1.0;
	m_colormap = 0;
	m_colormap_proj = 0;
	m_id_col_disp_mode = 0;

	//blend mode
	m_blend_mode = 0;

	m_saved_mode = 0;

	m_2d_mask = 0;
	m_2d_weight1 = 0;
	m_2d_weight2 = 0;
	m_2d_dmap = 0;

	//clip distance
	m_clip_dist_x = 0;
	m_clip_dist_y = 0;
	m_clip_dist_z = 0;

	//legend
	m_legend = false;
	//interpolate
	m_interpolate = true;

	//valid brick number
	m_brick_num = 0;
	
	m_brkxml_mask = NULL;

	m_skip_brick = false;
}

/*
VolumeData::VolumeData(VolumeData &copy)
{
	m_reader = copy.m_reader;
	//duplication
	m_dup = true;
	copy.m_dup_counter++;
	m_dup_counter = copy.m_dup_counter;

	//layer properties
	type = 2;//volume
	SetName(copy.GetName()+wxString::Format("_%d", m_dup_counter));
	SetGamma(copy.GetGamma());
	SetBrightness(copy.GetBrightness());
	SetHdr(copy.GetHdr());
	SetSyncR(copy.GetSyncR());
	SetSyncG(copy.GetSyncG());
	SetSyncB(copy.GetSyncB());

	//path and bounds
	m_tex_path = copy.m_tex_path;
	m_bounds = copy.m_bounds;

	//volume renderer and texture
	m_vr = new VolumeRenderer(*copy.m_vr);
	m_tex = copy.m_tex;

	//current channel index
	m_chan = copy.m_chan;
	m_time = 0;

	//mdoes
	m_mode = copy.m_mode;
	//stream modes
	m_stream_mode = copy.m_stream_mode;

	//mask mode
	m_mask_mode = copy.m_mask_mode;
	m_use_mask_threshold = false;

	//volume properties
	m_scalar_scale = copy.m_scalar_scale;
	m_gm_scale = copy.m_gm_scale;
	m_max_value = copy.m_max_value;
	//gamma
	m_gamma3d = copy.m_gamma3d;
	m_gm_thresh = copy.m_gm_thresh;
	m_offset = copy.m_offset;
	m_lo_thresh = copy.m_lo_thresh;
	m_hi_thresh = copy.m_hi_thresh;
	m_color = copy.m_color;
	SetHSV();
	m_alpha = copy.m_alpha;
	m_sample_rate = copy.m_sample_rate;
	m_mat_amb = copy.m_mat_amb;
	m_mat_diff = copy.m_mat_diff;
	m_mat_spec = copy.m_mat_spec;
	m_mat_shine = copy.m_mat_shine;
	//noise reduction
	m_noise_rd = copy.m_noise_rd;
	//shading
	m_shading = copy.m_shading;
	//shadow
	m_shadow = copy.m_shadow;
	m_shadow_darkness = copy.m_shadow_darkness;

	//resolution, scaling, spacing
	m_res_x = copy.m_res_x;	m_res_y = copy.m_res_y;	m_res_z = copy.m_res_z;
	m_sclx = copy.m_sclx;	m_scly = copy.m_scly;	m_sclz = copy.m_sclz;
	m_spcx = copy.m_spcx;	m_spcy = copy.m_spcy;	m_spcz = copy.m_spcz;
	m_spc_from_file = copy.m_spc_from_file;

	//display control
	m_disp = copy.m_disp;
	m_draw_bounds = copy.m_draw_bounds;
	m_test_wiref = copy.m_test_wiref;

	//colormap mode
	m_colormap_mode = copy.m_colormap_mode;
	m_colormap_disp = copy.m_colormap_disp;
	m_colormap_low_value = copy.m_colormap_low_value;
	m_colormap_hi_value = copy.m_colormap_hi_value;
	m_colormap = copy.m_colormap;
	m_colormap_proj = copy.m_colormap_proj;
	m_id_col_disp_mode = copy.m_id_col_disp_mode;

	//blend mode
	m_blend_mode = copy.m_blend_mode;

	m_saved_mode = copy.m_saved_mode;

	m_2d_mask = 0;
	m_2d_weight1 = 0;f
	m_2d_weight2 = 0;
	m_2d_dmap = 0;

	//clip distance
	m_clip_dist_x = 0;
	m_clip_dist_y = 0;
	m_clip_dist_z = 0;

	//compression
	m_compression = false;

	//skip brick
	m_skip_brick = false;

	//legend
	m_legend = true;

	//interpolate
	m_interpolate = copy.m_interpolate;

	//valid brick number
	m_brick_num = 0;

	m_annotation = copy.m_annotation;

	m_landmarks = copy.m_landmarks;
}
*/

VolumeData::~VolumeData()
{
	if (m_vr)
		delete m_vr;
	if (m_tex)
		delete m_tex;
	if (m_brkxml_mask)
		delete m_brkxml_mask;
}

VolumeData* VolumeData::DeepCopy(VolumeData &copy, bool use_default_settings, DataManager *d_manager)
{
	VolumeData* vd = new VolumeData();

	vd->m_reader = copy.m_reader;
	//duplication
	vd->m_dup = true;
	copy.m_dup_counter++;
	vd->m_dup_counter = copy.m_dup_counter;

	Texture *tex = copy.GetTexture();
	if (!tex)
	{
		delete(vd);
		return NULL;
	}
	bool is_brxml = tex->isBrxml();
	int time = is_brxml ? 0 : copy.GetCurTime();
    shared_ptr<VL_Nrrd> nv;
    if (is_brxml)
    {
        nv = vd->m_reader->Convert(time, copy.GetCurChannel(), true);
        if (!nv)
            return NULL;
    }
    else
    {
		auto indata = copy.GetVolume(true);
        if (indata)
        {
			nv = make_shared<VL_Nrrd>(indata->getNrrdDeepCopy());
        }
        else
        {
            delete(vd);
            return NULL;
        }
    }

	if (is_brxml) vd->Load(nv, copy.GetName()+wxString::Format("_%d", vd->m_dup_counter), wxString(""), (BRKXMLReader*)vd->m_reader);
	else vd->Load(nv, copy.GetName()+wxString::Format("_%d", vd->m_dup_counter), wxString(""));

	if (use_default_settings && d_manager)
	{
		if (is_brxml) ((BRKXMLReader*)vd->m_reader)->SetLevel(0);
		vd->SetBaseSpacings(vd->m_reader->GetXSpc(),
			vd->m_reader->GetYSpc(),
			vd->m_reader->GetZSpc());
		bool valid_spc = vd->m_reader->IsSpcInfoValid();
		vd->SetSpcFromFile(valid_spc);
		vd->SetScalarScale(vd->m_reader->GetScalarScale());
		vd->SetMaxValue(vd->m_reader->GetMaxValue());
		vd->SetCurTime(time);
		vd->SetCurChannel(copy.GetCurChannel());

		vd->SetCompression(d_manager->GetCompression());
		d_manager->SetVolumeDefault(vd);

		//get excitation wavelength
		double wavelength = vd->m_reader->GetExcitationWavelength(copy.GetCurChannel());
		if (wavelength > 0.0) {
			FLIVR::Color col = d_manager->GetWavelengthColor(wavelength);
			vd->SetColor(col);
		}
		else if (wavelength < 0.) {
			FLIVR::Color white = Color(1.0, 1.0, 1.0);
			vd->SetColor(white);
		}
		else
		{
			FLIVR::Color white = Color(1.0, 1.0, 1.0);
			FLIVR::Color red   = Color(1.0, 0.0, 0.0);
			FLIVR::Color green = Color(0.0, 1.0, 0.0);
			FLIVR::Color blue  = Color(0.0, 0.0, 1.0);
			if (vd->m_reader->GetChanNum() == 1) {
				vd->SetColor(white);
			}
			else
			{
				if (copy.GetCurChannel() == 0)
					vd->SetColor(red);
				else if (copy.GetCurChannel() == 1)
					vd->SetColor(green);
				else if (copy.GetCurChannel() == 2)
					vd->SetColor(blue);
				else
					vd->SetColor(white);
			}
		}
	}
	else
	{
		//layer properties
		vd->type = 2;//volume
		vd->SetGamma(copy.GetGamma());
		vd->SetBrightness(copy.GetBrightness());
		vd->SetHdr(copy.GetHdr());
		vd->SetSyncR(copy.GetSyncR());
		vd->SetSyncG(copy.GetSyncG());
		vd->SetSyncB(copy.GetSyncB());

		double spc[3];
		if (tex->isBrxml())
		{
			copy.GetBaseSpacings(spc[0], spc[1], spc[2]);
			vd->SetBaseSpacings(spc[0], spc[1], spc[2]);
			copy.GetSpacingScales(spc[0], spc[1], spc[2]);
			vd->SetSpacingScales(spc[0], spc[1], spc[2]);
		}
		else
		{
			copy.GetSpacings(spc[0], spc[1], spc[2]);
			vd->SetSpacings(spc[0], spc[1], spc[2]);
		}
		double scl[3];
		copy.GetScalings(scl[0], scl[1], scl[2]);
		vd->SetScalings(scl[0], scl[1], scl[2]);

		vd->SetColor(copy.GetColor());
		bool bval = copy.GetEnableAlpha();
		vd->SetEnableAlpha(copy.GetEnableAlpha());
		vd->SetShading(copy.GetShading());
		vd->SetShadow(copy.GetShadow());
		double darkness;
		copy.GetShadowParams(darkness);
		vd->SetShadowParams(darkness);
		//other settings
		double amb, diff, spec, shine;
		copy.GetMaterial(amb, diff, spec, shine);
		vd->Set3DGamma(copy.Get3DGamma());
		vd->SetBoundary(copy.GetBoundary());
		vd->SetOffset(copy.GetOffset());
		vd->SetLeftThresh(copy.GetLeftThresh());
		vd->SetRightThresh(copy.GetRightThresh());
		vd->SetAlpha(copy.GetAlpha());
		vd->SetSampleRate(copy.GetSampleRate());
		vd->SetMaterial(amb, diff, spec, shine);

		//current channel index
		vd->m_chan = copy.m_chan;
		vd->m_time = time;

		//modes
		vd->SetMode(copy.GetMode());
		//stream modes
		vd->SetStreamMode(copy.GetStreamMode());

		//volume properties
		vd->SetScalarScale(copy.GetScalarScale());
		vd->SetGMScale(copy.GetGMScale());
		vd->SetHSV();

		//noise reduction
		vd->SetNR(copy.GetNR());

		//display control
		vd->SetDisp(copy.GetDisp());
		vd->SetDrawBounds(copy.GetDrawBounds());
		vd->m_test_wiref = copy.m_test_wiref;

		//colormap mode
		vd->SetColormapMode(copy.GetColormapMode());
		vd->SetColormapDisp(copy.GetColormapDisp());
		vd->SetColormapValues(copy.m_colormap_low_value, copy.m_colormap_hi_value);
		vd->m_colormap = copy.m_colormap;
		vd->m_colormap_proj = copy.m_colormap_proj;

		//blend mode
		vd->SetBlendMode(copy.GetBlendMode());

		vd->m_2d_mask = 0;
		vd->m_2d_weight1 = 0;
		vd->m_2d_weight2 = 0;
		vd->m_2d_dmap = 0;

		//clip distance
		int dists[3];
		copy.GetClipDistance(dists[0], dists[1], dists[2]);
		vd->SetClipDistance(dists[0], dists[1], dists[2]);

		//compression
		vd->m_compression = false;

		//skip brick
		vd->m_skip_brick = false;

		//legend
		vd->m_legend = false;

		//interpolate
		vd->m_interpolate = copy.m_interpolate;

		vd->m_annotation = copy.m_annotation;

		vd->m_landmarks = copy.m_landmarks;

		vd->SetMaskHideMode(copy.GetMaskHideMode());
	}

	if (is_brxml) vd->SetLevel(copy.GetLevel());

	auto maskindata = copy.GetMask(true);
	if (maskindata)
	{
		auto mask = make_shared<VL_Nrrd>(maskindata->getNrrdDeepCopy());
		vd->LoadMask(mask);
	}

	auto labelindata = copy.GetLabel(true);
	if (labelindata)
	{
		auto label = make_shared<VL_Nrrd>(labelindata->getNrrdDeepCopy());
		vd->LoadLabel(label);
	}

	auto strokeindata = copy.GetStroke(true);
	if (strokeindata)
	{
		auto stroke = make_shared<VL_Nrrd>(strokeindata->getNrrdDeepCopy());
		vd->LoadStroke(stroke);
	}

	return vd;
}

void VolumeData::FlipHorizontally()
{
	if (!m_tex || !m_vr || isBrxml()) return;
	shared_ptr<VL_Nrrd> vlnrrd = GetVolume(true);
	shared_ptr<VL_Nrrd> vlmask = GetMask(true);
	shared_ptr<VL_Nrrd> vllabel = GetLabel(true);
	shared_ptr<VL_Nrrd> vlstroke = GetStroke(true);

	Nrrd* nrrd = vlnrrd && vlnrrd->getNrrd() ? vlnrrd->getNrrd() : nullptr;
	Nrrd* mask = vlmask && vlmask->getNrrd() ? vlmask->getNrrd() : nullptr;
	Nrrd* label = vllabel && vllabel->getNrrd() ? vllabel->getNrrd() : nullptr;
	Nrrd* stroke = vlstroke && vlstroke->getNrrd() ? vlstroke->getNrrd() : nullptr;

	int iw, ih, id;
	GetResolution(iw, ih, id);
	size_t w = iw, h = ih, d = id;
	size_t linenum = h*d;
	size_t nthreads = std::thread::hardware_concurrency();
	if (nthreads > linenum) nthreads = linenum;
	if (nrrd)
	{
		std::vector<std::thread> threads(nthreads);
		if (nrrd->type == nrrdTypeUChar) {
			unsigned char *data = (unsigned char *)nrrd->data;
			for(size_t t = 0; t < nthreads; t++)
			{
				threads[t] = std::thread(std::bind(
					[&](const size_t bi, const size_t ei, const size_t t)
					{
						for(size_t i = bi; i<ei; i++)
						{
							size_t baseid = i*w;
							for (size_t j = 0; j < w/2; j++) {
								swap(data[baseid + w-j-1], data[baseid + j]);
							}
						}
					}, t*linenum/nthreads, (t+1)==nthreads ? linenum : (t+1)*linenum/nthreads, t));
			}
			std::for_each(threads.begin(),threads.end(),[](std::thread& x){x.join();});
		} else if (nrrd->type == nrrdTypeUShort) {
			unsigned short *data = (unsigned short *)nrrd->data;
			for(size_t t = 0; t < nthreads; t++)
			{
				threads[t] = std::thread(std::bind(
					[&](const size_t bi, const size_t ei, const size_t t)
				{
					for(size_t i = bi; i<ei; i++)
					{
						size_t baseid = i*w;
						for (size_t j = 0; j < w/2; j++) {
							swap(data[baseid + w-j-1], data[baseid + j]);
						}
					}
				}, t*linenum/nthreads, (t+1)==nthreads ? linenum : (t+1)*linenum/nthreads, t));
			}
			std::for_each(threads.begin(),threads.end(),[](std::thread& x){x.join();});
		} else if (nrrd->type == nrrdTypeFloat) {
			float* data = (float*)nrrd->data;
			for (size_t t = 0; t < nthreads; t++)
			{
				threads[t] = std::thread(std::bind(
					[&](const size_t bi, const size_t ei, const size_t t)
				{
					for (size_t i = bi; i < ei; i++)
					{
						size_t baseid = i * w;
						for (size_t j = 0; j < w / 2; j++) {
							swap(data[baseid + w - j - 1], data[baseid + j]);
						}
					}
				}, t * linenum / nthreads, (t + 1) == nthreads ? linenum : (t + 1) * linenum / nthreads, t));
			}
			std::for_each(threads.begin(), threads.end(), [](std::thread& x) {x.join(); });
		}
	}

	if (mask)
	{
		std::vector<std::thread> threads(nthreads);
		unsigned char *data = (unsigned char *)mask->data;
		for(size_t t = 0; t < nthreads; t++)
		{
			threads[t] = std::thread(std::bind(
				[&](const size_t bi, const size_t ei, const size_t t)
			{
				for(size_t i = bi; i<ei; i++)
				{
					size_t baseid = i*w;
					for (size_t j = 0; j < w/2; j++) {
						swap(data[baseid + w-j-1], data[baseid + j]);
					}
				}
			}, t*linenum/nthreads, (t+1)==nthreads ? linenum : (t+1)*linenum/nthreads, t));
		}
		std::for_each(threads.begin(),threads.end(),[](std::thread& x){x.join();});
	}

	if (label)
	{
		std::vector<std::thread> threads(nthreads);
		unsigned int *data = (unsigned int *)label->data;
		for(size_t t = 0; t < nthreads; t++)
		{
			threads[t] = std::thread(std::bind(
				[&](const size_t bi, const size_t ei, const size_t t)
			{
				for(size_t i = bi; i<ei; i++)
				{
					size_t baseid = i*w;
					for (size_t j = 0; j < w/2; j++) {
						swap(data[baseid + w-j-1], data[baseid + j]);
					}
				}
			}, t*linenum/nthreads, (t+1)==nthreads ? linenum : (t+1)*linenum/nthreads, t));
		}
		std::for_each(threads.begin(),threads.end(),[](std::thread& x){x.join();});
	}

	if (stroke)
	{
		std::vector<std::thread> threads(nthreads);
		unsigned char *data = (unsigned char *)stroke->data;
		for(size_t t = 0; t < nthreads; t++)
		{
			threads[t] = std::thread(std::bind(
				[&](const size_t bi, const size_t ei, const size_t t)
			{
				for(size_t i = bi; i<ei; i++)
				{
					size_t baseid = i*w;
					for (size_t j = 0; j < w/2; j++) {
						swap(data[baseid + w-j-1], data[baseid + j]);
					}
				}
			}, t*linenum/nthreads, (t+1)==nthreads ? linenum : (t+1)*linenum/nthreads, t));
		}
		std::for_each(threads.begin(),threads.end(),[](std::thread& x){x.join();});
	}

	m_vr->clear_tex_current();
}

void VolumeData::FlipVertically()
{
	if (!m_tex || !m_vr || isBrxml()) return;
	shared_ptr<VL_Nrrd> vlnrrd = GetVolume(true);
	shared_ptr<VL_Nrrd> vlmask = GetMask(true);
	shared_ptr<VL_Nrrd> vllabel = GetLabel(true);
	shared_ptr<VL_Nrrd> vlstroke = GetStroke(true);

	Nrrd* nrrd = vlnrrd && vlnrrd->getNrrd() ? vlnrrd->getNrrd() : nullptr;
	Nrrd* mask = vlmask && vlmask->getNrrd() ? vlmask->getNrrd() : nullptr;
	Nrrd* label = vllabel && vllabel->getNrrd() ? vllabel->getNrrd() : nullptr;
	Nrrd* stroke = vlstroke && vlstroke->getNrrd() ? vlstroke->getNrrd() : nullptr;

	int iw, ih, id;
	GetResolution(iw, ih, id);
	size_t w = iw, h = ih, d = id;

	size_t nthreads = std::thread::hardware_concurrency();
	if (nthreads > d) nthreads = d;
	if (nrrd)
	{
		std::vector<std::thread> threads(nthreads);
		if (nrrd->type == nrrdTypeUChar) {
			unsigned char *data = (unsigned char *)nrrd->data;
			for (size_t t = 0; t < nthreads; t++)
			{
				threads[t] = std::thread(std::bind(
					[&](const size_t bi, const size_t ei, const size_t t)
				{
					for (size_t z = bi; z < ei; z++)
					for (size_t x = 0; x < w; x++)
					{
						size_t baseid = z * w*h + x;
						for (size_t y = 0; y < h / 2; y++)
							swap(data[baseid + (h - y - 1)*w], data[baseid + y * w]);
					}
				}, t*d / nthreads, (t + 1) == nthreads ? d : (t + 1)*d / nthreads, t));
			}
			std::for_each(threads.begin(), threads.end(), [](std::thread& x) {x.join(); });
		}
		else if (nrrd->type == nrrdTypeUShort) {
			unsigned short *data = (unsigned short *)nrrd->data;
			for (size_t t = 0; t < nthreads; t++)
			{
				threads[t] = std::thread(std::bind(
					[&](const size_t bi, const size_t ei, const size_t t)
				{
					for (size_t z = bi; z < ei; z++)
					for (size_t x = 0; x < w; x++)
					{
						size_t baseid = z * w*h + x;
						for (size_t y = 0; y < h / 2; y++)
							swap(data[baseid + (h - y - 1)*w], data[baseid + y * w]);
					}
				}, t*d / nthreads, (t + 1) == nthreads ? d : (t + 1)*d / nthreads, t));
			}
			std::for_each(threads.begin(), threads.end(), [](std::thread& x) {x.join(); });
		}
		else if (nrrd->type == nrrdTypeFloat) {
			float* data = (float*)nrrd->data;
			for (size_t t = 0; t < nthreads; t++)
			{
				threads[t] = std::thread(std::bind(
					[&](const size_t bi, const size_t ei, const size_t t)
				{
					for (size_t z = bi; z < ei; z++)
						for (size_t x = 0; x < w; x++)
						{
							size_t baseid = z * w * h + x;
							for (size_t y = 0; y < h / 2; y++)
								swap(data[baseid + (h - y - 1) * w], data[baseid + y * w]);
						}
				}, t * d / nthreads, (t + 1) == nthreads ? d : (t + 1) * d / nthreads, t));
			}
			std::for_each(threads.begin(), threads.end(), [](std::thread& x) {x.join(); });
		}
	}
	
	if (mask)
	{
		std::vector<std::thread> threads(nthreads);
		unsigned char *data = (unsigned char *)mask->data;
		for (size_t t = 0; t < nthreads; t++)
		{
			threads[t] = std::thread(std::bind(
				[&](const size_t bi, const size_t ei, const size_t t)
			{
				for (size_t z = bi; z < ei; z++)
				for (size_t x = 0; x < w; x++)
				{
					size_t baseid = z * w*h + x;
					for (size_t y = 0; y < h / 2; y++)
						swap(data[baseid + (h - y - 1)*w], data[baseid + y * w]);
				}
			}, t*d / nthreads, (t + 1) == nthreads ? d : (t + 1)*d / nthreads, t));
		}
		std::for_each(threads.begin(), threads.end(), [](std::thread& x) {x.join(); });
	}

	if (label)
	{
		std::vector<std::thread> threads(nthreads);
		unsigned int *data = (unsigned int *)label->data;
		for (size_t t = 0; t < nthreads; t++)
		{
			threads[t] = std::thread(std::bind(
				[&](const size_t bi, const size_t ei, const size_t t)
			{
				for (size_t z = bi; z < ei; z++)
				for (size_t x = 0; x < w; x++)
				{
					size_t baseid = z * w*h + x;
					for (size_t y = 0; y < h / 2; y++)
						swap(data[baseid + (h - y - 1)*w], data[baseid + y * w]);
				}
			}, t*d / nthreads, (t + 1) == nthreads ? d : (t + 1)*d / nthreads, t));
		}
		std::for_each(threads.begin(), threads.end(), [](std::thread& x) {x.join(); });
	}

	if (stroke)
	{
		std::vector<std::thread> threads(nthreads);
		unsigned char *data = (unsigned char *)stroke->data;
		for (size_t t = 0; t < nthreads; t++)
		{
			threads[t] = std::thread(std::bind(
				[&](const size_t bi, const size_t ei, const size_t t)
			{
				for (size_t z = bi; z < ei; z++)
				for (size_t x = 0; x < w; x++)
				{
					size_t baseid = z * w*h + x;
					for (size_t y = 0; y < h / 2; y++)
						swap(data[baseid + (h - y - 1)*w], data[baseid + y * w]);
				}
			}, t*d / nthreads, (t + 1) == nthreads ? d : (t + 1)*d / nthreads, t));
		}
		std::for_each(threads.begin(), threads.end(), [](std::thread& x) {x.join(); });
	}

	m_vr->clear_tex_current();
}

//duplication
bool VolumeData::GetDup()
{
	return m_dup;
}

//increase duplicate counter
void VolumeData::IncDupCounter()
{
	m_dup_counter++;
}

//data related
//compression
void VolumeData::SetCompression(bool compression)
{
	m_compression = compression;
	if (m_vr)
		m_vr->set_compression(compression);
}

bool VolumeData::GetCompression()
{
	return m_compression;
}

//skip brick
void VolumeData::SetSkipBrick(bool skip)
{
	m_skip_brick = skip;
}

bool VolumeData::GetSkipBrick()
{
	return m_skip_brick;
}

int VolumeData::Load(const shared_ptr<VL_Nrrd> &data, const wxString &name, const wxString &path, BRKXMLReader *breader)
{
	if (!data || !data->getNrrd() || data->getNrrd()->dim != 3)
		return 0;

	m_tex_path = path;
	m_name = name;

	if (m_tex)
	{
		delete m_tex;
		m_tex = NULL;
	}

	Nrrd *nv = data->getNrrd();
	Nrrd *gm = 0;
	m_res_x = nv->axis[0].size;
	m_res_y = nv->axis[1].size;
	m_res_z = nv->axis[2].size;

	BBox bounds;
	Point pmax(nv->axis[0].max, nv->axis[1].max, nv->axis[2].max);
	Point pmin(nv->axis[0].min, nv->axis[1].min, nv->axis[2].min);
	bounds.extend(pmin);
	bounds.extend(pmax);
	m_bounds = bounds;

	m_tex = new Texture();
	m_tex->set_use_priority(m_skip_brick);
	if(breader)
	{
		vector<Pyramid_Level> pyramid;
		vector<vector<vector<vector<FileLocInfo *>>>> fnames;
		int ftype = BRICK_FILE_TYPE_NONE;

		breader->build_pyramid(pyramid, fnames, 0, breader->GetCurChan());

		int lmnum = breader->GetLandmarkNum();
		for (int j = 0; j < lmnum; j++)
		{
			wstring name;
			VD_Landmark vlm;
			breader->GetLandmark(j, vlm.name, vlm.x, vlm.y, vlm.z, vlm.spcx, vlm.spcy, vlm.spcz);
			m_landmarks.push_back(vlm);
			breader->GetMetadataID(m_metadata_id);
		}
		if (!m_tex->buildPyramid(pyramid, fnames, breader->isURL())) return 0;
        
        m_tex->SetCopyableLevel(breader->GetCopyableLevel());
        m_tex->SetMaskLv(breader->GetMaskLevel());
	}
	else if(!m_tex->build(data, nullptr, 0, 256, 0, 0)) return 0;
	
	if (m_tex)
	{
		if (m_vr)
			delete m_vr;

		vector<Plane*> planelist(0);
		Plane* plane = 0;
		//x
		plane = new Plane(Point(0.0, 0.0, 0.0), Vector(1.0, 0.0, 0.0));
		planelist.push_back(plane);
		plane = new Plane(Point(1.0, 0.0, 0.0), Vector(-1.0, 0.0, 0.0));
		planelist.push_back(plane);
		//y
		plane = new Plane(Point(0.0, 0.0, 0.0), Vector(0.0, 1.0, 0.0));
		planelist.push_back(plane);
		plane = new Plane(Point(0.0, 1.0, 0.0), Vector(0.0, -1.0, 0.0));
		planelist.push_back(plane);
		//z
		plane = new Plane(Point(0.0, 0.0, 0.0), Vector(0.0, 0.0, 1.0));
		planelist.push_back(plane);
		plane = new Plane(Point(0.0, 0.0, 1.0), Vector(0.0, 0.0, -1.0));
		planelist.push_back(plane);
        
        planelist[0]->SetRange(planelist[0]->get_point(), planelist[0]->normal(), planelist[1]->get_point(), planelist[1]->normal());
        planelist[1]->SetRange(planelist[1]->get_point(), planelist[1]->normal(), planelist[0]->get_point(), planelist[0]->normal());
        planelist[2]->SetRange(planelist[2]->get_point(), planelist[2]->normal(), planelist[3]->get_point(), planelist[3]->normal());
        planelist[3]->SetRange(planelist[3]->get_point(), planelist[3]->normal(), planelist[2]->get_point(), planelist[2]->normal());
        planelist[4]->SetRange(planelist[4]->get_point(), planelist[4]->normal(), planelist[5]->get_point(), planelist[5]->normal());
        planelist[5]->SetRange(planelist[5]->get_point(), planelist[5]->normal(), planelist[4]->get_point(), planelist[4]->normal());

		m_vr = new VolumeRenderer(m_tex, planelist, true);
		m_vr->set_sampling_rate(m_sample_rate);
		m_vr->set_material(m_mat_amb, m_mat_diff, m_mat_spec, m_mat_shine);
		m_vr->set_shading(true);
		m_vr->set_scalar_scale(m_scalar_scale);
		m_vr->set_gm_scale(m_scalar_scale);

		SetMode(m_mode);

		if (breader)
		{
			wstring tree = breader->GetROITree();
			if (!tree.empty())
			{
				ImportROITree(tree);
				SetColormapMode(3);//ID color mode
				SelectAllNamedROI();
			}
		}
	}

	return 1;
}

int VolumeData::Replace(const std::shared_ptr<VL_Nrrd>& data, bool del_tex)
{
	if (!data || !data->getNrrd() || data->getNrrd()->dim != 3)
		return 0;
	if (m_tex && m_tex->get_nrrd(0) == data)
		return 0;
	
	double spcx = 1.0, spcy = 1.0, spcz = 1.0;
	if (del_tex)
	{
		Nrrd *nv = data->getNrrd();
		Nrrd *gm = 0;
		m_res_x = nv->axis[0].size;
		m_res_y = nv->axis[1].size;
		m_res_z = nv->axis[2].size;

		if (m_vr) {
			m_vr->clear_tex_current();
			m_vr->reset_texture();
		}
		if (m_tex) {
			m_tex->get_spacings(spcx, spcy, spcz);
			delete m_tex;
		}
		m_tex = new Texture();
		m_tex->set_use_priority(m_skip_brick);
		m_tex->build(data, nullptr, 0, m_max_value, 0, 0);
		m_tex->set_spacings(spcx, spcy, spcz);
	}
	else
	{
		//set new
		m_tex->set_nrrd(data, 0);
	}

	//clear pool
	if (m_vr)
		m_vr->set_texture(m_tex);
	else
		return 0;

	return 1;
}

int VolumeData::Replace(VolumeData* data)
{
	if (!data ||
		m_res_x!=data->m_res_x ||
		m_res_y!=data->m_res_y ||
		m_res_z!=data->m_res_z)
		return 0;

	double spcx = 1.0, spcy = 1.0, spcz = 1.0;

	if (m_tex && m_vr)
	{
		m_tex->get_spacings(spcx, spcy, spcz);
		m_vr->clear_tex_current();
		delete m_tex;
		m_vr->reset_texture();
	}
	m_tex = data->GetTexture();
	data->SetTexture();
	SetScalarScale(data->GetScalarScale());
	SetGMScale(data->GetGMScale());
	SetMaxValue(data->GetMaxValue());
	if (m_vr)
		m_vr->set_texture(m_tex);
	else
		return 0;
	return 1;
}

//volume data
void VolumeData::AddEmptyData(int bits,
	int nx, int ny, int nz,
	double spcx, double spcy, double spcz)
{
	if (bits!=8 && bits!=16 && bits!=32)
		return;

	if (m_vr)
		delete m_vr;
	if (m_tex)
		delete m_tex;

	Nrrd *nv = nrrdNew();
	if (bits == 8)
	{
		unsigned long long mem_size = (unsigned long long)nx*
			(unsigned long long)ny*(unsigned long long)nz;
		uint8 *val8 = new (std::nothrow) uint8[mem_size];
		if (!val8)
		{
			wxMessageBox("Not enough memory. Please save project and restart.");
			return;
		}
		memset((void*)val8, 0, sizeof(uint8)*nx*ny*nz);
		nrrdWrap(nv, val8, nrrdTypeUChar, 3, (size_t)nx, (size_t)ny, (size_t)nz);
	}
	else if (bits == 16)
	{
		unsigned long long mem_size = (unsigned long long)nx*
			(unsigned long long)ny*(unsigned long long)nz;
		uint16 *val16 = new (std::nothrow) uint16[mem_size];
		if (!val16)
		{
			wxMessageBox("Not enough memory. Please save project and restart.");
			return;
		}
		memset((void*)val16, 0, sizeof(uint16)*nx*ny*nz);
		nrrdWrap(nv, val16, nrrdTypeUShort, 3, (size_t)nx, (size_t)ny, (size_t)nz);
	}
	else if (bits == 32)
	{
		unsigned long long mem_size = (unsigned long long)nx *
			(unsigned long long)ny * (unsigned long long)nz;
		float* val32 = new (std::nothrow) float[mem_size];
		if (!val32)
		{
			wxMessageBox("Not enough memory. Please save project and restart.");
			return;
		}
		memset((void*)val32, 0, sizeof(float) * nx * ny * nz);
		nrrdWrap(nv, val32, nrrdTypeFloat, 3, (size_t)nx, (size_t)ny, (size_t)nz);
	}
	nrrdAxisInfoSet(nv, nrrdAxisInfoSpacing, spcx, spcy, spcz);
	nrrdAxisInfoSet(nv, nrrdAxisInfoMax, spcx*nx, spcy*ny, spcz*nz);
	nrrdAxisInfoSet(nv, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
	nrrdAxisInfoSet(nv, nrrdAxisInfoSize, (size_t)nx, (size_t)ny, (size_t)nz);

	//resolution
	m_res_x = nv->axis[0].size;
	m_res_y = nv->axis[1].size;
	m_res_z = nv->axis[2].size;

	//bounding box
	BBox bounds;
	Point pmax(nv->axis[0].max, nv->axis[1].max, nv->axis[2].max);
	Point pmin(nv->axis[0].min, nv->axis[1].min, nv->axis[2].min);
	bounds.extend(pmin);
	bounds.extend(pmax);
	m_bounds = bounds;

	//create texture
	m_tex = new Texture();
	m_tex->set_use_priority(false);
	shared_ptr<VL_Nrrd> sptr = make_shared<VL_Nrrd>(nv);
	m_tex->build(sptr, 0, 0, 256, 0, 0);
    SetBaseSpacings(spcx, spcy, spcz);

	//clipping planes
	vector<Plane*> planelist(0);
	Plane* plane = 0;
	//x
	plane = new Plane(Point(0.0, 0.0, 0.0), Vector(1.0, 0.0, 0.0));
	planelist.push_back(plane);
	plane = new Plane(Point(1.0, 0.0, 0.0), Vector(-1.0, 0.0, 0.0));
	planelist.push_back(plane);
	//y
	plane = new Plane(Point(0.0, 0.0, 0.0), Vector(0.0, 1.0, 0.0));
	planelist.push_back(plane);
	plane = new Plane(Point(0.0, 1.0, 0.0), Vector(0.0, -1.0, 0.0));
	planelist.push_back(plane);
	//z
	plane = new Plane(Point(0.0, 0.0, 0.0), Vector(0.0, 0.0, 1.0));
	planelist.push_back(plane);
	plane = new Plane(Point(0.0, 0.0, 1.0), Vector(0.0, 0.0, -1.0));
	planelist.push_back(plane);
    
    planelist[0]->SetRange(planelist[0]->get_point(), planelist[0]->normal(), planelist[1]->get_point(), planelist[1]->normal());
    planelist[1]->SetRange(planelist[1]->get_point(), planelist[1]->normal(), planelist[0]->get_point(), planelist[0]->normal());
    planelist[2]->SetRange(planelist[2]->get_point(), planelist[2]->normal(), planelist[3]->get_point(), planelist[3]->normal());
    planelist[3]->SetRange(planelist[3]->get_point(), planelist[3]->normal(), planelist[2]->get_point(), planelist[2]->normal());
    planelist[4]->SetRange(planelist[4]->get_point(), planelist[4]->normal(), planelist[5]->get_point(), planelist[5]->normal());
    planelist[5]->SetRange(planelist[5]->get_point(), planelist[5]->normal(), planelist[4]->get_point(), planelist[4]->normal());

	//create volume renderer
	m_vr = new VolumeRenderer(m_tex, planelist, true);
	m_vr->set_sampling_rate(m_sample_rate);
	m_vr->set_material(m_mat_amb, m_mat_diff, m_mat_spec, m_mat_shine);
	m_vr->set_shading(true);
	m_vr->set_scalar_scale(m_scalar_scale);
	m_vr->set_gm_scale(m_scalar_scale);

	SetMode(m_mode);
}

//volume mask
bool VolumeData::LoadMask(const std::shared_ptr<VL_Nrrd>& mask)
{
	if (!mask || !mask->getNrrd() || !m_tex || !m_vr)
		return false;

	//prepare the texture bricks for the mask
	Nrrd* mnrrd = mask->getNrrd();

	size_t dim = mnrrd->dim;
	std::vector<int> size(dim);
	int offset = 0;
	if (dim > 3) offset = 1; 
	for (size_t p = 0; p < dim; p++) 
		size[p] = (int)mnrrd->axis[p + offset].size;
	int mskw = size[0];
	int mskh = size[1];
	int mskd = size[2];
    
	int w, h, d;
    int curlv = -1;
    if (isBrxml())
    {
        curlv = GetLevel();
        
        bool found = false;
        int lvnum = GetLevelNum();
        for (int i = 0; i < lvnum; i++)
        {
            SetLevel(i);
            m_tex->GetDimensionLv(GetLevel(), w, h, d);
            if (w == mskw && h == mskh && d == mskd)
            {
                found = true;
                m_tex->SetMaskLv(i);
                break;
            }
        }
        if (!found)
            return false;
    }
    else
    {
        m_tex->GetDimensionLv(GetLevel(), w, h, d);
        if (w != mskw || h != mskh || d != mskd)
            return false;
    }

	m_vr->clear_tex_current_mask();
	m_tex->add_empty_mask();
	if (!isBrxml()) AddEmptyStroke();
	m_tex->set_nrrd(mask, m_tex->nmask());

	if (isBrxml())
		SetLevel(curlv);

	return true;
}

void VolumeData::DeleteMask()
{
	if (!m_tex || !m_vr)
		return;

	int curlv = -1;
	if (isBrxml())
	{
		curlv = GetLevel();
		SetLevel(GetMaskLv());
	}

	m_tex->delete_mask();

	if (isBrxml())
		SetLevel(curlv);
}

void VolumeData::LoadStroke(const std::shared_ptr<VL_Nrrd>& stroke)
{
	if (!stroke || !m_tex || !m_vr)
		return;

	int curlv = -1;
	if (isBrxml())
	{
		curlv = GetLevel();
		SetLevel(GetMaskLv());
	}

	//prepare the texture bricks for the mask
	m_tex->add_empty_stroke();
	m_tex->set_nrrd(stroke, m_tex->nstroke());

	if (isBrxml())
		SetLevel(curlv);
}

void VolumeData::DeleteStroke()
{
	if (!m_tex || !m_vr)
		return;

	m_tex->delete_stroke();
}

void VolumeData::AddEmptyMask()
{
	if (!m_tex || !m_vr)
		return;

	int curlv = -1;
	if (isBrxml())
	{
		curlv = GetLevel();
		SetLevel(GetMaskLv());
	}

	size_t res_x = m_tex->nx();
	size_t res_y = m_tex->ny();
	size_t res_z = m_tex->nz();

	//prepare the texture bricks for the mask
	if (m_tex->add_empty_mask())
	{
		//add the nrrd data for mask
		Nrrd *nrrd_mask = nrrdNew();
		size_t mem_size = res_x*res_y*res_z;
		uint8 *val8 = new (std::nothrow) uint8[mem_size];
		if (!val8)
		{
			wxMessageBox("Not enough memory. Please save project and restart.");
			return;
		}
		double spcx, spcy, spcz;
		m_tex->get_spacings(spcx, spcy, spcz);
		memset((void*)val8, 0, mem_size*sizeof(uint8));
		nrrdWrap(nrrd_mask, val8, nrrdTypeUChar, 3, res_x, res_y, res_z);
		nrrdAxisInfoSet(nrrd_mask, nrrdAxisInfoSize, res_x, res_y, res_z);
		nrrdAxisInfoSet(nrrd_mask, nrrdAxisInfoSpacing, spcx, spcy, spcz);
		nrrdAxisInfoSet(nrrd_mask, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
		nrrdAxisInfoSet(nrrd_mask, nrrdAxisInfoMax, spcx*res_x, spcy*res_y, spcz*res_z);

		auto sptr = make_shared<VL_Nrrd>(nrrd_mask);
		m_tex->set_nrrd(sptr, m_tex->nmask());
	}

	//if (!isBrxml())
		AddEmptyStroke();

	if (isBrxml())
		SetLevel(curlv);
}

void VolumeData::AddEmptyStroke()
{
	if (!m_tex || !m_vr)
		return;

	int curlv = -1;
	if (isBrxml())
	{
		curlv = GetLevel();
		SetLevel(GetMaskLv());
	}

	size_t res_x = m_tex->nx();
	size_t res_y = m_tex->ny();
	size_t res_z = m_tex->nz();

	if (m_tex->add_empty_stroke())
	{
		//add the nrrd data for mask
		Nrrd *nrrd_stroke = nrrdNew();
		size_t mem_size = res_x*res_y*res_z;
		uint8 *val8 = new (std::nothrow) uint8[mem_size];
		if (!val8)
		{
			wxMessageBox("Not enough memory. Please save project and restart.");
			return;
		}
		double spcx, spcy, spcz;
		m_tex->get_spacings(spcx, spcy, spcz);
		memset((void*)val8, 0, mem_size*sizeof(uint8));
		nrrdWrap(nrrd_stroke, val8, nrrdTypeUChar, 3, res_x, res_y, res_z);
		nrrdAxisInfoSet(nrrd_stroke, nrrdAxisInfoSize, res_x, res_y, res_z);
		nrrdAxisInfoSet(nrrd_stroke, nrrdAxisInfoSpacing, spcx, spcy, spcz);
		nrrdAxisInfoSet(nrrd_stroke, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
		nrrdAxisInfoSet(nrrd_stroke, nrrdAxisInfoMax, spcx*res_x, spcy*res_y, spcz*res_z);

		auto sptr = make_shared<VL_Nrrd>(nrrd_stroke);
		m_tex->set_nrrd(sptr, m_tex->nstroke());
	}

	if (isBrxml())
		SetLevel(curlv);
}

//volume label
void VolumeData::LoadLabel(const std::shared_ptr<VL_Nrrd>& label)
{
	if (!label || !label->getNrrd() || !m_tex || !m_vr)
		return;

	int nb = label->getBytesPerSample();
	m_tex->add_empty_label(nb);
	m_tex->set_nrrd(label, m_tex->nlabel());
}

void VolumeData::DeleteLabel()
{
	if (!m_tex || !m_vr)
		return;

	m_tex->delete_label();
}

void VolumeData::SetOrderedID(unsigned int* val)
{
	for (int i=0; i<m_res_x; i++)
		for (int j=0; j<m_res_y; j++)
			for (int k=0; k<m_res_z; k++)
			{
				unsigned int index = m_res_y*m_res_z*i + m_res_z*j + k;
				val[index] = index+1;
			}
}

void VolumeData::SetReverseID(unsigned int* val)
{
	for (int i=0; i<m_res_x; i++)
		for (int j=0; j<m_res_y; j++)
			for (int k=0; k<m_res_z; k++)
			{
				unsigned int index = m_res_y*m_res_z*i + m_res_z*j + k;
				val[index] = m_res_x*m_res_y*m_res_z - index;
			}
}

void VolumeData::SetShuffledID(unsigned int* val)
{
	unsigned int x, y, z;
	unsigned int res;
	unsigned int len = 0;
	unsigned int r = Max(m_res_x, Max(m_res_y, m_res_z));
	while (r > 0)
	{
		r /= 2;
		len++;
	}
	for (int i=0; i<m_res_x; i++)
		for (int j=0; j<m_res_y; j++)
			for (int k=0; k<m_res_z; k++)
			{
				x = reverse_bit(i, len);
				y = reverse_bit(j, len);
				z = reverse_bit(k, len);
				res = 0;
				for (unsigned int ii=0; ii<len; ii++)
				{
					res |= (1<<ii & x)<<(2*ii);
					res |= (1<<ii & y)<<(2*ii+1);
					res |= (1<<ii & z)<<(2*ii+2);
				}
				unsigned int index = m_res_x*m_res_y*k + m_res_x*j + i;
				val[index] = m_res_x*m_res_y*m_res_z - res;
			}
}

void VolumeData::AddEmptyLabel(int mode)
{
	if (!m_tex || !m_vr)
		return;

	int curlv = -1;
	if (isBrxml())
	{
		curlv = GetLevel();
		SetLevel(GetMaskLv());
	}

	size_t res_x = m_tex->nx();
	size_t res_y = m_tex->ny();
	size_t res_z = m_tex->nz();

	Nrrd *nrrd_label = 0;
	unsigned int *val32 = 0;
	//prepare the texture bricks for the labeling mask
	if (m_tex->add_empty_label())
	{
		//add the nrrd data for the labeling mask
		nrrd_label = nrrdNew();
		size_t mem_size = res_x*res_y*res_z;
		val32 = new (std::nothrow) unsigned int[mem_size];
		if (!val32)
		{
			wxMessageBox("Not enough memory. Please save project and restart.");
			return;
		}

		double spcx, spcy, spcz;
		m_tex->get_spacings(spcx, spcy, spcz);
		nrrdWrap(nrrd_label, val32, nrrdTypeUInt, 3, res_x, res_y, res_z);
		nrrdAxisInfoSet(nrrd_label, nrrdAxisInfoSpacing, spcx, spcy, spcz);
		nrrdAxisInfoSet(nrrd_label, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
		nrrdAxisInfoSet(nrrd_label, nrrdAxisInfoMax, res_x, res_y, res_z);
		nrrdAxisInfoSet(nrrd_label, nrrdAxisInfoSize, res_x, res_y, res_z);

		auto sptr = make_shared<VL_Nrrd>(nrrd_label);
		m_tex->set_nrrd(sptr, m_tex->nlabel());

		//apply values
		switch (mode)
		{
		case 0://zeros
			memset(val32, 0, sizeof(unsigned int) * (unsigned long long)res_x *
				(unsigned long long)res_y * (unsigned long long)res_z);
			break;
		case 1://ordered
			SetOrderedID(val32);
			break;
		case 2://shuffled
			SetShuffledID(val32);
			break;
		}
	}
	else
	{
		auto vlnrrd_label = m_tex->get_nrrd(m_tex->nlabel());
		if (vlnrrd_label && vlnrrd_label->getNrrd())
		{
			auto data = (unsigned char*)vlnrrd_label->getNrrd()->data;
			if (data)
				memset(data, 0, sizeof(unsigned char) * (unsigned long long)res_x * (unsigned long long)res_y *
					(unsigned long long)res_z * (unsigned long long)vlnrrd_label->getBytesPerSample());
		}
	}

	if (isBrxml())
		SetLevel(curlv);
}

bool VolumeData::SearchLabel(unsigned int label)
{
	if (!m_tex)
		return false;

	auto nrrd_label = m_tex->get_nrrd(m_tex->nlabel());
	if (!nrrd_label || !nrrd_label->getNrrd())
		return false;
	
	unsigned long long for_size = (unsigned long long)m_res_x *
		(unsigned long long)m_res_y * (unsigned long long)m_res_z;

	int bytes = nrrd_label->getBytesPerSample();
	if (bytes == 4)
	{
		unsigned int* data_label = (unsigned int*)(nrrd_label->getNrrd()->data);
		if (!data_label)
			return false;
		for (unsigned long long index = 0; index < for_size; ++index)
			if (data_label[index] == label)
				return true;
	}
	else if (bytes == 2)
	{
		unsigned short* data_label = (unsigned short*)(nrrd_label->getNrrd()->data);
		if (!data_label)
			return false;
		for (unsigned long long index = 0; index < for_size; ++index)
			if (data_label[index] == label)
				return true;
	}
	else if (bytes == 1)
	{
		unsigned char* data_label = (unsigned char*)(nrrd_label->getNrrd()->data);
		if (!data_label)
			return false;
		for (unsigned long long index = 0; index < for_size; ++index)
			if (data_label[index] == label)
				return true;
	}
	return false;
}

std::shared_ptr<VL_Nrrd> VolumeData::GetVolume(bool ret)
{
	if (m_vr && m_tex)
	{
		if (ret) m_vr->return_volume();
		return m_tex->get_nrrd(0);
	}

	return nullptr;
}

std::shared_ptr<VL_Nrrd> VolumeData::GetMask(bool ret)
{
	if (m_vr && m_tex && m_tex->nmask()!=-1)
	{
		int curlv = -1;
		if (isBrxml())
		{
			curlv = GetLevel();
			SetLevel(GetMaskLv());
		}

		if (ret) m_vr->return_mask();
		return m_tex->get_nrrd(m_tex->nmask());

		if (isBrxml())
			SetLevel(curlv);
	}

	return nullptr;
}

std::shared_ptr<VL_Nrrd> VolumeData::GetLabel(bool ret)
{
	if (m_vr && m_tex && m_tex->nlabel() != -1)
	{

		int curlv = -1;
		if (isBrxml())
		{
			curlv = GetLevel();
			SetLevel(GetMaskLv());
		}

		if (ret) m_vr->return_label();
		return m_tex->get_nrrd(m_tex->nlabel());

		if (isBrxml())
			SetLevel(curlv);
	}

	return nullptr;
}

std::shared_ptr<VL_Nrrd> VolumeData::GetStroke(bool ret)
{
	if (m_vr && m_tex && m_tex->nstroke()!=-1)
	{
		int curlv = -1;
		if (isBrxml())
		{
			curlv = GetLevel();
			SetLevel(GetMaskLv());
		}

		if (ret) m_vr->return_stroke();
		return m_tex->get_nrrd(m_tex->nstroke());

		if (isBrxml())
			SetLevel(curlv);
	}

	return nullptr;
}

double VolumeData::GetOriginalValue(int i, int j, int k, bool normalize)
{
	auto vlnrrd = m_tex->get_nrrd(0);
	if (!vlnrrd) return 0.0;
	Nrrd* data = vlnrrd->getNrrd();
	if (!data) return 0.0;

	int bits = data->type;
	int64_t nx = (int64_t)(data->axis[0].size);
	int64_t ny = (int64_t)(data->axis[1].size);
	int64_t nz = (int64_t)(data->axis[2].size);

	if (isBrxml())
	{
		double scx, scy, scz;
		int resx, resy, resz;
		GetResolution(resx, resy, resz);
		scx = scy = scz = 1.0;
		scx = (double)nx / resx; scy = (double)ny / resy; scz = (double)nz / resz;
		i = (int)(i * scx); j = (int)(j * scy); k = (int)(k * scz);
	}

	if (i<0 || i>=nx || j<0 || j>=ny || k<0 || k>=nz)
		return 0.0;
	uint64_t ii = i, jj = j, kk = k;

	if (!m_tex->isBrxml())
	{
		if (!data->data) return 0.0;
		if (bits == nrrdTypeUChar)
		{
			uint64_t index = (nx)*(ny)*(kk) + (nx)*(jj) + (ii);
			uint8 old_value = ((uint8*)(data->data))[index];
			if (m_colormap_mode == 3)
			{
				int combined_seg_id = IsROICombined((int)old_value);
				if (combined_seg_id > 0)
					old_value = (uint8)combined_seg_id;
			}
			return normalize ? double(old_value)/255.0 : double(old_value);
		}
		else if (bits == nrrdTypeUShort)
		{
			uint64_t index = (nx)*(ny)*(kk) + (nx)*(jj) + (ii);
			uint16 old_value = ((uint16*)(data->data))[index];
			if (m_colormap_mode == 3)
			{
				int combined_seg_id = IsROICombined((int)old_value);
				if (combined_seg_id > 0)
					old_value = (uint16)combined_seg_id;
			}
			return normalize ? double(old_value)*m_scalar_scale/65535.0 : double(old_value);
		}
		else if (bits == nrrdTypeFloat)
		{
			uint64_t index = (nx) * (ny) * (kk)+(nx) * (jj)+(ii);
			float old_value = ((float*)(data->data))[index];
			if (m_colormap_mode == 3)
			{
				int combined_seg_id = IsROICombined((int)old_value);
				if (combined_seg_id > 0)
					old_value = (float)combined_seg_id;
			}
			return normalize ? double(old_value) * m_scalar_scale : double(old_value);
		}
	}
	else
	{
		double rval = 0.0;
		rval = m_tex->get_brick_original_value(ii, jj, kk, normalize);
		if (m_colormap_mode == 3)
		{
			int combined_seg_id = IsROICombined((int)rval);
			if (combined_seg_id > 0)
				rval = combined_seg_id;
		}
		if ((bits == nrrdTypeUShort || bits == nrrdTypeFloat) && normalize)
			rval *= m_scalar_scale;
		return rval;
	}

	return 0.0;
}

double VolumeData::GetTransferedValue(int i, int j, int k)
{
	auto vlnrrd = m_tex->get_nrrd(0);
	if (!vlnrrd) return 0.0;
	Nrrd* data = vlnrrd->getNrrd();
	if (!data) return 0.0;

	int bits = data->type;
	int64_t nx = (int64_t)(data->axis[0].size);
	int64_t ny = (int64_t)(data->axis[1].size);
	int64_t nz = (int64_t)(data->axis[2].size);
	if (i<0 || i>=nx || j<0 || j>=ny || k<0 || k>=nz)
		return 0.0;
	int64_t ii = i, jj = j, kk = k;

	double v1, v2, v3, v4, v5, v6;
	if (bits == nrrdTypeUChar)
	{
		double old_value;
		if (!m_tex->isBrxml())
		{
			if (!data->data) return 0.0;
			uint64_t index = nx*ny*kk + nx*jj + ii;
			old_value = (double)((uint8*)(data->data))[index];
		}
		else
			old_value = m_tex->get_brick_original_value(ii, jj, kk, false);

		double gm = 0.0;
		double new_value = old_value/255.0;
		if (m_vr->get_inversion())
			new_value = 1.0-new_value;
		if (i>0 && i<nx-1 &&
			j>0 && j<ny-1 &&
			k>0 && k<nz-1)
		{
			if (!m_tex->isBrxml())
			{
				v1 = ((uint8*)(data->data))[nx*ny*kk + nx*jj + ii-1];
				v2 = ((uint8*)(data->data))[nx*ny*kk + nx*jj + ii+1];
				v3 = ((uint8*)(data->data))[nx*ny*kk + nx*(jj-1) + ii];
				v4 = ((uint8*)(data->data))[nx*ny*kk + nx*(jj+1) + ii];
				v5 = ((uint8*)(data->data))[nx*ny*(kk-1) + nx*jj + ii];
				v6 = ((uint8*)(data->data))[nx*ny*(kk+1) + nx*jj + ii];
			}
			else
			{
				v1 = m_tex->get_brick_original_value(ii-1, jj, kk, false);
				v2 = m_tex->get_brick_original_value(ii+1, jj, kk, false);
				v3 = m_tex->get_brick_original_value(ii, jj-1, kk, false);
				v4 = m_tex->get_brick_original_value(ii, jj+1, kk, false);
				v5 = m_tex->get_brick_original_value(ii, jj, kk-1, false);
				v6 = m_tex->get_brick_original_value(ii, jj, kk+1, false);
			}
			double normal_x, normal_y, normal_z;
			normal_x = (v2 - v1) / 255.0;
			normal_y = (v4 - v3) / 255.0;
			normal_z = (v6 - v5) / 255.0;
			gm = sqrt(normal_x*normal_x + normal_y*normal_y + normal_z*normal_z)*0.53;
		}
		if (new_value<m_lo_thresh-m_sw ||
			new_value>m_hi_thresh+m_sw ||
			gm<m_gm_thresh)
			new_value = 0.0;
		else
		{
			double gamma = 1.0 / m_gamma3d;
			new_value = (new_value<m_lo_thresh?
				(m_sw-m_lo_thresh+new_value)/m_sw:
			(new_value>m_hi_thresh?
				(m_sw-new_value+m_hi_thresh)/m_sw:1.0))
				*new_value;
			new_value = pow(Clamp(new_value/m_offset,
				gamma<1.0?-(gamma-1.0)*0.00001:0.0,
				gamma>1.0?0.9999:1.0), gamma);
			new_value *= m_alpha;
		}
		return new_value;
	}
	else if (bits == nrrdTypeUShort)
	{
		uint64_t index = nx*ny*kk + nx*jj + ii;
		double old_value;
		if (!m_tex->isBrxml())
		{
			if (!data->data) return 0.0;
			uint64_t index = nx*ny*kk + nx*jj + ii;
			old_value = (double)((uint16*)(data->data))[index];
		}
		else
			old_value = m_tex->get_brick_original_value(ii, jj, kk, false);

		double gm = 0.0;
		double new_value = double(old_value)*m_scalar_scale/65535.0;
		if (m_vr->get_inversion())
			new_value = 1.0-new_value;
		if (ii>0 && ii<nx-1 &&
			jj>0 && jj<ny-1 &&
			kk>0 && kk<nz-1)
		{
			if (!m_tex->isBrxml())
			{
				v1 = ((uint16*)(data->data))[nx*ny*kk + nx*jj + ii-1];
				v2 = ((uint16*)(data->data))[nx*ny*kk + nx*jj + ii+1];
				v3 = ((uint16*)(data->data))[nx*ny*kk + nx*(jj-1) + ii];
				v4 = ((uint16*)(data->data))[nx*ny*kk + nx*(jj+1) + ii];
				v5 = ((uint16*)(data->data))[nx*ny*(kk-1) + nx*jj + ii];
				v6 = ((uint16*)(data->data))[nx*ny*(kk+1) + nx*jj + ii];
			}
			else
			{
				v1 = m_tex->get_brick_original_value(ii-1, jj, kk, false);
				v2 = m_tex->get_brick_original_value(ii+1, jj, kk, false);
				v3 = m_tex->get_brick_original_value(ii, jj-1, kk, false);
				v4 = m_tex->get_brick_original_value(ii, jj+1, kk, false);
				v5 = m_tex->get_brick_original_value(ii, jj, kk-1, false);
				v6 = m_tex->get_brick_original_value(ii, jj, kk+1, false);
			}
			double normal_x, normal_y, normal_z;
			normal_x = (v2 - v1)*m_scalar_scale/65535.0;
			normal_y = (v4 - v3)*m_scalar_scale/65535.0;
			normal_z = (v6 - v5)*m_scalar_scale/65535.0;
			gm = sqrt(normal_x*normal_x + normal_y*normal_y + normal_z*normal_z)*0.53;
		}
		if (new_value<m_lo_thresh-m_sw ||
			new_value>m_hi_thresh+m_sw ||
			gm<m_gm_thresh)
			new_value = 0.0;
		else
		{
			double gamma = 1.0 / m_gamma3d;
			new_value = (new_value<m_lo_thresh?
				(m_sw-m_lo_thresh+new_value)/m_sw:
			(new_value>m_hi_thresh?
				(m_sw-new_value+m_hi_thresh)/m_sw:1.0))
				*new_value;
			new_value = pow(Clamp(new_value/m_offset,
				gamma<1.0?-(gamma-1.0)*0.00001:0.0,
				gamma>1.0?0.9999:1.0), gamma);
			new_value *= m_alpha;
		}
		return new_value;
	}
	else if (bits == nrrdTypeFloat)
	{
		uint64_t index = nx * ny * kk + nx * jj + ii;
		double old_value;
		if (!m_tex->isBrxml())
		{
			if (!data->data) return 0.0;
			uint64_t index = nx * ny * kk + nx * jj + ii;
			old_value = (double)((float*)(data->data))[index];
		}
		else
			old_value = m_tex->get_brick_original_value(ii, jj, kk, false);

		double gm = 0.0;
		double new_value = double(old_value) * m_scalar_scale / 65535.0;
		if (m_vr->get_inversion())
			new_value = 1.0 - new_value;
		if (ii > 0 && ii < nx - 1 &&
			jj>0 && jj < ny - 1 &&
			kk>0 && kk < nz - 1)
		{
			if (!m_tex->isBrxml())
			{
				v1 = ((float*)(data->data))[nx * ny * kk + nx * jj + ii - 1];
				v2 = ((float*)(data->data))[nx * ny * kk + nx * jj + ii + 1];
				v3 = ((float*)(data->data))[nx * ny * kk + nx * (jj - 1) + ii];
				v4 = ((float*)(data->data))[nx * ny * kk + nx * (jj + 1) + ii];
				v5 = ((float*)(data->data))[nx * ny * (kk - 1) + nx * jj + ii];
				v6 = ((float*)(data->data))[nx * ny * (kk + 1) + nx * jj + ii];
			}
			else
			{
				v1 = m_tex->get_brick_original_value(ii - 1, jj, kk, false);
				v2 = m_tex->get_brick_original_value(ii + 1, jj, kk, false);
				v3 = m_tex->get_brick_original_value(ii, jj - 1, kk, false);
				v4 = m_tex->get_brick_original_value(ii, jj + 1, kk, false);
				v5 = m_tex->get_brick_original_value(ii, jj, kk - 1, false);
				v6 = m_tex->get_brick_original_value(ii, jj, kk + 1, false);
			}
			double normal_x, normal_y, normal_z;
			normal_x = (v2 - v1) * m_scalar_scale;
			normal_y = (v4 - v3) * m_scalar_scale;
			normal_z = (v6 - v5) * m_scalar_scale;
			gm = sqrt(normal_x * normal_x + normal_y * normal_y + normal_z * normal_z) * 0.53;
		}
		if (new_value<m_lo_thresh - m_sw ||
			new_value>m_hi_thresh + m_sw ||
			gm < m_gm_thresh)
			new_value = 0.0;
		else
		{
			double gamma = 1.0 / m_gamma3d;
			new_value = (new_value < m_lo_thresh ?
				(m_sw - m_lo_thresh + new_value) / m_sw :
				(new_value > m_hi_thresh ?
				(m_sw - new_value + m_hi_thresh) / m_sw : 1.0))
				* new_value;
			new_value = pow(Clamp(new_value / m_offset,
				gamma < 1.0 ? -(gamma - 1.0) * 0.00001 : 0.0,
				gamma>1.0 ? 0.9999 : 1.0), gamma);
			new_value *= m_alpha;
		}
		return new_value;
	}

	return 0.0;
}

int VolumeData::GetLabellValue(int i, int j, int k)
{
	auto vlnrrd = m_tex->get_nrrd(m_tex->nlabel());
	if (!vlnrrd) return 0;
	Nrrd* data = vlnrrd->getNrrd();
	if (!data) return 0;

	int bits = data->type;
	int64_t nx = (int64_t)(data->axis[0].size);
	int64_t ny = (int64_t)(data->axis[1].size);
	int64_t nz = (int64_t)(data->axis[2].size);

	if (i < 0 || i >= nx || j < 0 || j >= ny || k < 0 || k >= nz)
		return 0;
	uint64_t ii = i, jj = j, kk = k;

	if (!data->data) return 0.0;
	if (bits == nrrdTypeUChar)
	{
		uint64_t index = (nx) * (ny) * (kk)+(nx) * (jj)+(ii);
		uint8 old_value = ((uint8*)(data->data))[index];
		return int(old_value);
	}
	else if (bits == nrrdTypeUShort)
	{
		uint64_t index = (nx) * (ny) * (kk)+(nx) * (jj)+(ii);
		uint16 old_value = ((uint16*)(data->data))[index];
		return int(old_value);
	}
	else if (bits == nrrdTypeUInt)
	{
		uint64_t index = (nx) * (ny) * (kk)+(nx) * (jj)+(ii);
		uint16 old_value = ((uint32*)(data->data))[index];
		return int(old_value);
	}
	else if (bits == nrrdTypeFloat)
	{
		uint64_t index = (nx) * (ny) * (kk)+(nx) * (jj)+(ii);
		float old_value = ((float*)(data->data))[index];
		return int(old_value);
	}

	return 0;
}


//save
void VolumeData::Save(wxString &filename, int mode, bool bake, bool compress, bool save_msk, bool save_label, VolumeLoader *vl, bool crop, int lv)
{
	if (m_vr && m_tex)
	{
		Nrrd* data = 0;

		BaseWriter *writer = 0;

		switch (mode)
		{
		case 0://multi-page tiff
			writer = new TIFWriter();
			break;
		case 1://single-page tiff sequence
			writer = new TIFWriter();
			break;
		case 2://nrrd
			writer = new NRRDWriter();
			break;
		}

		double spcx, spcy, spcz;
		GetSpacings(spcx, spcy, spcz);
        
        BBox bbox;
        if (crop)
        {
            //Cropping
            vector<Plane*> *planes = m_vr->get_planes();
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
		auto vlnrrd = m_tex->get_nrrd(0);
		if (vlnrrd && vlnrrd->getNrrd())
		{
			data = vlnrrd->getNrrd();
			if (!isBrxml())
			{
                int w = int(data->axis[0].size);
                int h = int(data->axis[1].size);
                int d = int(data->axis[2].size);
                
                int sx = (int)(bbox.min().x() * w + 0.5);
                int sy = (int)(bbox.min().y() * h + 0.5);
                int sz = (int)(bbox.min().z() * d + 0.5);
                int nx = (int)(bbox.max().x() * w + 0.5) - sx;
                int ny = (int)(bbox.max().y() * h + 0.5) - sy;
                int nz = (int)(bbox.max().z() * d + 0.5) - sz;
                
				if (bake)
				{
					wxProgressDialog *prg_diag = new wxProgressDialog(
						"VVDViewer: Baking volume data...",
						"Baking volume data. Please wait.",
						100, 0, wxPD_APP_MODAL|wxPD_SMOOTH|wxPD_ELAPSED_TIME|wxPD_AUTO_HIDE);

					//process the data
					int bits = vlnrrd->getBytesPerSample() * 8;

					//clipping planes
					vector<Plane*> *planes = m_vr->get_planes();

					Nrrd* baked_data = nrrdNew();
					if (bits == 8)
					{
						unsigned long long mem_size = (unsigned long long)nx*
							(unsigned long long)ny*(unsigned long long)nz;
						uint8 *val8 = new (std::nothrow) uint8[mem_size];
						if (!val8)
						{
							wxMessageBox("Not enough memory. Please save project and restart.");
							return;
						}
						//transfer function
						for (int i=0; i<nx; i++)
						{
							prg_diag->Update(95*(i+1)/nx);
							for (int j=0; j<ny; j++)
								for (int k=0; k<nz; k++)
								{
									int index = nx*ny*k + nx*j + i;
									bool clipped = false;
									Point p(double(i+sx) / double(w),
										double(j+sy) / double(h),
										double(k+sz) / double(d));
									for (int pi = 0; pi < 6; ++pi)
									{
										if ((*planes)[pi] &&
											(*planes)[pi]->eval_point(p) < 0.0)
										{
											val8[index] = 0;
											clipped = true;
										}
									}
									if (!clipped)
									{
										double new_value = GetTransferedValue(i+sx, j+sy, k+sz);
										val8[index] = uint8(new_value*255.0);
									}
								}
						}
						nrrdWrap(baked_data, val8, nrrdTypeUChar, 3, (size_t)nx, (size_t)ny, (size_t)nz);
					}
					else if (bits == 16)
					{
						unsigned long long mem_size = (unsigned long long)nx*
							(unsigned long long)ny*(unsigned long long)nz;
						uint16 *val16 = new (std::nothrow) uint16[mem_size];
						if (!val16)
						{
							wxMessageBox("Not enough memory. Please save project and restart.");
							return;
						}
						//transfer function
						for (int i=0; i<nx; i++)
						{
							prg_diag->Update(95*(i+1)/nx);
							for (int j=0; j<ny; j++)
								for (int k=0; k<nz; k++)
								{
									int index = nx*ny*k + nx*j + i;
									bool clipped = false;
									Point p(double(i+sx) / double(w),
										double(j+sy) / double(h),
										double(k+sz) / double(d));
									for (int pi = 0; pi < 6; ++pi)
									{
										if ((*planes)[pi] &&
											(*planes)[pi]->eval_point(p) < 0.0)
										{
											val16[index] = 0;
											clipped = true;
										}
									}
									if (!clipped)
									{
										double new_value = GetTransferedValue(i+sx, j+sy, k+sz);
										val16[index] = uint16(new_value*65535.0);
									}
								}
						}
						nrrdWrap(baked_data, val16, nrrdTypeUShort, 3, (size_t)nx, (size_t)ny, (size_t)nz);
					}
					else if (bits == 32)
					{
						unsigned long long mem_size = (unsigned long long)nx *
							(unsigned long long)ny * (unsigned long long)nz;
						float* val32 = new (std::nothrow) float[mem_size];
						if (!val32)
						{
							wxMessageBox("Not enough memory. Please save project and restart.");
							return;
						}
						//transfer function
						for (int i = 0; i < nx; i++)
						{
							prg_diag->Update(95 * (i + 1) / nx);
							for (int j = 0; j < ny; j++)
								for (int k = 0; k < nz; k++)
								{
									int index = nx * ny * k + nx * j + i;
									bool clipped = false;
									Point p(double(i + sx) / double(w),
										double(j + sy) / double(h),
										double(k + sz) / double(d));
									for (int pi = 0; pi < 6; ++pi)
									{
										if ((*planes)[pi] &&
											(*planes)[pi]->eval_point(p) < 0.0)
										{
											val32[index] = 0;
											clipped = true;
										}
									}
									if (!clipped)
									{
										double new_value = GetTransferedValue(i + sx, j + sy, k + sz);
										val32[index] = float(new_value);
									}
								}
						}
						nrrdWrap(baked_data, val32, nrrdTypeFloat, 3, (size_t)nx, (size_t)ny, (size_t)nz);
					}
					nrrdAxisInfoSet(baked_data, nrrdAxisInfoSpacing, spcx, spcy, spcz);
					nrrdAxisInfoSet(baked_data, nrrdAxisInfoMax, spcx*nx, spcy*ny, spcz*nz);
					nrrdAxisInfoSet(baked_data, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
					nrrdAxisInfoSet(baked_data, nrrdAxisInfoSize, (size_t)nx, (size_t)ny, (size_t)nz);

					auto baked_vlnrrd = make_shared<VL_Nrrd>(baked_data);
					writer->SetData(baked_vlnrrd);
					writer->SetSpacings(spcx, spcy, spcz);
					writer->SetCompression(compress);
					writer->Save(filename.ToStdWstring(), mode);

					prg_diag->Update(100);
					delete prg_diag;
				}
				else
				{
                    if (crop || (m_tex->nmask() != -1 && save_msk && GetMaskHideMode() != VOL_MASK_HIDE_NONE))
                    {
                        wxProgressDialog *prg_diag = new wxProgressDialog(
                                                                          "VVDViewer: Saving volume data...",
                                                                          "Saving volume data. Please wait.",
                                                                          100, 0, wxPD_APP_MODAL|wxPD_SMOOTH|wxPD_ELAPSED_TIME|wxPD_AUTO_HIDE);
                        
                        uint8 *mskdata = NULL;
                        if (m_tex->nmask() != -1 && save_msk && GetMaskHideMode() != VOL_MASK_HIDE_NONE)
                        {
                            m_vr->return_mask();
                            auto vlnrrd_msk = m_tex->get_nrrd(m_tex->nmask());
                            mskdata = (uint8*)vlnrrd_msk->getNrrd()->data;
                        }
                        
                        Nrrd* baked_data = nrrdNew();
                        
                        //process the data
                        int bits = vlnrrd->getBytesPerSample() * 8;
                        
                        //clipping planes
                        vector<Plane*> *planes = m_vr->get_planes();
                        
                        Nrrd* segmented_data = nrrdNew();
                        if (bits == 8)
                        {
                            unsigned long long mem_size = (unsigned long long)nx*
                            (unsigned long long)ny*(unsigned long long)nz;
                            uint8 *val8 = new (std::nothrow) uint8[mem_size];
                            if (!val8)
                            {
                                wxMessageBox("Not enough memory. Please save project and restart.");
                                return;
                            }
                            //transfer function
                            for (int i=0; i<nx; i++)
                            {
                                prg_diag->Update(95*(i+1)/nx);
                                for (int j=0; j<ny; j++)
                                    for (int k=0; k<nz; k++)
                                    {
                                        int index = nx*ny*k + nx*j + i;
                                        bool clipped = false;
                                        Point p(double(i+sx) / double(w),
                                                double(j+sx) / double(h),
                                                double(k+sx) / double(d));
                                        for (int pi = 0; pi < 6; ++pi)
                                        {
                                            if ((*planes)[pi] &&
                                                (*planes)[pi]->eval_point(p) < 0.0)
                                            {
                                                val8[index] = 0;
                                                clipped = true;
                                            }
                                        }
                                        
                                        if (!clipped && (mskdata == NULL || mskdata[index] >= 128))
                                        {
                                            double new_value = GetOriginalValue(i+sx, j+sy, k+sz);
                                            val8[index] = uint8(new_value*255.0);
                                        }
                                    }
                            }
                            nrrdWrap(baked_data, val8, nrrdTypeUChar, 3, (size_t)nx, (size_t)ny, (size_t)nz);
                        }
                        else if (bits == 16)
                        {
                            unsigned long long mem_size = (unsigned long long)nx*
                            (unsigned long long)ny*(unsigned long long)nz;
                            uint16 *val16 = new (std::nothrow) uint16[mem_size];
                            if (!val16)
                            {
                                wxMessageBox("Not enough memory. Please save project and restart.");
                                return;
                            }
                            //transfer function
                            for (int i=0; i<nx; i++)
                            {
                                prg_diag->Update(95*(i+1)/nx);
                                for (int j=0; j<ny; j++)
                                    for (int k=0; k<nz; k++)
                                    {
                                        int index = nx*ny*k + nx*j + i;
                                        bool clipped = false;
                                        Point p(double(i+sx) / double(w),
                                                double(j+sy) / double(h),
                                                double(k+sz) / double(d));
                                        for (int pi = 0; pi < 6; ++pi)
                                        {
                                            if ((*planes)[pi] &&
                                                (*planes)[pi]->eval_point(p) < 0.0)
                                            {
                                                val16[index] = 0;
                                                clipped = true;
                                            }
                                        }
                                        if (!clipped && (mskdata == NULL || mskdata[index] >= 128))
                                        {
                                            double new_value = GetOriginalValue(i+sx, j+sy, k+sz);
                                            val16[index] = uint16(new_value*65535.0);
                                        }
                                    }
                            }
                            nrrdWrap(baked_data, val16, nrrdTypeUShort, 3, (size_t)nx, (size_t)ny, (size_t)nz);
                        }
						else if (bits == 32)
						{
							unsigned long long mem_size = (unsigned long long)nx *
								(unsigned long long)ny * (unsigned long long)nz;
							float* val32 = new (std::nothrow) float[mem_size];
							if (!val32)
							{
								wxMessageBox("Not enough memory. Please save project and restart.");
								return;
							}
							//transfer function
							for (int i = 0; i < nx; i++)
							{
								prg_diag->Update(95 * (i + 1) / nx);
								for (int j = 0; j < ny; j++)
									for (int k = 0; k < nz; k++)
									{
										int index = nx * ny * k + nx * j + i;
										bool clipped = false;
										Point p(double(i + sx) / double(w),
											double(j + sy) / double(h),
											double(k + sz) / double(d));
										for (int pi = 0; pi < 6; ++pi)
										{
											if ((*planes)[pi] &&
												(*planes)[pi]->eval_point(p) < 0.0)
											{
												val32[index] = 0;
												clipped = true;
											}
										}
										if (!clipped && (mskdata == NULL || mskdata[index] >= 128))
										{
											double new_value = GetOriginalValue(i + sx, j + sy, k + sz);
											val32[index] = float(new_value);
										}
									}
							}
							nrrdWrap(baked_data, val32, nrrdTypeFloat, 3, (size_t)nx, (size_t)ny, (size_t)nz);
						}
                        nrrdAxisInfoSet(baked_data, nrrdAxisInfoSpacing, spcx, spcy, spcz);
                        nrrdAxisInfoSet(baked_data, nrrdAxisInfoMax, spcx*nx, spcy*ny, spcz*nz);
                        nrrdAxisInfoSet(baked_data, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
                        nrrdAxisInfoSet(baked_data, nrrdAxisInfoSize, (size_t)nx, (size_t)ny, (size_t)nz);
                        
                        auto baked_vlnrrd = make_shared<VL_Nrrd>(baked_data);
                        writer->SetData(baked_vlnrrd);
                        writer->SetSpacings(spcx, spcy, spcz);
                        writer->SetCompression(compress);
                        writer->Save(filename.ToStdWstring(), mode);
                        
                        prg_diag->Update(100);
                        delete prg_diag;
                    }
                    else {
                        writer->SetData(vlnrrd);
                        writer->SetSpacings(spcx, spcy, spcz);
                        writer->SetCompression(compress);
                        writer->Save(filename.ToStdWstring(), mode);
                    }
				}
			}
			else if (vl)
			{
                wxProgressDialog *prg_diag = new wxProgressDialog(
                                                                  "VVDViewer: Saving volume data...",
                                                                  "Saving volume data. Please wait.",
                                                                  100, 0, wxPD_APP_MODAL|wxPD_SMOOTH|wxPD_ELAPSED_TIME|wxPD_AUTO_HIDE);
                
				int curlv = -1;
				curlv = GetLevel();
				int tarlv = lv;
				SetLevel(tarlv);
				GetSpacings(spcx, spcy, spcz);
				size_t w, h, d;
				m_tex->get_dimensions(w, h, d);
                
                int sx = (int)(bbox.min().x() * w + 0.5);
                int sy = (int)(bbox.min().y() * h + 0.5);
                int sz = (int)(bbox.min().z() * d + 0.5);
                int nx = (int)(bbox.max().x() * w + 0.5) - sx;
                int ny = (int)(bbox.max().y() * h + 0.5) - sy;
                int nz = (int)(bbox.max().z() * d + 0.5) - sz;

				TIFWriter *tifwriter = (TIFWriter *)writer;
				int ndigit = int(log10(double(d))) + 1;
				
				auto bricks = m_tex->get_bricks();
				std::sort(
					bricks->begin(),
					bricks->end(),
					[](TextureBrick *x, TextureBrick *y){ return x->bbox().min().z() < y->bbox().min().z(); }
				);

				vector<int> bricknum_layer;
				int count = 0;
				int minz = (*bricks)[0]->oz();
				int bnz = (*bricks)[0]->nz();
				bool check = false;
				for (auto &b : *bricks)
				{
					if (minz == b->oz())
					{
						if (bnz != b->nz())
							check = true;
						count++;
					}
					else
					{
						bricknum_layer.push_back(count);
						minz = b->oz();
						bnz = b->nz();
						count = 1;
					}
				}
				bricknum_layer.push_back(count);

				if (!check)
				{
					TIFF* tif = nullptr;
					if (mode == 0)
						tif = TIFFOpenW(filename.ToStdWstring().c_str(), "wb");

					int bid = 0;
                    int layernum = bricknum_layer.size();
                    int cur_layer = 0;
					int slice_id = 0;
					for (auto layercount : bricknum_layer)
					{
                        prg_diag->Update(95*(cur_layer+1)/layernum);
                        cur_layer++;
                        
						vector<VolumeLoaderData> queues;
						vector<TextureBrick*> lbs;
						long long req_mem = 0;
						for (int i = 0; i < layercount; i++, bid++)
						{
							TextureBrick* b = (*bricks)[bid];
                            if (b->ox() + b->nx() < sx || b->oy() + b->ny() < sy || b->oz() + b->nz() < sz || b->ox() >= sx + nx || b->oy() >= sy + ny || b->oz() >= sz + nz)
                                continue;
							b->lock_brickdata(true);
							lbs.push_back(b);

							VolumeLoaderData d;
							d.brick = b;
							d.finfo = m_tex->GetFileName(b->getID(), tarlv);
							d.vd = this;
							d.mode = 0;
							queues.push_back(d);

							if (!b->isLoaded())
								req_mem += (size_t)(b->nx())*(size_t)(b->ny())*(size_t)(b->nz())*(size_t)(b->nb(0));
						}
                        
                        if (lbs.empty())
                            continue;
						
						size_t slicenum = lbs[0]->nz();
						size_t buffer_size = w*h*slicenum*(size_t)(lbs[0]->nb(0));
						//vl->TryToFreeMemory(req_mem + buffer_size);
                        vl->RemoveAllLoadedData();
						long long avmem = vl->GetAvailableMemory();
                        cout << "queues: " << queues.size() << endl;
                        cout << "req_mem: " << req_mem << endl;
                        cout << "avmem (pre): " << avmem << endl;
						bool runVL = true;
						if (avmem < req_mem + buffer_size)
						{
							if (avmem > req_mem)
								slicenum = (avmem - req_mem) / (w*h*lbs[0]->nb(0));
							else
							{
								slicenum = avmem / (w*h*lbs[0]->nb(0));
								runVL = false;
							}
							if (slicenum == 0) slicenum = 1;
							buffer_size = w*h*slicenum*(size_t)(lbs[0]->nb(0));
						}
                        
						if (runVL)
						{
							vl->Set(queues);
							vl->Run();
							vl->Join();
						}
                        
                        cout << "runVL: " << runVL << endl;
                        cout << "slicenum: " << slicenum << endl;
                        cout << "avmem (post): " << vl->GetAvailableMemory() << endl;

						int lnz = lbs[0]->nz();
						int loz = lbs[0]->oz();
						int tmp_lnz = lnz;
						int tmp_loz = loz;
						if (loz < sz)
						{
							lnz = lnz + loz - sz;
							loz = sz;
						}
                        if (loz + lnz >= sz + nz)
							lnz = sz + nz - loz;

						for (int s = 0; s < lnz; s += slicenum)
						{
							size_t bufd = (s+slicenum <= lnz) ? slicenum : lnz-s;

							Nrrd* datablock = m_tex->getSubData(tarlv, GetMaskHideMode(), &lbs, sx, sy, s+loz, nx, ny, bufd);
							auto block_vlnrrd = make_shared<VL_Nrrd>(datablock);
							if (m_colormap_mode == 3)
							{
								size_t pixnum = block_vlnrrd->getDatasize() / block_vlnrrd->getBytesPerSample();
								size_t nthreads = std::thread::hardware_concurrency() - 1;
								if (nthreads < 1) nthreads = 1;
								std::vector<std::thread> threads(nthreads);
								int grain_size = pixnum / nthreads;
								auto worker = [this, &block_vlnrrd, &pixnum](size_t start, size_t end) {
									if (start >= pixnum) start = pixnum;
									if (end > pixnum) end = pixnum;
									if (start < end) {
										for (uint32_t idx = start; idx < end; idx++)
										{
											int val = (int)block_vlnrrd->getFloatUnsafe(idx);
											int combined_seg = this->IsROICombined(val);
											if (combined_seg > 0)
												block_vlnrrd->setFloatUnsafe(idx, (float)combined_seg);
										}
									}
								};
								size_t offset = 0;
								for (uint32_t i = 0; i < nthreads - 1; i++)
								{
									threads[i] = std::thread(worker, offset, offset + grain_size);
									offset += grain_size;
								}
								threads.back() = std::thread(worker, offset, pixnum);
								for (auto&& i : threads) {
									i.join();
								}
							}

							if (mode == 1)
							{
								tifwriter->SetData(block_vlnrrd);
								tifwriter->SetBaseSeqID(s + loz + 1); //start from 1
								tifwriter->SetDigitNum(ndigit);
								tifwriter->SetSpacings(spcx, spcy, spcz);
								tifwriter->SetCompression(compress);
								tifwriter->Save(filename.ToStdWstring(), mode);
							}
							else if (mode == 0 && tif)
							{
								for (int ss = 0; ss < bufd; ss++)
								{
									TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, nx);
									TIFFSetField(tif, TIFFTAG_IMAGELENGTH, ny);
									TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
									TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, block_vlnrrd->getBytesPerSample() * 8);
									TIFFSetField(tif, TIFFTAG_XRESOLUTION, spcx);
									TIFFSetField(tif, TIFFTAG_YRESOLUTION, spcy);
									TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
									TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
									TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
									if (compress)
										TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
									TIFFSetField(tif, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE);
									TIFFSetField(tif, TIFFTAG_PAGENUMBER, slice_id + ss);
									TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));
									ostringstream strs;
									strs << "ImageJ=1.52a\n";
									strs << "spacing=" << spcz << "\n";
									strs << "images=" << nz << "\n";
									strs << "slices=" << nz << "\n";
									strs << "loop=false";
									string desc = strs.str();
									TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, desc.c_str());

									switch (datablock->type)
									{
									case nrrdTypeChar:
									case nrrdTypeUChar:
									{
										uint8_t* slice = (uint8_t*)datablock->data + nx * ny * ss;
										for (uint32_t row = 0; row < ny; ++row) {
											if (TIFFWriteScanline(tif, const_cast<uint8_t*>(&slice[row * nx]), row, 0) < 0) {
												std::cerr << "Error writing scanline " << row << std::endl;
												return;
											}
										}
									}
									break;
									case nrrdTypeShort:
									case nrrdTypeUShort:
									{
										uint16_t* slice = (uint16_t*)datablock->data + nx * ny * ss;
										for (uint32_t row = 0; row < ny; ++row) {
											if (TIFFWriteScanline(tif, const_cast<uint16_t*>(&slice[row * nx]), row, 0) < 0) {
												std::cerr << "Error writing scanline " << row << std::endl;
												return;
											}
										}
									}
									break;
									case nrrdTypeInt:
									case nrrdTypeUInt:
									{
										uint32_t* slice = (uint32_t*)datablock->data + nx * ny * ss;
										for (uint32_t row = 0; row < ny; ++row) {
											if (TIFFWriteScanline(tif, const_cast<uint32_t*>(&slice[row * nx]), row, 0) < 0) {
												std::cerr << "Error writing scanline " << row << std::endl;
												return;
											}
										}
									}
									break;
									case nrrdTypeFloat:
									{
										float* slice = (float*)datablock->data + nx * ny * ss;
										for (uint32_t row = 0; row < ny; ++row) {
											if (TIFFWriteScanline(tif, const_cast<float*>(&slice[row * nx]), row, 0) < 0) {
												std::cerr << "Error writing scanline " << row << std::endl;
												return;
											}
										}
									}
									break;
									}

									TIFFWriteDirectory(tif);
								}
							}
							slice_id += bufd;
						}

						for (auto &b : lbs)
							b->lock_brickdata(false);
					}
					if (mode == 0 && tif)
						TIFFClose(tif);
				}

				m_tex->set_sort_bricks();
				SetLevel(curlv);
                
                prg_diag->Update(100);
                delete prg_diag;
			}
		}
		delete writer;


		int curlv = -1;
		if ( isBrxml() )
		{
			curlv = GetLevel();
			SetLevel(GetMaskLv());
		}

		GetSpacings(spcx, spcy, spcz);

		//save mask
		if (m_tex->nmask() != -1 && save_msk && GetMaskHideMode() == VOL_MASK_HIDE_NONE)
		{
			m_vr->return_mask();
			vlnrrd = m_tex->get_nrrd(m_tex->nmask());
			if (vlnrrd)
			{
				MSKWriter msk_writer;
				msk_writer.SetData(vlnrrd);
				msk_writer.SetSpacings(spcx, spcy, spcz);
				msk_writer.Save(filename.ToStdWstring(), 0);
			}
		}

		//save label
		if (m_tex->nlabel() != -1 && save_label && GetMaskHideMode() == VOL_MASK_HIDE_NONE)
		{
			vlnrrd = m_tex->get_nrrd(m_tex->nlabel());
			if (vlnrrd)
			{
				MSKWriter msk_writer;
				msk_writer.SetData(vlnrrd);
				msk_writer.SetSpacings(spcx, spcy, spcz);
				msk_writer.Save(filename.ToStdWstring(), 1);
			}
		}

		if ( isBrxml() )
			SetLevel(curlv);

		if ( !isBrxml() ) m_tex_path = filename;
	}
}


void VolumeData::ExportMask(wxString &filename)
{
	if (m_vr && m_tex)
	{
		int curlv = -1;
		if ( isBrxml() )
		{
			curlv = GetLevel();
			SetLevel(GetMaskLv());
		}

		double spcx, spcy, spcz;
		GetSpacings(spcx, spcy, spcz);

		//save mask
		if (m_tex->nmask() != -1)
		{
			m_vr->return_mask();
			auto data = m_tex->get_nrrd(m_tex->nmask());
			if (data)
			{
				MSKWriter msk_writer;
				msk_writer.SetData(data);
				msk_writer.SetSpacings(spcx, spcy, spcz);
				msk_writer.Save(filename.ToStdWstring(), -1);
			}
		}

		if ( isBrxml() )
			SetLevel(curlv);
	}
}

void VolumeData::ImportMask(wxString &filename)
{
	MSKReader msk_reader;
	msk_reader.SetAddExt(false);
    std::string str = filename.ToStdString();
	msk_reader.SetFile(str);
	auto mask = msk_reader.Convert(0, 0, true);
	if (mask)
	{
		if (!LoadMask(mask))
		{
			wxMessageBox(wxT("Unable to load the mask data.\nMask data must have same dimensions as the volume data."), wxT("Mask Import Error"));
		}
	}
}

void VolumeData::ExportEachSegment(wxString dir, const std::shared_ptr<VL_Nrrd>& label_nrrd, int mode, bool compress)
{
	if (m_vr && m_tex)
	{
		wxProgressDialog* prog_diag = new wxProgressDialog(
			"VVDViewer: Export Segments...",
			"Please wait.",
			100, 0,
			wxPD_APP_MODAL|wxPD_SMOOTH|wxPD_ELAPSED_TIME|wxPD_AUTO_HIDE);
		int progress = 0;

		BaseWriter *writer = 0;
		switch (mode)
		{
		case 0://multi-page tiff
			writer = new TIFWriter();
			break;
		case 1://single-page tiff sequence
			writer = new TIFWriter();
			break;
		case 2://nrrd
			writer = new NRRDWriter();
			break;
		}

		double spcx, spcy, spcz;
		GetSpacings(spcx, spcy, spcz);

		//save data
		auto main = m_tex->get_nrrd(0);

		shared_ptr<VL_Nrrd> label;
		if (label_nrrd) label = label_nrrd;
		else
		{
			m_vr->return_label();
			label = m_tex->get_nrrd(m_tex->nlabel());
		}

		if (main && main->getNrrd() && main->getNrrd()->data && label && label->getNrrd() && label->getNrrd()->data)
		{
			size_t voxelnum = (size_t)m_res_x * (size_t)m_res_y * (size_t)m_res_z;
			size_t data_size = voxelnum;
			
			void *origdata = main->getNrrd()->data;
			
			data_size *= main->getBytesPerSample();
			void *tmpdata = new unsigned char[data_size];
			memset(tmpdata, 0, data_size);
			
			//scan label
			void* label_data = label->getNrrd()->data;
			int bytes = label->getBytesPerSample();
			unordered_map <unsigned int, vector<size_t>> segs;
			int segnum = 0;
			for (size_t pos = 0; pos < voxelnum; pos++)
			{
				unsigned int id = 0;
				switch (bytes)
				{
				case 1:
					id = ((unsigned char*)label_data)[pos];
					break;
				case 2:
					id = ((unsigned short*)label_data)[pos];
					break;
				case 4:
					id = ((unsigned int*)label_data)[pos];
					break;
				}
				if (id > 0)
				{
					unordered_map <unsigned int, vector<size_t>>::iterator ite = segs.find(id);
					if (ite != segs.end())
						ite->second.push_back(pos);
					else 
					{
						vector<size_t> v;
						v.push_back(pos);
						segs.insert(pair<unsigned int, vector<size_t>>(id, v));
						segnum++;
					}
				}
			}

			//save segments
			main->getNrrd()->data = tmpdata;
			if (!dir.EndsWith(wxFileName::GetPathSeparator())) dir += wxFileName::GetPathSeparator();
			unordered_map <unsigned int, vector<size_t>>:: const_iterator cite;
			int segcount = 0;
			if (main->getNrrd()->type == nrrdTypeChar || main->getNrrd()->type == nrrdTypeUChar) {
				unsigned char *src = (unsigned char *)origdata;
				unsigned char *tar = (unsigned char *)tmpdata;
				for ( cite = segs.begin(); cite != segs.end(); ++cite )
				{
					wxString fname = dir + wxString::Format("%05d", cite->first) + "_" + m_name;
					size_t segsize = cite->second.size();
					for (size_t i = 0; i < segsize; i++)
						tar[cite->second[i]] = src[cite->second[i]];
					
					writer->SetData(main);
					writer->SetSpacings(spcx, spcy, spcz);
					writer->SetCompression(compress);
					writer->Save(fname.ToStdWstring(), mode);

					memset(tmpdata, 0, data_size);
					
					segcount++;
					prog_diag->Update(100 * segcount / segnum);
				}
			}
			else if (main->getNrrd()->type == nrrdTypeShort || main->getNrrd()->type == nrrdTypeUShort) {
				unsigned short *src = (unsigned short *)origdata;
				unsigned short *tar = (unsigned short *)tmpdata;
				for ( cite = segs.begin(); cite != segs.end(); ++cite )
				{
					wxString fname = dir + wxString::Format("%05d", cite->first) + "_" + m_name;
					size_t segsize = cite->second.size();
					for (size_t i = 0; i < segsize; i++)
						tar[cite->second[i]] = src[cite->second[i]];
					
					writer->SetData(main);
					writer->SetSpacings(spcx, spcy, spcz);
					writer->SetCompression(compress);
					writer->Save(fname.ToStdWstring(), mode);

					memset(tmpdata, 0, data_size);
					
					segcount++;
					prog_diag->Update(100 * segcount / segnum);
				}
			}
			else if (main->getNrrd()->type == nrrdTypeFloat) {
				float* src = (float*)origdata;
				float* tar = (float*)tmpdata;
				for (cite = segs.begin(); cite != segs.end(); ++cite)
				{
					wxString fname = dir + wxString::Format("%05d", cite->first) + "_" + m_name;
					size_t segsize = cite->second.size();
					for (size_t i = 0; i < segsize; i++)
						tar[cite->second[i]] = src[cite->second[i]];

					writer->SetData(main);
					writer->SetSpacings(spcx, spcy, spcz);
					writer->SetCompression(compress);
					writer->Save(fname.ToStdWstring(), mode);

					memset(tmpdata, 0, data_size);

					segcount++;
					prog_diag->Update(100 * segcount / segnum);
				}
			}

			main->getNrrd()->data = origdata;
			delete [] tmpdata;
		}

		delete writer;

		prog_diag->Update(100);
		delete prog_diag;
	}
}

//bounding box
BBox VolumeData::GetBounds()
{
	return m_bounds;
}

//path
void VolumeData::SetPath(wxString path)
{
	m_tex_path = path;
}

wxString VolumeData::GetPath()
{
	return m_tex_path;
}

//multi-channel
void VolumeData::SetCurChannel(int chan)
{
	m_chan = chan;
}

int VolumeData::GetCurChannel()
{
	return m_chan;
}

//time sequence
void VolumeData::SetCurTime(int time)
{
	if (time < 0) time = 0;
	if (m_reader)
	{
		if (time >= m_reader->GetTimeNum())
			time = m_reader->GetTimeNum() - 1;
	}
	m_time = time;
}

int VolumeData::GetCurTime()
{
	return m_time;
}

//MIP & normal modes
void VolumeData::SetMode(int mode)
{
	if (!m_vr)
		return;

	m_saved_mode = m_mode;
	m_mode = mode;

	switch (mode)
	{
	case 0://normal
		m_vr->set_mode(TextureRenderer::MODE_OVER);

		m_vr->set_color(m_color);
		m_vr->set_alpha(m_alpha);
		m_vr->set_lo_thresh(m_lo_thresh);
		m_vr->set_hi_thresh(m_hi_thresh);
		m_vr->set_gm_thresh(m_gm_thresh);
		m_vr->set_gamma3d(m_gamma3d);
		m_vr->set_offset(m_offset);
		break;
	case 1://MIP
		m_vr->set_mode(TextureRenderer::MODE_MIP);
		{
			double h, s, v;
			GetHSV(h, s, v);
			HSVColor hsv(h, s, 1.0);
			Color rgb = Color(hsv);
			m_vr->set_color(rgb);
		}
		m_vr->set_alpha(1.0);
		m_vr->set_lo_thresh(0.0);
		m_vr->set_hi_thresh(1.0);
		m_vr->set_gm_thresh(0.0);
		m_vr->set_gamma3d(m_gamma3d);
		m_vr->set_offset(m_offset);
		break;
	case 2://white shading
		m_vr->set_mode(TextureRenderer::MODE_OVER);
		m_vr->set_colormap_mode(0);

		m_vr->set_color(Color(1.0, 1.0, 1.0));
		m_vr->set_alpha(1.0);
		m_vr->set_lo_thresh(m_lo_thresh);
		m_vr->set_hi_thresh(m_hi_thresh);
		m_vr->set_gm_thresh(m_gm_thresh);
		m_vr->set_gamma3d(m_gamma3d);
		m_vr->set_offset(m_offset);
		break;
	case 3://white mip
		m_vr->set_mode(TextureRenderer::MODE_MIP);
		m_vr->set_colormap_mode(0);

		m_vr->set_color(Color(1.0, 1.0, 1.0));
		m_vr->set_alpha(1.0);
		m_vr->set_lo_thresh(0.0);
		m_vr->set_hi_thresh(1.0);
		m_vr->set_gm_thresh(0.0);
		m_vr->set_gamma3d(1.0);
		m_vr->set_offset(1.0);
		break;
	}
}

int VolumeData::GetMode()
{
	return m_mode;
}

void VolumeData::RestoreMode()
{
	SetMode(m_saved_mode);
}

//inversion
void VolumeData::SetInvert(bool mode)
{
	if (m_vr)
		m_vr->set_inversion(mode);
}

bool VolumeData::GetInvert()
{
	if (m_vr)
		return m_vr->get_inversion();
	else
		return false;
}

//mask mode
void VolumeData::SetMaskMode(int mode)
{
	m_mask_mode = mode;
	if (m_vr)
		m_vr->set_ml_mode(mode);
}

int VolumeData::GetMaskMode()
{
	return m_mask_mode;
}

bool VolumeData::isActiveMask()
{
	if (m_tex->nmask()==-1 && m_shared_msk_name.IsEmpty()) return false;

	bool mask;
	switch (m_mask_mode)
	{
	case 0:
		mask = false;
		break;
	case 1:
		mask = true;
		break;
	case 2:
		mask = true;
		break;
	case 3:
		mask = false;
		break;
	case 4:
		mask = true;
		break;
	}

	return mask;
}

bool VolumeData::isActiveLabel()
{
	if (m_tex->nlabel()==-1 && m_shared_lbl_name.IsEmpty()) return false;

	bool label;
	switch (m_mask_mode)
	{
	case 0:
		label = false;
		break;
	case 1:
		label = false;
		break;
	case 2:
		label = false;
		break;
	case 3:
		label = true;
		break;
	case 4:
		label = true;
		break;
	}

	return label;
}

//noise reduction
void VolumeData::SetNR(bool val)
{
	m_noise_rd = val;
	if (m_vr)
		m_vr->SetNoiseRed(val);
}

bool VolumeData::GetNR()
{
	return m_noise_rd;
}

//volumerenderer
VolumeRenderer *VolumeData::GetVR()
{
	return m_vr;
}

//texture
Texture* VolumeData::GetTexture()
{
	return m_tex;
}

void VolumeData::SetTexture()
{
	if (m_vr)
		m_vr->reset_texture();
	m_tex = 0;
}

void VolumeData::SetMatrices(glm::mat4 &mv_mat,
	glm::mat4 &proj_mat, glm::mat4 &tex_mat)
{
	glm::mat4 scale_mv = glm::scale(mv_mat, glm::vec3(m_sclx, m_scly, m_sclz));
	if (m_vr)
		m_vr->set_matrices(scale_mv, proj_mat, tex_mat);
}

//draw volume
void VolumeData::Draw(
	std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf,
	bool ortho, bool interactive, double zoom, double sampling_frq_fac, VkClearColorValue clearColor, Texture* ext_msk, Texture* ext_lbl)
{
	if (m_vr)
	{
		int curlv = -1;
		if ( isBrxml() && (m_vr->is_mask_active() || m_vr->is_label_active()) )
		{
			curlv = GetLevel();
			SetLevel(GetMaskLv());
		}
		
		m_vr->draw(framebuf, clear_framebuf, m_test_wiref, interactive, ortho, zoom, m_stream_mode, sampling_frq_fac, clearColor, ext_msk, ext_lbl);
		
		if ( isBrxml() && (m_vr->is_mask_active() || m_vr->is_label_active()) )
			SetLevel(curlv);
	}
	if (m_draw_bounds)
		DrawBounds();
}

void VolumeData::DrawBounds()
{
}

//draw mask (create the mask)
//type: 0-initial; 1-diffusion-based growing
//paint_mode: 1-select; 2-append; 3-erase; 4-diffuse; 5-flood; 6-clear, 7-all
//			  11-posterize
//hr_mode (hidden removal): 0-none; 1-ortho; 2-persp
void VolumeData::DrawMask(int type, int paint_mode, int hr_mode,
						  double ini_thresh, double gm_falloff, double scl_falloff, double scl_translate,
						  double w2d, double bins, bool ortho, Texture* ext_msk, bool clear_msk_cache, bool use_absolute_value, bool save_stroke)
{
	if (m_vr)
	{
		int curlv = -1;
		if (isBrxml())
		{
			curlv = GetLevel();
			SetLevel(GetMaskLv());
		}
		//OutputDebugStringA("DrawMask Enter\n");
		m_vr->set_2d_mask(m_2d_mask);
		m_vr->set_2d_weight(m_2d_weight1, m_2d_weight2);
		m_vr->draw_mask(type, paint_mode, hr_mode, ini_thresh, gm_falloff, scl_falloff, scl_translate, w2d, bins, ortho, false, ext_msk, use_absolute_value, save_stroke);
		if (isBrxml())
		{
			int lvnum = GetLevelNum();
			int masklv = GetMaskLv();
            if (clear_msk_cache)
            {
                for (int i = 0; i < lvnum; i++)
                {
                    SetLevel(i);
                    m_vr->clear_tex_current_mask();
                }
            }
			SetLevel(curlv);
		}
		//OutputDebugStringA("DrawMask Leave\n");
	}
}

void VolumeData::DrawMaskThreshold(float th, bool ortho, bool use_absolute_value)
{
	if (m_vr)
	{
		int curlv = -1;
		if (isBrxml())
		{
			curlv = GetLevel();
			SetLevel(GetMaskLv());
		}

		m_vr->draw_mask_th(th, ortho, use_absolute_value);

		if (isBrxml())
			SetLevel(curlv);
	}
}

void VolumeData::DrawMaskDSLT(int type, int paint_mode, int hr_mode,
							  double ini_thresh, double gm_falloff, double scl_falloff, double scl_translate,
							  double w2d, double bins, int dslt_r, int dslt_q, double dslt_c, bool ortho, bool use_absolute_value)
{
	/*if (m_vr)
	{
		int curlv = -1;
		if (isBrxml())
		{
			curlv = GetLevel();
			SetLevel(GetMaskLv());
		}
		m_vr->set_2d_mask(m_2d_mask);
		m_vr->set_2d_weight(m_2d_weight1, m_2d_weight2);
		m_vr->draw_mask_dslt(type, paint_mode, hr_mode, ini_thresh, gm_falloff, scl_falloff, scl_translate, w2d, bins, ortho, false, dslt_r, dslt_q, dslt_c*GetMaxValue());

		if (isBrxml())
			SetLevel(curlv);
	}*/
}

//draw label (create the label)
//type: 0-initialize; 1-maximum intensity filtering
//mode: 0-normal; 1-posterized; 2-copy values; 3-poster, copy
void VolumeData::DrawLabel(int type, int mode, double thresh, double gm_falloff)
{
	if (m_vr)
	{
		int curlv = -1;
		if (isBrxml())
		{
			curlv = GetLevel();
			SetLevel(GetMaskLv());
		}

		m_vr->draw_label(type, mode, thresh, gm_falloff);

		if (isBrxml())
			SetLevel(curlv);
	}
}

//calculation
void VolumeData::Calculate(int type, VolumeData *vd_a, VolumeData *vd_b, VolumeData* vd_c)
{
	if (m_vr)
	{
		if (type == 10 || type == 11)
		{
			VolumeRenderer *a, *b, *c;
			Texture *ma = nullptr, *mb = nullptr, *mc = nullptr;
			Texture* la = nullptr, *lb = nullptr, *lc = nullptr;
			a = vd_a ? vd_a->GetVR() : nullptr;
			b = vd_b ? vd_b->GetVR() : nullptr;
			c = vd_c ? vd_c->GetVR() : nullptr;
			if (vd_a && vd_a->GetSharedMaskName().length() > 0)
			{
				if (vd_b && vd_b->GetName() == vd_a->GetSharedMaskName())
					ma = vd_b->GetTexture();
				else if (vd_c && vd_c->GetName() == vd_a->GetSharedMaskName())
					ma = vd_c->GetTexture();

				if (vd_b && vd_b->GetName() == vd_a->GetSharedLabelName())
					la = vd_b->GetTexture();
				else if (vd_c && vd_c->GetName() == vd_a->GetSharedLabelName())
					la = vd_c->GetTexture();
			}
			m_vr->calculate(type, a, b, c, ma, la);
			m_vr->return_volume();
			if (m_tex && m_tex->get_nrrd(0))
			{
				Nrrd* nrrd_data = m_tex->get_nrrd(0)->getNrrd();
				uint8* val8nr = (uint8*)nrrd_data->data;
				int max_val = 255;
				int bytes = m_tex->get_nrrd(0)->getBytesPerSample();
				unsigned long long mem_size = (unsigned long long)m_res_x * (unsigned long long)m_res_y * (unsigned long long)m_res_z * bytes;
				if (nrrd_data->type == nrrdTypeUChar)
					max_val = *std::max_element(val8nr, val8nr + mem_size);
				else if (nrrd_data->type == nrrdTypeUShort)
					max_val = *std::max_element((uint16*)val8nr, (uint16*)val8nr + mem_size / bytes);
				else if (nrrd_data->type == nrrdTypeFloat)
					max_val = *std::max_element((float*)val8nr, (float*)val8nr + mem_size / bytes);
				SetMaxValue(max_val);
			}
		}
		else
		{
			if (type == 6 || type == 7)
				m_vr->set_hi_thresh(vd_a->GetRightThresh());
			m_vr->calculate(type, vd_a ? vd_a->GetVR() : 0, vd_b ? vd_b->GetVR() : 0);
			m_vr->return_volume();
			if (m_tex && m_tex->get_nrrd(0))
			{
				Nrrd* nrrd_data = m_tex->get_nrrd(0)->getNrrd();
				uint8* val8nr = (uint8*)nrrd_data->data;
				int max_val = 255;
				int bytes = m_tex->get_nrrd(0)->getBytesPerSample();
				unsigned long long mem_size = (unsigned long long)m_res_x * (unsigned long long)m_res_y * (unsigned long long)m_res_z * bytes;
				if (nrrd_data->type == nrrdTypeUChar)
					max_val = *std::max_element(val8nr, val8nr + mem_size);
				else if (nrrd_data->type == nrrdTypeUShort)
					max_val = *std::max_element((uint16*)val8nr, (uint16*)val8nr + mem_size / bytes);
				else if (nrrd_data->type == nrrdTypeFloat)
					max_val = *std::max_element((float*)val8nr, (float*)val8nr + mem_size / bytes);
				SetMaxValue(max_val);
			}
		}
	}
}

//set 2d mask for segmentation
void VolumeData::Set2dMask(std::shared_ptr<vks::VTexture>& mask)
{
	m_2d_mask = mask;
}

//set 2d weight map for segmentation
void VolumeData::Set2DWeight(std::shared_ptr<vks::VTexture>& weight1, std::shared_ptr<vks::VTexture>& weight2)
{
	m_2d_weight1 = weight1;
	m_2d_weight2 = weight2;
}

//set 2d depth map for rendering shadows
void VolumeData::Set2dDmap(std::shared_ptr<vks::VTexture>& dmap)
{
	m_2d_dmap = dmap;
	if (m_vr)
		m_vr->set_2d_dmap(m_2d_dmap);
}

//properties
//transfer function
void VolumeData::Set3DGamma(double dVal)
{
	m_gamma3d = dVal;
	if (m_vr)
		m_vr->set_gamma3d(m_gamma3d);
}

double VolumeData::Get3DGamma()
{
	return m_gamma3d;
}

void VolumeData::SetBoundary(double dVal)
{
	m_gm_thresh = dVal;
	if (m_vr)
		m_vr->set_gm_thresh(m_gm_thresh);
}

double VolumeData::GetBoundary()
{
	return m_gm_thresh;
}

void VolumeData::SetOffset(double dVal)
{
	m_offset = dVal;
	if (m_vr)
		m_vr->set_offset(m_offset);
}

double VolumeData::GetOffset()
{
	return m_offset;
}

void VolumeData::SetLeftThresh(double dVal)
{
	m_lo_thresh = dVal;
	if (m_vr)
		m_vr->set_lo_thresh(m_lo_thresh);
}

double VolumeData::GetLeftThresh()
{
	return m_lo_thresh;
}

void VolumeData::SetRightThresh(double dVal)
{
	m_hi_thresh = dVal;
	if (m_vr)
		m_vr->set_hi_thresh(m_hi_thresh);
}

double VolumeData::GetRightThresh()
{
	return m_hi_thresh;
}

void VolumeData::SetColor(const Color &color, bool update_hsv)
{
	m_color = color;
	if (update_hsv)
		SetHSV();
	if (m_vr)
		m_vr->set_color(color);
}

Color VolumeData::GetColor()
{
	return m_color;
}

void VolumeData::SetMaskColor(Color &color, bool set)
{
	if (m_vr)
		m_vr->set_mask_color(color, set);
}

Color VolumeData::GetMaskColor()
{
	Color result;
	if (m_vr)
		result = m_vr->get_mask_color();
	return result;
}

bool VolumeData::GetMaskColorSet()
{
	if (m_vr)
		return m_vr->get_mask_color_set();
	else
		return false;
}

void VolumeData::ResetMaskColorSet()
{
	if (m_vr)
		m_vr->reset_mask_color_set();
}

Color VolumeData::SetLuminance(double dVal)
{
	double h, s, v;
	GetHSV(h, s, v);
	HSVColor hsv(h, s, dVal);
	m_color = Color(hsv);
	if (m_vr)
		m_vr->set_color(m_color);
	return m_color;
}

double VolumeData::GetLuminance()
{
	HSVColor hsv(m_color);
	return hsv.val();
}

void VolumeData::SetAlpha(double alpha)
{
	m_alpha = alpha;
	if (m_vr)
		m_vr->set_alpha(m_alpha);
}

double VolumeData::GetAlpha()
{
	return m_alpha;
}

void VolumeData::SetEnableAlpha(bool mode)
{
	if (m_vr)
	{
		m_vr->set_solid(!mode);
		if (mode)
			m_vr->set_alpha(m_alpha);
		else
			m_vr->set_alpha(1.0);
	}
}

bool VolumeData::GetEnableAlpha()
{
	if (m_vr)
		return !m_vr->get_solid();
	else
		return true;
}

void VolumeData::SetHSV(double hue, double sat, double val)
{
	if (hue < 0 || sat < 0 || val < 0)
	{
		m_hsv = HSVColor(m_color);
	}
	else
	{
		m_hsv = HSVColor(hue, sat, val);
	}
}

void VolumeData::GetHSV(double &hue, double &sat, double &val)
{
	hue = m_hsv.hue();
	sat = m_hsv.sat();
	val = m_hsv.val();
}

//mask threshold
void VolumeData::SetMaskThreshold(double thresh)
{
	if (m_use_mask_threshold && m_vr)
		m_vr->set_mask_thresh(thresh);
}

void VolumeData::SetUseMaskThreshold(bool mode)
{
	m_use_mask_threshold = mode;
	if (m_vr && !m_use_mask_threshold)
		m_vr->set_mask_thresh(0.0);
}

//shading
void VolumeData::SetShading(bool bVal)
{
	m_shading = bVal;
	if (m_vr)
		m_vr->set_shading(bVal);
}

bool VolumeData::GetShading()
{
	return m_shading;
}

//shadow
void VolumeData::SetShadow(bool bVal)
{
	m_shadow = bVal;
}

bool VolumeData::GetShadow()
{
	return m_shadow;
}

void VolumeData::SetShadowParams(double val)
{
	m_shadow_darkness = val;
}

void VolumeData::GetShadowParams(double &val)
{
	val = m_shadow_darkness;
}

void VolumeData::SetMaterial(double amb, double diff, double spec, double shine)
{
	m_mat_amb = amb;
	m_mat_diff = diff;
	m_mat_spec = spec;
	m_mat_shine = shine;
	if (m_vr)
		m_vr->set_material(m_mat_amb, m_mat_diff, m_mat_spec, m_mat_shine);
}

void VolumeData::GetMaterial(double &amb, double &diff, double &spec, double &shine)
{
	amb = m_mat_amb;
	diff = m_mat_diff;
	spec = m_mat_spec;
	shine = m_mat_shine;
}

void VolumeData::SetLowShading(double dVal)
{
	double amb, diff, spec, shine;
	GetMaterial(amb, diff, spec, shine);
	SetMaterial(dVal, diff, spec, shine);
}

void VolumeData::SetHiShading(double dVal)
{
	double amb, diff, spec, shine;
	GetMaterial(amb, diff, spec, shine);
	SetMaterial(amb, diff, spec, dVal);
}

//sample rate
void VolumeData::SetSampleRate(double rate)
{
	m_sample_rate = rate;
	if (m_vr)
		m_vr->set_sampling_rate(m_sample_rate);
}

double VolumeData::GetSampleRate()
{
	return m_sample_rate;
}

//colormap mode
void VolumeData::SetColormapMode(int mode)
{
	m_colormap_mode = mode;
	if (m_vr)
	{
		m_vr->set_colormap_mode(m_colormap_mode);
		m_vr->set_color(m_color);
	}
}

int VolumeData::GetColormapMode()
{
	return m_colormap_mode;
}

void VolumeData::SetColormapDisp(bool disp)
{
	m_colormap_disp = disp;
}

bool VolumeData::GetColormapDisp()
{
	return m_colormap_disp;
}

void VolumeData::SetColormapValues(double low, double high)
{
	m_colormap_low_value = low;
	m_colormap_hi_value = high;
	if (m_vr)
		m_vr->set_colormap_values(
		m_colormap_low_value, m_colormap_hi_value);
}

void VolumeData::GetColormapValues(double &low, double &high)
{
	low = m_colormap_low_value;
	high = m_colormap_hi_value;
}

void VolumeData::SetColormap(int value)
{
	m_colormap = value;
	if (m_vr)
		m_vr->set_colormap(m_colormap);
}

void VolumeData::SetColormapProj(int value)
{
	m_colormap_proj = value;
	if (m_vr)
		m_vr->set_colormap_proj(m_colormap_proj);
}

int VolumeData::GetColormap()
{
	return m_colormap;
}

int VolumeData::GetColormapProj()
{
	return m_colormap_proj;
}

//resolution  scaling and spacing
void VolumeData::GetResolution(int &res_x, int &res_y, int &res_z)
{
	res_x = m_res_x;
	res_y = m_res_y;
	res_z = m_res_z;
}

void VolumeData::SetScalings(double sclx, double scly, double sclz)
{
	m_sclx = sclx; m_scly = scly; m_sclz = sclz;
}

void VolumeData::GetScalings(double &sclx, double &scly, double &sclz)
{
	sclx = m_sclx; scly = m_scly; sclz = m_sclz;
}

//brxml�̂Ƃ��͍ŉ��w��spacing���ݒ肷���i���̊K�w�͎����I�Ɍv�Z�j
void VolumeData::SetSpacings(double spcx, double spcy, double spcz)
{
	if (GetTexture())
	{
		GetTexture()->set_spacings(spcx, spcy, spcz);
		m_bounds.reset();
		GetTexture()->get_bounds(m_bounds);
	}
}

void VolumeData::GetSpacings(double &spcx, double &spcy, double & spcz, int lv)
{
	if (GetTexture())
		GetTexture()->get_spacings(spcx, spcy, spcz, lv);
}

void VolumeData::SetBaseSpacings(double spcx, double spcy, double spcz)
{
	if (GetTexture())
	{
		GetTexture()->set_base_spacings(spcx, spcy, spcz);
		m_bounds.reset();
		GetTexture()->get_bounds(m_bounds);
	}
}

void VolumeData::GetBaseSpacings(double &spcx, double &spcy, double & spcz)
{
	if (GetTexture())
		GetTexture()->get_base_spacings(spcx, spcy, spcz);
}

void VolumeData::SetSpacingScales(double s_spcx, double s_spcy, double s_spcz)
{
	if (GetTexture())
	{
		GetTexture()->set_spacing_scales(s_spcx, s_spcy, s_spcz);
		m_bounds.reset();
		GetTexture()->get_bounds(m_bounds);
	}
}

void VolumeData::GetSpacingScales(double &s_spcx, double &s_spcy, double &s_spcz)
{
	if (GetTexture())
		GetTexture()->get_spacing_scales(s_spcx, s_spcy, s_spcz);
}

void VolumeData::SetLevel(int lv)
{
	if (GetTexture() && isBrxml())
	{
		GetTexture()->setLevel(lv);
		m_bounds.reset();
		GetTexture()->get_bounds(m_bounds);
	}
}

int VolumeData::GetLevel()
{
	if (GetTexture() && isBrxml())
		return GetTexture()->GetCurLevel();
	else
		return -1;
}

int VolumeData::GetLevelNum()
{
	if (GetTexture() && isBrxml())
		return GetTexture()->GetLevelNum();
	else
		return -1;
}

void VolumeData::GetFileSpacings(double &spcx, double &spcy, double &spcz)
{
	spcx = m_spcx; spcy = m_spcy; spcz = m_spcz;
}

//display controls
void VolumeData::SetDisp(bool disp)
{
	m_disp = disp;
	GetTexture()->set_sort_bricks();
	if (!disp)
	{
		GetMask(true);
		GetVR()->clear_tex_current();
	}
}

bool VolumeData::GetDisp()
{
	return m_disp;
}

void VolumeData::ToggleDisp()
{
	m_disp = !m_disp;
	GetTexture()->set_sort_bricks();
	if (!m_disp)
	{
		GetMask(true);
		GetVR()->clear_tex_current();
	}
}

//bounding box
void VolumeData::SetDrawBounds(bool draw)
{
	m_draw_bounds = draw;
}

bool VolumeData::GetDrawBounds()
{
	return m_draw_bounds;
}

void VolumeData::ToggleDrawBounds()
{
	m_draw_bounds = !m_draw_bounds;
}

//wireframe
void VolumeData::SetWireframe(bool val)
{
	m_test_wiref = val;
}

//blend mode
void VolumeData::SetBlendMode(int mode)
{
	m_blend_mode = mode;
}

int VolumeData::GetBlendMode()
{
	return m_blend_mode;
}

//clip distance
void VolumeData::SetClipDistance(int distx, int disty, int distz)
{
	m_clip_dist_x = distx;
	m_clip_dist_y = disty;
	m_clip_dist_z = distz;
}

void VolumeData::GetClipDistance(int &distx, int &disty, int &distz)
{
	distx = m_clip_dist_x;
	disty = m_clip_dist_y;
	distz = m_clip_dist_z;
}

//randomize color
void VolumeData::RandomizeColor()
{
	double hue = (double)rand()/(RAND_MAX) * 360.0;
	Color color(HSVColor(hue, 1.0, 1.0));
	SetColor(color);
}

//shown in legend
void VolumeData::SetLegend(bool val)
{
	m_legend = val;
}

bool VolumeData::GetLegend()
{
	return m_legend;
}

//interpolate
void VolumeData::SetInterpolate(bool val)
{
	if (m_vr)
		m_vr->set_interpolate(val);
	m_interpolate = val;
}

bool VolumeData::GetInterpolate()
{
	return m_interpolate;
}

void VolumeData::SetFog(bool use_fog,
	double fog_intensity, double fog_start, double fog_end)
{
	if (m_vr)
		m_vr->set_fog(use_fog, fog_intensity, fog_start, fog_end);
}

int VolumeData::GetLevelBySize(size_t size)
{
	if (!m_tex || !m_tex->isBrxml())
		return 0;

	size_t nx, ny, nz, nb;
	int lvnum = m_tex->GetLevelNum();
	int i;
	for (i = 0; i < lvnum; i++)
	{
		nx = ny = nz = nb = 0;
		m_tex->get_dimensions(nx, ny, nz, i);
		nb = m_tex->nb(0);
		if (nx*ny*nz*nb <= size) break; 
	}
	return i;
}

VolumeData* VolumeData::CopyLevel(int lv)
{
	if(!m_tex->isBrxml()) return NULL;

	VolumeData* vd = new VolumeData();

	m_tex->set_FrameAndChannel(m_time, m_chan);
	auto src_nv = make_shared<VL_Nrrd>(m_tex->loadData(lv));
	if (!src_nv) return NULL;
	vd->Load(src_nv, GetName() + wxT("_Copy_Lv") + wxString::Format("%d", lv), wxString(""));

	vd->m_dup = true;
	vd->m_dup_counter = m_dup_counter;
	
	Texture *tex = vd->GetTexture();
	if (!tex || !tex->get_nrrd(0)) return NULL;

	vector<Plane*> *planes = GetVR() ? GetVR()->get_planes() : 0;
	if (planes && vd->GetVR())
		vd->GetVR()->set_planes(planes);

	double spc[3];
	GetSpacings(spc[0], spc[1], spc[2], lv);
	vd->SetSpacings(spc[0], spc[1], spc[2]);
	double scl[3];
	GetScalings(scl[0], scl[1], scl[2]);
	vd->SetScalings(scl[0], scl[1], scl[2]);

	vd->SetColor(GetColor());
	bool bval = GetEnableAlpha();
	vd->SetEnableAlpha(GetEnableAlpha());
	vd->SetShading(GetShading());
	vd->SetShadow(GetShadow());
	double darkness;
	GetShadowParams(darkness);
	vd->SetShadowParams(darkness);
	//other settings
	double amb, diff, spec, shine;
	GetMaterial(amb, diff, spec, shine);
	vd->Set3DGamma(Get3DGamma());
	vd->SetBoundary(GetBoundary());
	vd->SetOffset(GetOffset());
	vd->SetLeftThresh(GetLeftThresh());
	vd->SetRightThresh(GetRightThresh());
	vd->SetAlpha(GetAlpha());
	vd->SetSampleRate(GetSampleRate());
	vd->SetMaterial(amb, diff, spec, shine);

	//layer properties
	vd->SetGamma(GetGamma());
	vd->SetBrightness(GetBrightness());
	vd->SetHdr(GetHdr());
	vd->SetSyncR(GetSyncR());
	vd->SetSyncG(GetSyncG());
	vd->SetSyncB(GetSyncB());

	//current channel index
	vd->m_chan = 0;
	vd->m_time = 0;

	//modes
	vd->SetMode(GetMode());
	//stream modes
	vd->SetStreamMode(GetStreamMode());
	
	//volume properties
	vd->SetScalarScale(GetScalarScale());
	vd->SetGMScale(GetGMScale());
	vd->SetHSV();

	//noise reduction
	vd->SetNR(GetNR());
	
	//display control
	vd->SetDisp(GetDisp());
	vd->SetDrawBounds(GetDrawBounds());
	vd->m_test_wiref = m_test_wiref;

	//colormap mode
	vd->SetColormapMode(GetColormapMode());
	vd->SetColormapDisp(GetColormapDisp());
	vd->SetColormapValues(m_colormap_low_value, m_colormap_hi_value);
	
	//blend mode
	vd->SetBlendMode(GetBlendMode());

	vd->m_2d_mask = 0;
	vd->m_2d_weight1 = 0;
	vd->m_2d_weight2 = 0;
	vd->m_2d_dmap = 0;

	//clip distance
	int dists[3];
	GetClipDistance(dists[0], dists[1], dists[2]);
	vd->SetClipDistance(dists[0], dists[1], dists[2]);
	
	//compression
	vd->m_compression = false;

	//skip brick
	vd->m_skip_brick = false;

	//legend
	vd->m_legend = false;

	vd->m_annotation = m_annotation;

	vd->m_landmarks = m_landmarks;

	auto maskindata = GetMask(true);
	if (maskindata)
	{
		auto mask = make_shared<VL_Nrrd>(maskindata->getNrrdDeepCopy());
		vd->LoadMask(mask);
	}

	return vd;
}

bool VolumeData::isBrxml()
{
	if (!m_tex) return false;

	return m_tex->isBrxml();
}

MeshData *VolumeData::ExportMeshMask()
{
    MeshData *result = NULL;
//    
//#ifdef _WIN32
//    
//	if (!m_tex || m_tex->nmask() == -1) return NULL;
//
//	int curlv = -1;
//	if (isBrxml())
//	{
//		curlv = GetLevel();
//		SetLevel(GetMaskLv());
//	}
//	
//	m_vr->return_mask();
//	
//	Nrrd *nrrd = m_tex->get_nrrd(m_tex->nmask());
//	double spcx = 1.0, spcy = 1.0, spcz = 1.0;
//	GetSpacings(spcx, spcy, spcz);
//	size_t res_x = m_tex->nx();
//	size_t res_y = m_tex->ny();
//	size_t res_z = m_tex->nz();
//
//	vtkSmartPointer<vtkImageImport> imageImport = vtkSmartPointer<vtkImageImport>::New();
//	imageImport->SetDataSpacing(spcx, spcy, spcz);
//	imageImport->SetDataOrigin(0, 0, 0);
//	imageImport->SetWholeExtent(0, res_x-1, 0, res_y-1, 0, res_z-1);
//	imageImport->SetDataExtentToWholeExtent();
//	imageImport->SetDataScalarTypeToUnsignedChar();
//	imageImport->SetNumberOfScalarComponents(1);
//	imageImport->SetImportVoidPointer(nrrd->data);
//	imageImport->Update();
//	vtkSmartPointer<vtkImageData> volume = imageImport->GetOutput();
//
//	vtkSmartPointer<vtkFlyingEdges3D> surface = vtkSmartPointer<vtkFlyingEdges3D>::New();
//	surface->SetInputData(imageImport->GetOutput());
//	surface->ComputeNormalsOn();
//	surface->SetValue(0, 0.5);
//	surface->Update();
///*	vtkSmartPointer<vtkPolyDataNormals> normalGenerator = vtkSmartPointer<vtkPolyDataNormals>::New();
//	normalGenerator->SetInputData(surface->GetOutput());
//	normalGenerator->ComputePointNormalsOn();
//	normalGenerator->ComputeCellNormalsOff();
//	normalGenerator->SetOutputPointsPrecision(vtkAlgorithm::DesiredOutputPrecision::SINGLE_PRECISION);
//	normalGenerator->Update();
//	vtkSmartPointer<vtkPolyData> mesh = normalGenerator->GetOutput();
//*/	
//	vtkSmartPointer<vtkPolyData> mesh = surface->GetOutput();
//
//	//Convert to MeshData
//	vtkSmartPointer<vtkPoints> points = mesh->GetPoints();
//	vtkSmartPointer<vtkCellArray> faces = mesh->GetPolys();
//	vtkSmartPointer<vtkFloatArray> normals = vtkFloatArray::SafeDownCast(mesh->GetPointData()->GetNormals());
//	float *raw_normals = normals->GetPointer(0); 
//	
//	vtkIdType vnum = points->GetNumberOfPoints();
//	vtkIdType fnum = faces->GetNumberOfCells();
//
//	GLMmodel *model = (GLMmodel*)malloc(sizeof(GLMmodel));
//	float *verts = (float*)malloc(sizeof(float)*vnum*3);
//	float *norms = (float*)malloc(sizeof(float)*vnum*3);
//	GLMtriangle *tris = (GLMtriangle*)malloc(sizeof(GLMtriangle)*fnum);
//	unsigned int *gtris = (unsigned int*)malloc(sizeof(unsigned int)*fnum);
//
//	#pragma omp parallel
//	{
//		#pragma omp for
//		for (int i = 0; i < vnum; i++)
//		{
//			double v[3];
//			points->GetPoint(i, v);
//			verts[3*i  ] = (float)v[0];
//			verts[3*i+1] = (float)v[1];
//			verts[3*i+2] = (float)v[2];
//			norms[3*i  ] = raw_normals[3*i  ];
//			norms[3*i+1] = raw_normals[3*i+1];
//			norms[3*i+2] = raw_normals[3*i+2];
//		}
//
//		#pragma omp for
//		for (vtkIdType i = 0; i < fnum; i++)
//		{
//			vtkIdType numIds;
//			vtkIdType *pointIds;
//
//			faces->GetCell(4*i, numIds, pointIds);
//
//			if (numIds != 3)
//				break;
//
//			tris[i].vindices[0] = pointIds[0];
//			tris[i].nindices[0] = pointIds[0];
//			tris[i].vindices[1] = pointIds[1];
//			tris[i].nindices[1] = pointIds[1];
//			tris[i].vindices[2] = pointIds[2];
//			tris[i].nindices[2] = pointIds[2];
//			gtris[i] = i;
//		}
//	}
//
//	model->pathname    = STRDUP(m_name.ToStdString().c_str());
//	model->mtllibname    = NULL;
//	model->numvertices   = vnum;
//	model->vertices    = verts;
//	model->numnormals    = fnum;
//	model->normals     = norms;
//	model->numtexcoords  = 0;
//	model->texcoords       = NULL;
//	model->numfacetnorms = 0;
//	model->facetnorms    = NULL;
//	model->numtriangles  = fnum;
//	model->triangles       = tris;
//	model->numlines = 0;
//	model->lines = NULL;
//	model->nummaterials  = 0;
//	model->materials       = NULL;
//	model->numgroups       = 0;
//	model->groups      = NULL;
//	model->position[0]   = 0.0;
//	model->position[1]   = 0.0;
//	model->position[2]   = 0.0;
//	model->hastexture = false;
//	
//	GLMgroup* group;
//	group = (GLMgroup*)malloc(sizeof(GLMgroup));
//	group->name = STRDUP("default");
//	group->material = 0;
//	group->numtriangles = fnum;
//	group->triangles = gtris;
//	group->next = model->groups;
//	model->groups = group;
//	model->numgroups++;
//
//	result = new MeshData();
//	result->Load(model);
//	wxString newname = m_name + wxT("_MeshMask");
//	result->SetName(newname);
//    
//#endif
//
//	if (isBrxml())
//		SetLevel(curlv);
    
    return result;
}

//test if point is inside the clipping planes
bool VolumeData::InsideClippingPlanes(Point pos)
{
    pos.z(-pos.z());
    
    if (!m_vr)
        return true;
    
    vector<Plane*> *planes = m_vr->get_planes();
    if (!planes)
        return true;
    if (planes->size() != 6)
        return true;
    
    
    if (!m_tex)
        return true;
    Transform *tform = m_tex->transform();
    if (!tform)
        return true;
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
    
    BBox dbox;
    for (int j = 0; j < 8; j++)
    {
        p[j] = tform_copy.project(p[j]);
        dbox.extend(p[j]);
    }
    
    pos.x( (pos.x() - dbox.min().x()) / (dbox.max().x() - dbox.min().x()) );
    pos.y( (pos.y() - dbox.min().y()) / (dbox.max().y() - dbox.min().y()) );
    pos.z( (pos.z() + dbox.max().z()) / (dbox.max().z() - dbox.min().z()) );

    Plane* plane = 0;
    for (int i=0; i<6; i++)
    {
        plane = (*planes)[i];
        if (!plane)
            continue;
        if (plane->eval_point(pos) < 0)
            return false;
    }

    return true;
}

Nrrd* VolumeData::NrrdScale(Nrrd* src, size_t dst_datasize, bool interpolation)
{
	if (!src) return nullptr;

	int bytes = 0;
	int nx, ny, nz;
	double spcx, spcy, spcz;

	if (src->type == nrrdTypeChar || src->type == nrrdTypeUChar)
		bytes = 1;
	else if(src->type == nrrdTypeShort || src->type == nrrdTypeUShort)
		bytes = 2;
	else if (src->type == nrrdTypeFloat)
		bytes = 4;

	size_t dim = src->dim;

	if (dim <= 2)
		return nullptr;

	std::vector<size_t> ssize(dim);
	std::vector<double> sspc(dim);

	int offset = 0;
	if (dim > 3) offset = 1;

	double maxspc = sspc[0];
	int maxspc_id = 0;
	for (size_t p = 0; p < dim; p++)
	{
		ssize[p] = (int)src->axis[p + offset].size;
		sspc[p] = src->axis[p + offset].spacing;
		if (sspc[p] > maxspc)
		{
			maxspc = sspc[p];
			maxspc_id = p;
		}
	}

	std::vector<double> sc1(3);
	for (size_t p = 0; p < 3; p++)
		sc1[p] = sspc[p] / maxspc;
	
	size_t src_datasize = ssize[0] * ssize[1] * ssize[2] * bytes;

	if (src_datasize < dst_datasize)
		return nullptr;

	if (src_datasize * sc1[0] * sc1[1] * sc1[2] > dst_datasize)
	{
		double dnx = ssize[0] * sc1[0];
		double dny = ssize[1] * sc1[1];
		double dnz = ssize[2] * sc1[2];

		double sc2 = cbrt(dst_datasize / (dnx * dny * dnz * bytes));

		nx = (size_t)(dnx * sc2);
		ny = (size_t)(dny * sc2);
		nz = (size_t)(dnz * sc2);
		spcx = spcy = spcz = maxspc / sc2;
	}
	else
	{
		std::vector<size_t> dsize(3);
		std::vector<double> dspc(3);

		double sc2 = sqrt((double)dst_datasize / (double)src_datasize);
		for (size_t p = 0; p < 3; p++)
		{
			if (p != maxspc_id)
			{
				dsize[p] = (size_t)(ssize[p] * sc2);
				dspc[p] = sspc[p] / sc2;
			}
            else
            {
                dsize[p] = ssize[p];
                dspc[p] = sspc[p];
            }
		}
		nx = dsize[0];
		ny = dsize[1];
		nz = dsize[2];
		spcx = dspc[0];
		spcy = dspc[1];
		spcz = dspc[2];
	}

	return NrrdScale(src, nx, ny, nz, interpolation);
}

Nrrd* VolumeData::NrrdScale(Nrrd* src, size_t nx, size_t ny, size_t nz, bool interpolation)
{
	if (!src) return nullptr;

	int bytes = 0;

	if (src->type == nrrdTypeChar || src->type == nrrdTypeUChar)
		bytes = 1;
	else if (src->type == nrrdTypeShort || src->type == nrrdTypeUShort)
		bytes = 2;
	else if (src->type == nrrdTypeFloat)
		bytes = 4;

	size_t dim = src->dim;

	if (dim <= 2)
		return nullptr;

	std::vector<size_t> ssize(dim);
	std::vector<double> sspc(dim);

	int offset = 0;
	if (dim > 3) offset = 1;

	double maxspc = sspc[0];
	int maxspc_id = 0;
	for (size_t p = 0; p < dim; p++)
	{
		ssize[p] = (int)src->axis[p + offset].size;
		sspc[p] = src->axis[p + offset].spacing;
	}

	double spcx = sspc[0] * ssize[0] / nx;
	double spcy = sspc[1] * ssize[1] / ny;
	double spcz = sspc[2] * ssize[2] / nz;

	Nrrd* nv = nrrdNew();
	if (bytes == 1)
	{
		unsigned long long mem_size = (unsigned long long)nx *
			(unsigned long long)ny * (unsigned long long)nz;
		uint8* val8 = new (std::nothrow) uint8[mem_size];
		if (!val8)
		{
			wxMessageBox("Not enough memory. Please save project and restart.");
			return nullptr;
		}
		memset((void*)val8, 0, sizeof(uint8) * nx * ny * nz);
		nrrdWrap(nv, val8, nrrdTypeUChar, 3, (size_t)nx, (size_t)ny, (size_t)nz);
	}
	else if (bytes == 2)
	{
		unsigned long long mem_size = (unsigned long long)nx *
			(unsigned long long)ny * (unsigned long long)nz;
		uint16* val16 = new (std::nothrow) uint16[mem_size];
		if (!val16)
		{
			wxMessageBox("Not enough memory. Please save project and restart.");
			return nullptr;
		}
		memset((void*)val16, 0, sizeof(uint16) * nx * ny * nz);
		nrrdWrap(nv, val16, nrrdTypeUShort, 3, (size_t)nx, (size_t)ny, (size_t)nz);
	}
	else if (bytes == 4)
	{
		unsigned long long mem_size = (unsigned long long)nx *
			(unsigned long long)ny * (unsigned long long)nz;
		float* val32 = new (std::nothrow) float[mem_size];
		if (!val32)
		{
			wxMessageBox("Not enough memory. Please save project and restart.");
			return nullptr;
		}
		memset((void*)val32, 0, sizeof(float) * nx * ny * nz);
		nrrdWrap(nv, val32, nrrdTypeFloat, 3, (size_t)nx, (size_t)ny, (size_t)nz);
	}
	nrrdAxisInfoSet(nv, nrrdAxisInfoSpacing, spcx, spcy, spcz);
	nrrdAxisInfoSet(nv, nrrdAxisInfoMax, spcx * nx, spcy * ny, spcz * nz);
	nrrdAxisInfoSet(nv, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
	nrrdAxisInfoSet(nv, nrrdAxisInfoSize, (size_t)nx, (size_t)ny, (size_t)nz);

	NrrdScaler scaler(src, nv, interpolation);

	scaler.Run();

	return nv;
}

NrrdScaler::NrrdScaler(Nrrd* src, Nrrd* dst, bool interpolation)
{
	m_src = src;
	m_dst = dst;
	m_interpolation = interpolation;
}

NrrdScaler::~NrrdScaler()
{

}

void NrrdScaler::Run()
{
	m_running_nrrd_scale_th = 0;

	size_t dim = m_dst->dim;

	std::vector<size_t> dsize(dim);
	std::vector<double> dspc(dim);

	int doffset = 0;
	if (dim > 3) doffset = 1;

	for (size_t p = 0; p < dim; p++)
		dsize[p] = (int)m_dst->axis[p + doffset].size;

	int nz = dsize[2];

	int thread_num = wxThread::GetCPUCount() - 1;
	if (thread_num <= 0)
		thread_num = 1;
	if (nz / thread_num == 0)
		thread_num = nz;
	int interval = nz / thread_num;

	for (int z = 0; z < nz; z += interval)
	{
		int zed = (z + interval > nz) ? nz : z + interval;
		NrrdScaleThread* th = new NrrdScaleThread(this, m_src, m_dst, z, zed, m_interpolation);
		th->Run();
	}

	while (1)
	{
		{
			wxCriticalSectionLocker enter(m_pThreadCS);
			if (m_running_nrrd_scale_th <= 0)
				break;
		}
		wxThread::This()->Sleep(10);
	}
	m_running_nrrd_scale_th = 0;

}

NrrdScaleThread::NrrdScaleThread(NrrdScaler* scaler, Nrrd* src, Nrrd* dst, int zst, int zed, bool interpolation)
{
	m_scaler = scaler;
	m_src = src;
	m_dst = dst;
	m_zst = zst;
	m_zed = zed;
	m_interpolation = interpolation;
}

NrrdScaleThread::~NrrdScaleThread()
{
	if (m_scaler)
	{
		wxCriticalSectionLocker enter(m_scaler->m_pThreadCS);
		m_scaler->m_running_nrrd_scale_th--;
	}
}

wxThread::ExitCode NrrdScaleThread::Entry()
{
	m_scaler->m_pThreadCS.Enter();
	m_scaler->m_running_nrrd_scale_th++;
	m_scaler->m_pThreadCS.Leave();

	int bits = 0;

	if (m_src->type == nrrdTypeChar || m_src->type == nrrdTypeUChar)
		bits = 1;
	else if (m_src->type == nrrdTypeShort || m_src->type == nrrdTypeUShort)
		bits = 2;
	else if (m_src->type == nrrdTypeFloat)
		bits = 4;

	size_t dim = m_src->dim;

	if (dim <= 2)
		return (wxThread::ExitCode)1;

	std::vector<size_t> ssize(dim);
	std::vector<double> sspc(dim);

	int offset = 0;
	if (dim > 3) offset = 1;

	for (size_t p = 0; p < dim; p++)
	{
		ssize[p] = (int)m_src->axis[p + offset].size;
		sspc[p] = m_src->axis[p + offset].spacing;
	}

	std::vector<size_t> dsize(dim);
	std::vector<double> dspc(dim);

	int doffset = 0;
	if (dim > 3) doffset = 1;

	for (size_t p = 0; p < dim; p++)
	{
		dsize[p] = (int)m_dst->axis[p + doffset].size;
		dspc[p] = m_dst->axis[p + doffset].spacing;
	}

	double scx = (double)ssize[0] / dsize[0];
	double scy = (double)ssize[1] / dsize[1];
	double scz = (double)ssize[2] / dsize[2];
	int nx = dsize[0];
	int ny = dsize[1];
	int nz = dsize[2];
	if (m_interpolation)
	{
		if (bits == 1)
		{
			uint8* data = (uint8*)m_src->data;
			uint8* dst_data = (uint8*)m_dst->data;
			for (int z = m_zst; z < m_zed; z++)
			{
				for (int y = 0; y < ny; y++)
				{
					for (int x = 0; x < nx; x++)
					{
						double dx = x * scx;
						double dy = y * scy;
						double dz = z * scz;

						size_t ix = (size_t)dx;
						size_t iy = (size_t)dy;
						size_t iz = (size_t)dz;

						dx = dx - ix;
						dy = dy - iy;
						dz = dz - iz;

						size_t id = iz * ssize[1] * ssize[0] + iy * ssize[0] + ix;
                        
                        size_t iddx = (x == nx - 1) ? 0 : 1;
                        size_t iddy = (y == ny - 1) ? 0 : ssize[0];
                        size_t iddz = (z == m_zed - 1) ? 0 : ssize[1] * ssize[0];

						float c000 = data[id];
						float c100 = data[id + iddx];
						float c010 = data[id + iddy];
						float c110 = data[id + iddy + iddx];
						float c001 = data[id + iddz];
						float c101 = data[id + iddz + iddx];
						float c011 = data[id + iddz + iddy];
						float c111 = data[id + iddz + iddy + iddx];

						float c00 = c000 * (1 - dx) + c100 * dx;
						float c01 = c001 * (1 - dx) + c101 * dx;
						float c10 = c010 * (1 - dx) + c110 * dx;
						float c11 = c011 * (1 - dx) + c111 * dx;

						float c0 = c00 * (1 - dy) + c10 * dy;
						float c1 = c01 * (1 - dy) + c11 * dy;

						float c = c0 * (1 - dz) + c1 * dz;

						dst_data[z * ny * nx + y * nx + x] = (uint8)c;
					}
				}
			}
		}
		else if (bits == 2)
		{
			uint16* data = (uint16*)m_src->data;
			uint16* dst_data = (uint16*)m_dst->data;
			for (int z = m_zst; z < m_zed; z++)
			{
				for (int y = 0; y < ny; y++)
				{
					for (int x = 0; x < nx; x++)
					{
						double dx = x * scx;
						double dy = y * scy;
						double dz = z * scz;

						size_t ix = (size_t)dx;
						size_t iy = (size_t)dy;
						size_t iz = (size_t)dz;

						dx = dx - ix;
						dy = dy - iy;
						dz = dz - iz;

						size_t id = iz * ssize[1] * ssize[0] + iy * ssize[0] + ix;
                        
                        size_t iddx = (x == nx - 1) ? 0 : 1;
                        size_t iddy = (y == ny - 1) ? 0 : ssize[0];
                        size_t iddz = (z == m_zed - 1) ? 0 : ssize[1] * ssize[0];
                        
                        float c000 = data[id];
                        float c100 = data[id + iddx];
                        float c010 = data[id + iddy];
                        float c110 = data[id + iddy + iddx];
                        float c001 = data[id + iddz];
                        float c101 = data[id + iddz + iddx];
                        float c011 = data[id + iddz + iddy];
                        float c111 = data[id + iddz + iddy + iddx];

						float c00 = c000 * (1 - dx) + c100 * dx;
						float c01 = c001 * (1 - dx) + c101 * dx;
						float c10 = c010 * (1 - dx) + c110 * dx;
						float c11 = c011 * (1 - dx) + c111 * dx;

						float c0 = c00 * (1 - dy) + c10 * dy;
						float c1 = c01 * (1 - dy) + c11 * dy;

						float c = c0 * (1 - dz) + c1 * dz;

						dst_data[z * ny * nx + y * nx + x] = (uint16)c;
					}
				}
			}
		}
		else if (bits == 4)
		{
			float* data = (float*)m_src->data;
			float* dst_data = (float*)m_dst->data;
			for (int z = m_zst; z < m_zed; z++)
			{
				for (int y = 0; y < ny; y++)
				{
					for (int x = 0; x < nx; x++)
					{
						double dx = x * scx;
						double dy = y * scy;
						double dz = z * scz;

						size_t ix = (size_t)dx;
						size_t iy = (size_t)dy;
						size_t iz = (size_t)dz;

						dx = dx - ix;
						dy = dy - iy;
						dz = dz - iz;

						size_t id = iz * ssize[1] * ssize[0] + iy * ssize[0] + ix;

						size_t iddx = (x == nx - 1) ? 0 : 1;
						size_t iddy = (y == ny - 1) ? 0 : ssize[0];
						size_t iddz = (z == m_zed - 1) ? 0 : ssize[1] * ssize[0];

						float c000 = data[id];
						float c100 = data[id + iddx];
						float c010 = data[id + iddy];
						float c110 = data[id + iddy + iddx];
						float c001 = data[id + iddz];
						float c101 = data[id + iddz + iddx];
						float c011 = data[id + iddz + iddy];
						float c111 = data[id + iddz + iddy + iddx];

						float c00 = c000 * (1 - dx) + c100 * dx;
						float c01 = c001 * (1 - dx) + c101 * dx;
						float c10 = c010 * (1 - dx) + c110 * dx;
						float c11 = c011 * (1 - dx) + c111 * dx;

						float c0 = c00 * (1 - dy) + c10 * dy;
						float c1 = c01 * (1 - dy) + c11 * dy;

						float c = c0 * (1 - dz) + c1 * dz;

						dst_data[z * ny * nx + y * nx + x] = c;
					}
				}
			}
		}
	}
	else {
		if (bits == 1)
		{
			uint8* data = (uint8*)m_src->data;
			uint8* dst_data = (uint8*)m_dst->data;
			for (int z = m_zst; z < m_zed; z++)
			{
				for (int y = 0; y < ny; y++)
				{
					for (int x = 0; x < nx; x++)
					{
						double dx = x * scx;
						double dy = y * scy;
						double dz = z * scz;

						size_t ix = (size_t)round(dx);
						size_t iy = (size_t)round(dy);
						size_t iz = (size_t)round(dz);

						size_t id = iz * ssize[1] * ssize[0] + iy * ssize[0] + ix;

						dst_data[z * ny * nx + y * nx + x] = (uint8)data[id];
					}
				}
			}
		}
		else if (bits == 2)
		{
			uint16* data = (uint16*)m_src->data;
			uint16* dst_data = (uint16*)m_dst->data;
			for (int z = m_zst; z < m_zed; z++)
			{
				for (int y = 0; y < ny; y++)
				{
					for (int x = 0; x < nx; x++)
					{
						double dx = x * scx;
						double dy = y * scy;
						double dz = z * scz;

						size_t ix = (size_t)round(dx);
						size_t iy = (size_t)round(dy);
						size_t iz = (size_t)round(dz);

						size_t id = iz * ssize[1] * ssize[0] + iy * ssize[0] + ix;

						dst_data[z * ny * nx + y * nx + x] = (uint16)data[id];
					}
				}
			}
		}
		else if (bits == 4)
		{
			float* data = (float*)m_src->data;
			float* dst_data = (float*)m_dst->data;
			for (int z = m_zst; z < m_zed; z++)
			{
				for (int y = 0; y < ny; y++)
				{
					for (int x = 0; x < nx; x++)
					{
						double dx = x * scx;
						double dy = y * scy;
						double dz = z * scz;

						size_t ix = (size_t)round(dx);
						size_t iy = (size_t)round(dy);
						size_t iz = (size_t)round(dz);

						size_t id = iz * ssize[1] * ssize[0] + iy * ssize[0] + ix;

						dst_data[z * ny * nx + y * nx + x] = data[id];
					}
				}
			}
		}
	}
	return (wxThread::ExitCode)0;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
MeshData::MeshData() :
m_data(0),
	m_mr(0),
	m_center(0.0, 0.0, 0.0),
	m_disp(true),
	m_draw_bounds(false),
	m_light(true),
	m_mat_amb(0.3, 0.3, 0.3),
	m_mat_diff(1.0, 0.0, 0.0),
	m_mat_spec(0.2, 0.2, 0.2),
	m_mat_shine(30.0),
	m_mat_alpha(1.0),
	m_shadow(true),
	m_shadow_darkness(0.6),
	m_enable_limit(false),
	m_limit(50)
{
	type = 3;//mesh

	m_trans[0] = 0.0;
	m_trans[1] = 0.0;
	m_trans[2] = 0.0;
	m_rot[0] = 0.0;
	m_rot[1] = 0.0;
	m_rot[2] = 0.0;
	m_scale[0] = 1.0;
	m_scale[1] = 1.0;
	m_scale[2] = 1.0;

	double hue, sat, val;
	hue = double(rand()%360);
	sat = 1.0;
	val = 1.0;
	Color color(HSVColor(hue, sat, val));
	m_mat_diff = color;

	m_legend = false;

	m_swc = false;
	m_r_scale = 1.0;
	m_def_r = 0.25;
	m_subdiv = 0;
	m_swc_reader = NULL;
    m_ply_reader = NULL;
	m_clip_dist_x = 1;
	m_clip_dist_y = 1;
	m_clip_dist_z = 1;
    m_extra_vertex_data = NULL;
    m_anno = nullptr;
    m_show_labels = false;
}

MeshData::~MeshData()
{
	if (m_mr)
		delete m_mr;
	if (m_data)
		glmDelete(m_data);
	if (m_swc_reader)
		delete m_swc_reader;
    if (m_extra_vertex_data)
        delete [] m_extra_vertex_data;
    if (m_anno)
        delete m_anno;
}

int MeshData::Load(GLMmodel* mesh)
{
	if (!mesh) return 0;

	m_data_path = "";
	m_name = "New Mesh";

	if (m_data)
		delete m_data;
	m_data = mesh;

	if (!m_data->normals)
	{
		if (!m_data->facetnorms)
			glmFacetNormals(m_data);
		glmVertexNormals(m_data, 89.0);
	}

	if (!m_data->materials)
	{
		m_data->materials = new GLMmaterial;
		m_data->nummaterials = 1;
	}

	/* set the default material */
	m_data->materials[0].name = NULL;
	m_data->materials[0].ambient[0] = m_mat_amb.r();
	m_data->materials[0].ambient[1] = m_mat_amb.g();
	m_data->materials[0].ambient[2] = m_mat_amb.b();
	m_data->materials[0].ambient[3] = m_mat_alpha;
	m_data->materials[0].diffuse[0] = m_mat_diff.r();
	m_data->materials[0].diffuse[1] = m_mat_diff.g();
	m_data->materials[0].diffuse[2] = m_mat_diff.b();
	m_data->materials[0].diffuse[3] = m_mat_alpha;
	m_data->materials[0].specular[0] = m_mat_spec.r();
	m_data->materials[0].specular[1] = m_mat_spec.g();
	m_data->materials[0].specular[2] = m_mat_spec.b();
	m_data->materials[0].specular[3] = m_mat_alpha;
	m_data->materials[0].shininess = m_mat_shine;
	m_data->materials[0].emmissive[0] = 0.0;
	m_data->materials[0].emmissive[1] = 0.0;
	m_data->materials[0].emmissive[2] = 0.0;
	m_data->materials[0].emmissive[3] = 0.0;
	m_data->materials[0].havetexture = false;
	m_data->materials[0].textureID = 0;

	//bounds
	float fbounds[6];
	glmBoundingBox(m_data, fbounds);
	BBox bounds;
	Point pmin(fbounds[0], fbounds[2], fbounds[4]);
	Point pmax(fbounds[1], fbounds[3], fbounds[5]);
	bounds.extend(pmin);
	bounds.extend(pmax);
	m_bounds = bounds;
	m_center = Point((m_bounds.min().x()+m_bounds.max().x())*0.5,
		(m_bounds.min().y()+m_bounds.max().y())*0.5,
		(m_bounds.min().z()+m_bounds.max().z())*0.5);

	if (m_mr)
		delete m_mr;
	m_mr = new FLIVR::MeshRenderer(m_data);

	return 1;
}

int MeshData::Load(wxString &filename)
{
	m_data_path = filename;
	m_name = m_data_path.Mid(m_data_path.Find(GETSLASH(), true)+1);
	wxString suffix = m_data_path.Mid(m_data_path.Find('.', true)).MakeLower();

	if (m_data)
		glmDelete(m_data);

	if (suffix == ".obj")
	{
		string str_fn = filename.ToStdString();
		bool no_fail = true;
		m_data = glmReadOBJ(str_fn.c_str(),&no_fail);
		while (!no_fail) {
			wxMessageDialog *dial = new wxMessageDialog(NULL, 
				wxT("A part of the OBJ file failed to load. Would you like to try re-loading?"), 
				wxT("OBJ Load Failure"), 
				wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
			if (dial->ShowModal() == wxID_YES) {
				if (m_data)
					delete m_data;
				m_data = glmReadOBJ(str_fn.c_str(),&no_fail);
			} else break;
		}
	}
	else if (suffix == ".swc")
	{
		if (!m_swc_reader) m_swc_reader = new SWCReader();
		wstring wstr = m_data_path.ToStdWstring();
		m_swc_reader->SetFile(wstr);
		m_swc_reader->Preprocess();
		m_data = m_swc_reader->GenerateSolidModel(m_def_r, m_r_scale, m_subdiv);
		m_swc = true;
	}
    else if (suffix == ".ply")
    {
        if (!m_ply_reader) m_ply_reader = new PLYReader();
        wstring wstr = m_data_path.ToStdWstring();
        m_ply_reader->SetFile(wstr);
        m_data = m_ply_reader->GetSolidModel();
    }

	if (!m_data)
		return 0;

    if (!m_data->facetnorms && m_data->numtriangles)
        glmFacetNormals(m_data);
    /*
	if (!m_data->normals && m_data->numtriangles)
	{
		if (!m_data->facetnorms)
			glmFacetNormals(m_data);
		glmVertexNormals(m_data, 89.0);
	}
    */

	if (!m_data->materials)
	{
		m_data->materials = new GLMmaterial;
		m_data->nummaterials = 1;
	}

	/* set the default material */
	m_data->materials[0].name = NULL;
	m_data->materials[0].ambient[0] = m_mat_amb.r();
	m_data->materials[0].ambient[1] = m_mat_amb.g();
	m_data->materials[0].ambient[2] = m_mat_amb.b();
	m_data->materials[0].ambient[3] = m_mat_alpha;
	m_data->materials[0].diffuse[0] = m_mat_diff.r();
	m_data->materials[0].diffuse[1] = m_mat_diff.g();
	m_data->materials[0].diffuse[2] = m_mat_diff.b();
	m_data->materials[0].diffuse[3] = m_mat_alpha;
	m_data->materials[0].specular[0] = m_mat_spec.r();
	m_data->materials[0].specular[1] = m_mat_spec.g();
	m_data->materials[0].specular[2] = m_mat_spec.b();
	m_data->materials[0].specular[3] = m_mat_alpha;
	m_data->materials[0].shininess = m_mat_shine;
	m_data->materials[0].emmissive[0] = 0.0;
	m_data->materials[0].emmissive[1] = 0.0;
	m_data->materials[0].emmissive[2] = 0.0;
	m_data->materials[0].emmissive[3] = 0.0;
	m_data->materials[0].havetexture = false;
	m_data->materials[0].textureID = 0;

	//bounds
	float fbounds[6];
	glmBoundingBox(m_data, fbounds);
	BBox bounds;
	Point pmin(fbounds[0], fbounds[2], fbounds[4]);
	Point pmax(fbounds[1], fbounds[3], fbounds[5]);
	bounds.extend(pmin);
	bounds.extend(pmax);
	m_bounds = bounds;
	m_center = Point((m_bounds.min().x()+m_bounds.max().x())*0.5,
		(m_bounds.min().y()+m_bounds.max().y())*0.5,
		(m_bounds.min().z()+m_bounds.max().z())*0.5);

	if (m_mr)
		delete m_mr;
	m_mr = new FLIVR::MeshRenderer(m_data);
    
    if (m_swc_reader)
    {
        SetExtraVertexData(m_swc_reader->GetModelExtraData());
        
        int group_num = m_swc_reader->GetGroupNum();
        for (int i = 0; i < group_num; i++)
        {
            PutROINode(m_swc_reader->GetGroupName(i), i);
        }
        InitROIGroup();
    }

	return 1;
}

MeshData* MeshData::DeepCopy(MeshData &copy, bool use_default_settings, DataManager *d_manager)
{
	if (!d_manager || !copy.m_data || !copy.m_mr) return NULL;

	MeshData* md = new MeshData();
	md->m_data_path = copy.m_data_path;
	md->m_name = copy.m_name;
	
	GLMmodel* mi = copy.m_data;
	GLMmodel* model = (GLMmodel*)malloc(sizeof(GLMmodel));
	model = (GLMmodel*)malloc(sizeof(GLMmodel));
	model->pathname    = STRDUP(mi->pathname);
	model->mtllibname  = STRDUP(mi->mtllibname);
	model->numvertices = mi->numvertices;
	model->vertices =  NULL;
	if (model->numvertices)
	{
		model->vertices = (float*)malloc(sizeof(float) * 3 * (model->numvertices + 1));
		memcpy(model->vertices, mi->vertices, sizeof(float) * 3 * (model->numvertices + 1));
        if (copy.m_extra_vertex_data)
        {
            float* copydata = new float[model->numvertices + 1];
            memcpy(copydata, copy.m_extra_vertex_data, sizeof(float) * (model->numvertices + 1));
            md->SetExtraVertexData(copydata);
        }
	}
	model->numnormals = mi->numnormals;
	model->normals = NULL;
	if (model->numnormals)
	{
		model->normals = (float*)malloc(sizeof(float) * 3 * (model->numnormals + 1));
		memcpy(model->normals, mi->normals, sizeof(float) * 3 * (model->numnormals + 1));
	}
	model->numtexcoords = mi->numtexcoords;
	model->texcoords = NULL;
	if (model->numtexcoords)
	{
		model->texcoords = (float*)malloc(sizeof(float) * 2 * (model->numtexcoords + 1));
		memcpy(model->texcoords, mi->texcoords, sizeof(float) * 2 * (model->numtexcoords + 1));
	}
	model->numfacetnorms = mi->numfacetnorms;
	model->facetnorms = NULL;
	if (model->numfacetnorms)
	{
		model->facetnorms = (float*)malloc(sizeof(float) * 3 * (model->numfacetnorms + 1));
		memcpy(model->facetnorms, mi->facetnorms, sizeof(float) * 3 * (model->numfacetnorms + 1));
	}
	model->numtriangles = mi->numtriangles;
	model->triangles = NULL;
	if (model->numtriangles)
	{
		model->triangles = (GLMtriangle*)malloc(sizeof(GLMtriangle) * model->numtriangles);
		memcpy(model->triangles, mi->triangles, sizeof(GLMtriangle) * model->numtriangles);
	}
	model->numlines = mi->numlines;
	model->lines = NULL;
	if (model->numlines)
	{
		model->lines = (GLMline*)malloc(sizeof(GLMline) * model->numlines);
		memcpy(model->lines, mi->lines, sizeof(GLMline) * model->numlines);
	}
	model->nummaterials = mi->nummaterials;
	model->materials = NULL;
	if (model->nummaterials)
	{
		model->materials = (GLMmaterial*)malloc(sizeof(GLMmaterial) * model->nummaterials);
		for (int i = 0; i < model->nummaterials; i++)
		{
			model->materials[i].name = STRDUP(mi->materials[i].name);
			model->materials[i].shininess = mi->materials[i].shininess;
			model->materials[i].diffuse[0] = mi->materials[i].diffuse[0];
			model->materials[i].diffuse[1] = mi->materials[i].diffuse[1];
			model->materials[i].diffuse[2] = mi->materials[i].diffuse[2];
			model->materials[i].diffuse[3] = mi->materials[i].diffuse[3];
			model->materials[i].ambient[0] = mi->materials[i].ambient[0];
			model->materials[i].ambient[1] = mi->materials[i].ambient[1];
			model->materials[i].ambient[2] = mi->materials[i].ambient[2];
			model->materials[i].ambient[3] = mi->materials[i].ambient[3];
			model->materials[i].specular[0] = mi->materials[i].specular[0];
			model->materials[i].specular[1] = mi->materials[i].specular[1];
			model->materials[i].specular[2] = mi->materials[i].specular[2];
			model->materials[i].specular[3] = mi->materials[i].specular[3];
			model->materials[i].emmissive[0] = mi->materials[i].emmissive[0];
			model->materials[i].emmissive[1] = mi->materials[i].emmissive[1];
			model->materials[i].emmissive[2] = mi->materials[i].emmissive[2];
			model->materials[i].emmissive[3] = mi->materials[i].emmissive[3];
			model->materials[i].havetexture = mi->materials[i].havetexture;
			model->materials[i].textureID = mi->materials[i].textureID;
		}
	}
	model->numgroups = mi->numgroups;
	GLMgroup* group = mi->groups;
	GLMgroup* og = NULL;
	GLMgroup* tmpg = NULL;
	model->groups = NULL;
	while(group)
	{
		og = (GLMgroup*)malloc(sizeof(GLMgroup));
		og->name = STRDUP(group->name);
		og->material = group->material;
		og->numtriangles = group->numtriangles;
		og->next = NULL;
		if (group->numtriangles)
		{
			og->triangles = (unsigned int*)malloc(sizeof(unsigned int) * group->numtriangles);
			memcpy(og->triangles, group->triangles, sizeof(unsigned int) * group->numtriangles);
		}
		if (!model->groups) 
		{
			model->groups = og;
			tmpg = og;
		}
		else 
		{
			tmpg->next = og;
			tmpg = tmpg->next;
		}
		group = group->next;
	}
	model->position[0]   = mi->position[0];
	model->position[1]   = mi->position[1];
	model->position[2]   = mi->position[2];
	model->hastexture = mi->hastexture;
	md->m_data = model;

	md->m_swc = copy.m_swc;
	if (copy.m_swc)
	{
		md->m_swc_reader = new SWCReader();
		SWCReader::DeepCopy(copy.m_swc_reader, md->m_swc_reader);
	}

	if (use_default_settings)
	{
		/* set the default material */
		md->m_data->materials[0].name = NULL;
		md->m_data->materials[0].ambient[0] = md->m_mat_amb.r();
		md->m_data->materials[0].ambient[1] = md->m_mat_amb.g();
		md->m_data->materials[0].ambient[2] = md->m_mat_amb.b();
		md->m_data->materials[0].ambient[3] = md->m_mat_alpha;
		md->m_data->materials[0].diffuse[0] = md->m_mat_diff.r();
		md->m_data->materials[0].diffuse[1] = md->m_mat_diff.g();
		md->m_data->materials[0].diffuse[2] = md->m_mat_diff.b();
		md->m_data->materials[0].diffuse[3] = md->m_mat_alpha;
		md->m_data->materials[0].specular[0] = md->m_mat_spec.r();
		md->m_data->materials[0].specular[1] = md->m_mat_spec.g();
		md->m_data->materials[0].specular[2] = md->m_mat_spec.b();
		md->m_data->materials[0].specular[3] = md->m_mat_alpha;
		md->m_data->materials[0].shininess = md->m_mat_shine;
		md->m_data->materials[0].emmissive[0] = 0.0;
		md->m_data->materials[0].emmissive[1] = 0.0;
		md->m_data->materials[0].emmissive[2] = 0.0;
		md->m_data->materials[0].emmissive[3] = 0.0;
		md->m_data->materials[0].havetexture = false;
		md->m_data->materials[0].textureID = 0;

		//bounds
		float fbounds[6];
		glmBoundingBox(md->m_data, fbounds);
		BBox bounds;
		Point pmin(fbounds[0], fbounds[2], fbounds[4]);
		Point pmax(fbounds[1], fbounds[3], fbounds[5]);
		bounds.extend(pmin);
		bounds.extend(pmax);
		md->m_bounds = bounds;
		md->m_center = Point((md->m_bounds.min().x()+md->m_bounds.max().x())*0.5,
			(md->m_bounds.min().y()+md->m_bounds.max().y())*0.5,
			(md->m_bounds.min().z()+md->m_bounds.max().z())*0.5);
	}
	else
	{
		md->m_mat_alpha = copy.m_mat_alpha;
		md->m_mat_amb = copy.m_mat_amb;
		md->m_mat_diff = copy.m_mat_diff;
		md->m_mat_spec = copy.m_mat_spec;
		md->m_mat_shine = copy.m_mat_shine;

		md->m_bounds = copy.m_bounds;
		md->m_center = copy.m_center;

		md->m_disp = copy.m_disp;
		md->m_draw_bounds = copy.m_draw_bounds;
		md->m_light = copy.m_light;
		md->m_fog = copy.m_fog;
		md->m_shadow = copy.m_shadow;
		md->m_shadow_darkness = copy.m_shadow_darkness;
		md->m_enable_limit = copy.m_enable_limit;
		md->m_limit = copy.m_limit;
		md->m_legend = copy.m_legend;
		md->m_r_scale = copy.m_r_scale;
		md->m_def_r = copy.m_def_r;
		md->m_subdiv = copy.m_subdiv;

		memcpy(md->m_trans, copy.m_trans, sizeof(double)*3);
		memcpy(md->m_rot, copy.m_rot, sizeof(double)*3);
		memcpy(md->m_scale, copy.m_scale, sizeof(double)*3);

		md->m_info = copy.m_info;

		md->m_gamma = copy.m_gamma;
		md->m_brightness = copy.m_brightness;
		md->m_hdr = copy.m_hdr;
		md->m_sync_r = copy.m_sync_r;
		md->m_sync_g = copy.m_sync_g;
		md->m_sync_b = copy.m_sync_b;
	}

	md->m_clip_dist_x = copy.m_clip_dist_x;
	md->m_clip_dist_y = copy.m_clip_dist_y;
	md->m_clip_dist_z = copy.m_clip_dist_z;
	
	md->m_mr = new FLIVR::MeshRenderer(md->m_data);
	md->m_mr->set_alpha(copy.m_mr->get_alpha());
	md->m_mr->set_depth_peel(copy.m_mr->get_depth_peel());
	md->m_mr->set_lighting(copy.m_mr->get_lighting());
	md->m_mr->set_limit(copy.m_mr->get_limit());
	md->m_mr->set_planes(copy.m_mr->get_planes());
	md->m_mr->set_bounds(md->GetBounds());
	
	return md;
}

void MeshData::Save(wxString& filename)
{
	if (m_data)
	{
		char* str = new char[filename.length()+1];
		for (int i=0; i<(int)filename.length(); i++)
			str[i] = (char)filename[i];
		str[filename.length()] = 0;
		glmWriteOBJ(m_data, str, GLM_SMOOTH);
		delete []str;
		m_data_path = filename;
	}
}

//clip distance
void MeshData::SetClipDistance(int distx, int disty, int distz)
{
	m_clip_dist_x = distx;
	m_clip_dist_y = disty;
	m_clip_dist_z = distz;
}

void MeshData::GetClipDistance(int &distx, int &disty, int &distz)
{
	distx = m_clip_dist_x;
	disty = m_clip_dist_y;
	distz = m_clip_dist_z;
}

bool MeshData::UpdateModelSWC()
{
	if (!m_swc || !m_swc_reader)
		return false;

	if (m_data)
		glmDelete(m_data);

	m_data = m_swc_reader->GenerateSolidModel(m_def_r, m_r_scale, m_subdiv);

	if (!m_data)
		return false;

	if (!m_data->normals && m_data->numtriangles)
	{
		if (!m_data->facetnorms)
			glmFacetNormals(m_data);
		glmVertexNormals(m_data, 89.0);
	}

	if (!m_data->materials)
	{
		m_data->materials = new GLMmaterial;
		m_data->nummaterials = 1;
	}

	/* set the default material */
	m_data->materials[0].name = NULL;
	m_data->materials[0].ambient[0] = m_mat_amb.r();
	m_data->materials[0].ambient[1] = m_mat_amb.g();
	m_data->materials[0].ambient[2] = m_mat_amb.b();
	m_data->materials[0].ambient[3] = m_mat_alpha;
	m_data->materials[0].diffuse[0] = m_mat_diff.r();
	m_data->materials[0].diffuse[1] = m_mat_diff.g();
	m_data->materials[0].diffuse[2] = m_mat_diff.b();
	m_data->materials[0].diffuse[3] = m_mat_alpha;
	m_data->materials[0].specular[0] = m_mat_spec.r();
	m_data->materials[0].specular[1] = m_mat_spec.g();
	m_data->materials[0].specular[2] = m_mat_spec.b();
	m_data->materials[0].specular[3] = m_mat_alpha;
	m_data->materials[0].shininess = m_mat_shine;
	m_data->materials[0].emmissive[0] = 0.0;
	m_data->materials[0].emmissive[1] = 0.0;
	m_data->materials[0].emmissive[2] = 0.0;
	m_data->materials[0].emmissive[3] = 0.0;
	m_data->materials[0].havetexture = false;
	m_data->materials[0].textureID = 0;

	//bounds
	float fbounds[6];
	glmBoundingBox(m_data, fbounds);
	BBox bounds;
	Point pmin(fbounds[0], fbounds[2], fbounds[4]);
	Point pmax(fbounds[1], fbounds[3], fbounds[5]);
	bounds.extend(pmin);
	bounds.extend(pmax);
	m_bounds.extend(bounds);
	m_center = Point((m_bounds.min().x()+m_bounds.max().x())*0.5,
		(m_bounds.min().y()+m_bounds.max().y())*0.5,
		(m_bounds.min().z()+m_bounds.max().z())*0.5);

    if (m_mr)
        m_mr->set_model(m_data);
/*
	MeshRenderer *tmp = 0;
	if (m_mr)
		tmp = m_mr;
	m_mr = new FLIVR::MeshRenderer(m_data);

	m_mr->set_alpha(m_mat_alpha);
	m_mr->set_bounds(m_bounds);
	if (tmp)
	{
		m_mr->set_planes(tmp->get_planes());
		m_mr->set_depth_peel(tmp->get_depth_peel());
		m_mr->set_lighting(tmp->get_lighting());
		m_mr->set_limit(tmp->get_limit());
		delete tmp;
	}
*/
	return true;
}

void MeshData::SetBounds(BBox b)
{
	m_bounds = b;
	m_center = Point((m_bounds.min().x()+m_bounds.max().x())*0.5,
					 (m_bounds.min().y()+m_bounds.max().y())*0.5,
					 (m_bounds.min().z()+m_bounds.max().z())*0.5);
}

//MR
MeshRenderer* MeshData::GetMR()
{
	return m_mr;
}

void MeshData::SetMatrices(glm::mat4 &mv_mat, glm::mat4 &proj_mat)
{
	if (m_mr)
	{
		glm::mat4 mv_temp;
		mv_temp = glm::translate(
			mv_mat, glm::vec3(
		m_trans[0]+m_center.x(),
		m_trans[1]+m_center.y(),
		m_trans[2]+m_center.z()));
		mv_temp = glm::rotate(
			mv_temp, float(m_rot[0]),
			glm::vec3(1.0, 0.0, 0.0));
		mv_temp = glm::rotate(
			mv_temp, float(m_rot[1]),
			glm::vec3(0.0, 1.0, 0.0));
		mv_temp = glm::rotate(
			mv_temp, float(m_rot[2]),
			glm::vec3(0.0, 0.0, 1.0));
		mv_temp = glm::scale(mv_temp,
			glm::vec3(float(m_scale[0]), float(m_scale[1]), float(m_scale[2])));
		mv_temp = glm::translate(mv_temp,
			glm::vec3(-m_center.x(), -m_center.y(), -m_center.z()));

		m_mr->SetMatrices(mv_temp, proj_mat);
	}
}

void MeshData::Draw(const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf, int peel)
{
	if (!m_mr)
		return;

	m_mr->set_depth_peel(peel);
	m_mr->set_bounds(m_bounds);
	m_mr->draw(framebuf, clear_framebuf);
	if (m_draw_bounds)
		DrawBounds(framebuf, false);
}

void MeshData::DrawBounds(const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf)
{
	if (!m_mr)
		return;

	m_mr->draw_wireframe(framebuf, clear_framebuf);
}

void MeshData::DrawInt(unsigned int name, const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf, VkRect2D scissor, bool segment)
{
	if (!m_mr)
		return;

	m_mr->draw_integer(name, framebuf, clear_framebuf, scissor, segment);
}

//lighting
void MeshData::SetLighting(bool bVal)
{
	m_light = bVal;
	if (m_mr) m_mr->set_lighting(m_light);
}

bool MeshData::GetLighting()
{
	return m_light;
}

//fog
void MeshData::SetFog(bool bVal,
	double fog_intensity, double fog_start, double fog_end)
{
	m_fog = bVal;
	if (m_mr) m_mr->set_fog(m_fog, fog_intensity, fog_start, fog_end);
}

bool MeshData::GetFog()
{
	return m_fog;
}

void MeshData::SetMaterial(Color& amb, Color& diff, Color& spec, 
	double shine, double alpha)
{
	m_mat_amb = amb;
	m_mat_diff = diff;
	m_mat_spec = spec;
	m_mat_shine = shine;
	m_mat_alpha = alpha;

	if (m_data && m_data->materials)
	{
		for (int i=0; i<(int)m_data->nummaterials; i++)
		{
			if (i==0)
			{
				m_data->materials[i].ambient[0] = m_mat_amb.r();
				m_data->materials[i].ambient[1] = m_mat_amb.g();
				m_data->materials[i].ambient[2] = m_mat_amb.b();
				m_data->materials[i].diffuse[0] = m_mat_diff.r();
				m_data->materials[i].diffuse[1] = m_mat_diff.g();
				m_data->materials[i].diffuse[2] = m_mat_diff.b();
				m_data->materials[i].specular[0] = m_mat_spec.r();
				m_data->materials[i].specular[1] = m_mat_spec.g();
				m_data->materials[i].specular[2] = m_mat_spec.b();
				m_data->materials[i].shininess = m_mat_shine;
			}
			m_data->materials[i].specular[3] = m_mat_alpha;
			m_data->materials[i].ambient[3] = m_mat_alpha;
			m_data->materials[i].diffuse[3] = m_mat_alpha;
		}
	}
}

void MeshData::SetColor(Color &color, int type)
{
	switch (type)
	{
	case MESH_COLOR_AMB:
		m_mat_amb = color;
		if (m_data && m_data->materials)
		{
			m_data->materials[0].ambient[0] = m_mat_amb.r();
			m_data->materials[0].ambient[1] = m_mat_amb.g();
			m_data->materials[0].ambient[2] = m_mat_amb.b();
		}
		break;
	case MESH_COLOR_DIFF:
		m_mat_diff = color;
		if (m_data && m_data->materials)
		{
			m_data->materials[0].diffuse[0] = m_mat_diff.r();
			m_data->materials[0].diffuse[1] = m_mat_diff.g();
			m_data->materials[0].diffuse[2] = m_mat_diff.b();
		}
		break;
	case MESH_COLOR_SPEC:
		m_mat_spec = color;
		if (m_data && m_data->materials)
		{
			m_data->materials[0].specular[0] = m_mat_spec.r();
			m_data->materials[0].specular[1] = m_mat_spec.g();
			m_data->materials[0].specular[2] = m_mat_spec.b();
		}
		break;
	}
}

void MeshData::SetFloat(double &value, int type)
{
	switch (type)
	{
	case MESH_FLOAT_SHN:
		m_mat_shine = value;
		if (m_data && m_data->materials)
		{
			m_data->materials[0].shininess = m_mat_shine;
		}
		break;
	case MESH_FLOAT_ALPHA:
		m_mat_alpha = value;
		if (m_data && m_data->materials)
		{
			for (int i=0; i<(int)m_data->nummaterials; i++)
			{
				m_data->materials[i].ambient[3] = m_mat_alpha;
				m_data->materials[i].diffuse[3] = m_mat_alpha;
				m_data->materials[i].specular[3] = m_mat_alpha;
			}
		}
		if (m_mr) m_mr->set_alpha(value);
		break;
	}

}

void MeshData::GetMaterial(Color& amb, Color& diff, Color& spec,
	double& shine, double& alpha)
{
	amb = m_mat_amb;
	diff = m_mat_diff;
	spec = m_mat_spec;
	shine = m_mat_shine;
	alpha = m_mat_alpha;
}

bool MeshData::IsTransp()
{
	if (m_mat_alpha>=1.0)
		return false;
	else
		return true;
}

//shadow
void MeshData::SetShadow(bool bVal)
{
	m_shadow= bVal;
}

bool MeshData::GetShadow()
{
	return m_shadow;
}

void MeshData::SetShadowParams(double val)
{
	m_shadow_darkness = val;
}

void MeshData::GetShadowParams(double &val)
{
	val = m_shadow_darkness;
}

wxString MeshData::GetPath()
{
	return m_data_path;
}

BBox MeshData::GetBounds()
{
	BBox bounds;
	Point p[8];
	p[0] = Point(m_bounds.min().x(), m_bounds.min().y(), m_bounds.min().z());
	p[1] = Point(m_bounds.min().x(), m_bounds.min().y(), m_bounds.max().z());
	p[2] = Point(m_bounds.min().x(), m_bounds.max().y(), m_bounds.min().z());
	p[3] = Point(m_bounds.min().x(), m_bounds.max().y(), m_bounds.max().z());
	p[4] = Point(m_bounds.max().x(), m_bounds.min().y(), m_bounds.min().z());
	p[5] = Point(m_bounds.max().x(), m_bounds.min().y(), m_bounds.max().z());
	p[6] = Point(m_bounds.max().x(), m_bounds.max().y(), m_bounds.min().z());
	p[7] = Point(m_bounds.max().x(), m_bounds.max().y(), m_bounds.max().z());
	double s, c;
	Point temp;
	for (int i=0 ; i<8 ; i++)
	{
		p[i] = Point(p[i].x()*m_scale[0], p[i].y()*m_scale[1], p[i].z()*m_scale[2]);
		s = sin(d2r(m_rot[2]));
		c = cos(d2r(m_rot[2]));
		temp = Point(c*p[i].x()-s*p[i].y(), s*p[i].x()+c*p[i].y(), p[i].z());
		p[i] = temp;
		s = sin(d2r(m_rot[1]));
		c = cos(d2r(m_rot[1]));
		temp = Point(c*p[i].x()+s*p[i].z(), p[i].y(), -s*p[i].x()+c*p[i].z());
		p[i] = temp;
		s = sin(d2r(m_rot[0]));
		c = cos(d2r(m_rot[0]));
		temp = Point(p[i].x(), c*p[i].y()-s*p[i].z(), s*p[i].y()+c*p[i].z());
		p[i] = Point(temp.x()+m_trans[0], temp.y()+m_trans[1], temp.z()+m_trans[2]);
		bounds.extend(p[i]);
	}
	return bounds;
}

GLMmodel* MeshData::GetMesh()
{
	return m_data;
}

void MeshData::SetDisp(bool disp)
{
	m_disp = disp;
}

void MeshData::ToggleDisp()
{
	m_disp = !m_disp;
}

bool MeshData::GetDisp()
{
	return m_disp;
}

void MeshData::SetDrawBounds(bool draw)
{
	m_draw_bounds = draw;
}

void MeshData::ToggleDrawBounds()
{
	m_draw_bounds = !m_draw_bounds;
}

bool MeshData::GetDrawBounds()
{
	return m_draw_bounds;
}

void MeshData::SetTranslation(double x, double y, double z)
{
	m_trans[0] = x;
	m_trans[1] = y;
	m_trans[2] = z;
}

void MeshData::GetTranslation(double &x, double &y, double &z)
{
	x = m_trans[0];
	y = m_trans[1];
	z = m_trans[2];
}

void MeshData::SetRotation(double x, double y, double z)
{
	m_rot[0] = x;
	m_rot[1] = y;
	m_rot[2] = z;
}

void MeshData::GetRotation(double &x, double &y, double &z)
{
	x = m_rot[0];
	y = m_rot[1];
	z = m_rot[2];
}

void MeshData::SetScaling(double x, double y, double z)
{
	m_scale[0] = x;
	m_scale[1] = y;
	m_scale[2] = z;
}

void MeshData::GetScaling(double &x, double &y, double &z)
{
	x = m_scale[0];
	y = m_scale[1];
	z = m_scale[2];
}

//randomize color
void MeshData::RandomizeColor()
{
	double hue = (double)rand()/(RAND_MAX) * 360.0;
	Color color(HSVColor(hue, 1.0, 1.0));
	SetColor(color, MESH_COLOR_DIFF);
    Color amb = color * 0.3;
	SetColor(amb, MESH_COLOR_AMB);
}

//shown in legend
void MeshData::SetLegend(bool val)
{
	m_legend = val;
}

bool MeshData::GetLegend()
{
	return m_legend;
}

//size limiter
void MeshData::SetLimit(bool bVal)
{
	m_enable_limit = bVal;
	if (m_enable_limit)
		m_mr->set_limit(m_limit);
	else
		m_mr->set_limit(-1);
//	m_mr->update();
}

bool MeshData::GetLimit()
{
	return m_enable_limit;
}

void MeshData::SetLimitNumer(int val)
{
	m_limit = val;
	if (m_enable_limit)
	{
		m_mr->set_limit(val);
//		m_mr->update();
	}
}

int MeshData::GetLimitNumber()
{
	return m_limit;
}

void MeshData::RecalcBounds()
{
	float fbounds[6];
	glmBoundingBox(m_data, fbounds);
	BBox bounds;
	Point pmin(fbounds[0], fbounds[2], fbounds[4]);
	Point pmax(fbounds[1], fbounds[3], fbounds[5]);
	bounds.extend(pmin);
	bounds.extend(pmax);
	m_bounds = bounds;
	m_center = Point((m_bounds.min().x() + m_bounds.max().x()) * 0.5,
		(m_bounds.min().y() + m_bounds.max().y()) * 0.5,
		(m_bounds.min().z() + m_bounds.max().z()) * 0.5);
}

//test if point is inside the clipping planes
bool MeshData::InsideClippingPlanes(Point pos)
{
    if (!m_mr)
        return true;
    
    vector<Plane*> *planes = m_mr->get_planes();
    if (!planes)
        return true;
    if (planes->size() != 6)
        return true;
    
    BBox dbox = m_mr->get_bounds();
    
    pos.x( (pos.x() - dbox.min().x()) / (dbox.max().x() - dbox.min().x()) );
    pos.y( (pos.y() - dbox.min().y()) / (dbox.max().y() - dbox.min().y()) );
    pos.z( (pos.z() + dbox.max().z()) / (dbox.max().z() - dbox.min().z()) );

    Plane* plane = 0;
    for (int i=0; i<6; i++)
    {
        plane = (*planes)[i];
        if (!plane)
            continue;
        if (plane->eval_point(pos) < 0)
            return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
AText::AText()
{
}

AText::AText(const string &str, const Point &pos)
{
	m_txt = str;
	m_pos = pos;
}

AText::~AText()
{
}

string AText::GetText()
{
	return m_txt;
}

Point AText::GetPos()
{
	return m_pos;
}

void AText::SetText(string str)
{
	m_txt = str;
}

void AText::SetPos(Point pos)
{
	m_pos = pos;
}

void AText::SetInfo(string str)
{
	m_info = str;
}

int Annotations::m_num = 0;

Annotations::Annotations()
{
	type = 4;//annotations
	m_num++;
	m_name = wxString::Format("Antn_%d", m_num);
	m_tform = 0;
	m_vd = 0;
	m_disp = true;
	m_memo_ro = false;
	m_label = NULL;
    m_md = NULL;
    m_alpha = 1.0;
    m_max_score = 65535.0;
}

Annotations::~Annotations()
{
	Clear();
}

int Annotations::GetTextNum()
{
	return (int)m_alist.size();
}

string Annotations::GetTextText(int index)
{
	if (index>=0 && index<(int)m_alist.size())
	{
		AText* atext = m_alist[index];
		if (atext)
			return atext->m_txt;
	}
	return "";
}

Point Annotations::GetTextPos(int index)
{
	if (index>=0 && index<(int)m_alist.size())
	{
		AText* atext = m_alist[index];
		if (atext)
			return atext->m_pos;
	}
	return Point(Vector(0.0));
}

Point Annotations::GetTextTransformedPos(int index)
{
	if (index>=0 && index<(int)m_alist.size())
	{
		AText* atext = m_alist[index];
		if (atext && m_tform)
			return m_tform->transform(atext->m_pos);
	}
	return Point(Vector(0.0));
}

string Annotations::GetTextInfo(int index)
{
	if (index>=0 && index<(int)m_alist.size())
	{
		AText* atext = m_alist[index];
		if(atext)
			return atext->m_info;
	}
	return "";
}

void Annotations::AddText(std::string str, Point pos, std::string info)
{
	AText* atext = new AText(str, pos);
	atext->SetInfo(info);
	m_alist.push_back(atext);
}

void Annotations::SetTransform(Transform *tform)
{
	m_tform = tform;
}

void Annotations::SetVolume(VolumeData *vd)
{
	m_vd = vd;
	if (m_vd)
		m_name += "_FROM_" + m_vd->GetName();
}

VolumeData* Annotations::GetVolume()
{
	return m_vd;
}

void Annotations::SetMesh(MeshData *md)
{
    m_md = md;
}

MeshData* Annotations::GetMesh()
{
    return m_md;
}

void Annotations::Clear()
{
	for (int i=0; i<(int)m_alist.size(); i++)
	{
		AText* atext = m_alist[i];
		if (atext)
			delete atext;
	}
	m_alist.clear();
    
    //if (m_md)
    //    delete m_md;
}

//memo
void Annotations::SetMemo(string &memo)
{
	m_memo = memo;
}

string& Annotations::GetMemo()
{
	return m_memo;
}

void Annotations::SetMemoRO(bool ro)
{
	m_memo_ro = ro;
}

bool Annotations::GetMemoRO()
{
	return m_memo_ro;
}

//save/load
wxString Annotations::GetPath()
{
	return m_data_path;
}

int Annotations::Load(wxString &filename, DataManager* mgr)
{
	wxFileInputStream fis(filename);
	if (!fis.Ok())
		return 0;

	wxTextInputStream tis(fis);
	wxString str;

	while (!fis.Eof())
	{
		wxString sline = tis.ReadLine();

		if (sline.SubString(0, 5) == "Name: ")
		{
			m_name = sline.SubString(6, sline.Length()-1);
		}
		else if (sline.SubString(0, 8) == "Display: ")
		{
			str = sline.SubString(9, 9);
			if (str == "0")
				m_disp = false;
			else
				m_disp = true;
		}
		else if (sline.SubString(0, 4) == "Memo:")
		{
			str = tis.ReadLine();
			while (str.SubString(0, 12) != "Memo Update: " &&
				!fis.Eof())
			{
				m_memo += str + "\n";
				str = tis.ReadLine();
			}
			if (str.SubString(13, 13) == "0")
				m_memo_ro = false;
			else
				m_memo_ro = true;
		}
		else if (sline.SubString(0, 7) == "Volume: ")
		{
			str = sline.SubString(8, sline.Length()-1);
			VolumeData* vd = mgr->GetVolumeData(str);
			if (vd)
			{
				m_vd = vd;
				m_tform = vd->GetTexture()->transform();
			}
		}
		else if (sline.SubString(0, 9) == "Transform:")
		{
			str = tis.ReadLine();
			str = tis.ReadLine();
			str = tis.ReadLine();
			str = tis.ReadLine();
		}
		else if (sline.SubString(0, 10) == "Components:")
		{
			str = tis.ReadLine();
			int tab_counter = 0;
			for (int i=0; i<(int)str.Length(); i++)
			{
				if (str[i] == '\t')
					tab_counter++;
				if (tab_counter == 4)
				{
					m_info_meaning = str.SubString(i+1, str.Length()-1);
					break;
				}
			}

			str = tis.ReadLine();
			while (!fis.Eof())
			{
				if (AText* atext = GetAText(str))
					m_alist.push_back(atext);
				str = tis.ReadLine();
			}
		}
        else if (sline.SubString(0, 5) == "Mesh: ")
        {
            str = sline.SubString(6, sline.Length()-1);
            MeshData *md = mgr->GetMeshData(str);
            if (md)
                m_md = md;
        }
        else if (sline.SubString(0, 6) == "Alpha: ")
        {
            str = sline.SubString(7, sline.Length()-1);
            str.ToDouble(&m_alpha);
        }
        else if (sline.SubString(0, 10) == "Threshold: ")
        {
            double th;
            str = sline.SubString(11, sline.Length()-1);
            str.ToDouble(&th);
            if (m_md)
                m_md->SetThreshold(th);
        }
        else if (sline.SubString(0, 9) == "MaxScore: ")
        {
            str = sline.SubString(10, sline.Length()-1);
            str.ToDouble(&m_max_score);
        }
	}

	m_data_path = filename;
	return 1;
}

void Annotations::Save(wxString &filename)
{
	wxFileOutputStream fos(filename);
	if (!fos.Ok())
		return;

	wxTextOutputStream tos(fos);

	int resx = 1;
	int resy = 1;
	int resz = 1;
	if (m_vd)
		m_vd->GetResolution(resx, resy, resz);

	tos << "Name: " << m_name << "\n";
	tos << "Display: " << m_disp << "\n";
	tos << "Memo:\n" << m_memo << "\n";
	tos << "Memo Update: " << m_memo_ro << "\n";
	if (m_vd)
	{
		tos << "Volume: " << m_vd->GetName() << "\n";
		tos << "Voxel size (X Y Z):\n";
		double spcx, spcy, spcz;
		m_vd->GetSpacings(spcx, spcy, spcz);
		tos << spcx << "\t" << spcy << "\t" << spcz << "\n";
	}


	tos << "\nComponents:\n";
	tos << "ID\tX\tY\tZ\t" << m_info_meaning << "\n\n";
	for (int i=0; i<(int)m_alist.size(); i++)
	{
		AText* atext = m_alist[i];
		if (atext)
		{
			tos << atext->m_txt << "\t";
			tos << int(atext->m_pos.x()*resx+1.0) << "\t";
			tos << int(atext->m_pos.y()*resy+1.0) << "\t";
			tos << int(atext->m_pos.z()*resz+1.0) << "\t";
			tos << atext->m_info << "\n";
		}
	}

	m_data_path = filename;

	wxString labelpath = filename.Mid(0, filename.find_last_of(wxT('.'))) + ".lbl";

	if (m_label && m_vd)
	{
		double spcx = 1.0, spcy = 1.0, spcz = 1.0; 
		m_vd->GetSpacings(spcx, spcy, spcz);
		NRRDWriter writer;
		writer.SetData(m_label);
		writer.SetSpacings(spcx, spcy, spcz);
		writer.SetCompression(true);
		writer.Save(labelpath.ToStdWstring(), 0);
	}
    
    if (m_md)
    {
        wxString mesh_path = filename.Mid(0, filename.find_last_of(wxT('.'))) + ".swc";
        tos << "Mesh: " << m_md->GetName() << "\n";
        tos << "Alpha: " << m_alpha << "\n";
        tos << "Threshold: " << m_md->GetThreshold() << "\n";
        tos << "MaxScore: " << m_max_score << "\n";
    }
}

wxString Annotations::GetInfoMeaning()
{
	return m_info_meaning;
}

void Annotations::SetInfoMeaning(wxString &str)
{
	m_info_meaning = str;
}

//test if point is inside the clipping planes
bool Annotations::InsideClippingPlanes(Point &pos)
{
	if (!m_vd)
		return true;

	vector<Plane*> *planes = m_vd->GetVR()->get_planes();
	if (!planes)
		return true;
	if (planes->size() != 6)
		return true;

	Plane* plane = 0;
	for (int i=0; i<6; i++)
	{
		plane = (*planes)[i];
		if (!plane)
			continue;
		if (plane->eval_point(pos) < 0)
			return false;
	}

	return true;
}

AText* Annotations::GetAText(wxString str)
{
	AText *atext = 0;
	wxString sID;
	wxString sX;
	wxString sY;
	wxString sZ;
	wxString sInfo;
	int tab_counter = 0;

	for (int i=0; i<(int)str.Length(); i++)
	{
		if (str[i] == '\t')
			tab_counter++;
		else
		{
			if (tab_counter == 0)
				sID += str[i];
			else if (tab_counter == 1)
				sX += str[i];
			else if (tab_counter == 2)
				sY += str[i];
			else if (tab_counter == 3)
				sZ += str[i];
			else if (tab_counter == 4)
			{
				sInfo = str.SubString(i, str.Length()-1);
				break;
			}
		}
	}
	if (tab_counter == 4)
	{
		double x, y, z;
		sX.ToDouble(&x);
		sY.ToDouble(&y);
		sZ.ToDouble(&z);
		int resx = 1;
		int resy = 1;
		int resz = 1;
		if (m_vd)
			m_vd->GetResolution(resx, resy, resz);
		x /= resx?resx:1;
		y /= resy?resy:1;
		z /= resz?resz:1;
		Point pos(x, y, z);
		atext = new AText(sID.ToStdString(), pos);
		atext->SetInfo(sInfo.ToStdString());
	}

	return atext;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
size_t callbackWriteAnnotation(char *ptr, size_t size, size_t nmemb, string *stream)
{
	int data_len = size * nmemb;
	stream->append(ptr, data_len);
	return data_len;
}

RulerBalloon::RulerBalloon(bool visibility, Point point, wxArrayString annotations)
	: m_visibility(visibility), m_point(point), m_annotations(annotations), m_annotationdb(vector<AnnotationDB>())
{
	//m_curl = curl_easy_init();
}

RulerBalloon::~RulerBalloon()
{
	extern CURLM *_g_curlm;

	for(int i = 0; i < m_curl.size(); i++)
	{
		if(m_curl[i])
		{
			curl_multi_remove_handle(_g_curlm, m_curl[i]);
			curl_easy_cleanup(m_curl[i]);
		}
	}
}

void RulerBalloon::SetAnnotationsFromDatabase(vector<AnnotationDB> ann, Point new_p, double spcx, double spcy, double spcz)
{
	if(ann == m_annotationdb && m_point.x() == new_p.x() && m_point.y() == new_p.y() && m_point.z() == new_p.z()) return;

	m_point = new_p;
	m_annotationdb = ann;
	
	if(m_curl.size() < m_annotationdb.size()) m_curl.resize(m_annotationdb.size(), NULL);
	if(m_bufs.size() < m_annotationdb.size()) m_bufs.resize(m_annotationdb.size(), string());

	for(int i = 0; i < m_annotationdb.size(); i++){
		wxString url = m_annotationdb[i].url;

		url << wxT("?annotations=");
		for(int j = 0; j < m_annotationdb[i].contents.size(); j++)
		{
			if(j != 0) url << wxT("+");
			url << m_annotationdb[i].contents[j];
		}
		url << wxT("&coordinate=");
		url << (int)(m_point.x()/spcx) << "+" << (int)(m_point.y()/spcy) << "+" << (int)(m_point.z()/spcz);
		/*
		stringstream coord;
		coord << (int)(m_point.x()/spcx) << "+" << (int)(m_point.y()/spcy) << "+" << (int)(m_point.z()/spcz);
		m_buf = coord.str() + " ";
		*/
		extern CURLM *_g_curlm;

		if(!m_curl[i]) m_curl[i] = curl_easy_init();

		curl_multi_remove_handle(_g_curlm, m_curl[i]);

		if (m_curl[i] == NULL) {
			//cerr << "curl_easy_init() failed" << endl;
			return;
		}
		curl_easy_reset(m_curl[i]);
		curl_easy_setopt(m_curl[i], CURLOPT_URL, url.ToStdString().c_str());
		curl_easy_setopt(m_curl[i], CURLOPT_TIMEOUT, 10L);
		curl_easy_setopt(m_curl[i], CURLOPT_USERAGENT, "libcurl-agent/1.0");
		curl_easy_setopt(m_curl[i], CURLOPT_WRITEFUNCTION, callbackWriteAnnotation);
		m_annotations.Clear();
		m_bufs[i] = "";
		curl_easy_setopt(m_curl[i], CURLOPT_WRITEDATA, &m_bufs[i]);
		curl_easy_setopt(m_curl[i], CURLOPT_SSL_VERIFYPEER, 0);

		curl_multi_add_handle(_g_curlm, m_curl[i]);
		int handle_count;
		curl_multi_perform(_g_curlm, &handle_count);

	}

	//curl_easy_perform(m_curl);

/*
	char a[]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

	m_annotations.Clear();
	int lines = 1 + rand() % 5;
	for(int i = 0; i < lines; i++)
	{
		stringstream ss;
		int char_num = rand() % 20;
		for(int j = 0; j < char_num; j++)
		{
			ss << a[rand() % 62];
		}
		m_annotations.Add(wxString(ss.str()));
	}
*/
}

wxArrayString RulerBalloon::GetAnnotations()
{
    wxArrayString annotations;
    
    int stline = 0;
    for(int a = 0; a < m_bufs.size(); a++)
    {
        stringstream ss(m_bufs[a]);
        string temp, elem;
        int i = 0;
        while (getline(ss, temp))
        {
            std::stringstream ls(temp);
            int j = stline;
            while (getline(ls, elem, '\t'))
            {
                if(i == 0) annotations.Add(wxString(elem) + wxT(": "));
                else if (j < annotations.size()) annotations.Item(j) += wxT(" ") + elem;
                j++;
            }
            i++;
        }
        stline = annotations.size();
    }
/*
    char a[]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    
    annotations.Clear();
    int lines = 1 + rand() % 5;
    for(int i = 0; i < lines; i++)
    {
        stringstream ss;
        int char_num = rand() % 20;
        for(int j = 0; j < char_num; j++)
        {
            ss << a[rand() % 62];
        }
        annotations.Add(wxString(ss.str()));
    }
*/    
    return annotations;
}

int Ruler::m_num = 0;
Ruler::Ruler()
{
	type = 7;//ruler
	m_num++;
	m_name = wxString::Format("%d", m_num);
	m_disp = true;
	m_tform = 0;
	m_ruler_type = 0;
	m_finished = false;
	m_use_color = false;

	//time-dependent
	m_time_dep = false;
	m_time = 0;

	m_name_disp = wxString::Format("%d", m_num);
	m_desc = wxString();

}

Ruler::~Ruler()
{
}

//data
int Ruler::GetNumPoint()
{
	return (int)m_ruler.size();
}

Point *Ruler::GetPoint(int index)
{
	if (index>=0 && (size_t)index<m_ruler.size())
		return &(m_ruler[index]);
	else
		return 0;
}

int Ruler::GetRulerType()
{
	return m_ruler_type;
}

void Ruler::SetRulerType(int type)
{
	m_ruler_type = type;
}

bool Ruler::GetFinished()
{
	return m_finished;
}

void Ruler::SetFinished()
{
	m_finished = true;
}

double Ruler::GetLength()
{
	double length = 0.0;
	Point p1, p2;

	for (unsigned int i=1; i<m_ruler.size(); ++i)
	{
		p1 = m_ruler[i-1];
		p2 = m_ruler[i];
		length += (p2-p1).length();
	}

	return length;
}

double Ruler::GetLengthObject(double spcx, double spcy, double spcz)
{
	double length = 0.0;
	Point p1, p2;

	for (unsigned int i=1; i<m_ruler.size(); ++i)
	{
		p1 = m_ruler[i-1];
		p2 = m_ruler[i];
		p1 = Point(p1.x()/spcx, p1.y()/spcy, p1.z()/spcz);
		p2 = Point(p2.x()/spcx, p2.y()/spcy, p2.z()/spcz);
		length += (p2-p1).length();
	}

	return length;
}

double Ruler::GetAngle()
{
	double angle = 0.0;

	if (m_ruler_type == 0 ||
		m_ruler_type == 3)
	{
		if (m_ruler.size() >= 2)
		{
			Vector v = m_ruler[1] - m_ruler[0];
			v.normalize();
			angle = atan2(-v.y(), (v.x()>0.0?1.0:-1.0)*sqrt(v.x()*v.x() + v.z()*v.z()));
			angle = r2d(angle);
			angle = angle<0.0?angle+180.0:angle;
		}
	}
	else if (m_ruler_type == 4)
	{
		if (m_ruler.size() >=3)
		{
			Vector v1, v2;
			v1 = m_ruler[0] - m_ruler[1];
			v1.normalize();
			v2 = m_ruler[2] - m_ruler[1];
			v2.normalize();
			angle = acos(Dot(v1, v2));
			angle = r2d(angle);
		}
	}

	return angle;
}

void Ruler::Scale(double spcx, double spcy, double spcz)
{
	for (size_t i = 0; i < m_ruler.size(); ++i)
		m_ruler[i].scale(spcx, spcy, spcz);
}

bool Ruler::AddPoint(const Point &point)
{
	if (m_ruler_type == 2 &&
		m_ruler.size() == 1)
		return false;
	else if ((m_ruler_type == 0 ||
		m_ruler_type == 3) &&
		m_ruler.size() == 2)
		return false;
	else if (m_ruler_type == 4 &&
		m_ruler.size() == 3)
		return false;
	else
	{
		m_ruler.push_back(point);
		m_balloons.push_back(RulerBalloon(false, point, wxArrayString()));
		if (m_ruler_type == 2 &&
			m_ruler.size() == 1)
			m_finished = true;
		else if ((m_ruler_type == 0 ||
			m_ruler_type == 3) &&
			m_ruler.size() == 2)
			m_finished = true;
		else if (m_ruler_type == 4 &&
			m_ruler.size() == 3)
			m_finished = true;
		return true;
	}
}

void Ruler::SetTransform(Transform *tform)
{
	m_tform = tform;
}

void Ruler::Clear()
{
	m_ruler.clear();
}

wxArrayString Ruler::GetAnnotations(int index, vector<AnnotationDB> annotationdb, double spcx, double spcy, double spcz)
{
    if (index < 0 || (size_t)index >= m_ruler.size())
        return wxArrayString();
    
    m_balloons[index].SetAnnotationsFromDatabase(annotationdb, m_ruler[index], spcx, spcy, spcz);
    return m_balloons[index].GetAnnotations();
}

wxString Ruler::GetDelInfoValues(wxString del)
{
	wxString output;

	for (size_t i=0; i<m_info_values.length(); i++)
	{
		if (m_info_values[i] == '\t')
			output += del;
		else
			output += m_info_values[i];
	}

	return output;
}

void Ruler::SaveProfile(wxString &filename)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int TraceGroup::m_num = 0;
TraceGroup::TraceGroup()
{
	type = 8;//traces
	m_num++;
	m_name = wxString::Format("Traces %d", m_num);
	m_cur_time = -1;
	m_prv_time = -1;
	m_ghost_num = 10;
	m_draw_tail = true;
	m_draw_lead = false;
	m_cell_size = 20;
}

TraceGroup::~TraceGroup()
{
}

void TraceGroup::SetCurTime(int time)
{
	m_cur_time = time;
}

int TraceGroup::GetCurTime()
{
	return m_cur_time;
}
void TraceGroup::SetPrvTime(int time)
{
	m_prv_time = time;
}

int TraceGroup::GetPrvTime()
{
	return m_prv_time;
}

//get information
void TraceGroup::GetLinkLists(size_t frame,
	FL::VertexList &in_orphan_list,
	FL::VertexList &out_orphan_list,
	FL::VertexList &in_multi_list,
	FL::VertexList &out_multi_list)
{
	if (in_orphan_list.size())
		in_orphan_list.clear();
	if (out_orphan_list.size())
		out_orphan_list.clear();
	if (in_multi_list.size())
		in_multi_list.clear();
	if (out_multi_list.size())
		out_multi_list.clear();

	FL::TrackMapProcessor tm_processor;
	tm_processor.SetSizeThresh(m_cell_size);
	tm_processor.GetLinkLists(m_track_map, frame,
		in_orphan_list, out_orphan_list,
		in_multi_list, out_multi_list);
}

void TraceGroup::ClearCellList()
{
	m_cell_list.clear();
}

//cur_sel_list: ids from previous time point
//m_prv_time: previous time value
//m_id_map: ids of current time point that are linked to previous
//m_cur_time: current time value
//time values are check with frame ids in the frame list
void TraceGroup::UpdateCellList(FL::CellList &cur_sel_list)
{
	ClearCellList();
	FL::CellListIter cell_iter;

	//why does not the time change?
	//because I just want to find out the current selection
	if (m_prv_time == m_cur_time)
	{
		//copy cur_sel_list to m_cell_list
		for (cell_iter = cur_sel_list.begin();
			cell_iter != cur_sel_list.end();
			++cell_iter)
		{
			if (cell_iter->second->GetSizeUi() >
				(unsigned int)m_cell_size)
				m_cell_list.insert(pair<unsigned int, FL::pCell>
					(cell_iter->second->Id(), cell_iter->second));
		}
		return;
	}

	//get mapped cells
	//cur_sel_list -> m_cell_list
	FL::TrackMapProcessor tm_processor;
	tm_processor.GetMappedCells(m_track_map,
		cur_sel_list, m_cell_list,
		(unsigned int)m_prv_time,
		(unsigned int)m_cur_time);
}

FL::CellList &TraceGroup::GetCellList()
{
	return m_cell_list;
}

bool TraceGroup::FindCell(unsigned int id)
{
	return m_cell_list.find(id) != m_cell_list.end();
}

//modifications
bool TraceGroup::AddCell(FL::pCell &cell, size_t frame)
{
	FL::TrackMapProcessor tm_processor;
	FL::CellListIter iter;
	return tm_processor.AddCell(m_track_map, cell, frame, iter);
}

bool TraceGroup::LinkCells(FL::CellList &list1, FL::CellList &list2,
	size_t frame1, size_t frame2, bool exclusive)
{
	FL::TrackMapProcessor tm_processor;
	return tm_processor.LinkCells(m_track_map,
		list1, list2, frame1, frame2, exclusive);
}

bool TraceGroup::IsolateCells(FL::CellList &list, size_t frame)
{
	FL::TrackMapProcessor tm_processor;
	return tm_processor.IsolateCells(m_track_map,
		list, frame);
}

bool TraceGroup::UnlinkCells(FL::CellList &list1, FL::CellList &list2,
	size_t frame1, size_t frame2)
{
	FL::TrackMapProcessor tm_processor;
	return tm_processor.UnlinkCells(m_track_map,
		list1, list2, frame1, frame2);
}

bool TraceGroup::CombineCells(FL::pCell &cell, FL::CellList &list,
	size_t frame)
{
	FL::TrackMapProcessor tm_processor;
	return tm_processor.CombineCells(m_track_map,
		cell, list, frame);
}

bool TraceGroup::DivideCells(FL::CellList &list, size_t frame)
{
	FL::TrackMapProcessor tm_processor;
	return tm_processor.DivideCells(m_track_map, list, frame);
}

bool TraceGroup::ReplaceCellID(unsigned int old_id, unsigned int new_id, size_t frame)
{
	FL::TrackMapProcessor tm_processor;
	return tm_processor.ReplaceCellID(m_track_map, old_id, new_id, frame);
}

bool TraceGroup::GetMappedRulers(FL::RulerList &rulers)
{
	size_t frame_num = m_track_map.GetFrameNum();
	if (m_ghost_num <= 0 ||
		m_cur_time < 0 ||
		m_cur_time >= frame_num)
		return false;

	//estimate verts size
	size_t remain_num = frame_num - m_cur_time - 1;
	size_t ghost_lead, ghost_tail;
	ghost_lead = m_draw_lead ?
		(remain_num>m_ghost_num ?
			m_ghost_num : remain_num) : 0;
	ghost_tail = m_draw_tail ?
		(m_cur_time >= m_ghost_num ?
			m_ghost_num : m_cur_time) : 0;

	FL::CellList temp_sel_list1, temp_sel_list2;
	FL::TrackMapProcessor tm_processor;

	if (m_draw_lead)
	{
		temp_sel_list1 = m_cell_list;
		for (size_t i = m_cur_time;
		i < m_cur_time + ghost_lead; ++i)
		{
			tm_processor.GetMappedRulers(m_track_map,
				temp_sel_list1, temp_sel_list2,
				rulers, i, i + 1);
			//swap
			temp_sel_list1 = temp_sel_list2;
			temp_sel_list2.clear();
		}
	}

	//clear ruler id
	for (FL::RulerListIter iter = rulers.begin();
	iter != rulers.end(); ++iter)
		(*iter)->Id(0);

	if (m_draw_tail)
	{
		temp_sel_list1 = m_cell_list;
		for (size_t i = m_cur_time;
		i > m_cur_time - ghost_tail; --i)
		{
			tm_processor.GetMappedRulers(
				m_track_map, temp_sel_list1, temp_sel_list2,
				rulers, i, i - 1);
			//sawp
			temp_sel_list1 = temp_sel_list2;
			temp_sel_list2.clear();
		}
	}

	return true;
}

bool TraceGroup::Load(wxString &filename)
{
	m_data_path = filename;
	FL::TrackMapProcessor tm_processor;
    std::string str = ws2s(m_data_path.ToStdWstring());
	return tm_processor.Import(m_track_map, str);
}

bool TraceGroup::Save(wxString &filename)
{
	m_data_path = filename;
	FL::TrackMapProcessor tm_processor;
    
    std::string str = ws2s(m_data_path.ToStdWstring());
	return tm_processor.Export(m_track_map, str);
}

unsigned int TraceGroup::Draw(vector<float> &verts)
{
	unsigned int result = 0;
	size_t frame_num = m_track_map.GetFrameNum();
	if (m_ghost_num <= 0 ||
		m_cur_time < 0 ||
		m_cur_time >= frame_num)
		return result;

	//estimate verts size
	size_t remain_num = frame_num - m_cur_time - 1;
	size_t ghost_lead, ghost_tail;
	ghost_lead = m_draw_lead ?
		(remain_num>m_ghost_num ?
		m_ghost_num : remain_num) : 0;
	ghost_tail = m_draw_tail ?
		(m_cur_time>=m_ghost_num ?
		m_ghost_num : m_cur_time) : 0;
	verts.reserve((ghost_lead + ghost_tail) *
		m_cell_list.size() * 3 * 6 * 3);//1.5 branches each

	FL::CellList temp_sel_list1, temp_sel_list2;
	FL::TrackMapProcessor tm_processor;

	if (m_draw_lead)
	{
		temp_sel_list1 = m_cell_list;
		for (size_t i = m_cur_time;
			i < m_cur_time + ghost_lead; ++i)
		{
			result += tm_processor.GetMappedEdges(m_track_map,
				temp_sel_list1, temp_sel_list2,
				verts, i, i + 1);
			//swap
			temp_sel_list1 = temp_sel_list2;
			temp_sel_list2.clear();
		}
	}

	if (m_draw_tail)
	{
		temp_sel_list1 = m_cell_list;
		for (size_t i = m_cur_time;
			i > m_cur_time - ghost_tail; --i)
		{
			result += tm_processor.GetMappedEdges(
				m_track_map, temp_sel_list1, temp_sel_list2,
				verts, i, i - 1);
			//sawp
			temp_sel_list1 = temp_sel_list2;
			temp_sel_list2.clear();
		}
	}

	return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int DataGroup::m_num = 0;
DataGroup::DataGroup()
{
	type = 5;//group
	m_num++;
	m_name = wxString::Format("Group %d", m_num);
	m_disp = true;
	m_sync_volume_prop = false;
	m_sync_volume_spc = false;
}

DataGroup::~DataGroup()
{
}

int DataGroup::GetBlendMode()
{
	if (!m_vd_list.empty())
		return m_vd_list[0]->GetBlendMode();
	else
		return 0;
}

//set gamma to all
void DataGroup::SetGammaAll(Color &gamma)
{
	SetGamma(gamma);
	for (int i=0; i<(int)m_vd_list.size(); i++)
	{
		VolumeData* vd = m_vd_list[i];
		if (vd)
			vd->SetGamma(gamma);
	}
}

//set brightness to all
void DataGroup::SetBrightnessAll(Color &brightness)
{
	SetBrightness(brightness);
	for (int i=0; i<(int)m_vd_list.size(); i++)
	{
		VolumeData* vd = m_vd_list[i];
		if (vd)
			vd->SetBrightness(brightness);
	}
}

//set Hdr to all
void DataGroup::SetHdrAll(Color &hdr)
{
	SetHdr(hdr);
	for (int i=0; i<(int)m_vd_list.size(); i++)
	{
		VolumeData* vd = m_vd_list[i];
		if (vd)
			vd->SetHdr(hdr);
	}
}

//set levels to all
void DataGroup::SetLevelsAll(Color &levels)
{
    SetLevels(levels);
    for (int i=0; i<(int)m_vd_list.size(); i++)
    {
        VolumeData* vd = m_vd_list[i];
        if (vd)
            vd->SetLevels(levels);
    }
}

//set sync red to all
void DataGroup::SetSyncRAll(bool sync_r)
{
	SetSyncR(sync_r);
	for (int i=0; i<(int)m_vd_list.size(); i++)
	{
		VolumeData* vd = m_vd_list[i];
		if (vd)
			vd->SetSyncR(sync_r);
	}
}

//set sync green to all
void DataGroup::SetSyncGAll(bool sync_g)
{
	SetSyncG(sync_g);
	for (int i=0; i<(int)m_vd_list.size(); i++)
	{
		VolumeData* vd = m_vd_list[i];
		if (vd)
			vd->SetSyncG(sync_g);
	}
}

//set sync blue to all
void DataGroup::SetSyncBAll(bool sync_b)
{
	SetSyncB(sync_b);
	for (int i=0; i<(int)m_vd_list.size(); i++)
	{
		VolumeData* vd = m_vd_list[i];
		if (vd)
			vd->SetSyncB(sync_b);
	}
}

void DataGroup::ResetSync()
{
	int i;
	int cnt = 0;
	bool r_v = false;
	bool g_v = false;
	bool b_v = false;

	for (i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
		{
			Color c = vd->GetColor();
			bool r, g, b;
			r = g = b = false;
			cnt = 0;
			if (c.r()>0) {cnt++; r=true;}
			if (c.g()>0) {cnt++; g=true;}
			if (c.b()>0) {cnt++; b=true;}

			if (cnt > 1)
			{
				r_v = r_v||r;
				g_v = g_v||g;
				b_v = b_v||b;
			}
		}
	}

	SetSyncRAll(r_v);
	SetSyncGAll(g_v);
	SetSyncBAll(b_v);
}

//volume properties
void DataGroup::SetEnableAlpha(bool mode)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetEnableAlpha(mode);
	}
}

void DataGroup::SetAlpha(double dVal)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetAlpha(dVal);
	}
}

void DataGroup::SetSampleRate(double dVal)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetSampleRate(dVal);
	}
}

void DataGroup::SetBoundary(double dVal)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetBoundary(dVal);
	}
}

void DataGroup::Set3DGamma(double dVal)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->Set3DGamma(dVal);
	}
}

void DataGroup::SetOffset(double dVal)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetOffset(dVal);
	}
}

void DataGroup::SetLeftThresh(double dVal)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetLeftThresh(dVal);
	}
}

void DataGroup::SetRightThresh(double dVal)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetRightThresh(dVal);
	}
}

void DataGroup::SetLowShading(double dVal)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetLowShading(dVal);
	}
}

void DataGroup::SetHiShading(double dVal)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetHiShading(dVal);
	}
}

void DataGroup::SetLuminance(double dVal)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetLuminance(dVal);
	}
}

void DataGroup::SetColormapMode(int mode)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetColormapMode(mode);
	}
}

void DataGroup::SetColormapDisp(bool disp)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetColormapDisp(disp);
	}
}

void DataGroup::SetColormapValues(double low, double high)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
		{
			double l, h;
			vd->GetColormapValues(l, h);
			vd->SetColormapValues(low<0?l:low, high<0?h:high);
		}
	}
}

void DataGroup::SetColormap(int value)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetColormap(value);
	}
}

void DataGroup::SetColormapProj(int value)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetColormapProj(value);
	}
}

void DataGroup::SetShading(bool shading)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetShading(shading);
	}
}

void DataGroup::SetShadow(bool shadow)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetShadow(shadow);
	}
}

void DataGroup::SetShadowParams(double val)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetShadowParams(val);
	}
}

void DataGroup::SetMode(int mode)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetMode(mode);
	}
}

void DataGroup::SetNR(bool val)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetNR(val);
	}
}
//inversion
void DataGroup::SetInterpolate(bool mode)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetInterpolate(mode);
	}
}

//inversion
void DataGroup::SetInvert(bool mode)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetInvert(mode);
	}
}

//blend mode
void DataGroup::SetBlendMode(int mode)
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
			vd->SetBlendMode(mode);
	}
}

//randomize color
void DataGroup::RandomizeColor()
{
	for (int i=0; i<GetVolumeNum(); i++)
	{
		VolumeData* vd = GetVolumeData(i);
		if (vd)
		{
			double hue = (double)rand()/(RAND_MAX) * 360.0;
			Color color(HSVColor(hue, 1.0, 1.0));
			vd->SetColor(color);
		}
	}
}

void DataGroup::SetMaskHideMode(int mode)
{
    for (int i=0; i<GetVolumeNum(); i++)
    {
        VolumeData* vd = GetVolumeData(i);
        if (vd)
            vd->SetMaskHideMode(mode);
    }
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int MeshGroup::m_num = 0;
MeshGroup::MeshGroup()
{
	type = 6;//mesh group
	m_num++;
	m_name = wxString::Format("MGroup %d", m_num);
	m_disp = true;
	m_sync_mesh_prop = false;
}

MeshGroup::~MeshGroup()
{
}

//randomize color
void MeshGroup::RandomizeColor()
{
	for (int i=0; i<GetMeshNum(); i++)
	{
		MeshData* md = GetMeshData(i);
		if (md)
		{
			double hue = (double)rand()/(RAND_MAX) * 360.0;
			Color color(HSVColor(hue, 1.0, 1.0));
			md->SetColor(color, MESH_COLOR_DIFF);
            Color amb = color * 0.3;
			md->SetColor(amb, MESH_COLOR_AMB);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
DataManager::DataManager() :
m_vol_exb(0.0),
	m_vol_gam(1.0),
	m_vol_of1(1.0),
	m_vol_of2(1.0),
	m_vol_lth(0.0),
	m_vol_hth(1.0),
	m_vol_lsh(0.5),
	m_vol_hsh(10.0),
	m_vol_alf(0.5),
	m_vol_spr(1.5),
	m_vol_xsp(1.0),
	m_vol_ysp(1.0),
	m_vol_zsp(2.5),
	m_vol_lum(1.0),
	m_vol_cmp(0),
	m_vol_lcm(0.0),
	m_vol_hcm(1.0),
	m_vol_eap(true),
	m_vol_esh(true),
	m_vol_interp(true),
	m_vol_inv(false),
	m_vol_mip(false),
	m_vol_nrd(false),
	m_vol_shw(false),
	m_vol_swi(0.0),
	m_vol_test_wiref(false),
	m_use_defaults(true),
	m_override_vox(true)
{
	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(),NULL);
#ifdef _DARWIN
	wxString dft = expath + "/../Resources/default_volume_settings.dft";
#else
	wxString dft = expath + GETSLASHS() + "default_volume_settings.dft";
	if (!wxFileExists(dft))
		dft = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "default_volume_settings.dft";
#endif
	wxFileInputStream is(dft);
	if (!is.IsOk())
		return;
	wxFileConfig fconfig(is);

	double val;
	int ival;
	if (fconfig.Read("extract_boundary", &val))
		m_vol_exb = val;
	if (fconfig.Read("gamma", &val))
		m_vol_gam = val;
	if (fconfig.Read("low_offset", &val))
		m_vol_of1 = val;
	if (fconfig.Read("high_offset", &val))
		m_vol_of2 = val;
	if (fconfig.Read("low_thresholding", &val))
		m_vol_lth = val;
	if (fconfig.Read("high_thresholding", &val))
		m_vol_hth = val;
	if (fconfig.Read("low_shading", &val))
		m_vol_lsh = val;
	if (fconfig.Read("high_shading", &val))
		m_vol_hsh = val;
	if (fconfig.Read("alpha", &val))
		m_vol_alf = val;
	if (fconfig.Read("sample_rate", &val))
		m_vol_spr = val;
	if (fconfig.Read("x_spacing", &val))
		m_vol_xsp = val;
	if (fconfig.Read("y_spacing", &val))
		m_vol_ysp = val;
	if (fconfig.Read("z_spacing", &val))
		m_vol_zsp = val;
	if (fconfig.Read("luminance", &val))
		m_vol_lum = val;
	if (fconfig.Read("colormap", &ival))
		m_vol_cmp = ival;
	if (fconfig.Read("colormap_low", &val))
		m_vol_lcm = val;
	if (fconfig.Read("colormap_hi", &val))
		m_vol_hcm = val;

	bool bval;
	if (fconfig.Read("enable_alpha", &bval))
		m_vol_eap = bval;
	if (fconfig.Read("enable_shading", &bval))
		m_vol_esh = bval;
	if (fconfig.Read("enable_interp", &bval))
		m_vol_interp = bval;
	if (fconfig.Read("enable_inv", &bval))
		m_vol_inv = bval;
	if (fconfig.Read("enable_mip", &bval))
		m_vol_mip = bval;
	if (fconfig.Read("noise_rd", &bval))
		m_vol_nrd = bval;

	//shadow
	if (fconfig.Read("enable_shadow", &bval))
		m_vol_shw = bval;
	if (fconfig.Read("shadow_intensity", &val))
		m_vol_swi = val;

	//wavelength to color table
	m_vol_wav[0] = Color(1.0, 1.0, 1.0);
	m_vol_wav[1] = Color(1.0, 1.0, 1.0);
	m_vol_wav[2] = Color(1.0, 1.0, 1.0);
	m_vol_wav[3] = Color(1.0, 1.0, 1.0);

	//slice sequence
	m_sliceSequence = false;
	m_timeSequence = false;
	//compression
	m_compression = false;
	//skip brick
	m_skip_brick = false;
	//time sequence identifier
	m_timeId = "_T";
	//load mask
	m_load_mask = false;
}

DataManager::~DataManager()
{
    for (int i=0; i<(int)m_annotation_list.size(); i++)
        if (m_annotation_list[i])
            delete m_annotation_list[i];
	for (int i=0 ; i<(int)m_vd_list.size() ; i++)
		if (m_vd_list[i])
			delete m_vd_list[i];
	for (int i=0 ; i<(int)m_md_list.size() ; i++)
		if (m_md_list[i])
			delete m_md_list[i];
	for (int i=0; i<(int)m_reader_list.size(); i++)
		if (m_reader_list[i])
			delete m_reader_list[i];
}

void DataManager::ClearAll()
{
    for (int i=0; i<(int)m_annotation_list.size(); i++)
        if (m_annotation_list[i])
            delete m_annotation_list[i];
	for (int i=0 ; i<(int)m_vd_list.size() ; i++)
		if (m_vd_list[i])
			delete m_vd_list[i];
	for (int i=0 ; i<(int)m_md_list.size() ; i++)
		if (m_md_list[i])
			delete m_md_list[i];
	for (int i=0; i<(int)m_reader_list.size(); i++)
		if (m_reader_list[i])
			delete m_reader_list[i];
	m_vd_list.clear();
	m_md_list.clear();
	m_reader_list.clear();
	m_annotation_list.clear();
}

void DataManager::SetVolumeDefault(VolumeData* vd)
{
	if (m_use_defaults)
	{
		vd->SetWireframe(m_vol_test_wiref);
		vd->Set3DGamma(m_vol_gam);
		vd->SetBoundary(m_vol_exb);
		vd->SetOffset(m_vol_of1);
		vd->SetLeftThresh(m_vol_lth);
		vd->SetRightThresh(m_vol_hth);
		vd->SetAlpha(m_vol_alf);
		double amb, diff, spec, shine;
		vd->GetMaterial(amb, diff, spec, shine);
		vd->SetMaterial(m_vol_lsh, diff, spec, m_vol_hsh);
		vd->SetColormap(m_vol_cmp);
		vd->SetColormapValues(m_vol_lcm, m_vol_hcm);

		int xres, yres, zres;
		vd->GetResolution(xres, yres, zres);
		if (zres == 1) vd->SetSampleRate(1.0);
		else vd->SetSampleRate(m_vol_spr);
		if (!vd->GetSpcFromFile())
		{
			double zspcfac = (double)Max(xres,yres)/256.0;
			if (zspcfac < 1.0) zspcfac = 1.0;
			if (zres == 1) vd->SetSpacings(m_vol_xsp, m_vol_ysp, m_vol_xsp*zspcfac);
			else vd->SetSpacings(m_vol_xsp, m_vol_ysp, m_vol_zsp);
		}

		vd->SetEnableAlpha(m_vol_eap);
		int resx, resy, resz;
		vd->GetResolution(resx, resy, resz);
		if (resz > 1)
			vd->SetShading(m_vol_esh);
		else
			vd->SetShading(false);
		vd->SetMode(m_vol_mip?1:0);
		vd->SetNR(m_vol_nrd);
		//inversion
		vd->SetInterpolate(m_vol_interp);
		//inversion
		vd->SetInvert(m_vol_inv);

		//shadow
		vd->SetShadow(m_vol_shw);
		vd->SetShadowParams(m_vol_swi);
	}
}

//set project path
//when data and project are moved, use project file's path
//if data's directory doesn't exist
void DataManager::SetProjectPath(wxString path)
{
	m_prj_path.Clear();
	int sep = path.Find(GETSLASH(), true);
	if (sep != wxNOT_FOUND)
		m_prj_path = path.Left(sep);
}

wxString DataManager::SearchProjectPath(wxString &filename)
{
	int i;

	wxString pathname = filename;

	if (m_prj_path == "")
		return "";
	wxString search_str;
	for (i=pathname.Length()-1; i>=0; i--)
	{
		search_str.Prepend(pathname[i]);
		if (pathname[i]=='\\' || pathname[i]=='/')
		{
            search_str[0] = GETSLASH();
			wxString name_temp = m_prj_path + search_str;
			if (wxFileExists(name_temp))
				return name_temp;
		}
	}
	return "";
}

bool DataManager::DownloadToCurrentDir(wxString &filename)
{
	wxString pathname = filename;
	wxURL url(pathname);
	if (!url.IsOk())
		return false;
	url.GetProtocol().SetTimeout(10);
	wxString suffix = pathname.Mid(pathname.Find('.', true)).MakeLower();
	if(url.GetError() != wxURL_NOERR) return false;
	wxInputStream *in = url.GetInputStream();
	if(!in || !in->IsOk()) return false;
#define DOWNLOAD_BUFSIZE 8192
	unsigned char buffer[DOWNLOAD_BUFSIZE];
	size_t count = -1;
	wxMemoryBuffer mem_buf;
	while(!in->Eof() && count != 0)
	{
		in->Read(buffer, DOWNLOAD_BUFSIZE-1);
		count = in->LastRead();
		if(count > 0) mem_buf.AppendData(buffer, count);
	}

#ifdef _WIN32
	wxString tmpfname = wxStandardPaths::Get().GetTempDir() + "\\" + pathname.Mid(pathname.Find(wxT('/'), true)).Mid(1);
#else
    wxString tmpfname = wxStandardPaths::Get().GetTempDir() + "/" + pathname.Mid(pathname.Find(wxT('/'), true)).Mid(1);
#endif

	wxFile of(tmpfname, wxFile::write);
	of.Write(mem_buf.GetData(), mem_buf.GetDataLen());
	of.Close();

	filename = tmpfname;

	return true;
}

int DataManager::LoadVolumeData(wxString &filename, int type, int ch_num, int t_num, size_t datasize, wxString prefix, wxString metadata)
{
	wxString pathname = filename;
	bool isURL = false;
	bool downloaded = false;
	wxString downloaded_filepath;
	bool downloaded_metadata = false;
	wxString downloaded_metadatafilepath;
    
    wxString suffix = filename.Mid(filename.Find('.', true)).MakeLower();
	
	if (!wxFileExists(pathname) && suffix != ".n5fs_ch" && suffix != ".n5" && suffix != ".zarr" && suffix != ".zfs_ch")
	{
		pathname = SearchProjectPath(filename);
		if (!wxFileExists(pathname))
		{
			pathname = filename;
			if (!DownloadToCurrentDir(pathname)) return 0;
			//wxString tmpfname = pathname.Mid(pathname.Find(wxT('/'), true)).Mid(1);
			//pathname = tmpfname;
			
			downloaded = true;
			downloaded_filepath = pathname;

			if(type == LOAD_TYPE_BRKXML)
			{
				wxString metadatapath = filename;
				metadatapath = metadatapath.Mid(0, metadatapath.Find(wxT('/'), true)+1) + wxT("_metadata.xml");
				//std::remove("_metadata.xml");
				if (DownloadToCurrentDir(metadatapath))
				{
					downloaded_metadata = true;
					downloaded_metadatafilepath = metadatapath;
				}
				isURL = true;
			}
		}
	}

	int i;
	int result = 0;
	BaseReader* reader = 0;
    
    wxString idi_metadata_path;
    int idi_type = 0;
    if (type == LOAD_TYPE_IDI)
    {
        wxString idi_volpath;
        wxFileInputStream ist(pathname);
        if (!ist.IsOk()) return result;
        wxZipInputStream zstream(ist);
        if (!zstream.IsOk()) return result;
        
        wxZipEntry *entry;
        while (entry = zstream.GetNextEntry())
        {
            wxString ename = entry->GetName();
            if (entry->IsDir())
                continue;
            if (ename.Mid(ename.Find(wxFileName::GetPathSeparator(), true)+1).StartsWith("."))
                continue;
            wxString ret = (wxStandardPaths::Get().GetTempDir() + wxFileName::GetPathSeparator() + ename).ToStdWstring();
            wxString esuffix = ret.Mid(ret.Find('.', true)).MakeLower();
            if (esuffix == ".nrrd" || esuffix == ".tif" || esuffix == ".xml")
            {
                if (esuffix == ".nrrd")
                {
                    idi_volpath = ret;
                    idi_type = LOAD_TYPE_NRRD;
                }
                else if (esuffix == ".tif")
                {
                    idi_volpath = ret;
                    idi_type = LOAD_TYPE_TIFF;
                }
                else if (esuffix == ".xml")
                    idi_metadata_path = ret;
                wxFileOutputStream file(ret);
                if (!file.IsOk()) return result;
                file.Write(zstream);
            }
        }
        zstream.CloseEntry();
        if (idi_volpath.IsEmpty() || idi_metadata_path.IsEmpty())
            return result;
        pathname = idi_volpath;
    }

	for (i=0; i<(int)m_reader_list.size(); i++)
	{
		wstring wstr = pathname.ToStdWstring();
		if (m_reader_list[i]->Match(wstr))
		{
			reader = m_reader_list[i];
			break;
		}
	}

	if (reader)
	{
		bool preprocess = false;
		if (reader->GetSliceSeq() != m_sliceSequence)
		{
			reader->SetSliceSeq(m_sliceSequence);
			preprocess = true;
		}
		if (reader->GetSliceSeq() != m_timeSequence)
		{
			reader->SetTimeSeq(m_timeSequence);
			preprocess = true;
		}
		if (reader->GetTimeId() != m_timeId.ToStdWstring())
		{
			wstring str_w = m_timeId.ToStdWstring();
			reader->SetTimeId(str_w);
			preprocess = true;
		}
		if (preprocess)
        {
            if (type == LOAD_TYPE_BRKXML)
            {
                wxString msuffix = metadata.Mid(metadata.Find('.', true)).MakeLower();
                if (msuffix.compare(".xml") == 0)
                {
                    BRKXMLReader* br = (BRKXMLReader *)reader;
                    br->SetBDVMetadataPath(metadata.ToStdWstring().c_str());
                }
            }
			reader->Preprocess();
        }
	}
	else
	{
		//RGB tiff
		if (type == LOAD_TYPE_TIFF || (type == LOAD_TYPE_IDI && idi_type == LOAD_TYPE_TIFF))
			reader = new TIFReader();
		else if (type == LOAD_TYPE_NRRD || (type == LOAD_TYPE_IDI && idi_type == LOAD_TYPE_NRRD))
			reader = new NRRDReader();
		else if (type == LOAD_TYPE_OIB)
			reader = new OIBReader();
		else if (type == LOAD_TYPE_OIF)
			reader = new OIFReader();
		else if (type == LOAD_TYPE_LSM)
			reader = new LSMReader();
        else if (type == LOAD_TYPE_CZI)
            reader = new CZIReader();
        else if (type == LOAD_TYPE_ND2)
            reader = new ND2Reader();
		else if (type == LOAD_TYPE_PVXML)
			reader = new PVXMLReader();
		else if (type == LOAD_TYPE_BRKXML)
			reader = new BRKXMLReader();
		else if (type == LOAD_TYPE_H5J)
			reader = new H5JReader();
		else if (type == LOAD_TYPE_V3DPBD)
			reader = new V3DPBDReader();

		m_reader_list.push_back(reader);
		wstring str_w = pathname.ToStdWstring();
		reader->SetFile(str_w);
		reader->SetSliceSeq(m_sliceSequence);
		reader->SetTimeSeq(m_timeSequence);
		str_w = m_timeId.ToStdWstring();
		reader->SetTimeId(str_w);

		if(isURL && type == LOAD_TYPE_BRKXML)
		{
			str_w = filename.ToStdWstring();
#ifdef _WIN32
			wchar_t slash = L'\\';
#else
			wchar_t slash = L'/';
#endif
			wstring dir_name = str_w.substr(0, str_w.find_last_of(slash)+1);
			((BRKXMLReader *)reader)->SetDir(str_w);
		}
        
        if (type == LOAD_TYPE_BRKXML)
        {
            wxString suffix = metadata.Mid(metadata.Find('.', true)).MakeLower();
            if (suffix.compare(".xml") == 0)
            {
                BRKXMLReader* br = (BRKXMLReader *)reader;
                br->SetBDVMetadataPath(metadata.ToStdWstring().c_str());
            }
        }
		reader->Preprocess();

		if (type == LOAD_TYPE_BRKXML && !((BRKXMLReader *)reader)->GetExMetadataURL().empty())
		{
			wxString metadatapath = ((BRKXMLReader *)reader)->GetExMetadataURL();
			if (DownloadToCurrentDir(metadatapath))
			{
				downloaded_metadata = true;
				downloaded_metadatafilepath = metadatapath;
				((BRKXMLReader *)reader)->loadMetadata(metadatapath.ToStdWstring());
			}
		}
	}
    
    wxString msg = reader->GetErrorMsg();
    if (!msg.IsEmpty())
    {
        wxMessageBox(msg);
        return result;
    }

	if (!m_latest_vols.empty())
		m_latest_vols.clear();

	int chan = reader->GetChanNum();
	size_t voxnum = reader->GetXSize() * reader->GetYSize() * reader->GetSliceNum();
	size_t ch_datasize = 0;
	size_t bytes = 0;
    
    double sspcx = -1.0;
    double sspcy = -1.0;
    double sspcz = -1.0;

	for (i=(ch_num>=0?ch_num:0);
		i<(ch_num>=0?ch_num+1:chan); i++)
	{
		VolumeData *found_vd = NULL;
		VolumeData *vd;
		for (int j = 0; j < m_vd_list.size(); j++)
			if (m_vd_list[j] && m_vd_list[j]->GetReader() == reader && !m_vd_list[j]->GetDup() &&
				m_vd_list[j]->GetCurChannel() == i)
				found_vd = m_vd_list[j];
		/*if (found_vd)
		{
			vd = VolumeData::DeepCopy(*found_vd, true, this);
			AddVolumeData(vd);
			result++;
		}
		else*/
		{
			VolumeData *vd = new VolumeData();
			vd->SetSkipBrick(m_skip_brick);
			auto data = reader->Convert(t_num>=0?t_num:reader->GetCurTime(), i, true);

			if (datasize > 0)
			{
				if (ch_datasize == 0)
				{
					bytes = data->getBytesPerSample();
					if (ch_num >= 0)
						ch_datasize = (datasize * bytes) / (3ULL + bytes);
					else
						ch_datasize = (datasize * bytes) / (3ULL + bytes * chan);
				}
				if (ch_datasize > 0 && bytes > 0 && ch_datasize < voxnum * bytes)
				{
					Nrrd* tmp;
					tmp = VolumeData::NrrdScale(data->getNrrd(), ch_datasize);
					if (tmp)
					{
						data = make_shared<VL_Nrrd>(tmp);
                        
                        size_t dim = data->getNrrd()->dim;
                        if (dim >= 3)
                        {
                            int offset = 0;
                            if (dim > 3) offset = 1;
                            sspcx = tmp->axis[0 + offset].spacing;
                            sspcy = tmp->axis[1 + offset].spacing;
                            sspcz = tmp->axis[2 + offset].spacing;
                        }
					}
				}
			}

			if (!data)
				continue;

			wxString name;
			if (type != LOAD_TYPE_BRKXML)
			{
				name = wxString(reader->GetDataName());
				if (chan > 1)
					name += wxString::Format("_Ch%d", i+1);
			}
			else 
			{
				BRKXMLReader* breader = (BRKXMLReader*)reader;
				name = reader->GetDataName();
				name = name.Mid(0, name.find_last_of(wxT('.')));
                wstring chan_name = breader->getChannelName(i);
                if (!chan_name.empty())
                    name += wxT("_") + chan_name;
				else if(chan > 1)
                    name += wxT("_Ch") + wxString::Format("%i", i);
				pathname = filename;
				breader->SetCurChan(i);
				breader->SetCurTime(0);
			}
            
            if (!prefix.IsEmpty())
                name = prefix + wxT("_") + name;

			bool valid_spc = reader->IsSpcInfoValid();
			if (vd && vd->Load(data, name, pathname, (type == LOAD_TYPE_BRKXML) ? (BRKXMLReader*)reader : NULL))
			{
				if (m_load_mask)
				{
					//mask
					MSKReader msk_reader;
					std::wstring str = reader->GetPathName();
					msk_reader.SetFile(str);
					auto mask = msk_reader.Convert(t_num>=0?t_num:reader->GetCurTime(), i, true);
					if (mask)
						vd->LoadMask(mask);
					//label mask
					LBLReader lbl_reader;
					str = reader->GetPathName();
					lbl_reader.SetFile(str);

					auto label = lbl_reader.Convert(t_num>=0?t_num:reader->GetCurTime(), i, true);
					if (label)
						vd->LoadLabel(label);
				}
				if (type == LOAD_TYPE_BRKXML) ((BRKXMLReader*)reader)->SetLevel(0);
				//for 2D data
				int xres, yres, zres;
				vd->GetResolution(xres, yres, zres);
				double zspcfac = (double)Max(xres,yres)/256.0;
				if (zspcfac < 1.0) zspcfac = 1.0;
				if (zres == 1) vd->SetBaseSpacings(reader->GetXSpc(), reader->GetYSpc(), reader->GetXSpc()*zspcfac);
				else if (sspcx > 0.0 && sspcy > 0.0 && sspcz > 0.0) vd->SetBaseSpacings(sspcx, sspcy, sspcz);
                else vd->SetBaseSpacings(reader->GetXSpc(), reader->GetYSpc(), reader->GetZSpc());
				vd->SetSpcFromFile(valid_spc);
				vd->SetScalarScale(reader->GetScalarScale());
				vd->SetMaxValue(reader->GetMaxValue());
				vd->SetCurTime(reader->GetCurTime());
				vd->SetCurChannel(i);
				//++
				result++;
			}
			else
			{
				delete vd;
				continue;
			}
			vd->SetReader(reader);
			vd->SetCompression(m_compression);
			AddVolumeData(vd);

			SetVolumeDefault(vd);
            /*
			if (type == LOAD_TYPE_TIFF)
			{
				double minval, maxval, vxmax;
				((TIFReader*)reader)->GetDisplayRange(i, minval, maxval);
				vxmax = vd->GetMaxValue();
				if (minval >= 0)
					vd->SetLeftThresh(minval/vxmax);
				if (maxval >= 0)
					vd->SetOffset(maxval/vxmax);
			}
             */
            
            if (type == LOAD_TYPE_BRKXML && vd->GetMaxValue() == 65535)
            {
                vd->SetOffset(4096.0/65535.0);
            }

			//get excitation wavelength
			double wavelength = reader->GetExcitationWavelength(i);
			if (wavelength > 0.0) {
				FLIVR::Color col = GetWavelengthColor(wavelength);
				vd->SetColor(col);
			}
			else if (wavelength < 0.) {
				FLIVR::Color white = Color(1.0, 1.0, 1.0);
				vd->SetColor(white);
			}
			else
			{
				FLIVR::Color white = Color(1.0, 1.0, 1.0);
				FLIVR::Color red   = Color(1.0, 0.0, 0.0);
				FLIVR::Color green = Color(0.0, 1.0, 0.0);
				FLIVR::Color blue  = Color(0.0, 0.0, 1.0);
				if (chan == 1) {
					vd->SetColor(white);
				}
				else
				{
					if (i == 0)
						vd->SetColor(red);
					else if (i == 1)
						vd->SetColor(green);
					else if (i == 2)
						vd->SetColor(blue);
					else
						vd->SetColor(white);
				}
			}
            
            if (type == LOAD_TYPE_IDI)
            {
                vd->ImportROITreeXML(idi_metadata_path.ToStdWstring());
                if (!vd->ExportROITree().empty())
                {
                    vd->SetColormapMode(3);
                    vd->SelectAllNamedROI();
                    vd->SetIDColDispMode(2);
                    vd->SetAlpha(0.5);
                }
            }
            
            if (type == LOAD_TYPE_BRKXML)
            {
                wxString msuffix = metadata.Mid(metadata.Find('.', true)).MakeLower();
                if (msuffix.compare(".xml") == 0)
                {
                    BRKXMLReader* br = (BRKXMLReader *)reader;
                    Transform bdv_tranform;
                    if (suffix == ".n5fs_ch")
                        bdv_tranform = br->GetBDVTransform();
                    else
                        bdv_tranform = br->GetBDVTransform(i, t_num>=0?t_num:reader->GetCurTime());
                    Texture* tex = vd->GetTexture();
                    if (tex)
                    {
                        Transform* tform = tex->transform();
                        tform->post_trans(bdv_tranform);
                        tex->set_additional_transform(bdv_tranform);
                    }
                }
            }
            vd->SetMetadataPath(metadata);
			m_latest_vols.push_back(vd);
		}

	}

	if (downloaded) wxRemoveFile(downloaded_filepath);
	if (downloaded_metadata) wxRemoveFile(downloaded_metadatafilepath);

	return result;
}

int DataManager::LoadMeshData(wxString &filename, wxString prefix)
{
	wxString pathname = filename;
	if (!wxFileExists(pathname))
	{
		pathname = SearchProjectPath(filename);
		if (!wxFileExists(pathname))
			return 0;
	}

	MeshData *md = new MeshData();
	if (!md->Load(pathname))
        return 0;

	wxString name = md->GetName();
    wxString new_name = (prefix.IsEmpty() ? wxT("") : prefix + wxT("_")) + name;
	int i;
	for (i=1; CheckNames(new_name, DATA_MESH); i++)
		new_name = name+wxString::Format("_%d", i);
	if (i>1 || !prefix.IsEmpty())
		md->SetName(new_name);
	m_md_list.push_back(md);

	return 1;
}

int DataManager::LoadMeshData(GLMmodel* mesh)
{
	if (!mesh) return 0;

	MeshData *md = new MeshData();
	md->Load(mesh);

	wxString name = md->GetName();
	wxString new_name = name;
	int i;
	for (i=1; CheckNames(new_name, DATA_MESH); i++)
		new_name = name+wxString::Format("_%d", i);
	if (i>1)
		md->SetName(new_name);
	m_md_list.push_back(md);

	return 1;
}

VolumeData* DataManager::GetVolumeData(int index)
{
	if (index>=0 && index<(int)m_vd_list.size())
		return m_vd_list[index];
	else
		return 0;
}

MeshData* DataManager::GetMeshData(int index)
{
	if (index>=0 && index<(int)m_md_list.size())
		return m_md_list[index];
	else
		return 0;
}

VolumeData* DataManager::GetVolumeData(const wxString &name)
{
	for (int i=0 ; i<(int)m_vd_list.size() ; i++)
	{
		if (name == m_vd_list[i]->GetName())
		{
			return m_vd_list[i];
		}
	}
	return 0;
}

MeshData* DataManager::GetMeshData(wxString &name)
{
	for (int i=0 ; i<(int)m_md_list.size() ; i++)
	{
		if (name == m_md_list[i]->GetName())
		{
			return m_md_list[i];
		}
	}
	return 0;
}

int DataManager::GetVolumeIndex(wxString &name)
{
	for (int i=0 ; i<(int)m_vd_list.size() ; i++)
	{
		if (!m_vd_list[i])
			continue;
		if (name == m_vd_list[i]->GetName())
		{
			return i;
		}
	}
	return -1;
}

int DataManager::GetMeshIndex(wxString &name)
{
	for (int i=0 ; i<(int)m_md_list.size() ; i++)
	{
		if (name == m_md_list[i]->GetName())
		{
			return i;
		}
	}
	return -1;
}

void DataManager::ReplaceVolumeData(int index, VolumeData *vd)
{
	if (index < 0 || index >= m_vd_list.size())
		return;

	VolumeData* data = m_vd_list[index];
	if (data)
	{
		auto p = find(m_latest_vols.begin(), m_latest_vols.end(), data);
		if (p != m_latest_vols.end())
			m_latest_vols.erase(p);

		delete data;
		data = 0;
	}
	m_vd_list[index] = vd;
}

void DataManager::RemoveVolumeData(int index)
{
	if (index < 0 || index >= m_vd_list.size())
		return;

	VolumeData* data = m_vd_list[index];
	if (data)
	{
        for (int i=0 ; i<(int)m_annotation_list.size() ; i++)
        {
            if (m_annotation_list[i] && data == m_annotation_list[i]->GetVolume())
                m_annotation_list[i]->SetVolume(NULL);
        }
        
		auto p = find(m_latest_vols.begin(), m_latest_vols.end(), data);
		if (p != m_latest_vols.end())
			m_latest_vols.erase(p);

		m_vd_list.erase(m_vd_list.begin()+index);
		delete data;
		data = 0;
	}	
}

void DataManager::RemoveVolumeData(const wxString &name)
{
	for (int i=0 ; i<(int)m_vd_list.size() ; i++)
	{
		if (name == m_vd_list[i]->GetName())
		{
			RemoveVolumeData(i);
		}
	}
}

void DataManager::RemoveVolumeDataset(BaseReader *reader, int channel)
{
	
	for (int i = m_vd_list.size()-1; i >= 0; i--)
	{
		VolumeData* data = m_vd_list[i];
		if (data && data->GetReader() == reader && data->GetCurChannel() == channel)
		{
            RemoveVolumeData(i);
		}
	}
}

void DataManager::RemoveMeshData(int index)
{
	MeshData* data = m_md_list[index];
	if (data)
	{
        for (int i=0 ; i<(int)m_annotation_list.size() ; i++)
        {
            if (m_annotation_list[i] && data == m_annotation_list[i]->GetMesh())
                m_annotation_list[i]->SetMesh(NULL);
        }
		m_md_list.erase(m_md_list.begin()+index);
		delete data;
		data = 0;
	}
}

void DataManager::RemoveMeshData(const wxString &name)
{
	for (int i=0 ; i<(int)m_md_list.size() ; i++)
	{
		if (name == m_md_list[i]->GetName())
		{
			RemoveMeshData(i);
		}
	}
}

int DataManager::GetVolumeNum()
{
	return m_vd_list.size();
}

int DataManager::GetMeshNum()
{
	return m_md_list.size();
}

void DataManager::AddVolumeData(VolumeData* vd)
{
	if (!vd)
		return;

	wxString name = vd->GetName();
	wxString new_name = name;

	int i;
	for (i=1; CheckNames(new_name, DATA_VOLUME); i++)
		new_name = name+wxString::Format("_%d", i);

	if (i>1)
		vd->SetName(new_name);
	
	if (m_override_vox)
	{
		if (m_vd_list.size() > 0)
		{
			double spcx, spcy, spcz;
			m_vd_list[0]->GetSpacings(spcx, spcy, spcz, 0);
			vd->SetSpacings(spcx, spcy, spcz);
			vd->SetSpcFromFile(true);
		}
	}
	
	m_vd_list.push_back(vd);
}

void DataManager::AddEmptyVolumeData(wxString name, int bits, int nx, int ny, int nz, double spcx, double spcy, double spcz)
{
	wxString new_name = name;

	int i;
	for (i = 1; CheckNames(new_name, DATA_VOLUME); i++)
		new_name = name + wxString::Format("_%d", i);

	VolumeData* vd = new VolumeData();
	vd->AddEmptyData(bits, nx, ny, nz, spcx, spcy, spcz);

	vd->SetName(new_name);

	if (m_override_vox)
	{
		if (m_vd_list.size() > 0)
		{
			double spcx, spcy, spcz;
			m_vd_list[0]->GetSpacings(spcx, spcy, spcz, 0);
			vd->SetSpacings(spcx, spcy, spcz);
			vd->SetSpcFromFile(true);
		}
	}

	m_vd_list.push_back(vd);
}

void DataManager::AddMeshData(MeshData *md)
{
	wxString name = md->GetName();
	wxString new_name = name;
	int i;
	for (i=1; CheckNames(new_name, DATA_MESH); i++)
		new_name = name+wxString::Format("_%d", i);
	if (i>1)
		md->SetName(new_name);
	m_md_list.push_back(md);

	return;
}

VolumeData* DataManager::DuplicateVolumeData(VolumeData* vd, bool use_default_settings)
{
	VolumeData* vd_new = 0;

	if (vd)
	{
		vd_new = VolumeData::DeepCopy(*vd, use_default_settings, this);
		if (vd_new) AddVolumeData(vd_new);
	}

	return vd_new;
}

MeshData* DataManager::DuplicateMeshData(MeshData* md, bool use_default_settings)
{
	MeshData* md_new = 0;

	if (md)
	{
		md_new = MeshData::DeepCopy(*md, use_default_settings, this);
		if (md_new) AddMeshData(md_new);
	}

	return md_new;
}

Annotations* DataManager::LoadAnnotations(wxString &filename)
{
	wxString pathname = filename;
	if (!wxFileExists(pathname))
	{
		pathname = SearchProjectPath(filename);
		if (!wxFileExists(pathname))
			return nullptr;
	}

	Annotations* ann = new Annotations();
	ann->Load(pathname, this);

	wxString name = ann->GetName();
	wxString new_name = name;
	int i;
	for (i=1; CheckNames(new_name, DATA_ANNOTATIONS); i++)
		new_name = name+wxString::Format("_%d", i);
	if (i>1)
		ann->SetName(new_name);

	wxString labelpathname = filename.Mid(0, filename.find_last_of(wxT('.'))) + ".lbl";
	if (!wxFileExists(labelpathname))
	{
		labelpathname = SearchProjectPath(labelpathname);
		if (!wxFileExists(labelpathname)) 
			labelpathname = "";
	}

	if (wxFileExists(labelpathname)) {
		LBLReader reader;
        wstring fn = labelpathname.ToStdWstring();
		reader.SetFile(fn);
		ann->SetLabel(reader.Convert(0, 0, false));
	}

	m_annotation_list.push_back(ann);

	return ann;
}

void DataManager::AddAnnotations(Annotations* ann)
{
	if (!ann)
		return;

	wxString name = ann->GetName();
	wxString new_name = name;

	int i;
	for (i=1; CheckNames(new_name, DATA_ANNOTATIONS); i++)
		new_name = name+wxString::Format("_%d", i);

	if (i>1)
		ann->SetName(new_name);
    
    MeshData* md = ann->GetMesh();
    if (md)
    {
        bool found = false;
        for (int i=0 ; i<(int)m_md_list.size() ; i++)
        {
            if (md == m_md_list[i])
            {
                found = true;
                break;
            }
        }
        if (!found)
            AddMeshData(md);
    }

	m_annotation_list.push_back(ann);
}

void DataManager::RemoveAnnotations(int index)
{
	Annotations* ann = m_annotation_list[index];
	if (ann)
	{
        MeshData* md = ann->GetMesh();
        if (md)
        {
            int id = -1;
            for (int i=0 ; i<(int)m_md_list.size() ; i++)
            {
                if (md == m_md_list[i])
                {
                    id = i;
                    break;
                }
            }
            if (id >= 0)
            {
                bool found = false;
                for (int i=0 ; i<(int)m_annotation_list.size() ; i++)
                {
                    if (m_md_list[id] == m_annotation_list[i]->GetMesh())
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    RemoveMeshData(id);
            }
            else
                delete md;
        }
		m_annotation_list.erase(m_annotation_list.begin()+index);
		delete ann;
		ann = 0;
	}
}

void DataManager::RemoveAnnotations(const wxString &name)
{
	for (int i=0 ; i<(int)m_annotation_list.size() ; i++)
	{
		if (name == m_annotation_list[i]->GetName())
		{
			RemoveAnnotations(i);
		}
	}
}

int DataManager::GetAnnotationNum()
{
	return m_annotation_list.size();
}

Annotations* DataManager::GetAnnotations(int index)
{
	if (index>=0 && index<(int)m_annotation_list.size())
		return m_annotation_list[index];
	else
		return 0;
}

Annotations* DataManager::GetAnnotations(wxString &name)
{
	for (int i=0; i<(int)m_annotation_list.size(); i++)
	{
		if (name == m_annotation_list[i]->GetName())
			return m_annotation_list[i];
	}
	return 0;
}

int DataManager::GetAnnotationIndex(wxString &name)
{
	for (int i=0; i<(int)m_annotation_list.size(); i++)
	{
		if (!m_annotation_list[i])
			continue;
		if (name == m_annotation_list[i]->GetName())
			return i;
	}
	return -1;
}

wxString DataManager::CheckNewName(const wxString &name, int type)
{
	wxString result = name;

	int i;
	for (i=1; CheckNames(result, type); i++)
		result = name+wxString::Format("_%d", i);

	return result;
}

bool DataManager::CheckNames(const wxString &str, int type)
{
	bool result = false;

	switch(type)
	{
	case DATA_VOLUME:
		for (unsigned int i=0; i<m_vd_list.size(); i++)
		{
			VolumeData* vd = m_vd_list[i];
			if (vd && vd->GetName()==str)
			{
				result = true;
				break;
			}
		}
		break;

	case DATA_MESH:
		for (unsigned int i=0; i<m_md_list.size(); i++)
		{
			MeshData* md = m_md_list[i];
			if (md && md->GetName()==str)
			{
				result = true;
				break;
			}
		}
		break;

	case DATA_ANNOTATIONS:
		for (unsigned int i=0; i<m_annotation_list.size(); i++)
		{
			Annotations* ann = m_annotation_list[i];
			if (ann && ann->GetName()==str)
			{
				result = true;
				break;
			}
		}
		break;
	}

	return result;
}

bool DataManager::CheckNames(const wxString &str)
{
	bool result = false;
	for (unsigned int i=0; i<m_vd_list.size(); i++)
	{
		VolumeData* vd = m_vd_list[i];
		if (vd && vd->GetName()==str)
		{
			result = true;
			break;
		}
	}
	if (!result)
	{
		for (unsigned int i=0; i<m_md_list.size(); i++)
		{
			MeshData* md = m_md_list[i];
			if (md && md->GetName()==str)
			{
				result = true;
				break;
			}
		}
	}
	if (!result)
	{
		for (unsigned int i=0; i<m_annotation_list.size(); i++)
		{
			Annotations* ann = m_annotation_list[i];
			if (ann && ann->GetName()==str)
			{
				result = true;
				break;
			}
		}
	}
	return result;
}

void DataManager::SetWavelengthColor(int c1, int c2, int c3, int c4)
{
	switch (c1)
	{
	case 1:
		m_vol_wav[0] = Color(1.0, 0.0, 0.0);
		break;
	case 2:
		m_vol_wav[0] = Color(0.0, 1.0, 0.0);
		break;
	case 3:
		m_vol_wav[0] = Color(0.0, 0.0, 1.0);
		break;
	case 4:
		m_vol_wav[0] = Color(1.0, 0.0, 1.0);
		break;
	case 5:
		m_vol_wav[0] = Color(1.0, 1.0, 1.0);
		break;
	default:
		m_vol_wav[0] = Color(1.0, 1.0, 1.0);
		break;
	}
	switch (c2)
	{
	case 1:
		m_vol_wav[1] = Color(1.0, 0.0, 0.0);
		break;
	case 2:
		m_vol_wav[1] = Color(0.0, 1.0, 0.0);
		break;
	case 3:
		m_vol_wav[1] = Color(0.0, 0.0, 1.0);
		break;
	case 4:
		m_vol_wav[1] = Color(1.0, 0.0, 1.0);
		break;
	case 5:
		m_vol_wav[1] = Color(1.0, 1.0, 1.0);
		break;
	default:
		m_vol_wav[1] = Color(1.0, 1.0, 1.0);
		break;
	}
	switch (c3)
	{
	case 1:
		m_vol_wav[2] = Color(1.0, 0.0, 0.0);
		break;
	case 2:
		m_vol_wav[2] = Color(0.0, 1.0, 0.0);
		break;
	case 3:
		m_vol_wav[2] = Color(0.0, 0.0, 1.0);
		break;
	case 4:
		m_vol_wav[2] = Color(1.0, 0.0, 1.0);
		break;
	case 5:
		m_vol_wav[2] = Color(1.0, 1.0, 1.0);
		break;
	default:
		m_vol_wav[2] = Color(1.0, 1.0, 1.0);
		break;
	}
	switch (c4)
	{
	case 1:
		m_vol_wav[3] = Color(1.0, 0.0, 0.0);
		break;
	case 2:
		m_vol_wav[3] = Color(0.0, 1.0, 0.0);
		break;
	case 3:
		m_vol_wav[3] = Color(0.0, 0.0, 1.0);
		break;
	case 4:
		m_vol_wav[3] = Color(1.0, 0.0, 1.0);
		break;
	case 5:
		m_vol_wav[3] = Color(1.0, 1.0, 1.0);
		break;
	default:
		m_vol_wav[0] = Color(1.0, 1.0, 1.0);
		break;
	}
}

Color DataManager::GetWavelengthColor(double wavelength)
{
	if (wavelength < 340.0)
		return Color(1.0, 1.0, 1.0);
	else if (wavelength < 440.0)
		return m_vol_wav[0];
	else if (wavelength < 500.0)
		return m_vol_wav[1];
	else if (wavelength < 600.0)
		return m_vol_wav[2];
	else if (wavelength < 750.0)
		return m_vol_wav[3];
	else
		return Color(1.0, 1.0, 1.0);
}




/////////////////////////////////////////////////////////////////////////

VolumeDecompressorThread::VolumeDecompressorThread(VolumeLoader *vl)
	: wxThread(wxTHREAD_DETACHED), m_vl(vl)
{
	wxCriticalSectionLocker enter(*m_vl->ms_pThreadCS);
	m_vl->m_running_decomp_th++;
}

VolumeDecompressorThread::~VolumeDecompressorThread()
{
	wxCriticalSectionLocker enter(*m_vl->ms_pThreadCS);
	// the thread is being destroyed; make sure not to leave dangling pointers around
	m_vl->m_running_decomp_th--;
}

wxThread::ExitCode VolumeDecompressorThread::Entry()
{
	unsigned int st_time = GET_TICK_COUNT();

	if (!m_vl->ms_pThreadCS)
		return (wxThread::ExitCode)0;

	while(1)
	{
		m_vl->ms_pThreadCS->Enter();

		if (m_vl->m_decomp_queues.size() == 0)
		{
			m_vl->ms_pThreadCS->Leave();
			break;
		}
		VolumeDecompressorData q = m_vl->m_decomp_queues[0];
		m_vl->m_decomp_queues.erase(m_vl->m_decomp_queues.begin());

		m_vl->ms_pThreadCS->Leave();

		if (q.vd->isBrxml())
		{
			size_t bsize = (size_t)(q.b->nx()) * (size_t)(q.b->ny()) * (size_t)(q.b->nz()) * (size_t)(q.b->nb(0));
			char* result = new char[bsize];
			if (TextureBrick::decompress_brick(result, q.in_data, bsize, q.in_size, q.finfo->type, q.b->nx(), q.b->ny(), q.b->nz(), q.b->nb(0), q.finfo->blosc_blocksize_x, q.finfo->blosc_blocksize_y, q.finfo->blosc_blocksize_z, q.finfo->endianness, q.finfo->is_row_major))
			{
				m_vl->ms_pThreadCS->Enter();

				//OutputDebugStringA("decompress: enter\n");

				delete[] q.in_data;
				shared_ptr<VL_Array> sp = make_shared<VL_Array>(result, bsize);
				q.b->set_brkdata(sp);
				q.b->set_loading_state(false);
				m_vl->m_memcached_data[q.finfo->id_string] = sp;
				
				VolumeLoaderData vld;
				vld.brick = q.b;
				vld.datasize = q.datasize;
				vld.finfo = q.finfo;
				vld.mode = q.mode;
				vld.vd = q.vd;
				m_vl->m_loaded[q.b] = vld;

				//OutputDebugStringA("decompress: leave\n");

				m_vl->ms_pThreadCS->Leave();
			}
			else
			{
                m_vl->ms_pThreadCS->Enter();
                
                cerr << "failed to load " << q.finfo->filename << endl;
                
                delete[] q.in_data;
                shared_ptr<VL_Array> sp = make_shared<VL_Array>(result, bsize);
                q.b->set_brkdata(sp);
                q.b->set_loading_state(false);
                m_vl->m_memcached_data[q.finfo->id_string] = sp;
                
                VolumeLoaderData vld;
                vld.brick = q.b;
                vld.datasize = q.datasize;
                vld.finfo = q.finfo;
                vld.mode = q.mode;
                vld.vd = q.vd;
                m_vl->m_loaded[q.b] = vld;
                
                m_vl->ms_pThreadCS->Leave();
                
                /*
				delete[] result;

				m_vl->ms_pThreadCS->Enter();

				delete[] q.in_data;
				m_vl->m_used_memory -= bsize;
				q.b->set_drawn(q.mode, true);
				q.b->set_loading_state(false);

				m_vl->ms_pThreadCS->Leave();
                */
			}
		}
		else
		{
			BaseReader* reader = q.vd->GetReader();
			wstring nrrd_idstring;
			std::wstringstream wss;
			wss << reader->GetPathName() << L" " << q.chid << L" " << q.frameid;
			nrrd_idstring = wss.str();
			Nrrd* nrrd = reader->Convert_ThreadSafe(q.frameid, q.chid, false);
			if (nrrd)
			{
				m_vl->ms_pThreadCS->Enter();
				VolumeLoaderImage tmp;
				tmp.vd = q.vd;
				tmp.chid = q.chid;
				tmp.frameid = q.frameid;
				tmp.vlnrrd = make_shared<VL_Nrrd>(nrrd);
				m_vl->m_used_memory += tmp.vlnrrd->getDatasize();
				m_vl->m_loading_files.erase(nrrd_idstring);
				m_vl->m_loaded_files[nrrd_idstring] = tmp;
				m_vl->ms_pThreadCS->Leave();
			}
			else
			{
				m_vl->ms_pThreadCS->Enter();
				m_vl->m_loading_files.erase(nrrd_idstring);
				m_vl->ms_pThreadCS->Leave();
			}
		}
	}

	return (wxThread::ExitCode)0;
}

/*
wxDEFINE_EVENT(wxEVT_VLTHREAD_COMPLETED, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_VLTHREAD_PAUSED, wxCommandEvent);
*/
VolumeLoaderThread::VolumeLoaderThread(VolumeLoader *vl)
	: wxThread(wxTHREAD_JOINABLE), m_vl(vl)
{

}

VolumeLoaderThread::~VolumeLoaderThread()
{
	wxCriticalSectionLocker enter(*m_vl->ms_pThreadCS);
	if (!m_vl->m_decomp_queues.empty())
	{
		for (int i = 0; i < m_vl->m_decomp_queues.size(); i++)
		{
			if (m_vl->m_decomp_queues[i].vd->isBrxml())
			{
				if (m_vl->m_decomp_queues[i].in_data != NULL)
					delete[] m_vl->m_decomp_queues[i].in_data;
				m_vl->m_used_memory -= m_vl->m_decomp_queues[i].datasize;
			}
			else
			{
				BaseReader* reader = m_vl->m_decomp_queues[i].vd->GetReader();
				if (reader)
				{
					wstring nrrd_idstring;
					std::wstringstream wss;
					wss << reader->GetPathName() << L" " << m_vl->m_decomp_queues[i].chid << L" " << m_vl->m_decomp_queues[i].frameid;
					nrrd_idstring = wss.str();
					m_vl->m_loading_files.erase(nrrd_idstring);
				}
			}
		}
		m_vl->m_decomp_queues.clear();
	}
	// the thread is being destroyed; make sure not to leave dangling pointers around
}

wxThread::ExitCode VolumeLoaderThread::Entry()
{
	unsigned int st_time = GET_TICK_COUNT();

	while(m_vl->m_running_decomp_th > 0)
	{
		if (TestDestroy())
			return (wxThread::ExitCode)0;
		Sleep(10);
	}
	
	
	m_vl->ms_pThreadCS->Enter();

	//OutputDebugStringA("vl cleaning loading brick: enter\n");

	auto ite = m_vl->m_loaded.begin();
	while(ite != m_vl->m_loaded.end())
	{
		if (!ite->second.brick->isLoaded() && ite->second.brick->isLoading())
		{
			ite->second.brick->set_loading_state(false);
			ite = m_vl->m_loaded.erase(ite);
		}
		else
			ite++;
	}
	
	//OutputDebugStringA("vl cleaning loading brick: leave\n");

	m_vl->ms_pThreadCS->Leave();

	long long submitted_frame_size = 0;
	auto it = m_vl->m_queues.begin();
	while (it != m_vl->m_queues.end())
	{
		if (!it->vd->isBrxml() && it->vd && it->vd->GetReader())
		{
			submitted_frame_size += it->vd->GetDataSize();
			if (submitted_frame_size >= m_vl->m_memory_limit)
				it = m_vl->m_queues.erase(it);
			else
				it++;
		}
		else
			it++;
	}

	while(1)
	{
		if (TestDestroy())
			break;

		m_vl->ms_pThreadCS->Enter();
		if (m_vl->m_queues.size() == 0)
		{
			m_vl->ms_pThreadCS->Leave();
			break;
		}
		VolumeLoaderData b = m_vl->m_queues[0];

		if (b.vd->isBrxml())
		{
			b.brick->set_loading_state(false);
			m_vl->m_queues.erase(m_vl->m_queues.begin());
			m_vl->m_queued.push_back(b);
			m_vl->ms_pThreadCS->Leave();

			if (!b.brick->isLoaded() && !b.brick->isLoading() && b.finfo)
			{
				wstring id = b.finfo->id_string;
				if (m_vl->m_memcached_data.find(b.finfo->id_string) != m_vl->m_memcached_data.end())
				{
					m_vl->ms_pThreadCS->Enter();
					b.brick->set_brkdata(m_vl->m_memcached_data[b.finfo->id_string]);
					m_vl->m_loaded[b.brick] = b;
					m_vl->ms_pThreadCS->Leave();
					continue;
				}

				if (m_vl->m_used_memory >= m_vl->m_memory_limit || m_vl->m_used_memory < 0)
				{
					m_vl->ms_pThreadCS->Enter();
					//OutputDebugStringA("CleanupLoadedBrick: enter\n");
					while (1)
					{
						m_vl->CleanupLoadedBrick();
						if (m_vl->m_used_memory < m_vl->m_memory_limit || TestDestroy())
							break;
						m_vl->ms_pThreadCS->Leave();
						Sleep(10);
						m_vl->ms_pThreadCS->Enter();
					}
					//OutputDebugStringA("CleanupLoadedBrick: leave\n");
					m_vl->ms_pThreadCS->Leave();
				}

				char* ptr = NULL;
				size_t readsize;
				TextureBrick::read_brick_without_decomp(ptr, readsize, b.finfo, this);
				if (!ptr)
                {
                    b.finfo->isvalid = false;
					continue;
                }

				if (b.finfo->type == BRICK_FILE_TYPE_RAW)
				{
					m_vl->ms_pThreadCS->Enter();
					shared_ptr<VL_Array> sp = make_shared<VL_Array>(ptr, readsize);
					b.brick->set_brkdata(sp);
					b.datasize = readsize;
					m_vl->AddLoadedBrick(b);
					m_vl->ms_pThreadCS->Leave();
				}
				else
				{
					bool decomp_in_this_thread = false;
					VolumeDecompressorData dq;
					dq.b = b.brick;
					dq.finfo = b.finfo;
					dq.vd = b.vd;
					dq.mode = b.mode;
					dq.in_data = ptr;
					dq.in_size = readsize;

					size_t bsize = (size_t)(b.brick->nx()) * (size_t)(b.brick->ny()) * (size_t)(b.brick->nz()) * (size_t)(b.brick->nb(0));
					b.datasize = bsize;
					dq.datasize = bsize;

					if (m_vl->m_max_decomp_th == 0)
						decomp_in_this_thread = true;
					else if (m_vl->m_max_decomp_th < 0 ||
						m_vl->m_running_decomp_th < m_vl->m_max_decomp_th)
					{
						VolumeDecompressorThread* dthread = new VolumeDecompressorThread(m_vl);
						if (dthread->Create() == wxTHREAD_NO_ERROR)
						{
							m_vl->ms_pThreadCS->Enter();

							//OutputDebugStringA("vl-1: enter\n");

							m_vl->m_decomp_queues.push_back(dq);
							m_vl->m_used_memory += bsize;
							b.brick->set_loading_state(true);
							m_vl->m_loaded[b.brick] = b;

							//OutputDebugStringA("vl-1: leave\n");
							m_vl->ms_pThreadCS->Leave();

							dthread->Run();
						}
						else
						{
							if (m_vl->m_running_decomp_th <= 0)
								decomp_in_this_thread = true;
							else
							{

								m_vl->ms_pThreadCS->Enter();

								//OutputDebugStringA("vl-2: enter\n");

								m_vl->m_decomp_queues.push_back(dq);
								m_vl->m_used_memory += bsize;
								b.brick->set_loading_state(true);
								m_vl->m_loaded[b.brick] = b;

								//OutputDebugStringA("vl-2: leave\n");

								m_vl->ms_pThreadCS->Leave();
							}
						}
					}
					else
					{
						m_vl->ms_pThreadCS->Enter();

						//OutputDebugStringA("vl-3: enter\n");

						m_vl->m_decomp_queues.push_back(dq);
						m_vl->m_used_memory += bsize;
						b.brick->set_loading_state(true);
						m_vl->m_loaded[b.brick] = b;

						//OutputDebugStringA("vl-3: leave\n");

						m_vl->ms_pThreadCS->Leave();
					}

					if (decomp_in_this_thread)
					{
						char* result = new char[bsize];
						if (TextureBrick::decompress_brick(result, dq.in_data, bsize, dq.in_size, dq.finfo->type, dq.b->nx(), dq.b->ny(), dq.b->nz(), dq.b->nb(0), dq.finfo->blosc_blocksize_x, dq.finfo->blosc_blocksize_y, dq.finfo->blosc_blocksize_z))
						{
							m_vl->ms_pThreadCS->Enter();

							//OutputDebugStringA("vl-4: enter\n");

							delete[] dq.in_data;
							shared_ptr<VL_Array> sp = make_shared<VL_Array>(result, bsize);
							b.brick->set_brkdata(sp);
							b.datasize = bsize;
							m_vl->AddLoadedBrick(b);

							//OutputDebugStringA("vl-4: leave\n");

							m_vl->ms_pThreadCS->Leave();
						}
						else
						{
							delete[] result;

							m_vl->ms_pThreadCS->Enter();
							//OutputDebugStringA("vl-5: enter\n");
							delete[] dq.in_data;
							dq.b->set_drawn(dq.mode, true);
							//OutputDebugStringA("vl-5: leave\n");
							m_vl->ms_pThreadCS->Leave();
						}
					}
				}

			}
			else
			{
				size_t bsize = (size_t)(b.brick->nx()) * (size_t)(b.brick->ny()) * (size_t)(b.brick->nz()) * (size_t)(b.brick->nb(0));
				b.datasize = bsize;

				m_vl->ms_pThreadCS->Enter();
				if (m_vl->m_loaded.find(b.brick) != m_vl->m_loaded.end())
					m_vl->m_loaded[b.brick] = b;
				m_vl->ms_pThreadCS->Leave();
			}
		}
		else
		{
			if (!b.vd || !b.vd->GetReader())
			{
				m_vl->ms_pThreadCS->Leave();
				continue;
			}

			m_vl->m_queues.erase(m_vl->m_queues.begin());
			m_vl->m_queued.push_back(b);

			wstring nrrd_idstring;
			std::wstringstream wss;
			wss << b.vd->GetReader()->GetPathName() << L" " << b.chid << L" " << b.frameid;
			nrrd_idstring = wss.str();

			if (m_vl->m_loaded_files.find(nrrd_idstring) != m_vl->m_loaded_files.end() ||
				m_vl->m_loading_files.find(nrrd_idstring) != m_vl->m_loading_files.end())
			{
				m_vl->ms_pThreadCS->Leave();
				continue;
			}
			m_vl->ms_pThreadCS->Leave();

			if (m_vl->m_used_memory >= m_vl->m_memory_limit)
			{
				m_vl->ms_pThreadCS->Enter();
				while (1)
				{
					m_vl->CleanupLoadedBrick();
					if (m_vl->m_used_memory < m_vl->m_memory_limit || TestDestroy())
						break;
					m_vl->ms_pThreadCS->Leave();
					Sleep(10);
					m_vl->ms_pThreadCS->Enter();
				}
				m_vl->ms_pThreadCS->Leave();
			}

			bool read_in_this_thread = false;
			VolumeDecompressorData dq;
			dq.vd = b.vd;
			dq.chid = b.chid;
			dq.frameid = b.frameid;

			size_t bsize = dq.vd->GetDataSize();
			b.datasize = bsize;
			dq.datasize = bsize;

			bool decomp_in_this_thread = false;

			if (m_vl->m_frame_reader_max_decomp_th == 0)
				decomp_in_this_thread = true;
			else if (m_vl->m_frame_reader_max_decomp_th < 0 ||
				m_vl->m_running_decomp_th < m_vl->m_frame_reader_max_decomp_th)
			{
				VolumeDecompressorThread* dthread = new VolumeDecompressorThread(m_vl);
				if (dthread->Create() == wxTHREAD_NO_ERROR)
				{
					m_vl->ms_pThreadCS->Enter();
					m_vl->m_decomp_queues.push_back(dq);
					m_vl->m_loading_files.insert(nrrd_idstring);
					m_vl->ms_pThreadCS->Leave();

					dthread->Run();
				}
				else
				{
					if (dthread)
						delete dthread;
					dthread = nullptr;
					if (m_vl->m_running_decomp_th <= 0)
						decomp_in_this_thread = true;
					else
					{
						m_vl->ms_pThreadCS->Enter();
						m_vl->m_decomp_queues.push_back(dq);
						m_vl->m_loading_files.insert(nrrd_idstring);
						m_vl->ms_pThreadCS->Leave();
					}
				}
			}
			else
			{
				m_vl->ms_pThreadCS->Enter();
				m_vl->m_decomp_queues.push_back(dq);
				m_vl->m_loading_files.insert(nrrd_idstring);
				m_vl->ms_pThreadCS->Leave();
			}

			if (decomp_in_this_thread)
			{
				BaseReader* reader = b.vd->GetReader();
				Nrrd* nrrd = reader->Convert_ThreadSafe(b.frameid, b.chid, false);
				if (nrrd)
				{
					wstring nrrd_idstring;
					std::wstringstream wss;
					wss << reader->GetPathName() << L" " << b.chid << L" " << b.frameid;
					nrrd_idstring = wss.str();

					m_vl->ms_pThreadCS->Enter();
					VolumeLoaderImage tmp;
					tmp.vd = b.vd;
					tmp.chid = b.chid;
					tmp.frameid = b.frameid;
					tmp.vlnrrd = make_shared<VL_Nrrd>(nrrd);
					m_vl->m_used_memory += tmp.vlnrrd->getDatasize();
					m_vl->m_loaded_files[nrrd_idstring] = tmp;
					m_vl->ms_pThreadCS->Leave();
				}
			}
		}
	}

	/*
	wxCommandEvent evt(wxEVT_VLTHREAD_COMPLETED, GetId());
	VolumeLoaderData *data = new VolumeLoaderData;
	data->m_cur_cat = 0;
	data->m_cur_item = 0;
	data->m_progress = 0;
	evt.SetClientData(data);
	wxPostEvent(m_pParent, evt);
	*/
	return (wxThread::ExitCode)0;
}

wxCriticalSection* VolumeLoader::ms_pThreadCS = nullptr;

VolumeLoader::VolumeLoader()
{
	m_thread = NULL;
	m_running_decomp_th = 0;
	m_max_decomp_th = wxThread::GetCPUCount()-1;
	if (m_max_decomp_th < 0)
		m_max_decomp_th = -1;
	m_frame_reader_max_decomp_th = 6;
	m_memory_limit = 10000000LL;
	m_used_memory = 0LL;
}

VolumeLoader::~VolumeLoader()
{
	if (m_thread)
	{
		if (m_thread->IsAlive())
		{
			m_thread->Delete();
			if (m_thread->IsAlive()) m_thread->Wait();
		}
		delete m_thread;
	}
	RemoveAllLoadedBrick();
}

void VolumeLoader::Queue(VolumeLoaderData brick)
{
	wxCriticalSectionLocker enter(*ms_pThreadCS);
	m_queues.push_back(brick);
}

void VolumeLoader::ClearQueues()
{
	if (!m_queues.empty())
	{
		Abort();
		m_queues.clear();
	}
}

void VolumeLoader::Set(vector<VolumeLoaderData> vld)
{
	Abort();
	//StopAll();
	m_queues = vld;
}

void VolumeLoader::Abort()
{
	if (m_thread)
	{
		if (m_thread->IsAlive())
		{
			m_thread->Delete();
			if (m_thread->IsAlive()) m_thread->Wait();
		}
		delete m_thread;
		m_thread = NULL;
	}
}

void VolumeLoader::StopAll()
{
	Abort();

	while(m_running_decomp_th > 0)
	{
		wxMilliSleep(10);
	}
}

void VolumeLoader::Join()
{
	while(m_thread->IsRunning() || m_running_decomp_th > 0)
	{
		wxMilliSleep(10);
	}
}

bool VolumeLoader::Run()
{
	Abort();
	//StopAll();
	if (!m_queued.empty())
		m_queued.clear();

	m_thread = new VolumeLoaderThread(this);
	if (m_thread->Create() != wxTHREAD_NO_ERROR)
	{
		delete m_thread;
		m_thread = NULL;
		return false;
	}
	m_thread->Run();

	return true;
}

void VolumeLoader::CheckMemoryCache()
{
	for(int i = 0; i < m_queues.size(); i++)
	{
		TextureBrick *b = m_queues[i].brick;
		if (!b->isLoaded())
		{
			if (m_memcached_data.find(m_queues[i].finfo->id_string) != m_memcached_data.end())
			{
				b->set_brkdata(m_memcached_data[m_queues[i].finfo->id_string]);
				m_loaded[b] = m_queues[i];
			}
		}
	}
}

void VolumeLoader::CleanupLoadedBrick()
{
	long long required = 0;
	long long b_required = 0;

	for(int i = 0; i < m_queues.size(); i++)
	{
		TextureBrick *b = m_queues[i].brick;
		if (b && !b->isLoaded())
		{
			wstring id = m_queues[i].finfo->id_string;
			if (m_memcached_data.find(m_queues[i].finfo->id_string) != m_memcached_data.end())
			{
				b->set_brkdata(m_memcached_data[m_queues[i].finfo->id_string]);
				m_loaded[b] = m_queues[i];
			}
			else
				b_required += (size_t)b->nx() * (size_t)b->ny() * (size_t)b->nz() * (size_t)b->nb(0);
		}
		else if (m_queues[i].vd)
		{
			if (!IsNrrdLoaded(m_queues[i].vd, m_queues[i].chid, m_queues[i].frameid))
				required += m_queues[i].vd->GetDataSize();
		}
	}

	required += b_required;

	if (required > 3LL*1024LL*1024LL*1024LL) 
		required = 3LL*1024LL*1024LL*1024LL;

	m_used_memory = 0;
	for (auto& elem : m_memcached_data)
		m_used_memory += elem.second->getSize();
	for (auto& elem : m_loaded_files)
		m_used_memory += elem.second.vlnrrd->getDatasize();

	if (required > 0 || m_used_memory >= m_memory_limit)
	{
		vector<VolumeLoaderImageKey> loaded_image_keys;
		size_t cur_time = 0;
		for (auto& elem : m_loaded_files)
		{
			if (cur_time < elem.second.vd->GetCurTime())
				cur_time = elem.second.vd->GetCurTime();
			VolumeLoaderImageKey k;
			k.frameid = elem.second.frameid;
			k.key = elem.first;
			loaded_image_keys.push_back(k);
		}
		std::sort(loaded_image_keys.begin(), loaded_image_keys.end(), less_vld_frame);

		if (required > 0 || m_used_memory >= m_memory_limit)
		{
			auto it = loaded_image_keys.begin();
			while (it != loaded_image_keys.end() &&
				m_loaded_files[it->key].frameid < cur_time &&
				(required > 0 || m_used_memory >= m_memory_limit))
			{
				if (!m_loaded_files[it->key].vd->GetDisp() && m_loaded_files[it->key].vlnrrd)
				{
					long long datasize = m_loaded_files[it->key].vlnrrd->getDatasize();
					required -= datasize;
					m_used_memory -= datasize;
					m_loaded_files.erase(it->key);
					it = loaded_image_keys.erase(it);
				}
				else
					it++;
			}
		}
		if (required > 0 || m_used_memory >= m_memory_limit)
		{
			auto it = loaded_image_keys.rbegin();
			while (it != loaded_image_keys.rend() &&
				m_loaded_files[it->key].frameid > cur_time &&
				(required > 0 || m_used_memory >= m_memory_limit))
			{
				if (!m_loaded_files[it->key].vd->GetDisp() && m_loaded_files[it->key].vlnrrd)
				{
					long long datasize = m_loaded_files[it->key].vlnrrd->getDatasize();
					required -= datasize;
					m_used_memory -= datasize;
					m_loaded_files.erase(it->key);
					it = decltype(it)(loaded_image_keys.erase(next(it).base()));
				}
				else
					it++;
			}
		}
		if (required > 0 || m_used_memory >= m_memory_limit)
		{
			auto it = loaded_image_keys.begin();
			while (it != loaded_image_keys.end() &&
				m_loaded_files[it->key].frameid < cur_time &&
				(required > 0 || m_used_memory >= m_memory_limit))
			{
				if (m_loaded_files[it->key].vlnrrd)
				{
					long long datasize = m_loaded_files[it->key].vlnrrd->getDatasize();
					required -= datasize;
					m_used_memory -= datasize;
					m_loaded_files.erase(it->key);
					it = loaded_image_keys.erase(it);
				}
				else
					it++;
			}
		}
		if (required > 0 || m_used_memory >= m_memory_limit)
		{
			auto it = loaded_image_keys.rbegin();
			while (it != loaded_image_keys.rend() &&
				m_loaded_files[it->key].frameid > cur_time &&
				(required > 0 || m_used_memory >= m_memory_limit))
			{
				if (m_loaded_files[it->key].vlnrrd)
				{
					long long datasize = m_loaded_files[it->key].vlnrrd->getDatasize();
					required -= datasize;
					b_required -= datasize;
					m_used_memory -= datasize;
					m_loaded_files.erase(it->key);
					it = decltype(it)(loaded_image_keys.erase(next(it).base()));
				}
				else
					it++;
			}
		}
	}

	vector<VolumeLoaderData> b_locked;
	vector<VolumeLoaderData> vd_undisp;
	vector<VolumeLoaderData> b_undisp;
	vector<VolumeLoaderData> b_drawn;
	if (required > 0 || m_used_memory >= m_memory_limit)
	{
		for (auto elem : m_loaded)
		{
			if (elem.second.brick->is_brickdata_locked())
				b_locked.push_back(elem.second);
			else if (!elem.second.vd->GetDisp())
				vd_undisp.push_back(elem.second);
			else if (!elem.second.brick->get_disp())
				b_undisp.push_back(elem.second);
			else if (elem.second.brick->drawn(elem.second.mode))
				b_drawn.push_back(elem.second);
		}

		for (int i = 0; i < vd_undisp.size(); i++)
		{
			if (!vd_undisp[i].brick->isLoaded())
				continue;
			if (vd_undisp[i].brick->getBrickDataSPCount() <= 2)
			{
				required -= vd_undisp[i].datasize;
				m_used_memory -= vd_undisp[i].datasize;
				m_memcached_data[vd_undisp[i].finfo->id_string].reset();
				m_memcached_data.erase(vd_undisp[i].finfo->id_string);
			}
			vd_undisp[i].brick->freeBrkData();
			m_loaded.erase(vd_undisp[i].brick);
			if (required <= 0 && m_used_memory < m_memory_limit)
				break;
		}
	}
	if (required > 0 || m_used_memory >= m_memory_limit)
	{
		for (int i = 0; i < b_undisp.size(); i++)
		{
			if (!b_undisp[i].brick->isLoaded())
				continue;
			if (b_undisp[i].brick->getBrickDataSPCount() <= 2)
			{
				required -= b_undisp[i].datasize;
				m_used_memory -= b_undisp[i].datasize;
				m_memcached_data[b_undisp[i].finfo->id_string].reset();
				m_memcached_data.erase(b_undisp[i].finfo->id_string);
			}
			b_undisp[i].brick->freeBrkData();
			m_loaded.erase(b_undisp[i].brick);
			if (required <= 0 && m_used_memory < m_memory_limit)
				break;
		}
	}
	if (required > 0 || m_used_memory >= m_memory_limit)
	{
		for (int i = 0; i < b_drawn.size(); i++)
		{
			TextureBrick* b = b_drawn[i].brick;

			if (!b->isLoaded())
				continue;

			bool skip = false;
			for (int j = m_queued.size() - 1; j >= 0; j--)
			{
				if (m_queued[j].brick == b && !b->drawn(m_queued[j].mode))
					skip = true;
			}
			if (!skip)
			{
				if (b->getBrickDataSPCount() <= 2)
				{
					required -= b_drawn[i].datasize;
					m_used_memory -= b_drawn[i].datasize;
					m_memcached_data[b_drawn[i].finfo->id_string].reset();
					m_memcached_data.erase(b_drawn[i].finfo->id_string);
				}
				b->freeBrkData();
				m_loaded.erase(b);
			}
			if (required <= 0 && m_used_memory < m_memory_limit)
				break;
		}
	}
	if (m_used_memory >= m_memory_limit)
	{
		for(int i = m_queues.size()-1; i >= 0; i--)
		{
			TextureBrick *b = m_queues[i].brick;
			if (b && b->isLoaded() && !b->is_brickdata_locked() && m_loaded.find(b) != m_loaded.end())
			{
				bool skip = false;
				for (int j = m_queued.size()-1; j >= 0; j--)
				{
					if (m_queued[j].brick == b && !b->drawn(m_queued[j].mode))
						skip = true;
				}
				if (!skip)
				{
					if (b->getBrickDataSPCount() <= 2)
					{
						long long datasize = (size_t)(b->nx()) * (size_t)(b->ny()) * (size_t)(b->nz()) * (size_t)(b->nb(0));
						required -= datasize;
						m_used_memory -= datasize;
						m_memcached_data[m_queues[i].finfo->id_string].reset();
						m_memcached_data.erase(m_queues[i].finfo->id_string);
					}
					else
						int dummy = 0;

					b->freeBrkData();
					m_loaded.erase(b);

					if (m_used_memory < m_memory_limit)
						break;
				}
			}
		}
	}

	if (m_used_memory >= m_memory_limit)
	{
		for (int i = 0; i < b_locked.size(); i++)
		{
			TextureBrick *b = b_locked[i].brick;
			if (!b || !b->isLoaded())
				continue;
			bool skip = false;
			for (int j = m_queued.size()-1; j >= 0; j--)
			{
				if (m_queued[j].brick == b && !b->drawn(m_queued[j].mode))
					skip = true;
			}
			if (!skip)
			{
				if (b->getBrickDataSPCount() <= 2)
				{
					required -= b_locked[i].datasize;
					m_used_memory -= b_locked[i].datasize;
					m_memcached_data[b_locked[i].finfo->id_string].reset();
					m_memcached_data.erase(b_locked[i].finfo->id_string);
				}
				b->freeBrkData();
				m_loaded.erase(b);
				if (m_used_memory < m_memory_limit)
					break;
			}
		}
	}
}

void VolumeLoader::TryToFreeMemory(long long req)
{
	StopAll();

	if (!m_queued.empty())
		m_queued.clear();

	long long available_memory = m_memory_limit - m_used_memory;

	if (available_memory >= req) return;

	long long required = req > 0 ? req-available_memory : m_used_memory;


	vector<VolumeLoaderImageKey> loaded_image_keys;
	size_t cur_time = 0;
	for (auto& elem : m_loaded_files)
	{
		if (cur_time < elem.second.vd->GetCurTime())
			cur_time = elem.second.vd->GetCurTime();
		VolumeLoaderImageKey k;
		k.frameid = elem.second.frameid;
		k.key = elem.first;
		loaded_image_keys.push_back(k);
	}
	std::sort(loaded_image_keys.begin(), loaded_image_keys.end(), less_vld_frame);

	if (required > 0)
	{
		auto it = loaded_image_keys.begin();
		while (it != loaded_image_keys.end() &&
			m_loaded_files[it->key].frameid < cur_time &&
			(required > 0 || m_used_memory >= m_memory_limit))
		{
			if (!m_loaded_files[it->key].vd->GetDisp() && m_loaded_files[it->key].vlnrrd)
			{
				long long datasize = m_loaded_files[it->key].vlnrrd->getDatasize();
				required -= datasize;
				m_used_memory -= datasize;
				m_loaded_files.erase(it->key);
				it = loaded_image_keys.erase(it);
			}
			else
				it++;
		}
	}
	if (required > 0)
	{
		auto it = loaded_image_keys.rbegin();
		while (it != loaded_image_keys.rend() &&
			m_loaded_files[it->key].frameid > cur_time &&
			(required > 0 || m_used_memory >= m_memory_limit))
		{
			if (!m_loaded_files[it->key].vd->GetDisp() && m_loaded_files[it->key].vlnrrd)
			{
				long long datasize = m_loaded_files[it->key].vlnrrd->getDatasize();
				required -= datasize;
				m_used_memory -= datasize;
				m_loaded_files.erase(it->key);
				loaded_image_keys.erase((++it).base());
			}
			else
				it++;
		}
	}
	if (required > 0)
	{
		auto it = loaded_image_keys.begin();
		while (it != loaded_image_keys.end() &&
			m_loaded_files[it->key].frameid < cur_time &&
			(required > 0 || m_used_memory >= m_memory_limit))
		{
			if (m_loaded_files[it->key].vlnrrd)
			{
				long long datasize = m_loaded_files[it->key].vlnrrd->getDatasize();
				required -= datasize;
				m_used_memory -= datasize;
				m_loaded_files.erase(it->key);
				it = loaded_image_keys.erase(it);
			}
			else
				it++;
		}
	}
	if (required > 0)
	{
		auto it = loaded_image_keys.rbegin();
		while (it != loaded_image_keys.rend() &&
			m_loaded_files[it->key].frameid > cur_time &&
			(required > 0 || m_used_memory >= m_memory_limit))
		{
			if (m_loaded_files[it->key].vlnrrd)
			{
				long long datasize = m_loaded_files[it->key].vlnrrd->getDatasize();
				required -= datasize;
				m_used_memory -= datasize;
				m_loaded_files.erase(it->key);
				loaded_image_keys.erase((++it).base());
			}
			else
				it++;
		}
	}

	vector<VolumeLoaderData> b_locked;
	vector<VolumeLoaderData> vd_undisp;
	vector<VolumeLoaderData> b_undisp;
	vector<VolumeLoaderData> b_others;
	
	if (required > 0)
	{
		for (auto elem : m_loaded)
		{
			if (elem.second.brick->is_brickdata_locked())
				b_locked.push_back(elem.second);
			else if (!elem.second.vd->GetDisp())
				vd_undisp.push_back(elem.second);
			else if (!elem.second.brick->get_disp())
				b_undisp.push_back(elem.second);
			else
				b_others.push_back(elem.second);
		}

		for (int i = 0; i < vd_undisp.size(); i++)
		{
			if (!vd_undisp[i].brick->isLoaded())
				continue;
			if (vd_undisp[i].brick->getBrickDataSPCount() <= 2)
			{
				required -= vd_undisp[i].datasize;
				m_used_memory -= vd_undisp[i].datasize;
				m_memcached_data[vd_undisp[i].finfo->id_string].reset();
				m_memcached_data.erase(vd_undisp[i].finfo->id_string);
			}
			vd_undisp[i].brick->freeBrkData();
			m_loaded.erase(vd_undisp[i].brick);
			if (required <= 0)
				break;
		}
	}
	if (required > 0)
	{
		for (int i = 0; i < b_undisp.size(); i++)
		{
			if (!b_undisp[i].brick->isLoaded())
				continue;
			if (b_undisp[i].brick->getBrickDataSPCount() <= 2)
			{
				required -= b_undisp[i].datasize;
				m_used_memory -= b_undisp[i].datasize;
				m_memcached_data[b_undisp[i].finfo->id_string].reset();
				m_memcached_data.erase(b_undisp[i].finfo->id_string);
			}
			b_undisp[i].brick->freeBrkData();
			m_loaded.erase(b_undisp[i].brick);
			if (required <= 0)
				break;
		}
	}
	if (required > 0)
	{
		for (int i = 0; i < b_others.size(); i++)
		{
			if (!b_others[i].brick->isLoaded())
				continue;
			if (b_others[i].brick->getBrickDataSPCount() <= 2)
			{
				required -= b_others[i].datasize;
				m_used_memory -= b_others[i].datasize;
				m_memcached_data[b_others[i].finfo->id_string].reset();
				m_memcached_data.erase(b_others[i].finfo->id_string);
			}
			b_others[i].brick->freeBrkData();
			m_loaded.erase(b_others[i].brick);
			if (required <= 0)
				break;
		}
	}
	
	//something wrong
	if (m_used_memory < 0)
	{
		m_used_memory = 0;
		for(auto &elem : m_memcached_data)
			m_used_memory += elem.second->getSize();
		//cerr << "Volume Loader: error in CleanupLoadedBrick" << endl;
	}

}

void VolumeLoader::RemoveAllLoadedBrick()
{
	StopAll();
	for(auto e : m_loaded)
	{
		if (e.second.brick->isLoaded())
			e.second.brick->freeBrkData();
	}
	for(auto &elem : m_memcached_data)
	{
		m_used_memory -= elem.second->getSize();
		elem.second.reset();
	}
	m_loaded.clear();
	m_memcached_data.clear();
}

void VolumeLoader::RemoveAllLoadedData()
{
	StopAll();

	for (auto& elem : m_loaded_files)
		m_used_memory -= elem.second.vlnrrd->getDatasize();
	m_loaded_files.clear();

	RemoveAllLoadedBrick();
}

void VolumeLoader::RemoveDataVD(VolumeData *vd)
{
	StopAll();
	auto ite = m_loaded.begin();
	while(ite != m_loaded.end())
	{
		if (ite->second.vd == vd && ite->second.brick->isLoaded())
		{
			ite->second.brick->freeBrkData();
			ite = m_loaded.erase(ite);
		}
		else
			ite++;
	}

	auto ite2 = m_memcached_data.begin();
	while (ite2 != m_memcached_data.end())
	{
		if (ite2->second.use_count() == 1)
		{
			m_used_memory -= ite2->second->getSize();
			ite2->second.reset();
			ite2 = m_memcached_data.erase(ite2);
		}
		else
			ite2++;
	}

	auto ite3 = m_loaded_files.begin();
	while (ite3 != m_loaded_files.end())
	{
		if (ite3->second.vd == vd)
		{
			m_used_memory -= ite3->second.vlnrrd->getDatasize();
			ite3 = m_loaded_files.erase(ite3);
		}
		else
			ite3++;
	}
}

void VolumeLoader::PreloadLevel(VolumeData *vd, int lv, bool lock)
{
	//OutputDebugStringA("Preloader enter.\n");

	if (!vd || !vd->isBrxml()) return;
	if (lv < 0 || lv >= vd->GetLevelNum()) return;

	Texture *tex = vd->GetTexture();
	if (!tex) return;

	StopAll();

	int curlevel = vd->GetLevel();

	vd->SetLevel(lv);

	vector<TextureBrick*> *bricks = tex->get_bricks();
	vector<VolumeLoaderData> queues;
	int mode = vd->GetMode() == 1 ? 1 : 0;
	size_t required = 0;
	for (int i = 0; i < bricks->size(); i++)
	{
		VolumeLoaderData d;
		TextureBrick* b = (*bricks)[i];
		b->lock_brickdata(true);
		if (!b->isLoaded())
		{
			d.brick = b;
			d.finfo = tex->GetFileName(b->getID());
			d.vd = vd;
			d.mode = mode;
			queues.push_back(d);
			required += (size_t)b->nx()*(size_t)b->ny()*(size_t)b->nz()*(size_t)b->nb(0);
		}
	}

	if (queues.empty()) return;

	Set(queues);
	long long cur_memlimit = m_memory_limit;
	SetMemoryLimitByte(m_used_memory + required + 100);
	//OutputDebugStringA("Preloader running.\n");
	Run();
	Join();
	//OutputDebugStringA("Preload done.\n");
	SetMemoryLimitByte(cur_memlimit);
}

std::shared_ptr<VL_Nrrd> VolumeLoader::GetLoadedNrrd(VolumeData* vd, int ch, int frame)
{
	wstring nrrd_idstring = GetTimeDataKeyString(vd, ch, frame);

	if (m_loaded_files.find(nrrd_idstring) != m_loaded_files.end())
		return m_loaded_files[nrrd_idstring].vlnrrd;
	
	return nullptr;
}

bool VolumeLoader::IsNrrdLoaded(VolumeData* vd, int ch, int frame)
{
	wstring nrrd_idstring = GetTimeDataKeyString(vd, ch, frame);

	if (m_loaded_files.find(nrrd_idstring) != m_loaded_files.end())
		return true;

	return false;
}

bool VolumeLoader::IsNrrdLoading(VolumeData* vd, int ch, int frame)
{
	wstring nrrd_idstring = GetTimeDataKeyString(vd, ch, frame);

	if (m_loading_files.find(nrrd_idstring) != m_loading_files.end())
		return true;

	return false;
}

wstring VolumeLoader::GetTimeDataKeyString(VolumeData* vd, int ch, int frame)
{
	BaseReader* reader = vd->GetReader();
	wstring nrrd_idstring;
	std::wstringstream wss;
	wss << reader->GetPathName() << L" " << ch << L" " << frame;
	return wss.str();
}

void VolumeLoader::AddLoadedNrrd(const std::shared_ptr<VL_Nrrd> &nrrd, VolumeData* vd, int ch, int frame)
{
	VolumeLoaderImage vli;
	vli.chid = ch;
	vli.frameid = frame;
	vli.vd = vd;
	vli.vlnrrd = nrrd;

	BaseReader* reader = vd->GetReader();
	wstring nrrd_idstring;
	std::wstringstream wss;
	wss << reader->GetPathName() << L" " << ch << L" " << frame;
	nrrd_idstring = wss.str();

	m_loaded_files[nrrd_idstring] = vli;
}

void VolumeLoader::GetPalams(long long &used_mem, int &running_decomp_th, int &queue_num, int &decomp_queue_num)
{
	long long us = 0;
	int ll = 0;
/*	for(auto e : m_loaded)
	{
		if (e.second.brick->isLoaded())
			us += e.second.datasize;
		if (!e.second.brick->get_disp())
			ll++;
	}
*/	used_mem = m_used_memory;
	running_decomp_th = m_running_decomp_th;
	queue_num = m_queues.size();
	decomp_queue_num = m_decomp_queues.size();
}



ProjectDataLoaderThread::ProjectDataLoaderThread(ProjectDataLoader *pdl)
: wxThread(wxTHREAD_DETACHED), m_pdl(pdl)
{
    wxCriticalSectionLocker enter(*m_pdl->ms_pThreadCS);
    m_pdl->m_running_th++;
}

ProjectDataLoaderThread::~ProjectDataLoaderThread()
{
    wxCriticalSectionLocker enter(*m_pdl->ms_pThreadCS);
    // the thread is being destroyed; make sure not to leave dangling pointers around
    m_pdl->m_running_th--;
}

wxThread::ExitCode ProjectDataLoaderThread::Entry()
{
    if (!m_pdl || !m_pdl->m_dm)
        return (wxThread::ExitCode)0;
    
    while(1)
    {
        if (TestDestroy())
            break;
        
        m_pdl->ms_pThreadCS->Enter();
        if (m_pdl->m_queues.size() == 0)
        {
            m_pdl->ms_pThreadCS->Leave();
            break;
        }
        
        wxString filename = m_pdl->m_queues[0].path;
        wxString name = m_pdl->m_queues[0].name;
        wxString metadata = m_pdl->m_queues[0].metadata;
        int ch_num = m_pdl->m_queues[0].ch;
        int t_num = m_pdl->m_queues[0].t;
        bool compression = m_pdl->m_queues[0].compression;
        bool skip_brick = m_pdl->m_queues[0].skip_brick;
        bool slice_seq =m_pdl->m_queues[0].slice_seq;
        bool time_seq = m_pdl->m_queues[0].time_seq;
        wxString time_id = m_pdl->m_queues[0].time_id;
        bool load_mask = m_pdl->m_queues[0].load_mask;
        wxString mskpath = m_pdl->m_queues[0].mskpath;
        wxString lblpath = m_pdl->m_queues[0].lblpath;
        int type = m_pdl->m_queues[0].type;
        
        m_pdl->m_queued.push_back(m_pdl->m_queues[0]);
        m_pdl->m_queues.erase(m_pdl->m_queues.begin());
        
        m_pdl->ms_pThreadCS->Leave();
        
        
        wxString pathname = filename;
        bool isURL = false;
        bool downloaded = false;
        wxString downloaded_filepath;
        bool downloaded_metadata = false;
        wxString downloaded_metadatafilepath;
        
        wxString suffix = filename.Mid(filename.Find('.', true)).MakeLower();
        
        if (!wxFileExists(pathname) && suffix != ".n5fs_ch")
        {
            pathname = m_pdl->m_dm->SearchProjectPath(filename);
            if (!wxFileExists(pathname))
            {
                pathname = filename;
                if (!m_pdl->m_dm->DownloadToCurrentDir(pathname)) continue;
                
                downloaded = true;
                downloaded_filepath = pathname;
                
                if(type == LOAD_TYPE_BRKXML)
                {
                    wxString metadatapath = filename;
                    metadatapath = metadatapath.Mid(0, metadatapath.Find(wxT('/'), true)+1) + wxT("_metadata.xml");
                    //std::remove("_metadata.xml");
                    if (m_pdl->m_dm->DownloadToCurrentDir(metadatapath))
                    {
                        downloaded_metadata = true;
                        downloaded_metadatafilepath = metadatapath;
                    }
                    isURL = true;
                }
            }
        }
        
        if (!mskpath.IsEmpty() && !wxFileExists(mskpath))
            mskpath = m_pdl->m_dm->SearchProjectPath(mskpath);
        if (!lblpath.IsEmpty() && !wxFileExists(lblpath))
            lblpath = m_pdl->m_dm->SearchProjectPath(lblpath);
        
        int i;
        int result = 0;
        BaseReader* reader = 0;
        
        wxString idi_metadata_path;
        int idi_type = 0;
        if (type == LOAD_TYPE_IDI)
        {
            wxString idi_volpath;
            wxFileInputStream ist(pathname);
            if (!ist.IsOk()) return (wxThread::ExitCode)1;
            wxZipInputStream zstream(ist);
            if (!zstream.IsOk()) return (wxThread::ExitCode)1;
            
            wxZipEntry *entry;
            while (entry = zstream.GetNextEntry())
            {
                wxString ename = entry->GetName();
                if (entry->IsDir())
                    continue;
                if (ename.Mid(ename.Find(wxFileName::GetPathSeparator(), true)+1).StartsWith("."))
                    continue;
                wxString ret = (wxStandardPaths::Get().GetTempDir() + wxFileName::GetPathSeparator() + ename).ToStdWstring();
                wxString esuffix = ret.Mid(ret.Find('.', true)).MakeLower();
                if (esuffix == ".nrrd" || esuffix == ".tif" || esuffix == ".xml")
                {
                    if (esuffix == ".nrrd")
                    {
                        idi_volpath = ret;
                        idi_type = LOAD_TYPE_NRRD;
                    }
                    else if (esuffix == ".tif")
                    {
                        idi_volpath = ret;
                        idi_type = LOAD_TYPE_TIFF;
                    }
                    else if (esuffix == ".xml")
                        idi_metadata_path = ret;
                    wxFileOutputStream file(ret);
                    if (!file.IsOk()) return (wxThread::ExitCode)1;
                    file.Write(zstream);
                }
            }
            zstream.CloseEntry();
            if (idi_volpath.IsEmpty() || idi_metadata_path.IsEmpty())
                return (wxThread::ExitCode)1;
            pathname = idi_volpath;
        }
        
        //RGB tiff
        if (type == LOAD_TYPE_TIFF || (type == LOAD_TYPE_IDI && idi_type == LOAD_TYPE_TIFF))
            reader = new TIFReader();
        else if (type == LOAD_TYPE_NRRD || (type == LOAD_TYPE_IDI && idi_type == LOAD_TYPE_NRRD))
            reader = new NRRDReader();
        else if (type == LOAD_TYPE_OIB)
            reader = new OIBReader();
        else if (type == LOAD_TYPE_OIF)
            reader = new OIFReader();
        else if (type == LOAD_TYPE_LSM)
            reader = new LSMReader();
        else if (type == LOAD_TYPE_CZI)
            reader = new CZIReader();
        else if (type == LOAD_TYPE_ND2)
            reader = new ND2Reader();
        else if (type == LOAD_TYPE_PVXML)
            reader = new PVXMLReader();
        else if (type == LOAD_TYPE_BRKXML)
            reader = new BRKXMLReader();
        else if (type == LOAD_TYPE_H5J)
            reader = new H5JReader();
        else if (type == LOAD_TYPE_V3DPBD)
            reader = new V3DPBDReader();
        
        m_pdl->ms_pThreadCS->Enter();
        m_pdl->m_dm->PushReader(reader);
        m_pdl->ms_pThreadCS->Leave();
        
        wstring str_w = pathname.ToStdWstring();
        reader->SetFile(str_w);
        reader->SetSliceSeq(slice_seq);
        reader->SetTimeSeq(time_seq);
        str_w = time_id.ToStdWstring();
        reader->SetTimeId(str_w);
        
        if(isURL && type == LOAD_TYPE_BRKXML)
        {
            str_w = filename.ToStdWstring();
#ifdef _WIN32
            wchar_t slash = L'\\';
#else
            wchar_t slash = L'/';
#endif
            wstring dir_name = str_w.substr(0, str_w.find_last_of(slash)+1);
            ((BRKXMLReader *)reader)->SetDir(str_w);
        }
        
        if (type == LOAD_TYPE_BRKXML)
        {
            wxString suffix = metadata.Mid(metadata.Find('.', true)).MakeLower();
            if (suffix.compare(".xml") == 0)
            {
                BRKXMLReader* br = (BRKXMLReader *)reader;
                br->SetBDVMetadataPath(metadata.ToStdWstring().c_str());
            }
        }
        
        if (type == LOAD_TYPE_H5J)
            m_pdl->ms_pThreadCS->Enter();
        
        reader->Preprocess();
        
        if (type == LOAD_TYPE_H5J)
            m_pdl->ms_pThreadCS->Leave();
        
        if (type == LOAD_TYPE_BRKXML && !((BRKXMLReader *)reader)->GetExMetadataURL().empty())
        {
            wxString metadatapath = ((BRKXMLReader *)reader)->GetExMetadataURL();
            if (m_pdl->m_dm->DownloadToCurrentDir(metadatapath))
            {
                downloaded_metadata = true;
                downloaded_metadatafilepath = metadatapath;
                ((BRKXMLReader *)reader)->loadMetadata(metadatapath.ToStdWstring());
            }
        }
        
        int chan = reader->GetChanNum();
        size_t voxnum = reader->GetXSize() * reader->GetYSize() * reader->GetSliceNum();
        size_t ch_datasize = 0;
        size_t bytes = 0;
        
        double sspcx = -1.0;
        double sspcy = -1.0;
        double sspcz = -1.0;
        
        for (i=(ch_num>=0?ch_num:0);
             i<(ch_num>=0?ch_num+1:chan); i++)
        {
            VolumeData *vd = new VolumeData();
            vd->SetSkipBrick(skip_brick);
            
            if (type == LOAD_TYPE_H5J)
                m_pdl->ms_pThreadCS->Enter();
            
            auto data = reader->Convert(t_num>=0?t_num:reader->GetCurTime(), i, true);
            
            if (type == LOAD_TYPE_H5J)
                m_pdl->ms_pThreadCS->Leave();
            
            /*
            if (datasize > 0)
            {
                if (ch_datasize == 0)
                {
                    switch (data->getNrrd()->type)
                    {
                        case nrrdTypeUChar:
                        case nrrdTypeChar:
                            bytes = 1;
                            break;
                        case nrrdTypeUShort:
                        case nrrdTypeShort:
                            bytes = 2;
                            break;
                        default:
                            bytes = 0;
                    }
                    if (ch_num >= 0)
                        ch_datasize = (datasize * bytes) / (3ULL + bytes);
                    else
                        ch_datasize = (datasize * bytes) / (3ULL + bytes * chan);
                }
                if (ch_datasize > 0 && bytes > 0 && ch_datasize < voxnum * bytes)
                {
                    Nrrd* tmp;
                    tmp = VolumeData::NrrdScale(data->getNrrd(), ch_datasize);
                    if (tmp)
                    {
                        data = make_shared<VL_Nrrd>(tmp);
                        
                        size_t dim = data->getNrrd()->dim;
                        if (dim >= 3)
                        {
                            int offset = 0;
                            if (dim > 3) offset = 1;
                            sspcx = tmp->axis[0 + offset].spacing;
                            sspcy = tmp->axis[1 + offset].spacing;
                            sspcz = tmp->axis[2 + offset].spacing;
                        }
                    }
                }
            }
            */
            
            if (!data)
                continue;
            
            if (name.IsEmpty())
            {
                if (type != LOAD_TYPE_BRKXML)
                {
                    name = wxString(reader->GetDataName());
                    if (chan > 1)
                        name += wxString::Format("_Ch%d", i+1);
                }
                else
                {
                    BRKXMLReader* breader = (BRKXMLReader*)reader;
                    name = reader->GetDataName();
                    name = name.Mid(0, name.find_last_of(wxT('.')));
                    if(ch_num > 1) name = wxT("_Ch") + wxString::Format("%i", i);
                    pathname = filename;
                    breader->SetCurChan(i);
                    breader->SetCurTime(0);
                }
            }
            
            bool valid_spc = reader->IsSpcInfoValid();
            if (vd)
            {
                m_pdl->ms_pThreadCS->Enter();
                bool vd_result = vd->Load(data, name, pathname, (type == LOAD_TYPE_BRKXML) ? (BRKXMLReader*)reader : NULL);
                m_pdl->ms_pThreadCS->Leave();
                if (vd_result)
                {
                    if (load_mask)
                    {
                        //mask
                        MSKReader msk_reader;
                        std::wstring str = mskpath.IsEmpty() ? reader->GetPathName() : mskpath.ToStdWstring();
                        msk_reader.SetFile(str);
                        auto mask = msk_reader.Convert(t_num>=0?t_num:reader->GetCurTime(), i, true);
                        if (mask)
                            vd->LoadMask(mask);
                        //label mask
                        LBLReader lbl_reader;
                        str = lblpath.IsEmpty() ? reader->GetPathName() : lblpath.ToStdWstring();
                        lbl_reader.SetFile(str);
                        
                        auto label = lbl_reader.Convert(t_num>=0?t_num:reader->GetCurTime(), i, true);
                        if (label)
                            vd->LoadLabel(label);
                    }
                    if (type == LOAD_TYPE_BRKXML) ((BRKXMLReader*)reader)->SetLevel(0);
                    //for 2D data
                    int xres, yres, zres;
                    vd->GetResolution(xres, yres, zres);
                    double zspcfac = (double)Max(xres,yres)/256.0;
                    if (zspcfac < 1.0) zspcfac = 1.0;
                    if (zres == 1) vd->SetBaseSpacings(reader->GetXSpc(), reader->GetYSpc(), reader->GetXSpc()*zspcfac);
                    else if (sspcx > 0.0 && sspcy > 0.0 && sspcz > 0.0) vd->SetBaseSpacings(sspcx, sspcy, sspcz);
                    else vd->SetBaseSpacings(reader->GetXSpc(), reader->GetYSpc(), reader->GetZSpc());
                    vd->SetSpcFromFile(valid_spc);
                    vd->SetScalarScale(reader->GetScalarScale());
                    vd->SetMaxValue(reader->GetMaxValue());
                    vd->SetCurTime(reader->GetCurTime());
                    vd->SetCurChannel(i);
                    //++
                    result++;
                }
                else
                {
                    delete vd;
                    continue;
                }
            }
            else
                continue;
            
            vd->SetReader(reader);
            vd->SetCompression(compression);
            
            m_pdl->ms_pThreadCS->Enter();
            m_pdl->m_dm->AddVolumeData(vd);
            m_pdl->ms_pThreadCS->Leave();
            
            m_pdl->m_dm->SetVolumeDefault(vd);
            /*
            if (type == LOAD_TYPE_TIFF)
            {
                double minval, maxval, vxmax;
                ((TIFReader*)reader)->GetDisplayRange(i, minval, maxval);
                vxmax = vd->GetMaxValue();
                if (minval >= 0)
                    vd->SetLeftThresh(minval/vxmax);
                if (maxval >= 0)
                    vd->SetOffset(maxval/vxmax);
            }
             */
            
            if (type == LOAD_TYPE_BRKXML && vd->GetMaxValue() == 65535)
            {
                vd->SetOffset(4096.0/65535.0);
            }
            
            //get excitation wavelength
            double wavelength = reader->GetExcitationWavelength(i);
            if (wavelength > 0.0) {
                FLIVR::Color col = m_pdl->m_dm->GetWavelengthColor(wavelength);
                vd->SetColor(col);
            }
            else if (wavelength < 0.) {
                FLIVR::Color white = Color(1.0, 1.0, 1.0);
                vd->SetColor(white);
            }
            else
            {
                FLIVR::Color white = Color(1.0, 1.0, 1.0);
                FLIVR::Color red   = Color(1.0, 0.0, 0.0);
                FLIVR::Color green = Color(0.0, 1.0, 0.0);
                FLIVR::Color blue  = Color(0.0, 0.0, 1.0);
                if (chan == 1) {
                    vd->SetColor(white);
                }
                else
                {
                    if (i == 0)
                        vd->SetColor(red);
                    else if (i == 1)
                        vd->SetColor(green);
                    else if (i == 2)
                        vd->SetColor(blue);
                    else
                        vd->SetColor(white);
                }
            }
            if (type == LOAD_TYPE_IDI)
            {
                vd->ImportROITreeXML(idi_metadata_path.ToStdWstring());
                if (!vd->ExportROITree().empty())
                {
                    vd->SetColormapMode(3);
                    vd->SelectAllNamedROI();
                    vd->SetIDColDispMode(2);
                    vd->SetAlpha(0.5);
                }
            }
            
            if (type == LOAD_TYPE_BRKXML)
            {
                wxString msuffix = metadata.Mid(metadata.Find('.', true)).MakeLower();
                if (msuffix.compare(".xml") == 0)
                {
                    BRKXMLReader* br = (BRKXMLReader *)reader;
                    Transform bdv_tranform;
                    if (suffix == ".n5fs_ch")
                        bdv_tranform = br->GetBDVTransform();
                    else
                        bdv_tranform = br->GetBDVTransform(i, t_num>=0?t_num:reader->GetCurTime());
                    Texture* tex = vd->GetTexture();
                    if (tex)
                    {
                        Transform* tform = tex->transform();
                        tform->post_trans(bdv_tranform);
                        tex->set_additional_transform(bdv_tranform);
                    }
                }
            }
            vd->SetMetadataPath(metadata);
        }
        
        if (downloaded)
        {
            m_pdl->ms_pThreadCS->Enter();
            wxRemoveFile(downloaded_filepath);
            m_pdl->ms_pThreadCS->Leave();
        }

        if (downloaded_metadata)
        {
            m_pdl->ms_pThreadCS->Enter();
            wxRemoveFile(downloaded_metadatafilepath);
            m_pdl->ms_pThreadCS->Leave();
        }
        
        m_pdl->m_progress++;
    }
    
    return (wxThread::ExitCode)0;
}

wxCriticalSection* ProjectDataLoader::ms_pThreadCS = nullptr;

ProjectDataLoader::ProjectDataLoader()
{
    m_max_th = wxThread::GetCPUCount()-1;
    if (m_max_th < 0)
        m_max_th = -1;
    m_running_th = 0;
}

ProjectDataLoader::~ProjectDataLoader()
{
    ClearQueues();
    Join();
}

void ProjectDataLoader::Queue(ProjectDataLoaderQueue path)
{
	if (!ms_pThreadCS) return;
    wxCriticalSectionLocker enter(*ms_pThreadCS);
    m_queues.push_back(path);
}

void ProjectDataLoader::ClearQueues()
{
	if (!ms_pThreadCS) return;
    wxCriticalSectionLocker enter(*ms_pThreadCS);
    if (!m_queues.empty())
    {
        m_queues.clear();
    }
}

void ProjectDataLoader::Set(vector<ProjectDataLoaderQueue> &queues)
{
    Join();
    m_queues = queues;
}

void ProjectDataLoader::Join()
{
    while(m_running_th > 0)
    {
        wxMilliSleep(10);
    }
    m_running_th = 0;
}

bool ProjectDataLoader::Run()
{
    Join();
    if (!m_queued.empty())
        m_queued.clear();
    
    m_queue_count = m_queues.size();
    m_progress = 0;
    
    for (int i = 0; i < m_max_th; i++)
    {
        ProjectDataLoaderThread* th = new ProjectDataLoaderThread(this);
        if (th->Create() == wxTHREAD_NO_ERROR)
        {
            th->Run();
        }
    }
    
    return true;
}



EmptyBlockDetectorThread::EmptyBlockDetectorThread(EmptyBlockDetector *ebd)
: wxThread(wxTHREAD_DETACHED), m_ebd(ebd)
{
    wxCriticalSectionLocker enter(*m_ebd->ms_pThreadCS);
    m_ebd->m_running_th++;
}

EmptyBlockDetectorThread::~EmptyBlockDetectorThread()
{
    wxCriticalSectionLocker enter(*m_ebd->ms_pThreadCS);
    // the thread is being destroyed; make sure not to leave dangling pointers around
    m_ebd->m_running_th--;
}

wxThread::ExitCode EmptyBlockDetectorThread::Entry()
{
    if (!m_ebd)
        return (wxThread::ExitCode)0;
    
    while(1)
    {
        m_ebd->ms_pThreadCS->Enter();
        
        if (m_ebd->m_queues.size() == 0)
        {
            m_ebd->ms_pThreadCS->Leave();
            break;
        }
        
        TextureBrick* b = m_ebd->m_queues[0].b;
        int c = m_ebd->m_queues[0].c;
        auto nrrd = m_ebd->m_queues[0].nrrd;
        auto msknrrd = m_ebd->m_queues[0].msknrrd;
        
        m_ebd->m_queued.push_back(m_ebd->m_queues[0]);
        m_ebd->m_queues.erase(m_ebd->m_queues.begin());
        
        m_ebd->ms_pThreadCS->Leave();
        
        if (!b->modified(c))
            continue;
        
        if (!nrrd || !nrrd->getNrrd())
            continue;
        
        size_t dim = nrrd->getNrrd()->dim;
        size_t size[3] = {};
        
        int offset = 0;
        if (dim > 3) offset = 1;
        
        for (size_t p = 0; p < 3; p++)
            size[p] = (size_t)nrrd->getNrrd()->axis[p + offset].size;
        
        size_t ox = b->ox();
        size_t oy = b->oy();
        size_t oz = b->oz();
        size_t nx = b->nx();
        size_t ny = b->ny();
        size_t nz = b->nz();
        size_t nb = b->nb(c);
        
        if (nrrd->getNrrd()->type == nrrdTypeChar ||
            nrrd->getNrrd()->type == nrrdTypeUChar)
            nb = 1;
        else
            nb = 2;
        
        size_t ypitch = size[0];
        size_t zpitch = size[1] * size[0];
        
        if (msknrrd)
        {
            size_t msk_dim = msknrrd->getNrrd()->dim;
            size_t msk_size[3] = {};
            
            int msk_offset = 0;
            if (msk_dim > 3) msk_offset = 1;
            
            for (size_t p = 0; p < 3; p++)
                msk_size[p] = (size_t)msknrrd->getNrrd()->axis[p + msk_offset].size;
            
            double xscale = (double)msk_size[0] / size[0];
            double yscale = (double)msk_size[1] / size[1];
            double zscale = (double)msk_size[2] / size[2];
            
            ox = (size_t)floor((double)b->ox() * xscale);
            oy = (size_t)floor((double)b->oy() * yscale);
            oz = (size_t)floor((double)b->oz() * zscale);
            
            double endx_d = (double)(b->ox() + b->nx()) * xscale;
            double endy_d = (double)(b->oy() + b->ny()) * yscale;
            double endz_d = (double)(b->oz() + b->nz()) * zscale;
            nx = (size_t)ceil(endx_d) - ox;
            ny = (size_t)ceil(endy_d) - oy;
            nz = (size_t)ceil(endz_d) - oz;

			nx = ox + nx > msk_size[0] ? msk_size[0] - ox : nx;
			ny = oy + ny > msk_size[1] ? msk_size[1] - oy : ny;
			nz = oz + nz > msk_size[2] ? msk_size[2] - oz : nz;
            
            if (msknrrd->getNrrd()->type == nrrdTypeChar ||
                msknrrd->getNrrd()->type == nrrdTypeUChar)
                nb = 1;
            else
                nb = 2;
            
            ypitch = msk_size[0];
            zpitch = msk_size[1] * msk_size[0];
            
            nrrd = msknrrd;
        }

        bool isempty = true;
        if (nb == 1)
        {
            unsigned char *data = (unsigned char *)(nrrd->getNrrd()->data) + oz*zpitch + oy*ypitch + ox;
            for (int zz = 0; zz < nz && isempty; zz++)
            {
                unsigned char *line = data;
                for (int yy = 0; yy < ny && isempty; yy++)
                {
                    for (int xx = 0; xx < nx && isempty; xx++)
                    {
                        if (line[xx] != 0)
                            isempty = false;
                    }
                    line += ypitch;
                }
                data += zpitch;
            }
        }
        else if (nb == 2)
        {
            unsigned short *data = (unsigned short *)(nrrd->getNrrd()->data) + oz*zpitch + oy*ypitch + ox;
            for (int zz = 0; zz < nz && isempty; zz++)
            {
                unsigned short *line = data;
                for (int yy = 0; yy < ny && isempty; yy++)
                {
                    for (int xx = 0; xx < nx && isempty; xx++)
                    {
                        if (line[xx] != 0)
                            isempty = false;
                    }
                    line += ypitch;
                }
                data += zpitch;
            }
        }
        
		m_ebd->ms_pThreadCS->Enter();
        b->set_skip(c, isempty);
        b->set_modified(c, false);
        m_ebd->ms_pThreadCS->Leave();
    }
    
    return (wxThread::ExitCode)0;
}

wxCriticalSection* EmptyBlockDetector::ms_pThreadCS = nullptr;

EmptyBlockDetector::EmptyBlockDetector()
{
    m_max_th = wxThread::GetCPUCount()-1;
    if (m_max_th < 0)
        m_max_th = -1;
    m_running_th = 0;
}

EmptyBlockDetector::~EmptyBlockDetector()
{
    ClearQueues();
    Join();
}

void EmptyBlockDetector::Queue(EmptyBlockDetectorQueue path)
{
    if (!ms_pThreadCS) return;
    wxCriticalSectionLocker enter(*ms_pThreadCS);
    m_queues.push_back(path);
}

void EmptyBlockDetector::ClearQueues()
{
    if (!ms_pThreadCS) return;
    wxCriticalSectionLocker enter(*ms_pThreadCS);
    if (!m_queues.empty())
    {
        m_queues.clear();
    }
}

void EmptyBlockDetector::Set(vector<EmptyBlockDetectorQueue> &queues)
{
    ms_pThreadCS->Enter();
    m_queues.clear();
    ms_pThreadCS->Leave();
    Join();
    m_queues = queues;
}

void EmptyBlockDetector::Join()
{
    while(m_running_th > 0)
    {
        wxMilliSleep(10);
    }
    m_running_th = 0;
}

bool EmptyBlockDetector::Run()
{
    Join();
    if (!m_queued.empty())
        m_queued.clear();
    
    m_queue_count = m_queues.size();
    m_progress = 0;
    
    for (int i = 0; i < m_max_th; i++)
    {
        EmptyBlockDetectorThread* th = new EmptyBlockDetectorThread(this);
        if (th->Create() == wxTHREAD_NO_ERROR)
        {
            th->Run();
        }
    }
    
    return true;
}

