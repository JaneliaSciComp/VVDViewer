#include "MoviePanel.h"
#include "TestRun.h"
#include "RecorderPanel.h"
#include "RenderCanvas.h"
#include "ViewOverlays.h"
#include "CaptureWriter.h"
#include "ProjectIO.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QIntValidator>
#include <QDoubleValidator>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>

#include "FLIVR/TextureRenderer.h"
#include "Animator/Interpolator.h"
#include "Formats/QVideoEncoder.h"
#include "DataManager.h"
#include "Core/FileUtil.h"
#include "img/icons.h"

double MoviePanel::m_Mbitrate = 10.0;

namespace {

QIcon IconFromPng(const unsigned char* data, int len)
{
	QPixmap pm;
	pm.loadFromData(data, (uint)len, "PNG");
	return QIcon(pm);
}
#define VVD_ICON(name) IconFromPng(icons::name##_png, (int)sizeof(icons::name##_png))

QLineEdit* MakeEdit(const QString& text, int width)
{
	QLineEdit* e = new QLineEdit(text);
	e->setFixedWidth(width);
	return e;
}

QLabel* MakeLabel(const QString& text)
{
	QLabel* l = new QLabel(text);
	l->setMinimumWidth(l->sizeHint().width());
	return l;
}

// wx STOI / STOD / ToLong / ToDouble: leading number, 0 when none
int STOI(const QString& s)
{
	QByteArray b = s.trimmed().toLatin1();
	return (int)std::strtol(b.constData(), nullptr, 10);
}

double STOD(const QString& s)
{
	QByteArray b = s.trimmed().toLatin1();
	return std::strtod(b.constData(), nullptr);
}

// wx ChangeValue: set the text without the text-changed handlers
void ChangeValue(QLineEdit* e, const QString& text)
{
	QSignalBlocker b(e);
	e->setText(text);
}

QString Fmt2(double v) { return QString::asprintf("%.2f", v); }

} // namespace

QString MoviePanel::StyleSheetText()
{
	return QStringLiteral(
		"MoviePanel, MoviePanel QStackedWidget > QWidget { background: #ececec; }"
		"MoviePanel QLabel, MoviePanel QCheckBox, MoviePanel QRadioButton"
		" { font-size: 11px; color: #000000; background: transparent; }"
		"MoviePanel QCheckBox:disabled, MoviePanel QRadioButton:disabled, MoviePanel QLabel:disabled"
		" { color: #8c8c8c; }"
		"MoviePanel QLineEdit, MoviePanel QSpinBox {"
		" font-size: 11px; color: #000000; background: #ffffff;"
		" border: 1px solid #b4b4b4; border-radius: 2px; padding: 1px 3px; min-height: 15px; }"
		"MoviePanel QLineEdit:disabled, MoviePanel QSpinBox:disabled"
		" { color: #a0a0a0; background: #f4f4f4; border-color: #d0d0d0; }"
		"MoviePanel QComboBox {"
		" font-size: 11px; color: #000000; background: #ffffff;"
		" border: 1px solid #b4b4b4; border-radius: 2px; padding: 1px 4px; min-height: 15px; }"
		"MoviePanel QPushButton {"
		" font-size: 11px; color: #000000; background: #ffffff;"
		" border: 1px solid #b4b4b4; border-radius: 3px; padding: 2px 6px; min-height: 16px; }"
		"MoviePanel QPushButton:pressed { background: #dcdcdc; }"
		"MoviePanel QPushButton:disabled { color: #a0a0a0; background: #f4f4f4; border-color: #d0d0d0; }"
		"MoviePanel QTabBar { font-size: 11px; background: transparent; }"
		"MoviePanel QTabBar::tab {"
		" color: #000000; background: transparent; border: 1px solid transparent;"
		" border-radius: 3px; padding: 2px 9px; margin: 1px 1px; }"
		"MoviePanel QTabBar::tab:selected { background: #ffffff; border-color: #c4c4c4; }"
		"MoviePanel QSlider::groove:horizontal { height: 3px; background: #c8c8c8; border-radius: 1px; }"
		"MoviePanel QSlider::sub-page:horizontal { background: #3b99fc; border-radius: 1px; }"
		"MoviePanel QSlider::handle:horizontal {"
		" width: 11px; height: 11px; margin: -4px 0; border-radius: 6px;"
		" background: #ffffff; border: 1px solid #a8a8a8; }"
		"MoviePanel QTreeWidget {"
		" font-size: 11px; background: #ffffff; border: 1px solid #b4b4b4; }"
		"MoviePanel QTreeWidget QLineEdit, MoviePanel QTreeWidget QComboBox { border: 0px; padding: 0px 2px; min-height: 12px; }"
		"MoviePanel QHeaderView::section { font-size: 11px; background: #f2f2f2;"
		" border: 0px; border-right: 1px solid #d0d0d0; border-bottom: 1px solid #d0d0d0; padding: 1px 4px; }");
}

MoviePanel::MoviePanel(QWidget* parent) : QWidget(parent)
{
	setStyleSheet(StyleSheetText());
	m_encoder = std::make_unique<QVideoEncoder>();

	LoadSettings();

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(4, 2, 4, 2);
	root->setSpacing(2);

	//notebook
	m_tabs = new QTabBar;
	m_tabs->setDrawBase(false);
	m_tabs->setExpanding(false);
	m_tabs->setFocusPolicy(Qt::NoFocus);
	m_tabs->addTab(QStringLiteral("Basic"));
	m_tabs->addTab(QStringLiteral("Advanced"));
	m_tabs->addTab(QStringLiteral("Auto Key"));
	m_tabs->addTab(QStringLiteral("Cropping"));
	QHBoxLayout* tabrow = new QHBoxLayout;
	tabrow->setContentsMargins(0, 0, 0, 0);
	tabrow->addStretch(1);
	tabrow->addWidget(m_tabs);
	tabrow->addStretch(1);
	root->addLayout(tabrow);

	m_stack = new QStackedWidget;
	m_stack->addWidget(m_pages[0] = CreateSimplePage());
	m_stack->addWidget(m_pages[1] = CreateAdvancedPage());
	m_stack->addWidget(m_pages[2] = CreateAutoKeyPage());
	m_stack->addWidget(m_pages[3] = CreateCroppingPage());
	root->addWidget(m_stack, 1);

	BuildFooter(root);

	m_timer = new QTimer(this);
	m_timer->setTimerType(Qt::PreciseTimer);
	connect(m_timer, &QTimer::timeout, this, &MoviePanel::OnTimer);
	connect(m_tabs, &QTabBar::currentChanged, this, &MoviePanel::OnPageChanged);

	Init();
}

MoviePanel::~MoviePanel()
{
	if (m_encoder_open)
		m_encoder->close();
	if (!VVDQtScriptedRun())   // scripted test runs keep the user's defaults
		SaveDefault();
}

// ---------------------------------------------------------------- pages

