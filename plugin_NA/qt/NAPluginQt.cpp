#include "NAPluginQt.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QFrame>
#include <QTreeWidget>
#include <QHeaderView>
#include <QTimer>
#include <QProgressDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QProcess>
#include <QCoreApplication>
#include <QRandomGenerator>
#include <thread>
#include <map>
#include <algorithm>

#include "TestRun.h"
#include "QtPluginManager.h"
#include "FileDialogs.h"
#include "RenderCanvas.h"
#include "TreePanel.h"
#include "BrushTool.h"
#include "BrushToolDialog.h"
#include "AdjustPanel.h"
#include "VolumePropPanel.h"
#include "DataManager.h"
#include "Core/FileUtil.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/Texture.h"
#include "FLIVR/TextureBrick.h"
#include "Formats/v3dpbd_reader.h"
#include "Formats/nrrd_writer.h"
#include "Formats/msk_writer.h"

using FLIVR::BBox;
using FLIVR::Point;
using FLIVR::VL_Nrrd;

vvd::Plugin* CreateNAPluginQt()
{
	return new NAPluginQt();
}

namespace
{
	// axis sizes of a (3D or 4D, channel first) nrrd
	void Dims(const std::shared_ptr<VL_Nrrd>& n, size_t& nx, size_t& ny, size_t& nz)
	{
		const Nrrd* r = n->getNrrd();
		const int off = r->dim > 3 ? 1 : 0;
		nx = r->axis[off + 0].size;
		ny = r->axis[off + 1].size;
		nz = r->axis[off + 2].size;
	}
	int LabelAt(const Nrrd* lbl, size_t index)
	{
		if (lbl->type == nrrdTypeUChar) return ((const unsigned char*)lbl->data)[index];
		if (lbl->type == nrrdTypeUShort) return ((const unsigned short*)lbl->data)[index];
		return 0;
	}
	double ValAt(const Nrrd* n, size_t index)
	{
		if (n->type == nrrdTypeUChar) return ((const unsigned char*)n->data)[index];
		if (n->type == nrrdTypeUShort) return ((const unsigned short*)n->data)[index];
		return 0.0;
	}
	// wxImage::Scale(w, h, wxIMAGE_QUALITY_HIGH)
	QImage ScaleTo(const QImage& img, int w)
	{
		if (img.isNull() || img.width() <= 0) return QImage();
		const int h = (int)((double)img.height() / (double)img.width() * w);
		return img.scaled(w, std::max(1, h), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	}
	// wxImage(w, h, data): RGB
	QImage FromRGB(const std::vector<unsigned char>& rgb, int w, int h)
	{
		QImage img(w, h, QImage::Format_RGB888);
		for (int y = 0; y < h; y++)
			memcpy(img.scanLine(y), rgb.data() + (size_t)w * 3 * y, (size_t)w * 3);
		return img;
	}
	// wxCmdLineParser(options) for -l -v -c -s -d (quoted values allowed)
	QString OptionValue(const QStringList& args, const QString& name, bool& found)
	{
		found = false;
		for (int i = 0; i < args.size(); i++)
		{
			const QString& a = args[i];
			if (a == "-" + name && i + 1 < args.size()) { found = true; return args[i + 1]; }
			if (a.startsWith("-" + name + "=")) { found = true; return a.mid(name.size() + 2); }
		}
		return QString();
	}
}

// ---------------------------------------------------------------------------
// plugin (wx NAGuiPlugin)
// ---------------------------------------------------------------------------

NAPluginQt::NAPluginQt()
{
}

NAPluginQt::~NAPluginQt()
{
	delete m_lbl_reader;
	delete m_vol_reader;
}

QWidget* NAPluginQt::CreatePanel(QWidget* parent)
{
	return new NAPanel(this, parent);
}

void NAPluginQt::OnInit()
{
	LoadConfigFile();
}

void NAPluginQt::OnTreeUpdate()
{
	if (!m_lock)
		m_dirty = true;
}

bool NAPluginQt::OnRun(const VVDString& options)
{
	const QStringList args = QProcess::splitCommand(options.ToQt());
	QString label, volume, ch = "sssr", spacings, prefix;
	bool f = false;
	QString v = OptionValue(args, "l", f); if (f) label = v;
	v = OptionValue(args, "v", f); if (f) volume = v;
	v = OptionValue(args, "c", f); if (f) ch = v;
	v = OptionValue(args, "s", f); if (f) spacings = v;
	v = OptionValue(args, "d", f); if (f) prefix = v;
	runNALoader(label, volume, ch, spacings, prefix, true);
	return true;
}

bool NAPluginQt::runNALoader()
{
	return runNALoader(m_id_path, m_vol_path, "sssr");
}

// wx MIPGeneratorThread::Entry for one segment
void NAPluginQt::MakeSegmentMIP(int s)
{
	if (!m_nrrd_s[0] || !m_lbl_nrrd || s < 0 || s >= (int)m_segs.size()) return;
	size_t snx, sny, snz;
	Dims(m_nrrd_s[0], snx, sny, snz);
	const int nx = (int)snx, ny = (int)sny;
	const int mx = 300;
	const int my = (int)((double)mx * ny / nx);
	const double dx = (double)nx / mx;
	const double dy = (double)ny / my;
	std::vector<unsigned char> temp((size_t)mx * my * 3, 0);
	const BBox bbox = m_segs[s].bbox;
	const int sx = (int)(bbox.min().x() / dx);
	const int sy = (int)(bbox.min().y() / dy);
	const int ex = (int)(bbox.max().x() / dx);
	const int ey = (int)(bbox.max().y() / dy);
	const Nrrd* lbl = m_lbl_nrrd->getNrrd();
	for (int i = sx; i <= ex; i++)
	{
		for (int j = sy; j <= ey; j++)
		{
			if (i >= mx || j >= my) continue;
			const size_t xx = (size_t)(i * dx);
			const size_t yy = (size_t)(j * dy);
			double maxvals[3] = {};
			for (int c = 0; c < m_scount; c++)
			{
				if (!m_nrrd_s[c]) continue;
				const Nrrd* sn = m_nrrd_s[c]->getNrrd();
				for (int k = int(bbox.min().z()); k <= int(bbox.max().z()); k++)
				{
					const size_t index = (size_t)nx * ny * k + (size_t)nx * yy + xx;
					if (m_segs[s].id == LabelAt(lbl, index))
					{
						const double val = ValAt(sn, index);
						if (val > maxvals[c]) maxvals[c] = val;
					}
				}
				if (sn->type == nrrdTypeUChar)
					temp[(size_t)mx * 3 * j + 3 * i + c] = (unsigned char)maxvals[c];
				else if (sn->type == nrrdTypeUShort)
					temp[(size_t)mx * 3 * j + 3 * i + c] = (unsigned char)(maxvals[c] / m_gmaxvals[c] * 255.0);
			}
		}
	}
	m_segs[s].image = FromRGB(temp, mx, my);
	m_segs[s].thumbnail = ScaleTo(m_segs[s].image, 300);
}

// bounding boxes / sizes of the labels (wx first pass), the segment list
// (background first, the fragments by size), the MIP threads and the
// reference MIP; vis: the visibility of known label ids (LoadSettings)
void NAPluginQt::BuildSegmentsAndMIPs(size_t nx, size_t ny, size_t nz, const std::map<int, int>* vis)
{
	const Nrrd* lbl = m_lbl_nrrd->getNrrd();
	std::map<int, BBox> seg_bbox;
	std::map<int, size_t> seg_size;
	const double gmax = (m_nrrd_s[0]->getNrrd()->type == nrrdTypeUChar) ? 255.0 : 4096.0;
	const double gmaxval_r = gmax;
	m_gmaxvals[0] = m_gmaxvals[1] = m_gmaxvals[2] = gmax;
	for (size_t i = 0; i < nx; i++)
		for (size_t j = 0; j < ny; j++)
			for (size_t k = 0; k < nz; k++)
			{
				const size_t index = nx * ny * k + nx * j + i;
				const int label = LabelAt(lbl, index);
				if (label > 0)
				{
					auto it = seg_bbox.find(label);
					if (it == seg_bbox.end())
					{
						BBox bbox;
						bbox.extend(Point((double)i, (double)j, (double)k));
						seg_bbox[label] = bbox;
						seg_size[label] = 1;
					}
					else
					{
						it->second.extend(Point((double)i, (double)j, (double)k));
						seg_size[label]++;
					}
				}
			}

	m_segs.clear();
	NASegment bgseg;
	bgseg.id = 0;
	bgseg.size = nx * ny * nz;
	bgseg.bbox = BBox(Point(0, 0, 0), Point((double)nx - 1, (double)ny - 1, (double)nz - 1));
	if (vis && vis->count(0) == 1) bgseg.visible = vis->at(0);
	m_segs.push_back(bgseg);
	for (const auto& kv : seg_bbox)
	{
		NASegment seg;
		seg.id = kv.first;
		seg.size = seg_size[seg.id];
		seg.bbox = kv.second;
		if (vis && vis->count(seg.id) == 1) seg.visible = vis->at(seg.id);
		m_segs.push_back(seg);
	}
	// wx sort_data_asc (s2.size < s1.size): largest first, background kept first
	std::stable_sort(m_segs.begin() + 1, m_segs.end(), [](const NASegment& s1, const NASegment& s2) { return s2.size < s1.size; });

	// wx: GetCPUCount() - 1 detached MIPGeneratorThreads, queues of
	// m_segs.size() / thread_num segments (the last one takes the rest)
	int thread_num = (int)std::thread::hardware_concurrency() - 1;
	if (thread_num <= 0) thread_num = 1;
	if ((int)m_segs.size() / thread_num == 0) thread_num = (int)m_segs.size();
	const int qnum = (int)m_segs.size() / thread_num;
	std::vector<std::thread> threads;
	for (int i = 0; i < thread_num; i++)
	{
		std::vector<int> queue;
		for (int j = 0; j < qnum; j++) queue.push_back(qnum * i + j);
		if (i == thread_num - 1)
		{
			const int remainder = (int)m_segs.size() % thread_num;
			const int offset = qnum * (i + 1);
			for (int k = 0; k < remainder; k++) queue.push_back(offset + k);
		}
		threads.emplace_back([this, queue]() { for (int s : queue) MakeSegmentMIP(s); });
	}

	// the reference MIP (main thread, wx)
	const size_t mx = 300;
	const size_t my = (size_t)(300.0 * ny / nx);
	const double dx = (double)nx / mx;
	const double dy = (double)ny / my;
	std::vector<unsigned char> temp_r(mx * my * 3, 0);
	if (m_nrrd_r)
	{
		const Nrrd* rn = m_nrrd_r->getNrrd();
		for (size_t i = 0; i < mx; i++)
			for (size_t j = 0; j < my; j++)
			{
				const size_t xx = (size_t)(i * dx);
				const size_t yy = (size_t)(j * dy);
				double maxval_r = 0.0;
				for (size_t k = 0; k < nz; k++)
				{
					const double val = ValAt(rn, nx * ny * k + nx * yy + xx);
					if (val > maxval_r) maxval_r = val;
				}
				unsigned char v = 0;
				if (rn->type == nrrdTypeUChar) v = (unsigned char)maxval_r;
				else if (rn->type == nrrdTypeUShort) v = (unsigned char)(maxval_r / gmaxval_r * 255.0);
				temp_r[mx * 3 * j + 3 * i + 0] = v;
				temp_r[mx * 3 * j + 3 * i + 1] = v;
				temp_r[mx * 3 * j + 3 * i + 2] = v;
			}
	}
	for (auto& t : threads) t.join();
	m_ref_image = FromRGB(temp_r, (int)mx, (int)my);
	m_ref_image_thumb = ScaleTo(m_ref_image, 300);
}

bool NAPluginQt::runNALoader(const QString& id_path, const QString& vol_path, const QString& chspec,
	const QString& spacings, const QString& prefix, bool set_dirty)
{
	if (!QFileInfo::exists(id_path) || !QFileInfo::exists(vol_path))
		return false;
	if (!m_host) return false;
	DataManager* dm = m_host->PluginDataManager();
	RenderCanvas* vrv = m_host->PluginView(0);
	TreePanel* tree = m_host->PluginTree();
	BrushToolDialog* brushdlg = m_host->PluginBrushDialog();
	if (!dm || !vrv || !tree || !brushdlg) return false;

	Lock();
	// the groups of the previous NA data leave the view
	if (DataGroup* group = vrv->GetParentGroup(VVDString(m_vol_s[0])))
		m_host->PluginRemoveGroup(group->GetName().ToQt(), vrv);
	if (DataGroup* group = vrv->GetParentGroup(VVDString(m_vol_r)))
		m_host->PluginRemoveGroup(group->GetName().ToQt(), vrv);
	for (int c = 0; c < 3; c++) { m_vol_s[c].clear(); m_nrrd_s[c].reset(); }
	m_vol_r.clear();
	m_nrrd_r.reset();
	m_host->PluginUpdateTree();
	tree->ClearVisHistory();

	delete m_lbl_reader;
	m_lbl_reader = nullptr;
	delete m_vol_reader;
	m_vol_reader = nullptr;

	m_xspc = m_yspc = m_zspc = -1.0;
	if (!spacings.isEmpty())
	{
		const QStringList strspc = spacings.split('x', Qt::SkipEmptyParts);
		if (strspc.size() == 3)
		{
			bool ok = false;
			double val = strspc[0].toDouble(&ok); if (ok) m_xspc = val;
			val = strspc[1].toDouble(&ok); if (ok) m_yspc = val;
			val = strspc[2].toDouble(&ok); if (ok) m_zspc = val;
		}
	}
	m_prefix = prefix;
	if (!m_prefix.isEmpty()) m_prefix += " ";

	m_lbl_reader = new V3DPBDReader();
	std::wstring str_w = id_path.toStdWString();
	m_lbl_reader->SetFile(str_w);
	m_lbl_reader->Preprocess();
	auto v3d_nrrd = m_lbl_reader->Convert(0, 0, true);
	if (!v3d_nrrd || !v3d_nrrd->getNrrd())
	{
		Unlock();
		return false;
	}

	const size_t avmem = (size_t)(vrv->GetAvailableGraphicsMemory(0) * 1024.0 * 1024.0 * 0.8);
	LoadFiles(vol_path, avmem, prefix);
	const int vol_chan = dm->GetLatestVolumeChannelNum();
	const int dot = vol_path.lastIndexOf('.');
	m_vol_suffix = dot >= 0 ? vol_path.mid(dot).toLower() : QString();

	VolumeData* vols[3] = {};
	VolumeData* volr = nullptr;
	int scount = 0;
	for (int c = 0; c < vol_chan; c++)
	{
		if (c >= chspec.length()) continue;
		VolumeData* vd = dm->GetLatestVolumeDataset(c);
		if (!vd || !vd->GetTexture() || !vd->GetTexture()->get_nrrd(0)) continue;
		if (chspec[c] == 's' && scount < 3)
		{
			m_nrrd_s[scount] = vd->GetTexture()->get_nrrd(0);
			m_vol_s[scount] = vd->GetName().ToQt();
			vols[scount] = vd;
			if (m_xspc > 0.0 && m_yspc > 0.0 && m_zspc > 0.0)
				vd->SetSpacings(m_xspc, m_yspc, m_zspc);
			scount++;
		}
		if (chspec[c] == 'r')
		{
			m_nrrd_r = vd->GetTexture()->get_nrrd(0);
			m_vol_r = vd->GetName().ToQt();
			volr = vd;
			if (m_xspc > 0.0 && m_yspc > 0.0 && m_zspc > 0.0)
				vd->SetSpacings(m_xspc, m_yspc, m_zspc);
		}
	}
	if (!m_nrrd_s[0])   // wx dereferenced it anyway
	{
		Unlock();
		return false;
	}

	size_t nx, ny, nz;
	Dims(m_nrrd_s[0], nx, ny, nz);
	if (m_xspc <= 0.0) m_xspc = m_nrrd_s[0]->getNrrd()->axis[(m_nrrd_s[0]->getNrrd()->dim > 3 ? 1 : 0) + 0].spacing;
	if (m_yspc <= 0.0) m_yspc = m_nrrd_s[0]->getNrrd()->axis[(m_nrrd_s[0]->getNrrd()->dim > 3 ? 1 : 0) + 1].spacing;
	if (m_zspc <= 0.0) m_zspc = m_nrrd_s[0]->getNrrd()->axis[(m_nrrd_s[0]->getNrrd()->dim > 3 ? 1 : 0) + 2].spacing;

	Nrrd* scaled = VolumeData::NrrdScale(v3d_nrrd->getNrrd(), nx, ny, nz, false);
	if (!scaled) { Unlock(); return false; }
	m_lbl_nrrd = std::make_shared<VL_Nrrd>(scaled);
	nrrdAxisInfoSet(m_lbl_nrrd->getNrrd(), nrrdAxisInfoSpacing, m_xspc, m_yspc, m_zspc);
	nrrdAxisInfoSet(m_lbl_nrrd->getNrrd(), nrrdAxisInfoMax, m_xspc * nx, m_yspc * ny, m_zspc * nz);
	nrrdAxisInfoSet(m_lbl_nrrd->getNrrd(), nrrdAxisInfoMin, 0.0, 0.0, 0.0);
	nrrdAxisInfoSet(m_lbl_nrrd->getNrrd(), nrrdAxisInfoSize, (size_t)nx, (size_t)ny, (size_t)nz);
	v3d_nrrd.reset();

	for (int c = 0; c < scount; c++)
	{
		if (c == 0) vols[c]->LoadLabel(m_lbl_nrrd);
		else vols[c]->SetSharedLabelName(vols[0]->GetName());
	}
	for (int c = 0; c < scount; c++)
	{
		if (c == 0) vols[c]->AddEmptyMask();
		else vols[c]->SetSharedMaskName(vols[0]->GetName());
	}
	if (volr)
		volr->SetSharedMaskName(vols[0]->GetName());
	m_scount = scount;

	QProgressDialog prg(QObject::tr("Loading labels..."), QString(), 0, 4, m_host->PluginParentWidget());
	prg.setWindowTitle("Neuron Annotator Plugin");
	prg.setWindowModality(Qt::ApplicationModal);
	prg.setMinimumDuration(300);
	prg.setValue(2);
	prg.setLabelText(QObject::tr("Calculating bounding boxes..."));
	BuildSegmentsAndMIPs(nx, ny, nz, nullptr);
	prg.setValue(4);
	prg.setLabelText(QObject::tr("Generating MIP images..."));

	m_id_path = id_path;
	m_vol_path = vol_path;
	m_dirty = set_dirty;
	m_reload_list = set_dirty;

	for (size_t s = 0; s < m_segs.size(); s++)
	{
		const int id = m_segs[s].id;
		for (int c = 0; c < scount; c++)
		{
			vols[c]->SetNAMode(true);
			vols[c]->SetSegmentMask(id, true);
		}
		m_allsig_visible = true;
	}

	if (DataGroup* group = vrv->GetParentGroup(VVDString(m_vol_s[0])))
	{
		group->SetVolumeSyncProp(true);
		group->SetVolumeSyncSpc(true);
		VolumeData* vdr = dm->GetVolumeData(VVDString(m_vol_r));
		if (vdr)
			vrv->MoveLayertoView(group->GetName(), vdr->GetName(), VVDString());
		if (vols[0])
			m_host->PluginUpdateTree(vols[0]->GetName().ToQt(), 2);
		else if (vdr)
			m_host->PluginUpdateTree(vdr->GetName().ToQt(), 2);
	}

	if (BrushTool* bt = m_host->PluginBrushTool(0)) bt->SetSelectGroup(true);
	brushdlg->SetBrushSclTranslate(0.0);
	brushdlg->GetSettings();
	vrv->SetForceHideMask(true);
	// wx InitView(INIT_BOUNDS | INIT_CENTER | INIT_OBJ_TRANSL | INIT_ROTATE)
	// (the rotation is kept with the view defaults, wx m_use_dft_settings)
	vrv->RecomputeBounds();
	vrv->SetObjTrans(0.0, 0.0, 0.0);
	vrv->RefreshView();

	Unlock();
	return true;
}

void NAPluginQt::SwitchPropPanel(bool ref)
{
	if (!m_host) return;
	DataManager* dm = m_host->PluginDataManager();
	RenderCanvas* vrv = m_host->PluginView(0);
	VolumePropPanel* prop = m_host->PluginPropPanel();
	if (!dm || !vrv || !prop) return;
	const QString name = ref ? m_vol_r : m_vol_s[0];
	if (name.isEmpty()) return;
	VolumeData* vd = dm->GetVolumeData(VVDString(name));
	if (!vd || vd == prop->GetVolume()) return;
	DataGroup* group = vrv->GetParentGroup(vd);
	if (AdjustPanel* adj = m_host->PluginAdjustPanel()) adj->SetGroupLink(group);
	// wx OnSelection(2, vrv, group, vd, 0) + vrv->SetVolumeA(vd)
	m_host->PluginSelect(2, vrv, group, vd);
}

void NAPluginQt::ResizeThumbnails(int w)
{
	if (!m_ref_image.isNull())
		m_ref_image_thumb = ScaleTo(m_ref_image, w);
	for (auto& s : m_segs)
		if (!s.image.isNull())
			s.thumbnail = ScaleTo(s.image, w);
}

const QImage* NAPluginQt::getSegMIPThumbnail(int id) const
{
	return (id >= 0 && id < (int)m_segs.size()) ? &m_segs[id].thumbnail : nullptr;
}

const QImage* NAPluginQt::getSegMIP(int id) const
{
	return (id >= 0 && id < (int)m_segs.size()) ? &m_segs[id].image : nullptr;
}

bool NAPluginQt::runNALoaderRemote(const QString& url, const QString& usr, const QString& pwd, const QString& outdir, const QString& ofname)
{
	if (outdir.isEmpty() || ofname.isEmpty() || !m_host) return false;
	VolumeData* vd = m_host->GetCurSelVol();
	if (!vd || !vd->GetTexture() || !vd->GetVR()) return false;
	const QString tmp = QDir::cleanPath(QString::fromStdWString(vvd::GetTempDir()));
	const QString tempvdpath = tmp + "/vvdnbtmpv.nrrd";
	const QString maskfname = tmp + "/vvdnbtmpv.msk";
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
			msk_writer.Save(maskfname.toStdWString(), 0);
			QFile::remove(tempvdpath);
			QFile::rename(maskfname, tempvdpath);
		}
	}
	else
		vd->Save(VVDString(tempvdpath), 2, false, true, true, false);
	static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	QString randname;
	for (int i = 0; i < 16; ++i)
		randname += QChar(alphanum[QRandomGenerator::global()->bounded((int)sizeof(alphanum) - 1)]);
	m_host->PluginUploadFileRemote(url + "opt/NBLAST/input/", randname + ".nrrd", tempvdpath, usr, pwd);
	return true;
}

