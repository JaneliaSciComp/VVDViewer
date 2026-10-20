#include "VolumePropPanel.h"

#include <QGridLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QEvent>
#include <QRegularExpression>
#include <QComboBox>
#include <QColorDialog>
#include <QSignalBlocker>
#include <QDoubleValidator>
#include <algorithm>
#include <cmath>

#include "DataManager.h"
#include "FLIVR/VolumeRenderer.h"
#include "ViewOverlays.h"
#include "RenderCanvas.h"
#include "TestRun.h"
#include "Core/FileUtil.h"
#include <QMessageBox>
#include <QFileInfo>
#include <QCoreApplication>

using FLIVR::Color;

// ---------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------

VolumePropPanel::SliderRow VolumePropPanel::MakeRow(const QString& name,
	int smin, int smax, double scale, int decimals, double vmin, double vmax)
{
	SliderRow r;
	r.scale = scale;
	r.slider = new QSlider(Qt::Horizontal, this);
	r.slider->setRange(smin, smax);
	r.slider->setMinimumWidth(60);
	r.spin = new QDoubleSpinBox(this);
	r.spin->setDecimals(decimals);
	r.spin->setRange(vmin, vmax);
	r.spin->setSingleStep(std::pow(10.0, -decimals));
	r.spin->setKeyboardTracking(false);
	r.spin->setButtonSymbols(QAbstractSpinBox::NoButtons);   // wx uses plain text boxes
	r.spin->setFixedWidth(decimals > 2 ? 62 : 52);
	r.label = new QLabel(name, this);
	return r;
}

void VolumePropPanel::Bind(SliderRow& r, void (VolumePropPanel::*apply)(double))
{
	connect(r.slider, &QSlider::valueChanged, this, [this, &r, apply](int iv)
	{
		if (m_updating) return;
		double v = iv / r.scale;
		{ QSignalBlocker b(r.spin); r.spin->setValue(v); }
		(this->*apply)(v);
	});
	connect(r.spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, &r, apply](double v)
	{
		if (m_updating) return;
		{ QSignalBlocker b(r.slider); r.slider->setValue(int(v * r.scale + (v >= 0 ? 0.5 : -0.5))); }
		(this->*apply)(v);
	});
}

void VolumePropPanel::SetRow(SliderRow& r, double value)
{
	QSignalBlocker b1(r.slider), b2(r.spin);
	r.spin->setValue(value);
	r.slider->setValue(int(value * r.scale + (value >= 0 ? 0.5 : -0.5)));
}

