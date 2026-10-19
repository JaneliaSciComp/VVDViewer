#include "AdjustPanel.h"
#include "RenderCanvas.h"

#include "DataManager.h"
#include "Core/FileUtil.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

const char* kQss =
	"AdjustPanel { background: transparent; }"
	"AdjustPanel QLabel { font-size: 11px; }"
	"AdjustPanel QCheckBox { font-size: 11px; spacing: 3px; }"
	"AdjustPanel QPushButton { font-size: 11px; min-height: 20px; max-height: 24px;"
	"  padding: 0px 2px; }"
	"AdjustPanel QLineEdit { font-size: 10px; min-height: 16px; max-height: 20px;"
	"  padding: 0px 1px; }"
	"AdjustPanel QSlider::groove:vertical { width: 4px; background: #2f7bf5;"
	"  border-radius: 2px; margin: 0 0; }"
	"AdjustPanel QSlider::handle:vertical { background: #ffffff; border: 1px solid #b0b0b0;"
	"  height: 14px; width: 14px; margin: 0 -6px; border-radius: 8px; }"
	// the sliders have their minimum at the top (setInvertedAppearance, as
	// wx): blue from the minimum down to the handle (sub-page = above it)
	"AdjustPanel QSlider::add-page:vertical { background: #c8c8c8; }"
	"AdjustPanel QSlider::sub-page:vertical { background: #2f7bf5; }";

const char* kChanName[3] = {"Red:", "Green:", "Blue:"};
const char* kChanColor[3] = {"#ff0000", "#00ff00", "#0000ff"};

} // namespace

AdjustPanel::AdjustPanel(QWidget* parent) : QWidget(parent)
{
	for (int c = 0; c < 3; ++c)
	{
		m_val[c][kGamma] = 1.0;  m_val[c][kLum] = 0.0;
		m_val[c][kEql] = 0.0;    m_val[c][kLevel] = 0.0;
		for (int k = 0; k < kKinds; ++k) m_dft[c][k] = m_val[c][k];
	}
	setStyleSheet(QString::fromLatin1(kQss));

	QVBoxLayout* main = new QVBoxLayout(this);
	main->setContentsMargins(2, 2, 2, 2);
	main->setSpacing(0);

	m_easyChk = new QCheckBox(tr("Easy Mode "), this);
	main->addWidget(m_easyChk, 0, Qt::AlignHCenter);
	main->addSpacing(5);

	m_defaultPage = buildPage(false);
	m_easyPage = buildPage(true);
	m_easyPage->hide();
	main->addWidget(m_defaultPage, 1);
	main->addWidget(m_easyPage, 1);

	m_dftBtn = new QPushButton(tr("Set Default"), this);
	main->addWidget(m_dftBtn, 0);

	connect(m_easyChk, &QCheckBox::toggled, this, &AdjustPanel::onEasyToggled);
	connect(m_dftBtn, &QPushButton::clicked, this, &AdjustPanel::onSetDefault);

	for (int c = 0; c < 3; ++c)
		for (int k = 0; k < kKinds; ++k)
			refreshWidget(c, k);

	// wx ctor: DisableAll(); LoadSettings();
	setEnabledAll(false);
	LoadSettings();
}