QWidget* MoviePanel::CreateSimplePage()
{
	QWidget* page = new QWidget;
	QVBoxLayout* v = new QVBoxLayout(page);
	v->setContentsMargins(2, 4, 2, 2);
	v->setSpacing(5);

	//rotations
	m_rot_chk = new QCheckBox(QStringLiteral("Rotation"));
	m_rot_chk->setChecked(true);
	v->addWidget(m_rot_chk);

	//axis
	m_x_rd = new QRadioButton(QStringLiteral("X"));
	m_y_rd = new QRadioButton(QStringLiteral("Y"));
	m_z_rd = new QRadioButton(QStringLiteral("Z"));
	m_y_rd->setChecked(true);

	QHBoxLayout* hx = new QHBoxLayout;
	hx->setContentsMargins(20, 0, 0, 0);
	hx->addWidget(m_x_rd);
	hx->addStretch(1);
	v->addLayout(hx);

	//degrees
	m_degree_end = MakeEdit(QStringLiteral("360"), 40);
	m_rot_int_cmb = new QComboBox;
	m_rot_int_cmb->addItems(QStringList() << QStringLiteral("Linear") << QStringLiteral("Smooth"));
	m_rot_int_cmb->setCurrentIndex(0);
	m_rot_int_cmb->setMinimumContentsLength(6);
	m_rot_int_cmb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	QHBoxLayout* hy = new QHBoxLayout;
	hy->setContentsMargins(20, 0, 0, 0);
	hy->setSpacing(4);
	hy->addWidget(m_y_rd);
	hy->addSpacing(4);
	hy->addWidget(m_degree_end);
	hy->addWidget(MakeLabel(QStringLiteral("Degrees")));
	hy->addSpacing(2);
	hy->addWidget(MakeLabel(QStringLiteral("Style:")));
	hy->addWidget(m_rot_int_cmb);
	hy->addStretch(1);
	v->addLayout(hy);

	QHBoxLayout* hz = new QHBoxLayout;
	hz->setContentsMargins(20, 0, 0, 0);
	hz->addWidget(m_z_rd);
	hz->addStretch(1);
	v->addLayout(hz);

	v->addSpacing(5);

	//time sequence
	m_seq_chk = new QCheckBox(QStringLiteral("Time Sequence / Batch"));
	QHBoxLayout* h1 = new QHBoxLayout;
	h1->setContentsMargins(0, 0, 0, 0);
	h1->addWidget(m_seq_chk);
	h1->addStretch(1);
	h1->addWidget(MakeLabel(QStringLiteral("Current Time")));
	h1->addSpacing(20);
	v->addLayout(h1);

	m_time_start_text = MakeEdit(QStringLiteral("1"), 35);
	m_time_end_text = MakeEdit(QStringLiteral("10"), 35);
	m_time_current_text = MakeEdit(QStringLiteral("0"), 50);
	m_inc_time_btn = new QPushButton;
	m_dec_time_btn = new QPushButton;
	m_inc_time_btn->setIcon(VVD_ICON(plus));
	m_dec_time_btn->setIcon(VVD_ICON(minus));
	for (QPushButton* b : { m_inc_time_btn, m_dec_time_btn }) {
		b->setFixedWidth(30);
		b->setIconSize(QSize(16, 16));
		b->setFocusPolicy(Qt::NoFocus);
	}
	QHBoxLayout* h2 = new QHBoxLayout;
	h2->setContentsMargins(5, 0, 0, 0);
	h2->setSpacing(3);
	h2->addWidget(MakeLabel(QStringLiteral("Start:")));
	h2->addWidget(m_time_start_text);
	h2->addSpacing(15);
	h2->addWidget(MakeLabel(QStringLiteral("End:")));
	h2->addWidget(m_time_end_text);
	h2->addStretch(1);
	h2->addWidget(m_dec_time_btn);
	h2->addSpacing(2);
	h2->addWidget(m_time_current_text);
	h2->addSpacing(2);
	h2->addWidget(m_inc_time_btn);
	h2->addSpacing(5);
	v->addLayout(h2);

	v->addSpacing(5);

	//movie length
	m_movie_time = MakeEdit(QStringLiteral("5"), 40);
	m_movie_time_basic = 5;
	QHBoxLayout* h7 = new QHBoxLayout;
	h7->setContentsMargins(0, 0, 0, 0);
	h7->setSpacing(4);
	h7->addStretch(1);
	h7->addWidget(MakeLabel(QStringLiteral("Movie Length:")));
	h7->addWidget(m_movie_time);
	h7->addWidget(MakeLabel(QStringLiteral("seconds")));
	h7->addSpacing(4);
	v->addLayout(h7);

	v->addSpacing(5);

	//rewind
	m_rewind_chk = new QCheckBox(QStringLiteral("Rewind"));
	m_rewind_chk->setChecked(false);
	QHBoxLayout* h8 = new QHBoxLayout;
	h8->setContentsMargins(0, 0, 0, 0);
	h8->addStretch(1);
	h8->addWidget(m_rewind_chk);
	h8->addSpacing(4);
	v->addLayout(h8);

	v->addStretch(1);

	connect(m_rot_chk, &QCheckBox::clicked, this, &MoviePanel::OnRotateChecked);
	connect(m_seq_chk, &QCheckBox::clicked, this, &MoviePanel::OnSequenceChecked);
	connect(m_inc_time_btn, &QPushButton::clicked, this, [this]() { UpFrame(); });     // wx OnUpFrame
	connect(m_dec_time_btn, &QPushButton::clicked, this, [this]() { DownFrame(); });   // wx OnDownFrame
	connect(m_time_current_text, &QLineEdit::textChanged, this, &MoviePanel::OnTimeText);
	connect(m_rot_int_cmb, QOverload<int>::of(&QComboBox::activated),
		this, [this](int i) { m_rot_int_type = i; });   // wx OnRotIntCmb
	return page;
}

QWidget* MoviePanel::CreateAdvancedPage()
{
	QWidget* page = new QWidget;
	QVBoxLayout* v = new QVBoxLayout(page);
	v->setContentsMargins(2, 5, 2, 5);
	v->setSpacing(0);
	m_advanced_movie = new RecorderPanel(page);
	v->addWidget(m_advanced_movie, 1);
	return page;
}

QWidget* MoviePanel::CreateAutoKeyPage()
{
	QWidget* page = new QWidget;
	QVBoxLayout* v = new QVBoxLayout(page);
	v->setContentsMargins(2, 10, 2, 10);
	v->setSpacing(10);
	v->addWidget(MakeLabel(QStringLiteral("Choose an auto key type")));

	//list of options
	m_auto_key_list = new QTreeWidget;
	m_auto_key_list->setColumnCount(2);
	m_auto_key_list->setHeaderLabels(QStringList()
		<< QStringLiteral("ID") << QStringLiteral("Auto Key Type"));
	m_auto_key_list->setRootIsDecorated(false);
	m_auto_key_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_auto_key_list->setMinimumHeight(60);
	const char* names[3] = { "Channel combination nC1", "Channel combination nC2",
		"Channel combination nC3" };
	for (int i = 0; i < 3; ++i)
		m_auto_key_list->addTopLevelItem(new QTreeWidgetItem(
			QStringList() << QString::number(i + 1) << QString::fromLatin1(names[i])));
	m_auto_key_list->header()->setStretchLastSection(true);
	m_auto_key_list->setColumnWidth(0, 36);
	v->addWidget(m_auto_key_list, 1);

	//button
	m_gen_keys_btn = new QPushButton(QStringLiteral("Generate"));
	QHBoxLayout* h = new QHBoxLayout;
	h->addStretch(1);
	h->addWidget(m_gen_keys_btn);
	v->addLayout(h);

	connect(m_gen_keys_btn, &QPushButton::clicked, this, [this]() { GenKey(); });   // wx OnGenKey
	connect(m_auto_key_list, &QTreeWidget::itemActivated, this,
		[this](QTreeWidgetItem*, int) { GenKey(); });                               // wx OnListItemAct
	return page;
}