VolumePropPanel::VolumePropPanel(QWidget* parent)
	: QWidget(parent)
{
	setStyleSheet("QWidget { font-size: 11px; } QLineEdit, QComboBox, QPushButton { min-height: 18px; }");

	// ---- sliders (same set as VPropView; the hidden ones keep the appliers uniform)
	// The text boxes accept the wx ranges (VPropView), wider than the
	// sliders: a value set in wx or loaded from a project must not be
	// clamped here, then written back by Sync / Save as Default.
	m_gamma       = MakeRow(":Gamma",             10, 400, 100.0, 2, 0.0, 10.0);
	m_boundary    = MakeRow("Extract Boundary:",   0, 1000, 2000.0, 4, 0.0, 1.0);
	m_saturation  = MakeRow(":Saturation Point",   0, 255, 1.0, 0, 0.0, 65535.0);
	m_left_thresh = MakeRow("Threshold:",          0, 255, 1.0, 0, 0.0, 65535.0);
	m_right_thresh= MakeRow(QString(),             0, 255, 1.0, 0, 0.0, 65535.0);
	m_luminance   = MakeRow(":Luminance",          0, 255, 1.0, 0, 0.0, 65535.0);
	m_alpha       = MakeRow(":",                   0, 255, 1.0, 0, 0.0, 65535.0);
	m_low_shading = MakeRow(":",                   0, 200, 100.0, 2, 0.0, 10.0);
	m_shadow      = MakeRow(QString(),             0, 100, 100.0, 2, 0.0, 100.0);
	m_sample_rate = MakeRow("Sample Rate:",        0, 100, 100.0, 2, 0.0, 100.0);
	m_colormap_low  = MakeRow(QString(),           0, 255, 1.0, 0, 0.0, 65535.0);
	m_colormap_high = MakeRow("High (R)",          0, 255, 1.0, 0, 0.0, 65535.0);
	m_hi_shading  = MakeRow(QString(),             0, 100, 10.0, 2, 0.0, 10.0);   // hidden in the wx UI too
	for (QWidget* w : { (QWidget*)m_hi_shading.slider, (QWidget*)m_hi_shading.spin, (QWidget*)m_hi_shading.label })
		w->hide();

	Bind(m_gamma, &VolumePropPanel::ApplyGamma);
	Bind(m_boundary, &VolumePropPanel::ApplyBoundary);
	Bind(m_saturation, &VolumePropPanel::ApplySaturation);
	Bind(m_left_thresh, &VolumePropPanel::ApplyLeftThresh);
	Bind(m_luminance, &VolumePropPanel::ApplyLuminance);
	Bind(m_right_thresh, &VolumePropPanel::ApplyRightThresh);
	Bind(m_alpha, &VolumePropPanel::ApplyAlpha);
	Bind(m_hi_shading, &VolumePropPanel::ApplyHiShading);
	Bind(m_low_shading, &VolumePropPanel::ApplyLowShading);
	Bind(m_sample_rate, &VolumePropPanel::ApplySampleRate);
	Bind(m_colormap_low, &VolumePropPanel::ApplyColormapLow);
	Bind(m_colormap_high, &VolumePropPanel::ApplyColormapHigh);
	Bind(m_shadow, &VolumePropPanel::ApplyShadow);

	m_alpha_chk = new QCheckBox(tr("Alpha"), this);
	m_shading_chk = new QCheckBox(tr("Shading"), this);
	m_shadow_chk = new QCheckBox(tr("Shadow / Light:"), this);
	m_inv_chk = new QCheckBox(tr(":Inv"), this);
	m_mip_chk = new QCheckBox(tr(":MIP"), this);
	m_nr_chk = new QCheckBox(tr(":Smoothing"), this);
	m_colormap_chk = new QCheckBox(tr("Colormap: Low (B)"), this);
	// not shown in the wx pane (they live in the view bar / legend dialog)
	m_interp_chk = new QCheckBox(this);
	m_legend_chk = new QCheckBox(this);
	m_interp_chk->hide();
	m_legend_chk->hide();

	auto text_label = [this](const QString& t) { return new QLabel(t, this); };
	// slider row builders -------------------------------------------------
	auto left_row = [this](SliderRow& r, QWidget* tail_label, QWidget* tail_chk) {
		auto* h = new QHBoxLayout();
		h->setSpacing(4);
		h->addWidget(r.slider, 1);
		h->addWidget(r.spin);
		if (tail_label) h->addWidget(tail_label);
		if (tail_chk) h->addWidget(tail_chk);
		return h;
	};

	auto* left = new QVBoxLayout();
	left->setSpacing(2);
	left->addLayout(left_row(m_gamma, m_gamma.label, nullptr));
	left->addLayout(left_row(m_saturation, m_saturation.label, nullptr));
	left->addLayout(left_row(m_luminance, m_luminance.label, nullptr));
	left->addLayout(left_row(m_alpha, m_alpha.label, m_alpha_chk));
	left->addLayout(left_row(m_low_shading, m_low_shading.label, m_shading_chk));
	left->addStretch(1);

	auto* right = new QVBoxLayout();
	right->setSpacing(2);
	auto r1 = new QHBoxLayout();
	r1->setSpacing(4);
	r1->addWidget(m_boundary.label); r1->addWidget(m_boundary.spin); r1->addWidget(m_boundary.slider, 1);
	auto r2 = new QHBoxLayout();
	r2->setSpacing(4);
	r2->addWidget(m_left_thresh.label); r2->addWidget(m_left_thresh.spin); r2->addWidget(m_left_thresh.slider, 1);
	r2->addWidget(m_right_thresh.spin); r2->addWidget(m_right_thresh.slider, 1);
	auto r3 = new QHBoxLayout();
	r3->setSpacing(4);
	r3->addSpacing(10); r3->addWidget(m_shadow_chk); r3->addWidget(m_shadow.spin); r3->addWidget(m_shadow.slider, 1);
	auto r4 = new QHBoxLayout();
	r4->setSpacing(4);
	r4->addWidget(m_sample_rate.label); r4->addWidget(m_sample_rate.spin); r4->addWidget(m_sample_rate.slider, 1);
	m_cmap_row = new QWidget(this);
	auto r5 = new QHBoxLayout(m_cmap_row);
	r5->setContentsMargins(0, 0, 0, 0);
	r5->setSpacing(4);
	r5->addSpacing(10); r5->addWidget(m_colormap_chk); r5->addWidget(m_colormap_low.spin); r5->addWidget(m_colormap_low.slider, 1);
	r5->addWidget(m_colormap_high.spin); r5->addWidget(m_colormap_high.slider, 1); r5->addWidget(m_colormap_high.label);
	// wx m_sizer_r6: "Segment:" name / colour / display mode (index colour)
	m_roi_row = new QWidget(this);
	auto r6 = new QHBoxLayout(m_roi_row);
	r6->setContentsMargins(0, 0, 0, 0);
	r6->setSpacing(4);
	m_roi_st = new QLabel(tr("Segment:"), m_roi_row);
	m_roi_st->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	m_roi_st->setMinimumWidth(100);
	m_roi_text = new QLineEdit(m_roi_row);
	m_roi_text->setObjectName("ROINameText");
	m_roi_text->setFixedWidth(150);
	m_roi_color_btn = new QPushButton(m_roi_row);
	m_roi_color_btn->setFixedWidth(40);
	m_roi_color_btn->setStyleSheet("background-color: #ff0000;");
	m_roi_disp_mode_combo = new QComboBox(m_roi_row);
	m_roi_disp_mode_combo->addItems({ "Highlight/Dark", "Highlight/DarkGray", "Highlight/Invisible" });
	m_roi_disp_mode_combo->setCurrentIndex(0);
	r6->addWidget(m_roi_st);
	r6->addWidget(m_roi_text);
	r6->addSpacing(10);
	r6->addWidget(m_roi_color_btn);
	r6->addSpacing(10);
	r6->addWidget(m_roi_disp_mode_combo);
	r6->addStretch(1);
	m_roi_row->hide();
	connect(m_roi_text, &QLineEdit::returnPressed, this, [this]() { Q_EMIT roiNameEntered(m_roi_text->text()); });
	connect(m_roi_color_btn, &QPushButton::clicked, this, &VolumePropPanel::OnROIColorBtn);
	connect(m_roi_disp_mode_combo, QOverload<int>::of(&QComboBox::activated), this, [this](int mode) {
		// wx OnROIDispModesCombo
		if (!m_vd) return;
		m_vd->SetIDColDispMode(mode);
		Q_EMIT volumeChanged();
	});
	for (QLayout* l : { (QLayout*)r1, (QLayout*)r2, (QLayout*)r3, (QLayout*)r4 }) right->addLayout(l);
	right->addWidget(m_cmap_row);
	right->addWidget(m_roi_row);
	right->addStretch(1);

	// separator lines between the parameter groups
	auto vline = [this]() {
		auto* f = new QFrame(this);
		f->setFrameShape(QFrame::VLine);
		f->setFrameShadow(QFrame::Sunken);
		return f;
	};
	auto hline = [this]() {
		auto* f = new QFrame(this);
		f->setFrameShape(QFrame::HLine);
		f->setFrameShadow(QFrame::Sunken);
		return f;
	};
	// left and right parameter columns, with a separator line between them
	auto* sliders = new QHBoxLayout();
	sliders->setSpacing(10);
	sliders->addLayout(left, 4);
	sliders->addWidget(vline());
	sliders->addLayout(right, 5);

	// ---- bottom row: Max Value, Color, spacing, scale bar, mask, group, depth
	auto* bottom = new QHBoxLayout();
	bottom->setSpacing(5);
	bottom->addSpacing(10);
	bottom->addWidget(text_label(tr("Max Value:")));
	m_max_value = new QDoubleSpinBox(this);
	m_max_value->setDecimals(2);
	m_max_value->setRange(0.0, 1e9);
	m_max_value->setKeyboardTracking(false);
	m_max_value->setButtonSymbols(QAbstractSpinBox::NoButtons);
	m_max_value->setFixedWidth(70);
	bottom->addWidget(m_max_value);
	bottom->addSpacing(10);
	bottom->addWidget(text_label(tr("Color:")));
	m_color_text = new QLineEdit("255 , 255 , 255", this);
	m_color_text->setObjectName("ColorText");
	m_color_text->installEventFilter(this);   // double-click selects all (wx OnColorTextFocus)
	m_color_text->setToolTip(tr(
		"Type \"R , G , B\" (0-255) or a colour code:\n"
		"R red, G green, B blue, W white, P purple/magenta, Y yellow,\n"
		"C cyan, M magenta, O orange, K gray, A amber, V violet, S sky blue, L lime\n"
		"or a dye / protein name: DAPI, Hoechst, BFP, CFP, GFP, EGFP, YFP, Venus,\n"
		"FITC, mCherry, RFP, tdTomato, mScarlet, TRITC, Cy3, Cy5, Cy7,\n"
		"Alexa405/488/546/555/568/594/633/647, or a laser line such as 405nm, 488nm, 561nm, 640nm"));
	m_color_text->setFixedWidth(100);
	bottom->addWidget(m_color_text);
	m_color_btn = new QPushButton(this);
	m_color_btn->setFixedSize(30, 20);
	bottom->addWidget(m_color_btn);

	auto make_spc = [this](const QString& name, QHBoxLayout* h) {
		auto* s = new QDoubleSpinBox(this);
		s->setDecimals(6); s->setRange(0.000001, 1e6); s->setKeyboardTracking(false);
		s->setButtonSymbols(QAbstractSpinBox::NoButtons);
		s->setFixedWidth(78);
		h->addSpacing(5);
		h->addWidget(new QLabel(name, this));
		h->addWidget(s);
		return s;
	};
	bottom->addSpacing(5);
	m_spc_x = make_spc(tr("X:"), bottom);
	m_spc_y = make_spc(tr("Y:"), bottom);
	m_spc_z = make_spc(tr("Z:"), bottom);

	// Controls below have no Qt-side backing yet: shown (so the pane matches
	// the wx one) but disabled.
	auto not_ported = [](QWidget* w) { w->setEnabled(false); w->setToolTip(QObject::tr("Not ported to Qt yet")); return w; };
	// scale bar (VPropView: view state, applied to the ViewOverlays)
	bottom->addSpacing(10);
	m_scale_chk = new QCheckBox(tr("SclBar:"), this);
	m_scale_chk->setLayoutDirection(Qt::RightToLeft);   // wxALIGN_RIGHT
	bottom->addWidget(m_scale_chk);
	bottom->addSpacing(5);
	m_scale_te_chk = new QCheckBox(tr("Text:"), this);
	m_scale_te_chk->setLayoutDirection(Qt::RightToLeft);
	bottom->addWidget(m_scale_te_chk);
	bottom->addSpacing(5);
	m_scale_text = new QLineEdit(this);
	m_scale_text->setFixedWidth(50);
	{
		auto* vald = new QDoubleValidator(0.0, 1e12, 3, m_scale_text);   // wx vald_fp3
		vald->setNotation(QDoubleValidator::StandardNotation);
		m_scale_text->setValidator(vald);
	}
	bottom->addWidget(m_scale_text);
	bottom->addSpacing(5);
	m_scale_cmb = new QComboBox(this);
	m_scale_cmb->addItems({ "nm", QString::fromUtf8("\xc2\xb5m"), "mm" });
	m_scale_cmb->setCurrentIndex(1);
	bottom->addWidget(m_scale_cmb);
	bottom->addSpacing(5);
	bottom->addWidget(text_label(tr("Digit:")));
	m_scale_digit_cmb = new QComboBox(this);
	for (int c = 0; c <= 8; c++)
		m_scale_digit_cmb->addItem(QString::asprintf(" %i", c));
	m_scale_digit_cmb->setCurrentIndex(0);
	bottom->addWidget(m_scale_digit_cmb);
	bottom->addSpacing(5);
	m_scale_lenfix_chk = new QCheckBox(tr("Fix length:"), this);
	m_scale_lenfix_chk->setLayoutDirection(Qt::RightToLeft);
	bottom->addWidget(m_scale_lenfix_chk);
	// user actions only (wx ChangeValue / Select / SetValue do not fire)
	connect(m_scale_chk, &QCheckBox::clicked, this, [this](bool) { OnScaleCheck(); });
	connect(m_scale_te_chk, &QCheckBox::clicked, this, [this](bool) { OnScaleTextCheck(); });
	connect(m_scale_text, &QLineEdit::textEdited, this, [this](const QString&) { OnScaleTextEditing(); });
	connect(m_scale_cmb, qOverload<int>(&QComboBox::activated), this, [this](int) { OnScaleUnitSelected(); });
	connect(m_scale_digit_cmb, qOverload<int>(&QComboBox::activated), this, [this](int) { OnScaleDigitSelected(); });
	connect(m_scale_lenfix_chk, &QCheckBox::clicked, this, [this](bool) { OnScaleLenFixCheck(); });
	bottom->addSpacing(10);
	bottom->addWidget(text_label(tr("Mask:")));
	m_mask_out_chk = new QCheckBox(tr("Hide Outside"), this);
	m_mask_in_chk = new QCheckBox(tr("Hide Inside"), this);
	bottom->addWidget(m_mask_out_chk);
	bottom->addWidget(m_mask_in_chk);
	bottom->addStretch(1);
	bottom->addSpacing(10);
	bottom->addWidget(text_label(tr("Group:")));
	m_sync_group_chk = new QCheckBox(tr("Sync"), this);
	m_sync_spc_chk = new QCheckBox(tr("Sync Spacings"), this);
	m_depth_chk = new QCheckBox(tr("Depth"), this);
	bottom->addWidget(m_sync_group_chk);
	bottom->addWidget(m_sync_spc_chk);
	bottom->addWidget(m_depth_chk);
	// user actions only (wx SetValue does not fire the handlers)
	connect(m_mask_out_chk, &QCheckBox::clicked, this, [this](bool) { OnMaskHide(true); });
	connect(m_mask_in_chk, &QCheckBox::clicked, this, [this](bool) { OnMaskHide(false); });
	connect(m_sync_group_chk, &QCheckBox::clicked, this, [this](bool) { OnSyncGroupCheck(); });
	connect(m_sync_spc_chk, &QCheckBox::clicked, this, [this](bool) { OnSyncGroupSpcCheck(); });
	connect(m_depth_chk, &QCheckBox::clicked, this, [this](bool) { OnDepthCheck(); });

	auto* sl_b = new QVBoxLayout();
	sl_b->setSpacing(4);
	sl_b->addLayout(sliders);
	sl_b->addWidget(hline());
	sl_b->addLayout(bottom);

	// ---- right: check boxes and buttons (as in VPropView)
	auto* checks = new QVBoxLayout();
	checks->setSpacing(2);
	checks->addSpacing(4);
	checks->addWidget(m_inv_chk);
	m_idcl_chk = new QCheckBox(tr(":Index"), this);
	checks->addWidget(m_idcl_chk);
	connect(m_idcl_chk, &QCheckBox::clicked, this, [this](bool) { OnIdclCheck(); });
	checks->addWidget(m_mip_chk);
	checks->addWidget(m_nr_chk);
	checks->addStretch(1);
	auto* reset = new QPushButton(tr("Reset"), this);
	auto* save = new QPushButton(tr("Save as Default"), this);
	reset->setFixedWidth(115);
	save->setFixedWidth(115);
	checks->addWidget(reset);
	checks->addWidget(save);

	auto* outer = new QHBoxLayout(this);
	outer->setContentsMargins(6, 4, 6, 4);
	// narrow gaps around the line: the pane must still fit small screens
	outer->setSpacing(6);
	outer->addLayout(sl_b, 1);
	outer->addWidget(vline());
	outer->addLayout(checks, 0);

	connect(m_alpha_chk, &QCheckBox::toggled, this, [this](bool) { if (!m_updating && m_vd) OnAlphaCheck(); });
	connect(m_shading_chk, &QCheckBox::toggled, this, [this](bool) { if (!m_updating && m_vd) OnShadingCheck(); });
	connect(m_shadow_chk, &QCheckBox::toggled, this, [this](bool) { if (!m_updating && m_vd) OnShadowCheck(); });
	connect(m_inv_chk, &QCheckBox::toggled, this, [this](bool on) {
		if (m_updating || !m_vd) return;
		ForTargets([on](VolumeData* vd) { vd->SetInvert(on); }); Q_EMIT volumeChanged(); });
	connect(m_mip_chk, &QCheckBox::toggled, this, [this](bool) { if (!m_updating && m_vd) OnMIPCheck(); });
	connect(m_interp_chk, &QCheckBox::toggled, this, [this](bool on) {
		if (m_updating || !m_vd) return;
		m_vd->SetInterpolate(on); Q_EMIT volumeChanged(); });
	connect(m_nr_chk, &QCheckBox::toggled, this, [this](bool) { if (!m_updating && m_vd) OnNRCheck(); });
	connect(m_colormap_chk, &QCheckBox::toggled, this, [this](bool on) {
		if (m_updating || !m_vd) return;
		// wx OnColormapEnable: the Index box wins (colormode 3)
		const int mode = m_idcl_chk->isChecked() ? 3 : (on ? 1 : 0);
		ForTargets([mode, on](VolumeData* vd) { vd->SetColormapMode(mode); vd->SetColormapDisp(on); });
		// wx OnEnableColormap, MIP mode: the colormap disables gamma /
		// saturation / luminance; off enables gamma / saturation only
		if (m_vd->GetMode() == 1)
		{
			SetRowEnabled(m_gamma, !on);
			SetRowEnabled(m_saturation, !on);
			if (on) SetRowEnabled(m_luminance, false);
		}
		Q_EMIT colormapModeChanged(); Q_EMIT volumeChanged(); });
	connect(m_legend_chk, &QCheckBox::toggled, this, [this](bool on) {
		if (m_updating || !m_vd) return;
		m_vd->SetLegend(on); Q_EMIT volumeChanged(); });
	connect(m_color_btn, &QPushButton::clicked, this, &VolumePropPanel::OnColorButton);
	connect(m_color_text, &QLineEdit::textEdited, this, &VolumePropPanel::OnColorTextEdited);
	connect(m_color_text, &QLineEdit::editingFinished, this, &VolumePropPanel::OnColorTextFinished);
	connect(reset, &QPushButton::clicked, this, &VolumePropPanel::OnResetDefault);
	connect(save, &QPushButton::clicked, this, &VolumePropPanel::OnSaveDefault);
	// wx OnEnterInMaxText -> UpdateMaxValue
	connect(m_max_value, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
		if (m_updating || !m_vd) return;
		UpdateMaxValue(); });
	for (QDoubleSpinBox* s : { m_spc_x, m_spc_y, m_spc_z })
		connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { ApplySpacing(); });

	SetVolume(nullptr);
}