QWidget* AdjustPanel::buildPage(bool easy)
{
	QWidget* page = new QWidget(this);
	QVBoxLayout* v = new QVBoxLayout(page);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);

	// header
	if (!easy)
	{
		QHBoxLayout* h = new QHBoxLayout;
		h->setSpacing(0);
		const char* names[3] = {"Gam.", "Lum.", "Eql."};
		for (int i = 0; i < 3; ++i)
		{
			QLabel* l = new QLabel(tr(names[i]), page);
			l->setAlignment(Qt::AlignCenter);
			h->addWidget(l, 1);
		}
		v->addLayout(h);
	}
	else
	{
		QLabel* l = new QLabel(tr("Saturation"), page);
		l->setAlignment(Qt::AlignCenter);
		v->addWidget(l);
	}
	v->addSpacing(5);

	const int nk = easy ? 1 : 3;
	const int kinds[3] = {kGamma, kLum, kEql};
	for (int c = 0; c < 3; ++c)
	{
		// name + Link
		QHBoxLayout* h = new QHBoxLayout;
		h->setSpacing(0);
		QLabel* nm = new QLabel(tr(kChanName[c]), page);
		nm->setAlignment(Qt::AlignCenter);
		h->addWidget(nm, 1);
		QCheckBox* link = new QCheckBox(tr("Link"), page);
		h->addWidget(link, 1, Qt::AlignHCenter);
		v->addLayout(h);
		m_ch[c].link[easy] = link;
		connect(link, &QCheckBox::clicked, this,
			[this, c](bool on) { onLinkToggled(c, on); });
		v->addSpacing(3);

		// colour bar
		QFrame* bar = new QFrame(page);
		bar->setFixedHeight(5);
		bar->setAutoFillBackground(true);
		bar->setStyleSheet(QStringLiteral("background-color: %1;").arg(kChanColor[c]));
		v->addWidget(bar);

		// sliders
		QHBoxLayout* hs = new QHBoxLayout;
		hs->setContentsMargins(0, 0, 0, 0);
		hs->setSpacing(0);
		for (int i = 0; i < nk; ++i)
		{
			const int kind = easy ? int(kLevel) : kinds[i];
			QSlider* s = new QSlider(Qt::Vertical, page);
			// wx: wxSL_VERTICAL without wxSL_INVERSE, which on Windows has the
			// minimum at the top: dragging down raises the value (Gam. / Lum. /
			// Eql. / Easy-mode level: brighter). QSlider has it at the bottom.
			s->setInvertedAppearance(true);
			// wheel / arrow keys move the handle as on a native slider
			s->setInvertedControls(true);
			s->setMinimumHeight(40);
			s->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
			switch (kind)
			{
			case kGamma: s->setRange(10, 999); break;
			case kLum: s->setRange(-256, 256); break;
			case kEql: s->setRange(0, 100); break;
			default: s->setRange(-99, 99); break;
			}
			m_ch[c].sldr[kind] = s;
			hs->addWidget(s, 1, Qt::AlignHCenter);
			connect(s, &QSlider::valueChanged, this,
				[this, c, kind](int p) { onSliderChanged(c, kind, p); });
		}
		v->addLayout(hs, 1);

		// reset
		QPushButton* rb = new QPushButton(tr("Reset"), page);
		m_ch[c].reset[easy] = rb;
		connect(rb, &QPushButton::clicked, this, [this, c]() { onReset(c); });
		v->addWidget(rb);

		// value boxes
		QHBoxLayout* ht = new QHBoxLayout;
		ht->setContentsMargins(0, 0, 0, 0);
		ht->setSpacing(0);
		for (int i = 0; i < nk; ++i)
		{
			const int kind = easy ? int(kLevel) : kinds[i];
			QLineEdit* t = new QLineEdit(page);
			t->setAlignment(Qt::AlignLeft);
			t->setMinimumWidth(30);
			m_ch[c].text[kind] = t;
			ht->addWidget(t, 1);
			connect(t, &QLineEdit::editingFinished, this,
				[this, c, kind]() { onTextEdited(c, kind); });
		}
		v->addLayout(ht);
		if (c < 2) v->addSpacing(5);
	}
	return page;
}

QSize AdjustPanel::minimumSizeHint() const
{
	QSize s = QWidget::minimumSizeHint();
	return QSize(std::max(110, s.width()), s.height());
}

QSize AdjustPanel::sizeHint() const
{
	QSize s = QWidget::sizeHint();
	return QSize(std::max(125, s.width()), std::max(400, s.height()));
}


