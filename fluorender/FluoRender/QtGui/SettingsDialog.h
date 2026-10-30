#ifndef VVD_QT_SETTINGSDIALOG_H
#define VVD_QT_SETTINGSDIALOG_H

// "Settings": Qt port of SettingDlg (wx). Four tabs (Project, Rendering,
// Performance, File format) with the same groups, labels and controls, and a
// Cancel / Done button row. Like the wx pane it is modeless: changes take
// effect immediately, Cancel only hides it, Done also writes the settings
// file.
//
// Settings file: "fluorender.set" in the wxFileConfig text format, read and
// written with vvd::IniFile at the same location the wx app uses: written to
// vvd::GetUserSettingsDir() (macOS ~/Library/Application Support/VVDViewer),
// read from there with the bundle / install copy as the factory default
// (vvd::SettingsReadPath / SettingsWritePath, Core/FileUtil.h).
//
// Engine-static settings (TextureRenderer, VolumeRenderer, MultiVolumeRenderer,
// TreeLayer, VolumeMeshConv, Texture::mask_undo_num_, DataManager) are applied
// here by ApplyToEngine() and by the controls that change them. Settings that
// live in the render view are exposed through getters; settingsChanged() is
// emitted whenever one of them changes so the main window can push them.

#include <QDialog>
#include <QString>
#include "RigidCellsWarp.h"
#include "ColorDepthMIP.h"

class QCheckBox;
class QSlider;
class QLineEdit;
class QComboBox;
class QPushButton;
class QWidget;
class DataManager;

#define SETTING_FILE_NAME_QT "fluorender.set"

class SettingsDialog : public QDialog
{
	Q_OBJECT
public:
	explicit SettingsDialog(QWidget* parent = nullptr);
	~SettingsDialog() override;

	// DataManager receiving override vox / pvxml flip / wavelength colors /
	// compression / skip bricks / time id. Applies them right away.
	void SetDataManager(DataManager* mgr);

	// full path of the settings file (wx SettingDlg::GetSettings/SaveSettings)
	static QString SettingsFilePath(bool forSave = false);
	// <resources>/Fonts (wx: GetResourcesDir()/Fonts on macOS, exe dir/Fonts elsewhere)
	static QString FontsDir();

	void LoadSettings();   // wx GetSettings(): defaults, then the file, then UpdateUI()
	void UpdateUI();       // members -> controls (no side effects, emits settingsChanged())
	void SaveSettings();   // members -> file

	// Applies every engine-static setting, mirroring VRenderFrame's start-up
	// code, SetTextureRendererSettings() and SetTextureUndos(). The main memory
	// buffer is applied only on the first call (wx: at start-up only).
	void ApplyToEngine();