bool NAPluginQt::LoadSWC(const QString& name, const QString& swc_zip_path)
{
	if (!m_host || !QFileInfo::exists(swc_zip_path)) return false;
	QString ename = name;
	if (QFileInfo(ename).suffix() != "swc") ename += ".swc";
	const QString tmp = QDir::cleanPath(QString::fromStdWString(vvd::GetTempDir()));
	const QString tmpdir = tmp + "/vvd_na_swc";
	std::vector<VVDString> extracted;
	const std::vector<VVDString> exts = { VVDString(L".swc") };
	vvd::UnzipToDir(VVDString(swc_zip_path), VVDString(tmpdir), extracted, true, &exts);
	bool found = false;
	for (const VVDString& f : extracted)
		if (QFileInfo(f.ToQt()).fileName() == QFileInfo(ename).fileName())
		{
			const QString p = tmp + "/" + QFileInfo(ename).fileName();
			QFile::remove(p);
			QFile::copy(f.ToQt(), p);
			m_host->PluginLoadMeshes(QStringList() << p);
			QFile::remove(p);
			found = true;
		}
	QDir(tmpdir).removeRecursively();
	return found;
}

bool NAPluginQt::LoadFiles(const QString& path, size_t datasize, const QString& desc)
{
	if (!m_host) return false;
	QStringList descs;
	if (!desc.isEmpty()) descs << desc;
	m_host->PluginStartupLoad(QStringList() << path, datasize, descs);
	return true;
}