// ---------------------------------------------------------------------------
// volume <-> controls
// ---------------------------------------------------------------------------

void VolumePropPanel::SetVolume(VolumeData* vd)
{
	m_vd = vd;
	setEnabled(vd != nullptr);
	if (vd) GetSettings();
}

void VolumePropPanel::GetSettings()
{
	if (!m_vd) return;
	GetScaleBarSettings();
	m_updating = true;

	// integer data: sliders run 0..max value in raw units; float data: 0..1 * 100
	m_is_float = false;
	if (m_vd->GetTexture() && m_vd->GetTexture()->get_nrrd(0))
	{
		int t = m_vd->GetTexture()->get_nrrd(0)->getNrrdDataType();
		m_is_float = (t == nrrdTypeFloat || t == nrrdTypeDouble);
	}
	m_max_val = m_vd->GetMaxValue();
	m_max_val = m_is_float ? std::max(1.0, m_max_val) : std::max(255.0, m_max_val);
	// wx sldr_max: float data m_max_val * 100 (two decimals), else m_max_val
	int imax = m_is_float ? int(m_max_val * 100 + 0.5) : int(m_max_val);
	double vscale = m_is_float ? 100.0 : 1.0;
	for (SliderRow* r : { &m_saturation, &m_left_thresh, &m_right_thresh, &m_luminance, &m_alpha, &m_colormap_low, &m_colormap_high })
	{
		r->scale = vscale;
		r->slider->setRange(0, imax);
		r->spin->setDecimals(m_is_float ? 2 : 0);
		r->spin->setRange(0.0, m_max_val);
	}
	// wx: the saturation point text accepts up to 10x the maximum
	m_saturation.spin->setRange(0.0, m_max_val * 10.0);

	SetRow(m_gamma, m_vd->Get3DGamma());
	SetRow(m_boundary, m_vd->GetBoundary());
	SetRow(m_saturation, m_vd->GetOffset() * m_max_val);
	SetRow(m_left_thresh, m_vd->GetLeftThresh() * m_max_val);
	SetRow(m_right_thresh, m_vd->GetRightThresh() * m_max_val);
	SetRow(m_luminance, m_vd->GetLuminance() * m_max_val);
	SetRow(m_alpha, m_vd->GetAlpha() * m_max_val);
	SetRow(m_sample_rate, m_vd->GetSampleRate());
	double amb, diff, spec, shine;
	m_vd->GetMaterial(amb, diff, spec, shine);
	SetRow(m_low_shading, amb);
	SetRow(m_hi_shading, shine);
	double shadow = 0.0;
	m_vd->GetShadowParams(shadow);
	SetRow(m_shadow, shadow);
	double low, high;
	m_vd->GetColormapValues(low, high);
	SetRow(m_colormap_low, low * m_max_val);
	SetRow(m_colormap_high, high * m_max_val);

	m_max_value->setValue(m_max_val);   // wx: "%.2f" of max(255 | 1, GetMaxValue())
	double sx, sy, sz;
	m_vd->GetSpacings(sx, sy, sz);
	m_spc_x->setValue(sx); m_spc_y->setValue(sy); m_spc_z->setValue(sz);

	Color c = m_vd->GetColor();
	QColor qc = QColor::fromRgbF(std::clamp(c.r(), 0.0, 1.0), std::clamp(c.g(), 0.0, 1.0), std::clamp(c.b(), 0.0, 1.0));
	m_color_btn->setStyleSheet(QString("background-color: %1;").arg(qc.name()));
	if (!m_color_typing)
		m_color_text->setText(QString("%1 , %2 , %3").arg(qc.red()).arg(qc.green()).arg(qc.blue()));

	bool alpha = m_vd->GetEnableAlpha();
	bool shading = m_vd->GetVR() ? m_vd->GetVR()->get_shading() : m_vd->GetShading();
	m_alpha_chk->setChecked(alpha);
	m_shading_chk->setChecked(shading);
	m_shadow_chk->setChecked(m_vd->GetShadow());
	m_inv_chk->setChecked(m_vd->GetInvert());
	m_mip_chk->setChecked(m_vd->GetMode() == 1);
	m_interp_chk->setChecked(m_vd->GetInterpolate());
	m_nr_chk->setChecked(m_vd->GetNR());
	m_colormap_chk->setChecked(m_vd->GetColormapMode() == 1);
	m_legend_chk->setChecked(m_vd->GetLegend());

	//mask
	const int mask_hide_mode = m_vd->GetMaskHideMode();
	m_mask_out_chk->setChecked(mask_hide_mode == VOL_MASK_HIDE_OUTSIDE);
	m_mask_in_chk->setChecked(mask_hide_mode == VOL_MASK_HIDE_INSIDE);
	//sync group
	if (m_group)
	{
		m_sync_group = m_group->GetVolumeSyncProp();
		m_sync_group_spc = m_group->GetVolumeSyncSpc();
	}
	m_sync_group_chk->setChecked(m_sync_group);
	m_sync_spc_chk->setChecked(m_sync_group_spc);
	//Indexed color
	m_idcl_chk->setChecked(m_vd->GetColormapMode() == 3);
	UpdateEnables();
	UpdateUIsROI();
	//blend mode
	m_depth_chk->setChecked(m_vd->GetBlendMode() == 2);

	m_updating = false;
}

