#include "ClipPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEnterEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cfloat>
#include <cmath>

#include "RenderCanvas.h"
#include "DataManager.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/MeshRenderer.h"
#include "FLIVR/Plane.h"
#include "FLIVR/Transform.h"
#include "Core/FileUtil.h"
#include "TestRun.h"

using namespace FLIVR;

// ---------------------------------------------------------------------------
// DualSlider: vertical range slider with two thumbs; min at top, max at
// bottom (like the two side-by-side wx sliders). Coloured track. ',' / '.'
// move a linked pair (wx OnSliderKeyDown).
// ---------------------------------------------------------------------------
class DualSlider : public QWidget
{
	Q_OBJECT
public:
	DualSlider(const QColor& strong, const QColor& light, int axis, QWidget* parent)
		: QWidget(parent), m_strong(strong), m_light(light), m_axis(axis)
	{
		setMinimumSize(30, 40);
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
		setFocusPolicy(Qt::ClickFocus);
	}
	int lo() const { return m_lo; }
	int hi() const { return m_hi; }
	void setBounds(int mn, int mx) { m_min = mn; m_max = std::max(mx, mn + 1); update(); }
	void setValues(int lo, int hi)
	{
		m_lo = std::clamp(lo, m_min, m_max);
		m_hi = std::clamp(hi, m_lo, m_max);
		update();
	}
	void setLinked(bool l) { m_linked = l; }
	QSize sizeHint() const override { return QSize(36, 120); }

Q_SIGNALS:
	void rangeChanged(int axis, int lo, int hi, bool lowMoved);
	void keyMove(int axis, int dir);
	void rightClicked(int axis, int which);   // which: 0 low thumb, 1 high thumb

protected:
	static constexpr int kPad = 8;
	int toY(int v) const
	{
		double t = double(v - m_min) / double(m_max - m_min);
		return kPad + int(t * (height() - 2 * kPad) + 0.5);
	}
	int fromY(int y) const
	{
		double t = double(y - kPad) / double(std::max(1, height() - 2 * kPad));
		return std::clamp(m_min + int(t * (m_max - m_min) + 0.5), m_min, m_max);
	}
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		int cx = width() / 2;
		int y0 = toY(m_min), y1 = toY(m_max);
		p.setPen(Qt::NoPen);
		p.setBrush(m_light);
		p.drawRoundedRect(QRect(cx - 2, y0, 4, y1 - y0), 2, 2);
		p.setBrush(m_strong);
		p.drawRect(QRect(cx - 2, toY(m_lo), 4, std::max(1, toY(m_hi) - toY(m_lo))));
		for (int i = 0; i < 2; ++i) {
			int y = toY(i == 0 ? m_lo : m_hi);
			p.setPen(QPen(QColor(150, 150, 150), 1));
			p.setBrush(isEnabled() ? QColor(250, 250, 250) : QColor(225, 225, 225));
			p.drawEllipse(QPoint(cx, y), 6, 6);
		}
	}
	void mousePressEvent(QMouseEvent* e) override
	{
		int y = int(e->position().y());
		const int thumb = (std::abs(y - toY(m_lo)) <= std::abs(y - toY(m_hi))) ? 1 : 2;
		if (e->button() == Qt::RightButton)
		{
			// wx OnSliderRClick on the x1 (low) / x2 (high) slider: the
			// thumb nearest to the click stands for the wx slider
			int which = thumb;
			if (m_lo == m_hi) which = (y < toY(m_lo)) ? 1 : 2;
			Q_EMIT rightClicked(m_axis, which - 1);
			return;
		}
		m_drag = thumb;
		if (m_lo == m_hi) m_drag = (y < toY(m_lo)) ? 1 : 2;
		moveDrag(y);
	}
	void mouseMoveEvent(QMouseEvent* e) override { if (m_drag) moveDrag(int(e->position().y())); }
	void mouseReleaseEvent(QMouseEvent*) override { m_drag = 0; }
	void keyPressEvent(QKeyEvent* e) override
	{
		if (e->key() == Qt::Key_Comma) { Q_EMIT keyMove(m_axis, 0); return; }
		if (e->key() == Qt::Key_Period) { Q_EMIT keyMove(m_axis, 1); return; }
		QWidget::keyPressEvent(e);
	}
	void moveDrag(int y)
	{
		int v = fromY(y);
		int olo = m_lo, ohi = m_hi;
		if (m_drag == 1) {
			if (m_linked) { int d = m_hi - m_lo; m_lo = std::min(v, m_max - d); m_hi = m_lo + d; }
			else m_lo = std::min(v, m_hi);
		} else {
			if (m_linked) { int d = m_hi - m_lo; m_hi = std::max(v, m_min + d); m_lo = m_hi - d; }
			else m_hi = std::max(v, m_lo);
		}
		if (olo != m_lo || ohi != m_hi) {
			update();
			Q_EMIT rangeChanged(m_axis, m_lo, m_hi, m_drag == 1);
		}
	}

private:
	QColor m_strong, m_light;
	int m_axis;
	int m_min = 0, m_max = 512, m_lo = 0, m_hi = 512;
	int m_drag = 0;
	bool m_linked = false;
};

// ---------------------------------------------------------------------------

namespace {
const char* kAxisName[3] = {"X", "Y", "Z"};

// ClippingView.h
constexpr int PM_LOWTRANSBACK = 3;
constexpr int PM_NORMALBACK = 4;

QFrame* MakeBar(QWidget* parent, const QColor& c, int h = 5)
{
	QFrame* f = new QFrame(parent);
	f->setFixedHeight(h);
	f->setStyleSheet(QString("background:%1;").arg(c.name()));
	return f;
}

// ClippingView.cpp NormalizeAngle
double NormalizeAngle(double angle)
{
	angle = std::fmod(angle + 180.0, 360.0);
	if (angle < 0)
		angle += 360.0;
	return angle - 180.0;
}

void SetUnitPlanes(std::vector<Plane*>* planes)
{
	(*planes)[0]->ChangePlane(Point(0.0, 0.0, 0.0), Vector(1.0, 0.0, 0.0));
	(*planes)[1]->ChangePlane(Point(1.0, 0.0, 0.0), Vector(-1.0, 0.0, 0.0));
	(*planes)[2]->ChangePlane(Point(0.0, 0.0, 0.0), Vector(0.0, 1.0, 0.0));
	(*planes)[3]->ChangePlane(Point(0.0, 1.0, 0.0), Vector(0.0, -1.0, 0.0));
	(*planes)[4]->ChangePlane(Point(0.0, 0.0, 0.0), Vector(0.0, 0.0, 1.0));
	(*planes)[5]->ChangePlane(Point(0.0, 0.0, 1.0), Vector(0.0, 0.0, -1.0));
	for (int i = 0; i < 6; i += 2)
	{
		(*planes)[i]->SetRange((*planes)[i]->get_point(), (*planes)[i]->normal(), (*planes)[i + 1]->get_point(), (*planes)[i + 1]->normal());
		(*planes)[i + 1]->SetRange((*planes)[i + 1]->get_point(), (*planes)[i + 1]->normal(), (*planes)[i]->get_point(), (*planes)[i]->normal());
	}
}

// default_clip_settings.dft, same lookup as ClippingView
VVDString ClipDefaultFile(bool for_save)
{
	const VVDString name(L"default_clip_settings.dft");
	return for_save ? vvd::SettingsWritePath(name) : vvd::SettingsReadPath(name);
}
}

