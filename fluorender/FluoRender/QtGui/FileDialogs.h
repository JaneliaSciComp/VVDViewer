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
//   Open Mesh / Open Project / Save Mesh / Save Annotations: no extra controls
//   in wx (the platform dialogs here too).
//
// Testing aids (Exec; the platform dialogs become Qt's own under them):
//   VVD_QT_GRAB_FILEDLG=<dir>          picture of each dialog: <dir>/filedlg_<name>.png
//   VVD_QT_FILEDLG_OPTS_<NAME>=k=v,..  set option controls (by object name) through
//                                      the same click / edit path as the mouse;
//                                      filter=<i> selects the i-th file type
//   VVD_QT_FILEDLG_FILE_<NAME>=a;b     accept with these files (else: cancel after the
//                                      picture, or wait for the user); "<dir>/"
//                                      accepts the name in the box, in <dir>
//   VVD_QT_FILEDLG_NATIVE_<NAME>=1     SaveFile keeps the platform dialog: the file
//                                      it opens with is logged (LastOpened, no
//                                      selection), then it is cancelled (Qt cannot
//                                      accept the macOS panel from code)
// <NAME> = OPENVOLUME, SAVEVOLUME, SAVESEGMENTED, BAKEVOLUME, EXPORTMETA,
// SAVEPROJECT, OPENMESH, OPENPROJECT, SAVEMESH, SAVEANNOTATIONS.
// A scripted dialog logs the name in its box when it opens (LastOpened) and
// puts back the user's Qt file dialog settings (QtProject / FileDialog: last
// folder, history, view), which QFileDialog writes when it is destroyed.

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
	// chosen filter in filters (wx GetFilterIndex). data_name: the volume's
	// name in the Workspace; the dialog starts with DefaultSaveName of it and
	// that format, the name without its extension selected (wx: an empty
	// name, the first format). Returns "" when cancelled.
	QString SaveVolume(QWidget* parent, const char* name, const QString& title, const QStringList& filters,
		int* filter_index, const QString& data_name = QString());
	// The file name a Save dialog starts with for data (a volume, mesh or
	// annotations) named data_name in the Workspace: the extension of the
	// file it was read from, at the end of the name or before the "_..."
	// that channels, copies and transforms add ("a.nrrd_Ch2", "a.nrrd_1",
	// "a.tif_WARPED", "b.obj_1"), goes to the end. *filter_index: the first
	// of filters with that extension, else 0; the name ends with that
	// filter's extension ("a.czi_Ch2" -> "a_Ch2.tif", "n.swc" -> "n.obj").
	// Folder separators and characters Windows does not allow become "_".
	QString DefaultSaveName(const QString& data_name, const QStringList& filters, int* filter_index);

	// ---- Save Mesh Data / Save Annotations (wx: a plain wxFileDialog with
	// an empty name): the platform save dialog in the last folder used,
	// starting with DefaultSaveName(data_name) and that file type. name:
	// test-hook name (SAVEMESH, SAVEANNOTATIONS). Returns "" when cancelled.
	QString SaveFile(QWidget* parent, const char* name, const QString& title, const QString& filter,
		const QString& data_name);

	// ---- Save Project
	QString SaveProject(QWidget* parent, const QString& dir, bool& embed,
		const std::function<bool()>& get_compress, const std::function<void(bool)>& set_compress);

	// ---- plain dialogs (wx has no extra controls)
	QStringList OpenFiles(QWidget* parent, const char* name, const QString& title, const QString& dir, const QString& filter);
	QString OpenFile(QWidget* parent, const char* name, const QString& title, const QString& dir, const QString& filter);

	// exec with the testing aids above; returns QDialog::exec()
	int Exec(QFileDialog& dlg, const char* name);
	// testing: the file name box of the last scripted dialog when it opened
	// (its text and the selected part), the file type and the folder then
	struct OpenedState { QString name, selected, type, folder; };
	OpenedState LastOpened();

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
