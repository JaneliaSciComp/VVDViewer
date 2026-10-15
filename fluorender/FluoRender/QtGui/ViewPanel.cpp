#include "ViewPanel.h"
#include "TestRun.h"

#include <QTimer>
#include <QCursor>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include "PaneScrollArea.h"
#include <QCheckBox>
#include <QRadioButton>
#include <QPushButton>
#include <QSlider>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QSignalBlocker>
#include <QDoubleValidator>
#include <QToolButton>
#include <functional>
#include <algorithm>
#include <cmath>

#include "RenderCanvas.h"
#include "ViewOverlays.h"
#include "LegendMenu.h"
#include "DataManager.h"
#include "Core/FileUtil.h"

namespace
{
	QLabel* Label(const QString& t, QWidget* p) { return new QLabel(t, p); }

	QLineEdit* Edit(int width, const QString& t, QWidget* p)
	{
		auto* e = new QLineEdit(t, p);
		e->setFixedWidth(width);
		e->setAlignment(Qt::AlignLeft);
		return e;
	}

	QPushButton* Button(const QString& t, QWidget* p)
	{
		auto* b = new QPushButton(t, p);
		b->setAutoDefault(false);
		return b;
	}

	// wxSpinButton: two auto-repeating arrows; cb(+1) for "up", cb(-1) for "down"
	QWidget* SpinButtons(Qt::Orientation o, QWidget* p, std::function<void(int)> cb)
	{
		auto* w = new QWidget(p);
		QBoxLayout* l = o == Qt::Vertical ? (QBoxLayout*)new QVBoxLayout(w) : (QBoxLayout*)new QHBoxLayout(w);
		l->setContentsMargins(0, 0, 0, 0);
		l->setSpacing(0);
		auto* up = new QToolButton(w);
		auto* dn = new QToolButton(w);
		up->setArrowType(o == Qt::Vertical ? Qt::UpArrow : Qt::RightArrow);
		dn->setArrowType(o == Qt::Vertical ? Qt::DownArrow : Qt::LeftArrow);
		for (QToolButton* b : { up, dn })
		{
			b->setAutoRepeat(true);
			b->setFixedSize(o == Qt::Vertical ? QSize(18, 12) : QSize(12, 18));
			b->setFocusPolicy(Qt::NoFocus);
		}
		if (o == Qt::Vertical) { l->addWidget(up); l->addWidget(dn); }
		else { l->addWidget(dn); l->addWidget(up); }
		QObject::connect(up, &QToolButton::clicked, w, [cb]() { cb(1); });
		QObject::connect(dn, &QToolButton::clicked, w, [cb]() { cb(-1); });
		return w;
	}

	// default_view_settings.dft, same lookup as VRenderView::LoadSettings / SaveDefault
	VVDString ViewDefaultFile(bool for_save)
	{
		const VVDString name(L"default_view_settings.dft");
		const VVDString expath = vvd::GetExecutableDir();
#if defined(__APPLE__)
		(void)for_save;
		return expath + L"/../Resources/" + name;
#else
		VVDString dft = vvd::JoinPath(expath, name);
		const VVDString dft2 = vvd::JoinPath(vvd::GetUserConfigDir(), name);
		if (for_save)
		{
			if (!vvd::FileExists(dft) && vvd::FileExists(dft2))
				dft = dft2;
		}
		else if (!vvd::FileExists(dft))
			dft = dft2;
		return dft;
#endif
	}
}

ViewPanel::ViewPanel(QWidget* parent)
	: QWidget(parent)
{
	m_canvas = new RenderCanvas(this);

	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);
	// the control rows (Layered ... FreeFly + Quality: ~1070 px; the
	// rotation row: ~910 px) scroll horizontally when the view is narrower,
	// instead of setting the view's (and the window's) minimum width
	v->addWidget(new PaneScrollArea(BuildTopBar(), Qt::Horizontal, QSize(80, 0), this));

	auto* mid = new QHBoxLayout();
	mid->setContentsMargins(0, 0, 0, 0);
	mid->setSpacing(0);
	mid->addWidget(BuildDepthColumn());
	mid->addWidget(m_canvas, 1);
	mid->addWidget(BuildZoomColumn());
	v->addLayout(mid, 1);
	v->addWidget(new PaneScrollArea(BuildBottomBar(), Qt::Horizontal, QSize(80, 0), this));

	connect(m_canvas, &RenderCanvas::rotationChanged, this, &ViewPanel::OnCanvasRotation);
	connect(m_canvas, &RenderCanvas::zoomChanged, this, &ViewPanel::OnCanvasZoom);
	// SetPersp(false) leaves free-fly mode (wx m_vrv->m_free_chk->SetValue(false))
	connect(m_canvas, &RenderCanvas::freeModeChanged, this, [this](bool on) {
		QSignalBlocker b(m_free_chk); m_free_chk->setChecked(on); });
	OnCanvasZoom(m_canvas->GetScaleFactor());
	SetAovText(m_aov_sldr->value());
	LoadDefaultSettings();
	// wx m_timer (ID_Timer, 100 ms) -> OnAovSldrIdle
	auto* aov_timer = new QTimer(this);
	connect(aov_timer, &QTimer::timeout, this, &ViewPanel::PollAovHover);
	aov_timer->start(100);
}