QWidget* MoviePanel::CreateCroppingPage()
{
	QWidget* page = new QWidget;
	QVBoxLayout* v = new QVBoxLayout(page);
	v->setContentsMargins(2, 10, 2, 2);
	v->setSpacing(10);

	//8th line
	m_frame_chk = new QCheckBox;
	m_reset_btn = new QPushButton(QStringLiteral("Reset"));
	m_reset_btn->setIcon(VVD_ICON(reset));
	m_reset_btn->setMinimumWidth(110);
	QHBoxLayout* h8 = new QHBoxLayout;
	h8->setContentsMargins(5, 0, 0, 0);
	h8->setSpacing(4);
	QLabel* st = MakeLabel(QStringLiteral("Enable cropping:"));
	st->setMinimumWidth(std::max(110, st->minimumWidth()));
	h8->addWidget(st);
	h8->addWidget(m_frame_chk);
	h8->addSpacing(40);
	h8->addWidget(m_reset_btn);
	h8->addStretch(1);
	v->addLayout(h8);

	// wx: text + wxSpinButton (range +-INT_MAX, +1/-1 per click)
	auto makeSpin = [](QWidget* parent) {
		QSpinBox* s = new QSpinBox(parent);
		s->setRange(-INT_MAX, INT_MAX);
		s->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
		s->setMinimumWidth(70);
		s->setKeyboardTracking(true);
		return s;
	};
	m_center_x = makeSpin(this);
	m_center_y = makeSpin(this);
	m_width = makeSpin(this);
	m_height = makeSpin(this);

	//9th line
	QHBoxLayout* h9 = new QHBoxLayout;
	h9->setContentsMargins(5, 0, 0, 0);
	h9->setSpacing(3);
	st = MakeLabel(QStringLiteral("Center:  X:"));
	st->setMinimumWidth(std::max(85, st->minimumWidth()));
	h9->addWidget(st);
	h9->addWidget(m_center_x);
	st = MakeLabel(QStringLiteral("Y:"));
	st->setMinimumWidth(std::max(50, st->minimumWidth()));
	st->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	h9->addWidget(st);
	h9->addWidget(m_center_y);
	h9->addStretch(1);
	v->addLayout(h9);

	//10th line
	QHBoxLayout* h10 = new QHBoxLayout;
	h10->setContentsMargins(5, 0, 0, 0);
	h10->setSpacing(3);
	st = MakeLabel(QStringLiteral("Size:    Width:"));
	st->setMinimumWidth(std::max(85, st->minimumWidth()));
	h10->addWidget(st);
	h10->addWidget(m_width);
	st = MakeLabel(QStringLiteral("Height:"));
	st->setMinimumWidth(std::max(50, st->minimumWidth()));
	st->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	h10->addWidget(st);
	h10->addWidget(m_height);
	h10->addStretch(1);
	v->addLayout(h10);

	v->addStretch(1);

	connect(m_frame_chk, &QCheckBox::clicked, this, &MoviePanel::OnFrameCheck);
	connect(m_reset_btn, &QPushButton::clicked, this, &MoviePanel::OnResetFrame);
	for (QSpinBox* s : { m_center_x, m_center_y, m_width, m_height })
		connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this,
			[this](int) { OnFrameEditing(); });
	return page;
}

void MoviePanel::BuildFooter(QVBoxLayout* root)
{
	//FPS
	m_fps_text = MakeEdit(QStringLiteral("30"), 30);
	m_repeat_chk = new QCheckBox(QStringLiteral("repeat"));
	m_repeat_chk->setChecked(false);
	m_views_cmb = new QComboBox;
	m_views_cmb->setMinimumContentsLength(12);
	m_views_cmb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

	QHBoxLayout* h1 = new QHBoxLayout;
	h1->setContentsMargins(5, 0, 0, 0);
	h1->setSpacing(3);
	h1->addWidget(MakeLabel(QStringLiteral("FPS:")));
	h1->addWidget(m_fps_text);
	h1->addSpacing(10);
	h1->addWidget(m_repeat_chk);
	h1->addStretch(1);
	h1->addWidget(MakeLabel(QStringLiteral("Capture:")));
	h1->addWidget(m_views_cmb);
	root->addLayout(h1);

	//the play/rewind/slider/save
	m_play_btn = new QPushButton;
	m_play_btn->setIcon(VVD_ICON(play));
	m_rewind_btn = new QPushButton;
	m_rewind_btn->setIcon(VVD_ICON(rewind));
	for (QPushButton* b : { m_play_btn, m_rewind_btn }) {
		b->setFixedSize(30, 30);
		b->setIconSize(QSize(22, 22));
		b->setFlat(true);
		b->setFocusPolicy(Qt::NoFocus);
		b->setStyleSheet(QStringLiteral("QPushButton { border: none; background: transparent; padding: 0px; }"));
	}
	m_progress_sldr = new QSlider(Qt::Horizontal);
	m_progress_sldr->setRange(0, 360);
	m_progress_sldr->setMinimumWidth(30);
	m_progress_text = MakeEdit(QStringLiteral("0.00"), 50);
	m_save_btn = new QPushButton(QStringLiteral("Save..."));
	m_save_btn->setIcon(VVD_ICON(listicon_save));
	m_save_btn->setMinimumWidth(80);
	m_save_btn->setFocusPolicy(Qt::NoFocus);

	QHBoxLayout* h2 = new QHBoxLayout;
	h2->setContentsMargins(0, 0, 0, 0);
	h2->setSpacing(2);
	h2->addWidget(m_play_btn);
	h2->addWidget(m_rewind_btn);
	h2->addWidget(m_progress_sldr, 1);
	h2->addWidget(m_progress_text);
	h2->addSpacing(5);
	QLabel* s_lbl = MakeLabel(QStringLiteral("s"));
	s_lbl->setMinimumWidth(std::max(14, s_lbl->minimumWidth()));
	h2->addWidget(s_lbl);
	h2->addWidget(m_save_btn);
	root->addLayout(h2);

	connect(m_play_btn, &QPushButton::clicked, this, &MoviePanel::OnPrev);
	connect(m_rewind_btn, &QPushButton::clicked, this, &MoviePanel::OnRewind);
	connect(m_save_btn, &QPushButton::clicked, this, &MoviePanel::OnRun);
	connect(m_progress_sldr, &QSlider::valueChanged, this, &MoviePanel::OnTimeChange);
	connect(m_progress_text, &QLineEdit::textEdited, this, &MoviePanel::OnTimeEnter);
	connect(m_views_cmb, QOverload<int>::of(&QComboBox::activated), this, &MoviePanel::OnViewSelected);
}

// ---------------------------------------------------------------- setup

void MoviePanel::SetRenderCanvas(RenderCanvas* canvas)
{
	m_canvas = canvas;
	if (m_advanced_movie)
		m_advanced_movie->SetContext(m_canvas, m_interpolator, this);
}

void MoviePanel::SetInterpolator(Interpolator* ip)
{
	m_interpolator = ip;
	if (m_advanced_movie)
		m_advanced_movie->SetContext(m_canvas, m_interpolator, this);
}

// wx VMovieView::Init (the views are added by the frame)
void MoviePanel::Init()
{
	DisableTime();
}

// wx GetSettings: crop fields from the view's frame
void MoviePanel::GetSettings()
{
	if (!m_canvas) return;
	if (m_canvas->GetFrameEnabled())
	{
		QSignalBlocker b(m_frame_chk);
		m_frame_chk->setChecked(true);
		int x, y, w, h;
		m_canvas->GetFrame(x, y, w, h);
		SetFrameFields(x, y, w, h);
	}
	else
	{
		QSignalBlocker b(m_frame_chk);
		m_frame_chk->setChecked(false);
	}
}

