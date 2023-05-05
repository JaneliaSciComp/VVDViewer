#include "stdwx.h"
#include "NeuronAnnotatorPlugin.h"
#include "NeuronAnnotatorPluginWindow.h"
#include <wx/process.h>
#include <wx/mstream.h>
#include <wx/filename.h>
#include <wx/zipstrm.h>
#include <wx/tokenzr.h>
#include <wx/cmdline.h>
#include "compatibility.h"

MIPGeneratorThread::MIPGeneratorThread(NAGuiPlugin* plugin, const vector<int> &queue) : wxThread(wxTHREAD_DETACHED)
{
	m_plugin = NULL;

	if (plugin)
		m_plugin = plugin;
	m_queue = queue;
}

MIPGeneratorThread::~MIPGeneratorThread()
{
	wxCriticalSectionLocker enter(m_plugin->m_pThreadCS);
	m_plugin->m_running_mip_th--;
}

wxThread::ExitCode MIPGeneratorThread::Entry()
{
	if (!m_plugin || !m_plugin->m_nrrd_s[0])
		return (wxThread::ExitCode)0;

	m_plugin->m_running_mip_th++;

	int dim_offset = 0;
	if (m_plugin->m_nrrd_s[0]->getNrrd()->dim > 3) dim_offset = 1;
	int nx = m_plugin->m_nrrd_s[0]->getNrrd()->axis[dim_offset + 0].size;
	int ny = m_plugin->m_nrrd_s[0]->getNrrd()->axis[dim_offset + 1].size;
	int nz = m_plugin->m_nrrd_s[0]->getNrrd()->axis[dim_offset + 2].size;

	int mx = 300;
	int my = (int)((double)mx * ny / nx);
	double dx = (double)nx / mx;
	double dy = (double)ny / my;

	for (auto s : m_queue)
	{ 
		unsigned char* temp = (unsigned char*)malloc((size_t)mx * my * 3 * sizeof(unsigned char));
		memset(temp, 0, (size_t)mx * my * 3 * sizeof(unsigned char));
		BBox bbox = m_plugin->m_segs[s].bbox;
		int sx = (int)(bbox.min().x() / dx);
		int sy = (int)(bbox.min().y() / dy);
		int ex = (int)(bbox.max().x() / dx);
		int ey = (int)(bbox.max().y() / dy);
		for (int i = sx; i <= ex; i++)
		{
			for (int j = sy; j <= ey; j++)
			{
				size_t xx = (size_t)(i * dx);
				size_t yy = (size_t)(j * dy);
				double maxvals[3] = {};
				for (int c = 0; c < m_plugin->m_scount; c++)
				{
					for (int k = int(bbox.min().z()); k <= int(bbox.max().z()); k++)
					{
						size_t index = (size_t)nx * ny * k + nx * yy + xx;
						int label = 0;
						if (m_plugin->m_lbl_nrrd->getNrrd()->type == nrrdTypeUChar)
							label = ((unsigned char*)m_plugin->m_lbl_nrrd->getNrrd()->data)[index];
						else if (m_plugin->m_lbl_nrrd->getNrrd()->type == nrrdTypeUShort)
							label = ((unsigned short*)m_plugin->m_lbl_nrrd->getNrrd()->data)[index];
						if (m_plugin->m_segs[s].id == label)
						{
							double val = 0.0;
							if (m_plugin->m_nrrd_s[c]->getNrrd()->type == nrrdTypeUChar)
								val = ((unsigned char*)m_plugin->m_nrrd_s[c]->getNrrd()->data)[index];
							else if (m_plugin->m_nrrd_s[c]->getNrrd()->type == nrrdTypeUShort)
								val = ((unsigned short*)m_plugin->m_nrrd_s[c]->getNrrd()->data)[index];
							if (val > maxvals[c])
								maxvals[c] = val;
						}
					}
					if (m_plugin->m_nrrd_s[c]->getNrrd()->type == nrrdTypeUChar)
						temp[(size_t)mx * 3 * j + 3 * i + c] = (unsigned char)maxvals[c];
					else if (m_plugin->m_nrrd_s[c]->getNrrd()->type == nrrdTypeUShort)
						temp[(size_t)mx * 3 * j + 3 * i + c] = (unsigned char)(maxvals[c] / m_plugin->m_gmaxvals[c] * 255.0);
				}
			}
		}
		m_plugin->m_segs[s].image.Create(mx, my, temp);
		m_plugin->m_segs[s].thumbnail = m_plugin->m_segs[s].image.Scale(300, (int)((double)m_plugin->m_segs[s].image.GetHeight() / (double)m_plugin->m_segs[s].image.GetWidth() * 300.0), wxIMAGE_QUALITY_HIGH);
	}

	return (wxThread::ExitCode)0;
}


IMPLEMENT_DYNAMIC_CLASS(NAGuiPlugin, wxObject)

NAGuiPlugin::NAGuiPlugin()
	: wxGuiPluginBase(NULL, NULL)
{
	m_lbl_reader = NULL;
	m_vol_reader = NULL;
	m_lbl_nrrd = NULL;
	m_nrrd_r = NULL;
	m_nrrd_s[0] = m_nrrd_s[1] = m_nrrd_s[2] = NULL;
	m_scount = 0;
	m_dirty = false;
    m_reload_list = false;
	m_xspc = -1.0;
	m_yspc = -1.0;
	m_zspc = -1.0;
	m_allsig_visible = true;
	m_lock = true;
}

NAGuiPlugin::NAGuiPlugin(wxEvtHandler * handler, wxWindow * vvd)
	: wxGuiPluginBase(handler, vvd)
{

}

NAGuiPlugin::~NAGuiPlugin()
{
/*
	if (m_lbl_nrrd)
	{
		delete[] m_lbl_nrrd->data;
		nrrdNix(m_lbl_nrrd);
		m_lbl_nrrd = NULL;
	}
	if (m_nrrd_r)
	{
		delete[] m_nrrd_r->data;
		nrrdNix(m_nrrd_r);
		m_nrrd_r = NULL;
	}
	for (int i = 0; i < 3; i++)
	{
		if (m_nrrd_s[i])
		{
			delete[] m_nrrd_s[i]->data;
			nrrdNix(m_nrrd_s[i]);
		}
		m_nrrd_s[i] = NULL;
	}
 */
}

bool NAGuiPlugin::runNALoader()
{

	return runNALoader(m_id_path, m_vol_path, wxT("sssr"));
}