// wx VRenderView::OnAovSldrIdle
void ViewPanel::PollAovHover()
{
	// scripted test runs: only the test's pointer override, so that the real
	// pointer resting on the slider cannot change a capture
	if (!m_cursor_override && VVDQtScriptedRun())
		return;
	int plane_mode = 0;
	bool hold = false;
	if (!m_clip_state || !m_clip_state(plane_mode, hold))
		return;
	if (hold)
		return;
	if (m_canvas->IsCapturing())
		return;
	// ClippingView.h PM_LOWTRANSBACK 3, PM_NORMALBACK 4: planes behind the data
	const bool full = plane_mode == 3 || plane_mode == 4;
	const bool inside = m_aov_sldr->isVisible() &&
		m_aov_sldr->rect().contains(m_aov_sldr->mapFromGlobal(m_cursor_override ? m_cursor_pos : QCursor::pos()));
	if (inside)
	{
		if (!m_draw_clip)
		{
			m_canvas->SetDrawClip(true);
			m_canvas->SetClipMask(-1);
			if (full) m_canvas->RefreshView();
			else m_canvas->RefreshOverlays();
			m_draw_clip = true;
		}
	}
	else
	{
		if (m_draw_clip)
		{
			m_canvas->SetDrawClip(false);
			if (full) m_canvas->RefreshView();
			else m_canvas->RefreshOverlays();
			m_draw_clip = false;
		}
	}
}

QWidget* ViewPanel::BuildTopBar()
{
	auto* w = new QWidget(this);
	auto* h = new QHBoxLayout(w);
	h->setContentsMargins(10, 3, 5, 3);
	h->setSpacing(5);

	// wx: m_volume_seq_rd / m_volume_multi_rd / m_volume_comp_rd ->
	// OnVolumeMethodCheck -> SetVolMethod + RefreshGL
	m_layered_rd = new QRadioButton(tr("Layered"), w);
	m_depth_rd = new QRadioButton(tr("Depth"), w);
	m_comp_rd = new QRadioButton(tr("Composite"), w);
	h->addWidget(m_layered_rd);
	h->addWidget(m_depth_rd);
	h->addWidget(m_comp_rd);
	h->addSpacing(10);
	SyncVolMethod(m_canvas->GetVolMethod());
	connect(m_layered_rd, &QRadioButton::toggled, this, [this](bool on) { if (on && !m_updating) m_canvas->SetVolMethod(RenderCanvas::VolMethodSeq); });
	connect(m_depth_rd, &QRadioButton::toggled, this, [this](bool on) { if (on && !m_updating) m_canvas->SetVolMethod(RenderCanvas::VolMethodMulti); });
	connect(m_comp_rd, &QRadioButton::toggled, this, [this](bool on) { if (on && !m_updating) m_canvas->SetVolMethod(RenderCanvas::VolMethodComp); });
	connect(m_canvas, &RenderCanvas::volMethodChanged, this, &ViewPanel::SyncVolMethod);

	// wx VRenderView::OnCapture (CaptureDialog.cpp); disabled during a group
	// capture like wx m_capture_btn
	auto* capture = Button(tr("Capture"), w);
	m_capture_btn = capture;
	capture->setToolTip(tr("Save a snapshot of the render view"));
	connect(capture, &QPushButton::clicked, this, [this]() {
		CaptureDialog::Run(this, m_canvas, m_capture_hooks);
		if (m_canvas->IsCapturing() && CaptureDialog::GetGroupEach()) m_capture_btn->setEnabled(false);
	});
	connect(m_canvas, &RenderCanvas::groupCaptureFinished, this, [this]() { m_capture_btn->setEnabled(true); });
	h->addWidget(capture);
	h->addSpacing(10);

	h->addWidget(Label(tr("Background:"), w));
	m_bg_btn = Button(QString(), w);
	m_bg_btn->setFixedSize(60, 22);
	m_bg_btn->setStyleSheet("background-color: #000000; border: 1px solid #888;");
	connect(m_bg_btn, &QPushButton::clicked, this, &ViewPanel::OnBackground);
	h->addWidget(m_bg_btn);
	h->addSpacing(10);

	m_center_chk = new QCheckBox(tr("Center"), w);
	m_info_chk = new QCheckBox(tr("Info."), w);
	m_legend_chk = new QCheckBox(w);
	// wx CreateBar: the legend check box is created checked and hidden
	m_legend_chk->setChecked(true);
	m_legend_chk->hide();
	auto* legend_btn = Button(tr("Legend"), w);
	// drop-down: one check row per displayed volume / mesh (wx LegendPanel)
	m_legend_menu = new LegendMenu(m_canvas, legend_btn);
	legend_btn->setMenu(m_legend_menu);
	legend_btn->setFixedWidth(76);
	m_legend_btn = legend_btn;
	m_center_chk->setEnabled(false);   // until SetOverlays
	m_info_chk->setEnabled(false);
	legend_btn->setEnabled(false);
	m_intp_chk = new QCheckBox(tr("Intrp."), w);
	m_intp_chk->setChecked(true);
	h->addWidget(m_center_chk);
	h->addWidget(m_info_chk);
	h->addWidget(m_legend_chk);
	h->addWidget(legend_btn);
	h->addSpacing(10);
	h->addWidget(m_intp_chk);
	// wx OnIntpCheck: m_glview->SetIntp (view-wide; StartLoopUpdate pushes it
	// to every shown volume and the multi-volume renderer uses it) + RefreshGL
	connect(m_intp_chk, &QCheckBox::toggled, this, [this](bool on) { m_canvas->SetIntp(on); });

	// wx m_res_mode_text / m_res_mode_combo: shown only while a pyramid
	// (brxml) volume is displayed (VRenderView::ToggleQualityDropDownBox)
	h->addSpacing(5);
	m_res_label = Label(tr("Quality:"), w);
	m_res_combo = new QComboBox(w);
	m_res_combo->addItems({ tr("Max"), tr("Best(x1)"), tr("Fine(x1.5)"), tr("Standard(x2)"), tr("Coarse(x3)"), tr("Min") });
	m_res_combo->setCurrentIndex(m_canvas->GetResMode());
	h->addWidget(m_res_label);
	h->addWidget(m_res_combo);
	m_res_label->setVisible(m_canvas->HasPyramidVolume());
	m_res_combo->setVisible(m_canvas->HasPyramidVolume());
	connect(m_res_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
		if (!m_updating && i >= 0) m_canvas->SetResMode(i); });
	connect(m_canvas, &RenderCanvas::resModeChanged, this, [this](int mode) {
		QSignalBlocker b(m_res_combo); m_res_combo->setCurrentIndex(mode); });
	connect(m_canvas, &RenderCanvas::pyramidVolumeShown, this, [this](bool shown) {
		m_res_label->setVisible(shown); m_res_combo->setVisible(shown); });

	h->addSpacing(10);
	h->addWidget(Label(tr("V. AOV:"), w));
	m_aov_sldr = new QSlider(Qt::Horizontal, w);
	m_aov_sldr->setRange(10, 100);
	m_aov_sldr->setValue(10);
	m_aov_sldr->setFixedWidth(120);
	m_aov_text = Edit(60, QString(), w);
	h->addWidget(m_aov_sldr);
	h->addWidget(m_aov_text);
	connect(m_aov_sldr, &QSlider::valueChanged, this, &ViewPanel::OnAov);
	connect(m_aov_text, &QLineEdit::editingFinished, this, &ViewPanel::OnAovText);
	h->addSpacing(5);
	m_free_chk = new QCheckBox(tr("FreeFly"), w);
	h->addWidget(m_free_chk);
	connect(m_free_chk, &QCheckBox::toggled, this, &ViewPanel::OnFreeChk);
	h->addStretch(1);
	return w;
}