ClipPanel::ClipPanel(QWidget* parent) : QWidget(parent)
{
	setObjectName("ClipPanel");
	setMinimumWidth(150);
	setStyleSheet(
		"#ClipPanel { background:#ececec; }"
		"#ClipPanel QLabel, #ClipPanel QCheckBox { font-size:11px; color:#222; }"
		"#ClipPanel QPushButton { font-size:11px; padding:1px 4px; min-height:18px; max-height:20px;"
		"  background:#ffffff; border:1px solid #c4c4c4; border-radius:3px; }"
		"#ClipPanel QPushButton:pressed { background:#dcdcdc; }"
		"#ClipPanel QPushButton:disabled { color:#999; }"
		"#ClipPanel QComboBox { font-size:11px; min-height:18px; max-height:20px; background:#fff; }"
		"#ClipPanel QSpinBox, #ClipPanel QDoubleSpinBox { font-size:11px; padding:0 1px; min-height:16px;"
		"  max-height:20px; background:#fff; border:1px solid #c4c4c4; }"
		"#ClipPanel QSlider::groove:vertical { width:4px; background:#9ab8e8; border-radius:2px; }"
		"#ClipPanel QSlider::handle:vertical { height:12px; width:12px; margin:0 -5px; border-radius:6px;"
		"  background:#fafafa; border:1px solid #969696; }"
		"#ClipPanel QSpinBox[spinOnly=\"true\"] { color:transparent; border:none; background:transparent; }");

	auto small = [](QWidget* w) { w->setAttribute(Qt::WA_MacSmallSize, true); };

	QVBoxLayout* v = new QVBoxLayout(this);
	v->setContentsMargins(5, 4, 5, 4);
	v->setSpacing(3);

	// wx ctor: both unchecked
	m_syncChan = new QCheckBox("Sync All Chan.", this);
	m_syncGroup = new QCheckBox("Sync Group", this);
	v->addWidget(m_syncChan, 0, Qt::AlignLeft);
	v->addWidget(m_syncGroup, 0, Qt::AlignLeft);

	QLabel* dm = new QLabel("Display Mode:", this);
	v->addWidget(dm, 0, Qt::AlignHCenter);
	m_mode = new QComboBox(this);
	m_mode->addItems({"Normal", "Frame", "LowTrans", "LowTransBack", "NormalBack"});
	m_mode->setCurrentIndex(m_plane_mode);
	m_mode->setMinimumWidth(90);
	m_mode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	small(m_mode);
	v->addWidget(m_mode);

	m_clipReset = new QPushButton("Reset Clipping Planes", this);
	v->addWidget(m_clipReset);

	// three clipping columns
	QColor strong[3] = {QColor(255, 80, 80), QColor(110, 230, 110), QColor(100, 100, 255)};
	QColor light[3] = {QColor(255, 170, 170), QColor(190, 255, 130), QColor(150, 150, 255)};
	QColor topBar[3] = {QColor(255, 128, 128), QColor(128, 255, 128), QColor(128, 128, 255)};
	QColor botBar[3] = {QColor(255, 128, 255), QColor(255, 255, 128), QColor(128, 255, 255)};

	QHBoxLayout* cols = new QHBoxLayout();
	cols->setSpacing(2);
	cols->setContentsMargins(0, 0, 0, 0);
	for (int a = 0; a < 3; ++a) {
		QVBoxLayout* c = new QVBoxLayout();
		c->setSpacing(2);
		c->setContentsMargins(0, 0, 0, 0);
		QLabel* name = new QLabel(kAxisName[a], this);
		c->addWidget(name, 0, Qt::AlignHCenter);
		m_minBox[a] = new QDoubleSpinBox(this);
		m_maxBox[a] = new QDoubleSpinBox(this);
		for (QDoubleSpinBox* b : {m_minBox[a], m_maxBox[a]}) {
			b->setButtonSymbols(QAbstractSpinBox::NoButtons);
			b->setKeyboardTracking(false);
			b->setDecimals(0);
			b->setRange(0, 512);
			b->setMinimumWidth(30);
			b->setMaximumHeight(20);
		}
		m_maxBox[a]->setValue(512);
		c->addWidget(m_minBox[a]);
		c->addWidget(MakeBar(this, topBar[a]));
		m_dual[a] = new DualSlider(strong[a], light[a], a, this);
		m_dual[a]->setBounds(0, 512);
		m_dual[a]->setValues(0, 512);
		c->addWidget(m_dual[a], 1);
		c->addWidget(MakeBar(this, botBar[a]));
		c->addWidget(m_maxBox[a]);
		m_linkChk[a] = new QCheckBox(this);
		QLabel* lk = new QLabel("Link", this);
		c->addSpacing(4);
		c->addWidget(m_linkChk[a], 0, Qt::AlignHCenter);
		c->addWidget(lk, 0, Qt::AlignHCenter);
		cols->addLayout(c, 1);
	}
	v->addLayout(cols, 3);

	QHBoxLayout* btns = new QHBoxLayout();
	btns->setSpacing(5);
	QHBoxLayout* dists = new QHBoxLayout();
	dists->setSpacing(5);
	const char* pn[3] = {"YZ", "XZ", "XY"};
	for (int a = 0; a < 3; ++a) {
		m_planeBtn[a] = new QPushButton(pn[a], this);
		m_planeBtn[a]->setMinimumWidth(28);
		btns->addWidget(m_planeBtn[a], 1);
		m_distBox[a] = new QSpinBox(this);
		m_distBox[a]->setButtonSymbols(QAbstractSpinBox::NoButtons);
		m_distBox[a]->setRange(0, 100000);
		m_distBox[a]->setValue(1);
		m_distBox[a]->setMinimumWidth(28);
		m_distBox[a]->setKeyboardTracking(false);
		dists->addWidget(m_distBox[a], 1);
	}
	v->addLayout(btns);
	v->addLayout(dists);

	QFrame* sep = MakeBar(this, QColor(100, 100, 100), 5);
	v->addSpacing(6);
	v->addWidget(sep);
	v->addSpacing(4);

	v->addWidget(new QLabel("Clipping Plane Rotations:", this), 0, Qt::AlignHCenter);
	m_fix = new QCheckBox("Fix", this);
	m_display = new QCheckBox("Display Planes", this);
	v->addWidget(m_fix, 0, Qt::AlignHCenter);
	v->addWidget(m_display, 0, Qt::AlignHCenter);
	m_align = new QPushButton("Align to View", this);
	m_rotReset = new QPushButton("Reset to 0", this);
	m_align->setMinimumWidth(80);
	m_rotReset->setMinimumWidth(70);
	v->addWidget(m_align, 0, Qt::AlignHCenter);
	v->addWidget(m_rotReset, 0, Qt::AlignHCenter);

	QHBoxLayout* rots = new QHBoxLayout();
	rots->setSpacing(2);
	rots->setContentsMargins(0, 0, 0, 0);
	for (int a = 0; a < 3; ++a) {
		QVBoxLayout* c = new QVBoxLayout();
		c->setSpacing(2);
		c->setContentsMargins(0, 0, 0, 0);
		m_rotBox[a] = new QDoubleSpinBox(this);
		m_rotBox[a]->setButtonSymbols(QAbstractSpinBox::NoButtons);
		m_rotBox[a]->setRange(-180.0, 180.0);
		m_rotBox[a]->setDecimals(1);
		m_rotBox[a]->setKeyboardTracking(false);
		m_rotBox[a]->setMinimumWidth(30);
		m_rotSpin[a] = new QSpinBox(this);
		m_rotSpin[a]->setRange(-1000000, 1000000);
		m_rotSpin[a]->setProperty("spinOnly", true);
		m_rotSpin[a]->setFixedWidth(22);
		m_rotSpin[a]->setSuffix("");
		m_rotSldr[a] = new QSlider(Qt::Vertical, this);
		m_rotSldr[a]->setRange(-180, 180);
		m_rotSldr[a]->setMinimumHeight(40);
		c->addWidget(m_rotBox[a]);
		c->addWidget(new QLabel(kAxisName[a], this), 0, Qt::AlignHCenter);
		c->addWidget(m_rotSpin[a], 0, Qt::AlignHCenter);
		c->addWidget(m_rotSldr[a], 1, Qt::AlignHCenter);
		rots->addLayout(c, 1);
	}
	v->addLayout(rots, 2);

	// wiring
	for (int a = 0; a < 3; ++a) {
		connect(m_dual[a], &DualSlider::rangeChanged, this, &ClipPanel::OnSliderMoved);
		connect(m_dual[a], &DualSlider::keyMove, this, &ClipPanel::OnSliderKey);
		connect(m_dual[a], &DualSlider::rightClicked, this, &ClipPanel::OnSliderRClick);
		connect(m_minBox[a], &QDoubleSpinBox::editingFinished, this, [this, a] {
			if (!m_blocked) EditPlane(a, 0, m_minBox[a]->value()); });
		connect(m_maxBox[a], &QDoubleSpinBox::editingFinished, this, [this, a] {
			if (!m_blocked) EditPlane(a, 1, m_maxBox[a]->value()); });
		connect(m_linkChk[a], &QCheckBox::clicked, this, [this, a](bool on) { OnLinkToggled(a, on); });
		connect(m_planeBtn[a], &QPushButton::clicked, this, [this, a] { OnPlaneButton(a); });
		connect(m_rotBox[a], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
			[this, a](double d) { OnRotEdit(a, d); });
		// wx OnXRotChange: slider -> "%.1f" text -> OnXRotEdit
		connect(m_rotSldr[a], &QSlider::valueChanged, this, [this, a](int d) {
			if (!m_blocked) m_rotBox[a]->setValue(d); });
		connect(m_rotSpin[a], QOverload<int>::of(&QSpinBox::valueChanged), this,
			[this, a](int d) { OnRotSpin(a, d); });
	}
	connect(m_clipReset, &QPushButton::clicked, this, &ClipPanel::OnClipReset);
	connect(m_rotReset, &QPushButton::clicked, this, &ClipPanel::OnRotReset);
	connect(m_align, &QPushButton::clicked, this, &ClipPanel::OnAlign);
	connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ClipPanel::OnPlaneMode);
	connect(m_fix, &QCheckBox::clicked, this, &ClipPanel::OnFix);
	connect(m_display, &QCheckBox::toggled, this, &ClipPanel::OnHold);
	connect(m_syncChan, &QCheckBox::clicked, this, &ClipPanel::OnSyncChannels);
	connect(m_syncGroup, &QCheckBox::clicked, this, &ClipPanel::OnSyncGroup);

	SetEnabledAll(false);
	LoadDefault();
}