// ---------------------------------------------------------------------------
// conversions (AdjustView.h macros)
// ---------------------------------------------------------------------------

double AdjustPanel::scaleOf(int kind)
{
	return kind == kLum ? 1.0 : 100.0;
}

QString AdjustPanel::format(int kind, double v)
{
	if (kind == kLum) return QString::asprintf("%d", int(v + 0.5));   // Brightness2UIP
	return QString::asprintf("%.2f", v);
}

// GammaUI2 / BrightnessUI2 / HdrUI2 / LevelUI2
double AdjustPanel::toEngine(int kind, double ui)
{
	switch (kind)
	{
	case kGamma: return 1.0 / ui;
	case kLum: return ui / 256.0 + 1.0;
	case kEql: return ui;
	default: return 1.0 - ui;
	}
}

// Gamma2UI / Brightness2UI / Hdr2UI / Level2UI
double AdjustPanel::toUi(int kind, double engine)
{
	switch (kind)
	{
	case kGamma: return 1.0 / engine;
	case kLum: return (engine - 1.0) * 256.0;
	case kEql: return engine;
	default: return 1.0 - engine;
	}
}

namespace {
	FLIVR::Color GetCol(TreeLayer* l, int kind)
	{
		switch (kind)
		{
		case 0: return l->GetGamma();
		case 1: return l->GetBrightness();
		case 2: return l->GetHdr();
		default: return l->GetLevels();
		}
	}
	void SetCol(TreeLayer* l, int kind, const FLIVR::Color& c)
	{
		switch (kind)
		{
		case 0: l->SetGamma(c); break;
		case 1: l->SetBrightness(c); break;
		case 2: l->SetHdr(c); break;
		default: l->SetLevels(c); break;
		}
	}
	FLIVR::Color GetCol(RenderCanvas* v, int kind)
	{
		switch (kind)
		{
		case 0: return v->GetGamma();
		case 1: return v->GetBrightness();
		case 2: return v->GetHdr();
		default: return v->GetLevels();
		}
	}
	void SetCol(RenderCanvas* v, int kind, const FLIVR::Color& c)
	{
		switch (kind)
		{
		case 0: v->SetGamma(c); break;
		case 1: v->SetBrightness(c); break;
		case 2: v->SetHdr(c); break;
		default: v->SetLevels(c); break;
		}
	}
	void SetGroupAll(DataGroup* g, int kind, FLIVR::Color c)
	{
		switch (kind)
		{
		case 0: g->SetGammaAll(c); break;
		case 1: g->SetBrightnessAll(c); break;
		case 2: g->SetHdrAll(c); break;
		default: g->SetLevelsAll(c); break;
		}
	}

	// default_2d_adjustment_settings.dft, same lookup as AdjustView
	VVDString DefaultFilePath(bool for_save)
	{
		const VVDString name(L"default_2d_adjustment_settings.dft");
		return for_save ? vvd::SettingsWritePath(name) : vvd::SettingsReadPath(name);
	}

	const wchar_t* kChanKey[3] = { L"r", L"g", L"b" };
}

TreeLayer* AdjustPanel::layer() const
{
	if (m_type == 2 && m_vd) return m_vd;
	if (m_type == 5 && m_group) return m_group;
	return nullptr;
}

// ---------------------------------------------------------------------------
// targets
// ---------------------------------------------------------------------------