bool NAGuiPlugin::runNALoader(wxString id_path, wxString vol_path, wxString chspec, wxString spacings, wxString prefix, bool set_dirty)
{
	if (!wxFileExists(id_path) || !wxFileExists(vol_path))
		return false;

	VRenderFrame* vframe = (VRenderFrame*)m_vvd;
	if (!vframe) return false;
	DataManager* dm = vframe->GetDataManager();
	if (!dm) return false;
	VRenderView* vrv = vframe->GetView(0);
	if (!vrv) return false;
	TreePanel* tree = vframe->GetTree();
	if (!tree) return false;
    BrushToolDlg* brushdlg = vframe->GetBrushToolDlg();
    if (!brushdlg) return false;

	Lock();
    
    DataGroup* group = vrv->GetParentGroup(m_vol_s[0]);
    if (group)
    {
        wxString gname = group->GetName();
        vrv->RemoveGroup(gname);
    }
    group = vrv->GetParentGroup(m_vol_r);
    if (group)
    {
        wxString gname = group->GetName();
        vrv->RemoveGroup(gname);
    }
    
	for (int c = 0; c < 3; c++)
		m_vol_s[c].Clear();
	m_vol_r.Clear();
    
	vframe->UpdateTree();
	tree->ClearVisHistory();

	if (m_lbl_reader)
		delete m_lbl_reader;
	if (m_vol_reader)
		delete m_vol_reader;

	m_xspc = -1.0;
	m_yspc = -1.0;
	m_zspc = -1.0;
	if (!spacings.IsEmpty())
	{
		wxStringTokenizer tkz(spacings, wxT("x"));
		wxArrayString strspc;
		while (tkz.HasMoreTokens())
			strspc.Add(tkz.GetNextToken());
		
		if (strspc.Count() == 3)
		{
			double val;
			if (strspc.Item(0).ToDouble(&val))
				m_xspc = val;
			if (strspc.Item(1).ToDouble(&val))
				m_yspc = val;
			if (strspc.Item(2).ToDouble(&val))
				m_zspc = val;
		}
	}

	m_prefix = prefix;
	if (!m_prefix.IsEmpty())
		m_prefix += wxT(" ");

	m_lbl_reader = new V3DPBDReader();
	wstring str_w = id_path.ToStdWstring();
	m_lbl_reader->SetFile(str_w);
	m_lbl_reader->Preprocess();
	int lbl_chan = m_lbl_reader->GetChanNum();
	auto v3d_nrrd = m_lbl_reader->Convert(0, 0, true);
	void* v3d_data = v3d_nrrd->getNrrd()->data;

	size_t avmem = (size_t)(vrv->GetAvailableGraphicsMemory(0) * 1024.0 * 1024.0 * 0.8);
	//size_t avmem = (size_t)(1024.0 * 1024.0 * 1024.0);

	LoadFiles(vol_path, avmem, prefix);
	int vol_chan = dm->GetLatestVolumeChannelNum();
	
	m_vol_suffix = vol_path.Mid(vol_path.Find('.', true)).MakeLower();

	VolumeData* vols[3] = {};
    VolumeData* volr = NULL;
	void* data_s[3] = {};
	void* data_r;
	int scount = 0;
	for (int c = 0; c < vol_chan; c++)
	{
		if (c < chspec.Length())
		{
			VolumeData* vd = dm->GetLatestVolumeDataset(c);
			if (vd && vd->GetTexture() && vd->GetTexture()->get_nrrd(0))
			{
				if (chspec[c].GetValue() == 's' && scount < 3)
				{
					m_nrrd_s[scount] = vd->GetTexture()->get_nrrd(0);
					data_s[scount] = m_nrrd_s[scount]->getNrrd()->data;
					m_vol_s[scount] = vd->GetName();
					vols[scount] = vd;
                    if (m_xspc > 0.0 && m_yspc > 0.0 && m_zspc > 0.0)
                        vd->SetSpacings(m_xspc, m_yspc, m_zspc);
					scount++;
				}
				if (chspec[c].GetValue() == 'r')
				{
					m_nrrd_r = vd->GetTexture()->get_nrrd(0);
					data_r = m_nrrd_r->getNrrd()->data;
					m_vol_r = vd->GetName();
                    volr = vd;
                    if (m_xspc > 0.0 && m_yspc > 0.0 && m_zspc > 0.0)
                        vd->SetSpacings(m_xspc, m_yspc, m_zspc);
				}
			}
		}
	}

	int dim_offset = 0;
	if (m_nrrd_s[0]->getNrrd()->dim > 3) dim_offset = 1;
	size_t nx = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 0].size;
	size_t ny = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 1].size;
	size_t nz = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 2].size;
	if (m_xspc <= 0.0)
		m_xspc = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 0].spacing;
	if (m_yspc <= 0.0)
		m_yspc = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 1].spacing;
	if (m_zspc <= 0.0)
		m_zspc = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 2].spacing;
	
	m_lbl_nrrd = make_shared<VL_Nrrd>(VolumeData::NrrdScale(v3d_nrrd->getNrrd(), nx, ny, nz, false));

	if (!m_lbl_nrrd) return false;
	void* lbl_data = m_lbl_nrrd->getNrrd()->data;
	nrrdAxisInfoSet(m_lbl_nrrd->getNrrd(), nrrdAxisInfoSpacing, m_xspc, m_yspc, m_zspc);
	nrrdAxisInfoSet(m_lbl_nrrd->getNrrd(), nrrdAxisInfoMax, m_xspc * nx, m_yspc * ny, m_zspc * nz);
	nrrdAxisInfoSet(m_lbl_nrrd->getNrrd(), nrrdAxisInfoMin, 0.0, 0.0, 0.0);
	nrrdAxisInfoSet(m_lbl_nrrd->getNrrd(), nrrdAxisInfoSize, (size_t)nx, (size_t)ny, (size_t)nz);
	v3d_nrrd.reset();

	for (int c = 0; c < scount; c++)
	{
		if (c == 0)
			vols[c]->LoadLabel(m_lbl_nrrd);
		else
			vols[c]->SetSharedLabelName(vols[0]->GetName());
	}
    
    for (int c = 0; c < scount; c++)
    {
        if (c == 0)
            vols[c]->AddEmptyMask();
        else
            vols[c]->SetSharedMaskName(vols[0]->GetName());
    }
    if (volr)
        volr->SetSharedMaskName(vols[0]->GetName());

	m_scount = scount;
    
    wxProgressDialog* prg_diag = new wxProgressDialog(
                                                      "Neuron Annotator Plugin",
                                                      "Loading labels...",
                                                      4, 0, wxPD_SMOOTH | wxPD_AUTO_HIDE);

	prg_diag->Update(2, "Calculating bounding boxes...");

	int i, j, k;
	map<int, BBox> seg_bbox;
	map<int, size_t> seg_size;
	double gmaxval_r = (m_nrrd_s[0]->getNrrd()->type == nrrdTypeUChar) ? 255.0 : 4096.0;
	m_gmaxvals[0] = m_gmaxvals[1] = m_gmaxvals[2] = (m_nrrd_s[0]->getNrrd()->type == nrrdTypeUChar) ? 255.0 : 4096.0;
	//first pass: finding max value and calculating BBox
	for (i = 0; i < nx; i++)
	{
		for (j = 0; j < ny; j++)
		{
			for (k = 0; k < nz; k++)
			{
				size_t index = (size_t)nx * ny * k + nx * j + i;
				int label = 0;
				if (m_lbl_nrrd->getNrrd()->type == nrrdTypeUChar)
					label = ((unsigned char*)lbl_data)[index];
				else if (m_lbl_nrrd->getNrrd()->type == nrrdTypeUShort)
					label = ((unsigned short*)lbl_data)[index];
				if (label > 0)
				{
					if (seg_bbox.find(label) == seg_bbox.end())
					{
						BBox bbox;
						bbox.extend(Point(i, j, k));
						seg_bbox[label] = bbox;
						seg_size[label] = 1;
					}
					else
					{
						seg_bbox[label].extend(Point(i, j, k));
						seg_size[label]++;
					}
				}
			}
		}
	}

	prg_diag->Update(3, "Generating MIP images...");

	map<int, BBox>::iterator it = seg_bbox.begin();
	m_segs.clear();

	NASegment bgseg;
	bgseg.id = 0;
	bgseg.size = (size_t) nx * ny * nz;
	bgseg.bbox = BBox(Point(0, 0, 0), Point(nx-1, ny-1, nz-1));
	m_segs.push_back(bgseg);

	while (it != seg_bbox.end())
	{
		NASegment seg;
		seg.id = it->first;
		seg.size = seg_size[seg.id];
		seg.bbox = it->second;

		m_segs.push_back(seg);
		it++;
	}
	std::sort(m_segs.begin()+1, m_segs.end(), NAGuiPlugin::sort_data_asc);

	m_running_mip_th = 0;
	int thread_num = wxThread::GetCPUCount() - 1;
	if (thread_num <= 0)
		thread_num = 1;
	if (m_segs.size() / thread_num == 0)
		thread_num = m_segs.size();
	int qnum = m_segs.size() / thread_num;

	for (int i = 0; i < thread_num; i++)
	{
		vector<int> queue;
		for (int j = 0; j < qnum; j++)
			queue.push_back(qnum*i + j);
		if (i == thread_num - 1)
		{
			int remainder = m_segs.size() % thread_num;
			int offset = qnum * (i + 1);
			for (int k = 0; k < remainder; k++)
				queue.push_back(offset + k);
		}
		MIPGeneratorThread* th = new MIPGeneratorThread(this, queue);
		th->Run();
	}
	
	size_t mx = 300;
	size_t my = (size_t)(300.0 * ny / nx);
	double dx = (double)nx / mx;
	double dy = (double)ny / my;
	unsigned char* temp_r = (unsigned char*)malloc((size_t)mx * my * 3 * sizeof(unsigned char));
	for (i = 0; i < mx; i++)
	{
		for (j = 0; j < my; j++)
		{
			if (m_nrrd_r)
			{
				size_t xx = (size_t)(i * dx);
				size_t yy = (size_t)(j * dy);
				double maxval_r = 0.0;
				for (k = 0; k < nz; k++)
				{
					int index = (size_t)nx * ny * k + nx * yy + xx;
					double val = 0.0;
					if (m_nrrd_r->getNrrd()->type == nrrdTypeUChar)
						val = ((unsigned char*)data_r)[index];
					else if (m_nrrd_r->getNrrd()->type == nrrdTypeUShort)
						val = ((unsigned short*)data_r)[index];
					if (val > maxval_r)
						maxval_r = val;
				}
				unsigned char v = 0;
				if (m_nrrd_r->getNrrd()->type == nrrdTypeUChar)
					v = (unsigned char)maxval_r;
				else if (m_nrrd_r->getNrrd()->type == nrrdTypeUShort)
					v = (unsigned char)(maxval_r / gmaxval_r * 255.0);
				temp_r[(size_t)mx * 3 * j + 3 * i + 0] = v;
				temp_r[(size_t)mx * 3 * j + 3 * i + 1] = v;
				temp_r[(size_t)mx * 3 * j + 3 * i + 2] = v;
			}
		}
	}

	while (1)
	{
		{
			wxCriticalSectionLocker enter(m_pThreadCS);
			if (m_running_mip_th <= 0)
				break;
		}
		wxThread::This()->Sleep(10);
	}
	m_running_mip_th = 0;

	prg_diag->Update(4, "Generating MIP images...");

	m_ref_image.Create(mx, my, temp_r);
	m_ref_image_thumb = m_ref_image.Scale(300, (int)((double)m_ref_image.GetHeight() / (double)m_ref_image.GetWidth() * 300.0), wxIMAGE_QUALITY_HIGH);

	m_id_path = id_path;
	m_vol_path = vol_path;

	delete prg_diag;

	m_dirty = set_dirty;
    m_reload_list = set_dirty;

	for (int s = 0; s < m_segs.size(); s++)
	{
		int id = m_segs[s].id;
		for (int c = 0; c < scount; c++)
		{
			vols[c]->SetNAMode(true);
			vols[c]->SetSegmentMask(id, true);
		}
		m_allsig_visible = true;
	}

	group = vrv->GetParentGroup(m_vol_s[0]);
	if (group)
	{
		group->SetVolumeSyncProp(true);
		group->SetVolumeSyncSpc(true);
		VPropView* prop = vframe->GetPropView();
		if (prop)
		{
			VolumeData* vdr = dm->GetVolumeData(m_vol_r);
            
            if (vdr)
            {
                wxString gname = group->GetName();
                wxString rvname = vdr->GetName();
                wxString str;
                vrv->MoveLayertoView(gname, rvname, str);
            }
            if (vols[0])
            {
                wxString vname = vols[0]->GetName();
                vframe->UpdateTree(vname, 2);
            }
            else if (vdr)
            {
                wxString vname = vdr->GetName();
                vframe->UpdateTree(vname, 2);
            }
		}
	}
    
    vrv->SetSelectGroup(true);
    brushdlg->SetBrushSclTranslate(0.0);
    brushdlg->GetSettings(vrv);
    
    vrv->SetForceHideMask(true);
    
    vrv->InitView(INIT_BOUNDS|INIT_CENTER|INIT_OBJ_TRANSL|INIT_ROTATE);
    vrv->RefreshGL();

	Unlock();

	return true;
}

