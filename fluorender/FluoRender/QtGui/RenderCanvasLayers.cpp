// Layer list of the Qt render canvas: port of the VRenderVulkanView
// (VRenderView.cpp) m_layer_list model and API -- AddVolumeData,
// AddMeshData, AddGroup, AddMGroup, Remove*, Get*, Isolate, ShowAll,
// RandomizeColor, OrganizeLayers, the Move* family used by the workspace
// tree's drag & drop, PopVolumeList / PopMeshList.
//
// Differences from wx (the canvas does not own data):
//   * RemoveVolumeData / RemoveMeshData / RemoveGroup only take the data out
//     of the view; the caller removes them from the DataManager.
//   * The AdjustView calls of wx (SetVolumeData / SetGroupLink / UpdateSync)
//     are the groupLinkChanged signal; AdjustView::GetDefaults in AddGroup is
//     the SetGroupDefaultsProvider callback.
// m_volumes / m_meshes are kept as flat lists of every volume / mesh in layer
// order (SyncFlatLists) for the code that only needs "all data of the view".

#include "RenderCanvas.h"

#include <algorithm>

#include "VVulkan.h"
#include "Vulkan/VulkanDevice.hpp"
#include "FLIVR/VolumeRenderer.h"
#include "DataManager.h"

using namespace FLIVR;

// ---------------------------------------------------------------------------
// flat lists / pop lists
// ---------------------------------------------------------------------------

void RenderCanvas::SyncFlatLists()
{
	m_volumes.clear();
	m_meshes.clear();
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer) continue;
		switch (layer->IsA())
		{
		case 2: m_volumes.push_back((VolumeData*)layer); break;
		case 3: m_meshes.push_back((MeshData*)layer); break;
		case 5:
		{
			DataGroup* group = (DataGroup*)layer;
			for (int j = 0; j < group->GetVolumeNum(); j++)
				if (VolumeData* vd = group->GetVolumeData(j)) m_volumes.push_back(vd);
		}
		break;
		case 6:
		{
			MeshGroup* group = (MeshGroup*)layer;
			for (int j = 0; j < group->GetMeshNum(); j++)
				if (MeshData* md = group->GetMeshData(j)) m_meshes.push_back(md);
		}
		break;
		}
	}
	if (m_cur_vol && std::find(m_volumes.begin(), m_volumes.end(), m_cur_vol) == m_volumes.end())
		m_cur_vol = nullptr;
	if (m_cur_mesh && std::find(m_meshes.begin(), m_meshes.end(), m_cur_mesh) == m_meshes.end())
		m_cur_mesh = nullptr;
	m_md_pop_list.erase(std::remove_if(m_md_pop_list.begin(), m_md_pop_list.end(),
		[this](MeshData* md) { return std::find(m_meshes.begin(), m_meshes.end(), md) == m_meshes.end(); }),
		m_md_pop_list.end());
}

void RenderCanvas::LayersChanged(bool bounds)
{
	SyncFlatLists();
	if (bounds)
		ComputeBounds();   // wx InitView(INIT_BOUNDS | INIT_CENTER)
	RequestRender();
}

// verbatim from VRenderVulkanView::PopVolumeList (the quality combo toggle
// lives in StartLoopUpdate)
std::vector<VolumeData*> RenderCanvas::PopVolumeList() const
{
	std::vector<VolumeData*> list;
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer) continue;
		switch (layer->IsA())
		{
		case 2://volume data
		{
			VolumeData* vd = (VolumeData*)layer;
			if (vd->GetDisp())
				list.push_back(vd);
		}
		break;
		case 5://group
		{
			DataGroup* group = (DataGroup*)layer;
			if (!group->GetDisp())
				continue;
			for (int j = 0; j < group->GetVolumeNum(); j++)
			{
				VolumeData* vd = group->GetVolumeData(j);
				if (vd && vd->GetDisp())
					list.push_back(vd);
			}
		}
		break;
		}
	}
	return list;
}

// verbatim from VRenderVulkanView::PopMeshList
std::vector<MeshData*> RenderCanvas::GetPopMeshList() const
{
	std::vector<MeshData*> list;
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer) continue;
		switch (layer->IsA())
		{
		case 3://mesh data
		{
			MeshData* md = (MeshData*)layer;
			if (md->GetDisp())
				list.push_back(md);
		}
		break;
		case 6://mesh group
		{
			MeshGroup* group = (MeshGroup*)layer;
			if (!group->GetDisp())
				continue;
			for (int j = 0; j < group->GetMeshNum(); j++)
			{
				MeshData* md = group->GetMeshData(j);
				if (md && md->GetDisp())
					list.push_back(md);
			}
		}
		break;
		}
	}
	return list;
}

void RenderCanvas::PopMeshList()
{
	m_md_pop_list = GetPopMeshList();
}

// ---------------------------------------------------------------------------
// queries
// ---------------------------------------------------------------------------

