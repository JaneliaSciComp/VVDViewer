#ifndef VVD_QT_PROJECTIO_H
#define VVD_QT_PROJECTIO_H

// Project (.vrp) reading/writing for the Qt front end.
//
// Port of VRenderFrame::SaveProject / VRenderFrame::OpenProject (wx GUI,
// VRenderFrame.cpp) on top of vvd::IniFile, which reads and writes the
// wxFileConfig text format. Projects saved here open in the wx app and wx
// projects open here: the same groups/keys/value formats are used
// ("%f %f %f" colour triples, "%lf ..." quaternions, ver_major/ver_minor as
// strings, relative/moved data located via DataManager::SearchProjectPath,
// "<project>_files/" for embedded volumes/meshes/masks/labels).
//
// Round-tripped:
//   * /data/volume/N: name, path, metadata, compression, skip_brick,
//     slice_seq/time_seq/time_id, cur_time, cur_chan and every key of
//     properties/ (display, 3dgamma, boundary, contrast, thresholds, color,
//     hsv, mask colour, alpha, material, shading, samplerate (v1 /5 rule),
//     res/b_res/s_res/scl, x1..z2 plane params (also legacy *_val/_vali),
//     gamma/brightness/hdr/levels/sync, colormap*, id_color_disp_mode, inv,
//     mode (MIP), noise_red, depth_ovrd, shadow(+darkness), legend, roi_tree,
//     selected_rois, combined_rois, roi_disp_mode, mask/label (.msk/.lbl
//     files), mask_disp_mode, mask_lv, na_mode, shared_mask/label,
//     mask_alpha, clipping/ subgroup) plus a Qt-only "max_value" key that
//     the wx app ignores.
//   * /data/annotations/N: name, path (.txt written to "<project>_files"
//     when the annotations have no file yet), color / transparency /
//     max_score / threshold of annotations with a mesh; type-4 layers.
//   * /data/mesh/N: name, path (loaded via DataManager::LoadMeshData),
//     properties/ (display, lighting, material, alpha, 2D adjustment, sync,
//     shadow, radius_scale, bounds, planes) and transform/ (translation,
//     rotation, scaling, clipping/).
//   * /views/0: layer tree (volumes, meshes, volume groups and mesh groups
//     with their members, display, 2D adjustment, sync flags and clipping)
//     and properties/ (camera, object transform, background, AOV, persp,
//     clip planes, scale bar, 2D adjustment, ...). Unmodelled view property
//     keys (brush, painting, ...) are passed through untouched.
//   * /prop_panel (wx m_cur_sel_type / _vol / _mesh, plane_mode; the legacy
//     chann_link / x_link / linked_x1 ... keys of old projects are read),
//     /movie_panel, /tree_panel.
//   * /views/0/rulers: read into / written from ProjectView::rulers (the
//     RulerTool list) exactly like VRenderFrame::OpenProject/SaveProject;
//     kept verbatim (passthrough) when ProjectView::rulers is null.
//   * /memory settings: written from ProjectView::memory (the SettingDlg
//     values, as wx); read into it but, as in wx (whose loader code for it
//     is commented out), not applied by the caller.
//   * /interpolator (or passthrough), /brush_diag and /ui_layout (applied by
//     MainWindow, kept in ProjectView::passthrough).
//   * Doubles are written as wxConfigBase::Write(double) does ("%g"), so
//     a wx project saved again by the Qt app keeps its text.
//
//   * /views/N for N > 0 (wx: one per render view): ProjectView::extra_views,
//     each with its layers, properties (+ its unmodelled keys) and rulers;
//     /views/num = 1 + extra_views.size().
//
// NOT round-tripped:
//   * FLKEY_TYPE_INT interpolator keys (wx skips them on load as well)

#include <QString>
#include <utility>
#include <vector>

#include "FLIVR/Color.h"

class QWidget;
class DataManager;
class VolumeData;
class MeshData;
class ClippingLayer;
class Ruler;
class Interpolator;