void MoviePanel::OnViewSelected(int)
{
	ResolveView();
	GetSettings();
}

void MoviePanel::ResolveView()
{
	if (!m_hooks.find_view) return;
	RenderCanvas* c = nullptr;
	ViewOverlays* ov = nullptr;
	if (!m_hooks.find_view(m_views_cmb->currentText(), c, ov) || !c) return;
	if (c != m_canvas) SetRenderCanvas(c);
	m_overlays = ov;
}

void MoviePanel::AddView(const QString& name)
{
	m_views_cmb->addItem(name);
}

void MoviePanel::DeleteView(const QString& name)
{
	int cur = m_views_cmb->currentIndex();
	int del = m_views_cmb->findText(name);
	if (del >= 0)
		m_views_cmb->removeItem(del);
	if (cur == del && m_views_cmb->count() > 0)
		m_views_cmb->setCurrentIndex(0);
	ResolveView();
}

void MoviePanel::SetView(int index)
{
	if (index >= 0 && index < m_views_cmb->count())
		m_views_cmb->setCurrentIndex(index);
	ResolveView();
}

QString MoviePanel::CurrentView() const { return m_views_cmb->currentText(); }
int MoviePanel::ViewCount() const { return m_views_cmb->count(); }

void MoviePanel::SetMode(int page)
{
	if (page >= 0 && page < m_tabs->count())
		m_tabs->setCurrentIndex(page);
}

int MoviePanel::GetMode() const { return m_tabs->currentIndex(); }

QWidget* MoviePanel::PageWidget(int page) const
{
	return (page >= 0 && page < 4) ? m_pages[page] : nullptr;
}

long MoviePanel::GetFPS() const
{
	return STOI(m_fps_text->text());
}

bool MoviePanel::IsRotationEnabled() const { return m_rot_chk->isChecked(); }
int MoviePanel::RotationAxis() const { return m_x_rd->isChecked() ? 1 : (m_z_rd->isChecked() ? 3 : 2); }
double MoviePanel::RotationDegrees() const { return STOD(m_degree_end->text()); }
int MoviePanel::RotationStyle() const { return m_rot_int_type; }
bool MoviePanel::IsSequenceEnabled() const { return m_seq_chk->isChecked(); }
double MoviePanel::MovieLength() const { return STOD(m_movie_time->text()); }
bool MoviePanel::IsCroppingEnabled() const { return m_frame_chk->isChecked(); }

// the view-level rotation API of wx (VRenderView::GetRotations/SetRotations
// give and take 360 - x of the canvas angle)
void MoviePanel::ViewGetRotations(double& x, double& y, double& z) const
{
	m_canvas->GetRotations(x, y, z);
	x = 360.0 - x;
}

void MoviePanel::ViewSetRotations(double x, double y, double z)
{
	m_canvas->SetRotations(360.0 - x, y, z);
}

// ---------------------------------------------------------------- enabling

void MoviePanel::DisableRot()
{
	m_x_rd->setEnabled(false);
	m_y_rd->setEnabled(false);
	m_z_rd->setEnabled(false);
	m_degree_end->setEnabled(false);
	m_rot_chk->setChecked(false);
}

void MoviePanel::EnableRot()
{
	m_x_rd->setEnabled(true);
	m_y_rd->setEnabled(true);
	m_z_rd->setEnabled(true);
	m_degree_end->setEnabled(true);
	m_rot_chk->setChecked(true);
}

void MoviePanel::DisableTime()
{
	m_time_start_text->setEnabled(false);
	m_time_end_text->setEnabled(false);
	m_inc_time_btn->setEnabled(false);
	m_dec_time_btn->setEnabled(false);
	m_time_current_text->setEnabled(false);
	m_seq_chk->setChecked(false);
}

void MoviePanel::EnableTime()
{
	if (!m_canvas) return;
	int first = 0, sec = 0, tmp = 0;
	m_canvas->Get4DSeqFrames(first, sec, tmp);
	if (sec - first == 0)
		m_canvas->Get3DBatFrames(first, sec, tmp);
	ChangeValue(m_time_start_text, QString::number(first));
	ChangeValue(m_time_end_text, QString::number(sec));
	m_time_start_text->setEnabled(true);
	m_time_end_text->setEnabled(true);
	m_inc_time_btn->setEnabled(true);
	m_dec_time_btn->setEnabled(true);
	m_time_current_text->setEnabled(true);
	m_seq_chk->setChecked(true);

	unsigned long fps = (unsigned long)std::max(0L, GetFPS());
	if (fps == 0) fps = 1;   // wx divides by zero
	unsigned int mov_len = (unsigned int)((sec - first + 1) / fps + 1);
	m_movie_time->setText(QString::number(mov_len));
}

void MoviePanel::OnRotateChecked(bool)
{
	if (m_rot_chk->isChecked())
		EnableRot();
	else
		DisableRot();
}

void MoviePanel::OnSequenceChecked(bool)
{
	if (m_seq_chk->isChecked())
		EnableTime();
	else
		DisableTime();
}

// ---------------------------------------------------------------- time

void MoviePanel::UpFrame()
{
	if (m_running) return;
	int start_time = STOI(m_time_start_text->text());
	int end_time = STOI(m_time_end_text->text());
	int current_time = STOI(m_time_current_text->text()) + 1;
	if (current_time > end_time) current_time = start_time;
	int time = end_time - start_time + 1;
	double pcnt = (double)current_time / (double)time;
	ChangeValue(m_time_current_text, QString::number(current_time));
	SetProgress(pcnt);
	SetRendering(pcnt);
}

void MoviePanel::DownFrame()
{
	if (m_running) return;
	int start_time = STOI(m_time_start_text->text());
	int end_time = STOI(m_time_end_text->text());
	int current_time = STOI(m_time_current_text->text()) - 1;
	if (current_time < start_time) current_time = end_time;
	int time = end_time - start_time + 1;
	double pcnt = (double)current_time / (double)time;
	ChangeValue(m_time_current_text, QString::number(current_time));
	SetProgress(pcnt);
	SetRendering(pcnt);
}

void MoviePanel::SetCurrentTime(int t)
{
	ChangeValue(m_time_current_text, QString::number(t));
	OnTimeText();
}

// wx OnTimeText (EVT_TEXT of the current time)
void MoviePanel::OnTimeText()
{
	if (m_running) return;
	int start_time = STOI(m_time_start_text->text());
	int end_time = STOI(m_time_end_text->text());
	int current_time = STOI(m_time_current_text->text());
	if (current_time < start_time) current_time = end_time;
	if (current_time > end_time) current_time = start_time;
	int time = end_time - start_time + 1;
	double pcnt = time != 0 ? (double)current_time / (double)time : 0.0;
	SetProgress(pcnt);
	SetRendering(pcnt);
}

// wx OnTimeChange (EVT_COMMAND_SCROLL of the slider)
void MoviePanel::OnTimeChange(int frame)
{
	if (m_running) return;
	double pcnt = (((double)frame) / 360.);
	SetRendering(pcnt);

	int start_time = STOI(m_time_start_text->text());
	int end_time = STOI(m_time_end_text->text());
	int time = end_time - start_time + 1;
	ChangeValue(m_time_current_text, QString::number((int)(start_time + time * pcnt)));

	double movie_time = MovieLength();
	m_cur_time = pcnt * movie_time;
	ChangeValue(m_progress_text, Fmt2(m_cur_time));
}