TreeLayer* RenderCanvas::GetLayer(int index) const
{
	if (index >= 0 && index < (int)m_layer_list.size())
		return m_layer_list[index];
	return nullptr;
}

int RenderCanvas::GetGroupNum() const
{
	int n = 0;
	for (TreeLayer* layer : m_layer_list)
		if (layer && layer->IsA() == 5) n++;
	return n;
}

DataGroup* RenderCanvas::GetGroup(const VVDString& name) const
{
	for (TreeLayer* layer : m_layer_list)
		if (layer && layer->IsA() == 5 && layer->GetName() == name)
			return (DataGroup*)layer;
	return nullptr;
}

MeshGroup* RenderCanvas::GetMGroup(const VVDString& name) const
{
	for (TreeLayer* layer : m_layer_list)
		if (layer && layer->IsA() == 6 && layer->GetName() == name)
			return (MeshGroup*)layer;
	return nullptr;
}

DataGroup* RenderCanvas::GetParentGroup(const VVDString& name) const
{
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer || layer->IsA() != 5) continue;
		DataGroup* group = (DataGroup*)layer;
		for (int j = 0; j < group->GetVolumeNum(); j++)
		{
			VolumeData* vd = group->GetVolumeData(j);
			if (vd && vd->GetName() == name)
				return group;
		}
	}
	return nullptr;
}

MeshGroup* RenderCanvas::GetParentMGroup(const VVDString& name) const
{
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer || layer->IsA() != 6) continue;
		MeshGroup* group = (MeshGroup*)layer;
		for (int j = 0; j < group->GetMeshNum(); j++)
		{
			MeshData* md = group->GetMeshData(j);
			if (md && md->GetName() == name)
				return group;
		}
	}
	return nullptr;
}

DataGroup* RenderCanvas::GetParentGroup(VolumeData* vd) const
{
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer || layer->IsA() != 5) continue;
		DataGroup* group = (DataGroup*)layer;
		for (int j = 0; j < group->GetVolumeNum(); j++)
			if (group->GetVolumeData(j) == vd)
				return group;
	}
	return nullptr;
}

MeshGroup* RenderCanvas::GetParentMGroup(MeshData* md) const
{
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer || layer->IsA() != 6) continue;
		MeshGroup* group = (MeshGroup*)layer;
		for (int j = 0; j < group->GetMeshNum(); j++)
			if (group->GetMeshData(j) == md)
				return group;
	}
	return nullptr;
}

// verbatim from VRenderVulkanView::GetCurrentVolGroup: the group of the
// current volume, else the last group
DataGroup* RenderCanvas::GetCurrentVolGroup() const
{
	DataGroup* group = nullptr;
	DataGroup* cur_group = nullptr;
	for (TreeLayer* layer : m_layer_list)
	{
		if (layer && layer->IsA() == 5)
		{
			group = (DataGroup*)layer;
			for (int j = 0; j < group->GetVolumeNum(); j++)
				if (m_cur_vol && m_cur_vol == group->GetVolumeData(j))
					cur_group = group;
		}
	}
	return cur_group ? cur_group : group;
}

VolumeData* RenderCanvas::GetVolumeData(const VVDString& name) const
{
	for (VolumeData* vd : m_volumes)
		if (vd && vd->GetName() == name)
			return vd;
	return nullptr;
}

MeshData* RenderCanvas::GetMeshData(const VVDString& name) const
{
	for (MeshData* md : m_meshes)
		if (md && md->GetName() == name)
			return md;
	return nullptr;
}

VVDString RenderCanvas::CheckNewGroupName(const VVDString& name, int type) const
{
	VVDString result = name;
	for (int i = 1; CheckGroupNames(result, type); i++)
		result = name + VVDString::Format(L"_%d", i);
	return result;
}

bool RenderCanvas::CheckGroupNames(const VVDString& name, int type) const
{
	for (TreeLayer* layer : m_layer_list)
		if (layer && layer->IsA() == type && layer->GetName() == name)
			return true;
	return false;
}

// ---------------------------------------------------------------------------
// adding
// ---------------------------------------------------------------------------