void AdjustPanel::SetRenderCanvas(RenderCanvas* canvas)
{
	if (canvas != m_canvas && canvas)
	{
		// wx: the pane follows the view's blend mode (GetSettings enables the
		// sliders for the view only in Depth mode)
		connect(canvas, &RenderCanvas::volMethodChanged, this, [this, canvas](int) {
			if (m_canvas == canvas && m_type == 1) GetSettings(); });
	}
	m_canvas = canvas;
	if (!canvas)
	{
		m_type = -1;
		setEnabledAll(false);
		return;
	}
	if (!m_view_defaults_applied)
	{
		// VRenderFrame ctor: "set view default settings"
		m_view_defaults_applied = true;
		FLIVR::Color gamma, brightness, hdr, level;
		bool sr, sg, sb;
		GetDefaults(gamma, brightness, hdr, level, sr, sg, sb);
		canvas->SetGamma(gamma);
		canvas->SetBrightness(brightness);
		canvas->SetHdr(hdr);
		canvas->SetLevels(level);
		canvas->SetSyncR(sr);
		canvas->SetSyncG(sg);
		canvas->SetSyncB(sb);
		canvas->SetEasy2DAdjustMode(m_easy);
	}
	m_type = 1;
	GetSettings();
}

void AdjustPanel::SetVolume(VolumeData* vd)
{
	if (vd)
	{
		m_vd = vd;
		m_type = 2;
		GetSettings();
	}
	else
	{
		m_vd = nullptr;
		m_type = -1;
		setEnabledAll(false);
	}
}

void AdjustPanel::SetGroup(DataGroup* group)
{
	if (group)
	{
		m_group = group;
		m_type = 5;
		GetSettings();
	}
	else
	{
		m_type = -1;
		setEnabledAll(false);
	}
}

void AdjustPanel::SetGroupLink(DataGroup* group)
{
	m_group = group;
	m_link_group = group != nullptr;
}

// wx GetSettings
void AdjustPanel::GetSettings()
{
	bool sync[3] = { false, false, false };
	double ui[3][kKinds];
	for (int c = 0; c < 3; ++c)
	{
		ui[c][kGamma] = 1.0; ui[c][kLum] = 0.0; ui[c][kEql] = 0.0; ui[c][kLevel] = 1.0;
	}

	bool have = false;
	if (m_type == 1 && m_canvas)
	{
		sync[0] = m_canvas->GetSyncR(); sync[1] = m_canvas->GetSyncG(); sync[2] = m_canvas->GetSyncB();
		for (int k = 0; k < kKinds; ++k)
		{
			FLIVR::Color col = GetCol(m_canvas, k);
			for (int c = 0; c < 3; ++c) ui[c][k] = toUi(k, col[c]);
		}
		have = true;
	}
	else if (TreeLayer* l = layer())
	{
		sync[0] = l->GetSyncR(); sync[1] = l->GetSyncG(); sync[2] = l->GetSyncB();
		for (int k = 0; k < kKinds; ++k)
		{
			FLIVR::Color col = GetCol(l, k);
			for (int c = 0; c < 3; ++c) ui[c][k] = toUi(k, col[c]);
		}
		have = true;
	}

	if (have)
	{
		for (int c = 0; c < 3; ++c)
		{
			m_sync[c] = sync[c];
			for (int e = 0; e < 2; ++e)
				if (m_ch[c].link[e]) { QSignalBlocker b(m_ch[c].link[e]); m_ch[c].link[e]->setChecked(sync[c]); }
			for (int k = 0; k < kKinds; ++k)
			{
				m_val[c][k] = ui[c][k];
				refreshWidget(c, k);
			}
		}
		if (m_type == 2 || m_type == 5 ||
			(m_type == 1 && m_canvas && m_canvas->GetVolMethod() == RenderCanvas::VolMethodMulti))
			setEnabledAll(true);
		else
			setEnabledAll(false);
	}
	else
		setEnabledAll(false);

	if (m_canvas)
	{
		// m_easy_chk->SetValue(m_glview->GetEasy2DAdjustMode()); OnEasyModeCheck
		const bool easy = m_canvas->GetEasy2DAdjustMode();
		{ QSignalBlocker b(m_easyChk); m_easyChk->setChecked(easy); }
		m_easy = easy;
		showEasyPage(easy);
	}
}