// ---------------------------------------------------------------------------
// appliers
// ---------------------------------------------------------------------------

// wx VPropView handlers: "if (m_sync_group && m_group) m_group->Set..(val) else m_vd->Set..(val)"
void VolumePropPanel::ApplyGamma(double v) { if (m_vd) { ForTargets([v](VolumeData* vd) { vd->Set3DGamma(v); }); Q_EMIT volumeChanged(); } }
void VolumePropPanel::ApplyBoundary(double v) { if (m_vd) { ForTargets([v](VolumeData* vd) { vd->SetBoundary(v); }); Q_EMIT volumeChanged(); } }
void VolumePropPanel::ApplySaturation(double v) { if (m_vd) { const double d = v / m_max_val; ForTargets([d](VolumeData* vd) { vd->SetOffset(d); }); Q_EMIT volumeChanged(); } }
void VolumePropPanel::ApplyLuminance(double v) { if (m_vd) { const double d = v / m_max_val; ForTargets([d](VolumeData* vd) { vd->SetLuminance(d); }); GetSettings(); Q_EMIT volumeChanged(); } }
void VolumePropPanel::ApplyAlpha(double v) { if (m_vd) { const double d = v / m_max_val; ForTargets([d](VolumeData* vd) { vd->SetAlpha(d); }); Q_EMIT volumeChanged(); } }
void VolumePropPanel::ApplyLowShading(double v) { if (m_vd) { ForTargets([v](VolumeData* vd) { vd->SetLowShading(v); }); Q_EMIT volumeChanged(); } }
void VolumePropPanel::ApplyHiShading(double v) { if (m_vd) { ForTargets([v](VolumeData* vd) { vd->SetHiShading(v); }); Q_EMIT volumeChanged(); } }
// wx OnShadowText / OnSampleText: every volume of the view in Depth mode
void VolumePropPanel::ApplyShadow(double v) { if (m_vd) { ForTargetsView([v](VolumeData* vd) { vd->SetShadowParams(v); }, true); Q_EMIT volumeChanged(); } }
void VolumePropPanel::ApplySampleRate(double v) { if (m_vd) { ForTargetsView([v](VolumeData* vd) { vd->SetSampleRate(v); }, true); Q_EMIT volumeChanged(); } }

void VolumePropPanel::ApplyLeftThresh(double v)
{
	if (!m_vd) return;
	double right = m_right_thresh.spin->value();
	if (v > right) { right = v; SetRow(m_right_thresh, right); const double r = right / m_max_val; ForTargets([r](VolumeData* vd) { vd->SetRightThresh(r); }); }
	const double d = v / m_max_val;
	ForTargets([d](VolumeData* vd) { vd->SetLeftThresh(d); });
	Q_EMIT volumeChanged();
}

void VolumePropPanel::ApplyRightThresh(double v)
{
	if (!m_vd) return;
	double left = m_left_thresh.spin->value();
	if (v < left) { left = v; SetRow(m_left_thresh, left); const double l = left / m_max_val; ForTargets([l](VolumeData* vd) { vd->SetLeftThresh(l); }); }
	const double d = v / m_max_val;
	ForTargets([d](VolumeData* vd) { vd->SetRightThresh(d); });
	Q_EMIT volumeChanged();
}

void VolumePropPanel::ApplyColormapLow(double v)
{
	if (!m_vd) return;
	double high = m_colormap_high.spin->value();
	if (v > high) { high = v; SetRow(m_colormap_high, high); }
	const double lo = v / m_max_val, hi = high / m_max_val;
	ForTargets([lo, hi](VolumeData* vd) { vd->SetColormapValues(lo, hi); });
	Q_EMIT volumeChanged();
}

void VolumePropPanel::ApplyColormapHigh(double v)
{
	if (!m_vd) return;
	double low = m_colormap_low.spin->value();
	if (v < low) { low = v; SetRow(m_colormap_low, low); }
	const double lo = low / m_max_val, hi = v / m_max_val;
	ForTargets([lo, hi](VolumeData* vd) { vd->SetColormapValues(lo, hi); });
	Q_EMIT volumeChanged();
}

// wx VPropView::SetSpacings
void VolumePropPanel::ApplySpacing()
{
	if (m_updating || !m_vd) return;
	const double spcx = m_spc_x->value(), spcy = m_spc_y->value(), spcz = m_spc_z->value();
	if (spcx <= 0.0 || spcy <= 0.0 || spcz <= 0.0)
		return;
	if (spcz / std::min(spcx, spcy) >= 500.0)
		return;
	if ((m_sync_group || m_sync_group_spc) && HasGroup())
	{
		for (VolumeData* vd : GroupVolumes())
			if (vd && vd->GetName().Find(L"skeleton") == -1)
				vd->SetSpacings(spcx, spcy, spcz);
	}
	else
		m_vd->SetSpacings(spcx, spcy, spcz);
	Q_EMIT volumeChanged();
}

// ---------------------------------------------------------------------------
// group / mask / index / depth (wx VPropView)
// ---------------------------------------------------------------------------

void VolumePropPanel::SetGroup(DataGroup* group)
{
	m_group = group;
	if (m_group)
	{
		m_sync_group = m_group->GetVolumeSyncProp();
		QSignalBlocker b(m_sync_group_chk);
		m_sync_group_chk->setChecked(m_sync_group);
	}
}

std::vector<VolumeData*> VolumePropPanel::GroupVolumes() const
{
	if (m_group)
	{
		std::vector<VolumeData*> v;
		for (int i = 0; i < m_group->GetVolumeNum(); i++)
			if (VolumeData* vd = m_group->GetVolumeData(i)) v.push_back(vd);
		return v;
	}
	return m_group_vols;
}

void VolumePropPanel::ForTargets(const std::function<void(VolumeData*)>& f, bool depth)
{
	if (!m_vd) return;
	const std::vector<VolumeData*> vols = GroupVolumes();
	bool to_group = !vols.empty() && m_sync_group;
	if (!to_group && depth && !vols.empty())
	{
		// wx: "else if (m_group && m_group->GetBlendMode()==2)" (DataGroup::
		// GetBlendMode: the volumes' blend mode)
		to_group = true;
		for (VolumeData* vd : vols)
			if (vd->GetBlendMode() != 2) { to_group = false; break; }
	}
	if (to_group)
		for (VolumeData* vd : vols) f(vd);
	else
		f(m_vd);
}

void VolumePropPanel::ForTargetsView(const std::function<void(VolumeData*)>& f, bool depth)
{
	if (!m_vd) return;
	if (m_canvas && m_canvas->GetVolMethod() == RenderCanvas::VolMethodMulti)
	{
		for (VolumeData* vd : m_canvas->Volumes())   // wx GetAllVolumeNum / GetAllVolumeData
			if (vd) f(vd);
	}
	else
		ForTargets(f, depth);
}

void VolumePropPanel::SetRowEnabled(SliderRow& r, bool on)
{
	r.slider->setEnabled(on);
	r.spin->setEnabled(on);
}

