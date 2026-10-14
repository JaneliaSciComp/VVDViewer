#ifndef VVD_QT_MOVIEPANEL_H
#define VVD_QT_MOVIEPANEL_H

// Record/Export pane: Qt port of the wx VMovieView. Tabs: Basic, Advanced
// (RecorderPanel = wx RecorderDlg), Auto Key, Cropping, plus the shared
// footer (FPS / repeat / Capture view, play / rewind / time slider / time
// text / Save...).
//
//   wx VMovieView                         here
//   OnPrev / OnStop / OnTimer / OnRewind  OnPrev / OnStop / OnTimer / OnRewind
//   SetRendering / SetProgress /          same names; the view is the
//     SetInitRotation                     RenderCanvas (rotations, SetParams,
//                                         Set4DSeqFrame / Set3DBatFrame)
//   OnRun (file dialog +                  OnRun: QFileDialog (non-native) with
//     CreateExtraCaptureControl)          the same "Additional Options" box
//   SaveMovie / WriteFrameToFile          same; frames come from the canvas
//     (m_recording, DownloadRecordedFrame) recording (RequestRecordedFrame)
//   encoder_ (QVideoEncoder)              same encoder (Formats/QVideoEncoder)
//   Cropping page (EnableFrame, CalcFrame, the canvas frame (ViewOverlays draws it,
//     SetFrame, spin buttons)             the canvas crops recorded frames)
//   LoadSettings / SaveDefault            same file (default_movie_settings.dft)
//
// Timing is the wx one: one timer tick per frame at 1000/fps ms; while
// recording a tick does nothing until the frame of the current animation
// state has been rendered completely (streaming loop done) and read back,
// so the output never depends on the render speed. As in wx, a movie of
// L s at F fps has F*L+1 frames (index 0 .. F*L, both ends of the
// animation); TIFF names are "<name>_<index>.tif" with the index padded
// to the digits of F*L.
//
// Not ported: the 4D script runner (SettingDlg "Run script", wx
// Run4DScript / m_batch_mode), which the Qt app does not have.

#include <QWidget>
#include <QString>
#include <QRect>
#include <QStringList>
#include <functional>
#include <memory>

class QTabBar;
class QStackedWidget;
class QCheckBox;
class QRadioButton;
class QLineEdit;
class QComboBox;
class QPushButton;
class QSlider;
class QLabel;
class QSpinBox;
class QTreeWidget;
class QTimer;
class QVBoxLayout;
class RenderCanvas;
class ViewOverlays;
class Interpolator;
class RecorderPanel;
class QVideoEncoder;

namespace ProjectIO { struct ProjectMovie; }

class MoviePanel : public QWidget
{
	Q_OBJECT
public:
	enum Mode { MODE_BASIC = 0, MODE_ADVANCED = 1, MODE_AUTOKEY = 2, MODE_CROPPING = 3 };
	enum RotStyle { ROT_LINEAR = 0, ROT_SMOOTH = 1 };

	// What the pane needs from the main window (VRenderFrame in wx).
	struct Hooks
	{
		std::function<bool()> get_proj_save;                // SettingDlg::GetProjSave
		std::function<bool()> get_compress;                 // VRenderFrame::GetCompression
		std::function<void(bool)> set_compress;             // VRenderFrame::SetCompression
		std::function<bool()> get_embed;                    // VRenderFrame::GetEmbedProject
		std::function<void(bool)> set_embed;                // VRenderFrame::SetEmbedProject
		std::function<bool(const QString&)> save_project;   // VRenderFrame::SaveProject
		// SettingDlg::GetRunScript() && wxFileExists(GetScriptFile())
		std::function<bool()> script_enabled;
		// VRenderVulkanView::Run4DScript() (every shown volume)
		std::function<void()> run_4d_script;
		// VRenderFrame::GetView(name): the canvas / overlays of a view
		std::function<bool(const QString&, RenderCanvas*&, ViewOverlays*&)> find_view;
	};

	explicit MoviePanel(QWidget* parent = nullptr);
	~MoviePanel() override;

	void SetRenderCanvas(RenderCanvas* canvas);
	RenderCanvas* GetRenderCanvas() const { return m_canvas; }
	void SetOverlays(ViewOverlays* ov) { m_overlays = ov; }
	void SetInterpolator(Interpolator* ip);
	Interpolator* GetInterpolator() const { return m_interpolator; }
	void SetHooks(const Hooks& h) { m_hooks = h; }
	RecorderPanel* GetRecorder() const { return m_advanced_movie; }

	// capture-view list
	void AddView(const QString& name);
	void DeleteView(const QString& name);
	void SetView(int index);
	QString CurrentView() const;
	int ViewCount() const;                // entries of the Capture view combo
	// the view of the combo (wx GetView(m_views_cmb->GetValue())) becomes
	// the canvas the panel records / plays
	void ResolveView();

	// tabs (wx SetMode / GetMode)
	void SetMode(int page);
	int GetMode() const;

	// as in VMovieView
	void DisableRot();
	void EnableRot();
	void DisableTime();
	void EnableTime();
	void UpFrame();
	void DownFrame();
	void SetCurrentTime(int t);
	void SetMovieTime(double seconds);
	void SetProgress(double pcnt);          // 0..1
	void SetRendering(double pcnt);         // 0..1
	long GetFPS() const;
	void DisableRendering() { m_without_rendering = true; }
	void EnableRendering() { m_without_rendering = false; }
	void GenKey();
	// wx SaveMovie: record to file (.mov/.mp4: video, else TIFF sequence)
	// without the dialog; OnRun = dialog + rewind + SaveMovie
	void SaveMovie(const QString& filename);
	bool IsRecording() const { return m_record; }
	bool IsRunning() const { return m_running; }
	void Play() { OnPrev(); }
	void Stop() { OnStop(); }
	void Rewind() { OnRewind(); }
	static double GetBitrate() { return m_Mbitrate; }
	static void SetBitrate(double b) { m_Mbitrate = b; }