// wx ~ClippingView: SaveDefault (not in scripted test runs, TestRun.h)
ClipPanel::~ClipPanel()
{
	if (!VVDQtScriptedRun())
		SaveDefault();
}

// wx GetXdist / GetYdist / GetZdist: |x2 - x1| of the value texts read with
// ToLong (the integer part of "%.2f" when the factor is not 1)
int ClipPanel::GetDist(int a) const
{
	if (a < 0 || a > 2) return 0;
	const long v1 = long(m_minBox[a]->value());
	const long v2 = long(m_maxBox[a]->value());
	return int(std::labs(v2 - v1));
}

// wx OnSliderRClick: a right click on the low (x1) slider sets x2 = x1 + 1,
// on the high (x2) slider x1 = x2 - 1, and links the axis (a one-voxel slab
// that 's' / 'w' / ',' / '.' then step through); the other two axes are
// unlinked and reset to their full range. As wx, the new value (box units,
// voxels * factor) is compared with the voxel resolution.
void ClipPanel::OnSliderRClick(int a, int which)
{
	if (!Layer())
		return;
	double bd[3] = {0.0, 0.0, 0.0};
	CalcBoundingBoxDimensions(bd[0], bd[1], bd[2]);
	int res[3];
	for (int i = 0; i < 3; ++i)
		res[i] = int(std::round(bd[i]));

	if (which == 0)
	{
		double val = ((int)(m_minBox[a]->value() / m_factor[a] + 0.5) + 1.0) * m_factor[a];
		if (val < res[a])
		{
			SetLink(a, false);
			EditPlane(a, 1, val);
			SetLink(a, true);
		}
	}
	else
	{
		double val = ((int)(m_maxBox[a]->value() / m_factor[a] + 0.5) - 1.0) * m_factor[a];
		if (val > 0)
		{
			SetLink(a, false);
			EditPlane(a, 0, val);
			SetLink(a, true);
		}
	}
	m_dual[a]->setFocus();

	for (int b = 0; b < 3; ++b)
	{
		if (b == a)
			continue;
		// wx: the target's ClippingLink off, m_link_* = false, check box off
		SetLink(b, false);
		EditPlane(b, 0, 0.0);
		EditPlane(b, 1, res[b] * m_factor[b]);
	}
}

// ---------------------------------------------------------------------------
// enable / targets
// ---------------------------------------------------------------------------

void ClipPanel::SetEnabledAll(bool on)
{
	for (int a = 0; a < 3; ++a) {
		for (QWidget* w : std::initializer_list<QWidget*>{m_dual[a], m_minBox[a], m_maxBox[a],
			m_linkChk[a], m_planeBtn[a], m_distBox[a]})
			w->setEnabled(on);
	}
	for (QWidget* w : std::initializer_list<QWidget*>{m_syncChan, m_syncGroup, m_mode, m_clipReset,
		m_fix, m_display})
		w->setEnabled(on);
	SetRotationsEnabled(on && !m_fix_rots);
}

// wx EnableRotations / DisableRotations
void ClipPanel::SetRotationsEnabled(bool on)
{
	for (int a = 0; a < 3; ++a)
		for (QWidget* w : std::initializer_list<QWidget*>{m_rotBox[a], m_rotSpin[a], m_rotSldr[a]})
			w->setEnabled(on);
	m_align->setEnabled(on);
	m_rotReset->setEnabled(on);
}

void ClipPanel::SetRenderCanvas(RenderCanvas* canvas)
{
	m_canvas = canvas;
	UpdateCanvasDisplay();
}