QWidget* ViewPanel::BuildDepthColumn()
{
	auto* w = new QWidget(this);
	auto* v = new QVBoxLayout(w);
	v->setContentsMargins(5, 10, 5, 0);
	v->setSpacing(2);
	auto* st = Label(tr("Depth\nInt:"), w);
	st->setAlignment(Qt::AlignCenter);
	v->addWidget(st, 0, Qt::AlignHCenter);
	// wx m_depth_atten_chk / m_depth_atten_factor_sldr / _text / _reset_btn
	m_depth_chk = new QCheckBox(w);
	v->addWidget(m_depth_chk, 0, Qt::AlignHCenter);
	m_depth_sldr = new QSlider(Qt::Vertical, w);
	m_depth_sldr->setRange(0, 100);
	v->addWidget(m_depth_sldr, 1, Qt::AlignHCenter);
	auto* reset = Button(tr("Reset\nDepth\nInt"), w);
	reset->setFixedWidth(52);
	m_depth_reset_btn = reset;
	v->addWidget(reset, 0, Qt::AlignHCenter);
	m_depth_text = Edit(46, "0.0", w);
	m_depth_text->setValidator(new QDoubleValidator(0.0, 1.0, 2, m_depth_text));
	v->addWidget(m_depth_text);
	SyncFog(m_canvas->GetFog(), m_canvas->GetFogIntensity());
	// OnDepthAttenCheck: SetFog + enable/disable the controls + RefreshGL
	connect(m_depth_chk, &QCheckBox::toggled, this, [this](bool on) {
		if (m_updating) return;
		m_canvas->SetFog(on); });
	// OnDepthAttenFactorChange -> text -> OnDepthAttenFactorEdit -> SetFogIntensity
	connect(m_depth_sldr, &QSlider::valueChanged, this, [this](int x) {
		if (m_updating) return;
		m_depth_text->setText(QString::number(x / 100.0, 'f', 2));
		m_canvas->SetFogIntensity(x / 100.0); });
	connect(m_depth_text, &QLineEdit::editingFinished, this, [this]() {
		if (m_updating) return;
		m_canvas->SetFogIntensity(m_depth_text->text().toDouble()); });
	// OnDepthAttenReset: the default-view value when one was loaded, else 0.0
	connect(reset, &QPushButton::clicked, this, [this]() {
		const double v = m_use_dft_settings ? m_dft_depth_atten_factor : 0.0;
		m_depth_text->setText(QString::number(v, 'f', 2));
		m_canvas->SetFogIntensity(v); });
	connect(m_canvas, &RenderCanvas::fogChanged, this, &ViewPanel::SyncFog);
	w->setFixedWidth(62);
	return w;
}