void NAGuiPlugin::SwitchPropPanel(bool ref)
{
    VRenderFrame* vframe = (VRenderFrame*)m_vvd;
    if (!vframe) return;
    DataManager* dm = vframe->GetDataManager();
    if (!dm) return;
    VRenderView* vrv = vframe->GetView(0);
    if (!vrv) return;
    VPropView* prop = vframe->GetPropView();
    if (!prop) return;
    
    if (ref && !m_vol_r.IsEmpty())
    {
        VolumeData* vd = dm->GetVolumeData(m_vol_r);
        if (vd != prop->GetVolumeData())
        {
            wxString vname = vd->GetName();
            DataGroup* group = vrv->GetParentGroup(vname);
            vframe->GetAdjustView()->SetGroupLink(group);
            vframe->OnSelection(2, vrv, group, vd, 0);
            vrv->SetVolumeA(vd);
        }
    }
    else if (!ref && !m_vol_s[0].IsEmpty())
    {
        VolumeData* vd = dm->GetVolumeData(m_vol_s[0]);
        if (vd != prop->GetVolumeData())
        {
            wxString vname = vd->GetName();
            DataGroup* group = vrv->GetParentGroup(vname);
            vframe->GetAdjustView()->SetGroupLink(group);
            vframe->OnSelection(2, vrv, group, vd, 0);
            vrv->SetVolumeA(vd);
        }
    }
}

void NAGuiPlugin::ResizeThumbnails(int w)
{
	if (m_ref_image.IsOk())
		m_ref_image_thumb = 
			m_ref_image.Scale(w, (int)((double)m_ref_image.GetHeight() / (double)m_ref_image.GetWidth() * w), wxIMAGE_QUALITY_HIGH);
	for (int s = 0; s < m_segs.size(); s++)
	{
		if (m_segs[s].image.IsOk())
			m_segs[s].thumbnail = 
				m_segs[s].image.Scale(w, (int)((double)m_segs[s].image.GetHeight() / (double)m_segs[s].image.GetWidth() * w), wxIMAGE_QUALITY_HIGH);
	}
}