void AdjustPanel::setEnabledAll(bool on)
{
	// wx DisableAll / EnableAll: the Easy Mode box stays usable
	for (int c = 0; c < 3; ++c)
	{
		for (int e = 0; e < 2; ++e)
		{
			if (m_ch[c].link[e]) m_ch[c].link[e]->setEnabled(on);
			if (m_ch[c].reset[e]) m_ch[c].reset[e]->setEnabled(on);
		}
		for (int k = 0; k < kKinds; ++k)
		{
			if (m_ch[c].sldr[k]) m_ch[c].sldr[k]->setEnabled(on);
			if (m_ch[c].text[k]) m_ch[c].text[k]->setEnabled(on);
		}
	}
	if (m_dftBtn) m_dftBtn->setEnabled(on);
}

// ---------------------------------------------------------------------------
// values
// ---------------------------------------------------------------------------

double AdjustPanel::Gamma(int ch) const { return m_val[std::clamp(ch, 0, 2)][kGamma]; }
double AdjustPanel::Luminance(int ch) const { return m_val[std::clamp(ch, 0, 2)][kLum]; }
double AdjustPanel::Equalization(int ch) const { return m_val[std::clamp(ch, 0, 2)][kEql]; }
double AdjustPanel::Level(int ch) const { return m_val[std::clamp(ch, 0, 2)][kLevel]; }
bool AdjustPanel::Linked(int ch) const { return m_sync[std::clamp(ch, 0, 2)]; }

void AdjustPanel::refreshWidget(int ch, int kind)
{
	const bool was = m_updating;
	m_updating = true;
	QSlider* s = m_ch[ch].sldr[kind];
	QLineEdit* t = m_ch[ch].text[kind];
	const double v = m_val[ch][kind];
	if (s)
	{
		QSignalBlocker b(s);
		// wx: Gamma2UIP / Brightness2UIP / Hdr2UIP / Level2UIP
		s->setValue(kind == kLum ? int(v + 0.5) : int(v * 100.0 + 0.5));
	}
	if (t) { QSignalBlocker b(t); t->setText(format(kind, v)); }
	m_updating = was;
}

// wx On{R,G,B}{Gamma,Brightness,Hdr,Level}Text: the channel's box, the linked
// channels' boxes, then the view (m_type 1) and the volume / group target
void AdjustPanel::applyValue(int ch, int kind, double val)
{
	m_val[ch][kind] = val;
	refreshWidget(ch, kind);
	if (m_sync[ch])
		for (int o = 0; o < 3; ++o)
			if (o != ch && m_sync[o])
			{
				m_val[o][kind] = val;
				refreshWidget(o, kind);
			}

	const double ev = toEngine(kind, val);
	if (m_canvas && m_type == 1)
	{
		FLIVR::Color col = GetCol(m_canvas, kind);
		col[ch] = ev;
		if (m_sync[ch])
			for (int o = 0; o < 3; ++o)
				if (o != ch && m_sync[o]) col[o] = ev;
		SetCol(m_canvas, kind, col);
	}
	if (TreeLayer* l = layer())
	{
		FLIVR::Color col = GetCol(l, kind);
		col[ch] = ev;
		if (m_sync[ch])
			for (int o = 0; o < 3; ++o)
				if (o != ch && m_sync[o]) col[o] = ev;
		SetCol(l, kind, col);
		if (m_link_group && m_group)
			SetGroupAll(m_group, kind, col);
	}
}

void AdjustPanel::setValue(int ch, int kind, double v, bool apply)
{
	double lo, hi;
	switch (kind)
	{
	case kGamma: lo = 0.10; hi = 9.99; break;
	case kLum: lo = -256; hi = 256; break;
	case kEql: lo = 0.0; hi = 1.0; break;
	default: lo = -0.99; hi = 0.99; break;
	}
	v = std::clamp(v, lo, hi);
	if (!apply)
	{
		m_val[ch][kind] = v;
		refreshWidget(ch, kind);
		return;
	}
	applyValue(ch, kind, v);
	Q_EMIT adjustmentChanged();
}