bool VolumePropPanel::RowEnabled(int row) const
{
	const SliderRow* rows[] = { &m_gamma, &m_saturation, &m_luminance, &m_alpha, &m_low_shading,
		&m_boundary, &m_left_thresh, &m_right_thresh };
	if (row < 0 || row >= 8) return false;
	return rows[row]->slider->isEnabled() && rows[row]->spin->isEnabled();
}

QString VolumePropPanel::ThresholdLabel() const
{
	return m_left_thresh.label->text();
}

// wx VPropView::GetSettings: enable states
void VolumePropPanel::UpdateEnables()
{
	if (!m_vd) return;
	const bool alpha = m_vd->GetEnableAlpha();
	const bool shading = m_vd->GetVR() ? m_vd->GetVR()->get_shading() : m_vd->GetShading();
	SetRowEnabled(m_alpha, alpha);
	SetRowEnabled(m_low_shading, alpha && shading);
	//MIP
	if (m_vd->GetMode() == 1)
	{
		SetRowEnabled(m_alpha, false);
		SetRowEnabled(m_right_thresh, false);
		SetRowEnabled(m_boundary, false);
		SetRowEnabled(m_luminance, false);
		const bool cm = m_vd->GetColormapMode() == 1;
		SetRowEnabled(m_gamma, !cm);
		SetRowEnabled(m_saturation, !cm);
		m_left_thresh.label->setText(tr("Shade Threshold:"));
	}
	else
	{
		if (alpha)
			SetRowEnabled(m_alpha, true);
		SetRowEnabled(m_right_thresh, true);
		SetRowEnabled(m_boundary, true);
		SetRowEnabled(m_gamma, true);
		SetRowEnabled(m_saturation, true);
		SetRowEnabled(m_luminance, true);
		m_left_thresh.label->setText(tr("Threshold:"));
	}
	//Indexed color
	const bool idcl = m_vd->GetColormapMode() == 3;
	m_colormap_chk->setEnabled(!idcl);
	SetRowEnabled(m_colormap_low, !idcl);
	SetRowEnabled(m_colormap_high, !idcl);
}

// wx OnAlphaCheck
void VolumePropPanel::OnAlphaCheck()
{
	const bool alpha = m_alpha_chk->isChecked();
	if (alpha)
	{
		if (m_vd->GetMode() == 0)
			SetRowEnabled(m_alpha, true);
		//shading
		if (m_vd->GetShading())
		{
			SetRowEnabled(m_low_shading, true);
			{ QSignalBlocker b(m_shading_chk); m_shading_chk->setChecked(true); }
			if (m_vd->GetVR()) m_vd->GetVR()->set_shading(true);
		}
	}
	else
	{
		SetRowEnabled(m_alpha, false);
		//shading
		if (m_vd->GetShading())
		{
			SetRowEnabled(m_low_shading, false);
			{ QSignalBlocker b(m_shading_chk); m_shading_chk->setChecked(false); }
			if (m_vd->GetVR()) m_vd->GetVR()->set_shading(false);
		}
	}
	ForTargets([alpha](VolumeData* vd) { vd->SetEnableAlpha(alpha); });
	Q_EMIT volumeChanged();
}

// wx OnShadingEnable
void VolumePropPanel::OnShadingCheck()
{
	const bool shading = m_shading_chk->isChecked();
	ForTargets([shading](VolumeData* vd) { vd->SetShading(shading); });
	if (shading)
	{
		if (m_vd->GetEnableAlpha())
			SetRowEnabled(m_low_shading, true);
	}
	else
		SetRowEnabled(m_low_shading, false);
	Q_EMIT volumeChanged();
}

// wx OnMIPCheck
void VolumePropPanel::OnMIPCheck()
{
	const int val = m_mip_chk->isChecked() ? 1 : 0;
	if (val == 1)
	{
		// the frame's views (the Qt app has one)
		if (m_canvas && m_canvas->GetVolMethod() == RenderCanvas::VolMethodMulti)
		{
			QMessageBox::information(this, QCoreApplication::applicationName(), tr("MIP is not supported in Depth mode."));
			m_updating = true;
			m_mip_chk->setChecked(false);   // wx SetValue: no event
			m_updating = false;
			return;
		}
		SetRowEnabled(m_alpha, false);
		SetRowEnabled(m_right_thresh, false);
		SetRowEnabled(m_boundary, false);
		SetRowEnabled(m_luminance, false);
		if (m_vd->GetColormapMode() == 1)
		{
			SetRowEnabled(m_gamma, false);
			SetRowEnabled(m_saturation, false);
		}
		m_left_thresh.label->setText(tr("Shade Threshold:"));
	}
	else
	{
		if (m_vd->GetEnableAlpha())
			SetRowEnabled(m_alpha, true);
		SetRowEnabled(m_right_thresh, true);
		SetRowEnabled(m_boundary, true);
		SetRowEnabled(m_gamma, true);
		SetRowEnabled(m_saturation, true);
		SetRowEnabled(m_luminance, true);
		m_left_thresh.label->setText(tr("Threshold:"));
	}
	ForTargets([val](VolumeData* vd) { vd->SetMode(val); });
	Q_EMIT volumeChanged();
}

// wx OnShadowEnable: Depth mode -> every volume; sync -> the group; else
// (also a group in depth blend mode) the volume
void VolumePropPanel::OnShadowCheck()
{
	const bool shadow = m_shadow_chk->isChecked();
	ForTargetsView([shadow](VolumeData* vd) { vd->SetShadow(shadow); }, false);
	Q_EMIT volumeChanged();
}

// wx OnNRCheck
void VolumePropPanel::OnNRCheck()
{
	const bool val = m_nr_chk->isChecked();
	ForTargetsView([val](VolumeData* vd) { vd->SetNR(val); }, true);
	Q_EMIT volumeChanged();
}

// wx VPropView::UpdateMaxValue (Enter in the Max Value box)
void VolumePropPanel::UpdateMaxValue()
{
	const double val = m_max_value->value();
	if (m_vd && m_vd->GetTexture() && m_vd->GetTexture()->get_nrrd(0) && val > 0.0)
	{
		m_vd->SetMaxValue(val);
		double scalar_scale = 1.0;
		const int t = m_vd->GetTexture()->get_nrrd(0)->getNrrdDataType();
		if (t == nrrdTypeUShort)
			scalar_scale = 65535.0 / val;
		else if (t == nrrdTypeFloat || t == nrrdTypeDouble)
			scalar_scale = 1.0 / val;
		m_vd->SetScalarScale(scalar_scale);
		GetSettings();
	}
	Q_EMIT volumeChanged();
}

// wx OnSyncGroupCheck: turning Sync on copies the shown values to the group
void VolumePropPanel::OnSyncGroupCheck()
{
	m_sync_group = m_sync_group_chk->isChecked();
	m_sync_group_spc = m_sync_spc_chk->isChecked();
	if (m_group)
	{
		m_group->SetVolumeSyncProp(m_sync_group);
		m_group->SetVolumeSyncSpc(m_sync_group | m_sync_group_spc);
	}
	if (m_sync_group && HasGroup() && m_vd)
	{
		const double gamma = m_gamma.spin->value();
		const double boundary = m_boundary.spin->value();
		const double offset = m_saturation.spin->value() / m_max_val;
		const double lthresh = m_left_thresh.spin->value() / m_max_val;
		const double rthresh = m_right_thresh.spin->value() / m_max_val;
		const double lum = m_luminance.spin->value() / m_max_val;
		const bool shadow = m_shadow_chk->isChecked();
		const double shadow_p = m_shadow.spin->value();
		const double hi_shading = m_hi_shading.spin->value();
		const double alpha = m_alpha.spin->value() / m_max_val;
		const double srate = m_sample_rate.spin->value();
		const bool shading = m_shading_chk->isChecked();
		const double lo_shading = m_low_shading.spin->value();
		const double cm_lo = m_colormap_low.spin->value() / m_max_val;
		const double cm_hi = m_colormap_high.spin->value() / m_max_val;
		const bool inv = m_inv_chk->isChecked();
		const bool mip = m_mip_chk->isChecked();
		const bool nr = m_nr_chk->isChecked();
		for (VolumeData* vd : GroupVolumes())
		{
			vd->Set3DGamma(gamma);
			vd->SetBoundary(boundary);
			vd->SetOffset(offset);
			vd->SetLeftThresh(lthresh);
			vd->SetRightThresh(rthresh);
			vd->SetLuminance(lum);
			vd->SetShadow(shadow);
			vd->SetShadowParams(shadow_p);
			vd->SetHiShading(hi_shading);
			vd->SetAlpha(alpha);
			vd->SetSampleRate(srate);
			vd->SetShading(shading);
			vd->SetLowShading(lo_shading);
			vd->SetColormapValues(cm_lo, cm_hi);
			vd->SetInvert(inv);
			vd->SetMode(mip ? 1 : 0);
			vd->SetNR(nr);
		}
	}
	if ((m_sync_group_spc || m_sync_group) && HasGroup())
		ApplySpacing();
	Q_EMIT volumeChanged();
}