void NAPluginQt::SaveCombinedFragment(const QString& path)
{
	if (!m_nrrd_s[0] || !m_lbl_nrrd) return;
	size_t snx, sny, snz;
	Dims(m_nrrd_s[0], snx, sny, snz);
	const int nx = (int)snx, ny = (int)sny, nz = (int)snz;
	const int off = m_nrrd_s[0]->getNrrd()->dim > 3 ? 1 : 0;
	const double spcx = m_nrrd_s[0]->getNrrd()->axis[off + 0].spacing;
	const double spcy = m_nrrd_s[0]->getNrrd()->axis[off + 1].spacing;
	const double spcz = m_nrrd_s[0]->getNrrd()->axis[off + 2].spacing;
	int bits = 8;
	const int t = m_nrrd_s[0]->getNrrd()->type;
	if (t == nrrdTypeUChar || t == nrrdTypeChar) bits = 8;
	else if (t == nrrdTypeUShort || t == nrrdTypeShort) bits = 16;
	const unsigned long long mem_size = (unsigned long long)nx * (unsigned long long)ny * (unsigned long long)nz;
	Nrrd* nv = nrrdNew();
	if (bits == 8)
	{
		uint8_t* val8 = new (std::nothrow) uint8_t[mem_size];
		if (!val8)
		{
			QMessageBox::warning(m_host ? m_host->PluginParentWidget() : nullptr, "Neuron Annotator Plugin",
				"Not enough memory. Please save project and restart.");
			nrrdNix(nv);
			return;
		}
		memset(val8, 0, mem_size);
		nrrdWrap(nv, val8, nrrdTypeUChar, 3, (size_t)nx, (size_t)ny, (size_t)nz);
	}
	else
	{
		uint16_t* val16 = new (std::nothrow) uint16_t[mem_size];
		if (!val16)
		{
			QMessageBox::warning(m_host ? m_host->PluginParentWidget() : nullptr, "Neuron Annotator Plugin",
				"Not enough memory. Please save project and restart.");
			nrrdNix(nv);
			return;
		}
		memset(val16, 0, mem_size * sizeof(uint16_t));
		nrrdWrap(nv, val16, nrrdTypeUShort, 3, (size_t)nx, (size_t)ny, (size_t)nz);
	}
	nrrdAxisInfoSet(nv, nrrdAxisInfoSpacing, spcx, spcy, spcz);
	nrrdAxisInfoSet(nv, nrrdAxisInfoMax, spcx * nx, spcy * ny, spcz * nz);
	nrrdAxisInfoSet(nv, nrrdAxisInfoMin, 0.0, 0.0, 0.0);
	nrrdAxisInfoSet(nv, nrrdAxisInfoSize, (size_t)nx, (size_t)ny, (size_t)nz);

	const Nrrd* lbl = m_lbl_nrrd->getNrrd();
	for (int s = 0; s < (int)m_segs.size(); s++)
	{
		if (GetSegmentVisibility(s) == 0) continue;
		const BBox bbox = m_segs[s].bbox;
		const int sx = (int)bbox.min().x(), sy = (int)bbox.min().y(), sz = (int)bbox.min().z();
		const int ex = (int)bbox.max().x(), ey = (int)bbox.max().y(), ez = (int)bbox.max().z();
		for (int i = sx; i <= ex; i++)
			for (int j = sy; j <= ey; j++)
				for (int k = sz; k <= ez; k++)
				{
					const size_t index = (size_t)nx * ny * k + (size_t)nx * j + i;
					if (m_segs[s].id != LabelAt(lbl, index)) continue;
					if (bits == 8)
					{
						unsigned char maxval = 0;
						for (int c = 0; c < m_scount; c++)
						{
							if (!m_nrrd_s[c]) continue;
							const unsigned char val = ((unsigned char*)m_nrrd_s[c]->getNrrd()->data)[index];
							if (val > maxval) maxval = val;
						}
						((unsigned char*)nv->data)[index] = maxval;
					}
					else
					{
						unsigned short maxval = 0;
						for (int c = 0; c < m_scount; c++)
						{
							if (!m_nrrd_s[c]) continue;
							const unsigned short val = ((unsigned short*)m_nrrd_s[c]->getNrrd()->data)[index];
							if (val > maxval) maxval = val;
						}
						((unsigned short*)nv->data)[index] = maxval;
					}
				}
	}
	NRRDWriter writer;
	std::shared_ptr<VL_Nrrd> vlnv = std::make_shared<VL_Nrrd>(nv);
	writer.SetData(vlnv);
	writer.SetCompression(true);
	writer.SetSpacings(spcx, spcy, spcz);
	writer.Save(path.toStdWString(), 0);
}

