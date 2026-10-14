#ifndef VVD_QT_ANALYSISCONTEXT_H
#define VVD_QT_ANALYSISCONTEXT_H

// Minimal, toolkit-neutral bridge between the Qt "Analyze" dialogs
// (NoiseReductionDialog, CountingDialog, ConvertDialog, ColocalizationDialog,
// InfoDialog, ...) and the main window / active render view.
// MainWindow implements it. This replaces the wx dialogs' reach into
// VRenderFrame / VRenderView / VRenderVulkanView.
//
// Shared header: extend it (add methods), do not overwrite.

#include <vector>

class VolumeData;
class DataManager;
class VolumeSelector;
class VolumeCalculator;
class BrushToolDialog;

class AnalysisContext
{
public:
	virtual ~AnalysisContext() = default;

	// Currently selected volume (wx: VRenderFrame::GetCurSelVol()). May be null.
	virtual VolumeData* CurrentVolume() = 0;
	virtual DataManager* GetDataManager() = 0;
	// Selector / calculator of the active view (wx: VRenderVulkanView::m_selector
	// / m_calculator, reached via VRenderView::GetVolumeSelector()/GetVolumeCalculator()).
	virtual VolumeSelector* GetSelector() = 0;
	virtual VolumeCalculator* GetCalculator() = 0;
	// Redraw all render views (wx: VRenderFrame::RefreshVRenderViews()).
	virtual void RefreshView() = 0;
	// Add a newly created volume to the data manager + active view + tree.
	virtual void AddVolume(VolumeData* vd) = 0;

	// ---- added for ColocalizationDialog ----
	// Volumes currently displayed (GetDisp()) in the active view, in layer order.
	virtual std::vector<VolumeData*> VisibleVolumes() = 0;
	// Component analysis on GetSelector()->GetVolume(), same as
	// VRenderVulkanView::CompAnalysis(): when !select, first hand the selector
	// the view's 2D paint mask / weight textures (Set2DMask, Set2DWeight);
	// SetSizeMap(false); call selector->CompAnalysis(min, max, thresh, 1.0,
	// select, gen_ann); then add selector->GetAnnotations() (if any) to the
	// DataManager. Returns the selector's return value (component count).
	// iter_limit (added for NoiseReductionDialog) is forwarded as the last
	// argument of VolumeSelector::CompAnalysis(); -1 = no limit.
	virtual int CompAnalysis(double min_voxels, double max_voxels, double thresh,
		bool select, bool gen_ann, int iter_limit = -1) = 0;
	// Run GetCalculator() with operation `type` like
	// VRenderVulkanView::Calculate(type, "", true) / CalculateSingle(): copy
	// brxml levels if needed, Calculate(type), copy render settings of volume A
	// to the result (planes, TF, color, shading, shadow, sample rate, 2D adjust),
	// add the result to DataManager + active view (override-vox off) + tree,
	// hide the inputs as wx does (type 1-4: A and B; 5,6,9: A; 10,11: A,B,C;
	// type 8 = colocalization result: nothing hidden), redraw.
	// NoiseReductionDialog "Erase" calls Calculate(6) after setting volume A to
	// the current volume (wx: DataTreeCtrl::BrushCreateInv -> SetVolumeA(vd);
	// Calculate(6, <group of vd>)): the "_DELETED" result goes into the group
	// that contains volume A (top level if none).
	virtual void Calculate(int type) = 0;

	// ---- added for CountingDialog / NoiseReductionDialog ----
	// VRenderVulkanView::CompExport(mode, select): mode 0 =
	// selector->CompExportMultiChann(select); mode 1 = look up "<sel>_COMP1/2/3"
	// in the DataManager and selector->CompExportRandomColor(1, r, g, b, select).
	// Then every volume in *selector->GetResultVols() is added to the
	// DataManager and to ONE new group in the active view; the group gets
	// SetSyncR/G/BAll(true) and the selector volume's Gamma/Brightness/Hdr/Levels
	// (Set*All). Update the tree (select the source volume), redraw.
	virtual void CompExport(int mode, bool select) = 0;
	// VRenderVulkanView::ShowAnnotations(): if selector->GetAnnotations() is
	// non-null, add it as a layer of the active view (AddAnnotations) and to
	// the tree; keep the current volume selected in the tree.
	virtual void ShowAnnotations() = 0;
	// VRenderVulkanView::SetEnhanceSelection(enhance) + RefreshGL(): the view
	// flag m_enhance_sel (default true in wx) that brightens masked voxels.
	virtual void SetEnhanceSelection(bool enhance) = 0;
	virtual bool GetEnhanceSelection() = 0;
	// ---- added for BrushToolDialog (Analysis tab, Noise Removal) ----
	// VRenderVulkanView::NoiseAnalysis: a .brxml selector volume is replaced
	// by a copy of its current level first; paint mask / 2D weights handed
	// over; selector->CompAnalysis(min, max, thresh, 1.0, false, false).
	virtual int NoiseAnalysis(double min_voxels, double max_voxels, double thresh) = 0;
	// VRenderVulkanView::NoiseRemoval: the "_NR" result goes into a new group
	// carrying the source's 2D adjustments; the source is hidden.
	virtual void NoiseRemoval(int iter, double thresh) = 0;
	// wx VRenderFrame::GetBrushToolDlg(): the (always existing) brush dialog,
	// which holds the noise-reduction / component-analysis defaults
	virtual BrushToolDialog* GetBrushDialog() = 0;
	// Scale-bar unit of the active view (wx: VRenderVulkanView::m_sb_unit):
	// 0 = nm, 1 = um, 2 = mm.
	virtual int GetScaleBarUnit() = 0;
	// ---- added for NoiseReductionDialog "Erase" ----
	// wx NoiseCancellingDlg::OnEraseBtn -> TreePanel::BrushErase ->
	// DataTreeCtrl::BrushCreateInv: view int mode 1, then Calculate(6) with the
	// selected volume as A, or -- volume in NA mode -- Calculate(11) with the
	// NA-mode label volumes of its group as A / B / C.
	virtual void BrushErase() = 0;
};

#endif