// wx OnTimeEnter (EVT_TEXT of the time text). wx ignores the typed value
// and re-renders m_cur_time; here the typed time is used.
void MoviePanel::OnTimeEnter()
{
	if (m_running) return;
	double movie_time = MovieLength();
	if (movie_time <= 0.0) return;
	m_cur_time = STOD(m_progress_text->text());
	double pcnt = (m_cur_time / movie_time);
	{
		QSignalBlocker b(m_progress_sldr);
		m_progress_sldr->setValue(int(360. * pcnt));
	}
	SetRendering(pcnt);

	int start_time = STOI(m_time_start_text->text());
	int end_time = STOI(m_time_end_text->text());
	int time = end_time - start_time + 1;
	ChangeValue(m_time_current_text, QString::number((int)(start_time + time * pcnt)));
}

void MoviePanel::SetProgress(double pcnt)
{
	pcnt = std::fabs(pcnt);
	{
		QSignalBlocker b(m_progress_sldr);
		m_progress_sldr->setValue(int(pcnt * 360.));
	}
	double movie_time = MovieLength();
	m_cur_time = pcnt * movie_time;
	ChangeValue(m_progress_text, Fmt2(m_cur_time));
}

void MoviePanel::SetMovieTime(double t)
{
	double runtime = MovieLength();
	double pcnt = runtime != 0.0 ? (m_cur_time / runtime) : 0.0;

	if (m_current_page == 0) {
		ChangeValue(m_movie_time, Fmt2(t));
		m_movie_time_basic = t;
	}
	else if (m_current_page == 1)
	{
		ChangeValue(m_movie_time, Fmt2(t));
	}

	SetProgress(pcnt);
}

void MoviePanel::SetInitRotation()
{
	if (!m_canvas) return;
	int slider_pos = m_progress_sldr->value();

	//basic options
	int rot_axis = 1;
	if (m_x_rd->isChecked())
		rot_axis = 1;
	if (m_y_rd->isChecked())
		rot_axis = 2;
	if (m_z_rd->isChecked())
		rot_axis = 3;

	double x, y, z;
	ViewGetRotations(x, y, z);
	if (rot_axis == 1)
		m_starting_rot = x - slider_pos;
	else if (rot_axis == 2)
		m_starting_rot = y - slider_pos;
	else
		m_starting_rot = z - slider_pos;
	while (m_starting_rot > 360.) m_starting_rot -= 360.;
	while (m_starting_rot < -360.) m_starting_rot += 360.;
	if (360. - std::abs(m_starting_rot) < 0.001)
		m_starting_rot = 0.;
}

void MoviePanel::SetRendering(double pcnt)
{
	if (m_without_rendering)
		return;
	if (!m_canvas)
		return;

	//advanced options
	if (m_current_page == 1) {
		if (!m_interpolator)
			return;
		int end_frame = int(m_interpolator->GetLastT());
		m_canvas->SetParams(m_interpolator, pcnt * end_frame);
		m_canvas->RefreshView();
		if (m_record) m_canvas->RequestRecordedFrame();
		return;
	}
	//basic options
	int start_time = STOI(m_time_start_text->text());
	int end_time = STOI(m_time_end_text->text());
	int time = end_time - start_time + 1;
	time = int(start_time + time * pcnt + 0.5);

	if (m_seq_chk->isChecked()) {
		int first = 0, sec = 0, tmp = 0;
		m_canvas->Get4DSeqFrames(first, sec, tmp);
		if (sec - first > 0) {
			m_canvas->Set4DSeqFrame(time);
		}
		else
			m_canvas->Set3DBatFrame(time);
	}
	if (m_rot_chk->isChecked() && m_current_page == 0) {
		double x, y, z, val = 0.0;
		ViewGetRotations(x, y, z);
		if (m_x_rd->isChecked())
			val = x;
		if (m_y_rd->isChecked())
			val = y;
		if (m_z_rd->isChecked())
			val = z;

		double deg = STOD(m_degree_end->text());

		if (!m_rewind_chk->isChecked())
		{
			if (m_rot_int_type == 0)
				val = m_starting_rot + pcnt * deg;
			else if (m_rot_int_type == 1)
				val = m_starting_rot +
				(-2.0 * pcnt * pcnt * pcnt + 3.0 * pcnt * pcnt) * deg;
		}
		else
		{
			if (0.25 <= pcnt && pcnt < 0.75)
			{
				pcnt = (pcnt - 0.25) * 2.0;
				if (m_rot_int_type == 0)
					val = m_starting_rot + (1.0 - 2.0 * pcnt) * deg;
				else if (m_rot_int_type == 1)
					val = m_starting_rot +
					(4.0 * pcnt * pcnt * pcnt - 6.0 * pcnt * pcnt + 1.0) * deg;
			}
			else
			{
				if (0.0 <= pcnt && pcnt < 0.25)
					pcnt = (pcnt + 0.25) * 2.0;
				else if (0.75 <= pcnt && pcnt <= 1.0)
					pcnt = (pcnt - 0.75) * 2.0;

				if (m_rot_int_type == 0)
					val = m_starting_rot + (2.0 * pcnt - 1.0) * deg;
				else if (m_rot_int_type == 1)
					val = m_starting_rot +
					(-4.0 * pcnt * pcnt * pcnt + 6.0 * pcnt * pcnt - 1.0) * deg;
			}
		}

		if (m_x_rd->isChecked())
			ViewSetRotations(val, y, z);
		else if (m_y_rd->isChecked())
			ViewSetRotations(x, val, z);
		else if (m_z_rd->isChecked())
			ViewSetRotations(x, y, val);
		m_canvas->RefreshView();
	}
	if (m_record) m_canvas->RequestRecordedFrame();
}

// ---------------------------------------------------------------- playback

void MoviePanel::OnTimer()
{
	//get all of the progress info
	double len = MovieLength();
	long fps = GetFPS();
	if (fps <= 0 || len <= 0.0)
	{
		OnStop();
		return;
	}

	if (m_canvas)
	{
		// wx: wait for the recorded frame / the streaming loop
		if ((m_record && !m_canvas->IsRecordedFrameReady()) ||
			(!m_record && m_canvas->IsLoopRunning()))
		{
			m_canvas->RefreshNoLoopRestart();
			return;
		}
	}

	if (m_delayed_stop)
	{
		if (m_record)
			WriteFrameToFile(int(fps * len + 0.5));
		m_delayed_stop = false;
		OnStop();
		return;
	}

	//move forward in time (limits FPS usability to 100 FPS)
	m_cur_time += 1.0 / double(fps);
	//frame only increments when time passes a whole number
	int frame = int(fps * m_cur_time + 0.5);
	SetProgress(m_cur_time / len);
	//update the rendering frame since we have advanced.
	if (frame != m_last_frame) {
		int start_time = STOI(m_time_start_text->text());
		int end_time = STOI(m_time_end_text->text());
		int tot_time = end_time - start_time + 1;
		ChangeValue(m_time_current_text, QString::number(
			((int)(start_time + tot_time * m_cur_time / len + 0.5))));
		if (m_record)
			WriteFrameToFile(int(fps * len + 0.5));
		m_last_frame = frame;
		SetRendering(m_cur_time / len);

		//run script for current frame
		if (m_batch_mode && m_canvas && m_hooks.run_4d_script)
		{
			m_timer->stop();
			if (m_prv_fr != m_canvas->GetTSeqCurNum())
			{
				m_hooks.run_4d_script();
				m_prv_fr = m_canvas->GetTSeqCurNum();
			}
			m_timer->start(int(1000.0 / double(fps) + 0.5));
		}
	}
	if (len - m_cur_time < 0.1 / double(fps) || m_cur_time > len)
	{
		if (!m_repeat_chk->isChecked() || m_record || m_batch_mode) m_delayed_stop = true;
		else m_cur_time = 0.0;
		m_batch_mode = false;
	}
}