bool NAPluginQt::LoadNrrd(int id)
{
	if (!m_host || !m_nrrd_s[0]) return false;
	DataManager* dm = m_host->PluginDataManager();
	RenderCanvas* vrv = m_host->PluginView(0);
	if (!dm || !vrv) return false;
	QProgressDialog prg(QObject::tr("Uploading volume data to VVDViewer..."), QString(), 0, 0, m_host->PluginParentWidget());
	prg.setWindowTitle("Neuron Annotator Plugin");
	prg.setMinimumDuration(300);
	size_t snx, sny, snz;
	Dims(m_nrrd_s[0], snx, sny, snz);
	const int nx = (int)snx, ny = (int)sny, nz = (int)snz;
	int bits = 8;
	const int t = m_nrrd_s[0]->getNrrd()->type;
	if (t == nrrdTypeUChar || t == nrrdTypeChar) bits = 8;
	else if (t == nrrdTypeUShort || t == nrrdTypeShort) bits = 16;
	const size_t mem_size = (size_t)nx * (size_t)ny * (size_t)nz * (size_t)(bits / 8);
	auto add = [&](const QString& name) -> VolumeData* {
		dm->AddEmptyVolumeData(VVDString(name), bits, nx, ny, nz, m_xspc, m_yspc, m_zspc);
		VolumeData* vd = dm->GetVolumeData(dm->GetVolumeNum() - 1);
		vd->SetSpcFromFile(true);
		dm->SetVolumeDefault(vd);
		if (bits == 16) vd->SetMaxValue(4096.0);
		return vd;
	};
	if (id == -2 && m_nrrd_r)
	{
		VolumeData* vd = add(m_prefix + "Reference");
		memcpy(vd->GetTexture()->get_nrrd(0)->getNrrd()->data, m_nrrd_r->getNrrd()->data, mem_size);
		vd->SetColor(FLIVR::Color(1.0, 1.0, 1.0));
		vrv->AddVolumeData(vd);
		m_host->PluginUpdateTree(vd->GetName().ToQt(), 2);
	}
	else if (id == -1)
	{
		int chan_num = 0;
		for (int i = 0; i < 3; i++)
		{
			if (!m_nrrd_s[i]) continue;
			VolumeData* vd = add(m_prefix + QString("Channel %1").arg(i));
			memcpy(vd->GetTexture()->get_nrrd(0)->getNrrd()->data, m_nrrd_s[i]->getNrrd()->data, mem_size);
			FLIVR::Color color(1.0, 1.0, 1.0);
			if (chan_num == 0) color = FLIVR::Color(1.0, 0.0, 0.0);
			else if (chan_num == 1) color = FLIVR::Color(0.0, 1.0, 0.0);
			else if (chan_num == 2) color = FLIVR::Color(0.0, 0.0, 1.0);
			if (chan_num >= 0 && chan_num < 3) vd->SetColor(color);
			else vd->RandomizeColor();
			vrv->AddVolumeData(vd);
			m_host->PluginUpdateTree(vd->GetName().ToQt(), 2);
			chan_num++;
		}
	}
	else if (id >= 0 && id < (int)m_segs.size())
	{
		VolumeData* vd = add(m_prefix + QString("Fragment %1").arg(id));
		void* dst_data = vd->GetTexture()->get_nrrd(0)->getNrrd()->data;
		int scount = 0;
		for (int c = 0; c < 3; c++, scount++)
			if (!m_nrrd_s[c]) break;
		const Nrrd* lbl = m_lbl_nrrd->getNrrd();
		const BBox bbox = m_segs[id].bbox;
		for (int i = int(bbox.min().x()); i <= int(bbox.max().x()); i++)
			for (int j = int(bbox.min().y()); j <= int(bbox.max().y()); j++)
				for (int k = int(bbox.min().z()); k <= int(bbox.max().z()); k++)
				{
					const size_t index = (size_t)nx * ny * k + (size_t)nx * j + i;
					if (m_segs[id].id != LabelAt(lbl, index)) continue;
					double maxval = 0.0;
					for (int c = 0; c < scount; c++)
					{
						const double val = ValAt(m_nrrd_s[c]->getNrrd(), index);
						if (val > maxval) maxval = val;
					}
					if (bits == 8) ((unsigned char*)dst_data)[index] = (unsigned char)maxval;
					else ((unsigned short*)dst_data)[index] = (unsigned short)maxval;
				}
		vd->RandomizeColor();
		vrv->AddVolumeData(vd);
		m_host->PluginUpdateTree(vd->GetName().ToQt(), 2);
	}
	vrv->RecomputeBounds();
	vrv->RefreshView();
	return true;
}