QWidget* ViewPanel::BuildZoomColumn()
{
	auto* w = new QWidget(this);
	auto* v = new QVBoxLayout(w);
	v->setContentsMargins(5, 10, 5, 0);
	v->setSpacing(2);
	auto* st = Label(tr("Zoom:"), w);
	v->addWidget(st, 0, Qt::AlignHCenter);
	auto* one = Button(tr("1:1"), w);
	one->setFixedWidth(52);
	// wx OnScale121 -> SetScale121 (one data pixel per screen pixel)
	// (+ m_mouse_focus: the canvas takes the focus back)
	connect(one, &QPushButton::clicked, this, [this]() { m_canvas->SetScale121(); m_canvas->TakeFocusIfMouseFocus(); });
	v->addWidget(one, 0, Qt::AlignHCenter);
	m_zoom_sldr = new QSlider(Qt::Vertical, w);
	m_zoom_sldr->setRange(50, 999);
	m_zoom_sldr->setValue(100);
	v->addWidget(m_zoom_sldr, 1, Qt::AlignHCenter);
	// wx m_scale_factor_spin
	v->addWidget(SpinButtons(Qt::Vertical, w, [this](int d) { OnZoomSpin(d); }), 0, Qt::AlignHCenter);
	auto* reset = Button(tr("Reset\nZoom"), w);
	reset->setFixedWidth(52);
	// wx OnScaleReset: the default zoom when default view settings were loaded
	connect(reset, &QPushButton::clicked, this, [this]() {
		const double v = m_use_dft_settings ? double(int(m_dft_scale_factor)) : 100.0;
		m_zoom_text->setText(QString::number(int(v)));
		OnZoomText();
		m_canvas->TakeFocusIfMouseFocus(); });
	v->addWidget(reset, 0, Qt::AlignHCenter);
	m_zoom_text = Edit(46, "100", w);
	v->addWidget(m_zoom_text);
	connect(m_zoom_sldr, &QSlider::valueChanged, this, &ViewPanel::OnZoomSlider);
	connect(m_zoom_text, &QLineEdit::editingFinished, this, &ViewPanel::OnZoomText);
	w->setFixedWidth(62);
	return w;
}

QWidget* ViewPanel::BuildBottomBar()
{
	auto* w = new QWidget(this);
	auto* h = new QHBoxLayout(w);
	h->setContentsMargins(5, 3, 5, 3);
	h->setSpacing(5);

	m_rot_link_chk = new QCheckBox(tr("Link"), w);
	h->addWidget(m_rot_link_chk);
	// wx OnRotLink: link the rotations of all render views
	connect(m_rot_link_chk, &QCheckBox::toggled, this, [this](bool on) { m_canvas->LinkAllRotations(on); });
	auto* reset_rot = Button(tr("Reset Rot."), w);
	// wx OnRotReset: rotations 0, InitView(INIT_BOUNDS|INIT_CENTER|INIT_ROTATE)
	connect(reset_rot, &QPushButton::clicked, this, [this]() {
		m_canvas->SetRotations(0, 0, 0);
		m_canvas->RecomputeBounds();
		m_canvas->TakeFocusIfMouseFocus(); });
	h->addWidget(reset_rot);
	h->addSpacing(5);

	const char* names[3] = { "X:", "Y:", "Z:" };
	for (int i = 0; i < 3; ++i)
	{
		h->addWidget(Label(tr(names[i]), w));
		m_rot_sldr[i] = new QSlider(Qt::Horizontal, w);
		m_rot_sldr[i]->setRange(0, 360);
		h->addWidget(m_rot_sldr[i], 1);
		m_rot_text[i] = Edit(60, "0.0", w);
		auto* val = new QDoubleValidator(0.0, 360.0, 1, m_rot_text[i]);
		m_rot_text[i]->setValidator(val);
		h->addWidget(m_rot_text[i]);
		// wx m_x_rot_spin / m_y_rot_spin / m_z_rot_spin
		h->addWidget(SpinButtons(Qt::Horizontal, w, [this, i](int d) { OnRotSpin(i, d); }));
		h->addSpacing(5);
		connect(m_rot_sldr[i], &QSlider::valueChanged, this, [this, i](int x) { OnRotSlider(i, x); });
		connect(m_rot_text[i], &QLineEdit::editingFinished, this, [this, i]() { OnRotText(i); });
	}

	m_rot_lock_chk = new QCheckBox(tr("45 Increments"), w);
	h->addWidget(m_rot_lock_chk);
	connect(m_rot_lock_chk, &QCheckBox::toggled, this, &ViewPanel::OnRotLock);
	auto* reset_tr = Button(tr("Reset Transl."), w);
	// wx m_center_btn -> OnCenter -> SetCenter
	connect(reset_tr, &QPushButton::clicked, this, [this]() { m_canvas->SetCenter(); });
	h->addWidget(reset_tr);
	auto* save = Button(tr("Save as Default"), w);
	connect(save, &QPushButton::clicked, this, &ViewPanel::SaveDefaultSettings);
	h->addWidget(save);
	return w;
}