void ClipPanel::SetVolume(VolumeData* vd)
{
	if (!vd) { if (m_sel_type == 2) ClearData(); return; }
	// wx ClippingView::SetVolumeData: the volume's DataGroup ("Sync Group")
	m_group = m_canvas ? m_canvas->GetParentGroup(vd) : nullptr;
	if (m_group)
	{
		QSignalBlocker b(m_syncGroup);
		m_syncGroup->setChecked(m_group->GetSyncClippingPlanes());
	}
	m_vd = vd;
	m_md = nullptr;
	m_sel_type = 2;
	GetSettings();
}

void ClipPanel::SetMeshData(MeshData* md)
{
	if (!md) { if (m_sel_type == 3) ClearData(); return; }
	// wx ClippingView::SetMeshData: the mesh's MeshGroup
	m_group = m_canvas ? m_canvas->GetParentMGroup(md) : nullptr;
	if (m_group)
	{
		QSignalBlocker b(m_syncGroup);
		m_syncGroup->setChecked(m_group->GetSyncClippingPlanes());
	}
	m_md = md;
	m_vd = nullptr;
	m_sel_type = 3;
	GetSettings();
}

void ClipPanel::SetGroup(ClippingLayer* group)
{
	m_group = group;
	if (m_group)
	{
		QSignalBlocker b(m_syncGroup);
		m_syncGroup->setChecked(m_group->GetSyncClippingPlanes());
	}
}

void ClipPanel::ClearData()
{
	m_vd = nullptr;
	m_md = nullptr;
	m_sel_type = -1;
	SetEnabledAll(false);
	UpdateCanvasDisplay();
}

std::vector<Plane*>* ClipPanel::Planes() const
{
	std::vector<Plane*>* planes = nullptr;
	if (m_sel_type == 2 && m_vd && m_vd->GetVR())
		planes = m_vd->GetVR()->get_planes();
	else if (m_sel_type == 3 && m_md && m_md->GetMR())
		planes = m_md->GetMR()->get_planes();
	return (planes && planes->size() == 6) ? planes : nullptr;
}

ClippingLayer* ClipPanel::Layer() const
{
	if (m_sel_type == 2 && m_vd) return m_vd;
	if (m_sel_type == 3 && m_md) return m_md;
	return nullptr;
}

bool ClipPanel::ViewSync() const { return m_canvas && m_canvas->GetSyncClippingPlanes(); }
bool ClipPanel::GroupSync() const { return m_group && m_group->GetSyncClippingPlanes(); }

void ClipPanel::UpdateCanvasDisplay()
{
	if (m_canvas)
		m_canvas->SetClipDisplay(m_plane_mode, m_syncChan->isChecked(), m_sel_type, m_vd, m_md);
}

// wx RefreshVRenderViews / RefreshVRenderViewsOverlay
void ClipPanel::RefreshViews(bool full)
{
	if (m_canvas)
	{
		if (full) m_canvas->RefreshView();
		else m_canvas->RefreshOverlays();
	}
	Q_EMIT clipChanged();
}

void ClipPanel::ReapplyViewRotations()
{
	if (!m_canvas) return;
	double rx, ry, rz;
	m_canvas->GetRotations(rx, ry, rz);
	m_canvas->SetRotations(rx, ry, rz);
}

void ClipPanel::ReapplyClipRotations()
{
	if (!m_canvas) return;
	double rx, ry, rz;
	m_canvas->GetClippingPlaneRotations(rx, ry, rz);
	m_canvas->SetClippingPlaneRotations(rx, ry, rz);
}

// ---------------------------------------------------------------------------
// wx CalcBoundingBoxDemensions
// ---------------------------------------------------------------------------

void ClipPanel::CalcBoundingBoxDimensions(double& w, double& h, double& d)
{
	double min_spcx = DBL_MAX, min_spcy = DBL_MAX, min_spcz = DBL_MAX;
	double max_w = 0.0, max_h = 0.0, max_d = 0.0;
	bool linked = false;

	std::vector<VolumeData*> vols;
	if (ViewSync() && m_canvas)
		vols = m_canvas->Volumes();
	else if (GroupSync())
	{
		if (DataGroup* group = dynamic_cast<DataGroup*>(m_group))
			for (int i = 0; i < group->GetVolumeNum(); i++)
				vols.push_back(group->GetVolumeData(i));
	}
	for (VolumeData* vd : vols)
	{
		if (!vd) continue;
		double spcx = DBL_MAX, spcy = DBL_MAX, spcz = DBL_MAX;
		vd->GetSpacings(spcx, spcy, spcz, 0);
		min_spcx = std::min(min_spcx, spcx);
		min_spcy = std::min(min_spcy, spcy);
		min_spcz = std::min(min_spcz, spcz);
		int vw = 0, vh = 0, vz = 0;
		vd->GetResolution(vw, vh, vz);
		max_w = std::max(max_w, double(vw));
		max_h = std::max(max_h, double(vh));
		max_d = std::max(max_d, double(vz));
		linked = true;
	}
	if (min_spcx == DBL_MAX) min_spcx = 1.0;
	if (min_spcy == DBL_MAX) min_spcy = 1.0;
	if (min_spcz == DBL_MAX) min_spcz = 1.0;

	std::vector<Plane*>* planes = Planes();
	if (!planes)
		return;
	Transform tform;
	tform.load_identity();
	if (m_sel_type == 2)
	{
		if (!m_vd->GetTexture() || !m_vd->GetTexture()->transform())
			return;
		tform = *m_vd->GetTexture()->transform();
	}
	else
	{
		BBox bb = m_md->GetBounds();
		Vector sc(bb.max().x() - bb.min().x(), bb.max().y() - bb.min().y(), bb.max().z() - bb.min().z());
		tform.post_scale(sc);
	}

	auto plane_dims = [&](double dims[3]) {
		for (int j = 0; j < 3; j++)
		{
			double param = (*planes)[2 * j]->GetParam();
			(*planes)[2 * j]->SetParam(0.0);
			Point p0 = (*planes)[2 * j]->get_point();
			(*planes)[2 * j]->SetParam(1.0);
			Point p1 = (*planes)[2 * j]->get_point();
			(*planes)[2 * j]->SetParam(param);
			p0 = tform.project(p0);
			p1 = tform.project(p1);
			dims[j] = (p1 - p0).length();
		}
	};

	if (m_sel_type == 2)
	{
		int resx, resy, resz;
		m_vd->GetResolution(resx, resy, resz);
		if (linked)
		{
			double mvmat[16];
			tform.get_trans(mvmat);
			std::swap(mvmat[3], mvmat[12]);
			std::swap(mvmat[7], mvmat[13]);
			std::swap(mvmat[11], mvmat[14]);
			tform.set(mvmat);
			double dims[3];
			plane_dims(dims);
			w = std::max(max_w, dims[0] / min_spcx);
			h = std::max(max_h, dims[1] / min_spcy);
			d = std::max(max_d, dims[2] / min_spcz);
		}
		else
		{
			w = resx;
			h = resy;
			d = resz;
		}
	}
	else
	{
		Vector dim = m_md->GetBounds().diagonal();
		if (linked)
		{
			double dims[3];
			plane_dims(dims);
			w = dims[0] / min_spcx;
			h = dims[1] / min_spcy;
			d = dims[2] / min_spcz;
		}
		else
		{
			w = (int)dim.x();
			h = (int)dim.y();
			d = (int)dim.z();
		}
	}
	w = w > 0 ? w : 1;
	h = h > 0 ? h : 1;
	d = d > 0 ? d : 1;
}