/*
bool NAGuiPlugin::runNALoader(wxString id_path, wxString vol_path, wxString chspec)
{
	if (!wxFileExists(id_path) || !wxFileExists(vol_path))
		return false;
	VRenderFrame* vframe = (VRenderFrame*)m_vvd;
	if (!vframe) return false;
	DataManager* dm = vframe->GetDataManager();
	if (!dm) return false;
	m_lbl_reader = new V3DPBDReader();
	chspec = wxT("sssr");
	wstring str_w = id_path.ToStdWstring();
	m_lbl_reader->SetFile(str_w);
	m_lbl_reader->Preprocess();
	int lbl_chan = m_lbl_reader->GetChanNum();
	Nrrd* v3d_nrrd = m_lbl_reader->Convert(0, 0, true);
	void* v3d_data = v3d_nrrd->data;
	LoadFiles(vol_path);
	int vol_chan = dm->GetLatestVolumeChannelNum();
	VolumeData* vols[3] = {};
	Nrrd* nrrd_s[3] = {};
	void* data_s[3] = {};
	Nrrd* nrrd_r;
	void* data_r;
	int scount = 0;
	for (int c = 0; c < vol_chan; c++)
	{
		if (c < chspec.Length())
		{
			VolumeData* vd = dm->GetLatestVolumeDataset(c);
			if (vd && vd->GetTexture() && vd->GetTexture()->get_nrrd(0))
			{
				if (chspec[c].GetValue() == 's' && scount < 3)
				{
					nrrd_s[scount] = vd->GetTexture()->get_nrrd(0);
					data_s[scount] = nrrd_s[scount]->data;
					vols[scount] = vd;
					scount++;
				}
				if (chspec[c].GetValue() == 'r')
				{
					nrrd_r = vd->GetTexture()->get_nrrd(0);
					data_r = nrrd_r->data;
				}
			}
		}
	}
	int dim_offset = 0;
	if (nrrd_r->dim > 3) dim_offset = 1;
	int nx = nrrd_r->axis[dim_offset + 0].size;
	int ny = nrrd_r->axis[dim_offset + 1].size;
	int nz = nrrd_r->axis[dim_offset + 2].size;
	double xspc = nrrd_r->axis[dim_offset + 0].spacing;
	double yspc = nrrd_r->axis[dim_offset + 1].spacing;
	double zspc = nrrd_r->axis[dim_offset + 2].spacing;
	Nrrd* lbl_nrrd = nrrdNew();
	size_t vxnum = nx * ny * nz;
	uint32_t* newdata = new uint32_t[vxnum];
	if (v3d_nrrd->type == nrrdTypeUChar)
	{
		for (size_t i = 0; i < vxnum; i++)
			newdata[i] = ((unsigned char*)v3d_data)[i];
	}
	else if (v3d_nrrd->type == nrrdTypeUShort)
	{
		for (size_t i = 0; i < vxnum; i++)
			newdata[i] = ((uint16_t*)v3d_data)[i];
	}
	nrrdWrap(lbl_nrrd, (uint32_t*)newdata, nrrdTypeUInt, 3, (size_t)nx, (size_t)ny, (size_t)nz);
	nrrdAxisInfoSet(lbl_nrrd, nrrdAxisInfoSpacing, xspc, yspc, zspc);
	nrrdAxisInfoSet(lbl_nrrd, nrrdAxisInfoMax, xspc * nx, yspc * ny, zspc * nz);
	nrrdAxisInfoSet(lbl_nrrd, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
	nrrdAxisInfoSet(lbl_nrrd, nrrdAxisInfoSize, (size_t)nx, (size_t)ny, (size_t)nz);
	void* lbl_data = lbl_nrrd->data;
	delete[] v3d_data;
	nrrdNix(v3d_nrrd);
	for (int c = 0; c < scount; c++)
	{
		if (c == 0)
			vols[c]->LoadLabel(lbl_nrrd);
		else
		{
			Nrrd* newnrrd;
			newnrrd = nrrdNew();
			nrrdCopy(newnrrd, lbl_nrrd);
			vols[c]->LoadLabel(newnrrd);
		}
	}

	int i, j, k;
	map<int, BBox> seg_bbox;
	map<int, size_t> seg_size;
	double gmaxval_r = 0.0;
	double gmaxvals[3] = {};
	//first pass: finding max value and calculating BBox
	for (i = 0; i < nx; i++)
	{
		for (j = 0; j < ny; j++)
		{
			for (k = 0; k < nz; k++)
			{
				int index = nx * ny * k + nx * j + i;
				int label = 0;
				label = ((uint32_t*)lbl_data)[index];
				if (label > 0)
				{
					if (seg_bbox.find(label) == seg_bbox.end())
					{
						BBox bbox;
						bbox.extend(Point(i, j, k));
						seg_bbox[label] = bbox;
						seg_size[label] = 1;
					}
					else
					{
						seg_bbox[label].extend(Point(i, j, k));
						seg_size[label]++;
					}
				}
				for (int c = 0; c < scount; c++)
				{
					double val = 0.0;
					if (nrrd_s[c]->type == nrrdTypeUChar)
						val = ((unsigned char*)data_s[c])[index];
					else if (nrrd_s[c]->type == nrrdTypeUShort)
						val = ((unsigned short*)data_s[c])[index];
					if (val > gmaxvals[c])
						gmaxvals[c] = val;
				}
				{
					double val = 0.0;
					if (nrrd_r->type == nrrdTypeUChar)
						val = ((unsigned char*)data_r)[index];
					else if (nrrd_r->type == nrrdTypeUShort)
						val = ((unsigned short*)data_r)[index];
					if (val > gmaxval_r)
						gmaxval_r = val;
				}
			}
		}
	}

	map<int, BBox>::iterator it = seg_bbox.begin();
	m_segs.clear();

	while (it != seg_bbox.end())
	{
		NASegment seg;
		seg.id = it->first;
		seg.size = seg_size[seg.id];
		seg.bbox = it->second;

		m_segs.push_back(seg);
		it++;
	}

	std::sort(m_segs.begin(), m_segs.end(), NAGuiPlugin::sort_data_asc);

	//second pass: generate MIPs
	for (int s = 0; s < m_segs.size(); s++)
	{
		unsigned char* temp = (unsigned char*)malloc(nx * ny * 3 * sizeof(unsigned char));
		memset(temp, 0, nx * ny * 3 * sizeof(unsigned char));
		BBox bbox = m_segs[s].bbox;
		for (i = int(bbox.min().x()); i <= int(bbox.max().x()); i++)
		{
			for (j = int(bbox.min().y()); j <= int(bbox.max().y()); j++)
			{
				double maxvals[3] = {};
				for (int c = 0; c < scount; c++)
				{
					for (k = int(bbox.min().z()); k <= int(bbox.max().z()); k++)
					{
						int index = nx * ny * k + nx * j + i;
						int label = 0;
						label = ((uint32_t*)lbl_data)[index];
						if (m_segs[s].id == label)
						{
							double val = 0.0;
							if (nrrd_s[c]->type == nrrdTypeUChar)
								val = ((unsigned char*)data_s[c])[index];
							else if (nrrd_s[c]->type == nrrdTypeUShort)
								val = ((unsigned short*)data_s[c])[index];
							if (val > maxvals[c])
								maxvals[c] = val;
						}
					}
					if (nrrd_s[c]->type == nrrdTypeUChar)
						temp[nx * 3 * j + 3 * i + c] = (unsigned char)maxvals[c];
					else if (nrrd_s[c]->type == nrrdTypeUShort)
						temp[nx * 3 * j + 3 * i + c] = (unsigned char)(maxvals[c] / gmaxvals[c] * 255.0);
				}
			}
		}
		m_segs[s].image.Create(nx, ny, temp);
		m_segs[s].thumbnail = m_segs[s].image.Scale(500, (int)((double)m_segs[s].image.GetHeight() / (double)m_segs[s].image.GetWidth() * 500.0), wxIMAGE_QUALITY_HIGH);
	}
	unsigned char* temp_r = (unsigned char*)malloc(nx * ny * 3 * sizeof(unsigned char));
	unsigned char* temp_s = (unsigned char*)malloc(nx * ny * 3 * sizeof(unsigned char));
	for (i = 0; i < nx; i++)
	{
		for (j = 0; j < ny; j++)
		{
			double maxval_r = 0.0;
			for (k = 0; k < nz; k++)
			{
				int index = nx * ny * k + nx * j + i;
				double val = 0.0;
				if (nrrd_r->type == nrrdTypeUChar)
					val = ((unsigned char*)data_r)[index];
				else if (nrrd_r->type == nrrdTypeUShort)
					val = ((unsigned short*)data_r)[index];
				if (val > maxval_r)
					maxval_r = val;
			}
			unsigned char v = 0;
			if (nrrd_r->type == nrrdTypeUChar)
				v = (unsigned char)maxval_r;
			else if (nrrd_r->type == nrrdTypeUShort)
				v = (unsigned char)(maxval_r / gmaxval_r * 255.0);
			temp_r[nx * 3 * j + 3 * i + 0] = v;
			temp_r[nx * 3 * j + 3 * i + 1] = v;
			temp_r[nx * 3 * j + 3 * i + 2] = v;
			double maxvals[3] = {};
			for (int c = 0; c < scount; c++)
			{
				for (k = 0; k < nz; k++)
				{
					int index = nx * ny * k + nx * j + i;
					double val = 0.0;
					if (nrrd_s[c]->type == nrrdTypeUChar)
						val = ((unsigned char*)data_s[c])[index];
					else if (nrrd_s[c]->type == nrrdTypeUShort)
						val = ((unsigned short*)data_s[c])[index];
					if (val > maxvals[c])
						maxvals[c] = val;
				}
				if (nrrd_s[c]->type == nrrdTypeUChar)
					temp_s[nx * 3 * j + 3 * i + c] = (unsigned char)maxvals[c];
				else if (nrrd_s[c]->type == nrrdTypeUShort)
					temp_s[nx * 3 * j + 3 * i + c] = (unsigned char)(maxvals[c] / gmaxvals[c] * 255.0);
			}
		}
	}
	m_ref_image.Create(nx, ny, temp_r);
	m_sig_image.Create(nx, ny, temp_s);
	m_ref_image_thumb = m_ref_image.Scale(500, (int)((double)m_ref_image.GetHeight() / (double)m_ref_image.GetWidth() * 500.0), wxIMAGE_QUALITY_HIGH);
	m_sig_image_thumb = m_sig_image.Scale(500, (int)((double)m_sig_image.GetHeight() / (double)m_sig_image.GetWidth() * 500.0), wxIMAGE_QUALITY_HIGH);
	m_id_path = id_path;
	m_vol_path = vol_path;
	for (int s = 0; s < m_segs.size() && s < 3; s++)
	{
		int id = m_segs[s].id;
		for (int c = 0; c < scount; c++)
		{
			vols[c]->SetNAMode(true);
			vols[c]->SetSegmentMask(id, true);
		}
	}
	return true;
}
*/

wxImage* NAGuiPlugin::getSegMIPThumbnail(int id)
{
	if (id >= 0 && id < m_segs.size())
		return &m_segs[id].thumbnail;
	else
		return nullptr;
}
wxImage* NAGuiPlugin::getSegMIP(int id)
{
	if (id >= 0 && id < m_segs.size())
		return &m_segs[id].image;
	else
		return nullptr;
}
wxImage* NAGuiPlugin::getRefMIPThumbnail()
{
	return &m_ref_image_thumb;
}
wxImage* NAGuiPlugin::getRefMIP()
{
	return &m_ref_image;
}