void MoviePanel::OnPrev()
{
	if (m_running) {
		OnStop();
		return;
	}
	if (!m_canvas) return;
	m_running = true;
	m_play_btn->setIcon(VVD_ICON(pause));
	Q_EMIT runningChanged(true);
	int slider_pos = m_progress_sldr->value();
	long fps = GetFPS();
	if (fps <= 0) fps = 1;
	double len = MovieLength();

	SetInitRotation();

	if (slider_pos < 360 && slider_pos > 0 &&
		!(len - m_cur_time < 0.1 / double(fps) || m_cur_time > len)) {
		m_timer->start(int(1000.0 / double(fps) + 0.5));
		return;
	}

	//advanced options
	int page = m_tabs->currentIndex();
	if (page <= 1) m_current_page = page;
	if (m_current_page == 1) {
		if (!m_interpolator)
			return;
		int frames = int(m_interpolator->GetLastT());
		double runtime = (double)frames / (double)fps;
		ChangeValue(m_movie_time, Fmt2(runtime));
	}
	SetProgress(0.);
	SetRendering(0.);
	m_last_frame = 0;
	m_delayed_stop = false;
	m_timer->start(int(1000.0 / double(fps) + 0.5));

	//run script for current frame
	if (m_hooks.script_enabled && m_hooks.script_enabled())
	{
		m_batch_mode = true;
		m_timer->stop();
		m_prv_fr = m_canvas->GetTSeqCurNum();
		if (m_hooks.run_4d_script) m_hooks.run_4d_script();
		m_timer->start(int(1000.0 / double(fps) + 0.5));
	}
}

void MoviePanel::OnStop()
{
	m_play_btn->setIcon(VVD_ICON(play));
	m_timer->stop();
	const bool was_running = m_running;
	const bool was_recording = m_record;
	m_running = false;
	m_record = false;
	m_delayed_stop = false;
	if (m_encoder_open)
	{
		m_encoder->close();
		m_encoder_open = false;
	}
	if (m_canvas)
		m_canvas->SetRecording(false);
	if (was_running)
		Q_EMIT runningChanged(false);
	if (was_recording)
		Q_EMIT movieSaved(m_full_filename, m_record_ok);
}

void MoviePanel::OnRewind()
{
	if (!m_canvas) return;
	m_canvas->SetParams(m_interpolator, 0.);
	m_canvas->RefreshView();
	OnStop();
	ChangeValue(m_time_current_text, QString::number(0));
	SetProgress(0.);
	SetRendering(0.);
}

void MoviePanel::OnPageChanged(int pageid)
{
	m_stack->setCurrentIndex(pageid);
	long fps = GetFPS();
	if (fps <= 0) fps = 1;

	double runtime = MovieLength();
	double pcnt = runtime != 0.0 ? (m_cur_time / runtime) : 0.0;

	switch (pageid)
	{
	case 0:
		if (m_current_page != 0) {
			ChangeValue(m_movie_time, Fmt2(m_movie_time_basic));
			m_current_page = 0;
		}
		break;
	case 1:
		if (m_current_page != 1) {
			if (!m_interpolator)
				break;
			int frames = int(m_interpolator->GetLastT());
			if (frames > 0)
			{
				runtime = (double)frames / (double)fps;
				m_movie_time_basic = MovieLength();
				ChangeValue(m_movie_time, Fmt2(runtime));
			}
			m_current_page = 1;
		}
		break;
	}

	SetInitRotation();
	SetProgress(pcnt);
	SetRendering(pcnt);
}

void MoviePanel::GenKey()
{
	QTreeWidgetItem* it = m_auto_key_list->currentItem();
	if (!it || !it->isSelected())
		return;
	int item = m_auto_key_list->indexOfTopLevelItem(it);
	if (item == 0)
		m_advanced_movie->AutoKeyChanComb(1);
	else if (item == 1)
		m_advanced_movie->AutoKeyChanComb(2);
	else if (item == 2)
		m_advanced_movie->AutoKeyChanComb(3);
	else
		return;
	SetMode(MODE_ADVANCED);
}

// ---------------------------------------------------------------- cropping

void MoviePanel::SetFrameFields(int x, int y, int w, int h)
{
	{
		QSignalBlocker b1(m_center_x), b2(m_center_y), b3(m_width), b4(m_height);
		m_center_x->setValue(int(x + w / 2.0 + 0.5));
		m_center_y->setValue(int(y + h / 2.0 + 0.5));
		m_width->setValue(w);
		m_height->setValue(h);
	}
	// wx: each SetValue sent EVT_TEXT -> OnFrameEditing
	OnFrameEditing();
}

void MoviePanel::PushFrame(bool enable, int x, int y, int w, int h)
{
	if (m_overlays)
	{
		m_overlays->SetFrame(x, y, w, h);
		m_overlays->SetDrawFrame(enable);
	}
	else if (m_canvas)
		m_canvas->SetCaptureFrame(enable, x, y, w, h);
}

void MoviePanel::OnFrameCheck(bool)
{
	if (!m_canvas) return;
	int x, y, w, h;
	m_canvas->GetFrame(x, y, w, h);
	if (m_frame_chk->isChecked())
	{
		m_canvas->CalcFrame(x, y, w, h);
		PushFrame(false, x, y, w, h);
		SetFrameFields(x, y, w, h);
		m_canvas->GetFrame(x, y, w, h);
		PushFrame(true, x, y, w, h);   // wx EnableFrame
	}
	else
		PushFrame(false, x, y, w, h);  // wx DisableFrame
	m_canvas->RefreshView();
}

void MoviePanel::OnResetFrame()
{
	if (!m_canvas) return;
	int x, y, w, h;
	m_canvas->CalcFrame(x, y, w, h);
	PushFrame(m_canvas->GetFrameEnabled(), x, y, w, h);
	SetFrameFields(x, y, w, h);
	m_canvas->RefreshView();
}

void MoviePanel::OnFrameEditing()
{
	if (!m_canvas) return;
	int x = m_center_x->value();
	int y = m_center_y->value();
	int w = m_width->value();
	int h = m_height->value();
	const bool enabled = m_canvas->GetFrameEnabled();
	PushFrame(enabled, int(x - w / 2.0 + 0.5), int(y - h / 2.0 + 0.5), w, h);
	if (enabled)
		m_canvas->RefreshView();
}

// ---------------------------------------------------------------- saving