void NAPluginQt::SetSegmentVisibility(int id, int vis, bool refresh)
{
	if (!m_host) return;
	DataManager* dm = m_host->PluginDataManager();
	if (!dm) return;
	if (id == -2 && m_nrrd_r)
	{
		if (VolumeData* v = dm->GetVolumeData(VVDString(m_vol_r)))
		{
			v->SetDisp(vis > 0);
			// (wx SetVolPopDirty on every view: the Qt views rebuild the shown
			// list on each frame)
		}
		m_host->PluginUpdateTreeIcons();
	}
	else if (id >= 0 && id < (int)m_segs.size())
	{
		m_segs[id].visible = vis;
		for (int i = 0; i < m_scount; i++)
			if (VolumeData* v = dm->GetVolumeData(VVDString(m_vol_s[i])))
			{
				v->SetNAMode(true);
				v->SetSegmentMask(m_segs[id].id, m_segs[id].visible);
			}
	}
	if (refresh)
		m_host->PluginRefreshViews(true);
}

int NAPluginQt::GetSegmentVisibility(int id)
{
	int ret_val = 0;
	if (!m_host) return ret_val;
	DataManager* dm = m_host->PluginDataManager();
	if (!dm) return ret_val;
	if (id == -2 && m_nrrd_r)
	{
		if (VolumeData* v = dm->GetVolumeData(VVDString(m_vol_r)))
			ret_val = v->GetDisp() ? 1 : 0;
	}
	if (id >= 0 && id < (int)m_segs.size())
	{
		for (int i = 0; i < m_scount; i++)
			if (VolumeData* v = dm->GetVolumeData(VVDString(m_vol_s[i])))
				if (v->GetNAMode())
					ret_val = v->GetSegmentMask(m_segs[id].id);
	}
	return ret_val;
}

void NAPluginQt::ToggleSegmentVisibility(int id)
{
	if (!m_host) return;
	DataManager* dm = m_host->PluginDataManager();
	if (!dm) return;
	if (id == -2 && m_nrrd_r)
	{
		if (VolumeData* v = dm->GetVolumeData(VVDString(m_vol_r)))
			v->SetDisp(!v->GetDisp());
		m_host->PluginUpdateTreeIcons();
	}
	if (id >= 0 && id < (int)m_segs.size())
	{
		m_segs[id].visible = !m_segs[id].visible;
		for (int i = 0; i < m_scount; i++)
			if (VolumeData* v = dm->GetVolumeData(VVDString(m_vol_s[i])))
			{
				v->SetNAMode(true);
				v->SetSegmentMask(m_segs[id].id, m_segs[id].visible);
			}
	}
	m_host->PluginRefreshViews(true);
}

void NAPluginQt::PushVisHistory()
{
	if (m_host && m_host->PluginTree()) m_host->PluginTree()->PushVisHistory();
}

void NAPluginQt::RefreshRenderViews()
{
	if (m_host) m_host->PluginRefreshViews(true);
}

QString NAPluginQt::ConfigFilePath(bool for_save)
{
	const QString name = "NA_plugin_settings.dft";
	const QString expath = QCoreApplication::applicationDirPath();
#ifdef __APPLE__
	(void)for_save;
	return expath + "/../Resources/" + name;
#else
	QString dft = expath + "/" + name;
	const QString dft2 = QString::fromStdWString(vvd::GetUserConfigDir()) + "/" + name;
	if (for_save) { if (!QFileInfo::exists(dft) && QFileInfo::exists(dft2)) dft = dft2; }
	else if (!QFileInfo::exists(dft)) dft = dft2;
	return dft;
#endif
}

void NAPluginQt::LoadConfigFile()
{
	LoadProjectSettingFile(VVDString(ConfigFilePath(false)));
}

void NAPluginQt::SaveConfigFile()
{
	if (VVDQtScriptedRun()) return;   // TestRun.h
	SaveProjectSettingFile(VVDString(ConfigFilePath(true)));
}

void NAPluginQt::LoadProjectSettingFile(const VVDString& path)
{
	if (!vvd::FileExists(path)) return;
	vvd::IniFile fconfig;
	if (!fconfig.Load(path)) return;
	VVDString str;
	int ival = 0;
	double dval = 0.0;
	if (fconfig.Read(L"Reference", str)) m_vol_r = str.ToQt();
	if (fconfig.Read(L"Signal1", str)) m_vol_s[0] = str.ToQt();
	if (fconfig.Read(L"Signal2", str)) m_vol_s[1] = str.ToQt();
	if (fconfig.Read(L"Signal3", str)) m_vol_s[2] = str.ToQt();
	if (fconfig.Read(L"s_count", ival)) m_scount = ival;
	if (fconfig.Read(L"xspc", dval)) m_xspc = dval;
	if (fconfig.Read(L"yspc", dval)) m_yspc = dval;
	if (fconfig.Read(L"zspc", dval)) m_zspc = dval;
	if (fconfig.Read(L"id_path", str)) m_id_path = str.ToQt();
	if (fconfig.Read(L"vol_path", str)) m_vol_path = str.ToQt();
	if (fconfig.Read(L"prefix", str)) m_prefix = str.ToQt();
	int seg_count = 0;
	if (fconfig.Read(L"seg_num", ival)) seg_count = ival;
	m_segs.clear();
	for (int i = 0; i < seg_count; i++)
	{
		if (!fconfig.Read(VVDString(QString("seg_%1").arg(i)), str)) continue;
		NASegment seg;
		const QStringList p = str.ToQt().split(',', Qt::SkipEmptyParts);
		if (p.size() == 2)
		{
			bool ok = false;
			long v = p[0].toLong(&ok); if (ok) seg.id = (int)v;
			v = p[1].toLong(&ok); if (ok) seg.visible = (int)v;
		}
		m_segs.push_back(seg);
	}
	LoadSettings();
}