// wx OnSyncGroupSpcCheck
void VolumePropPanel::OnSyncGroupSpcCheck()
{
	m_sync_group_spc = m_sync_spc_chk->isChecked();
	if (m_group)
		m_group->SetVolumeSyncSpc(m_sync_group_spc);
	if (m_sync_group_spc && HasGroup())
		ApplySpacing();
	Q_EMIT volumeChanged();
}

// wx OnMaskHideOutsideCheck / OnMaskHideInsideCheck
void VolumePropPanel::OnMaskHide(bool outside)
{
	if (!m_vd) return;
	int mode = VOL_MASK_HIDE_NONE;
	if (outside)
	{
		if (m_mask_out_chk->isChecked())
		{
			m_mask_in_chk->setChecked(false);
			mode = VOL_MASK_HIDE_OUTSIDE;
		}
	}
	else
	{
		if (m_mask_in_chk->isChecked())
		{
			m_mask_out_chk->setChecked(false);
			mode = VOL_MASK_HIDE_INSIDE;
		}
	}
	ForTargets([mode](VolumeData* vd) { vd->SetMaskHideMode(mode); });
	Q_EMIT volumeChanged();
}

// wx OnIDCLCheck: index (ID) colour = colormap mode 3
void VolumePropPanel::OnIdclCheck()
{
	if (!m_vd) return;
	const bool idcolor = m_idcl_chk->isChecked();
	const bool colormap = m_colormap_chk->isChecked();
	const int colormode = idcolor ? 3 : (colormap ? 1 : 0);
	m_vd->SetColormapMode(colormode);   // wx: the volume only (group sync commented out)
	UpdateIdclUi();
	UpdateUIsROI();
	Q_EMIT colormapModeChanged();
	Q_EMIT volumeChanged();
	Q_EMIT treeRefreshRequested();   // wx UpdateTree(name, 2) + Collapse/Expand
}

// wx VPropView::UpdateUIsROI / ShowUIsROI / HideUIsROI
void VolumePropPanel::UpdateUIsROI()
{
	if (!m_vd) return;
	if (m_vd->GetColormapMode() == 3)
	{
		SetROIindex(m_vd->GetEditSelID());
		{
			QSignalBlocker b(m_roi_disp_mode_combo);
			m_roi_disp_mode_combo->setCurrentIndex(m_vd->GetIDColDispMode());
		}
		if (!m_cmap_row->isHidden()) m_cmap_row->hide();
		if (m_roi_row->isHidden()) m_roi_row->show();
		if (m_vd->GetEditSelID() < -1)
		{
			m_roi_color_btn->hide();
			m_roi_text->show();
			m_roi_st->setText(tr("Segment Group:"));
		}
		else if (m_vd->GetEditSelID() >= 0)
		{
			m_roi_color_btn->show();
			m_roi_text->show();
			m_roi_st->setText(tr("Segment:"));
		}
		else
		{
			m_roi_color_btn->hide();
			m_roi_text->hide();
			m_roi_st->setText(tr("Segment:"));
		}
	}
	else if (!m_roi_row->isHidden())
	{
		// only when switching back: an explicit show() of the colormap row
		// re-lays out the pane (and so the view) on every selection
		m_roi_row->hide();
		m_cmap_row->show();
	}
}

// wx VPropView::SetROIindex
void VolumePropPanel::SetROIindex(int id)
{
	if (!m_vd) return;
	m_roi_text->setText(QString());
	if (id > 0)
	{
		unsigned char r = 0, g = 0, b = 0;
		m_vd->GetIDColor(r, g, b);
		m_roi_color_btn->setStyleSheet(QString("background-color: %1;").arg(QColor(r, g, b).name()));
		m_roi_text->setPlaceholderText(tr("Segment %1").arg(id));
	}
	m_roi_text->setText(QString::fromStdWString(m_vd->GetROIName()));
}

// wx VPropView::OnROIColorBtn
void VolumePropPanel::OnROIColorBtn()
{
	if (!m_vd) return;
	unsigned char r = 0, g = 0, b = 0;
	m_vd->GetIDColor(r, g, b);
	QColor c = QColorDialog::getColor(QColor(r, g, b), this, tr("Segment Color"));
	if (!c.isValid()) return;
	m_vd->SetIDColor(c.red(), c.green(), c.blue());
	m_roi_color_btn->setStyleSheet(QString("background-color: %1;").arg(c.name()));
	Q_EMIT volumeChanged();
}

void VolumePropPanel::UpdateIdclUi()
{
	const bool idcl = m_vd && m_vd->GetColormapMode() == 3;
	m_colormap_chk->setEnabled(!idcl);
	for (SliderRow* r : { &m_colormap_low, &m_colormap_high })
	{
		r->slider->setEnabled(!idcl);
		r->spin->setEnabled(!idcl);
	}
	// wx OnIDCLCheck: in MIP mode a plain colormap disables gamma /
	// saturation / luminance, otherwise they are enabled
	if (m_vd && m_vd->GetMode() == 1)
	{
		const bool cm = m_vd->GetColormapMode() == 1;
		for (SliderRow* r : { &m_gamma, &m_saturation, &m_luminance })
			SetRowEnabled(*r, !cm);
	}
}

// wx OnDepthCheck: the group is blended in depth (VOL_METHOD_MULTI) mode
void VolumePropPanel::OnDepthCheck()
{
	const std::vector<VolumeData*> vols = GroupVolumes();
	if (vols.empty()) { Q_EMIT volumeChanged(); return; }
	if (m_depth_chk->isChecked())
	{
		for (VolumeData* vd : vols) vd->SetBlendMode(2);
		if (m_vd)
		{
			const bool nr = m_vd->GetNR();
			const double srate = m_vd->GetSampleRate();
			const bool shadow = m_vd->GetShadow();
			double sp = 0.0;
			m_vd->GetShadowParams(sp);
			for (VolumeData* vd : vols)
			{
				vd->SetNR(nr);
				vd->SetSampleRate(srate);
				vd->SetShadow(shadow);
				vd->SetShadowParams(sp);
			}
		}
	}
	else
		for (VolumeData* vd : vols) vd->SetBlendMode(0);
	Q_EMIT volumeChanged();
}

void VolumePropPanel::OnColorButton()
{
	if (!m_vd) return;
	Color c = m_vd->GetColor();
	QColor init = QColor::fromRgbF(std::clamp(c.r(), 0.0, 1.0), std::clamp(c.g(), 0.0, 1.0), std::clamp(c.b(), 0.0, 1.0));
	QColor chosen = QColorDialog::getColor(init, this, tr("Volume color"));
	if (!chosen.isValid()) return;
	m_vd->SetColor(Color(chosen.redF(), chosen.greenF(), chosen.blueF()), true);
	GetSettings();
	Q_EMIT colorChanged();
	Q_EMIT volumeChanged();
}

// ---------------------------------------------------------------------------
// Color box (wx VPropView::OnColorTextChange): a single-letter code or
// "r , g , b" applies as soon as it is complete. Names of dyes / fluorescent
// proteins and wavelengths ("488nm") are an addition of the Qt port.
// ---------------------------------------------------------------------------

namespace
{
	struct NamedColor { const char* name; int r, g, b; };
	// single letters: the wx set (R G B W P) plus Y and a few more
	const NamedColor kLetterColors[] = {
		{ "r", 255, 0, 0 }, { "g", 0, 255, 0 }, { "b", 0, 0, 255 },
		{ "w", 255, 255, 255 }, { "p", 255, 0, 255 },
		{ "y", 255, 255, 0 }, { "c", 0, 255, 255 }, { "m", 255, 0, 255 },
		{ "o", 255, 128, 0 }, { "k", 128, 128, 128 }, { "a", 255, 191, 0 },
		{ "v", 128, 0, 255 }, { "s", 0, 128, 255 }, { "l", 128, 255, 0 },
	};
	// common fluorophores, by their usual display colour in microscopy
	const NamedColor kDyeColors[] = {
		{ "red", 255, 0, 0 }, { "green", 0, 255, 0 }, { "blue", 0, 0, 255 },
		{ "white", 255, 255, 255 }, { "purple", 255, 0, 255 }, { "magenta", 255, 0, 255 },
		{ "yellow", 255, 255, 0 }, { "cyan", 0, 255, 255 }, { "orange", 255, 128, 0 },
		{ "gray", 128, 128, 128 }, { "grey", 128, 128, 128 }, { "amber", 255, 191, 0 },
		{ "violet", 128, 0, 255 }, { "lime", 128, 255, 0 },
		{ "dapi", 0, 0, 255 }, { "hoechst", 0, 0, 255 }, { "bfp", 0, 64, 255 },
		{ "alexa405", 64, 0, 255 },
		{ "cfp", 0, 200, 255 }, { "ecfp", 0, 200, 255 }, { "mturquoise", 0, 200, 255 },
		{ "gfp", 0, 255, 0 }, { "egfp", 0, 255, 0 }, { "fitc", 0, 255, 0 },
		{ "alexa488", 0, 255, 0 }, { "gcamp", 0, 255, 0 },
		{ "yfp", 255, 255, 0 }, { "eyfp", 255, 255, 0 }, { "venus", 255, 255, 0 },
		{ "citrine", 255, 255, 0 },
		{ "alexa546", 255, 160, 0 }, { "alexa555", 255, 128, 0 }, { "cy3", 255, 128, 0 },
		{ "tritc", 255, 96, 0 }, { "tdtomato", 255, 64, 0 }, { "dsred", 255, 32, 0 },
		{ "alexa568", 255, 64, 0 }, { "mscarlet", 255, 0, 0 },
		{ "rfp", 255, 0, 0 }, { "mcherry", 255, 0, 0 }, { "alexa594", 255, 0, 0 },
		{ "texasred", 255, 0, 0 },
		{ "alexa633", 255, 0, 128 }, { "alexa647", 255, 0, 255 }, { "cy5", 255, 0, 255 },
		{ "cy7", 160, 0, 255 }, { "irfp", 160, 0, 255 },
	};