void ViewPanel::SetCurrentVolume(VolumeData* vd)
{
	m_vd = vd;
	m_canvas->SetCurrentVolume(vd);
}

// Center / Info. / Legend (wx OnCamCtrCheck, OnFpsCheck, OnLegendCheck,
// OnLegendButton)
void ViewPanel::SetOverlays(ViewOverlays* ov)
{
	if (m_overlays == ov) { SyncOverlayChecks(); return; }
	if (m_overlays) disconnect(m_overlays, nullptr, this, nullptr);
	m_overlays = ov;
	if (ov)
	{
		// view defaults read before the overlays existed (LoadDefaultSettings)
		if (m_pending_overlay.has_ctr) ov->SetDrawCamCtr(m_pending_overlay.ctr);
		if (m_pending_overlay.has_ctr_size) ov->SetCamCtrSize(m_pending_overlay.ctr_size);
		if (m_pending_overlay.has_fps) { ov->SetDrawInfo(m_pending_overlay.fps); ov->SetDrawCoord(m_pending_overlay.fps); }
		if (m_pending_overlay.has_legend) ov->SetDrawLegend(true);   // wx: m_draw_legend = true
		m_pending_overlay = PendingOverlay();
	}
	m_center_chk->setEnabled(ov != nullptr);
	m_info_chk->setEnabled(ov != nullptr);
	if (!ov) { m_legend_btn->setEnabled(false); return; }
	SyncOverlayChecks();
	if (m_overlay_conns) return;
	m_overlay_conns = true;
	connect(m_center_chk, &QCheckBox::toggled, this, [this](bool on) {
		if (m_overlays) m_overlays->SetDrawCamCtr(on); });
	connect(m_info_chk, &QCheckBox::toggled, this, [this](bool on) {
		// wx OnFpsCheck: m_draw_info only (m_draw_coord comes from the defaults)
		if (m_overlays) m_overlays->SetDrawInfo(on); });
	connect(m_legend_chk, &QCheckBox::toggled, this, [this](bool on) {
		if (!on && m_legend_menu) m_legend_menu->hide();
		m_legend_btn->setEnabled(on);
		if (m_overlays) m_overlays->SetDrawLegend(on); });
	// the Legend button opens m_legend_menu (QPushButton::setMenu), which
	// reloads its rows in aboutToShow
}

void ViewPanel::SyncOverlayChecks()
{
	if (!m_overlays) return;
	QSignalBlocker b1(m_center_chk), b2(m_info_chk), b3(m_legend_chk);
	m_center_chk->setChecked(m_overlays->GetDrawCamCtr());
	m_info_chk->setChecked(m_overlays->GetDrawInfo());
	m_legend_chk->setChecked(m_overlays->GetDrawLegend());
	m_legend_btn->setEnabled(m_overlays->GetDrawLegend());
}

void ViewPanel::ReloadLegend()
{
	if (m_legend_menu && m_legend_menu->isVisible()) m_legend_menu->Reload();
}

// ---------------------------------------------------------------------------

void ViewPanel::OnBackground()
{
	FLIVR::Color c = m_canvas->GetBackgroundColor();
	QColor chosen = QColorDialog::getColor(QColor::fromRgbF(c.r(), c.g(), c.b()), this, tr("Background color"));
	if (!chosen.isValid()) return;
	m_canvas->SetBackgroundColor(FLIVR::Color(chosen.redF(), chosen.greenF(), chosen.blueF()));
	m_bg_btn->setStyleSheet(QString("background-color: %1; border: 1px solid #888;").arg(chosen.name()));
}

void ViewPanel::SetAovText(int v)
{
	m_aov_text->setText(v <= 10 ? tr("Ortho") : QString::number(v));
}

// wx OnAovChange -> m_aov_text -> OnAovText
void ViewPanel::OnAov(int v)
{
	SetAovText(v);
	if (m_updating) return;
	if (v <= 10)
		m_canvas->SetPersp(false);
	else
	{
		m_canvas->SetAov(v);
		m_canvas->SetPersp(true);
	}
}