// body shared by wx AddVolumeData / MoveLayertoGroup / MoveLayerfromtoGroup:
// the volume takes the group's 2D adjustment, sync flags and (sync
// spacings / properties) the first volume's spacings / properties
void RenderCanvas::ApplyGroupSettings(DataGroup* group, VolumeData* vd)
{
	if (!group || !vd) return;
	Color gamma = group->GetGamma();
	vd->SetGamma(gamma);
	Color brightness = group->GetBrightness();
	vd->SetBrightness(brightness);
	Color hdr = group->GetHdr();
	vd->SetHdr(hdr);
	Color level = group->GetLevels();
	vd->SetLevels(level);
	vd->SetSyncR(group->GetSyncR());
	vd->SetSyncG(group->GetSyncG());
	vd->SetSyncB(group->GetSyncB());

	if ((group->GetVolumeSyncSpc() || group->GetVolumeSyncProp()) && group->GetVolumeNum() > 0)
	{
		double spcx = 1.0, spcy = 1.0, spcz = 1.0;
		group->GetVolumeData(0)->GetSpacings(spcx, spcy, spcz, 0);
		vd->SetSpacings(spcx, spcy, spcz);
	}

	if (group->GetVolumeSyncProp() && group->GetVolumeData(0) != vd)
	{
		VolumeData* srcvd = group->GetVolumeData(0);
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
}

// VRenderVulkanView::AddVolumeData: into the named group, else the group of
// the current volume, else the last group, else a new group
DataGroup* RenderCanvas::AddVolumeData(VolumeData* vd, const VVDString& group_name)
{
	if (!vd) return nullptr;
	DataGroup* group = nullptr;
	DataGroup* group_temp = nullptr;
	DataGroup* cur_group = nullptr;

	for (TreeLayer* layer : m_layer_list)
	{
		if (layer && layer->IsA() == 5)
		{
			group_temp = (DataGroup*)layer;
			if (group_temp->GetName() == group_name)
			{
				group = group_temp;
				break;
			}
			for (int j = 0; j < group_temp->GetVolumeNum(); j++)
				if (m_cur_vol && m_cur_vol == group_temp->GetVolumeData(j))
					cur_group = group_temp;
		}
	}

	if (!group && cur_group)
		group = cur_group;
	if (!group && group_temp)
		group = group_temp;
	if (!group)
	{
		VVDString name = AddGroup(VVDString());
		group = GetGroup(name);
		if (!group)
			return nullptr;
	}

	group->InsertVolumeData(group->GetVolumeNum() - 1, vd);
	ApplyGroupSettings(group, vd);
	SyncFlatLists();

	Q_EMIT groupLinkChanged(vd, group);
	CalcAndSetCombinedClippingPlanes();
	A2QSync();

	ComputeBounds();   // InitView(INIT_BOUNDS | INIT_CENTER)
	RequestRender();
	return group;
}

void RenderCanvas::AddVolume(VolumeData* vd)
{
	if (!vd) return;
	if (std::find(m_volumes.begin(), m_volumes.end(), vd) == m_volumes.end())
		AddVolumeData(vd);
	m_cur_vol = vd;
}

// VRenderVulkanView::AddMeshData: appended as a top-level layer
void RenderCanvas::AddMeshData(MeshData* md)
{
	if (!md) return;
	if (std::find(m_meshes.begin(), m_meshes.end(), md) != m_meshes.end())
		return;
	m_layer_list.push_back(md);
	SyncFlatLists();
	CalcAndSetCombinedClippingPlanes();
	A2QSync();
	ComputeBounds();
	RequestRender();
}

void RenderCanvas::AddAnnotations(Annotations* ann)
{
	if (!ann) return;
	if (std::find(m_layer_list.begin(), m_layer_list.end(), (TreeLayer*)ann) == m_layer_list.end())
		m_layer_list.push_back(ann);
}

void RenderCanvas::RemoveAnnotations(Annotations* ann)
{
	m_layer_list.erase(std::remove(m_layer_list.begin(), m_layer_list.end(), (TreeLayer*)ann), m_layer_list.end());
}

void RenderCanvas::AddMesh(MeshData* md)
{
	AddMeshData(md);
}

// VRenderVulkanView::AddGroup
VVDString RenderCanvas::AddGroup(const VVDString& str, const VVDString& prev_group)
{
	DataGroup* group = new DataGroup();
	if (!str.empty())
		group->SetName(str);

	bool found_prev = false;
	for (size_t i = 0; i < m_layer_list.size(); i++)
	{
		TreeLayer* layer = m_layer_list[i];
		if (layer && layer->IsA() == 5 && layer->GetName() == prev_group)
		{
			m_layer_list.insert(m_layer_list.begin() + i + 1, group);
			found_prev = true;
			break;
		}
	}
	if (!found_prev)
		m_layer_list.push_back(group);

	//set default settings
	if (m_group_defaults)
		m_group_defaults(group);

	SyncFlatLists();
	return group->GetName();
}

VVDString RenderCanvas::AddMGroup(const VVDString& str)
{
	MeshGroup* group = new MeshGroup();
	if (!str.empty())
		group->SetName(str);
	m_layer_list.push_back(group);
	return group->GetName();
}

// ---------------------------------------------------------------------------
// removing
// ---------------------------------------------------------------------------

namespace
{
	void ReleaseVolume(VolumeLoader* loader, VolumeData* vd)
	{
		if (loader) loader->RemoveDataVD(vd);
		if (vd->GetVR()) vd->GetVR()->clear_tex_current();
	}
}

void RenderCanvas::RemoveVolumeData(const VVDString& name)
{
	for (size_t i = 0; i < m_layer_list.size(); i++)
	{
		TreeLayer* layer = m_layer_list[i];
		if (!layer) continue;
		if (layer->IsA() == 2 && layer->GetName() == name)
		{
			VolumeData* vd = (VolumeData*)layer;
			m_layer_list.erase(m_layer_list.begin() + i);
			m_cur_vol = nullptr;
			ReleaseVolume(m_loader.get(), vd);
			LayersChanged(true);
			return;
		}
		if (layer->IsA() == 5)
		{
			DataGroup* group = (DataGroup*)layer;
			for (int j = 0; j < group->GetVolumeNum(); j++)
			{
				VolumeData* vd = group->GetVolumeData(j);
				if (vd && vd->GetName() == name)
				{
					group->RemoveVolumeData(j);
					m_cur_vol = nullptr;
					ReleaseVolume(m_loader.get(), vd);
					LayersChanged(true);
					return;
				}
			}
		}
	}
}

bool RenderCanvas::ReplaceVolumeData(const VVDString& name, VolumeData* dst)
{
	if (!dst) return false;
	for (size_t i = 0; i < m_layer_list.size(); i++)
	{
		TreeLayer* layer = m_layer_list[i];
		if (!layer) continue;
		if (layer->IsA() == 2 && layer->GetName() == name)
		{
			VolumeData* vd = (VolumeData*)layer;
			if (m_cur_vol == vd) m_cur_vol = dst;
			ReleaseVolume(m_loader.get(), vd);
			m_layer_list[i] = dst;
			LayersChanged(true);
			return true;
		}
		if (layer->IsA() == 5)
		{
			DataGroup* group = (DataGroup*)layer;
			for (int j = 0; j < group->GetVolumeNum(); j++)
			{
				VolumeData* vd = group->GetVolumeData(j);
				if (vd && vd->GetName() == name)
				{
					if (m_cur_vol == vd) m_cur_vol = dst;
					ReleaseVolume(m_loader.get(), vd);
					group->ReplaceVolumeData(j, dst);
					LayersChanged(true);
					return true;
				}
			}
		}
	}
	return false;
}

void RenderCanvas::RemoveVolume(VolumeData* vd)
{
	if (!vd) return;
	// wx RemoveVolumeData clears m_cur_vol; the Qt callers expect the last
	// volume to become current (as before)
	RemoveVolumeData(vd->GetName());
	if (!m_cur_vol) m_cur_vol = m_volumes.empty() ? nullptr : m_volumes.back();
}

void RenderCanvas::RemoveMeshData(const VVDString& name)
{
	MeshData* removed = nullptr;
	for (size_t i = 0; i < m_layer_list.size() && !removed; i++)
	{
		TreeLayer* layer = m_layer_list[i];
		if (!layer) continue;
		if (layer->IsA() == 3 && layer->GetName() == name)
		{
			removed = (MeshData*)layer;
			m_layer_list.erase(m_layer_list.begin() + i);
		}
		else if (layer->IsA() == 6)
		{
			MeshGroup* group = (MeshGroup*)layer;
			for (int j = 0; j < group->GetMeshNum(); j++)
			{
				MeshData* md = group->GetMeshData(j);
				if (md && md->GetName() == name)
				{
					removed = md;
					group->RemoveMeshData(j);
					break;
				}
			}
		}
	}
	if (!removed) return;
	if (m_cur_mesh == removed) m_cur_mesh = nullptr;
	// The mesh's vertex buffers / descriptor sets may still be referenced by
	// a submitted frame; the caller deletes the MeshData right after this.
	if (m_vulkan && m_vulkan->vulkanDevice)
		vkQueueWaitIdle(m_vulkan->vulkanDevice->queue);
	LayersChanged(true);
}

void RenderCanvas::RemoveMesh(MeshData* md)
{
	if (!md) return;
	RemoveMeshData(md->GetName());
}

void RenderCanvas::RemoveGroup(const VVDString& name, std::vector<VolumeData*>* removed_vols,
	std::vector<MeshData*>* removed_meshes)
{
	bool wait = false;
	for (size_t i = 0; i < m_layer_list.size(); i++)
	{
		TreeLayer* layer = m_layer_list[i];
		if (!layer) continue;
		if (layer->IsA() == 5 && layer->GetName() == name)
		{
			DataGroup* group = (DataGroup*)layer;
			for (int j = group->GetVolumeNum() - 1; j >= 0; j--)
			{
				VolumeData* vd = group->GetVolumeData(j);
				if (!vd) continue;
				if (m_cur_vol == vd)
					m_cur_vol = nullptr;
				group->RemoveVolumeData(j);
				ReleaseVolume(m_loader.get(), vd);
				if (removed_vols) removed_vols->push_back(vd);
			}
			m_layer_list.erase(m_layer_list.begin() + i);
			delete group;
			--i;
		}
		else if (layer->IsA() == 6 && layer->GetName() == name)
		{
			MeshGroup* group = (MeshGroup*)layer;
			for (int j = group->GetMeshNum() - 1; j >= 0; j--)
			{
				MeshData* md = group->GetMeshData(j);
				if (!md) continue;
				if (m_cur_mesh == md) m_cur_mesh = nullptr;
				group->RemoveMeshData(j);
				if (removed_meshes) removed_meshes->push_back(md);
				wait = true;
			}
			m_layer_list.erase(m_layer_list.begin() + i);
			delete group;
			--i;
		}
	}
	if (wait && m_vulkan && m_vulkan->vulkanDevice)
		vkQueueWaitIdle(m_vulkan->vulkanDevice->queue);
	LayersChanged(true);
}

// volumes, volume groups and annotations leave the view; groups are deleted
void RenderCanvas::ClearVolumes()
{
	for (size_t i = 0; i < m_layer_list.size(); i++)
	{
		TreeLayer* layer = m_layer_list[i];
		if (!layer) continue;
		if (layer->IsA() == 2 || layer->IsA() == 4 || layer->IsA() == 5)
		{
			if (layer->IsA() == 4)
				;   // annotations: owned by the DataManager
			else if (layer->IsA() == 5)
			{
				DataGroup* group = (DataGroup*)layer;
				for (int j = 0; j < group->GetVolumeNum(); j++)
					if (VolumeData* vd = group->GetVolumeData(j)) ReleaseVolume(m_loader.get(), vd);
				delete group;
			}
			else
				ReleaseVolume(m_loader.get(), (VolumeData*)layer);
			m_layer_list.erase(m_layer_list.begin() + i);
			--i;
		}
	}
	m_cur_vol = nullptr;
	SyncFlatLists();
	m_bounds.reset();
	if (!m_meshes.empty()) ComputeBounds();
	RequestRender();
}

void RenderCanvas::ClearMeshes()
{
	bool any = false;
	for (size_t i = 0; i < m_layer_list.size(); i++)
	{
		TreeLayer* layer = m_layer_list[i];
		if (!layer) continue;
		if (layer->IsA() == 3 || layer->IsA() == 6)
		{
			if (layer->IsA() == 6) delete (MeshGroup*)layer;
			m_layer_list.erase(m_layer_list.begin() + i);
			--i;
			any = true;
		}
	}
	if (!any) return;
	m_md_pop_list.clear();
	m_cur_mesh = nullptr;
	if (m_vulkan && m_vulkan->vulkanDevice)
		vkQueueWaitIdle(m_vulkan->vulkanDevice->queue);
	SyncFlatLists();
	ComputeBounds();
	RequestRender();
}

// ---------------------------------------------------------------------------
// visibility / colours
// ---------------------------------------------------------------------------

// verbatim from VRenderVulkanView::Isolate (incl. its top-level mesh quirk:
// a top-level mesh is always hidden)
void RenderCanvas::Isolate(int type, const VVDString& name)
{
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer) continue;
		switch (layer->IsA())
		{
		case 2://volume
		{
			VolumeData* vd = (VolumeData*)layer;
			vd->SetDisp(type == 2 && vd->GetName() == name);
		}
		break;
		case 3://mesh
		{
			MeshData* md = (MeshData*)layer;
			md->SetDisp(false);
		}
		break;
		case 4://annotation
		{
			Annotations* ann = (Annotations*)layer;
			ann->SetDisp(type == 4 && ann->GetName() == name);
		}
		break;
		case 5://volume group
		{
			DataGroup* group = (DataGroup*)layer;
			for (int i = 0; i < group->GetVolumeNum(); i++)
				if (VolumeData* vd = group->GetVolumeData(i))
					vd->SetDisp(type == 2 && vd->GetName() == name);
		}
		break;
		case 6://mesh group
		{
			MeshGroup* group = (MeshGroup*)layer;
			for (int i = 0; i < group->GetMeshNum(); i++)
				if (MeshData* md = group->GetMeshData(i))
					md->SetDisp(type == 3 && md->GetName() == name);
		}
		break;
		}
	}
	RequestRender();
}