	// ---- getters / setters (same names as SettingDlg) ----
	int GetGMCMode() const { return m_gmc_mode; }
	bool GetProjSave() const { return m_prj_save; }
	bool GetRealtimeCompress() const { return m_realtime_compress; }
	void SetRealtimeCompress(bool val) { m_realtime_compress = val; }
	bool GetSkipBricks() const { return m_skip_bricks; }
	void SetSkipBricks(bool val) { m_skip_bricks = val; }
	// "/save cmp" (VRenderFrame::m_save_compress in wx)
	bool GetSaveCompress() const { return m_save_compress; }
	void SetSaveCompress(bool val) { m_save_compress = val; }
	bool GetTestMode(int type) const;  // 1:speed test 2:parameter test 3:wireframe
	int GetPeelingLyers() const { return m_peeling_layers; }
	int GetPeelingLayers() const { return m_peeling_layers; }
	bool GetMicroBlend() const { return m_micro_blend; }
	bool GetShadowDirEnabled() const { return m_shadow_dir; }
	void GetShadowDir(double& x, double& y) const { x = m_shadow_dir_x; y = m_shadow_dir_y; }
	bool GetMouseInt() const { return m_mouse_int; }
	// sample rate while the mouse is down: 1/N of the normal rate (N = 2..5,
	// default 2 = the engine's former fixed value) -- TextureRenderer::set_interactive_rate_div
	int GetMouseIntDiv() const { return m_mouse_int_div; }
	bool GetMouseLv() const { return m_mouse_lv; }        // view SetAdaptiveRes()
	int GetWavelengthColor(int n) const;
	QString GetTimeId() const { return m_time_id; }
	void SetTimeId(const QString& id) { m_time_id = id; }
	bool GetGradBg() const { return m_grad_bg; }
	// Qt only (/sphere rotation, default on): virtual sphere rotation
	bool GetSphereRot() const { return m_sphere_rot; }
	void SetGradBg(bool val) { m_grad_bg = val; }
	bool GetRotLink() const;                               // view OnRotLink()
	bool GetOverrideVox() const { return m_override_vox; }
	void SetOverrideVox(bool val) { m_override_vox = val; }
	double GetSoftThreshold() const { return m_soft_threshold; }
	void SetSoftThreshold(double val) { m_soft_threshold = val; }
	bool GetRunScript() const { return m_run_script; }
	void SetRunScript(bool val) { m_run_script = val; }
	QString GetScriptFile() const { return m_script_file; }
	void SetScriptFile(const QString& file) { m_script_file = file; }
	int GetPaintHistDepth() const { return m_paint_hist_depth; }
	void SetPaintHistDepth(int val) { m_paint_hist_depth = val; }
	QString GetFontFile() const { return m_font_file; }    // "FreeSans.ttf" or ""
	void SetFontFile(const QString& file) { m_font_file = file; }
	// full path of the font to load (FreeSans.ttf when none is set), as
	// VRenderVulkanView's constructor builds it
	QString GetFontPath() const;
	int GetTextSize() const { return m_text_size; }
	void SetTextSize(int size) { m_text_size = size; }
	int GetTextColor() const { return m_text_color; }      // 0 BG inverted, 1 background, 2 vol sec color
	void SetTextColor(int c) { m_text_color = c; }
	bool GetStayTop() const { return m_stay_top; }
	bool GetShowCursor() const { return m_show_cursor; }
	bool GetMemSwap() const { return m_mem_swap; }
	void SetMemSwap(bool val) { m_mem_swap = val; }
	double GetGraphicsMem() const { return m_graphics_mem; }
	void SetGraphicsMem(double val) { m_graphics_mem = val; }
	double GetLargeDataSize() const { return m_large_data_size; }
	void SetLargeDataSize(double val) { m_large_data_size = val; }
	int GetForceBrickSize() const { return m_force_brick_size; }
	void SetForceBrickSize(int val) { m_force_brick_size = val; }
	int GetResponseTime() const { return m_up_time; }
	void SetResponseTime(int val) { m_up_time = val; }
	int GetUpdateOrder() const { return m_update_order; }
	void SetUpdateOrder(int val) { m_update_order = val; }
	double GetMainMemBufSize() const { return m_main_mem_buf_size; }
	void SetMainMemBufSize(double val) { m_main_mem_buf_size = val; }
	int GetPointVolumeMode() const { return m_point_volume_mode; }
	void SetPointVolumeMode(int mode) { m_point_volume_mode = mode; }
	bool GetRulerUseTransf() const { return m_ruler_use_transf; }
	void SetRulerUseTransf(bool val) { m_ruler_use_transf = val; }
	bool GetRulerTimeDep() const { return m_ruler_time_dep; }
	// Measurement "Auto-select" (Qt only, /ruler auto select; default off)
	bool GetRulerAutoSelect() const { return m_ruler_auto_select; }
	void SetRulerAutoSelect(bool val) { m_ruler_auto_select = val; }
	// Measurement "Cells: shape preserving" transform options (/rigid cells;
	// Qt only, the stiffness is the dialog's shared slider and not stored)
	const rigidcells::Params& GetCellWarpParams() const { return m_cell_warp; }
	void SetCellWarpParams(const rigidcells::Params& p) { m_cell_warp = p; }
	// "Save Color Depth MIP..." dialog values (/color depth mip; Qt only)
	const cdm::Options& GetColorDepthMIPOptions() const { return m_cdm; }
	void SetColorDepthMIPOptions(const cdm::Options& o) { m_cdm = o; }
	// Measurement "Target:" (/warp target; Qt only): "auto" (default),
	// "same" or "vol:<volume name>"
	QString GetWarpTarget() const { return m_warp_target; }
	void SetWarpTarget(const QString& t) { m_warp_target = t; }
	void SetRulerTimeDep(bool val) { m_ruler_time_dep = val; }
	bool GetPvxmlFlipX() const { return m_pvxml_flip_x; }
	void SetPvxmlFlipX(bool flip) { m_pvxml_flip_x = flip; }
	bool GetPvxmlFlipY() const { return m_pvxml_flip_y; }
	void SetPvxmlFlipY(bool flip) { m_pvxml_flip_y = flip; }
	int GetRedBit() const { return m_red_bit; }
	int GetGreenBit() const { return m_green_bit; }
	int GetBlueBit() const { return m_blue_bit; }
	int GetAlphaBit() const { return m_alpha_bit; }
	int GetDepthBit() const { return m_depth_bit; }
	int GetSamples() const { return m_samples; }
	int GetGLMajorVer() const { return m_gl_major_ver; }
	int GetGLMinorVer() const { return m_gl_minor_ver; }
	int GetGLProfileMask() const { return m_gl_profile_mask; }
	int GetCLDeviceID() const { return m_cl_device_id; }
	bool GetUseFogMesh() const { return m_use_fog_mesh; }
	void SetUseFogMesh(bool val) { m_use_fog_mesh = val; }

Q_SIGNALS:
	// Any view-side setting changed (peeling layers, micro blend, shadow
	// direction, gradient bg, mouse interaction / resolution, fog on meshes,
	// text color, ...). Push the getters to every view and redraw.
	void settingsChanged();
	// Font face or size changed: reload the text renderer face
	// (TextRenderer::LoadNewFace(path, size)) / SetSize(size), then redraw.
	void fontChanged(const QString& fontPath, int size);
	void textSizeChanged(int size);
	// "Link all rendering views' rotations." toggled (wx view->OnRotLink()).
	void rotationLinkChanged(bool link);
	// Streaming / memory settings were re-applied to TextureRenderer
	// (wx EnableStreaming -> RefreshVRenderViews()).
	void engineSettingsApplied();

private:
	QWidget* CreateProjectPage();
	QWidget* CreateRenderingPage();
	QWidget* CreatePerformancePage();
	QWidget* CreateFormatPage();