bool NAGuiPlugin::runNALoaderRemote(wxString url, wxString usr, wxString pwd, wxString outdir, wxString ofname)
{
	if (outdir.IsEmpty() || ofname.IsEmpty())
		return false;
	
	VRenderFrame *vframe = (VRenderFrame *)m_vvd;
	if (!vframe) return false;

	VolumeData *vd = vframe->GetCurSelVol();
	if (!vd || !vd->GetTexture() || !vd->GetVR()) return false;
	
	wxString tempvdpath = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + "vvdnbtmpv.nrrd";
	wxString maskfname = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + "vvdnbtmpv.msk";

	//save mask
	if (vd->GetTexture()->nmask() != -1)
	{
		vd->GetVR()->return_mask();
		auto data = vd->GetTexture()->get_nrrd(vd->GetTexture()->nmask());
		double spcx, spcy, spcz;
		vd->GetSpacings(spcx, spcy, spcz);

		if (data)
		{
			MSKWriter msk_writer;
			msk_writer.SetData(data);
			msk_writer.SetSpacings(spcx, spcy, spcz);
			msk_writer.Save(maskfname.ToStdWstring(), 0);
			wxRenameFile(maskfname, tempvdpath, true);
		}
	}
	else
		vd->Save(tempvdpath, 2, false, true, true, false); 

	wxString inurl = url + "opt/NBLAST/input/"; // "sftp://10.36.13.16:22//opt/NBLAST/";
	int len = 16;
	char s[17];
	const char alphanum[] =
		"0123456789"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz";

	for (int i = 0; i < len; ++i) {
		s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
	}
	s[len] = 0;
	wxString randname(s);
	wxString upvolname = randname + ".nrrd";
	vframe->UploadFileRemote(inurl, upvolname, tempvdpath, usr, pwd);

	return true;
}

bool NAGuiPlugin::OnRun(wxString options)
{
	static const wxCmdLineEntryDesc cmdLineDesc[] =
	{
	   { wxCMD_LINE_OPTION, "l", NULL, NULL, wxCMD_LINE_VAL_STRING, wxCMD_LINE_OPTION_MANDATORY },
	   { wxCMD_LINE_OPTION, "v", NULL, NULL, wxCMD_LINE_VAL_STRING, wxCMD_LINE_OPTION_MANDATORY },
	   { wxCMD_LINE_OPTION, "c", NULL, NULL, wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
	   { wxCMD_LINE_OPTION, "s", NULL, NULL, wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
	   { wxCMD_LINE_OPTION, "d", NULL, NULL, wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
	   { wxCMD_LINE_NONE }
	};

	wxCmdLineParser parser(cmdLineDesc, options);
	parser.Parse();

	wxString label;
	wxString volume;
	wxString ch = "sssr";
	wxString spacings;
	wxString prefix;

	wxString val;

	if (parser.Found(wxT("l"), &val))
		label = val;
	if (parser.Found(wxT("v"), &val))
		volume = val;
	if (parser.Found(wxT("c"), &val))
		ch = val;
	if (parser.Found(wxT("s"), &val))
		spacings = val;
	if (parser.Found(wxT("d"), &val))
		prefix = val;

	runNALoader(label, volume, ch, spacings, prefix, true);

	return true;
}

bool NAGuiPlugin::LoadSWC(wxString name, wxString swc_zip_path)
{
	wxFFileInputStream fis(swc_zip_path);
	if (!fis.IsOk()) return false;

	wxConvAuto conv;
	wxZipInputStream zis(fis, conv);
	if (!zis.IsOk()) return false;

	VRenderFrame *vframe = (VRenderFrame *)m_vvd;
	if (!vframe) return false;

	wxZipEntry *entry;
	wxString ename = name;

	if (ename.AfterLast(L'.') != wxT("swc"))
		ename += wxT(".swc");

	size_t esize = 0;
	while (entry = zis.GetNextEntry())
	{
		if (ename == entry->GetName())
		{
			esize = entry->GetSize();
			break;
		}
	}

	if (esize <= 0) return false;

	char *buff = new char[esize];
	zis.Read(buff, esize);

	wxString tempswcpath = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + ename;

	wxFile tmpswc(tempswcpath, wxFile::write);

	if (tmpswc.IsOpened())
	{
		tmpswc.Write(buff, esize);
		tmpswc.Close();
		wxArrayString arr;
		arr.Add(tempswcpath);
		vframe->LoadMeshes(arr);
		wxRemoveFile(tempswcpath);
	}

	delete [] buff;

	return true;
}

bool NAGuiPlugin::LoadFiles(wxString path, size_t datasize, wxString desc)
{
	VRenderFrame *vframe = (VRenderFrame *)m_vvd;
	if (!vframe) return false;

	wxArrayString arr;
	arr.Add(path);
    wxArrayString descs;
    if (!desc.IsEmpty())
        descs.Add(desc);
	vframe->StartupLoad(arr, datasize, descs);

	return true;
}

void NAGuiPlugin::SaveCombinedFragment(wxString path)
{
    VRenderFrame* vframe = (VRenderFrame*)m_vvd;
    if (!vframe) return;
    DataManager* dm = vframe->GetDataManager();
    if (!dm) return;
    VRenderView *vrv = vframe->GetView(0);
    if (!vrv) return;
    
    int dim_offset = 0;
    if (m_nrrd_s[0]->getNrrd()->dim > 3) dim_offset = 1;
    
    int nx = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 0].size;
    int ny = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 1].size;
    int nz = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 2].size;
    
    double spcx = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 0].spacing;
    double spcy = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 1].spacing;
    double spcz = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 2].spacing;
    
    int bits = 8;
    if (m_nrrd_s[0]->getNrrd()->type == nrrdTypeUChar || m_nrrd_s[0]->getNrrd()->type == nrrdTypeChar)
        bits = 8;
    else if (m_nrrd_s[0]->getNrrd()->type == nrrdTypeUShort || m_nrrd_s[0]->getNrrd()->type == nrrdTypeShort)
        bits = 16;
    
    unsigned long long mem_size = (unsigned long long)nx*(unsigned long long)ny*(unsigned long long)nz;
    
    Nrrd *nv = nrrdNew();
    if (bits == 8)
    {
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
        uint16 *val16 = new (std::nothrow) uint16[mem_size];
        if (!val16)
        {
            wxMessageBox("Not enough memory. Please save project and restart.");
            return;
        }
        memset((void*)val16, 0, sizeof(uint16)*nx*ny*nz);
        nrrdWrap(nv, val16, nrrdTypeUShort, 3, (size_t)nx, (size_t)ny, (size_t)nz);
    }
    nrrdAxisInfoSet(nv, nrrdAxisInfoSpacing, spcx, spcy, spcz);
    nrrdAxisInfoSet(nv, nrrdAxisInfoMax, spcx*nx, spcy*ny, spcz*nz);
    nrrdAxisInfoSet(nv, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
    nrrdAxisInfoSet(nv, nrrdAxisInfoSize, (size_t)nx, (size_t)ny, (size_t)nz);

    unsigned char* ndata8 = (unsigned char*)nv->data;
    unsigned short* ndata16 = (unsigned short*)nv->data;
    if (bits == 8)
    {
        for (int s = 0; s < m_segs.size(); s++)
        {
            if (GetSegmentVisibility(s) == 0) continue;
            BBox bbox = m_segs[s].bbox;
            int sx = (int)bbox.min().x();
            int sy = (int)bbox.min().y();
            int sz = (int)bbox.min().z();
            int ex = (int)bbox.max().x();
            int ey = (int)bbox.max().y();
            int ez = (int)bbox.max().z();
            for (int i = sx; i <= ex; i++)
            {
                for (int j = sy; j <= ey; j++)
                {
                    for (int k = sz; k <= ez; k++)
                    {
                        size_t index = (size_t)nx * ny * k + nx * j + i;
                        int label = 0;
                        if (m_lbl_nrrd->getNrrd()->type == nrrdTypeUChar)
                            label = ((unsigned char*)m_lbl_nrrd->getNrrd()->data)[index];
                        else if (m_lbl_nrrd->getNrrd()->type == nrrdTypeUShort)
                            label = ((unsigned short*)m_lbl_nrrd->getNrrd()->data)[index];
                        if (m_segs[s].id == label)
                        {
                            unsigned char maxval = 0;
                            for (int c = 0; c < m_scount; c++)
                            {
                                unsigned char val = ((unsigned char*)m_nrrd_s[c]->getNrrd()->data)[index];
                                if (val > maxval) maxval = val;
                            }
                            ndata8[index] = maxval;
                        }
                    }
                }
            }
        }
    }
    else if (bits == 16)
    {
        for (int s = 0; s < m_segs.size(); s++)
        {
            if (GetSegmentVisibility(s) == 0) continue;
            BBox bbox = m_segs[s].bbox;
            int sx = (int)bbox.min().x();
            int sy = (int)bbox.min().y();
            int sz = (int)bbox.min().z();
            int ex = (int)bbox.max().x();
            int ey = (int)bbox.max().y();
            int ez = (int)bbox.max().z();
            for (int i = sx; i <= ex; i++)
            {
                for (int j = sy; j <= ey; j++)
                {
                    for (int k = sz; k <= ez; k++)
                    {
                        size_t index = (size_t)nx * ny * k + nx * j + i;
                        int label = 0;
                        if (m_lbl_nrrd->getNrrd()->type == nrrdTypeUChar)
                            label = ((unsigned char*)m_lbl_nrrd->getNrrd()->data)[index];
                        else if (m_lbl_nrrd->getNrrd()->type == nrrdTypeUShort)
                            label = ((unsigned short*)m_lbl_nrrd->getNrrd()->data)[index];
                        if (m_segs[s].id == label)
                        {
                            unsigned short maxval = 0;
                            for (int c = 0; c < m_scount; c++)
                            {
                                unsigned short val = ((unsigned short*)m_nrrd_s[c]->getNrrd()->data)[index];
                                if (val > maxval) maxval = val;
                            }
                            ndata16[index] = maxval;
                        }
                    }
                }
            }
        }
    }
    
    NRRDWriter writer;
	shared_ptr<VL_Nrrd> vlnv = make_shared<VL_Nrrd>(nv);
    writer.SetData(vlnv);
    writer.SetCompression(true);
    writer.SetSpacings(spcx, spcy, spcz);
    writer.Save(path.ToStdWstring(), 0);

}