// verbatim from VRenderVulkanView::ShowAll (group visibility untouched)
void RenderCanvas::ShowAll()
{
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer) continue;
		switch (layer->IsA())
		{
		case 2: ((VolumeData*)layer)->SetDisp(true); break;
		case 3: ((MeshData*)layer)->SetDisp(true); break;
		case 4: ((Annotations*)layer)->SetDisp(true); break;
		case 5:
		{
			DataGroup* group = (DataGroup*)layer;
			for (int j = 0; j < group->GetVolumeNum(); ++j)
				if (VolumeData* vd = group->GetVolumeData(j)) vd->SetDisp(true);
		}
		break;
		case 6:
		{
			MeshGroup* group = (MeshGroup*)layer;
			for (int j = 0; j < group->GetMeshNum(); ++j)
				if (MeshData* md = group->GetMeshData(j)) md->SetDisp(true);
		}
		break;
		}
	}
	RequestRender();
}

void RenderCanvas::RandomizeColor()
{
	for (TreeLayer* layer : m_layer_list)
		if (layer) layer->RandomizeColor();
	RequestRender();
}

// VRenderVulkanView::OrganizeLayers
void RenderCanvas::OrganizeLayers()
{
	DataGroup* le_group = nullptr;
	//find last empty group
	for (int i = GetLayerNum() - 1; i >= 0; i--)
	{
		TreeLayer* layer = GetLayer(i);
		if (layer && layer->IsA() == 5 && ((DataGroup*)layer)->GetVolumeNum() == 0)
		{
			le_group = (DataGroup*)layer;
			break;
		}
	}

	bool changed = false;
	for (int i = 0; i < GetLayerNum(); i++)
	{
		TreeLayer* layer = GetLayer(i);
		if (!layer || layer->IsA() != 2)
			continue;
		//layer is volume
		VolumeData* vd = (VolumeData*)layer;
		if (!le_group)
		{
			VVDString group_name = AddGroup(VVDString());
			le_group = GetGroup(group_name);
			if (!le_group)
				continue;
		}
		// take the volume out of the layer list (wx: by name, first match)
		m_layer_list.erase(std::find(m_layer_list.begin(), m_layer_list.end(), layer));
		m_cur_vol = nullptr;
		le_group->InsertVolumeData(le_group->GetVolumeNum(), vd);
		changed = true;
		// the erased layer shifted the list
		i--;
		SyncFlatLists();
		Q_EMIT groupLinkChanged(nullptr, le_group);
	}
	if (changed)
		LayersChanged(false);
}