// wx ClippingView::CalcAndSetCombinedClippingPlanes
void ClipPanel::CalcAndSetCombinedClippingPlanes()
{
	if (m_canvas)
		m_canvas->CalcAndSetCombinedClippingPlanes();
	GetSettings();
}

// wx SyncClippingPlanes
void ClipPanel::SyncClippingPlanes()
{
	if (!Layer())
		return;
	CalcAndSetCombinedClippingPlanes();
	GetSettings();
	RefreshViews();
}

// ---------------------------------------------------------------------------
// wx GetSettings
// ---------------------------------------------------------------------------

QString ClipPanel::FormatValue(int a, double v) const
{
	return m_factor[a] == 1.0 ? QString::asprintf("%.0f", v) : QString::asprintf("%.2f", v);
}

void ClipPanel::SetRangeUi(int a)
{
	const bool was = m_blocked;
	m_blocked = true;
	m_dual[a]->setBounds(0, m_res[a]);
	m_dual[a]->setValues(m_lo[a], m_hi[a]);
	m_dual[a]->setLinked(m_link[a]);
	for (QDoubleSpinBox* b : {m_minBox[a], m_maxBox[a]})
	{
		QSignalBlocker bl(b);
		b->setDecimals(m_factor[a] == 1.0 ? 0 : 2);
		b->setRange(0.0, std::max(m_res[a] * m_factor[a], 1.0));
	}
	{ QSignalBlocker bl(m_minBox[a]); m_minBox[a]->setValue(m_lo[a] * m_factor[a]); }
	{ QSignalBlocker bl(m_maxBox[a]); m_maxBox[a]->setValue(m_hi[a] * m_factor[a]); }
	m_blocked = was;
}

void ClipPanel::GetSettings()
{
	ClippingLayer* clipping_data = Layer();
	std::vector<Plane*>* planes = Planes();
	if (!clipping_data)
	{
		SetEnabledAll(false);
		UpdateCanvasDisplay();
		return;
	}
	// wx: the selection must still be loaded
	if (m_canvas)
	{
		const auto& vols = m_canvas->Volumes();
		const auto& meshes = m_canvas->Meshes();
		bool found = (m_vd && std::find(vols.begin(), vols.end(), m_vd) != vols.end()) ||
			(m_md && std::find(meshes.begin(), meshes.end(), m_md) != meshes.end());
		if (!found)
		{
			ClearData();
			return;
		}
	}
	SetEnabledAll(true);
	if (!planes)
		return;

	int res[3] = {0, 0, 0};
	if (m_sel_type == 2)
		m_vd->GetResolution(res[0], res[1], res[2]);
	else
	{
		Vector dim = m_md->GetBounds().diagonal();
		res[0] = int(dim.x()); res[1] = int(dim.y()); res[2] = int(dim.z());
	}

	m_blocked = true;
	double bd[3] = {0.0, 0.0, 0.0};
	CalcBoundingBoxDimensions(bd[0], bd[1], bd[2]);
	for (int a = 0; a < 3; ++a)
	{
		m_res[a] = std::max(1, int(std::round(bd[a])));
		m_factor[a] = res[a] / double(m_res[a]);
		if (std::abs(m_factor[a] - 1.0) < 1e-7)
			m_factor[a] = 1.0;
	}

	//clip distance
	int dist[3] = {1, 1, 1};
	if (ViewSync())
		m_canvas->GetClipDistance(dist[0], dist[1], dist[2]);
	else if (GroupSync())
		m_group->GetClipDistance(dist[0], dist[1], dist[2]);
	else
		clipping_data->GetClipDistance(dist[0], dist[1], dist[2]);
	for (int a = 0; a < 3; ++a)
	{
		if (dist[a] == 0) dist[a] = 1;
		QSignalBlocker b(m_distBox[a]);
		m_distBox[a]->setValue(dist[a]);
	}
	if (ViewSync())
		m_canvas->SetClipDistance(dist[0], dist[1], dist[2]);
	else if (GroupSync())
		m_group->SetClipDistance(dist[0], dist[1], dist[2]);
	else
		clipping_data->SetClipDistance(dist[0], dist[1], dist[2]);

	//plane positions
	for (int a = 0; a < 3; ++a)
	{
		double p_lo, p_hi;
		if (ViewSync()) { p_lo = m_canvas->GetLinkedParam(2 * a); p_hi = 1.0 - m_canvas->GetLinkedParam(2 * a + 1); }
		else if (GroupSync()) { p_lo = m_group->GetLinkedParam(2 * a); p_hi = 1.0 - m_group->GetLinkedParam(2 * a + 1); }
		else { p_lo = (*planes)[2 * a]->GetParam(); p_hi = 1.0 - (*planes)[2 * a + 1]->GetParam(); }
		m_lo[a] = int(std::fabs(p_lo * m_res[a]) + 0.499);
		m_hi[a] = int(std::fabs(p_hi * m_res[a]) + 0.499);
	}

	//links
	for (int a = 0; a < 3; ++a)
	{
		bool bval = false;
		if (ViewSync())
			bval = a == 0 ? m_canvas->GetClippingLinkX() : a == 1 ? m_canvas->GetClippingLinkY() : m_canvas->GetClippingLinkZ();
		else if (GroupSync())
			bval = a == 0 ? m_group->GetClippingLinkX() : a == 1 ? m_group->GetClippingLinkY() : m_group->GetClippingLinkZ();
		else
			bval = a == 0 ? clipping_data->GetClippingLinkX() : a == 1 ? clipping_data->GetClippingLinkY() : clipping_data->GetClippingLinkZ();
		QSignalBlocker b(m_linkChk[a]);
		m_linkChk[a]->setChecked(bval);
		m_link[a] = bval;
		if (bval)
			m_sldr_dist[a] = m_hi[a] - m_lo[a];
		SetRangeUi(a);
	}

	//rotations
	double rot[3] = {0.0, 0.0, 0.0};
	if (ViewSync())
		m_canvas->GetClippingPlaneRotations(rot[0], rot[1], rot[2]);
	else if (GroupSync())
		m_group->GetClippingPlaneRotations(rot[0], rot[1], rot[2]);
	else
		clipping_data->GetClippingPlaneRotations(rot[0], rot[1], rot[2]);
	for (int a = 0; a < 3; ++a)
	{
		rot[a] = NormalizeAngle(rot[a]);
		QSignalBlocker b1(m_rotSldr[a]), b2(m_rotBox[a]);
		m_rotSldr[a]->setValue(int(rot[a]));
		m_rotBox[a]->setValue(rot[a]);
	}

	if (m_canvas)
	{
		QSignalBlocker b(m_syncChan);
		m_syncChan->setChecked(m_canvas->GetSyncClippingPlanes());
	}
	if (m_group)
	{
		QSignalBlocker b(m_syncGroup);
		m_syncGroup->setChecked(m_group->GetSyncClippingPlanes());
	}
	m_blocked = false;

	UpdateCanvasDisplay();
	if (m_canvas)
	{
		m_canvas->CalcAndSetCombinedClippingPlanes();
		ReapplyClipRotations();
		m_canvas->RefreshView();
	}
}