	// Usual display colour of a channel given by its excitation laser line
	// (405 -> blue, 488 -> green, 514 -> yellow, 561/568/594 -> red,
	// 633/640/647 -> magenta, >700 -> violet), as biologists label channels.
	QColor WavelengthColor(double nm)
	{
		if (nm < 450) return QColor(0, 0, 255);
		if (nm < 505) return QColor(0, 255, 0);
		if (nm < 540) return QColor(255, 255, 0);
		if (nm < 620) return QColor(255, 0, 0);
		if (nm < 700) return QColor(255, 0, 255);
		return QColor(160, 0, 255);
	}

	// wx parsing of "r , g , b": up to three numbers separated by anything.
	int ParseRgb(const QString& str, int rgb[3])
	{
		int filled = 0;
		QString cur;
		auto flush = [&]() {
			if (cur.isEmpty()) return;
			if (filled < 3) rgb[filled++] = std::clamp(int(cur.toDouble()), 0, 255);
			cur.clear();
		};
		for (QChar ch : str)
		{
			if (ch.isDigit() || ch == '.') cur += ch;
			else flush();
		}
		flush();
		return filled;
	}

	bool LookupColor(const QString& text, QColor& out)
	{
		QString key = text.trimmed().toLower();
		key.remove(' ').remove('-').remove('_');
		if (key.isEmpty()) return false;
		if (key.size() == 1)
		{
			for (const NamedColor& n : kLetterColors)
				if (key == QLatin1String(n.name)) { out = QColor(n.r, n.g, n.b); return true; }
			return false;
		}
		for (const NamedColor& n : kDyeColors)
			if (key == QLatin1String(n.name)) { out = QColor(n.r, n.g, n.b); return true; }
		if (key.endsWith("nm"))
		{
			bool ok = false;
			double nm = key.left(key.size() - 2).toDouble(&ok);   // "488nm"; a bare "488" is read as R
			if (ok && nm >= 350 && nm <= 800) { out = WavelengthColor(nm); return true; }
		}
		return false;
	}
}

bool VolumePropPanel::eventFilter(QObject* obj, QEvent* ev)
{
	if (obj == m_color_text && ev->type() == QEvent::MouseButtonDblClick)
	{
		m_color_text->selectAll();   // wx OnColorTextFocus: SetSelection(0, -1)
		return true;
	}
	return QWidget::eventFilter(obj, ev);
}

void VolumePropPanel::ApplyTypedColor(const QColor& qc)
{
	if (!m_vd) return;
	m_color_typing = true;
	m_vd->SetColor(Color(qc.redF(), qc.greenF(), qc.blueF()));   // wx: SetColor(color) from the text box
	GetSettings();
	m_color_typing = false;
	Q_EMIT colorChanged();
	Q_EMIT volumeChanged();
}

void VolumePropPanel::OnColorTextEdited(const QString& text)
{
	if (m_updating || !m_vd) return;
	QColor qc;
	if (LookupColor(text, qc))
	{
		// keep the typed text while editing so longer names ("gfp") can be typed
		m_color_typing = true;
		ApplyTypedColor(qc);
		return;
	}
	int rgb[3] = { 255, 255, 255 };
	if (ParseRgb(text, rgb) == 3 && text.contains(QRegularExpression("[^0-9.]")))
	{
		m_color_typing = true;
		ApplyTypedColor(QColor(rgb[0], rgb[1], rgb[2]));
	}
}

void VolumePropPanel::OnColorTextFinished()
{
	// normalise the box to "r , g , b" (wx rewrites it as soon as a colour is complete)
	m_color_typing = false;
	if (!m_vd) return;
	Color c = m_vd->GetColor();
	QColor qc = QColor::fromRgbF(std::clamp(c.r(), 0.0, 1.0), std::clamp(c.g(), 0.0, 1.0), std::clamp(c.b(), 0.0, 1.0));
	m_color_text->setText(QString("%1 , %2 , %3").arg(qc.red()).arg(qc.green()).arg(qc.blue()));
}

// wx VPropView::OnResetDefault: the DataManager defaults (m_vol_*, read from
// default_volume_settings.dft at start-up / set by Save as Default)
void VolumePropPanel::OnResetDefault()
{
	if (!m_vd || !m_data_mgr) return;
	DataManager* mgr = m_data_mgr;
	double dval;
	bool bval;

	//gamma
	m_vd->Set3DGamma(mgr->m_vol_gam);
	//extract boundary
	m_vd->SetBoundary(mgr->m_vol_exb);
	//low offset
	m_vd->SetOffset(mgr->m_vol_of1);
	//low thresholding
	m_vd->SetLeftThresh(mgr->m_vol_lth);
	//high thresholding
	m_vd->SetRightThresh(mgr->m_vol_hth);
	//low shading
	double amb, diff, spec, shine;
	m_vd->GetMaterial(amb, diff, spec, shine);
	m_vd->SetMaterial(mgr->m_vol_lsh, diff, spec, shine);
	//high shading
	m_vd->GetMaterial(amb, diff, spec, shine);
	m_vd->SetMaterial(amb, diff, spec, mgr->m_vol_hsh);
	//alpha
	m_vd->SetAlpha(mgr->m_vol_alf);
	//sample rate
	m_vd->SetSampleRate(mgr->m_vol_spr);
	//luminance: the HSV value of the colour
	dval = mgr->m_vol_lum;
	double h, sat, v;
	m_vd->GetHSV(h, sat, v);
	FLIVR::HSVColor hsv(h, sat, dval);
	Color color(hsv);
	m_vd->SetColor(color);
	//colormap low / high value
	m_vd->SetColormapValues(mgr->m_vol_lcm, mgr->m_vol_hcm);
	//shadow intensity
	m_vd->SetShadowParams(mgr->m_vol_swi);

	const bool to_group = m_sync_group && m_group;
	auto apply = [this, to_group](const std::function<void(VolumeData*)>& f) {
		if (to_group) { for (VolumeData* vd : GroupVolumes()) f(vd); }
		else f(m_vd);
	};
	//enable alpha
	bval = mgr->m_vol_eap;
	apply([bval](VolumeData* vd) { vd->SetEnableAlpha(bval); });
	//enable shading
	bval = mgr->m_vol_esh;
	apply([bval](VolumeData* vd) { vd->SetShading(bval); });
	//inversion
	bval = mgr->m_vol_inv;
	apply([bval](VolumeData* vd) { vd->SetInvert(bval); });
	//enable mip
	bval = mgr->m_vol_mip;
	apply([bval](VolumeData* vd) { vd->SetMode(bval ? 1 : 0); });
	//noise reduction
	bval = mgr->m_vol_nrd;
	apply([bval](VolumeData* vd) { vd->SetNR(bval); });
	//shadow
	bval = mgr->m_vol_shw;
	apply([bval](VolumeData* vd) { vd->SetShadow(bval); });

	// wx writes the controls one by one (ChangeValue / SetValue); the
	// result is what GetSettings shows
	GetSettings();
	//apply all
	Q_EMIT volumeChanged();
}

QString VolumePropPanel::DefaultSettingsPath()
{
	const QString env = qEnvironmentVariable("VVD_QT_VOLUME_DFT");
	if (!env.isEmpty())
		return env;
	const VVDString name(L"default_volume_settings.dft");
	return QString::fromStdWString(vvd::SettingsWritePath(name));   // save only (loads: DataManager)
}