void AdjustPanel::SetGamma(int ch, double v) { setValue(std::clamp(ch, 0, 2), kGamma, v, true); }
void AdjustPanel::SetLuminance(int ch, double v) { setValue(std::clamp(ch, 0, 2), kLum, v, true); }
void AdjustPanel::SetEqualization(int ch, double v) { setValue(std::clamp(ch, 0, 2), kEql, v, true); }
void AdjustPanel::SetLevel(int ch, double v) { setValue(std::clamp(ch, 0, 2), kLevel, v, true); }

// wx ChangeRGamma etc.: engine value -> box text -> the text handler
void AdjustPanel::changeValueEngine(int ch, int kind, double engine)
{
	double ui = toUi(kind, engine);
	if (kind == kLum) ui = double(int(std::lround(ui)));   // "%.0f"
	setValue(ch, kind, ui, true);
}

void AdjustPanel::onSliderChanged(int ch, int kind, int pos)
{
	if (m_updating) return;
	// wx On*Change: slider position -> text (-> On*Text)
	setValue(ch, kind, double(pos) / scaleOf(kind), true);
}

void AdjustPanel::onTextEdited(int ch, int kind)
{
	if (m_updating) return;
	bool ok = false;
	const double v = m_ch[ch].text[kind]->text().toDouble(&ok);
	if (!ok) { refreshWidget(ch, kind); return; }
	setValue(ch, kind, v, true);
}

// ---------------------------------------------------------------------------
// links (wx OnSync{R,G,B}Check / ChangeRSync)
// ---------------------------------------------------------------------------

void AdjustPanel::SetLinked(int ch, bool on)
{
	ch = std::clamp(ch, 0, 2);
	m_sync[ch] = on;
	for (int e = 0; e < 2; ++e)
		if (m_ch[ch].link[e]) { QSignalBlocker b(m_ch[ch].link[e]); m_ch[ch].link[e]->setChecked(on); }
	switch (m_type)
	{
	case 1:
		if (m_canvas)
		{
			if (ch == 0) m_canvas->SetSyncR(on);
			else if (ch == 1) m_canvas->SetSyncG(on);
			else m_canvas->SetSyncB(on);
		}
		break;
	case 2:
	case 5:
		if (TreeLayer* l = layer())
		{
			if (ch == 0) l->SetSyncR(on);
			else if (ch == 1) l->SetSyncG(on);
			else l->SetSyncB(on);
		}
		break;
	}
	if ((m_type == 2 || m_type == 5) && m_link_group && m_group)
	{
		if (ch == 0) m_group->SetSyncRAll(on);
		else if (ch == 1) m_group->SetSyncGAll(on);
		else m_group->SetSyncBAll(on);
	}
}

void AdjustPanel::onLinkToggled(int ch, bool on)
{
	SetLinked(ch, on);
}