namespace ProjectIO
{
	// ClippingLayer state as stored in a "clipping" group (and, for the
	// view, inline in /views/0/properties).
	struct ProjectClipping
	{
		bool valid = false;               // set when read from / to be written to a file
		// keys found by ReadClipping (wx applies each only when present)
		bool has_mode = false, has_sync = false, has_rot = false, has_dist = false;
		bool sync_clipping_planes = false;
		int clip_mode = 2;
		int clip_dist[3] = { 1, 1, 1 };
		bool link[3] = { false, false, false };
		double linked_param[6] = { 0, 0, 0, 0, 0, 0 };   // x1 x2 y1 y2 z1 z2
		// ClippingLayer: raw values (Get/SetClippingPlaneRotationsRaw).
		// View: VRenderView::Get/SetClippingPlaneRotations values.
		double rot_cl[3] = { 0, 0, 0 };
		bool has_fix = false;             // the *_fix block below is valid
		double q_cl_zero[4] = { 0, 0, 0, 1 };   // x y z w
		double q_cl_fix[4] = { 0, 0, 0, 1 };
		double q_fix[4] = { 0, 0, 0, 1 };
		double rot_cl_fix[3] = { 0, 0, 0 };
		double rot_fix[3] = { 0, 0, 0 };
		double trans_fix[3] = { 0, 0, 0 };
	};
	void CaptureClipping(ClippingLayer* layer, ProjectClipping& out);
	void ApplyClipping(const ProjectClipping& c, ClippingLayer* layer);

	// One entry of /views/0/layers (the workspace tree order).
	struct ProjectLayer
	{
		enum Type { Volume = 2, Mesh = 3, Annotations = 4, VolumeGroup = 5, MeshGroup = 6 };
		int type = Volume;
		QString name;
		// groups (type 5 / 6) only
		int id = 0;                       // DataGroup::GetID() / MeshGroup::GetID() at save time
		bool display = true;
		FLIVR::Color gamma{ 1.0, 1.0, 1.0 };
		FLIVR::Color brightness{ 1.0, 1.0, 1.0 };
		FLIVR::Color hdr{ 0.0, 0.0, 0.0 };
		FLIVR::Color levels{ 1.0, 1.0, 1.0 };
		bool sync_r = false, sync_g = false, sync_b = false;
		bool sync_vp = false;             // volume group: sync volume properties
		bool sync_vspc = false;           // volume group: sync spacings
		bool sync_mp = false;             // mesh group: sync mesh properties
		std::vector<QString> members;     // volume / mesh names in group order
		ProjectClipping clipping;
	};

	// /movie_panel (all text fields are stored as the wx text controls' strings)
	struct ProjectMovie
	{
		bool valid = false;
		int views_cmb = 0;
		bool rot_check = true;
		bool seq_check = false;
		int frame_range = 360;
		int time_frame = 0;
		bool x_rd = false, y_rd = true, z_rd = false;
		QString angle_end_text = "360";
		QString step_text = "5";          // movie length (s)
		QString frames_text = "30";       // fps
		bool frame_chk = false;           // crop frame
		QString center_x_text, center_y_text, width_text, height_text;
		QString time_start_text = "1", time_cur_text = "1", time_end_text = "1";
		QString progress_text = "0";      // seconds into the movie
	};

	// Camera / view state (/views/0/properties) plus the bits of the frame
	// that belong to the project. Defaults are VRenderVulkanView's.
	//
	// LoadProject updates this struct IN PLACE: keys missing from the file
	// leave the caller's values untouched, so pre-fill it with the current
	// canvas state if you want "missing = keep".
	struct ProjectView
	{
		// object rotation shown by the UI (wx "rotation" = VRenderView::Get/SetRotations)
		double rotx = 0.0, roty = 0.0, rotz = 0.0;
		// camera translation (wx "translation")
		double transx = 0.0, transy = 0.0, transz = 0.0;
		// camera centre / distances. Written only when has_camera_frame is
		// set (the wx app recomputes them from the scene bounds otherwise).
		bool has_camera_frame = false;
		double ctrx = 0.0, ctry = 0.0, ctrz = 0.0;
		double center_eye_dist = 0.0;
		double radius = 5.0;
		double init_dist = 0.0;
		double scale_factor = 1.0;        // zoom
		// object transform
		double obj_ctrx = 0.0, obj_ctry = 0.0, obj_ctrz = 0.0;
		double obj_transx = 0.0, obj_transy = 0.0, obj_transz = 0.0;
		double obj_rotx = 0.0, obj_roty = 0.0, obj_rotz = 0.0;