	void SetDefaults();
	void EnableStreaming(bool enable);   // UI enable + ApplyToEngine
	void EnableStreamingUI(bool enable);
	void ApplyTextureRendererSettings(); // VRenderFrame::SetTextureRendererSettings
	void ApplyWavelengthColors();
	void SetShadowDir(double deg);
	double GetShadowDirDeg() const;

	void OnDone();
	void OnCancel();
	void OnScriptFileBtn();

	DataManager* m_data_mgr = nullptr;
	bool m_engine_initialized = false;

	// ---- settings (see SettingDlg.h for meanings) ----
	int m_gmc_mode;
	bool m_prj_save;
	bool m_realtime_compress;
	bool m_skip_bricks;
	bool m_save_compress;
	bool m_test_speed;
	bool m_test_param;
	bool m_test_wiref;
	int m_peeling_layers;
	bool m_micro_blend;
	bool m_shadow_dir;
	double m_shadow_dir_x;
	double m_shadow_dir_y;
	bool m_mouse_int;
	int m_mouse_int_div = 2;
	bool m_mouse_lv;
	int m_wav_color1;   // 1-red 2-green 3-blue 4-purple 5-white
	int m_wav_color2;
	int m_wav_color3;
	int m_wav_color4;
	QString m_time_id;
	bool m_grad_bg;
	bool m_sphere_rot = true;
	bool m_override_vox;
	double m_soft_threshold;
	bool m_run_script;
	QString m_script_file;
	QString m_font_file;
	int m_text_size;
	int m_text_color;
	bool m_mem_swap;
	double m_graphics_mem;
	double m_main_mem_buf_size;
	double m_large_data_size;
	int m_force_brick_size;
	int m_up_time;
	int m_update_order;
	int m_point_volume_mode;
	bool m_ruler_use_transf;
	bool m_ruler_time_dep;
	bool m_ruler_auto_select;
	rigidcells::Params m_cell_warp;
	cdm::Options m_cdm;
	QString m_warp_target;
	bool m_pvxml_flip_x;
	bool m_pvxml_flip_y;
	int m_red_bit;
	int m_green_bit;
	int m_blue_bit;
	int m_alpha_bit;
	int m_depth_bit;
	int m_samples;
	int m_gl_major_ver;
	int m_gl_minor_ver;
	int m_gl_profile_mask;
	int m_cl_device_id;
	int m_paint_hist_depth;
	bool m_stay_top;
	bool m_show_cursor;
	bool m_use_fog_mesh;