// ---------------------------------------------------------------------------
// moving (workspace tree drag & drop)
// ---------------------------------------------------------------------------

// move layer of the same level within this view; source is after the destination
void RenderCanvas::MoveLayerinView(const VVDString& src_name, const VVDString& dst_name)
{
	int i, src_index = 0;
	TreeLayer* src = nullptr;
	for (i = 0; i < (int)m_layer_list.size(); i++)
	{
		if (m_layer_list[i] && m_layer_list[i]->GetName() == src_name)
		{
			src = m_layer_list[i];
			src_index = i;
			m_layer_list.erase(m_layer_list.begin() + i);
			break;
		}
	}
	if (!src)
		return;
	bool inserted = false;
	for (i = 0; i < (int)m_layer_list.size(); i++)
	{
		if (m_layer_list[i] && m_layer_list[i]->GetName() == dst_name)
		{
			if (i >= src_index)
				m_layer_list.insert(m_layer_list.begin() + i + 1, src);
			else
				m_layer_list.insert(m_layer_list.begin() + i, src);
			inserted = true;
			break;
		}
	}
	// wx drops the layer when the destination is not found; keep it instead
	if (!inserted)
		m_layer_list.insert(m_layer_list.begin() + std::min(src_index, (int)m_layer_list.size()), src);
	LayersChanged(false);
}