// wx OnAovText
void ViewPanel::OnAovText()
{
	const QString str = m_aov_text->text().trimmed();
	if (str == "Ortho")
	{
		m_canvas->SetPersp(false);
		QSignalBlocker b(m_aov_sldr);
		m_aov_sldr->setValue(10);
		return;
	}
	bool ok = false;
	long val = str.toLong(&ok);
	if (!ok)
		return;
	QSignalBlocker b(m_aov_sldr);
	if (val == 0 || val == 10)
	{
		m_canvas->SetPersp(false);
		m_aov_text->setText(tr("Ortho"));
		m_aov_sldr->setValue(10);
	}
	else if (val < 10)
		return;
	else
	{
		if (val > 100)
		{
			val = 100;
			m_aov_text->setText("100");
		}
		m_canvas->SetAov(double(val));
		m_canvas->SetPersp(true);
		m_aov_sldr->setValue(int(val));
	}
}

// wx OnFreeChk
void ViewPanel::OnFreeChk(bool on)
{
	if (on)
	{
		m_canvas->SetFree(true);
		// wx SetFree: m_aov_text / m_aov_sldr show the perspective angle
		const int aov = int(m_canvas->GetAovValue());
		QSignalBlocker b(m_aov_sldr);
		m_aov_sldr->setValue(aov);
		SetAovText(aov);
	}
	else
	{
		m_canvas->SetFree(false);
		m_canvas->SetPersp(m_aov_sldr->value() != 10);
	}
}

// wx OnRotLockCheck: sliders run 0..8 (45 degree steps) while locked
void ViewPanel::OnRotLock(bool on)
{
	m_canvas->SetRotLock(on);
	double r[3];
	m_canvas->GetRotations(r[0], r[1], r[2]);
	m_updating = true;
	for (int i = 0; i < 3; ++i)
	{
		QSignalBlocker b(m_rot_sldr[i]);
		m_rot_sldr[i]->setRange(0, on ? 8 : 360);
		m_rot_sldr[i]->setValue(on ? int(r[i] / 45.0) : int(r[i]));
	}
	m_updating = false;
}

// wx OnXRotScroll: slider -> text ("%.1f", x45 while locked) -> OnValueEdit
void ViewPanel::OnRotSlider(int axis, int v)
{
	if (m_updating) return;
	const double dval = m_canvas->GetRotLock() ? v * 45.0 : v;
	m_rot_text[axis]->setText(QString::asprintf("%.1f", dval));
	UpdateView();
}

// wx OnValueEdit
void ViewPanel::OnRotText(int axis)
{
	(void)axis;
	if (m_updating) return;
	UpdateView();
}

// wx VRenderView::UpdateView: SetRotations(360 - x, y, z) from the texts
void ViewPanel::UpdateView()
{
	const double rx = m_rot_text[0]->text().toDouble();
	const double ry = m_rot_text[1]->text().toDouble();
	const double rz = m_rot_text[2]->text().toDouble();
	m_canvas->SetRotations(360.0 - rx, ry, rz);
}

// wx On{X,Y,Z}RotSpinUp (-1) / SpinDown (+1), integer degrees, wrapped
void ViewPanel::OnRotSpin(int axis, int delta)
{
	int value = int(m_rot_text[axis]->text().toDouble()) - delta;
	if (delta > 0) value = value < 0 ? value + 360 : value;
	else value = value > 360 ? value - 360 : value;
	m_rot_text[axis]->setText(QString::asprintf("%.1f", double(value)));
	UpdateView();
}

// wx OnScaleFactorChange: slider -> text -> OnScaleFactorEdit
void ViewPanel::OnZoomSlider(int v)
{
	if (m_updating) return;
	m_zoom_text->setText(QString::number(v));
	OnZoomText();
}

// wx OnScaleFactorEdit
void ViewPanel::OnZoomText()
{
	if (m_updating) return;
	bool ok = false;
	long val = m_zoom_text->text().trimmed().toLong(&ok);
	if (!ok || val <= 0) return;
	{ QSignalBlocker b(m_zoom_sldr); m_zoom_sldr->setValue(int(val)); }
	m_canvas->SetScaleFactor(val / 100.0);
	m_canvas->RefreshInteractive();   // wx RefreshGL(true)
}

// wx OnScaleFactorSpinUp / SpinDown
void ViewPanel::OnZoomSpin(int delta)
{
	long val = m_zoom_text->text().trimmed().toLong();
	if (delta > 0) val++;
	else if (val > 1) val--;
	m_zoom_text->setText(QString::number(val));
	OnZoomText();
}

void ViewPanel::OnCanvasRotation(double rx, double ry, double rz)
{
	m_updating = true;
	const double r[3] = { 360.0 - rx, ry, rz };
	for (int i = 0; i < 3; ++i)
	{
		double d = r[i];
		if (d > 360.0) d -= 360.0;
		if (d < 0.0) d += 360.0;
		QSignalBlocker b(m_rot_sldr[i]);
		// wx SetRotations: int() of the angle, /45 while "45 Increments" is on
		m_rot_sldr[i]->setValue(m_canvas->GetRotLock() ? int(d / 45.0) : int(d));
		m_rot_text[i]->setText(QString::asprintf("%.1f", d));
	}
	m_updating = false;
}