bool NAGuiPlugin::LoadNrrd(int id)
{
	VRenderFrame* vframe = (VRenderFrame*)m_vvd;
	if (!vframe) return false;
	DataManager* dm = vframe->GetDataManager();
	if (!dm) return false;
	VRenderView *vrv = vframe->GetView(0);
	if (!vrv) return vrv;

	wxProgressDialog* prg_diag = new wxProgressDialog(
		"Neuron Annotator Plugin",
		"Uploading volume data to VVDViewer...",
		1, 0, wxPD_SMOOTH);
	prg_diag->Pulse("Uploading volume data to VVDViewer...");

	int dim_offset = 0;
	if (m_nrrd_s[0]->getNrrd()->dim > 3) dim_offset = 1;
	int nx = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 0].size;
	int ny = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 1].size;
	int nz = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 2].size;

	int bits = 8;
	if (m_nrrd_s[0]->getNrrd()->type == nrrdTypeUChar || m_nrrd_s[0]->getNrrd()->type == nrrdTypeChar)
		bits = 8;
	else if (m_nrrd_s[0]->getNrrd()->type == nrrdTypeUShort || m_nrrd_s[0]->getNrrd()->type == nrrdTypeShort)
		bits = 16;

	size_t mem_size = (size_t)nx * (size_t)ny * (size_t)nz * (size_t)(bits/8);

	if (id == -2 && m_nrrd_r)
	{
		dm->AddEmptyVolumeData(m_prefix+"Reference", bits, nx, ny, nz, m_xspc, m_yspc, m_zspc);
		VolumeData* vd = dm->GetVolumeData(dm->GetVolumeNum() - 1);
        vd->SetSpcFromFile(true);
        dm->SetVolumeDefault(vd);

		if (bits == 16)
			vd->SetMaxValue(4096.0);

		Nrrd *vn = vd->GetTexture()->get_nrrd(0)->getNrrd();
		memcpy(vn->data, m_nrrd_r->getNrrd()->data, mem_size);

		Color color(1.0, 1.0, 1.0);
		vd->SetColor(color);
		vrv->AddVolumeData(vd);
		vframe->UpdateTree(vd->GetName(), 2);
	}
	else if (id == -1)
	{
		int chan_num = 0;
		for (int i = 0; i < 3; i++)
		{
			if (m_nrrd_s[i])
			{
				wxString name = wxString::Format(m_prefix+"Channel %d", i);
				dm->AddEmptyVolumeData(name, bits, nx, ny, nz, m_xspc, m_yspc, m_zspc);
				VolumeData* vd = dm->GetVolumeData(dm->GetVolumeNum() - 1);
                vd->SetSpcFromFile(true);
                dm->SetVolumeDefault(vd);

				if (bits == 16)
					vd->SetMaxValue(4096.0);

				auto vn = vd->GetTexture()->get_nrrd(0);
				memcpy(vn->getNrrd()->data, m_nrrd_s[i]->getNrrd()->data, mem_size);
				
				Color color(1.0, 1.0, 1.0);
				if (chan_num == 0)
					color = Color(1.0, 0.0, 0.0);
				else if (chan_num == 1)
					color = Color(0.0, 1.0, 0.0);
				else if (chan_num == 2)
					color = Color(0.0, 0.0, 1.0);

				if (chan_num >= 0 && chan_num < 3)
					vd->SetColor(color);
				else
					vd->RandomizeColor();

				vrv->AddVolumeData(vd);
				vframe->UpdateTree(vd->GetName(), 2);
				
				chan_num++;
			}
		}
	}
	else if (id >= 0 && id < m_segs.size())
	{
		wxString name = wxString::Format(m_prefix+"Fragment %d", id);
		dm->AddEmptyVolumeData(name, bits, nx, ny, nz, m_xspc, m_yspc, m_zspc);
		VolumeData* vd = dm->GetVolumeData(dm->GetVolumeNum() - 1);
        vd->SetSpcFromFile(true);
        dm->SetVolumeDefault(vd);
		
		if (bits == 16)
			vd->SetMaxValue(4096.0);

		auto vn = vd->GetTexture()->get_nrrd(0);
		void* dst_data = vn->getNrrd()->data;

		int scount = 0;
		for (int c = 0; c < 3; c++, scount++)
		{
			if (!m_nrrd_s[c])
				break;
		}

		void* lbl_data = m_lbl_nrrd->getNrrd()->data;
		BBox bbox = m_segs[id].bbox;
		for (int i = int(bbox.min().x()); i <= int(bbox.max().x()); i++)
		{
			for (int j = int(bbox.min().y()); j <= int(bbox.max().y()); j++)
			{
				for (int k = int(bbox.min().z()); k <= int(bbox.max().z()); k++)
				{
					int index = nx * ny * k + nx * j + i;
					int label = 0;
					if (m_lbl_nrrd->getNrrd()->type == nrrdTypeUChar)
						label = ((unsigned char*)lbl_data)[index];
					else if (m_lbl_nrrd->getNrrd()->type == nrrdTypeUShort)
						label = ((unsigned short*)lbl_data)[index];
					if (m_segs[id].id == label)
					{
						double maxval = 0.0;
						for (int c = 0; c < scount; c++)
						{
							double val = 0.0;
							if (m_nrrd_s[c]->getNrrd()->type == nrrdTypeUChar)
								val = ((unsigned char*)(m_nrrd_s[c]->getNrrd()->data))[index];
							else if (m_nrrd_s[c]->getNrrd()->type == nrrdTypeUShort)
								val = ((unsigned short*)(m_nrrd_s[c]->getNrrd()->data))[index];
							if (val > maxval)
								maxval = val;
						}
						if (bits == 8)
							((unsigned char*)dst_data)[index] = (unsigned char)maxval;
						else if (bits == 16)
							((unsigned short*)dst_data)[index] = (unsigned short)maxval;
					}
				}
			}
		}
		vd->RandomizeColor();
		vrv->AddVolumeData(vd);
		vframe->UpdateTree(vd->GetName(), 2);
	}

	delete prg_diag;

	if (vrv)
		vrv->InitView(INIT_BOUNDS | INIT_CENTER);

	return true;
}

void NAGuiPlugin::SetSegmentVisibility(int id, int vis, bool refresh)
{
	VRenderFrame* vframe = (VRenderFrame*)m_vvd;
	if (!vframe) return;
	DataManager* dm = vframe->GetDataManager();
	if (!dm) return;
	VRenderView* vrv = vframe->GetView(0);
	if (!vrv) return;
	TreePanel* tree = vframe->GetTree();
	if (!tree) return;

    if (id == -2 && m_nrrd_r)
    {
        VolumeData* v = dm->GetVolumeData(m_vol_r);
        if (v)
        {
            v->SetDisp(vis > 0 ? true : false);
            for (int i = 0; i < vframe->GetViewNum(); i++)
            {
                VRenderView* vv = vframe->GetView(i);
                if (vv)
                    vv->SetVolPopDirty();
            }
        }
        vframe->UpdateTreeIcons();
    }
	else if (id >= 0 && id < m_segs.size())
	{
		m_segs[id].visible = vis;
		for (int i = 0; i < m_scount; i++)
		{
			VolumeData* v = dm->GetVolumeData(m_vol_s[i]);
			if (v)
			{
				int lblid = m_segs[id].id;
				v->SetNAMode(true);
				v->SetSegmentMask(lblid, m_segs[id].visible);
			}
		}
	}
    if (refresh)
        vframe->RefreshVRenderViews(true);
}