// ---------------------------------------------------------------------------
// planes (wx On{X,Y,Z}{1,2}ClipEdit / Change)
// ---------------------------------------------------------------------------

void ClipPanel::EditPlane(int a, int which, double value)
{
	std::vector<Plane*>* planes = Planes();
	if (!Layer() || !planes)
		return;
	const int res = m_res[a];
	long ival = (long)(value / m_factor[a] + 0.5);
	int ival2 = which == 0 ? m_hi[a] : m_lo[a];

	auto set_param = [&](int id, double v) {
		if (ViewSync())
			m_canvas->SetLinkedParam(id, v);
		else if (GroupSync())
			m_group->SetLinkedParam(id, v);
		else
			(*planes)[id]->SetParam(v);
	};

	if (which == 0)
	{
		if (m_link[a])
		{
			if (ival + m_sldr_dist[a] > res)
			{
				ival = res - m_sldr_dist[a];
				ival2 = res;
			}
			else
				ival2 = int(ival + m_sldr_dist[a]);
		}
		else if (ival > ival2)
			ival = ival2;
		ival = std::max(0L, ival);
		m_lo[a] = int(ival);
		set_param(2 * a, double(ival) / double(res));
		if (m_link[a])
		{
			m_hi[a] = ival2;
			set_param(2 * a + 1, 1.0 - double(ival2) / double(res));
		}
	}
	else
	{
		if (m_link[a])
		{
			if (ival - m_sldr_dist[a] < 0)
			{
				ival = m_sldr_dist[a];
				ival2 = 0;
			}
			else
				ival2 = int(ival - m_sldr_dist[a]);
		}
		else if (ival < ival2)
		{
			SetRangeUi(a);   // wx: rejected (OnX2ClipChange keeps the slider at x1)
			return;
		}
		ival = std::min<long>(res, ival);
		m_hi[a] = int(ival);
		set_param(2 * a + 1, 1.0 - double(ival) / double(res));
		if (m_link[a])
		{
			m_lo[a] = ival2;
			set_param(2 * a, double(ival2) / double(res));
		}
	}
	SetRangeUi(a);

	if (ViewSync() || GroupSync())
		CalcAndSetCombinedClippingPlanes();

	if (m_canvas)
	{
		const int bit = which == 0 ? 1 : 2;
		m_canvas->SetClipMask(m_link[a] ? (3 << (2 * a)) : (bit << (2 * a)));
		ReapplyViewRotations();
	}
	RefreshViews();
}

void ClipPanel::OnSliderMoved(int a, int lo, int hi, bool lowMoved)
{
	if (m_blocked) return;
	if (lowMoved)
		EditPlane(a, 0, lo * m_factor[a]);
	else
		EditPlane(a, 1, hi * m_factor[a]);
}

// wx OnLinkXCheck
void ClipPanel::OnLinkToggled(int a, bool on)
{
	m_link[a] = on;
	if (ViewSync())
	{
		if (a == 0) m_canvas->SetClippingLinkX(on);
		else if (a == 1) m_canvas->SetClippingLinkY(on);
		else m_canvas->SetClippingLinkZ(on);
	}
	else if (GroupSync())
	{
		if (a == 0) m_group->SetClippingLinkX(on);
		else if (a == 1) m_group->SetClippingLinkY(on);
		else m_group->SetClippingLinkZ(on);
	}
	else if (ClippingLayer* l = Layer())
	{
		if (a == 0) l->SetClippingLinkX(on);
		else if (a == 1) l->SetClippingLinkY(on);
		else l->SetClippingLinkZ(on);
	}
	if (on)
		m_sldr_dist[a] = m_hi[a] - m_lo[a];
	m_dual[a]->setLinked(on);
}

void ClipPanel::SetLink(int a, bool v)
{
	{ QSignalBlocker b(m_linkChk[a]); m_linkChk[a]->setChecked(v); }
	OnLinkToggled(a, v);
}

// wx OnYZClipBtn (a = 0) / OnXZClipBtn (1) / OnXYClipBtn (2): full range on
// the other two axes, a linked slab of the given thickness on this one
void ClipPanel::OnPlaneButton(int a)
{
	if (!Layer())
		return;
	double bd[3] = {0.0, 0.0, 0.0};
	CalcBoundingBoxDimensions(bd[0], bd[1], bd[2]);
	int res[3];
	for (int i = 0; i < 3; ++i) res[i] = int(std::round(bd[i]));
	const long dist = m_distBox[a]->value();

	for (int o = 0; o < 3; ++o)
	{
		if (o == a) continue;
		SetLink(o, false);
		EditPlane(o, 0, 0.0);
		EditPlane(o, 1, res[o] * m_factor[o]);
	}

	SetLink(a, false);
	if (dist < res[a])
	{
		const int cur1 = m_lo[a];
		if (cur1 + dist <= res[a])
		{
			EditPlane(a, 0, cur1 * m_factor[a]);
			EditPlane(a, 1, (cur1 + dist) * m_factor[a]);
		}
		else
		{
			EditPlane(a, 0, (res[a] - dist) * m_factor[a]);
			EditPlane(a, 1, res[a] * m_factor[a]);
		}
		SetLink(a, true);
		if (ClippingLayer* l = Layer())
		{
			int d[3];
			l->GetClipDistance(d[0], d[1], d[2]);
			d[a] = int(dist);
			l->SetClipDistance(d[0], d[1], d[2]);
		}
	}
	else
	{
		EditPlane(a, 0, 0.0);
		EditPlane(a, 1, res[a] * m_factor[a]);
	}
	m_dual[a]->setFocus();
}

// wx MoveLinkedClippingPlanes
void ClipPanel::MoveLinkedClippingPlanes(int dir)
{
	if (!Layer())
		return;
	double bd[3] = {0.0, 0.0, 0.0};
	CalcBoundingBoxDimensions(bd[0], bd[1], bd[2]);
	for (int a = 0; a < 3; ++a)
	{
		if (!m_link[a]) continue;
		const long dist = m_hi[a] - m_lo[a];
		if (dir == 0)
		{
			long v1 = long(m_minBox[a]->value() / m_factor[a] + 0.5) - dist;
			v1 = v1 < 0 ? 0 : v1;
			EditPlane(a, 0, v1 * m_factor[a]);
		}
		else
		{
			const long res = long(std::round(bd[a]));
			long v2 = long(m_maxBox[a]->value() / m_factor[a] + 0.5) + dist;
			v2 = v2 > res ? res : v2;
			EditPlane(a, 1, v2 * m_factor[a]);
		}
	}
}

// wx OnSliderKeyDown: ',' moves a linked slab down, '.' up
void ClipPanel::OnSliderKey(int a, int dir)
{
	if (!Layer() || !m_link[a])
		return;
	const long dist = m_hi[a] - m_lo[a];
	if (dir == 0)
	{
		long v1 = long(m_minBox[a]->value() / m_factor[a] + 0.5) - dist;
		EditPlane(a, 0, std::max(0L, v1) * m_factor[a]);
	}
	else
	{
		long v2 = long(m_maxBox[a]->value() / m_factor[a] + 0.5) + dist;
		EditPlane(a, 1, std::min<long>(m_res[a], v2) * m_factor[a]);
	}
}