// wx LoadSettings: the NA state of volumes already loaded (a project)
void NAPluginQt::LoadSettings()
{
	if (!m_host) return;
	DataManager* dm = m_host->PluginDataManager();
	RenderCanvas* vrv = m_host->PluginView(0);
	TreePanel* tree = m_host->PluginTree();
	if (!dm || !vrv || !tree || !m_host->PluginBrushDialog()) return;
	Lock();
	m_host->PluginUpdateTree();
	tree->ClearVisHistory();
	delete m_lbl_reader;
	m_lbl_reader = nullptr;
	delete m_vol_reader;
	m_vol_reader = nullptr;

	m_lbl_nrrd.reset();
	VolumeData* vd_s = dm->GetVolumeData(VVDString(m_vol_s[0]));
	if (vd_s && !vd_s->GetSharedLabelName().empty())
		vd_s = dm->GetVolumeData(vd_s->GetSharedLabelName());
	if (vd_s && vd_s->GetTexture())
	{
		FLIVR::Texture* tex = vd_s->GetTexture();
		if (tex->nlabel() >= 0) m_lbl_nrrd = tex->get_nrrd(tex->nlabel());
	}
	if (!m_lbl_nrrd) return;   // (wx: stays locked)

	m_nrrd_r.reset();
	if (VolumeData* vd_r = dm->GetVolumeData(VVDString(m_vol_r)))
		if (vd_r->GetTexture()) m_nrrd_r = vd_r->GetTexture()->get_nrrd(0);
	for (int i = 0; i < 3; i++)
	{
		m_nrrd_s[i].reset();
		if (VolumeData* v = dm->GetVolumeData(VVDString(m_vol_s[i])))
			if (v->GetTexture()) m_nrrd_s[i] = v->GetTexture()->get_nrrd(0);
	}
	if (!m_nrrd_s[0]) return;
	size_t nx, ny, nz;
	Dims(m_nrrd_s[0], nx, ny, nz);

	QProgressDialog prg(QObject::tr("Loading labels..."), QString(), 0, 4, m_host->PluginParentWidget());
	prg.setWindowTitle("Neuron Annotator Plugin");
	prg.setWindowModality(Qt::ApplicationModal);
	prg.setMinimumDuration(300);
	prg.setValue(2);
	std::map<int, int> seg_vis;
	for (const auto& s : m_segs) seg_vis[s.id] = s.visible;
	BuildSegmentsAndMIPs(nx, ny, nz, &seg_vis);
	prg.setValue(4);

	m_dirty = true;
	m_reload_list = true;
	for (const auto& s : m_segs)
		for (int c = 0; c < m_scount; c++)
			if (VolumeData* v = dm->GetVolumeData(VVDString(m_vol_s[c])))
			{
				v->SetNAMode(true);
				v->SetSegmentMask(s.id, s.visible);
			}
	vrv->RefreshView();
	Unlock();
}

void NAPluginQt::SaveProjectSettingFile(const VVDString& path)
{
	vvd::IniFile fconfig;
	fconfig.Write(L"Reference", VVDString(m_vol_r));
	fconfig.Write(L"Signal1", VVDString(m_vol_s[0]));
	fconfig.Write(L"Signal2", VVDString(m_vol_s[1]));
	fconfig.Write(L"Signal3", VVDString(m_vol_s[2]));
	fconfig.Write(L"s_count", m_scount);
	fconfig.Write(L"xspc", m_xspc);
	fconfig.Write(L"yspc", m_yspc);
	fconfig.Write(L"zspc", m_zspc);
	fconfig.Write(L"id_path", VVDString(m_id_path));
	fconfig.Write(L"vol_path", VVDString(m_vol_path));
	fconfig.Write(L"prefix", VVDString(m_prefix));
	fconfig.Write(L"seg_num", (int)m_segs.size());
	// wx wrote m_segs[i].visible, which misses changes made in the view (its
	// context menu sets the segment masks directly); the masks' state here
	for (size_t i = 0; i < m_segs.size(); i++)
	{
		const int vis = m_scount > 0 ? GetSegmentVisibility((int)i) : m_segs[i].visible;
		fconfig.Write(VVDString(QString("seg_%1").arg(i)), VVDString(QString("%1,%2").arg(m_segs[i].id).arg(vis)));
	}
	fconfig.Save(path);
}

// ---------------------------------------------------------------------------
// wx NAListCtrl
// ---------------------------------------------------------------------------

NAResultList::NAResultList(QWidget* parent)
	: QWidget(parent)
{
	auto* l = new QVBoxLayout(this);
	l->setContentsMargins(0, 0, 0, 0);
	m_tree = new QTreeWidget(this);
	m_tree->setObjectName("NAResultList");
	m_tree->setColumnCount(4);
	m_tree->setHeaderLabels({ QString(), QString(), tr("Name"), tr("Thumbnail") });
	m_tree->setRootIsDecorated(false);
	// wxLC_REPORT (no wxLC_SINGLE_SEL): several rows can be selected
	m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_tree->header()->setStretchLastSection(false);
	m_tree->setColumnWidth(0, 25);
	m_tree->setColumnHidden(1, true);   // wx width 0 ("+" when visible)
	m_tree->setColumnWidth(2, 110);
	m_tree->setColumnWidth(3, 300);
	m_tree->installEventFilter(this);
	m_tree->viewport()->installEventFilter(this);
	l->addWidget(m_tree);
	m_selTimer = new QTimer(this);
	m_selTimer->setSingleShot(true);
	connect(m_selTimer, &QTimer::timeout, this, &NAResultList::OnTimer);
	connect(m_tree, &QTreeWidget::itemChanged, this, &NAResultList::OnItemChanged);
	connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &NAResultList::OnSelectionChanged);
	// wx OnLeftDClick: toggle the check box of the row under the pointer
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
		const int item = m_tree->indexOfTopLevelItem(it);
		if (item >= 0) CheckItem(item, !IsItemChecked(item));
	});
}

int NAResultList::GetItemCount() const { return m_tree->topLevelItemCount(); }

int NAResultList::ItemImgId(int item) const
{
	QTreeWidgetItem* it = m_tree->topLevelItem(item);
	return it ? it->data(0, Qt::UserRole).toInt() : 0;
}

bool NAResultList::IsItemChecked(int item) const
{
	QTreeWidgetItem* it = m_tree->topLevelItem(item);
	return it && it->checkState(0) == Qt::Checked;
}

void NAResultList::CheckItem(int item, bool check)
{
	if (QTreeWidgetItem* it = m_tree->topLevelItem(item))
		it->setCheckState(0, check ? Qt::Checked : Qt::Unchecked);
}

bool NAResultList::IsItemSelected(int item) const
{
	QTreeWidgetItem* it = m_tree->topLevelItem(item);
	return it && it->isSelected();
}

void NAResultList::SetItemSelected(int item, bool sel)
{
	if (QTreeWidgetItem* it = m_tree->topLevelItem(item))
		it->setSelected(sel);
}

int NAResultList::ThumbnailWidth() const
{
	const int namew = m_tree->columnWidth(2) + (m_tree->isColumnHidden(1) ? 0 : m_tree->columnWidth(1)) + m_tree->columnWidth(0) + 20;
	return m_tree->viewport()->width() - namew;
}

void NAResultList::Append(int imgid, const QString& name, int mipid, bool visibility)
{
	auto* it = new QTreeWidgetItem();
	it->setData(0, Qt::UserRole, imgid);
	it->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
	it->setCheckState(0, Qt::Unchecked);
	it->setText(1, visibility ? "+" : "");
	it->setText(2, name);
	it->setData(3, Qt::UserRole, mipid);
	if (mipid >= 0 && mipid < (int)m_images.size())
		it->setData(3, Qt::DecorationRole, QPixmap::fromImage(m_images[mipid]));
	m_tree->addTopLevelItem(it);
}