// wx OnRun: "Save Movie Sequence" dialog with CreateExtraCaptureControl
void MoviePanel::OnRun()
{
	QFileDialog dlg(this, QStringLiteral("Save Movie Sequence"), QString(),
		QStringLiteral("MOV file (*.mov);;TIF files (*.tif);;MP4 file (*.mp4)"));
	dlg.setAcceptMode(QFileDialog::AcceptSave);
	dlg.setFileMode(QFileDialog::AnyFile);
	dlg.setOption(QFileDialog::DontUseNativeDialog, true);   // room for the extra controls
	dlg.setDefaultSuffix(QStringLiteral("mov"));
	dlg.selectFile(QStringLiteral("output"));
	QObject::connect(&dlg, &QFileDialog::filterSelected, &dlg, [&dlg](const QString& f) {
		if (f.contains("*.tif")) dlg.setDefaultSuffix("tif");
		else if (f.contains("*.mp4")) dlg.setDefaultSuffix("mp4");
		else dlg.setDefaultSuffix("mov");
	});

	// ---- CreateExtraCaptureControl
	auto* panel = new QGroupBox(QStringLiteral("Additional Options"), &dlg);
	auto* group1 = new QVBoxLayout(panel);
	group1->setSpacing(5);

	//copy all files check box
	const bool proj_save = m_hooks.get_proj_save ? m_hooks.get_proj_save() : false;
	if (proj_save)
	{
		auto* ch_embed = new QCheckBox(QStringLiteral("Embed all files in the project folder"), panel);
		ch_embed->setChecked(m_hooks.get_embed ? m_hooks.get_embed() : false);
		connect(ch_embed, &QCheckBox::toggled, panel, [this](bool on) {   // wx OnChEmbedCheck
			if (m_hooks.set_embed) m_hooks.set_embed(on);
		});
		group1->addWidget(ch_embed);
	}

	//compressed TIFF
	auto* line1 = new QHBoxLayout;
	auto* tiffopts = new QLabel(QStringLiteral("TIFF Options:"), panel);
	tiffopts->setMinimumWidth(95);
	auto* ch1 = new QCheckBox(QStringLiteral("Lempel-Ziv-Welch Compression"), panel);
	ch1->setChecked(m_hooks.get_compress ? m_hooks.get_compress() : true);
	connect(ch1, &QCheckBox::toggled, panel, [this](bool on) {   // wx OnCh1Check
		if (m_hooks.set_compress) m_hooks.set_compress(on);
	});
	line1->addWidget(tiffopts);
	line1->addWidget(ch1);
	line1->addStretch(1);
	group1->addLayout(line1);

	// movie quality
	auto* line2 = new QHBoxLayout;
	auto* MOVopts = new QLabel(QStringLiteral("MOV Options:"), panel);
	MOVopts->setMinimumWidth(95);
	auto* bitrate_text = new QLineEdit(QString::asprintf("%.1f", m_Mbitrate), panel);
	bitrate_text->setFixedWidth(50);
	auto* st = new QLabel(QStringLiteral("Bitrate:"), panel);
	st->setMinimumWidth(50);
	auto* st2 = new QLabel(QStringLiteral("Mb/s (0 - 20 Mb/s)"), panel);
	st2->setMinimumWidth(140);
	auto* st3 = new QLabel(QStringLiteral("Estimated size:"), panel);
	st3->setMinimumWidth(110);
	auto* estimated_size_text = new QLineEdit(QStringLiteral("0.25"), panel);
	estimated_size_text->setFixedWidth(50);
	estimated_size_text->setEnabled(false);
	{
		double size = m_Mbitrate * STOI(m_movie_time->text()) / 8.;
		estimated_size_text->setText(Fmt2(size));
	}
	connect(bitrate_text, &QLineEdit::textChanged, panel, [this, estimated_size_text](const QString& t) {   // wx OnMovieQuality
		m_Mbitrate = STOD(t);
		double size = m_Mbitrate * STOD(m_movie_time->text()) / 8.;
		estimated_size_text->setText(Fmt2(size));
	});
	line2->addWidget(MOVopts);
	line2->addWidget(st);
	line2->addSpacing(5);
	line2->addWidget(bitrate_text);
	line2->addSpacing(5);
	line2->addWidget(st2);
	line2->addStretch(1);
	line2->addWidget(st3);
	line2->addWidget(estimated_size_text);
	line2->addSpacing(5);
	line2->addWidget(new QLabel(QStringLiteral("MB"), panel));
	group1->addLayout(line2);
	group1->addWidget(new QLabel(QStringLiteral(
		"NOTE: Please make sure that the movie length is greater than 36 frames for correct compression!"), panel));

	if (auto* grid = qobject_cast<QGridLayout*>(dlg.layout()))
		grid->addWidget(panel, grid->rowCount(), 0, 1, grid->columnCount());
	else if (dlg.layout())
		dlg.layout()->addWidget(panel);

	// testing: VVD_QT_GRAB_MOVDLG=<file.png> saves a picture of this dialog and cancels it
	const QString grab_dlg = qEnvironmentVariable("VVD_QT_GRAB_MOVDLG");
	if (!grab_dlg.isEmpty())
		QTimer::singleShot(1500, &dlg, [&dlg, grab_dlg]() { dlg.grab().save(grab_dlg); dlg.reject(); });

	if (dlg.exec() != QDialog::Accepted)
		return;
	const QStringList files = dlg.selectedFiles();
	if (files.isEmpty() || files.first().isEmpty())
		return;
	OnRewind();
	SaveMovie(files.first());
}

void MoviePanel::SaveMovie(const QString& filename)
{
	if (!m_canvas || filename.isEmpty()) return;
	if (m_running) OnStop();
	m_full_filename = filename;
	m_record_ok = true;

	const QString suffix = QFileInfo(filename).suffix();
	m_filetype = suffix.isEmpty() ? QString() : "." + suffix.toLower();
	if (m_filetype == ".mov" || m_filetype == ".mp4") {
		int x, y, w, h;
		if (m_frame_chk->isChecked())
			m_canvas->GetFrame(x, y, w, h);
		else {
			x = 0;
			y = 0;
			w = m_canvas->GetDeviceSize().width();
			h = m_canvas->GetDeviceSize().height();
		}
		long fps = std::max(1L, GetFPS());
		m_video_w = w;
		m_video_h = h;
		m_encoder_open = m_encoder->open(QFile::encodeName(filename).toStdString(), w, h, fps,
			(size_t)(m_Mbitrate * 1000000));
		if (!m_encoder_open)
		{
			qWarning() << "MoviePanel: cannot open the video encoder for" << filename;
			m_record_ok = false;
		}
	}
	// wx: m_filename minus the 4-character extension
	m_filename = suffix.isEmpty() ? filename : filename.left(filename.length() - suffix.length() - 1);
	m_record = true;
	m_canvas->SetRecording(true);

	if (m_hooks.get_proj_save && m_hooks.get_proj_save() && m_hooks.save_project)
	{
		// wx: new_folder = m_filename + "_project"; prop_file = new_folder + "/" +
		// m_filename + "_project.vrp" (m_filename is the full path there, which
		// makes the wx path invalid; the file name is used here)
		const QString new_folder = m_filename + "_project";
		QDir().mkpath(new_folder);
		const QString prop_file = new_folder + "/" + QFileInfo(m_filename).fileName() + "_project.vrp";
		m_hooks.save_project(prop_file);
	}
	OnPrev();
}