int NAGuiPlugin::GetSegmentVisibility(int id)
{
	int ret_val = 0;

	VRenderFrame* vframe = (VRenderFrame*)m_vvd;
	if (!vframe) return ret_val;
	DataManager* dm = vframe->GetDataManager();
	if (!dm) return ret_val;

	if (id == -2 && m_nrrd_r)
	{
		VolumeData* v = dm->GetVolumeData(m_vol_r);
		if (v)
			ret_val = v->GetDisp() ? 1 : 0;
	}
	if (id >= 0 && id < m_segs.size())
	{
		for (int i = 0; i < m_scount; i++)
		{
			VolumeData* v = dm->GetVolumeData(m_vol_s[i]);
			if (v)
			{
				int lblid = m_segs[id].id;
				if (v->GetNAMode())
					ret_val = v->GetSegmentMask(lblid);
			}
		}
	}
	
	return ret_val;
}

void NAGuiPlugin::ToggleSegmentVisibility(int id)
{
	VRenderFrame* vframe = (VRenderFrame*)m_vvd;
	if (!vframe) return;
	DataManager* dm = vframe->GetDataManager();
	if (!dm) return;
	VRenderView* vrv = vframe->GetView(0);
	if (!vrv) return;
	TreePanel* tree = vframe->GetTree();
	if (!tree) return;

	if (id == -2 && m_nrrd_r)
	{
		VolumeData* v = dm->GetVolumeData(m_vol_r);
		if (v)
		{
			v->SetDisp(!v->GetDisp());
			for (int i = 0; i < vframe->GetViewNum(); i++)
			{
				VRenderView* vv = vframe->GetView(i);
				if (vv)
					vv->SetVolPopDirty();
			}
		}
		vframe->UpdateTreeIcons();
	}
	if (id >= 0 && id < m_segs.size())
	{
		m_segs[id].visible = !m_segs[id].visible;
		for (int i = 0; i < m_scount; i++)
		{
			VolumeData* v = dm->GetVolumeData(m_vol_s[i]);
			if (v)
			{
				int lblid = m_segs[id].id;
				v->SetNAMode(true);
				v->SetSegmentMask(lblid, m_segs[id].visible);
			}
		}
	}
	vframe->RefreshVRenderViews(true);
}

void NAGuiPlugin::PushVisHistory()
{
	VRenderFrame* vframe = (VRenderFrame*)m_vvd;
	if (!vframe) return;
	TreePanel* tree = vframe->GetTree();
	if (!tree) return;

	tree->PushVisHistory();
}

void NAGuiPlugin::RefreshRenderViews()
{
    VRenderFrame* vframe = (VRenderFrame*)m_vvd;
    if (!vframe) return;
    
    vframe->RefreshVRenderViews(true);
}

wxString NAGuiPlugin::GetName() const
{
	return _("NAPlugin");
}

wxString NAGuiPlugin::GetDisplayName() const
{
    return _("Neuron Annotator Plugin");
}

wxString NAGuiPlugin::GetId() const
{
	return wxT("{EEFDF66-5FBB-4719-AF17-76C1C82D3FE1}");
}

wxWindow * NAGuiPlugin::CreatePanel(wxWindow * parent)
{
	return new NAGuiPluginWindow(this, parent);
}

void NAGuiPlugin::OnInit()
{
	LoadConfigFile();
}

void NAGuiPlugin::OnDestroy()
{

}

void NAGuiPlugin::OnTreeUpdate()
{
	if (!m_lock)
		m_dirty = true;
}

void NAGuiPlugin::OnTimer(wxTimerEvent& event)
{

}

void NAGuiPlugin::LoadConfigFile()
{
    wxString expath = wxStandardPaths::Get().GetExecutablePath();
    expath = expath.BeforeLast(GETSLASH(), NULL);
#ifdef _DARWIN
    wxString dft = expath + "/../Resources/NA_plugin_settings.dft";
#else
   wxString dft = expath + GETSLASHS() + "NA_plugin_settings.dft";
    if (!wxFileExists(dft))
        dft = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "NA_plugin_settings.dft";
#endif
    LoadProjectSettingFile(dft);
}

void NAGuiPlugin::LoadProjectSettingFile(wxString path)
{
/*
	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(), NULL);
#ifdef _WIN32
	wxString dft = expath + "\\NA_plugin_settings.dft";
	if (!wxFileExists(dft))
		dft = wxStandardPaths::Get().GetUserConfigDir() + "\\NA_plugin_settings.dft";
#else
	wxString dft = expath + "/../Resources/NA_plugin_settings.dft";
#endif
*/
	if (wxFileExists(path))
	{
		wxFileInputStream is(path);
		if (is.IsOk())
		{
			wxFileConfig fconfig(is);
			wxString str;
            int ival = 0;
            double dval = 0.0;
            
			if (fconfig.Read("Reference", &str))
				m_vol_r = str;
			if (fconfig.Read("Signal1", &str))
				m_vol_s[0] = str;
            if (fconfig.Read("Signal2", &str))
                m_vol_s[1] = str;
            if (fconfig.Read("Signal3", &str))
                m_vol_s[2] = str;
            
            if (fconfig.Read("s_count", &ival))
                m_scount = ival;
            
            if (fconfig.Read("xspc", &dval))
                m_xspc = dval;
            if (fconfig.Read("yspc", &dval))
                m_yspc = dval;
            if (fconfig.Read("zspc", &dval))
                m_zspc = dval;
            
            if (fconfig.Read("id_path", &str))
                m_id_path = str;
            if (fconfig.Read("vol_path", &str))
                m_vol_path = str;
            if (fconfig.Read("prefix", &str))
                m_prefix = str;
            
            if (fconfig.Read("s_count", &ival))
                m_scount = ival;
            
            int seg_count = 0;
            if (fconfig.Read("seg_num", &ival))
                seg_count = ival;
            
            m_segs.clear();
            
            for (int i = 0; i < seg_count; i++)
            {
                if (fconfig.Read(wxString::Format("seg_%d", i), &str))
                {
                    NASegment seg;
                    wxStringTokenizer tkz(str, wxT(","));
                    wxArrayString strspc;
                    while (tkz.HasMoreTokens())
                        strspc.Add(tkz.GetNextToken());
                
                    if (strspc.Count() == 2)
                    {
                        long val;
                        if (strspc.Item(0).ToLong(&val))
                            seg.id = val;
                        if (strspc.Item(1).ToLong(&val))
                            seg.visible = val;
                    }
                    m_segs.push_back(seg);
                }
            }
            
            LoadSettings();
		}
	}

}