void NAResultList::LoadResults(const QString& idpath, const QString& volpath, const QString& chspec, const QString& prefix)
{
	if (!m_plugin) return;
	m_plugin->runNALoader(idpath, volpath, chspec, QString(), prefix);
	m_plugin->SetSegmentVisibility(IMG_ID_REF, false);
	for (int i = 0; i < m_plugin->getSegCount(); i++)
		m_plugin->SetSegmentVisibility(i, true);
	UpdateResults(true);
}

void NAResultList::UpdateResults(bool refresh_items)
{
	if (!m_plugin || m_plugin->getSegCount() <= 0) return;
	m_updating = true;
	const QSignalBlocker block(m_tree);
	const int id_offset = m_plugin->isRefExists() ? 1 : 0;
	bool ref_selected = false, bg_selected = false;
	if (GetItemCount() > 0 && m_plugin->isRefExists()) ref_selected = IsItemSelected(0);
	if (GetItemCount() > id_offset) bg_selected = IsItemSelected(id_offset);

	if (refresh_items)
	{
		m_tree->clear();
		m_listdata.clear();
		m_images.clear();
		const int tw = ThumbnailWidth();
		if (tw > 0) m_plugin->ResizeThumbnails(tw);
		const QImage* t0 = m_plugin->getSegMIPThumbnail(0);
		const int w = t0 ? t0->width() : 0, h = t0 ? t0->height() : 0;
		m_tree->setIconSize(QSize(w, h));
		int img_count = 0;
		if (m_plugin->isRefExists())
		{
			NAListItemData ref_data;
			ref_data.name = "Reference";
			ref_data.mipid = img_count;
			ref_data.imgid = IMG_ID_REF;
			ref_data.visibility = m_plugin->GetSegmentVisibility(IMG_ID_REF);
			m_images.push_back(*m_plugin->getRefMIPThumbnail());
			m_listdata.push_back(ref_data);
			Append(IMG_ID_REF, ref_data.name, ref_data.mipid, ref_data.visibility > 0);
			img_count++;
		}
		for (int i = 0; i < m_plugin->getSegCount(); i++)
		{
			NAListItemData data;
			data.name = i == 0 ? QString("Background") : QString("Fragment %1").arg(i);
			data.mipid = img_count;
			data.imgid = i;
			data.visibility = m_plugin->GetSegmentVisibility(i);
			m_images.push_back(*m_plugin->getSegMIPThumbnail(i));
			m_listdata.push_back(data);
			Append(data.imgid, data.name, data.mipid, data.visibility > 0);
			img_count++;
		}
		m_plugin->setReloaded(false);
#ifdef __APPLE__
		m_tree->setColumnWidth(3, w + 8);
#else
		m_tree->setColumnWidth(3, w + 2);
#endif
	}
	else
	{
		if (m_plugin->isRefExists() && !m_listdata.empty())
			m_listdata[0].visibility = m_plugin->GetSegmentVisibility(IMG_ID_REF);
		for (int i = 0; i < m_plugin->getSegCount() && i + id_offset < (int)m_listdata.size(); i++)
			m_listdata[id_offset + i].visibility = m_plugin->GetSegmentVisibility(i);
	}

	if (ref_selected) SetItemSelected(0, true);
	if (GetItemCount() > 0 && m_plugin->isRefExists())
		CheckItem(0, m_listdata[0].visibility > 0);
	if (bg_selected) SetItemSelected(1, true);
	if (GetItemCount() > id_offset)
		CheckItem(id_offset, m_listdata[id_offset].visibility > 0);
	for (int i = id_offset + 1; i < GetItemCount() && i < (int)m_listdata.size(); i++)
	{
		SetItemSelected(i, m_listdata[i].visibility == 2);
		CheckItem(i, m_listdata[i].visibility > 0);
	}
	m_prev_sel.assign(GetItemCount(), false);
	for (int i = 0; i < GetItemCount(); i++) m_prev_sel[i] = IsItemSelected(i);
	m_updating = false;
	m_plugin->setDirty(false);
}

void NAResultList::UpdateSelections()
{
	// wx UpdateSelections (assumes the reference in row 0, the background in row 1)
	if (!m_plugin || m_plugin->getSegCount() <= 0) return;
	m_updating = true;
	const QSignalBlocker block(m_tree);
	const bool ref_selected = GetItemCount() > 0 && IsItemSelected(0);
	const bool bg_selected = GetItemCount() > 1 && IsItemSelected(1);
	if (m_plugin->isRefExists() && !m_listdata.empty())
		m_listdata[0].visibility = m_plugin->GetSegmentVisibility(IMG_ID_REF);
	const int offset = m_plugin->isRefExists() ? 1 : 0;
	for (int i = 0; i < m_plugin->getSegCount() && i + offset < (int)m_listdata.size(); i++)
		m_listdata[offset + i].visibility = m_plugin->GetSegmentVisibility(i);
	if (ref_selected) SetItemSelected(0, true);
	if (GetItemCount() > 0 && !m_listdata.empty()) CheckItem(0, m_listdata[0].visibility > 0);
	if (bg_selected) SetItemSelected(1, true);
	if (GetItemCount() > 1 && m_listdata.size() > 1) CheckItem(1, m_listdata[1].visibility > 0);
	for (int i = 2; i < GetItemCount() && i < (int)m_listdata.size(); i++)
	{
		SetItemSelected(i, m_listdata[i].visibility == 2);
		CheckItem(i, m_listdata[i].visibility > 0);
	}
	m_prev_sel.assign(GetItemCount(), false);
	for (int i = 0; i < GetItemCount(); i++) m_prev_sel[i] = IsItemSelected(i);
	m_updating = false;
	m_plugin->setDirty(false);
}

void NAResultList::DeleteSelection()
{
	int item = -1;
	for (int i = 0; i < GetItemCount(); i++)
		if (IsItemSelected(i)) { item = i; break; }
	if (item < 0) return;
	const bool last = item + 1 >= GetItemCount();
	NAListItemData hdata;
	QTreeWidgetItem* it = m_tree->topLevelItem(item);
	hdata.imgid = ItemImgId(item);
	hdata.name = it->text(2);
	hdata.mipid = it->data(3, Qt::UserRole).toInt();
	hdata.itemid = item;
	hdata.visibility = IsItemChecked(item) ? 1 : 0;
	AddHistory(hdata);
	m_updating = true;
	delete m_tree->takeTopLevelItem(item);
	m_updating = false;
	const int sel = last ? item - 1 : item;
	if (sel >= 0 && sel < GetItemCount()) SetItemSelected(sel, true);
}

void NAResultList::DeleteAll()
{
	m_tree->clear();
	m_history.clear();
	m_history_pos = 0;
}

void NAResultList::AddHistory(const NAListItemData& data)
{
	if (m_history_pos < (int)m_history.size())
	{
		if (m_history_pos > 0)
			m_history = std::vector<NAListItemData>(m_history.begin(), m_history.begin() + m_history_pos);
		else
		{
			m_history.clear();
			m_history_pos = 0;
		}
	}
	if (m_history_pos >= 300)
	{
		m_history.erase(m_history.begin());
		m_history.push_back(data);
	}
	else
	{
		m_history.push_back(data);
		m_history_pos++;
	}
}

void NAResultList::Undo()
{
	if (m_history_pos <= 0) return;
	m_history_pos--;
	const NAListItemData comdata = m_history[m_history_pos];
	// wx Append(imgid, name, mipid, itemid): appended at the end
	m_updating = true;
	Append(comdata.imgid, comdata.name, comdata.mipid, comdata.itemid != 0);
	m_updating = false;
}

void NAResultList::Redo()
{
	if (m_history_pos >= (int)m_history.size()) return;
	const NAListItemData comdata = m_history[m_history_pos];
	m_updating = true;
	if (comdata.itemid >= 0 && comdata.itemid < GetItemCount())
		delete m_tree->takeTopLevelItem(comdata.itemid);
	m_updating = false;
	m_history_pos++;
}