void MoviePanel::WriteFrameToFile(int total_frames)
{
	if (!m_canvas) return;
	const QString s_length = QString::number(total_frames);
	const int length = s_length.length();
	QString suffix;

	if (m_seq_chk->isChecked()) {
		int first = 0, sec = 0, tmp = 0;
		m_canvas->Get4DSeqFrames(first, sec, tmp);
		if (sec - first <= 0) { // batch mode
			m_canvas->Get3DBatFrames(first, sec, tmp);
			if (sec - first > 0) {
				VolumeData* vd = m_canvas->GetCurrentVolume();
				if (vd && vd->GetDisp())
					suffix = "_" + vd->GetName().ToQt();
			}
		}
	}

	const QString outputfilename = m_filename + "_" +
		QString("%1").arg(m_last_frame, length, 10, QChar('0')) + suffix + ".tif";

	//capture
	int nx = 0, ny = 0;
	bool bgr = false;
	const std::vector<unsigned char>& image = m_canvas->GetRecordedFrame(nx, ny, bgr);
	if (image.empty() || nx <= 0 || ny <= 0)
	{
		m_record_ok = false;
		return;
	}
	const int chann = 4; //RGB or RGBA
	const int dst_chann = 3;
	int x = 0, y = 0, w = nx, h = ny;
	if (m_frame_chk->isChecked())
	{
		m_canvas->GetFrame(x, y, w, h);
		x = std::clamp(x, 0, nx);
		y = std::clamp(y, 0, ny);
		w = std::clamp(w, 0, nx - x);
		h = std::clamp(h, 0, ny - y);
	}
	const bool video = (m_filetype == ".mov" || m_filetype == ".mp4");
	// the encoder was opened with the frame size of the first frame
	const int ow = video ? m_video_w : w;
	const int oh = video ? m_video_h : h;
	if (ow <= 0 || oh <= 0) { m_record_ok = false; return; }

	std::vector<unsigned char> rgb_image((size_t)ow * oh * dst_chann, 0);
	for (int yy = 0; yy < std::min(h, oh); yy++)
	{
		const unsigned char* s = &image[((size_t)(yy + y) * nx + x) * chann];
		unsigned char* d = &rgb_image[(size_t)yy * ow * dst_chann];
		for (int xx = 0; xx < std::min(w, ow); xx++, s += chann, d += dst_chann)
		{
			if (bgr) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; }
			else { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
		}
	}

	if (video) {
		if (m_encoder_open)
		{
			// (write_video_frame returns false on success; wx ignores it too)
			if (m_encoder->set_frame_rgb_data(rgb_image.data()))
				m_encoder->write_video_frame(m_last_frame);
			else
				m_record_ok = false;
		}
	}
	else
	{
		const bool compress = m_hooks.get_compress ? m_hooks.get_compress() : true;
		if (!CaptureWriter::WriteRGB8Tiff(outputfilename, rgb_image.data(), ow, oh, (size_t)ow * dst_chann, compress))
		{
			qWarning() << "MoviePanel: cannot write" << outputfilename;
			m_record_ok = false;
		}
	}
}

// ---------------------------------------------------------------- settings

namespace {
QString MovieSettingsPath(bool for_save)
{
	const VVDString name(L"default_movie_settings.dft");
	return QString::fromStdWString(for_save ? vvd::SettingsWritePath(name) : vvd::SettingsReadPath(name));
}
}

void MoviePanel::LoadSettings()
{
	vvd::IniFile fconfig;
	if (!fconfig.Load(VVDString(MovieSettingsPath(false).toStdWString())))
		return;
	double dVal;
	if (fconfig.Read(VVDString(L"/recorder_bitrate"), dVal))
		m_Mbitrate = dVal;
	m_loaded_bitrate = m_Mbitrate;
}

void MoviePanel::SaveDefault()
{
	// wx writes the file on every exit; it is written here only when the
	// bitrate was changed
	if (m_Mbitrate == m_loaded_bitrate)
		return;
	vvd::IniFile fconfig;
	fconfig.Write(VVDString(L"/recorder_bitrate"), m_Mbitrate);
	fconfig.Save(VVDString(MovieSettingsPath(true).toStdWString()));
}

// ---------------------------------------------------------------- project

void MoviePanel::SaveProjectSettings(ProjectIO::ProjectMovie& mv) const
{
	mv.valid = true;
	mv.views_cmb = m_views_cmb->currentIndex();
	mv.rot_check = m_rot_chk->isChecked();
	mv.seq_check = m_seq_chk->isChecked();
	mv.frame_range = m_progress_sldr->maximum();
	mv.time_frame = m_progress_sldr->value();
	mv.x_rd = m_x_rd->isChecked();
	mv.y_rd = m_y_rd->isChecked();
	mv.z_rd = m_z_rd->isChecked();
	mv.angle_end_text = m_degree_end->text();
	mv.step_text = m_movie_time->text();
	mv.frames_text = m_fps_text->text();
	mv.frame_chk = m_frame_chk->isChecked();
	// wx writes the text controls, empty until a frame was set
	const bool crop_set = m_width->value() != 0 || m_height->value() != 0;
	mv.center_x_text = crop_set ? QString::number(m_center_x->value()) : QString();
	mv.center_y_text = crop_set ? QString::number(m_center_y->value()) : QString();
	mv.width_text = crop_set ? QString::number(m_width->value()) : QString();
	mv.height_text = crop_set ? QString::number(m_height->value()) : QString();
	mv.time_start_text = m_time_start_text->text();
	mv.time_cur_text = m_time_current_text->text();
	mv.time_end_text = m_time_end_text->text();
	mv.progress_text = m_progress_text->text();
}

// wx VRenderFrame::OpenProject, /movie_panel (+ the final SetProgress)
void MoviePanel::LoadProjectSettings(const ProjectIO::ProjectMovie& mv)
{
	if (!mv.valid) return;
	DisableRendering();

	if (mv.views_cmb >= 0 && mv.views_cmb < m_views_cmb->count())
		m_views_cmb->setCurrentIndex(mv.views_cmb);
	ResolveView();
	if (mv.rot_check) EnableRot(); else DisableRot();
	if (mv.seq_check) EnableTime(); else DisableTime();
	// wx reads x_rd and y_rd only; z_rd is honoured here
	if (mv.x_rd) m_x_rd->setChecked(true);
	else if (mv.y_rd) m_y_rd->setChecked(true);
	else if (mv.z_rd) m_z_rd->setChecked(true);
	m_degree_end->setText(mv.angle_end_text);
	m_movie_time->setText(mv.step_text);
	m_movie_time_basic = STOD(mv.step_text);   // wx GetSettings: m_mov_step
	m_fps_text->setText(mv.frames_text);
	{
		QSignalBlocker b(m_frame_chk);
		m_frame_chk->setChecked(mv.frame_chk);
	}
	bool bx = !mv.center_x_text.isEmpty(), by = !mv.center_y_text.isEmpty();
	bool bw = !mv.width_text.isEmpty(), bh = !mv.height_text.isEmpty();
	{
		QSignalBlocker b1(m_center_x), b2(m_center_y), b3(m_width), b4(m_height);
		if (bx) m_center_x->setValue(STOI(mv.center_x_text));
		if (by) m_center_y->setValue(STOI(mv.center_y_text));
		if (bw) m_width->setValue(STOI(mv.width_text));
		if (bh) m_height->setValue(STOI(mv.height_text));
	}
	int fx = 0, fy = 0, fw = 0, fh = 0;
	if (m_canvas) m_canvas->GetFrame(fx, fy, fw, fh);
	if (bx && by && bw && bh)
	{
		const int cx = STOI(mv.center_x_text), cy = STOI(mv.center_y_text);
		fw = STOI(mv.width_text);
		fh = STOI(mv.height_text);
		fx = int(cx - fw / 2.0 + 0.5);
		fy = int(cy - fh / 2.0 + 0.5);
	}
	PushFrame(mv.frame_chk, fx, fy, fw, fh);
	ChangeValue(m_time_start_text, mv.time_start_text);
	ChangeValue(m_time_end_text, mv.time_end_text);
	ChangeValue(m_time_current_text, mv.time_cur_text);

	EnableRendering();

	if (!mv.step_text.isEmpty() && !mv.progress_text.isEmpty())
	{
		double movcur = STOD(mv.progress_text), movlen = STOD(mv.step_text);
		SetProgress(movlen != 0.0 ? movcur / movlen : 0.0);
	}
	else
		SetProgress(0.0);
	if (m_canvas) m_canvas->RefreshView();
}