// wx OnClipResetBtn
void ClipPanel::OnClipReset()
{
	if (!Layer())
		return;
	std::vector<Plane*>* planes = nullptr;
	if (m_syncChan->isChecked() && m_canvas)
	{
		for (int id = 0; id < 6; id++)
			m_canvas->SetLinkedParam(id, 0.0);
		m_canvas->SetClippingLinkX(false);
		m_canvas->SetClippingLinkY(false);
		m_canvas->SetClippingLinkZ(false);
		m_canvas->SetClippingPlaneRotations(0.0, 0.0, 0.0);
	}
	else if (m_syncGroup->isChecked() && m_group)
	{
		for (int id = 0; id < 6; id++)
			m_group->SetLinkedParam(id, 0.0);
		m_group->SetClippingLinkX(false);
		m_group->SetClippingLinkY(false);
		m_group->SetClippingLinkZ(false);
		m_group->SetClippingPlaneRotations(0.0, 0.0, 0.0);
	}
	else
	{
		ClippingLayer* l = Layer();
		planes = Planes();
		l->SetClippingLinkX(false);
		l->SetClippingLinkY(false);
		l->SetClippingLinkZ(false);
		l->SetClippingPlaneRotations(0.0, 0.0, 0.0);
		if (!planes)
			return;
		SetUnitPlanes(planes);
		for (Plane* p : *planes)
		{
			p->SetParam(0.0);
			p->RememberParam();
		}
	}

	for (int a = 0; a < 3; ++a)
	{
		m_link[a] = false;
		QSignalBlocker b(m_linkChk[a]);
		m_linkChk[a]->setChecked(false);
		m_dual[a]->setLinked(false);
	}

	if (m_syncChan->isChecked() && m_canvas)
	{
		m_canvas->CalcAndSetCombinedClippingPlanes();
		for (VolumeData* vd : m_canvas->Volumes())
		{
			if (!vd || vd == m_vd || !vd->GetVR()) continue;
			for (Plane* p : *vd->GetVR()->get_planes())
			{
				p->SetParam(0.0);
				p->RememberParam();
			}
		}
		for (MeshData* md : m_canvas->Meshes())
		{
			if (!md || md == m_md || !md->GetMR()) continue;
			std::vector<Plane*>* mp = md->GetMR()->get_planes();
			if (!mp || mp->size() != 6) continue;
			for (Plane* p : *mp)
			{
				p->SetParam(0.0);
				p->RememberParam();
			}
		}
	}

	GetSettings();

	if (m_canvas)
	{
		m_canvas->SetClipMask(-1);
		ReapplyViewRotations();
	}
	RefreshViews();
}

// ---------------------------------------------------------------------------
// rotations
// ---------------------------------------------------------------------------

double ClipPanel::RotBoxValue(int a) const { return m_rotBox[a]->value(); }

// wx On{X,Y,Z}RotEdit
void ClipPanel::OnRotEdit(int a, double val)
{
	{
		QSignalBlocker b(m_rotSldr[a]);
		m_rotSldr[a]->setValue(int(val));
	}
	if (m_blocked)
		return;   // wx m_update_only_ui

	double r[3];
	auto set_axis = [&](auto* target) {
		target->GetClippingPlaneRotations(r[0], r[1], r[2]);
		r[a] = val;
		target->SetClippingPlaneRotations(r[0], r[1], r[2]);
	};
	if (ViewSync())
		set_axis(m_canvas);
	else if (m_canvas && GroupSync())
		set_axis(m_group);
	else if (m_canvas && Layer())
		set_axis(Layer());

	if (ViewSync() || GroupSync())
		CalcAndSetCombinedClippingPlanes();

	ReapplyClipRotations();
	RefreshViews();
}

// wx On{X,Y,Z}RotSpinUp / SpinDown: +-1 degree, wrapped to -180..180
void ClipPanel::OnRotSpin(int a, int v)
{
	if (m_blocked) return;
	int delta = v - m_spinLast[a];
	m_spinLast[a] = v;
	double nv = m_rotBox[a]->value() + delta;
	if (nv > 180.0) nv -= 360.0;
	if (nv < -180.0) nv += 360.0;
	m_rotBox[a]->setValue(nv);
}

// wx OnRotResetBtn
void ClipPanel::OnRotReset()
{
	if (ViewSync())
		m_canvas->SetClippingPlaneRotations(0.0, 0.0, 0.0);
	else if (m_canvas && GroupSync())
		m_group->SetClippingPlaneRotations(0.0, 0.0, 0.0);
	else if (m_canvas && Layer())
		Layer()->SetClippingPlaneRotations(0.0, 0.0, 0.0);

	SetClippingPlaneRotations(0.0, 0.0, 0.0, true);
	ReapplyClipRotations();
	RefreshViews();
}

// wx OnSetZeroBtn ("Align to View"): clip mode 2 at the current camera
void ClipPanel::OnAlign()
{
	if (!m_canvas)
	{
		Q_EMIT alignToViewRequested();
		return;
	}
	double rot[3] = {0.0, 0.0, 0.0};
	double cam_rotx, cam_roty, cam_rotz;
	Vector obj_ctr, obj_trans;
	m_canvas->GetCameraSettings(cam_rotx, cam_roty, cam_rotz, obj_ctr, obj_trans);
	if (ViewSync())
	{
		m_canvas->SetClipMode(2);
		m_canvas->GetClippingPlaneRotations(rot[0], rot[1], rot[2]);
	}
	else if (GroupSync())
	{
		if (DataGroup* g = dynamic_cast<DataGroup*>(m_group))
			g->SetClipMode(2, cam_rotx, cam_roty, cam_rotz, obj_ctr, obj_trans);
		else if (MeshGroup* mg = dynamic_cast<MeshGroup*>(m_group))
			mg->SetClipMode(2, cam_rotx, cam_roty, cam_rotz, obj_ctr, obj_trans);
		m_group->GetClippingPlaneRotations(rot[0], rot[1], rot[2]);
	}
	else if (m_sel_type == 2 && m_vd)
	{
		m_vd->SetClipMode(2, cam_rotx, cam_roty, cam_rotz, obj_ctr, obj_trans);
		m_vd->GetClippingPlaneRotations(rot[0], rot[1], rot[2]);
	}
	else if (m_sel_type == 3 && m_md)
	{
		m_md->SetClipMode(2, cam_rotx, cam_roty, cam_rotz, obj_ctr, obj_trans);
		m_md->GetClippingPlaneRotations(rot[0], rot[1], rot[2]);
	}
	SetClippingPlaneRotations(rot[0], rot[1], rot[2], true);
	RefreshViews();
}