// wx OnItemChecked / OnItemUnchecked
void NAResultList::OnItemChanged(QTreeWidgetItem* it, int column)
{
	if (m_updating || column != 0 || !m_plugin) return;
	const int id = it->data(0, Qt::UserRole).toInt();
	const bool checked = it->checkState(0) == Qt::Checked;
	m_plugin->PushVisHistory();
	if (checked)
		m_plugin->SetSegmentVisibility(id, id > 0 ? 2 : 1);   // NA_SET_ACTIVE / NA_SET_VISIBLE
	else
		m_plugin->SetSegmentVisibility(id, 0);                // NA_SET_INVISIBLE
	it->setText(1, checked ? "+" : "");
}

// wx OnSelect / OnDeselect, one call per row whose state changed
void NAResultList::OnSelectionChanged()
{
	if (m_updating || !m_plugin) return;
	const int n = GetItemCount();
	m_prev_sel.resize(n, false);
	for (int i = 0; i < n; i++)
	{
		const bool sel = IsItemSelected(i);
		if (sel == m_prev_sel[i]) continue;
		m_prev_sel[i] = sel;
		const int id = ItemImgId(i);
		if (sel)
		{
			if (id <= 0 && IsItemChecked(i))
				m_plugin->SwitchPropPanel(id == IMG_ID_REF);
			else if (id > 0 && IsItemChecked(i))
			{
				m_selTimer->start(30);   // wx StartOnce(30)
				m_selbuf.push_back({ id, 2 });
				m_plugin->SwitchPropPanel(false);
			}
		}
		else if (id > 0 && IsItemChecked(i))
		{
			m_selTimer->start(30);
			m_selbuf.push_back({ id, 1 });
		}
	}
}

void NAResultList::OnTimer()
{
	if (!m_selbuf.empty() && m_plugin)
	{
		m_plugin->PushVisHistory();   // NA_PUSH_HISTORY
		for (const Sel& d : m_selbuf)
		{
			if (d.visibility == 1) m_plugin->SetSegmentVisibility(d.itemid, 1, false);
			else if (d.visibility == 2) m_plugin->SetSegmentVisibility(d.itemid, 2, false);
		}
		m_plugin->RefreshRenderViews();   // NA_REFRESH_RENDERVIEWS
	}
	m_selbuf.clear();
}

bool NAResultList::eventFilter(QObject* o, QEvent* e)
{
	if (o == m_tree && e->type() == QEvent::KeyPress)
	{
		auto* ke = static_cast<QKeyEvent*>(e);
		const Qt::KeyboardModifiers m = ke->modifiers();
		const bool ctrl = m & Qt::ControlModifier, shift = m & Qt::ShiftModifier, alt = m & Qt::AltModifier;
		// wx OnKeyDown (Delete / Backspace commented out in wx)
		if (ke->key() == Qt::Key_Z && ctrl && !shift && !alt) { Undo(); return true; }
		if (ke->key() == Qt::Key_Y && ctrl && !shift && !alt) { Redo(); return true; }
		if (ke->key() == Qt::Key_Z && ctrl && shift && !alt) { Redo(); return true; }
	}
	// wx OnSize: the thumbnails follow the list width
	if (o == m_tree->viewport() && e->type() == QEvent::Resize && m_plugin &&
		(m_plugin->isRefExists() || m_plugin->isSegExists()))
	{
		const int w = static_cast<QResizeEvent*>(e)->size().width();
		if (w != m_last_width)
		{
			m_last_width = w;
			QTimer::singleShot(0, this, [this]() { UpdateResults(true); });
		}
	}
	return QWidget::eventFilter(o, e);
}

// ---------------------------------------------------------------------------
// wx NAGuiPluginWindow
// ---------------------------------------------------------------------------

NAPanel::NAPanel(NAPluginQt* plugin, QWidget* parent)
	: QWidget(parent), m_plugin(plugin)
{
	setObjectName("NAPanel");
	auto* top = new QHBoxLayout(this);
	top->setContentsMargins(0, 0, 0, 0);
	auto* nbpanel = new QFrame(this);
	nbpanel->setFrameShape(QFrame::Box);
	m_nbpanel = nbpanel;
	auto* v = new QVBoxLayout(nbpanel);
	v->setContentsMargins(0, 0, 0, 0);
	m_tb = new QToolBar(nbpanel);
	m_tb->setObjectName("NAToolBar");
	m_tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
	QAction* imp = m_tb->addAction(tr("Import"), this, &NAPanel::OnImportResults);
	imp->setToolTip(tr("Import search results"));
	m_tb->addSeparator();
	QAction* exp = m_tb->addAction(tr("export"), this, &NAPanel::OnExport);
	exp->setToolTip(tr("Export combined fragments"));
	v->addWidget(m_tb);
	auto* line = new QFrame(nbpanel);
	line->setFrameShape(QFrame::HLine);
	v->addWidget(line);
	m_results = new NAResultList(nbpanel);
	m_results->SetPlugin(plugin);
	v->addWidget(m_results, 1);
	top->addWidget(nbpanel);
	nbpanel->setMinimumSize(300, 400);
	m_idleTimer = new QTimer(this);
	connect(m_idleTimer, &QTimer::timeout, this, &NAPanel::OnIdle);
	m_idleTimer->start(100);
}

NAPanel::~NAPanel()
{
	m_idleTimer->stop();
	if (m_plugin) m_plugin->SaveConfigFile();
}

void NAPanel::OnIdle()
{
	if (!isVisible() || !m_results || !m_plugin) return;
	if (m_plugin->isDirty())
		m_results->UpdateResults(m_plugin->isReloaded());
}

void NAPanel::OnImportResults()
{
	if (!m_results || !m_plugin) return;
	auto pick = [this](const char* name, const QString& title, const QString& filter) {
		QFileDialog dlg(this, title, QString(), filter);
		dlg.setOption(QFileDialog::DontUseNativeDialog, true);
		dlg.setFileMode(QFileDialog::ExistingFile);
		if (FileDialogs::Exec(dlg, name) != QDialog::Accepted) return QString();
		const QStringList f = dlg.selectedFiles();
		return f.isEmpty() ? QString() : f.first();
	};
	const QString idpath = pick("NAIMPORT", tr("Choose a label"), "*.v3dpbd");
	if (idpath.isEmpty()) return;
	const int dot = idpath.lastIndexOf('.');
	QString volpath = (dot >= 0 ? idpath.left(dot) : QString()) + ".h5j";
	if (!QFileInfo::exists(volpath))
	{
		volpath = pick("NAIMPORTVOL", tr("Choose a confocal image file (h5j)"), tr("All Supported (*.h5j)"));
		if (volpath.isEmpty()) return;
	}
	m_results->LoadResults(idpath, volpath, "sssr", "");
	if (m_results->GetItemCount() > 0) m_nbpanel->setMinimumSize(1, 1);
	else m_nbpanel->setMinimumSize(400, 700);
}

void NAPanel::OnExport()
{
	if (!m_results || !m_plugin) return;
	QString vname = QFileInfo(m_plugin->getVolumePath()).completeBaseName();
	QFileDialog dlg(this, tr("Save Combined Fragments"), QString(), tr("Nrrd files (*.nrrd)"));
	dlg.setOption(QFileDialog::DontUseNativeDialog, true);
	dlg.setAcceptMode(QFileDialog::AcceptSave);
	dlg.setFileMode(QFileDialog::AnyFile);
	dlg.setDefaultSuffix("nrrd");
	dlg.selectFile(vname);
	if (FileDialogs::Exec(dlg, "NAEXPORT") != QDialog::Accepted) return;
	const QStringList f = dlg.selectedFiles();
	if (f.isEmpty()) return;
	m_plugin->SaveCombinedFragment(f.first());
}
