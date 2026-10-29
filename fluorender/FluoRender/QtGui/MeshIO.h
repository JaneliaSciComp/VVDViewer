#ifndef VVD_QT_MESHIO_H
#define VVD_QT_MESHIO_H

// Mesh loading for the Qt front end: port of VRenderFrame::LoadMeshes,
// ConvertCSV2SWC and ConvertNML2SWC (no wx). Files are loaded through
// DataManager::LoadMeshData, which owns the resulting MeshData objects.
//
// Suffix handling is the wx one:
//   .csv  -> converted to a temporary .swc (cell table: x/y/z, radius,
//            per-path colours, link segments, sub-mesh labels), then loaded
//   .nml  -> every <thing> becomes its own temporary .swc and mesh
//   other -> DataManager::LoadMeshData(file, desc)  (.obj, .swc, .ply ...)
//
// The Vulkan context must exist before loading (MeshRenderer is created in
// MeshData::Load): call RenderCanvas::EnsureVulkan() first.
//
// Differences from wx: the Qt view has no mesh groups yet, so the meshes the
// wx frame would put into a MeshGroup (several files at once, or an .nml) are
// returned as a flat list; the caller adds each with RenderCanvas::AddMesh.

#include <QString>
#include <QStringList>
#include <QList>

class DataManager;
class MeshData;

namespace MeshIO
{
	// File dialog filter, same entries as VRenderFrame::OnOpenMesh.
	QString FileDialogFilter();

	// Loads 'files' (descs[i] is the optional name prefix for files[i], as in
	// wx). Appends every MeshData created to *loaded (in load order) and a
	// message per failed file to *errors. Returns true if at least one mesh
	// was loaded.
	bool LoadMeshes(DataManager& dm, const QStringList& files,
		QList<MeshData*>* loaded = nullptr,
		const QStringList& descs = QStringList(),
		QStringList* errors = nullptr);
}

#endif // VVD_QT_MESHIO_H