void ViewPanel::OnCanvasZoom(double s)
{
	m_updating = true;
	{ QSignalBlocker b(m_zoom_sldr); m_zoom_sldr->setValue(int(std::lround(s * 100.0))); }
	m_zoom_text->setText(QString::number(int(std::lround(s * 100.0))));
	m_updating = false;
}

// ---------------------------------------------------------------------------
// canvas -> controls (wx SetVolMethod / SetFog / LoadSettings update the bar)

void ViewPanel::SyncVolMethod(int method)
{
	m_updating = true;
	m_layered_rd->setChecked(method == RenderCanvas::VolMethodSeq);
	m_depth_rd->setChecked(method == RenderCanvas::VolMethodMulti);
	m_comp_rd->setChecked(method == RenderCanvas::VolMethodComp);
	m_updating = false;
}

void ViewPanel::SyncFog(bool on, double intensity)
{
	m_updating = true;
	m_depth_chk->setChecked(on);
	m_depth_sldr->setValue(int(std::lround(intensity * 100.0)));
	m_depth_text->setText(QString::number(intensity, 'f', 2));
	m_depth_sldr->setEnabled(on);
	m_depth_text->setEnabled(on);
	m_depth_reset_btn->setEnabled(on);
	m_updating = false;
}

// ---------------------------------------------------------------------------
// view defaults (wx VRenderView::SaveDefault / LoadSettings)
// ---------------------------------------------------------------------------

void ViewPanel::Capture(const QString& file, const CaptureDialog::Options& opt)
{
	CaptureDialog::Execute(this, m_canvas, m_capture_hooks, file, opt);
	if (m_canvas->IsCapturing() && opt.group_each) m_capture_btn->setEnabled(false);
}

void ViewPanel::SaveDefaultSettings()
{
	// keys the Qt pane cannot produce (search_chk, min_dpi, ...)
	// keep their saved value
	vvd::IniFile fconfig;
	const VVDString dft = ViewDefaultFile(true);
	if (vvd::FileExists(dft))
		fconfig.Load(dft);

	//render modes
	fconfig.Write(L"volume_seq_rd", m_layered_rd->isChecked());
	fconfig.Write(L"volume_multi_rd", m_depth_rd->isChecked());
	fconfig.Write(L"volume_comp_rd", m_comp_rd->isChecked());
	//background color
	FLIVR::Color bg = m_canvas->GetBackgroundColor();
	QColor c = QColor::fromRgbF(std::clamp(bg.r(), 0.0, 1.0), std::clamp(bg.g(), 0.0, 1.0), std::clamp(bg.b(), 0.0, 1.0));
	fconfig.Write(L"bg_color_picker", VVDString(QString::asprintf("%d %d %d", c.red(), c.green(), c.blue())));
	//camera center, info, legend
	fconfig.Write(L"cam_ctr_chk", m_center_chk->isChecked());
	if (m_overlays)
		fconfig.Write(L"camctr_size", m_overlays->GetCamCtrSize());
	else if (!fconfig.Has(L"camctr_size"))
		fconfig.Write(L"camctr_size", 2.0);
	fconfig.Write(L"fps_chk", m_info_chk->isChecked());
	fconfig.Write(L"intp_chk", m_intp_chk->isChecked());
	fconfig.Write(L"legend_chk", m_overlays ? m_overlays->GetDrawLegend() : m_legend_chk->isChecked());
	fconfig.Write(L"mouse_focus", m_canvas->GetMouseFocus());
	if (!fconfig.Has(L"search_chk")) fconfig.Write(L"search_chk", false);
	//ortho/persp
	fconfig.Write(L"persp", m_canvas->GetPerspective());
	fconfig.Write(L"aov", m_canvas->GetAovValue());
	fconfig.Write(L"free_rd", m_free_chk->isChecked());
	//min dpi (no PPI control in the Qt pane)
	if (!fconfig.Has(L"min_dpi")) fconfig.Write(L"min_dpi", 20.0);
	fconfig.Write(L"res_mode", m_canvas->GetResMode());
	//rotations
	fconfig.Write(L"x_rot", VVDString(m_rot_text[0]->text()));
	fconfig.Write(L"y_rot", VVDString(m_rot_text[1]->text()));
	fconfig.Write(L"z_rot", VVDString(m_rot_text[2]->text()));
	fconfig.Write(L"rot_lock", m_canvas->GetRotLock());
	//depth atten
	fconfig.Write(L"depth_atten_chk", m_depth_chk->isChecked());
	fconfig.Write(L"depth_atten_factor_text", VVDString(m_depth_text->text()));
	m_dft_depth_atten_factor = m_depth_text->text().toDouble();
	//scale factor
	fconfig.Write(L"scale_factor_text", VVDString(m_zoom_text->text()));
	m_dft_scale_factor = m_zoom_text->text().toDouble();
	//camera center
	double x, y, z;
	m_canvas->GetCenters(x, y, z);
	fconfig.Write(L"center", VVDString(QString::asprintf("%f %f %f", x, y, z)));

	fconfig.Save(dft);
}