	// ---- controls ----
	// project
	QCheckBox* m_prj_save_chk = nullptr;
	QCheckBox* m_realtime_cmp_chk = nullptr;
	QComboBox* m_font_cmb = nullptr;
	QComboBox* m_font_size_cmb = nullptr;
	QComboBox* m_text_color_cmb = nullptr;
	QCheckBox* m_run_script_chk = nullptr;
	QLineEdit* m_script_file_text = nullptr;
	QPushButton* m_script_file_btn = nullptr;
	QSlider* m_paint_hist_depth_sldr = nullptr;
	QLineEdit* m_paint_hist_depth_text = nullptr;
	// rendering
	QCheckBox* m_micro_blend_chk = nullptr;
	QCheckBox* m_use_fog_mesh_chk = nullptr;
	QSlider* m_peeling_layers_sldr = nullptr;
	QLineEdit* m_peeling_layers_text = nullptr;
	QCheckBox* m_shadow_dir_chk = nullptr;
	QSlider* m_shadow_dir_sldr = nullptr;
	QLineEdit* m_shadow_dir_text = nullptr;
	QCheckBox* m_grad_bg_chk = nullptr;
	QCheckBox* m_rot_link_chk = nullptr;
	QCheckBox* m_sphere_rot_chk = nullptr;
	// performance
	QCheckBox* m_mouse_int_chk = nullptr;
	QComboBox* m_mouse_int_div_cmb = nullptr;
	QCheckBox* m_mouse_lv_chk = nullptr;
	QCheckBox* m_streaming_chk = nullptr;
	QSlider* m_graphics_mem_sldr = nullptr;
	QLineEdit* m_graphics_mem_text = nullptr;
	QSlider* m_large_data_sldr = nullptr;
	QLineEdit* m_large_data_text = nullptr;
	QSlider* m_block_size_sldr = nullptr;
	QLineEdit* m_block_size_text = nullptr;
	QSlider* m_response_time_sldr = nullptr;
	QLineEdit* m_response_time_text = nullptr;
	QSlider* m_main_mem_buf_sldr = nullptr;
	QLineEdit* m_main_mem_buf_text = nullptr;
	// file format
	QCheckBox* m_override_vox_chk = nullptr;
	QComboBox* m_wav_color1_cmb = nullptr;
	QComboBox* m_wav_color2_cmb = nullptr;
	QComboBox* m_wav_color3_cmb = nullptr;
	QComboBox* m_wav_color4_cmb = nullptr;
	// buttons
	QPushButton* m_save_btn = nullptr;
	QPushButton* m_close_btn = nullptr;
};

#endif // VVD_QT_SETTINGSDIALOG_H