// move layer (volume) of the same level within the given group
void RenderCanvas::MoveLayerinGroup(const VVDString& group_name, const VVDString& src_name, const VVDString& dst_name, int insert_mode)
{
	DataGroup* group = GetGroup(group_name);
	if (!group)
		return;
	VolumeData* src_vd = nullptr;
	int i, src_index = 0;
	for (i = 0; i < group->GetVolumeNum(); i++)
	{
		if (group->GetVolumeData(i)->GetName() == src_name)
		{
			src_index = i;
			src_vd = group->GetVolumeData(i);
			group->RemoveVolumeData(i);
			break;
		}
	}
	if (!src_vd)
		return;
	bool inserted = false;
	for (i = 0; i < group->GetVolumeNum(); i++)
	{
		if (group->GetVolumeData(i)->GetName() == dst_name)
		{
			int insert_offset = (insert_mode == 0) ? -1 : 0;
			group->InsertVolumeData(i + insert_offset, src_vd);
			inserted = true;
			break;
		}
	}
	if (!inserted)
		group->InsertVolumeData(src_index - 1, src_vd);
	LayersChanged(false);
}

// move layer (volume) from the given group up one level to this view
void RenderCanvas::MoveLayertoView(const VVDString& group_name, const VVDString& src_name, const VVDString& dst_name)
{
	DataGroup* group = GetGroup(group_name);
	if (!group)
		return;
	VolumeData* src_vd = nullptr;
	int i;
	for (i = 0; i < group->GetVolumeNum(); i++)
	{
		if (group->GetVolumeData(i)->GetName() == src_name)
		{
			src_vd = group->GetVolumeData(i);
			group->RemoveVolumeData(i);
			break;
		}
	}
	if (!src_vd)
		return;
	bool inserted = false;
	if (dst_name.empty())
	{
		m_layer_list.push_back(src_vd);
		inserted = true;
	}
	else
	{
		for (i = 0; i < (int)m_layer_list.size(); i++)
		{
			if (m_layer_list[i]->GetName() == dst_name)
			{
				m_layer_list.insert(m_layer_list.begin() + i + 1, src_vd);
				inserted = true;
				break;
			}
		}
	}
	if (!inserted)
		m_layer_list.push_back(src_vd);
	LayersChanged(false);
}