// wx VPropView::OnSaveDefault: the shown values -> default_volume_settings.dft
// and the DataManager defaults (new volumes, Reset). Scripted test runs only
// write the file when VVD_QT_VOLUME_DFT names one (TestRun.h).
void VolumePropPanel::OnSaveDefault()
{
	DataManager* mgr = m_data_mgr;
	if (!mgr)
		return;

	vvd::IniFile fconfig;
	double val;
	auto num = [](QDoubleSpinBox* sb) { return sb->value(); };
	//extract boundary
	val = num(m_boundary.spin);
	fconfig.Write(L"extract_boundary", val);
	mgr->m_vol_exb = val;
	//gamma
	val = num(m_gamma.spin);
	fconfig.Write(L"gamma", val);
	mgr->m_vol_gam = val;
	//low offset
	val = num(m_saturation.spin) / m_max_val;
	fconfig.Write(L"low_offset", val);
	mgr->m_vol_of1 = val;
	//high offset
	val = 1.0;
	fconfig.Write(L"high_offset", val);
	mgr->m_vol_of2 = val;
	//low thresholding
	val = num(m_left_thresh.spin) / m_max_val;
	fconfig.Write(L"low_thresholding", val);
	mgr->m_vol_lth = val;
	//high thresholding
	val = num(m_right_thresh.spin) / m_max_val;
	fconfig.Write(L"high_thresholding", val);
	mgr->m_vol_hth = val;
	//low shading
	val = num(m_low_shading.spin);
	fconfig.Write(L"low_shading", val);
	mgr->m_vol_lsh = val;
	//high shading
	val = num(m_hi_shading.spin);
	fconfig.Write(L"high_shading", val);
	mgr->m_vol_hsh = val;
	//alpha
	val = num(m_alpha.spin) / m_max_val;
	fconfig.Write(L"alpha", val);
	mgr->m_vol_alf = val;
	//sample rate
	val = num(m_sample_rate.spin);
	fconfig.Write(L"sample_rate", val);
	mgr->m_vol_spr = val;
	//x / y / z spacing
	val = num(m_spc_x);
	fconfig.Write(L"x_spacing", val);
	mgr->m_vol_xsp = val;
	val = num(m_spc_y);
	fconfig.Write(L"y_spacing", val);
	mgr->m_vol_ysp = val;
	val = num(m_spc_z);
	fconfig.Write(L"z_spacing", val);
	mgr->m_vol_zsp = val;
	//luminance
	val = num(m_luminance.spin) / m_max_val;
	fconfig.Write(L"luminance", val);
	mgr->m_vol_lum = val;
	//colormap low / high value
	val = num(m_colormap_low.spin) / m_max_val;
	fconfig.Write(L"colormap_low", val);
	mgr->m_vol_lcm = val;
	val = num(m_colormap_high.spin) / m_max_val;
	fconfig.Write(L"colormap_hi", val);
	mgr->m_vol_hcm = val;
	//alpha
	bool alpha = m_alpha_chk->isChecked();
	fconfig.Write(L"enable_alpha", alpha);
	mgr->m_vol_eap = alpha;
	//enable shading
	bool shading = m_shading_chk->isChecked();
	fconfig.Write(L"enable_shading", shading);
	mgr->m_vol_esh = shading;
	//inversion
	bool inv = m_inv_chk->isChecked();
	fconfig.Write(L"enable_inv", inv);
	mgr->m_vol_inv = inv;
	//enable mip
	bool mip = m_mip_chk->isChecked();
	fconfig.Write(L"enable_mip", mip);
	mgr->m_vol_mip = mip;
	//noise reduction
	bool nrd = m_nr_chk->isChecked();
	fconfig.Write(L"noise_rd", nrd);
	mgr->m_vol_nrd = nrd;
	//shadow
	bool shw = m_shadow_chk->isChecked();
	fconfig.Write(L"enable_shadow", shw);
	mgr->m_vol_shw = shw;
	//shadow intensity
	val = num(m_shadow.spin);
	fconfig.Write(L"shadow_intensity", val);
	mgr->m_vol_swi = val;

	if (VVDQtScriptedRun() && !qEnvironmentVariableIsSet("VVD_QT_VOLUME_DFT"))
		return;
	fconfig.Save(VVDString(DefaultSettingsPath().toStdWString()));
}

// ---------------------------------------------------------------------------
// scale bar (VPropView::GetSettings "scale bar" part, OnScaleCheck,
// OnScaleTextCheck, OnScaleTextEditing, OnScaleUnitSelected,
// OnScaleDigitSelected, OnScaleLenFixCheck). The wx handlers apply to every
// view of the frame; the Qt viewer has one (the overlays set here).
// ---------------------------------------------------------------------------

void VolumePropPanel::SetOverlays(ViewOverlays* ov)
{
	m_overlays = ov;
	GetScaleBarSettings();
}

QString VolumePropPanel::ScaleUnitText() const
{
	switch (m_scale_cmb->currentIndex())
	{
	case 0:
		return QStringLiteral("nm");
	case 1:
	default:
		return QString::fromUtf8("\xc2\xb5m");
	case 2:
		return QStringLiteral("mm");
	}
}

void VolumePropPanel::GetScaleBarSettings()
{
	if (!m_overlays) return;
	m_scale_cmb->setCurrentIndex(m_overlays->GetSbUnitSel());
	m_scale_text->setText(QString::asprintf("%.3f", m_overlays->GetScaleBarLen()));
	bool scale_check = m_overlays->GetScaleBar();
	m_scale_chk->setChecked(scale_check);
	m_scale_te_chk->setChecked(m_overlays->GetSBTextEnabled());
	int dig = m_overlays->GetScaleBarDigit();
	if (dig >= 0 && dig <= 8)
		m_scale_digit_cmb->setCurrentIndex(dig);
	bool fixed = false;
	double dval = 0.0;
	int ival = 0;
	m_overlays->GetScaleBarFixed(fixed, dval, ival);
	m_scale_lenfix_chk->setChecked(fixed);
	if (scale_check)
	{
		m_scale_te_chk->setEnabled(true);
		m_scale_text->setEnabled(!fixed);
		m_scale_cmb->setEnabled(!fixed);
	}
	else
	{
		m_scale_te_chk->setEnabled(false);
		m_scale_text->setEnabled(false);
		m_scale_cmb->setEnabled(false);
	}
}

void VolumePropPanel::OnScaleCheck()
{
	if (!m_overlays) return;
	if (m_scale_chk->isChecked())
	{
		double len = m_scale_text->text().toDouble();
		m_overlays->SetScaleBarLen(len);
		m_overlays->SetSBText(ScaleUnitText());
		m_overlays->EnableScaleBar();
		m_overlays->SetSbUnitSel(m_scale_cmb->currentIndex());
		m_overlays->FixScaleBarLen(m_scale_lenfix_chk->isChecked());
		m_scale_te_chk->setEnabled(true);
		m_scale_lenfix_chk->setEnabled(true);
		if (!m_scale_lenfix_chk->isChecked())
		{
			m_scale_text->setEnabled(true);
			m_scale_cmb->setEnabled(true);
		}
	}
	else
	{
		m_overlays->DisableScaleBar();
		m_scale_te_chk->setEnabled(false);
		m_scale_lenfix_chk->setEnabled(false);
		m_scale_text->setEnabled(false);
		m_scale_cmb->setEnabled(false);
	}
}

void VolumePropPanel::OnScaleTextCheck()
{
	if (!m_overlays) return;
	if (m_scale_te_chk->isChecked())
	{
		m_overlays->SetSBText(ScaleUnitText());
		m_overlays->EnableSBText();
		m_overlays->SetSbUnitSel(m_scale_cmb->currentIndex());
	}
	else
		m_overlays->DisableSBText();
}

void VolumePropPanel::OnScaleTextEditing()
{
	if (!m_overlays) return;
	double len = m_scale_text->text().toDouble();
	m_overlays->SetScaleBarLen(len);
	m_overlays->SetSBText(ScaleUnitText());
	m_overlays->SetSbUnitSel(m_scale_cmb->currentIndex());
}

void VolumePropPanel::OnScaleUnitSelected()
{
	if (!m_overlays) return;
	m_overlays->SetSBText(ScaleUnitText());
	m_overlays->SetSbUnitSel(m_scale_cmb->currentIndex());
}

void VolumePropPanel::OnScaleDigitSelected()
{
	if (!m_overlays) return;
	m_overlays->SetScaleBarDigit(m_scale_digit_cmb->currentIndex());
}

void VolumePropPanel::OnScaleLenFixCheck()
{
	if (!m_overlays) return;
	if (m_scale_lenfix_chk->isChecked())
	{
		m_overlays->FixScaleBarLen(true);
		m_scale_text->setEnabled(false);
		m_scale_cmb->setEnabled(false);
	}
	else
	{
		bool fixed;
		double len;
		int unitid;
		m_overlays->GetScaleBarFixed(fixed, len, unitid);
		QString unit_text = ScaleUnitText();
		m_overlays->FixScaleBarLen(false);
		m_overlays->SetScaleBarLen(len);
		m_overlays->SetSBText(unit_text);
		m_overlays->SetSbUnitSel(unitid);
		m_overlays->FixScaleBarLen(m_scale_lenfix_chk->isChecked());
		m_scale_cmb->setCurrentIndex(unitid);
		m_scale_text->setText(QString::asprintf("%.3f", len));
		m_scale_text->setEnabled(true);
		m_scale_cmb->setEnabled(true);
	}
}