		bool persp = false;
		bool free = false;
		double aov = 15.0;
		double near_clip = 0.1, far_clip = 100.0;
		FLIVR::Color bg_color{ 0.0, 0.0, 0.0 };
		bool draw_all = true;
		int draw_type = 1;
		int vol_method = 1;               // VOL_METHOD_SEQ
		int peel_layers = 1;
		double min_dpi = 20.0;
		int res_mode = 0;
		bool fog = true;
		double fog_intensity = 0.0;
		bool draw_camctr = false, draw_fps = false, draw_legend = true;
		bool has_draw_legend = false;     // LoadProject: the file has draw_legend

		// scale bar
		bool disp_scale_bar = false, disp_scale_bar_text = false;
		double sb_length = 50.0;
		QString sb_text = QString::fromUtf8("50 \xC2\xB5m"), sb_num = "50";
		int sb_unit = 1;
		int sb_digit = 0;
		bool sb_fixed = false;

		// view-level 2D adjustment
		FLIVR::Color gamma{ 1.0, 1.0, 1.0 };
		FLIVR::Color brightness{ 1.0, 1.0, 1.0 };
		FLIVR::Color hdr{ 0.0, 0.0, 0.0 };
		FLIVR::Color levels{ 1.0, 1.0, 1.0 };
		bool sync_r = false, sync_g = false, sync_b = false;
		bool easy_2d_adjustment = false;

		// view clipping (VRenderView clip mode / plane rotations / links)
		ProjectClipping clipping;

		// 4D sequence
		int st_time = 0, ed_time = 0, cur_time = 0;

		// workspace tree. Empty on save -> generated from the volume/mesh
		// lists (one ungrouped layer each, in list order).
		std::vector<ProjectLayer> layers;

		// /prop_panel: current selection (must be one of the saved/loaded
		// volumes/meshes, or null) and clip panel plane mode
		VolumeData* selected_volume = nullptr;
		MeshData* selected_mesh = nullptr;
		int plane_mode = 0;
		bool has_plane_mode = false;      // LoadProject: /prop_panel/plane_mode present
		// wx m_cur_sel_type / m_cur_sel_vol / m_cur_sel_mesh, written as is
		// when raw_selection is set (indices into the DataManager lists);
		// otherwise derived from selected_volume / selected_mesh. LoadProject
		// fills them (has_selection: the three keys were present).
		bool raw_selection = false, has_selection = false;
		int cur_sel_type = -1, cur_sel_vol = -1, cur_sel_mesh = -1;
		// legacy /prop_panel keys of old projects (wx OpenProject "chann_link")
		bool has_chann_link = false, chann_link = false;
		bool has_linked_legacy = false;   // linked_x1 present
		double linked_legacy[6] = { 0, 0, 0, 0, 0, 0 };
		int has_axis_link[3] = { -1, -1, -1 };   // x_link / y_link / z_link (-1: absent)

		// /memory settings (wx SaveProject writes the SettingDlg values; the
		// wx loader does not apply them, its code is commented out)
		struct Memory
		{
			bool valid = false;
			bool mem_swap = false;
			double graphics_mem = 1000.0;
			double large_data_size = 1000.0;
			int force_brick_size = 128;
			int up_time = 100;
			int update_order = 0;
		} memory;