// move layer (volume) one level down to the given group
void RenderCanvas::MoveLayertoGroup(const VVDString& group_name, const VVDString& src_name, const VVDString& dst_name)
{
	VolumeData* src_vd = nullptr;
	int i, src_index = 0;
	DataGroup* group = GetGroup(group_name);
	if (!group)
		return;
	for (i = 0; i < (int)m_layer_list.size(); i++)
	{
		if (m_layer_list[i]->GetName() == src_name && m_layer_list[i]->IsA() == 2)//is volume data
		{
			src_vd = (VolumeData*)m_layer_list[i];
			src_index = i;
			m_layer_list.erase(m_layer_list.begin() + i);
			break;
		}
	}
	(void)src_index;
	if (!src_vd)
		return;
	if (group->GetVolumeNum() == 0 || dst_name.empty())
	{
		group->InsertVolumeData(0, src_vd);
	}
	else
	{
		bool inserted = false;
		for (i = 0; i < group->GetVolumeNum(); i++)
		{
			if (group->GetVolumeData(i)->GetName() == dst_name)
			{
				group->InsertVolumeData(i, src_vd);
				inserted = true;
				break;
			}
		}
		if (!inserted)
			group->InsertVolumeData(group->GetVolumeNum(), src_vd);
	}

	//set the 2d adjustment settings of the volume the same as the group
	ApplyGroupSettings(group, src_vd);
	SyncFlatLists();
	Q_EMIT groupLinkChanged(src_vd, group);
	CalcAndSetCombinedClippingPlanes();
	A2QSync();
	LayersChanged(false);
}

// move layer (volume) from one group to another different group
void RenderCanvas::MoveLayerfromtoGroup(const VVDString& src_group_name, const VVDString& dst_group_name,
	const VVDString& src_name, const VVDString& dst_name, int insert_mode)
{
	DataGroup* src_group = GetGroup(src_group_name);
	DataGroup* dst_group = GetGroup(dst_group_name);
	if (!src_group || !dst_group)
		return;
	int i;
	VolumeData* src_vd = nullptr;
	for (i = 0; i < src_group->GetVolumeNum(); i++)
	{
		if (src_group->GetVolumeData(i)->GetName() == src_name)
		{
			src_vd = src_group->GetVolumeData(i);
			src_group->RemoveVolumeData(i);
			break;
		}
	}
	if (!src_vd)
		return;
	if (dst_group->GetVolumeNum() == 0 || dst_name.empty())
	{
		dst_group->InsertVolumeData(0, src_vd);
	}
	else
	{
		bool inserted = false;
		for (i = 0; i < dst_group->GetVolumeNum(); i++)
		{
			if (dst_group->GetVolumeData(i)->GetName() == dst_name)
			{
				int insert_offset = (insert_mode == 0) ? -1 : 0;
				dst_group->InsertVolumeData(i + insert_offset, src_vd);
				inserted = true;
				break;
			}
		}
		if (!inserted)
			dst_group->InsertVolumeData(dst_group->GetVolumeNum(), src_vd);
	}

	//set the 2d adjustment settings of the volume the same as the group
	ApplyGroupSettings(dst_group, src_vd);
	SyncFlatLists();
	Q_EMIT groupLinkChanged(src_vd, dst_group);
	CalcAndSetCombinedClippingPlanes();
	A2QSync();
	LayersChanged(false);
}

//move mesh within a group
void RenderCanvas::MoveMeshinGroup(const VVDString& group_name, const VVDString& src_name, const VVDString& dst_name, int insert_mode)
{
	MeshGroup* group = GetMGroup(group_name);
	if (!group)
		return;
	MeshData* src_md = nullptr;
	int i, src_index = 0;
	for (i = 0; i < group->GetMeshNum(); i++)
	{
		if (group->GetMeshData(i)->GetName() == src_name)
		{
			src_index = i;
			src_md = group->GetMeshData(i);
			group->RemoveMeshData(i);
			break;
		}
	}
	if (!src_md)
		return;
	bool inserted = false;
	for (i = 0; i < group->GetMeshNum(); i++)
	{
		if (group->GetMeshData(i)->GetName() == dst_name)
		{
			int insert_offset = (insert_mode == 0) ? -1 : 0;
			group->InsertMeshData(i + insert_offset, src_md);
			inserted = true;
			break;
		}
	}
	if (!inserted)
		group->InsertMeshData(src_index - 1, src_md);
	LayersChanged(true);
}

//move mesh out of a group
void RenderCanvas::MoveMeshtoView(const VVDString& group_name, const VVDString& src_name, const VVDString& dst_name)
{
	MeshGroup* group = GetMGroup(group_name);
	if (!group)
		return;
	MeshData* src_md = nullptr;
	int i;
	for (i = 0; i < group->GetMeshNum(); i++)
	{
		if (group->GetMeshData(i)->GetName() == src_name)
		{
			src_md = group->GetMeshData(i);
			group->RemoveMeshData(i);
			break;
		}
	}
	if (!src_md)
		return;
	bool inserted = false;
	if (!dst_name.empty())
	{
		for (i = 0; i < (int)m_layer_list.size(); i++)
		{
			if (m_layer_list[i]->GetName() == dst_name)
			{
				m_layer_list.insert(m_layer_list.begin() + i + 1, src_md);
				inserted = true;
				break;
			}
		}
	}
	if (!inserted)
		m_layer_list.push_back(src_md);
	LayersChanged(false);
}