// wx UpdateSync
void AdjustPanel::UpdateSync()
{
	std::vector<VolumeData*> vols;
	bool from_group = false;
	if ((m_type == 2 && m_link_group && m_group) || (m_type == 5 && m_group))
	{
		for (int i = 0; i < m_group->GetVolumeNum(); i++)
			vols.push_back(m_group->GetVolumeData(i));
		from_group = true;
	}
	else if (m_type == 1 && m_canvas)
	{
		//means this is depth mode
		if (m_canvas->GetVolMethod() != RenderCanvas::VolMethodMulti)
			return;
		vols = m_canvas->PopVolumeList();   // wx GetDispVolumeData
	}
	else
		return;   // a single volume: nothing to link

	bool r_v = false, g_v = false, b_v = false;
	for (VolumeData* vd : vols)
	{
		if (!vd) continue;
		if (vd->GetColormapMode())
			r_v = g_v = b_v = true;
		else
		{
			FLIVR::Color c = vd->GetColor();
			bool r = false, g = false, b = false;
			int cnt = 0;
			if (c.r() > 0) { cnt++; r = true; }
			if (c.g() > 0) { cnt++; g = true; }
			if (c.b() > 0) { cnt++; b = true; }
			if (cnt > 1)
			{
				r_v = r_v || r;
				g_v = g_v || g;
				b_v = b_v || b;
			}
		}
	}
	SetLinked(0, r_v);
	SetLinked(1, g_v);
	SetLinked(2, b_v);

	int cnt = 0;
	if (r_v) cnt++;
	if (g_v) cnt++;
	if (b_v) cnt++;
	if (cnt > 1)
	{
		FLIVR::Color gm, br, hd, lv;
		if (from_group) { gm = m_group->GetGamma(); br = m_group->GetBrightness(); hd = m_group->GetHdr(); lv = m_group->GetLevels(); }
		else { gm = m_canvas->GetGamma(); br = m_canvas->GetBrightness(); hd = m_canvas->GetHdr(); lv = m_canvas->GetLevels(); }
		double gamma = 1.0, brightness = 1.0, hdr = 0.0, level = 1.0;
		if (r_v) { gamma = gm.r(); brightness = br.r(); hdr = hd.r(); level = lv.r(); }
		else if (g_v) { gamma = gm.g(); brightness = br.g(); hdr = hd.g(); level = lv.g(); }
		for (int c = 1; c < 3; ++c)
		{
			if ((c == 1 && g_v) || (c == 2 && b_v))
			{
				changeValueEngine(c, kGamma, gamma);
				changeValueEngine(c, kLum, brightness);
				changeValueEngine(c, kEql, hdr);
				changeValueEngine(c, kLevel, level);
			}
		}
	}
	Q_EMIT adjustmentChanged();
}

// ---------------------------------------------------------------------------
// reset (wx On{R,G,B}Reset)
// ---------------------------------------------------------------------------

void AdjustPanel::onReset(int ch)
{
	for (int kind = kGamma; kind <= kLevel; ++kind)
	{
		// wx: 1.0 / 0 / 0 without defaults; the level falls through with the
		// hdr value (0) -> levels 1.0
		double dft_value = kind == kGamma ? 1.0 : 0.0;
		if (m_use_dft_settings)
			dft_value = m_dft[ch][kind];

		m_val[ch][kind] = dft_value;
		refreshWidget(ch, kind);
		if (m_sync[ch])
			for (int o = 0; o < 3; ++o)
				if (o != ch && m_sync[o])
				{
					m_val[o][kind] = dft_value;
					refreshWidget(o, kind);
				}

		const double ev = toEngine(kind, dft_value);
		// wx resets the view's values whenever a view is set, then the layer
		if (m_canvas)
		{
			FLIVR::Color col = GetCol(m_canvas, kind);
			col[ch] = ev;
			if (m_sync[ch])
				for (int o = 0; o < 3; ++o)
					if (o != ch && m_sync[o]) col[o] = ev;
			SetCol(m_canvas, kind, col);
		}
		if (TreeLayer* l = layer())
		{
			FLIVR::Color col = GetCol(l, kind);
			col[ch] = ev;
			if (m_sync[ch])
				for (int o = 0; o < 3; ++o)
					if (o != ch && m_sync[o]) col[o] = ev;
			SetCol(l, kind, col);
			if (m_link_group && m_group)
				SetGroupAll(m_group, kind, col);
		}
	}
	Q_EMIT adjustmentChanged();
}

void AdjustPanel::ResetAll()
{
	for (int c = 0; c < 3; ++c)
		onReset(c);
}

// ---------------------------------------------------------------------------
// defaults (wx OnSaveDefault / LoadSettings / GetDefaults)
// ---------------------------------------------------------------------------

void AdjustPanel::onSetDefault()
{
	SaveDefault();
}

