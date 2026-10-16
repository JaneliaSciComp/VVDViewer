#ifndef VVD_QT_CAPTUREDIALOG_H
#define VVD_QT_CAPTUREDIALOG_H

// "Capture" button of the render view: Qt port of VRenderView::OnCapture,
// CreateExtraCaptureControl and its handlers (OnCh1Check, OnChEmbedCheck,
// OnCapWidthTextChange, OnCapHeightTextChange, OnSetOrgResButton,
// OnSetDispResButton, the "capture each group" lambda) and the project
// save of OnCapture. The engine part (tiles, readback, TIFF) is
// RenderCanvasCapture.cpp.
//
// Save dialog: Qt's own QFileDialog (DontUseNativeDialog) with the wx
// "Additional Options" box added to its layout, filter "*.tif", overwrite
// prompt. Options and their persistence are the wx ones:
//   Lempel-Ziv-Welch Compression     VRenderFrame::m_save_compress -> settings "/save cmp" (hooks)
//   width / height, 1:1, Disp        VRenderView::m_cap_resx/resy (re-initialised on every open)
//   Embed all files in the project   VRenderFrame::m_vrp_embed (session; shown only with
//     folder                         the Settings "Save project when capture viewport" option)
//   Capture each group separately    VRenderView::m_cap_group_each (session)
// wx has no alpha, 32-bit float or DPI option for captures; neither has this.

#include <QString>
#include <functional>

class QWidget;
class RenderCanvas;

namespace CaptureDialog
{
	// What the dialog needs from the main window (VRenderFrame in wx).
	struct Hooks
	{
		std::function<bool()> get_proj_save;                   // SettingDlg::GetProjSave
		std::function<bool()> get_compress;                    // VRenderFrame::GetCompression
		std::function<void(bool)> set_compress;                // VRenderFrame::SetCompression
		std::function<bool()> get_embed;                       // VRenderFrame::GetEmbedProject
		std::function<void(bool)> set_embed;                   // VRenderFrame::SetEmbedProject
		std::function<bool(const QString&)> save_project;      // VRenderFrame::SaveProject
	};

	struct Options
	{
		int resx = -1, resy = -1;       // m_cap_resx / m_cap_resy
		bool compress = true;
		bool group_each = false;        // m_cap_group_each
		bool proj_save = false;         // SettingDlg::GetProjSave at dialog time
	};

	// wx VRenderView::OnCapture. Returns false when cancelled.
	bool Run(QWidget* parent, RenderCanvas* canvas, const Hooks& hooks);

	// The part of OnCapture after the dialog (also the VVD_QT_CAPTURE test
	// path): group capture, or capture (tiled when the size differs from the
	// view) + "<file>_project/<name>_project.vrp" when opt.proj_save.
	void Execute(QWidget* parent, RenderCanvas* canvas, const Hooks& hooks,
		const QString& file, const Options& opt);

	// wx m_cap_group_each (static, default false)
	bool GetGroupEach();
	void SetGroupEach(bool b);
}

#endif