// wx OnFixRotsCheck
void ClipPanel::OnFix(bool on)
{
	if (!m_canvas)
		return;
	double rot[3] = {0.0, 0.0, 0.0};
	double cam_rotx, cam_roty, cam_rotz;
	Vector obj_ctr, obj_trans;
	m_canvas->GetCameraSettings(cam_rotx, cam_roty, cam_rotz, obj_ctr, obj_trans);
	const int mode = on ? 3 : 4;
	if (ViewSync())
	{
		m_canvas->SetClipMode(mode);
		m_canvas->GetClippingPlaneRotations(rot[0], rot[1], rot[2]);
	}
	else if (GroupSync())
	{
		if (DataGroup* g = dynamic_cast<DataGroup*>(m_group))
			g->SetClipMode(mode, cam_rotx, cam_roty, cam_rotz, obj_ctr, obj_trans);
		else if (MeshGroup* mg = dynamic_cast<MeshGroup*>(m_group))
			mg->SetClipMode(mode, cam_rotx, cam_roty, cam_rotz, obj_ctr, obj_trans);
		m_group->GetClippingPlaneRotations(rot[0], rot[1], rot[2]);
	}
	else if (m_sel_type == 2 && m_vd)
	{
		m_vd->SetClipMode(mode, cam_rotx, cam_roty, cam_rotz, obj_ctr, obj_trans);
		m_vd->GetClippingPlaneRotations(rot[0], rot[1], rot[2]);
	}
	else if (m_sel_type == 3 && m_md)
	{
		m_md->SetClipMode(mode, cam_rotx, cam_roty, cam_rotz, obj_ctr, obj_trans);
		m_md->GetClippingPlaneRotations(rot[0], rot[1], rot[2]);
	}
	if (on)
		SetClippingPlaneRotations(rot[0], rot[1], rot[2], true);
	m_fix_rots = on;
	SetRotationsEnabled(!on);
	RefreshViews();
}

// wx OnHoldPlanesCheck + the m_hold_planes part of OnIdle
void ClipPanel::OnHold(bool on)
{
	m_hold_planes = on;
	if (!m_canvas)
		return;
	if (on)
	{
		m_draw_clip = true;
		m_canvas->SetDrawClip(true);
		if (!underMouse())
			m_canvas->SetClipMask(-1);
	}
	else if (!underMouse())
	{
		m_draw_clip = false;
		m_canvas->SetDrawClip(false);
	}
	RefreshViews(m_plane_mode == PM_LOWTRANSBACK || m_plane_mode == PM_NORMALBACK);
}

// wx ClippingView::OnIdle: the planes are shown while the pointer is over
// the pane (or always with "Display Planes")
void ClipPanel::enterEvent(QEnterEvent* e)
{
	QWidget::enterEvent(e);
	m_mouse_in = true;
	if (!m_canvas || !Layer())
		return;
	if (m_hold_planes || !m_draw_clip)
	{
		m_canvas->SetDrawClip(true);
		m_canvas->SetClipMask(-1);
		m_draw_clip = true;
		RefreshViews(m_plane_mode == PM_LOWTRANSBACK || m_plane_mode == PM_NORMALBACK);
	}
}

void ClipPanel::leaveEvent(QEvent* e)
{
	QWidget::leaveEvent(e);
	m_mouse_in = false;
	if (!m_canvas)
		return;
	if (m_draw_clip && !m_hold_planes)
	{
		m_canvas->SetDrawClip(false);
		m_draw_clip = false;
		RefreshViews(m_plane_mode == PM_LOWTRANSBACK || m_plane_mode == PM_NORMALBACK);
	}
}

// wx OnPlaneModesCombo
void ClipPanel::OnPlaneMode(int mode)
{
	m_plane_mode = mode;
	UpdateCanvasDisplay();
	RefreshViews();
}

// wx OnLinkChannelsCheck
void ClipPanel::OnSyncChannels(bool on)
{
	if (!m_canvas)
		return;
	if (on)
	{
		if (m_sel_type == 2 && m_vd)
			m_canvas->SetSyncClippingPlanes(true, m_vd);
		else if (m_sel_type == 3 && m_md)
			m_canvas->SetSyncClippingPlanes(true, m_md);
		else
			m_canvas->SetSyncClippingPlanes(true);
	}
	else
		m_canvas->SetSyncClippingPlanes(false);
	UpdateCanvasDisplay();
	SyncClippingPlanes();
}

// wx OnLinkGroupCheck
void ClipPanel::OnSyncGroup(bool on)
{
	if (!m_group)
		return;
	if (on)
	{
		m_group->SetSyncClippingPlanes(true);
		if (ClippingLayer* copy = Layer())
			m_group->CopyClippingParams(*copy);
		if (std::vector<Plane*>* planes = Planes())
			for (int i = 0; i < 6; i++)
				m_group->SetLinkedParam(i, (*planes)[i]->GetParam());
	}
	else
		m_group->SetSyncClippingPlanes(false);
	SyncClippingPlanes();
}

// ---------------------------------------------------------------------------
// defaults (wx LoadDefault / SaveDefault)
// ---------------------------------------------------------------------------

void ClipPanel::LoadDefault()
{
	const VVDString dft = ClipDefaultFile(false);
	vvd::IniFile fconfig;
	if (!vvd::FileExists(dft) || !fconfig.Load(dft))
		return;
	long ival;
	if (fconfig.Read(L"plane_mode", &ival) && ival >= 0 && ival < m_mode->count())
	{
		QSignalBlocker b(m_mode);
		m_mode->setCurrentIndex(int(ival));
		m_plane_mode = int(ival);
		UpdateCanvasDisplay();
	}
	// wx: link_channels is saved but not restored
}

void ClipPanel::SaveDefault()
{
	vvd::IniFile fconfig;
	fconfig.Write(L"plane_mode", m_mode->currentIndex());
	fconfig.Write(L"link_channels", m_syncChan->isChecked());
	fconfig.Save(ClipDefaultFile(true));
}

// ---------------------------------------------------------------------------
// accessors
// ---------------------------------------------------------------------------

void ClipPanel::SetClippingPlaneRotations(double rx, double ry, double rz, bool update_only_ui)
{
	bool was = m_blocked;
	m_blocked = true;
	double r[3] = {rx, ry, rz};
	for (int a = 0; a < 3; ++a) {
		m_rotBox[a]->setValue(r[a]);
		m_rotSldr[a]->setValue(int(r[a]));
	}
	m_blocked = was;
	if (!update_only_ui)
		for (int a = 0; a < 3; ++a)
			OnRotEdit(a, r[a]);
}

void ClipPanel::GetClippingPlaneRotations(double& rx, double& ry, double& rz) const
{
	rx = m_rotBox[0]->value(); ry = m_rotBox[1]->value(); rz = m_rotBox[2]->value();
}

void ClipPanel::GetClipRange(int a, int& lo, int& hi) const { lo = m_lo[a]; hi = m_hi[a]; }
bool ClipPanel::GetChannLink() const { return m_syncChan->isChecked(); }
void ClipPanel::SetChannLink(bool v) { m_syncChan->setChecked(v); OnSyncChannels(v); }
bool ClipPanel::GetGroupSync() const { return m_syncGroup->isChecked(); }
bool ClipPanel::GetLink(int a) const { return m_linkChk[a]->isChecked(); }
int ClipPanel::GetPlaneMode() const { return m_mode->currentIndex(); }
void ClipPanel::SetPlaneMode(int m) { if (m >= 0 && m < m_mode->count()) m_mode->setCurrentIndex(m); }
bool ClipPanel::GetFixRotations() const { return m_fix->isChecked(); }
bool ClipPanel::GetDisplayPlanes() const { return m_display->isChecked(); }

#include "ClipPanel.moc"