	// state getters
	bool IsRotationEnabled() const;
	int RotationAxis() const;               // 1=X 2=Y 3=Z
	double RotationDegrees() const;
	int RotationStyle() const;              // RotStyle
	bool IsSequenceEnabled() const;
	double MovieLength() const;             // seconds
	bool IsCroppingEnabled() const;

	// /movie_panel of a project (wx VRenderFrame::SaveProject / OpenProject)
	void SaveProjectSettings(ProjectIO::ProjectMovie& mv) const;
	void LoadProjectSettings(const ProjectIO::ProjectMovie& mv);

	// testing: widgets for screenshots
	QWidget* PageWidget(int page) const;
	// testing: VVD_QT_GRAB_MOVDLG=<png> grabs the save dialog and cancels it
	void RunSaveDialog() { OnRun(); }

	// wx widgets, public as in VMovieView (project I/O, tests)
	QLineEdit* m_fps_text = nullptr;
	QComboBox* m_views_cmb = nullptr;
	QCheckBox* m_repeat_chk = nullptr;
	QPushButton* m_play_btn = nullptr;
	QPushButton* m_rewind_btn = nullptr;
	QSlider* m_progress_sldr = nullptr;
	QLineEdit* m_progress_text = nullptr;
	QPushButton* m_save_btn = nullptr;
	QCheckBox* m_seq_chk = nullptr;
	QLineEdit* m_time_start_text = nullptr;
	QLineEdit* m_time_end_text = nullptr;
	QPushButton* m_inc_time_btn = nullptr;
	QPushButton* m_dec_time_btn = nullptr;
	QLineEdit* m_time_current_text = nullptr;
	QCheckBox* m_rot_chk = nullptr;
	QRadioButton* m_x_rd = nullptr;
	QRadioButton* m_y_rd = nullptr;
	QRadioButton* m_z_rd = nullptr;
	QLineEdit* m_degree_end = nullptr;
	QComboBox* m_rot_int_cmb = nullptr;
	QCheckBox* m_rewind_chk = nullptr;
	QLineEdit* m_movie_time = nullptr;
	QTreeWidget* m_auto_key_list = nullptr;
	QPushButton* m_gen_keys_btn = nullptr;
	QCheckBox* m_frame_chk = nullptr;
	QPushButton* m_reset_btn = nullptr;
	QSpinBox* m_center_x = nullptr;
	QSpinBox* m_center_y = nullptr;
	QSpinBox* m_width = nullptr;
	QSpinBox* m_height = nullptr;

Q_SIGNALS:
	// a recording finished (wx: none; used by the test hooks)
	void movieSaved(const QString& filename, bool ok);
	void runningChanged(bool running);

private Q_SLOTS:
	void OnPrev();          // play / pause
	void OnRun();           // Save...
	void OnStop();
	void OnRewind();
	void OnTimer();
	void OnRotateChecked(bool on);
	void OnSequenceChecked(bool on);
	void OnTimeChange(int pos);     // slider (user)
	void OnTimeEnter();             // progress text
	void OnTimeText();              // current time text
	void OnPageChanged(int idx);
	void OnFrameCheck(bool on);
	void OnResetFrame();
	void OnFrameEditing();
	void OnViewSelected(int idx);

private:
	QWidget* CreateSimplePage();
	QWidget* CreateAdvancedPage();
	QWidget* CreateAutoKeyPage();
	QWidget* CreateCroppingPage();
	void BuildFooter(QVBoxLayout* root);
	void Init();
	void GetSettings();
	void SetInitRotation();
	void WriteFrameToFile(int total_frames);
	void SetFrameFields(int x, int y, int w, int h);
	void PushFrame(bool enable, int x, int y, int w, int h);
	void ViewGetRotations(double& x, double& y, double& z) const;
	void ViewSetRotations(double x, double y, double z);
	void LoadSettings();
	void SaveDefault();
	static QString StyleSheetText();

	RenderCanvas* m_canvas = nullptr;
	ViewOverlays* m_overlays = nullptr;
	Interpolator* m_interpolator = nullptr;
	Hooks m_hooks;
	QTabBar* m_tabs = nullptr;
	QStackedWidget* m_stack = nullptr;
	RecorderPanel* m_advanced_movie = nullptr;
	QWidget* m_pages[4] = { nullptr, nullptr, nullptr, nullptr };

	QTimer* m_timer = nullptr;
	int m_last_frame = 0;
	// wx m_batch_mode / m_prv_fr: the 4D script runs once per new time point
	// while playing (OnPrev / OnTimer)
	bool m_batch_mode = false;
	int m_prv_fr = 0;
	double m_starting_rot = 0.0;
	double m_cur_time = 0.0;
	QString m_filename;          // without extension while recording (wx m_filename)
	QString m_full_filename;     // as chosen
	bool m_running = false;
	bool m_record = false;
	bool m_record_ok = true;
	int m_current_page = 0;
	double m_movie_time_basic = 5.0;
	std::unique_ptr<QVideoEncoder> m_encoder;   // wx encoder_
	bool m_encoder_open = false;
	QString m_filetype;          // wx filetype_ (".mov", ".mp4", ".tif")
	int m_rot_int_type = 0;      // 0-linear; 1-smooth
	bool m_delayed_stop = false;
	bool m_without_rendering = false;
	int m_video_w = 0, m_video_h = 0;
	double m_loaded_bitrate = 10.0;

	static double m_Mbitrate;    // wx VMovieView::m_Mbitrate
};

#endif // VVD_QT_MOVIEPANEL_H
