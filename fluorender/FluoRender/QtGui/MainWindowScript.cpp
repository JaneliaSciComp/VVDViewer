// 4D script runner: VRenderVulkanView::Run4DScript and its task handlers
// (VRenderView.cpp), run by the Record/Export playback (wx VMovieView
// m_batch_mode: OnPrev runs the script on the current time point, OnTimer
// on every new one) when Settings > 4D Script "Enable execution of a script
// on 4D data during playback" is on and the script file exists.
//
// The script is a wxFileConfig file:
//   [tasks]
//   tasknum=<n>
//   [tasks/task0]
//   type=noise_reduction | selection_tracking | random_colors |
//        separate_channels | external_exe | fetch_mask
//   ... task keys (see the handlers)
//
// As in wx, the run flag and the file name the runner checks are the ones
// the view read from the Settings at its last Set4DSeqFrame (RenderCanvas::
// GetRun4DScript / GetScriptFile), and the selection-tracking task has no
// trace group (the Qt app has no Tracking dialog): it keeps the labels of
// the previous time point that the mask selects.

#include "MainWindow.h"

#include <QDir>
#include <QFileInfo>
#include <unordered_map>
#include <cstring>

#include "RenderCanvas.h"
#include "DataManager.h"
#include "Core/FileUtil.h"
#include "Formats/lbl_reader.h"
#include "Formats/msk_reader.h"
#include "FLIVR/VolumeRenderer.h"
#include "TraceDialog.h"

namespace
{
	// wx: create every directory along pathname ("a/b/c/" -> a, a/b, a/b/c)
	void MakePathDirs(const QString& pathname)
	{
		int pos = 0;
		while (true)
		{
			pos = pathname.indexOf('/', pos);
			if (pos < 0)
				break;
			pos++;
			const QString str = pathname.left(pos);
			if (!QDir(str).exists())
				QDir().mkdir(str);
		}
	}

	QString ReadStr(const vvd::IniFile& f, const VVDString& key, const QString& def = QString())
	{
		VVDString v;
		return f.Read(key, v) ? QString::fromStdWString(v) : def;
	}

	// wx: format = "_T%0<digits of the time count>d"
	QString TimeSuffix(int time_num, int t, int& fr_length)
	{
		fr_length = QString::number(time_num).length();
		return QString::asprintf("_T%0*d", fr_length, t);
	}

	VVDString V(const QString& s) { return VVDString(s.toStdWString()); }

	// wx: data_name.Left(data_name.find_last_of('.')) + ext
	QString Companion(const std::wstring& data_name, const char* ext)
	{
		const QString n = QString::fromStdWString(data_name);
		const int dot = n.lastIndexOf('.');
		return (dot >= 0 ? n.left(dot) : n) + ext;
	}
}

// wx VRenderVulkanView::Run4DScript(): every shown volume
void MainWindow::Run4DScript()
{
	const QString file = m_canvas->GetScriptFile();
	for (VolumeData* vd : m_canvas->PopVolumeList())
		if (vd)
			Run4DScript(file, vd);
	m_canvas->RefreshView();
}

// wx VRenderVulkanView::Run4DScript(scriptname, vd)
void MainWindow::Run4DScript(const QString& scriptname, VolumeData* vd)
{
	if (m_canvas->GetRun4DScript() && QFileInfo::exists(m_canvas->GetScriptFile()) && !m_canvas->GetScriptFile().isEmpty())
	{
		m_sel->SetVolume(vd);
		m_calc->SetVolumeA(vd);
		m_script_vol = vd;
		m_canvas->SetCurrentVolume(vd);
	}
	else
		return;

	vvd::IniFile fconfig;
	if (!fconfig.Load(V(scriptname)))
		return;

	m_canvas->EnsureVulkan();
	//tasks
	long tasknum = 0;
	fconfig.Read(VVDString(L"/tasks/tasknum"), tasknum);
	for (long i = 0; i < tasknum; i++)
	{
		const VVDString prefix = V(QString("/tasks/task%1/").arg(i));
		const QString type = ReadStr(fconfig, prefix + L"type");
		if (type == "noise_reduction")
			RunNoiseReduction(fconfig, prefix);
		else if (type == "selection_tracking")
			RunSelectionTracking(fconfig, prefix);
		else if (type == "random_colors")
			RunRandomColors(fconfig, prefix);
		else if (type == "separate_channels")
			RunSeparateChannels(fconfig, prefix);
		else if (type == "external_exe")
			;   // wx RunExternalExe: its body is commented out
		else if (type == "fetch_mask")
			RunFetchMask(fconfig, prefix);
	}
}

