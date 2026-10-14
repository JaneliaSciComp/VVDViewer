#ifndef VVD_QT_FILEDIALOGS_H
#define VVD_QT_FILEDIALOGS_H

// File dialogs with the wx "Additional Options" boxes (wxFileDialog::
// SetExtraControlCreator). Qt's own QFileDialog (DontUseNativeDialog) with the
// options box added to its layout, as CaptureDialog does.
//
//   Open Volume   VRenderFrame::OnOpenVolume + CreateExtraControlVolume
//     Read a sequence as Z slices        VRenderFrame::m_sliceSequence (session)
//     Read a sequence as time series     VRenderFrame::m_timeSequence  (session)
//       (the two are exclusive; clicking the selected one clears it)
//     Compress data                      SettingDlg realtime compress (settings file)
//     Skip empty bricks                  SettingDlg skip bricks       (settings file)
//     Sequence identifier                VRenderFrame::m_time_id -> SettingDlg time id
//   Save Volume / Save segmented volume / Bake / Export metadata (vvd)
//                 DataTreeCtrl::CreateExtraControl (session statics)
//     Save metadata as an individual file  m_md_save_indv   (Export metadata of .vvd data)
//     Crop image using clipping planes     m_crop_output_image (Save, Save segmented)
//     Scale level (resolution pyramid)     m_save_scale_level (Save, Save segmented)
//   Save Project  VRenderFrame::OnSaveProject + CreateExtraControlProjectSave
//     Embed all files in the project folder  VRenderFrame::m_vrp_embed (session)
//     Compression                            VRenderFrame::m_save_compress (settings "/save cmp")
//   Open Mesh / Open Project: no extra controls in wx (plain dialogs here too).
//
// Testing aids (Exec):
//   VVD_QT_GRAB_FILEDLG=<dir>          picture of each dialog: <dir>/filedlg_<name>.png
//   VVD_QT_FILEDLG_OPTS_<NAME>=k=v,..  set option controls (by object name) through
//                                      the same click / edit path as the mouse;
//                                      filter=<i> selects the i-th file type
//   VVD_QT_FILEDLG_FILE_<NAME>=a;b     accept with these files (else: cancel after the
//                                      picture, or wait for the user)
// <NAME> = OPENVOLUME, SAVEVOLUME, SAVESEGMENTED, BAKEVOLUME, EXPORTMETA,
// SAVEPROJECT, OPENMESH, OPENPROJECT.

#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

class QWidget;
class QFileDialog;

namespace FileDialogs
{
	// ---- Open Volume
	struct OpenVolumeOptions
	{
		bool slice_seq = false;   // m_sliceSequence
		bool time_seq = false;    // m_timeSequence
		QString time_id = "_T";   // m_time_id
	};
	struct OpenVolumeHooks
	{
		std::function<bool()> get_compress;        // SettingDlg::GetRealtimeCompress
		std::function<void(bool)> set_compress;    // SettingDlg::SetRealtimeCompress
		std::function<bool()> get_skip_brick;      // SettingDlg::GetSkipBricks
		std::function<void(bool)> set_skip_brick;  // SettingDlg::SetSkipBricks
		std::function<void()> update_settings_ui;  // SettingDlg::UpdateUI
	};
	// Returns the chosen files (empty when cancelled). opt is updated as the
	// controls change (wx statics); compression / skip bricks are written
	// back through the hooks on OK only, as in wx.
	QStringList OpenVolume(QWidget* parent, const QString& dir, const QString& filter,
		OpenVolumeOptions& opt, const OpenVolumeHooks& hooks);

	// ---- Save Volume (wx DataTreeCtrl statics)
	bool GetMetadataSeparate();   // m_md_save_indv
	bool GetCropToClip();         // m_crop_output_image
	int GetScaleLevel();          // m_save_scale_level
	// name: test-hook name (SAVEVOLUME, ...). filter_index: index of the
	// chosen filter in filters (wx GetFilterIndex). Returns "" when cancelled.
	QString SaveVolume(QWidget* parent, const char* name, const QString& title, const QStringList& filters,
		int* filter_index, const QString& default_file = QString());

	// ---- Save Project
	QString SaveProject(QWidget* parent, const QString& dir, bool& embed,
		const std::function<bool()>& get_compress, const std::function<void(bool)>& set_compress);

	// ---- plain dialogs (wx has no extra controls)
	QStringList OpenFiles(QWidget* parent, const char* name, const QString& title, const QString& dir, const QString& filter);
	QString OpenFile(QWidget* parent, const char* name, const QString& title, const QString& dir, const QString& filter);

	// exec with the testing aids above; returns QDialog::exec()
	int Exec(QFileDialog& dlg, const char* name);

	// ---- wx DatasetSelectionDialog (VRenderFrame::LoadVolumes, Zarr / N5):
	// "Choose Zarr/N5 datasets", one check box per dataset (all unchecked;
	// a click on a row toggles it), Select All / Deselect All, OK / Cancel;
	// 500 x 600, titled with the opened path. Returns false when cancelled;
	// selected[i] = row i checked.
	// Testing aids: VVD_QT_DATASETS=all|none|cancel|<i,j,...> answers the
	// dialog (after a picture to VVD_QT_GRAB_FILEDLG/filedlg_datasets.png
	// when set).
	bool SelectDatasets(QWidget* parent, const QString& title, const QStringList& list, std::vector<bool>& selected);
}

#endif