void ViewPanel::LoadDefaultSettings()
{
	const VVDString dft = ViewDefaultFile(false);
	vvd::IniFile fconfig;
	if (!vvd::FileExists(dft) || !fconfig.Load(dft))
	{
		UpdateView();
		return;
	}

	bool bVal;
	double dVal;
	int iVal;
	VVDString str;
	if (fconfig.Read(L"volume_seq_rd", &bVal) && bVal)
		m_canvas->SetVolMethod(RenderCanvas::VolMethodSeq);
	if (fconfig.Read(L"volume_multi_rd", &bVal) && bVal)
		m_canvas->SetVolMethod(RenderCanvas::VolMethodMulti);
	if (fconfig.Read(L"volume_comp_rd", &bVal) && bVal)
		m_canvas->SetVolMethod(RenderCanvas::VolMethodComp);
	if (fconfig.Read(L"bg_color_picker", &str))
	{
		int r = 0, g = 0, b = 0;
		const QStringList parts = str.ToQt().split(' ', Qt::SkipEmptyParts);
		if (parts.size() >= 3) { r = parts[0].toInt(); g = parts[1].toInt(); b = parts[2].toInt(); }
		m_canvas->SetBackgroundColor(FLIVR::Color(r / 255.0, g / 255.0, b / 255.0));
		m_bg_btn->setStyleSheet(QString("background-color: %1; border: 1px solid #888;").arg(QColor(r, g, b).name()));
	}
	if (fconfig.Read(L"cam_ctr_chk", &bVal))
	{
		m_pending_overlay.has_ctr = true;
		m_pending_overlay.ctr = bVal;
	}
	if (fconfig.Read(L"camctr_size", &dVal))
	{
		m_pending_overlay.has_ctr_size = true;
		m_pending_overlay.ctr_size = dVal;
	}
	if (fconfig.Read(L"fps_chk", &bVal))
	{
		m_pending_overlay.has_fps = true;
		m_pending_overlay.fps = bVal;
	}
	if (fconfig.Read(L"intp_chk", &bVal))
		m_intp_chk->setChecked(bVal);
	if (fconfig.Read(L"legend_chk", &bVal))
	{
		m_pending_overlay.has_legend = true;
		m_pending_overlay.legend = bVal;
	}
	// wx: m_glview->m_mouse_focus (no control; only in the defaults file)
	if (fconfig.Read(L"mouse_focus", &bVal))
		m_canvas->SetMouseFocus(bVal);
	bool persp = m_canvas->GetPerspective();
	if (fconfig.Read(L"persp", &bVal))
		persp = bVal;
	double aov = m_canvas->GetAovValue();
	fconfig.Read(L"aov", &aov);
	{
		// wx SetPersp(persp); SetAov(aov): the slider shows the angle only in
		// perspective
		m_canvas->SetAov(aov);
		m_canvas->SetPersp(persp);
		QSignalBlocker b(m_aov_sldr);
		const int v = persp ? int(aov) : 10;
		m_aov_sldr->setValue(v);
		SetAovText(v);
	}
	if (fconfig.Read(L"res_mode", &iVal))
		m_canvas->SetResMode(iVal);
	else
		m_canvas->SetResMode(0);
	if (fconfig.Read(L"free_rd", &bVal) && bVal)
		m_free_chk->setChecked(true);   // -> OnFreeChk -> SetFree(true)
	const wchar_t* rot_keys[3] = { L"x_rot", L"y_rot", L"z_rot" };
	for (int i = 0; i < 3; ++i)
		if (fconfig.Read(rot_keys[i], &str))
		{
			m_rot_text[i]->setText(str.ToQt());
			m_dft_rot[i] = str.ToQt().toDouble();
		}
	if (fconfig.Read(L"rot_lock", &bVal))
		m_rot_lock_chk->setChecked(bVal);   // -> OnRotLock
	UpdateView();  //for rotations
	if (fconfig.Read(L"scale_factor_text", &str))
	{
		dVal = str.ToQt().toDouble();
		if (dVal <= 1.0)
			dVal = 100.0;
		m_zoom_text->setText(QString::number(int(dVal)));
		{ QSignalBlocker b(m_zoom_sldr); m_zoom_sldr->setValue(int(dVal)); }
		m_canvas->SetScaleFactor(dVal / 100.0);
		m_dft_scale_factor = dVal;
	}
	if (fconfig.Read(L"depth_atten_chk", &bVal))
		m_canvas->SetFog(bVal);   // fogChanged -> SyncFog enables the column
	if (fconfig.Read(L"depth_atten_factor_text", &str))
	{
		dVal = str.ToQt().toDouble();
		m_canvas->SetFogIntensity(dVal);
		m_dft_depth_atten_factor = dVal;
	}
	if (fconfig.Read(L"center", &str))
	{
		const QStringList parts = str.ToQt().split(' ', Qt::SkipEmptyParts);
		if (parts.size() >= 3)
			m_canvas->SetCenters(parts[0].toDouble(), parts[1].toDouble(), parts[2].toDouble());
	}

	m_use_dft_settings = true;
	m_canvas->SetUseDefaultSettings(true);
	m_canvas->RefreshView();
}