		ProjectMovie movie;
		QString tree_exp_states;          // /tree_panel/exp_states

		// /views/0/rulers (wx VRenderVulkanView::m_ruler_list). Not owned.
		// null: the section is passed through verbatim. Non-null:
		// SaveProject writes the list (wx SaveProject; nothing when empty);
		// LoadProject deletes the entries of the list, clears it and appends
		// one new Ruler per saved ruler (wx OpenProject) when the file has a
		// /views/0 section.
		std::vector<Ruler*>* rulers = nullptr;

		// /interpolator (wx VRenderFrame::m_interpolator, the Advanced-tab
		// keyframes). Not owned. null: passed through verbatim. Non-null:
		// SaveProject writes it, LoadProject replaces its keys when the file
		// has the section (interpolator_loaded is then set).
		::Interpolator* interpolator = nullptr;
		bool interpolator_loaded = false;

		// version of the file that was loaded (0 when unknown) and whether
		// it is newer than this build (the wx app shows a warning then)
		long ver_major = 0, ver_minor = 0;
		bool newer_version = false;

		// keys kept verbatim: full key ("/group/sub/key") -> raw value.
		// Filled by LoadProject, written back by SaveProject.
		std::vector<std::pair<QString, QString>> passthrough;

		// /views/1 .. /views/<num-1> (wx: the further render views). Only the
		// per-view members are used (layers, the /views/N/properties values,
		// clipping, rulers); for those, passthrough holds the view's own
		// /views/N/properties/ keys. SaveProject writes rulers from the
		// pointer when set; LoadProject reads them into owned_rulers (the
		// caller takes them over).
		std::vector<ProjectView> extra_views;
		std::vector<Ruler*> owned_rulers;
	};

	struct SaveOptions
	{
		bool embed = false;      // "Embed all files in the project folder" (m_vrp_embed)
		bool compress = false;   // "Compression" of embedded .nrrd files (m_save_compress)
	};

	// Writes a .vrp. Volumes without a path (or all of them when
	// opts.embed) are written to "<path>_files/<name>.nrrd"; masks and labels
	// always go to "<path>_files/<name>.msk/.lbl". Meshes without a path (or
	// with opts.embed) go to "<path>_files/<name>.obj/.swc". The volumes'
	// "compression" key is DataManager::GetCompression().
	bool SaveProject(const QString& path, DataManager* dm,
		const std::vector<VolumeData*>& volumes,
		const std::vector<MeshData*>& meshes,
		const ProjectView& view, QString* error,
		const SaveOptions& opts = SaveOptions());

	// Reads a .vrp: sets the project path on dm, loads every volume and mesh
	// through DataManager (paths that no longer exist are searched relative
	// to the project file) and applies the saved properties. The data stay
	// owned by dm. volumes_out / meshes_out receive the newly loaded data:
	// first in the order of the /views/0 layer tree (groups flattened), then
	// any loaded item the view does not reference. view_out is updated in
	// place (see ProjectView).
	//
	// The caller must clear the scene first if desired (the wx app calls
	// DataManager::ClearAll()) and bring the renderer up beforehand
	// (RenderCanvas::EnsureVulkan()), because volume loading creates GPU
	// resources. Returns false only when the file cannot be read; *error
	// then holds the reason. On success *error lists non-fatal problems
	// (missing data files, ...), one per line, or is empty.
	//
	// The volume files are read and decoded in parallel (VolumePrefetcher =
	// wx's ProjectDataLoader in prefetch mode); each volume is then finished
	// on the calling thread in file order, so the result is the same as a
	// sequential load. While waiting, events are processed (user input
	// excluded) and, when progress_parent is given, a modal progress dialog
	// is shown.
	bool LoadProject(const QString& path, DataManager* dm,
		std::vector<VolumeData*>& volumes_out,
		std::vector<MeshData*>& meshes_out,
		ProjectView& view_out, QString* error, QWidget* progress_parent = nullptr);
}

#endif // VVD_QT_PROJECTIO_H