// wx RunNoiseReduction: component analysis (size / threshold), Erase
// (Calculate(6), not added), the result saved as <savepath><name>_T<t>.tif|.nrrd
void MainWindow::RunNoiseReduction(const vvd::IniFile& fconfig, const VVDString& prefix)
{
	VolumeData* cur = m_script_vol;
	if (!cur || !cur->GetReader())
		return;
	double thresh = 0.0, size = 0.0;
	fconfig.Read(prefix + L"threshold", thresh);
	thresh /= cur->GetMaxValue();
	fconfig.Read(prefix + L"voxelsize", size);
	int mode = 0;
	fconfig.Read(prefix + L"format", mode);
	bool bake = false, compression = false;
	fconfig.Read(prefix + L"bake", bake);
	fconfig.Read(prefix + L"compress", compression);
	const QString pathname = ReadStr(fconfig, prefix + L"savepath");
	MakePathDirs(pathname);

	CompAnalysis(0.0, size, thresh, false, false, (int)size + 1);
	Calculate(6, false);
	VolumeData* vd = m_calc->GetResult();
	if (vd)
	{
		const int time_num = cur->GetReader()->GetTimeNum();
		const QString str = pathname + vd->GetName().ToQt() +
			TimeSuffix(time_num, m_canvas->GetTSeqCurNum(), m_fr_length) + (mode == 2 ? ".nrrd" : ".tif");
		vd->Save(V(str), mode, bake, compression, false, false);
		delete vd;   // wx: the calculator keeps the (now dangling) pointer; nothing reads it before the next Calculate
	}
	cur->DeleteMask();
	cur->DeleteLabel();
}

// wx RunSelectionTracking: the labels of the previous time
// point selected by the current mask (at least size_limit voxels) select the
// same label values in the new time point's .lbl
void MainWindow::RunSelectionTracking(const vvd::IniFile& fconfig, const VVDString& prefix)
{
	int slimit = 0;
	fconfig.Read(prefix + L"size_limit", slimit);
	VolumeData* cur = m_script_vol;
	if (!cur)
		return;
	if (cur->GetVR()) cur->GetVR()->return_mask();
	int nx, ny, nz;
	cur->GetResolution(nx, ny, nz);
	//find labels in the old that are selected by the current mask
	auto mask_nrrd = cur->GetMask(true);
	if (!mask_nrrd || !mask_nrrd->getNrrd())
	{
		cur->AddEmptyMask();
		mask_nrrd = cur->GetMask(false);
	}
	auto label_nrrd = cur->GetLabel(false);
	if (!label_nrrd || !label_nrrd->getNrrd())
		return;
	if (!mask_nrrd || !mask_nrrd->getNrrd())
		return;
	unsigned char* mask_data = (unsigned char*)(mask_nrrd->getNrrd()->data);
	if (!mask_data)
		return;
	unsigned int* label_data = (unsigned int*)(label_nrrd->getNrrd()->data);
	if (!label_data)
		return;
	std::unordered_map<unsigned int, unsigned int> sel_labels;   // wx FL::CellList (label -> size)
	const size_t count = (size_t)nx * ny * nz;
	for (size_t index = 0; index < count; index++)
	{
		const unsigned int label_value = label_data[index];
		if (mask_data[index] && label_value)
			sel_labels[label_value]++;
	}
	//clean label list according to the size limit
	for (auto it = sel_labels.begin(); it != sel_labels.end();)
	{
		if (it->second < (unsigned int)slimit)
			it = sel_labels.erase(it);
		else
			++it;
	}
	// wx: with a track map in the view's trace group, the selection of the
	// new time point is the mapped cells (UpdateCellList), not the same values
	TraceGroup* trace_group = m_canvas->GetTraceGroup();
	const bool use_map = trace_group && trace_group->GetTrackMap().GetFrameNum();
	if (use_map)
	{
		FL::CellList cells;
		for (const auto& kv : sel_labels)
		{
			FL::pCell cell(new FL::Cell(kv.first));
			cell->SetSizeUi(kv.second);
			cells.insert(std::pair<unsigned int, FL::pCell>(kv.first, cell));
		}
		trace_group->SetCurTime(m_canvas->GetTSeqCurNum());
		trace_group->SetPrvTime(m_canvas->GetTSeqPrvNum());
		trace_group->UpdateCellList(cells);
	}
	//load and replace the label
	BaseReader* reader = cur->GetReader();
	if (!reader)
		return;
	const int t = m_canvas->GetTSeqCurNum();
	std::wstring lblname = Companion(reader->GetCurName(t, cur->GetCurChannel()), ".lbl").toStdWString();
	LBLReader lbl_reader;
	lbl_reader.SetFile(lblname);
	BaseReader* lr = &lbl_reader;
	auto label_nrrd_new = lr->Convert(t, cur->GetCurChannel(), true);
	if (!label_nrrd_new || !label_nrrd_new->getNrrd())
	{
		cur->AddEmptyLabel();
		label_nrrd_new = cur->GetLabel(false);
	}
	else
		cur->LoadLabel(label_nrrd_new);
	if (!label_nrrd_new || !label_nrrd_new->getNrrd())
		return;
	label_data = (unsigned int*)(label_nrrd_new->getNrrd()->data);
	if (!label_data)
		return;
	//update the mask according to the new label
	std::memset((void*)mask_data, 0, sizeof(unsigned char) * count);
	for (size_t index = 0; index < count; index++)
	{
		if (use_map)
		{
			if (trace_group->FindCell(label_data[index]))
				mask_data[index] = 255;
		}
		else if (sel_labels.find(label_data[index]) != sel_labels.end())
			mask_data[index] = 255;
	}
	//add traces to trace dialog
	if (m_trace_dlg)
		m_trace_dlg->GetSettings(m_canvas);
}