void NAGuiPlugin::LoadSettings()
{
    VRenderFrame* vframe = (VRenderFrame*)m_vvd;
    if (!vframe) return;
    DataManager* dm = vframe->GetDataManager();
    if (!dm) return;
    VRenderView* vrv = vframe->GetView(0);
    if (!vrv) return;
    TreePanel* tree = vframe->GetTree();
    if (!tree) return;
    BrushToolDlg* brushdlg = vframe->GetBrushToolDlg();
    if (!brushdlg) return;
    
    Lock();
    
    vframe->UpdateTree();
    tree->ClearVisHistory();
    
    if (m_lbl_reader)
        delete m_lbl_reader;
    if (m_vol_reader)
        delete m_vol_reader;
    
    size_t avmem = (size_t)(vrv->GetAvailableGraphicsMemory(0) * 1024.0 * 1024.0);
    
    m_lbl_nrrd = NULL;
    VolumeData* vd_s = dm->GetVolumeData(m_vol_s[0]);
    if (vd_s && !vd_s->GetSharedLabelName().IsEmpty())
        vd_s = dm->GetVolumeData(vd_s->GetSharedLabelName());
    if (vd_s)
    {
        Texture* tex = vd_s->GetTexture();
        if (tex)
        {
            m_lbl_nrrd = tex->get_nrrd(tex->nlabel());
        }
    }
    
    if (!m_lbl_nrrd) return;
    void* lbl_data = m_lbl_nrrd->getNrrd()->data;
    
    m_nrrd_r = NULL;
    VolumeData* vd_r = dm->GetVolumeData(m_vol_r);
    if (vd_r)
    {
        Texture* tex = vd_r->GetTexture();
        if (tex)
            m_nrrd_r = tex->get_nrrd(0);
    }
    for (int i = 0; i < 3; i++)
    {
        m_nrrd_s[i] = NULL;
        vd_s = dm->GetVolumeData(m_vol_s[i]);
        if (vd_s)
        {
            Texture* tex = vd_s->GetTexture();
            if (tex)
                m_nrrd_s[i] = tex->get_nrrd(0);
        }
    }
    
    void* data_r = m_nrrd_r ? m_nrrd_r->getNrrd()->data : NULL;
    
    int dim_offset = 0;
    if (m_nrrd_s[0]->getNrrd()->dim > 3) dim_offset = 1;
    int nx = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 0].size;
    int ny = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 1].size;
    int nz = m_nrrd_s[0]->getNrrd()->axis[dim_offset + 2].size;
    
    wxProgressDialog* prg_diag = new wxProgressDialog(
                                                      "Neuron Annotator Plugin",
                                                      "Loading labels...",
                                                      4, 0, wxPD_SMOOTH | wxPD_AUTO_HIDE);
    
    prg_diag->Update(2, "Calculating bounding boxes...");
    
    int i, j, k;
    map<int, BBox> seg_bbox;
    map<int, size_t> seg_size;
    double gmaxval_r = (m_nrrd_s[0]->getNrrd()->type == nrrdTypeUChar) ? 255.0 : 4096.0;
    m_gmaxvals[0] = m_gmaxvals[1] = m_gmaxvals[2] = (m_nrrd_s[0]->getNrrd()->type == nrrdTypeUChar) ? 255.0 : 4096.0;
    //first pass: finding max value and calculating BBox
    for (i = 0; i < nx; i++)
    {
        for (j = 0; j < ny; j++)
        {
            for (k = 0; k < nz; k++)
            {
                size_t index = (size_t)nx * ny * k + nx * j + i;
                int label = 0;
                if (m_lbl_nrrd->getNrrd()->type == nrrdTypeUChar)
                    label = ((unsigned char*)lbl_data)[index];
                else if (m_lbl_nrrd->getNrrd()->type == nrrdTypeUShort)
                    label = ((unsigned short*)lbl_data)[index];
                if (label > 0)
                {
                    if (seg_bbox.find(label) == seg_bbox.end())
                    {
                        BBox bbox;
                        bbox.extend(Point(i, j, k));
                        seg_bbox[label] = bbox;
                        seg_size[label] = 1;
                    }
                    else
                    {
                        seg_bbox[label].extend(Point(i, j, k));
                        seg_size[label]++;
                    }
                }
            }
        }
    }
    
    prg_diag->Update(3, "Generating MIP images...");
    
    map<int, BBox>::iterator it = seg_bbox.begin();
    
    map<int, int> seg_vis;
    for (int i = 0; i < m_segs.size(); i++)
        seg_vis[m_segs[i].id] = m_segs[i].visible;
    
    m_segs.clear();
    
    NASegment bgseg;
    bgseg.id = 0;
    bgseg.size = (size_t) nx * ny * nz;
    bgseg.bbox = BBox(Point(0, 0, 0), Point(nx-1, ny-1, nz-1));
    if (seg_vis.count(0) == 1)
        bgseg.visible = seg_vis[0];
    m_segs.push_back(bgseg);
    
    while (it != seg_bbox.end())
    {
        NASegment seg;
        seg.id = it->first;
        seg.size = seg_size[seg.id];
        seg.bbox = it->second;
        if (seg_vis.count(seg.id) == 1)
            seg.visible = seg_vis[seg.id];
        
        m_segs.push_back(seg);
        it++;
    }
    std::sort(m_segs.begin()+1, m_segs.end(), NAGuiPlugin::sort_data_asc);
    
    m_running_mip_th = 0;
    int thread_num = wxThread::GetCPUCount() - 1;
    if (thread_num <= 0)
        thread_num = 1;
    if (m_segs.size() / thread_num == 0)
        thread_num = m_segs.size();
    int qnum = m_segs.size() / thread_num;
    
    for (int i = 0; i < thread_num; i++)
    {
        vector<int> queue;
        for (int j = 0; j < qnum; j++)
            queue.push_back(qnum*i + j);
        if (i == thread_num - 1)
        {
            int remainder = m_segs.size() % thread_num;
            int offset = qnum * (i + 1);
            for (int k = 0; k < remainder; k++)
                queue.push_back(offset + k);
        }
        MIPGeneratorThread* th = new MIPGeneratorThread(this, queue);
        th->Run();
    }
    
    size_t mx = 300;
    size_t my = (size_t)(300.0 * ny / nx);
    double dx = (double)nx / mx;
    double dy = (double)ny / my;
    unsigned char* temp_r = (unsigned char*)malloc((size_t)mx * my * 3 * sizeof(unsigned char));
    for (i = 0; i < mx; i++)
    {
        for (j = 0; j < my; j++)
        {
            if (m_nrrd_r)
            {
                size_t xx = (size_t)(i * dx);
                size_t yy = (size_t)(j * dy);
                double maxval_r = 0.0;
                for (k = 0; k < nz; k++)
                {
                    int index = (size_t)nx * ny * k + nx * yy + xx;
                    double val = 0.0;
                    if (m_nrrd_r->getNrrd()->type == nrrdTypeUChar)
                        val = ((unsigned char*)data_r)[index];
                    else if (m_nrrd_r->getNrrd()->type == nrrdTypeUShort)
                        val = ((unsigned short*)data_r)[index];
                    if (val > maxval_r)
                        maxval_r = val;
                }
                unsigned char v = 0;
                if (m_nrrd_r->getNrrd()->type == nrrdTypeUChar)
                    v = (unsigned char)maxval_r;
                else if (m_nrrd_r->getNrrd()->type == nrrdTypeUShort)
                    v = (unsigned char)(maxval_r / gmaxval_r * 255.0);
                temp_r[(size_t)mx * 3 * j + 3 * i + 0] = v;
                temp_r[(size_t)mx * 3 * j + 3 * i + 1] = v;
                temp_r[(size_t)mx * 3 * j + 3 * i + 2] = v;
            }
        }
    }
    
    while (1)
    {
        {
            wxCriticalSectionLocker enter(m_pThreadCS);
            if (m_running_mip_th <= 0)
                break;
        }
        wxThread::This()->Sleep(10);
    }
    m_running_mip_th = 0;
    
    prg_diag->Update(4, "Generating MIP images...");
    
    m_ref_image.Create(mx, my, temp_r);
    m_ref_image_thumb = m_ref_image.Scale(300, (int)((double)m_ref_image.GetHeight() / (double)m_ref_image.GetWidth() * 300.0), wxIMAGE_QUALITY_HIGH);
    
    delete prg_diag;
    
    m_dirty = true;
    m_reload_list = true;
    
    for (int s = 0; s < m_segs.size(); s++)
    {
        int id = m_segs[s].id;
        for (int c = 0; c < m_scount; c++)
        {
            vd_s = dm->GetVolumeData(m_vol_s[c]);
            if (vd_s)
            {
                vd_s->SetNAMode(true);
                vd_s->SetSegmentMask(id, m_segs[s].visible);
            }
        }
    }
    
    vrv->RefreshGL();
    
    Unlock();
    
    return;
}

void NAGuiPlugin::SaveConfigFile()
{
    wxString expath = wxStandardPaths::Get().GetExecutablePath();
    expath = expath.BeforeLast(GETSLASH(),NULL);
#ifdef _DARWIN
    wxString dft = expath + "/../Resources/NA_plugin_settings.dft";
#else
    wxString dft = expath + GETSLASHS() + "NA_plugin_settings.dft";
    wxString dft2 = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "NA_plugin_settings.dft";
    if (!wxFileExists(dft) && wxFileExists(dft2))
        dft = dft2;
#endif
    
    SaveProjectSettingFile(dft);
}

void NAGuiPlugin::SaveProjectSettingFile(wxString path)
{

	wxFileConfig fconfig("NA plugin default settings");

	fconfig.Write("Reference", m_vol_r);
	fconfig.Write("Signal1", m_vol_s[0]);
    fconfig.Write("Signal2", m_vol_s[1]);
    fconfig.Write("Signal3", m_vol_s[2]);
	fconfig.Write("s_count", m_scount);
	fconfig.Write("xspc", m_xspc);
    fconfig.Write("yspc", m_yspc);
    fconfig.Write("zspc", m_zspc);
	fconfig.Write("id_path", m_id_path);
	fconfig.Write("vol_path", m_vol_path);
    fconfig.Write("prefix", m_prefix);
	fconfig.Write("seg_num", (int)m_segs.size());
    for (int i = 0; i < m_segs.size(); i++)
        fconfig.Write(wxString::Format("seg_%d", i), wxString::Format("%d,%d", m_segs[i].id, m_segs[i].visible));

	wxFileOutputStream os(path);
	fconfig.Save(os);
 
}