//move mesh into a group
void RenderCanvas::MoveMeshtoGroup(const VVDString& group_name, const VVDString& src_name, const VVDString& dst_name)
{
	MeshGroup* group = GetMGroup(group_name);
	if (!group)
		return;
	MeshData* src_md = nullptr;
	int i;
	for (i = 0; i < (int)m_layer_list.size(); i++)
	{
		if (m_layer_list[i]->GetName() == src_name && m_layer_list[i]->IsA() == 3)
		{
			src_md = (MeshData*)m_layer_list[i];
			m_layer_list.erase(m_layer_list.begin() + i);
			break;
		}
	}
	if (!src_md)
		return;
	if (group->GetMeshNum() == 0 || dst_name.empty())
		group->InsertMeshData(0, src_md);
	else
	{
		bool inserted = false;
		for (i = 0; i < group->GetMeshNum(); i++)
		{
			if (group->GetMeshData(i)->GetName() == dst_name)
			{
				group->InsertMeshData(i, src_md);
				inserted = true;
				break;
			}
		}
		if (!inserted)
			group->InsertMeshData(group->GetMeshNum() - 1, src_md);
	}
	LayersChanged(true);
}

//move mesh out of then into a group
void RenderCanvas::MoveMeshfromtoGroup(const VVDString& src_group_name, const VVDString& dst_group_name,
	const VVDString& src_name, const VVDString& dst_name, int insert_mode)
{
	MeshGroup* src_group = GetMGroup(src_group_name);
	MeshGroup* dst_group = GetMGroup(dst_group_name);
	if (!src_group || !dst_group)
		return;
	int i;
	MeshData* src_md = nullptr;
	for (i = 0; i < src_group->GetMeshNum(); i++)
	{
		if (src_group->GetMeshData(i)->GetName() == src_name)
		{
			src_md = src_group->GetMeshData(i);
			src_group->RemoveMeshData(i);
			break;
		}
	}
	if (!src_md)
		return;
	if (dst_group->GetMeshNum() == 0 || dst_name.empty())
		dst_group->InsertMeshData(0, src_md);
	else
	{
		bool inserted = false;
		for (i = 0; i < dst_group->GetMeshNum(); i++)
		{
			if (dst_group->GetMeshData(i)->GetName() == dst_name)
			{
				int insert_offset = (insert_mode == 0) ? -1 : 0;
				dst_group->InsertMeshData(i + insert_offset, src_md);
				inserted = true;
				break;
			}
		}
		if (!inserted)
			dst_group->InsertMeshData(dst_group->GetMeshNum() - 1, src_md);
	}
	LayersChanged(true);
}

// ---------------------------------------------------------------------------
// drawing: the layer loop of wx DrawVolumes / DrawVolumesDP (non-Depth view
// method). Layers are walked from last to first; each displayed group is
// drawn on its own, its volumes from last to first, with the group's blend
// mode (Depth groups through the MultiVolumeRenderer).
// dp_variant false = DrawVolumes: with mesh depth layers a Depth group is
// first drawn against the mesh depth (peel 1), then again (as wx does).
// ---------------------------------------------------------------------------

void RenderCanvas::DrawLayerVolumes(int peel, bool dp_variant)
{
	const bool dp = !m_md_pop_list.empty() && !m_dp_tex_list.empty() && m_dp_tex_list[0];
	std::vector<VolumeData*> list;
	for (int i = (int)m_layer_list.size() - 1; i >= 0; i--)
	{
		TreeLayer* layer = m_layer_list[i];
		if (!layer)
			continue;
		switch (layer->IsA())
		{
		case 2://volume data (this won't happen now)
		{
			if (dp_variant && m_finished_peeling_layer != 0)
				continue;
			VolumeData* vd = (VolumeData*)layer;
			if (vd->GetDisp())
				list.push_back(vd);
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
			DataGroup* group = (DataGroup*)layer;
			if (!group->GetDisp())
				continue;
			for (int j = group->GetVolumeNum() - 1; j >= 0; j--)
			{
				VolumeData* vd = group->GetVolumeData(j);
				if (vd && vd->GetDisp())
					list.push_back(vd);
			}
			if (!list.empty())
			{
				if (group->GetBlendMode() == VolMethodMulti)
				{
					if (!dp_variant && dp)
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
	// wx leaves trailing top-level volumes undrawn (they are always
	// organised into groups first)
	if (!list.empty())
	{
		DrawVolumesComp(list, false, peel);
		if (m_draw_mask)
			DrawVolumesComp(list, true, peel);
	}
}