// wx RunRandomColors: the new time point's .lbl, CompExportRandomColor, the
// RGB volumes saved as <savepath><name>_T<t>_COMP<i>.tif (not added)
void MainWindow::RunRandomColors(const vvd::IniFile& fconfig, const VVDString& prefix)
{
	int hmode = 1;
	fconfig.Read(prefix + L"huemode", hmode);
	const QString pathname = ReadStr(fconfig, prefix + L"savepath");
	MakePathDirs(pathname);

	VolumeData* cur = m_script_vol;
	if (!cur)
		return;
	BaseReader* reader = cur->GetReader();
	if (!reader)
		return;
	const int t = m_canvas->GetTSeqCurNum();
	std::wstring lblname = Companion(reader->GetCurName(t, cur->GetCurChannel()), ".lbl").toStdWString();
	LBLReader lbl_reader;
	lbl_reader.SetFile(lblname);
	BaseReader* lr = &lbl_reader;
	auto label_nrrd_new = lr->Convert(t, cur->GetCurChannel(), true);
	if (!label_nrrd_new)
		return;
	cur->LoadLabel(label_nrrd_new);
	const int time_num = reader->GetTimeNum();
	//generate RGB volumes
	m_sel->CompExportRandomColor(hmode, 0, 0, 0, false, false);
	std::vector<VolumeData*>* vol_list = m_sel->GetResultVols();
	if (!vol_list)
		return;
	for (int ii = 0; ii < (int)vol_list->size(); ii++)
	{
		VolumeData* vd = (*vol_list)[ii];
		if (vd)
		{
			const QString str = pathname + vd->GetName().ToQt() + TimeSuffix(time_num, t, m_fr_length) +
				QString("_COMP%1").arg(ii + 1) + ".tif";
			vd->Save(V(str));
			delete vd;
		}
	}
	vol_list->clear();   // the deleted results (wx leaves them in the list until the next export clears it)
}

// wx RunSeparateChannels: the selector's volume saved as
// <savepath><name>_T<t>.tif|.nrrd
void MainWindow::RunSeparateChannels(const vvd::IniFile& fconfig, const VVDString& prefix)
{
	int mode = 0;
	fconfig.Read(prefix + L"format", mode);
	bool bake = false, compression = false;
	fconfig.Read(prefix + L"bake", bake);
	fconfig.Read(prefix + L"compress", compression);
	const QString pathname = ReadStr(fconfig, prefix + L"savepath");
	MakePathDirs(pathname);

	if (QDir(pathname).exists())
	{
		VolumeData* vd = m_sel->GetVolume();
		if (vd && vd->GetReader())
		{
			const int time_num = vd->GetReader()->GetTimeNum();
			const QString str = pathname + vd->GetName().ToQt() +
				TimeSuffix(time_num, m_canvas->GetTSeqCurNum(), m_fr_length) + (mode == 2 ? ".nrrd" : ".tif");
			vd->Save(V(str), mode, bake, compression);
		}
	}
}

// wx RunFetchMask: the new time point's .msk and .lbl replace mask / label
void MainWindow::RunFetchMask(const vvd::IniFile&, const VVDString&)
{
	VolumeData* cur = m_script_vol;
	if (!cur)
		return;
	BaseReader* reader = cur->GetReader();
	if (!reader)
		return;
	const int t = m_canvas->GetTSeqCurNum();
	const std::wstring data_name = reader->GetCurName(t, cur->GetCurChannel());
	std::wstring mskname = Companion(data_name, ".msk").toStdWString();
	MSKReader msk_reader;
	msk_reader.SetFile(mskname);
	BaseReader* mr = &msk_reader;
	auto mask_nrrd_new = mr->Convert(t, cur->GetCurChannel(), true);
	if (mask_nrrd_new)
		cur->LoadMask(mask_nrrd_new);

	//load and replace the label
	std::wstring lblname = Companion(data_name, ".lbl").toStdWString();
	LBLReader lbl_reader;
	lbl_reader.SetFile(lblname);
	BaseReader* lr = &lbl_reader;
	auto label_nrrd_new = lr->Convert(t, cur->GetCurChannel(), true);
	if (label_nrrd_new)
		cur->LoadLabel(label_nrrd_new);
}
