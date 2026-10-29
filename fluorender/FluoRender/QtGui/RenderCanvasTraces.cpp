// RenderCanvas: the view's trace group (wx VRenderVulkanView m_trace_group,
// CreateTraceGroup / LoadTraceGroup / SaveTraceGroup / GetTraces), used by
// the Components & Tracking dialog (TraceDialog) and the 4D script task
// selection_tracking.

#include "RenderCanvas.h"

#include "DataManager.h"
#include "FLIVR/VolumeRenderer.h"

void RenderCanvas::CreateTraceGroup()
{
	m_trace_group = std::make_unique<TraceGroup>();
}

bool RenderCanvas::LoadTraceGroup(const QString& file)
{
	m_trace_group = std::make_unique<TraceGroup>();
	return m_trace_group->Load(VVDString(file.toStdWString()));
}

bool RenderCanvas::SaveTraceGroup(const QString& file)
{
	if (!m_trace_group) return false;
	return m_trace_group->Save(VVDString(file.toStdWString()));
}

// wx VRenderVulkanView::GetTraces
void RenderCanvas::GetTraces()
{
	if (!m_trace_group)
		return;
	//return current mask (into system memory)
	VolumeData* vd = m_cur_vol;
	if (!vd || !vd->GetVR()) return;
	vd->GetVR()->return_mask();
	int nx, ny, nz;
	vd->GetResolution(nx, ny, nz);
	//find labels in the old that are selected by the current mask
	auto mask_nrrd = vd->GetMask(true);
	if (!mask_nrrd || !mask_nrrd->getNrrd()) return;
	auto label_nrrd = vd->GetLabel(false);
	if (!label_nrrd || !label_nrrd->getNrrd()) return;
	unsigned char* mask_data = (unsigned char*)(mask_nrrd->getNrrd()->data);
	if (!mask_data) return;
	unsigned int* label_data = (unsigned int*)(label_nrrd->getNrrd()->data);
	if (!label_data) return;
	FL::CellList sel_labels;
	for (int ii = 0; ii < nx; ii++)
		for (int jj = 0; jj < ny; jj++)
			for (int kk = 0; kk < nz; kk++)
			{
				int index = nx * ny * kk + nx * jj + ii;
				unsigned int label_value = label_data[index];
				if (mask_data[index] && label_value)
				{
					auto label_iter = sel_labels.find(label_value);
					if (label_iter == sel_labels.end())
					{
						FL::pCell cell(new FL::Cell(label_value));
						cell->Inc(ii, jj, kk, 1.0f);
						sel_labels.insert(std::pair<unsigned int, FL::pCell>(label_value, cell));
					}
					else
						label_iter->second->Inc(ii, jj, kk, 1.0f);
				}
			}
	//create id list
	m_trace_group->SetCurTime(m_tseq_cur_num);
	m_trace_group->SetPrvTime(m_tseq_cur_num);
	m_trace_group->UpdateCellList(sel_labels);
	//add traces to trace dialog
	Q_EMIT tracesUpdated();
}