void AdjustPanel::SaveDefault()
{
	vvd::IniFile fconfig;
	const wchar_t* names[kKinds] = { L"_gamma_text", L"_brightness_text", L"_hdr_text", L"_level_text" };
	for (int c = 0; c < 3; ++c)
	{
		fconfig.Write(VVDString(L"sync_") + kChanKey[c] + L"_chk", m_sync[c]);
		for (int k = 0; k < kKinds; ++k)
		{
			// wx reads the text boxes back (so the saved value is the shown one)
			double v = m_ch[c].text[k] ? m_ch[c].text[k]->text().toDouble() : m_val[c][k];
			fconfig.Write(VVDString(kChanKey[c]) + names[k], v);
			m_dft[c][k] = v;
		}
	}
	fconfig.Write(L"easy_mode", m_easyChk->isChecked());
	fconfig.Save(DefaultFilePath(true));
}

void AdjustPanel::LoadSettings()
{
	const VVDString dft = DefaultFilePath(false);
	vvd::IniFile fconfig;
	if (!vvd::FileExists(dft) || !fconfig.Load(dft))
		return;

	const wchar_t* names[kKinds] = { L"_gamma_text", L"_brightness_text", L"_hdr_text", L"_level_text" };
	const double fallback[kKinds] = { 1.0, 0.0, 0.0, 0.0 };
	for (int c = 0; c < 3; ++c)
	{
		bool bval;
		if (fconfig.Read(VVDString(L"sync_") + kChanKey[c] + L"_chk", &bval))
			m_dftSync[c] = bval;
		for (int k = 0; k < kKinds; ++k)
		{
			double v = fallback[k];
			fconfig.Read(VVDString(kChanKey[c]) + names[k], &v);
			m_dft[c][k] = v;
		}
	}
	bool easy;
	if (fconfig.Read(L"easy_mode", &easy))
	{
		{ QSignalBlocker b(m_easyChk); m_easyChk->setChecked(easy); }
		onEasyToggled(easy);
	}
	m_use_dft_settings = true;
}

void AdjustPanel::GetDefaults(FLIVR::Color& gamma, FLIVR::Color& brightness, FLIVR::Color& hdr, FLIVR::Color& level,
	bool& sync_r, bool& sync_g, bool& sync_b) const
{
	for (int c = 0; c < 3; ++c)
	{
		gamma[c] = toEngine(kGamma, m_dft[c][kGamma]);
		brightness[c] = toEngine(kLum, m_dft[c][kLum]);
		hdr[c] = m_dft[c][kEql];
		level[c] = toEngine(kLevel, m_dft[c][kLevel]);
	}
	sync_r = m_dftSync[0];
	sync_g = m_dftSync[1];
	sync_b = m_dftSync[2];
}

void AdjustPanel::ApplyDefaults(VolumeData* vd) const
{
	if (!vd) return;
	FLIVR::Color gamma, brightness, hdr, level;
	bool sr, sg, sb;
	GetDefaults(gamma, brightness, hdr, level, sr, sg, sb);
	vd->SetGamma(gamma);
	vd->SetBrightness(brightness);
	vd->SetHdr(hdr);
	vd->SetLevels(level);
	vd->SetSyncR(sr);
	vd->SetSyncG(sg);
	vd->SetSyncB(sb);
}

// ---------------------------------------------------------------------------
// easy mode (wx OnEasyModeCheck)
// ---------------------------------------------------------------------------

void AdjustPanel::showEasyPage(bool on)
{
	m_defaultPage->setVisible(!on);
	m_easyPage->setVisible(on);
	updateGeometry();
}

void AdjustPanel::onEasyToggled(bool on)
{
	m_easy = on;
	if (m_canvas)
		m_canvas->SetEasy2DAdjustMode(on);
	showEasyPage(on);
	Q_EMIT adjustmentChanged();
}
