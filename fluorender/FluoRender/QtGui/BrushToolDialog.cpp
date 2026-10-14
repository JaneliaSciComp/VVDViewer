#include "BrushToolDialog.h"
#include "TestRun.h"
#include "BrushTool.h"
#include "AnalysisContext.h"
#include "RenderCanvas.h"

#include <QTabWidget>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QSlider>
#include <QLineEdit>
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>
#include <QPushButton>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDoubleValidator>
#include <QIntValidator>
#include <QSignalBlocker>
#include <QLocale>
#include <QTimer>
#include <QCursor>
#include <QPixmap>
#include <QIcon>

#include <cmath>

#include "DataManager.h"
#include "VolumeSelector.h"
#include "VolumeCalculator.h"
#include "FLIVR/Texture.h"
#include "FLIVR/VolumeRenderer.h"
#include "Core/FileUtil.h"
#include "img/icons.h"

namespace
{
	QIcon IconFromPng(const unsigned char* data, int len)
	{
		QPixmap pm;
		pm.loadFromData(data, (uint)len, "PNG");
		return QIcon(pm);
	}
#define VVD_ICON(name) IconFromPng(icons::name##_png, (int)sizeof(icons::name##_png))

	double ToDouble(const QString& s)
	{
		bool ok = false;
		double v = QLocale::c().toDouble(s.trimmed(), &ok);
		return ok ? v : 0.0;
	}

	QLabel* Label(const QString& text, QWidget* parent, int w = -1, int h = -1)
	{
		auto* st = new QLabel(text, parent);
		if (w > 0) st->setMinimumWidth(w);   // wx sizes are hints; never clip the text
		if (h > 0) st->setMinimumHeight(h);
		return st;
	}

	QLineEdit* Text(const QString& text, QWidget* parent, int w, QValidator* v)
	{
		auto* t = new QLineEdit(text, parent);
		t->setFixedWidth(w);
		if (v) t->setValidator(v);
		return t;
	}

	QSlider* Slider(int val, int min, int max, QWidget* parent)
	{
		auto* s = new QSlider(Qt::Horizontal, parent);
		s->setRange(min, max);
		s->setValue(val);
		return s;
	}

	// wxCheckBox with wxALIGN_RIGHT: label left of the box
	QCheckBox* CheckRight(const QString& text, QWidget* parent)
	{
		auto* c = new QCheckBox(text, parent);
		c->setLayoutDirection(Qt::RightToLeft);
		return c;
	}

	void SetSlider(QSlider* s, int v) { QSignalBlocker b(s); s->setValue(v); }
	void SetSliderRange(QSlider* s, int min, int max) { QSignalBlocker b(s); s->setRange(min, max); }
	// wxTextCtrl::ChangeValue (no text event)
	void ChangeValue(QLineEdit* t, const QString& s) { QSignalBlocker b(t); t->setText(s); }

	QString VolName(VolumeData* vd) { return vd ? vd->GetName().ToQt() : QString(); }
}

// ---------------------------------------------------------------------------
// pages (BrushToolDlg::CreateBrushPage / CreateCalculationPage / CreateAnalysisPage)
// ---------------------------------------------------------------------------

QWidget* BrushToolDialog::CreateBrushPage(QWidget* parent)
{
	auto* page = new QWidget(parent);
	QLabel* st = nullptr;
	//validator: floating point 1
	auto* vald_fp1 = new QDoubleValidator(0.0, 1e12, 1, page);
	vald_fp1->setNotation(QDoubleValidator::StandardNotation);
	vald_fp1->setLocale(QLocale::c());
	//validator: floating point 2
	auto* vald_fp2 = new QDoubleValidator(0.0, 1e12, 2, page);
	vald_fp2->setNotation(QDoubleValidator::StandardNotation);
	vald_fp2->setLocale(QLocale::c());
	//validator: integer
	auto* vald_int = new QIntValidator(0, 2147483647, page);

	//toolbar
	m_toolbar = new QToolBar(page);
	m_toolbar->setIconSize(QSize(20, 20));
	m_toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
	m_toolbar->setStyleSheet("QToolBar { border: none; }");
	m_undo_act = m_toolbar->addAction(VVD_ICON(undo), "Undo");
	m_undo_act->setToolTip("Rollback previous brush operation");
	m_redo_act = m_toolbar->addAction(VVD_ICON(redo), "Redo");
	m_redo_act->setToolTip("Redo the rollback brush operation");
	m_toolbar->addSeparator();
	m_append_act = m_toolbar->addAction(VVD_ICON(listicon_brushappend), "Select");
	m_append_act->setCheckable(true);
	m_append_act->setToolTip("Highlight structures by painting on the render view (hold Shift)");
	m_diffuse_act = m_toolbar->addAction(VVD_ICON(listicon_brushdiffuse), "Diffuse");
	m_diffuse_act->setCheckable(true);
	m_diffuse_act->setToolTip("Diffuse highlighted structures by painting (hold Z)");
	m_desel_act = m_toolbar->addAction(VVD_ICON(listicon_brushdesel), "Unselect");
	m_desel_act->setCheckable(true);
	m_desel_act->setToolTip("Reset highlighted structures by painting (hold X)");
	QAction* clear_act = m_toolbar->addAction(VVD_ICON(listicon_brushclear), "Reset All");
	clear_act->setToolTip("Reset all highlighted structures");
	m_toolbar->addSeparator();
	QAction* erase_act = m_toolbar->addAction(VVD_ICON(listicon_brusherase), "Erase");
	erase_act->setToolTip("Erase highlighted structures");
	QAction* create_act = m_toolbar->addAction(VVD_ICON(listicon_brushcreate), "Extract");
	create_act->setToolTip("Extract highlighted structures out and create a new volume");

	connect(m_undo_act, &QAction::triggered, this, &BrushToolDialog::BrushUndo);
	connect(m_redo_act, &QAction::triggered, this, &BrushToolDialog::BrushRedo);
	connect(m_append_act, &QAction::triggered, this, &BrushToolDialog::OnBrushAppend);
	connect(m_diffuse_act, &QAction::triggered, this, &BrushToolDialog::OnBrushDiffuse);
	connect(m_desel_act, &QAction::triggered, this, &BrushToolDialog::OnBrushDesel);
	connect(clear_act, &QAction::triggered, this, &BrushToolDialog::OnBrushClear);
	connect(erase_act, &QAction::triggered, this, &BrushToolDialog::OnBrushErase);
	connect(create_act, &QAction::triggered, this, &BrushToolDialog::OnBrushCreate);

	//Selection adjustment
	auto* box1 = new QGroupBox("Selection Settings", page);
	auto* sizer1 = new QVBoxLayout(box1);
	//stop at boundary
	auto* sizer1_1 = new QHBoxLayout();
	m_edge_detect_chk = CheckRight("Edge Detect:", page);
	m_hidden_removal_chk = CheckRight("Visible Only:", page);
	m_select_group_chk = CheckRight("Apply to Group:", page);
	m_brush_use_absolute_chk = CheckRight("Absolute Value:", page);
	sizer1_1->addWidget(m_edge_detect_chk, 0, Qt::AlignVCenter);
	sizer1_1->addSpacing(5);
	sizer1_1->addWidget(m_hidden_removal_chk, 0, Qt::AlignVCenter);
	sizer1_1->addSpacing(5);
	sizer1_1->addWidget(m_select_group_chk, 0, Qt::AlignVCenter);
	sizer1_1->addSpacing(5);
	sizer1_1->addWidget(m_brush_use_absolute_chk, 0, Qt::AlignVCenter);
	sizer1_1->addStretch(1);
	m_edge_detect_chk->hide();
	m_hidden_removal_chk->hide();

	//threshold4
	auto* sizer1_2 = new QHBoxLayout();
	st = Label("Threshold:", page, 70, 20);
	m_brush_scl_translate_sldr = Slider(0, 0, 2550, page);
	m_brush_scl_translate_text = Text("0.0", page, 50, vald_fp1);
	sizer1_2->addSpacing(5);
	sizer1_2->addWidget(st, 0, Qt::AlignVCenter);
	sizer1_2->addWidget(m_brush_scl_translate_sldr, 1);
	sizer1_2->addWidget(m_brush_scl_translate_text, 0, Qt::AlignVCenter);
	sizer1_2->addSpacing(15);
	//2d
	auto* row1_4 = new QWidget(page);
	auto* sizer1_4 = new QHBoxLayout(row1_4);
	sizer1_4->setContentsMargins(0, 0, 0, 0);
	st = Label("2D Adj. Infl.:", row1_4, 70, 20);
	m_brush_2dinfl_sldr = Slider(100, 0, 200, row1_4);
	m_brush_2dinfl_sldr->setInvertedAppearance(true);   // wxSL_INVERSE
	m_brush_2dinfl_text = Text("1.00", row1_4, 40, vald_fp2);
	sizer1_4->addSpacing(5);
	sizer1_4->addWidget(st, 0, Qt::AlignVCenter);
	sizer1_4->addWidget(m_brush_2dinfl_sldr, 1);
	sizer1_4->addWidget(m_brush_2dinfl_text, 0, Qt::AlignVCenter);
	sizer1_4->addSpacing(15);
	//sizer1
	sizer1->addLayout(sizer1_1);
	sizer1->addSpacing(10);
	sizer1->addLayout(sizer1_2);
	sizer1->addWidget(row1_4);
	row1_4->hide();   // sizer1->Hide(sizer1_4, true)

	//Brush properties
	auto* box2 = new QGroupBox("Brush Properties", page);
	auto* sizer2 = new QVBoxLayout(box2);
	auto* sizer2_1 = new QHBoxLayout();
	st = Label("Brush sizes can also be set with mouse wheel in painting mode.", page);
	sizer2_1->addSpacing(5);
	sizer2_1->addWidget(st, 0, Qt::AlignVCenter);
	sizer2_1->addStretch(1);
	//brush size 1
	auto* sizer2_2 = new QHBoxLayout();
	st = Label("Center Size:", page, 80, 20);
	m_brush_size1_sldr = Slider(10, 1, 300, page);
	m_brush_size1_text = Text("10", page, 50, vald_int);
	sizer2_2->addSpacing(5);
	sizer2_2->addWidget(st, 0, Qt::AlignVCenter);
	sizer2_2->addWidget(m_brush_size1_sldr, 1);
	sizer2_2->addWidget(m_brush_size1_text, 0, Qt::AlignVCenter);
	sizer2_2->addWidget(Label("px", page, 25), 0, Qt::AlignVCenter);
	//brush size 2
	auto* sizer2_3 = new QHBoxLayout();
	m_brush_size2_chk = CheckRight("GrowSize", page);
	m_brush_size2_chk->setFixedWidth(80);
	auto* st_colon = Label(":", page, 5, 20);
	st_colon->hide();
	m_brush_size2_sldr = Slider(30, 1, 300, page);
	m_brush_size2_text = Text("30", page, 50, vald_int);
	m_brush_size2_chk->setChecked(true);
	m_brush_size2_sldr->setEnabled(true);
	m_brush_size2_text->setEnabled(true);
	m_brush_size2_chk->hide();
	m_brush_size2_sldr->hide();
	m_brush_size2_text->hide();
	sizer2_3->addWidget(m_brush_size2_chk, 0, Qt::AlignVCenter);
	sizer2_3->addWidget(st_colon, 0, Qt::AlignVCenter);
	sizer2_3->addWidget(m_brush_size2_sldr, 1);
	sizer2_3->addWidget(m_brush_size2_text, 0, Qt::AlignVCenter);
	st = Label("px", page, 25);
	st->hide();
	sizer2_3->addWidget(st, 0, Qt::AlignVCenter);
	//iterations
	auto* sizer2_4 = new QHBoxLayout();
	st = Label("Growth:", page, 70, 20);
	m_brush_iterw_rb = new QRadioButton("Weak", page);
	m_brush_iters_rb = new QRadioButton("Normal", page);
	m_brush_iterss_rb = new QRadioButton("Strong", page);
	auto* iter_group = new QButtonGroup(page);
	iter_group->addButton(m_brush_iterw_rb);
	iter_group->addButton(m_brush_iters_rb);
	iter_group->addButton(m_brush_iterss_rb);
	m_brush_iters_rb->setChecked(true);
	sizer2_4->addSpacing(5);
	sizer2_4->addWidget(st, 0, Qt::AlignVCenter);
	sizer2_4->addWidget(m_brush_iterw_rb, 0, Qt::AlignVCenter);
	sizer2_4->addSpacing(15);
	sizer2_4->addWidget(m_brush_iters_rb, 0, Qt::AlignVCenter);
	sizer2_4->addSpacing(15);
	sizer2_4->addWidget(m_brush_iterss_rb, 0, Qt::AlignVCenter);
	sizer2_4->addStretch(1);

	m_dslt_chk = new QCheckBox("Use DSLT", page);
	st_dslt_r = Label("Radius:", page, 70, 20);
	m_dslt_r_sldr = Slider(10, 1, 100, page);
	m_dslt_r_text = Text("10", page, 50, vald_int);
	auto* sizer2_5 = new QHBoxLayout();
	sizer2_5->addSpacing(5);
	sizer2_5->addWidget(st_dslt_r, 0, Qt::AlignVCenter);
	sizer2_5->addWidget(m_dslt_r_sldr, 1);
	sizer2_5->addWidget(m_dslt_r_text, 0, Qt::AlignVCenter);
	st_dslt_q = Label("Quality:", page, 70, 20);
	m_dslt_q_sldr = Slider(3, 1, 10, page);
	m_dslt_q_text = Text("3", page, 50, vald_int);
	auto* sizer2_6 = new QHBoxLayout();
	sizer2_6->addSpacing(5);
	sizer2_6->addWidget(st_dslt_q, 0, Qt::AlignVCenter);
	sizer2_6->addWidget(m_dslt_q_sldr, 1);
	sizer2_6->addWidget(m_dslt_q_text, 0, Qt::AlignVCenter);
	st_dslt_c = Label("C:", page, 70, 20);
	m_dslt_c_sldr = Slider(0, 0, 2550, page);
	m_dslt_c_text = Text("0.0", page, 50, vald_fp1);
	auto* sizer2_7 = new QHBoxLayout();
	sizer2_7->addSpacing(5);
	sizer2_7->addWidget(st_dslt_c, 0, Qt::AlignVCenter);
	sizer2_7->addWidget(m_dslt_c_sldr, 1);
	sizer2_7->addWidget(m_dslt_c_text, 0, Qt::AlignVCenter);

	//sizer2
	sizer2->addLayout(sizer2_4);
	sizer2->addSpacing(10);
	sizer2->addLayout(sizer2_2);
	sizer2->addLayout(sizer2_3);
	sizer2->addSpacing(10);
	sizer2->addLayout(sizer2_1);
	sizer2->addSpacing(10);
	sizer2->addWidget(m_dslt_chk, 0, Qt::AlignLeft);
	sizer2->addSpacing(5);
	sizer2->addLayout(sizer2_5);
	sizer2->addLayout(sizer2_6);
	sizer2->addLayout(sizer2_7);

	auto* box3 = new QGroupBox("Mask Display Settings", page);
	auto* sizer3 = new QVBoxLayout(box3);
	auto* sizer3_1 = new QHBoxLayout();
	st = Label("Mask Intensity:", page, 80, 20);
	m_mask_overlay_alpha_sldr = Slider(255, 0, 255, page);
	m_mask_overlay_alpha_text = Text("255", page, 50, vald_int);
	sizer3_1->addSpacing(5);
	sizer3_1->addWidget(st, 0, Qt::AlignVCenter);
	sizer3_1->addWidget(m_mask_overlay_alpha_sldr, 1);
	sizer3_1->addWidget(m_mask_overlay_alpha_text, 0, Qt::AlignVCenter);
	sizer3->addLayout(sizer3_1);

	//vertical sizer
	auto* sizer_v = new QVBoxLayout(page);
	sizer_v->addSpacing(10);
	sizer_v->addWidget(m_toolbar);
	sizer_v->addSpacing(30);
	sizer_v->addWidget(box1);
	sizer_v->addSpacing(30);
	sizer_v->addWidget(box2);
	sizer_v->addSpacing(30);
	sizer_v->addWidget(box3);
	sizer_v->addSpacing(30);
	sizer_v->addStretch(1);

	SetDSLTEnabled(false);
	for (QWidget* w : std::initializer_list<QWidget*>{ m_dslt_chk, st_dslt_r, m_dslt_r_sldr, m_dslt_r_text,
		st_dslt_q, m_dslt_q_sldr, m_dslt_q_text, st_dslt_c, m_dslt_c_sldr, m_dslt_c_text })
		w->hide();

	//events
	connect(m_brush_scl_translate_sldr, &QSlider::valueChanged, this, &BrushToolDialog::OnBrushSclTranslateChange);
	connect(m_brush_scl_translate_text, &QLineEdit::textChanged, this, &BrushToolDialog::OnBrushSclTranslateText);
	connect(m_brush_scl_translate_text, &QLineEdit::returnPressed, this, &BrushToolDialog::OnBrushSclTranslateTextEnter);
	connect(m_brush_2dinfl_sldr, &QSlider::valueChanged, this, &BrushToolDialog::OnBrush2dinflChange);
	connect(m_brush_2dinfl_text, &QLineEdit::textChanged, this, &BrushToolDialog::OnBrush2dinflText);
	connect(m_edge_detect_chk, &QCheckBox::toggled, this, &BrushToolDialog::OnBrushEdgeDetectChk);
	connect(m_hidden_removal_chk, &QCheckBox::toggled, this, &BrushToolDialog::OnBrushHiddenRemovalChk);
	connect(m_select_group_chk, &QCheckBox::toggled, this, &BrushToolDialog::OnBrushSelectGroupChk);
	connect(m_brush_use_absolute_chk, &QCheckBox::toggled, this, &BrushToolDialog::OnBrushUseAbsoluteValueChk);
	connect(m_brush_size1_sldr, &QSlider::valueChanged, this, &BrushToolDialog::OnBrushSize1Change);
	connect(m_brush_size1_text, &QLineEdit::textChanged, this, &BrushToolDialog::OnBrushSize1Text);
	connect(m_brush_size2_chk, &QCheckBox::toggled, this, &BrushToolDialog::OnBrushSize2Chk);
	connect(m_brush_size2_sldr, &QSlider::valueChanged, this, &BrushToolDialog::OnBrushSize2Change);
	connect(m_brush_size2_text, &QLineEdit::textChanged, this, &BrushToolDialog::OnBrushSize2Text);
	connect(m_brush_iterw_rb, &QRadioButton::clicked, this, &BrushToolDialog::OnBrushIterCheck);
	connect(m_brush_iters_rb, &QRadioButton::clicked, this, &BrushToolDialog::OnBrushIterCheck);
	connect(m_brush_iterss_rb, &QRadioButton::clicked, this, &BrushToolDialog::OnBrushIterCheck);
	connect(m_dslt_chk, &QCheckBox::toggled, this, &BrushToolDialog::OnDSLTBrushChk);
	connect(m_dslt_r_sldr, &QSlider::valueChanged, this, &BrushToolDialog::OnDSLTBrushRadChange);
	connect(m_dslt_r_text, &QLineEdit::textChanged, this, &BrushToolDialog::OnDSLTBrushRadText);
	connect(m_dslt_q_sldr, &QSlider::valueChanged, this, &BrushToolDialog::OnDSLTBrushQualityChange);
	connect(m_dslt_q_text, &QLineEdit::textChanged, this, &BrushToolDialog::OnDSLTBrushQualityText);
	connect(m_dslt_c_sldr, &QSlider::valueChanged, this, &BrushToolDialog::OnDSLTBrushCChange);
	connect(m_dslt_c_text, &QLineEdit::textChanged, this, &BrushToolDialog::OnDSLTBrushCText);
	connect(m_mask_overlay_alpha_sldr, &QSlider::valueChanged, this, &BrushToolDialog::OnMaskAlphaChange);
	connect(m_mask_overlay_alpha_text, &QLineEdit::textChanged, this, &BrushToolDialog::OnMaskAlphaText);

	return page;
}

QWidget* BrushToolDialog::CreateCalculationPage(QWidget* parent)
{
	auto* page = new QWidget(parent);

	//operand A
	auto* sizer1 = new QHBoxLayout();
	sizer1->addWidget(Label("Volume A:", page, 75, 20), 0, Qt::AlignVCenter);
	m_calc_load_a_btn = new QPushButton("Load", page);
	m_calc_load_a_btn->setFixedWidth(50);
	m_calc_a_text = new QLineEdit(page);
	m_calc_a_text->setMinimumWidth(200);
	sizer1->addWidget(m_calc_load_a_btn, 0, Qt::AlignVCenter);
	sizer1->addWidget(m_calc_a_text, 1);
	//operand B
	auto* sizer2 = new QHBoxLayout();
	sizer2->addWidget(Label("Volume B:", page, 75, 20), 0, Qt::AlignVCenter);
	m_calc_load_b_btn = new QPushButton("Load", page);
	m_calc_load_b_btn->setFixedWidth(50);
	m_calc_b_text = new QLineEdit(page);
	m_calc_b_text->setMinimumWidth(200);
	sizer2->addWidget(m_calc_load_b_btn, 0, Qt::AlignVCenter);
	sizer2->addWidget(m_calc_b_text, 1);
	//single operators
	auto* box3 = new QGroupBox("Single-valued Operators (Require A)", page);
	auto* sizer3 = new QVBoxLayout(box3);
	m_calc_fill_btn = new QPushButton("3D Fill Holes", page);
	sizer3->addWidget(m_calc_fill_btn);
	//two operators
	auto* box4 = new QGroupBox("Two-valued Operators (Require both A and B)", page);
	auto* sizer4 = new QHBoxLayout(box4);
	m_calc_sub_btn = new QPushButton("Subtract", page);
	m_calc_add_btn = new QPushButton("Max", page);
	m_calc_div_btn = new QPushButton("Divide", page);
	m_calc_isc_btn = new QPushButton("Colocalize", page);
	sizer4->addWidget(m_calc_sub_btn, 1);
	sizer4->addWidget(m_calc_add_btn, 1);
	sizer4->addWidget(m_calc_div_btn, 1);
	sizer4->addWidget(m_calc_isc_btn, 1);

	//vertical sizer
	auto* sizer_v = new QVBoxLayout(page);
	sizer_v->addSpacing(10);
	sizer_v->addLayout(sizer1);
	sizer_v->addSpacing(10);
	sizer_v->addLayout(sizer2);
	sizer_v->addSpacing(30);
	sizer_v->addWidget(box3);
	sizer_v->addSpacing(30);
	sizer_v->addWidget(box4);
	sizer_v->addSpacing(10);
	sizer_v->addStretch(1);

	connect(m_calc_load_a_btn, &QPushButton::clicked, this, &BrushToolDialog::OnLoadA);
	connect(m_calc_load_b_btn, &QPushButton::clicked, this, &BrushToolDialog::OnLoadB);
	connect(m_calc_sub_btn, &QPushButton::clicked, this, [this]() { Calc(1, true); });
	connect(m_calc_add_btn, &QPushButton::clicked, this, [this]() { Calc(2, true); });
	connect(m_calc_div_btn, &QPushButton::clicked, this, [this]() { Calc(3, true); });
	connect(m_calc_isc_btn, &QPushButton::clicked, this, [this]() { Calc(4, true); });
	connect(m_calc_fill_btn, &QPushButton::clicked, this, [this]() { Calc(9, false); });
	return page;
}

QWidget* BrushToolDialog::CreateAnalysisPage(QWidget* parent)
{
	auto* page = new QWidget(parent);
	//validator: floating point 1
	auto* vald_fp1 = new QDoubleValidator(0.0, 1e12, 1, page);
	vald_fp1->setNotation(QDoubleValidator::StandardNotation);
	vald_fp1->setLocale(QLocale::c());
	//validator: integer
	auto* vald_int = new QIntValidator(0, 2147483647, page);

	//component analyzer
	auto* box1 = new QGroupBox("Component Analyzer", page);
	auto* sizer1 = new QVBoxLayout(box1);
	//threshold of ccl
	auto* sizer1_1 = new QHBoxLayout();
	sizer1_1->addWidget(Label("Threshold:", page, 75, 20), 0, Qt::AlignVCenter);
	m_ca_thresh_sldr = Slider(0, 0, 2550, page);
	m_ca_thresh_text = Text("0.0", page, 50, vald_fp1);
	sizer1_1->addWidget(m_ca_thresh_sldr, 1);
	sizer1_1->addWidget(m_ca_thresh_text, 0, Qt::AlignVCenter);
	m_ca_analyze_btn = new QPushButton("Analyze", page);
	sizer1_1->addWidget(m_ca_analyze_btn, 0, Qt::AlignVCenter);
	//size of ccl
	auto* sizer1_2 = new QHBoxLayout();
	m_ca_select_only_chk = new QCheckBox("Selct. Only", page);
	m_ca_select_only_chk->setFixedWidth(90);
	sizer1_2->addSpacing(5);
	sizer1_2->addWidget(m_ca_select_only_chk, 0, Qt::AlignVCenter);
	sizer1_2->addStretch(1);
	sizer1_2->addWidget(Label("Min:", page, 40), 0, Qt::AlignVCenter);
	m_ca_min_text = Text("0", page, 40, vald_int);
	sizer1_2->addWidget(m_ca_min_text, 0, Qt::AlignVCenter);
	sizer1_2->addWidget(Label("vx", page, 15), 0, Qt::AlignVCenter);
	sizer1_2->addStretch(1);
	sizer1_2->addWidget(Label("Max:", page, 40), 0, Qt::AlignVCenter);
	m_ca_max_text = Text("1000", page, 40, vald_int);
	sizer1_2->addWidget(m_ca_max_text, 0, Qt::AlignVCenter);
	sizer1_2->addWidget(Label("vx", page, 15), 0, Qt::AlignVCenter);
	sizer1_2->addStretch(1);
	m_ca_ignore_max_chk = new QCheckBox("Ignore Max", page);
	sizer1_2->addWidget(m_ca_ignore_max_chk, 0, Qt::AlignVCenter);
	sizer1_2->addSpacing(5);
	//text result
	auto* sizer1_3 = new QHBoxLayout();
	sizer1_3->addSpacing(5);
	sizer1_3->addWidget(Label("Components:", page), 0, Qt::AlignVCenter);
	m_ca_comps_text = Text("0", page, 70, nullptr);
	m_ca_comps_text->setReadOnly(true);
	sizer1_3->addWidget(m_ca_comps_text, 0, Qt::AlignVCenter);
	sizer1_3->addStretch(1);
	sizer1_3->addWidget(Label("Total Volume:", page), 0, Qt::AlignVCenter);
	m_ca_volume_text = Text("0", page, 70, nullptr);
	m_ca_volume_text->setReadOnly(true);
	sizer1_3->addWidget(m_ca_volume_text, 0, Qt::AlignVCenter);
	//export
	auto* sizer1_4 = new QHBoxLayout();
	sizer1_4->addStretch(1);
	sizer1_4->addWidget(Label("Output:  ", page), 0, Qt::AlignVCenter);
	m_ca_multi_chann_btn = new QPushButton("Individual Channels", page);
	m_ca_random_color_btn = new QPushButton("Random RGB", page);
	m_ca_annotations_btn = new QPushButton("Annotations", page);
	sizer1_4->addWidget(m_ca_multi_chann_btn, 0, Qt::AlignVCenter);
	sizer1_4->addWidget(m_ca_random_color_btn, 0, Qt::AlignVCenter);
	sizer1_4->addWidget(m_ca_annotations_btn, 0, Qt::AlignVCenter);
	//sizer1
	sizer1->addSpacing(10);
	sizer1->addLayout(sizer1_1);
	sizer1->addSpacing(10);
	sizer1->addLayout(sizer1_2);
	sizer1->addSpacing(10);
	sizer1->addLayout(sizer1_3);
	sizer1->addSpacing(10);
	sizer1->addLayout(sizer1_4);
	sizer1->addSpacing(10);

	//noise removal
	auto* box2 = new QGroupBox("Noise Removal", page);
	auto* sizer2 = new QVBoxLayout(box2);
	//size threshold
	auto* sizer2_1 = new QHBoxLayout();
	sizer2_1->addSpacing(5);
	sizer2_1->addWidget(Label("Size Thresh.:", page, 75), 0, Qt::AlignVCenter);
	m_nr_size_sldr = Slider(10, 1, 100, page);
	m_nr_size_text = Text("10", page, 40, vald_int);
	sizer2_1->addWidget(m_nr_size_sldr, 1);
	sizer2_1->addWidget(m_nr_size_text, 0, Qt::AlignVCenter);
	sizer2_1->addWidget(Label("vx", page, 25), 0, Qt::AlignVCenter);
	//buttons
	auto* sizer2_2 = new QHBoxLayout();
	m_nr_analyze_btn = new QPushButton("Analyze", page);
	m_nr_remove_btn = new QPushButton("Remove", page);
	sizer2_2->addStretch(1);
	sizer2_2->addWidget(m_nr_analyze_btn, 0, Qt::AlignVCenter);
	sizer2_2->addWidget(m_nr_remove_btn, 0, Qt::AlignVCenter);
	//sizer2
	sizer2->addSpacing(10);
	sizer2->addLayout(sizer2_1);
	sizer2->addSpacing(10);
	sizer2->addLayout(sizer2_2);
	sizer2->addSpacing(10);

	//EVE
	auto* box3 = new QGroupBox("EVE (Partice Detection)", page);
	auto* sizer3 = new QVBoxLayout(box3);
	//min radius
	auto* sizer3_1 = new QHBoxLayout();
	sizer3_1->addSpacing(5);
	sizer3_1->addWidget(Label("Min Radius:", page, 75), 0, Qt::AlignVCenter);
	m_eve_min_radius_sldr = Slider(5, 1, 50, page);
	m_eve_min_radius_text = Text("5", page, 40, vald_int);
	sizer3_1->addWidget(m_eve_min_radius_sldr, 1);
	sizer3_1->addWidget(m_eve_min_radius_text, 0, Qt::AlignVCenter);
	sizer3_1->addWidget(Label("vx", page, 25), 0, Qt::AlignVCenter);
	//max radius
	auto* sizer3_2 = new QHBoxLayout();
	sizer3_2->addSpacing(5);
	sizer3_2->addWidget(Label("Max Radius:", page, 75), 0, Qt::AlignVCenter);
	m_eve_max_radius_sldr = Slider(5, 1, 50, page);
	m_eve_max_radius_text = Text("5", page, 40, vald_int);
	sizer3_2->addWidget(m_eve_max_radius_sldr, 1);
	sizer3_2->addWidget(m_eve_max_radius_text, 0, Qt::AlignVCenter);
	sizer3_2->addWidget(Label("vx", page, 25), 0, Qt::AlignVCenter);
	//threshold
	auto* sizer3_3 = new QHBoxLayout();
	sizer3_3->addSpacing(5);
	sizer3_3->addWidget(Label("Min Brightness:", page, 100), 0, Qt::AlignVCenter);
	m_eve_threshold_sldr = Slider(0, 0, 65535, page);
	m_eve_threshold_text = Text("0", page, 60, vald_int);
	sizer3_3->addWidget(m_eve_threshold_sldr, 1);
	sizer3_3->addWidget(m_eve_threshold_text, 0, Qt::AlignVCenter);
	//buttons
	auto* sizer3_4 = new QHBoxLayout();
	m_eve_analyze_btn = new QPushButton("Analyze", page);
	sizer3_4->addStretch(1);
	sizer3_4->addWidget(m_eve_analyze_btn, 0, Qt::AlignVCenter);
	sizer3->addSpacing(10);
	sizer3->addLayout(sizer3_1);
	sizer3->addSpacing(10);
	sizer3->addLayout(sizer3_2);
	sizer3->addSpacing(10);
	sizer3->addLayout(sizer3_3);
	sizer3->addSpacing(10);
	sizer3->addLayout(sizer3_4);
	sizer3->addSpacing(10);

	//vertical sizer
	auto* sizer_v = new QVBoxLayout(page);
	sizer_v->addSpacing(10);
	sizer_v->addWidget(box1);
	sizer_v->addSpacing(30);
	sizer_v->addWidget(box2);
	sizer_v->addSpacing(10);
	sizer_v->addWidget(box3);
	sizer_v->addSpacing(10);
	sizer_v->addStretch(1);

	connect(m_ca_thresh_sldr, &QSlider::valueChanged, this, &BrushToolDialog::OnCAThreshChange);
	connect(m_ca_thresh_text, &QLineEdit::textChanged, this, &BrushToolDialog::OnCAThreshText);
	connect(m_ca_ignore_max_chk, &QCheckBox::toggled, this, &BrushToolDialog::OnCAIgnoreMaxChk);
	connect(m_ca_analyze_btn, &QPushButton::clicked, this, &BrushToolDialog::OnCAAnalyzeBtn);
	connect(m_ca_multi_chann_btn, &QPushButton::clicked, this, &BrushToolDialog::OnCAMultiChannBtn);
	connect(m_ca_random_color_btn, &QPushButton::clicked, this, &BrushToolDialog::OnCARandomColorBtn);
	connect(m_ca_annotations_btn, &QPushButton::clicked, this, &BrushToolDialog::OnCAAnnotationsBtn);
	connect(m_nr_size_sldr, &QSlider::valueChanged, this, &BrushToolDialog::OnNRSizeChange);
	connect(m_nr_size_text, &QLineEdit::textChanged, this, &BrushToolDialog::OnNRSizeText);
	connect(m_nr_analyze_btn, &QPushButton::clicked, this, &BrushToolDialog::OnNRAnalyzeBtn);
	connect(m_nr_remove_btn, &QPushButton::clicked, this, &BrushToolDialog::OnNRRemoveBtn);
	connect(m_eve_min_radius_sldr, &QSlider::valueChanged, this, &BrushToolDialog::OnEVEMinRadiusChange);
	connect(m_eve_min_radius_text, &QLineEdit::textChanged, this, &BrushToolDialog::OnEVEMinRadiusText);
	connect(m_eve_max_radius_sldr, &QSlider::valueChanged, this, &BrushToolDialog::OnEVEMaxRadiusChange);
	connect(m_eve_max_radius_text, &QLineEdit::textChanged, this, &BrushToolDialog::OnEVEMaxRadiusText);
	connect(m_eve_threshold_sldr, &QSlider::valueChanged, this, &BrushToolDialog::OnEVEThresholdChange);
	connect(m_eve_threshold_text, &QLineEdit::textChanged, this, &BrushToolDialog::OnEVEThresholdText);
	connect(m_eve_analyze_btn, &QPushButton::clicked, this, &BrushToolDialog::OnEVEAnalyzeBtn);
	return page;
}

BrushToolDialog::BrushToolDialog(AnalysisContext* ctx, BrushTool* tool, QWidget* parent)
	: QDialog(parent),
	m_ctx(ctx),
	m_tool(tool)
{
	setWindowTitle("Analyze");
	setObjectName("BrushToolDlg");

	//notebook
	m_notebook = new QTabWidget(this);
	m_notebook->addTab(CreateBrushPage(m_notebook), "Paint Brush");
	m_notebook->addTab(CreateCalculationPage(m_notebook), "Calculations");
	m_notebook->addTab(CreateAnalysisPage(m_notebook), "Analysis");

	//all controls
	auto* sizerV = new QVBoxLayout(this);
	sizerV->addSpacing(10);
	sizerV->addWidget(m_notebook);
	sizerV->addSpacing(10);
	sizerV->addStretch(1);

	LoadDefault();

	if (m_tool)
	{
		connect(m_tool, &BrushTool::settingsChanged, this, &BrushToolDialog::GetSettings);
		connect(m_tool, &BrushTool::brushSelected, this, &BrushToolDialog::SelectBrush);
	}

	m_idleTimer = new QTimer(this);
	connect(m_idleTimer, &QTimer::timeout, this, &BrushToolDialog::OnIdle);
	m_idleTimer->start(100);

	resize(400, 550);
}

BrushToolDialog::~BrushToolDialog()
{
	if (m_idleTimer) m_idleTimer->stop();
	if (!VVDQtScriptedRun())   // scripted test runs keep the user's defaults
		SaveDefault();
}

void BrushToolDialog::showEvent(QShowEvent* e)
{
	QDialog::showEvent(e);
	GetSettings();
}

VolumeData* BrushToolDialog::CurSelVol() const
{
	return m_ctx ? m_ctx->CurrentVolume() : nullptr;
}

void BrushToolDialog::SetDSLTEnabled(bool enable)
{
	for (QWidget* w : std::initializer_list<QWidget*>{ st_dslt_r, m_dslt_r_sldr, m_dslt_r_text,
		st_dslt_q, m_dslt_q_sldr, m_dslt_q_text, st_dslt_c, m_dslt_c_sldr, m_dslt_c_text })
		w->setEnabled(enable);
}

void BrushToolDialog::SetBrushSclTranslate(double val)
{
	m_dft_scl_translate = val;
	SetSliderRange(m_brush_scl_translate_sldr, 0, int(m_max_value * 10.0));
	m_brush_scl_translate_text->setText(QString::asprintf("%.1f", m_dft_scl_translate * m_max_value));
}

void BrushToolDialog::SetCalcA(const QString& name) { m_calc_a_text->setText(name); }
void BrushToolDialog::SetCalcB(const QString& name) { m_calc_b_text->setText(name); }

// ---------------------------------------------------------------------------
// BrushToolDlg::GetSettings
// ---------------------------------------------------------------------------

void BrushToolDialog::GetSettings()
{
	BrushTool* vrv = Tool();
	if (!vrv)
		return;

	VolumeData* sel_vol = CurSelVol();
	double dval = 0.0;
	int ival = 0;
	bool bval = false;

	//selection strength
	dval = vrv->GetBrushSclTranslate();
	m_dft_scl_translate = dval;

	if (!sel_vol)
	{
		SetSliderRange(m_brush_scl_translate_sldr, 0, int(m_max_value * 10.0));
		SetSlider(m_brush_scl_translate_sldr, int(m_dft_scl_translate * m_max_value * 10.0 + 0.5));
		ChangeValue(m_brush_scl_translate_text, QString::asprintf("%.2f", m_dft_scl_translate * m_max_value));
	}
	//2d influence
	dval = vrv->GetW2d();
	SetSlider(m_brush_2dinfl_sldr, int(dval * 100.0 + 0.5));
	ChangeValue(m_brush_2dinfl_text, QString::asprintf("%.2f", dval));
	//edge detect
	bval = vrv->GetEdgeDetect();
	{ QSignalBlocker b(m_edge_detect_chk); m_edge_detect_chk->setChecked(bval); }
	//hidden removal
	bval = vrv->GetHiddenRemoval();
	{ QSignalBlocker b(m_hidden_removal_chk); m_hidden_removal_chk->setChecked(bval); }
	//select group
	bval = vrv->GetSelectGroup();
	{ QSignalBlocker b(m_select_group_chk); m_select_group_chk->setChecked(bval); }

	//size1
	dval = vrv->GetBrushSize1();
	SetSlider(m_brush_size1_sldr, int(dval));
	ChangeValue(m_brush_size1_text, QString::asprintf("%.0f", dval));
	//size2
	{ QSignalBlocker b(m_brush_size2_chk); m_brush_size2_chk->setChecked(vrv->GetBrushSize2Link()); }
	m_brush_size2_sldr->setEnabled(vrv->GetBrushSize2Link());
	m_brush_size2_text->setEnabled(vrv->GetBrushSize2Link());
	dval = vrv->GetBrushSize2();
	SetSlider(m_brush_size2_sldr, int(dval));
	ChangeValue(m_brush_size2_text, QString::asprintf("%.0f", dval));

	//iteration number
	ival = vrv->GetBrushIteration();
	if (ival <= BRUSH_TOOL_ITER_WEAK)
		m_brush_iterw_rb->setChecked(true);
	else if (ival <= BRUSH_TOOL_ITER_NORMAL)
		m_brush_iters_rb->setChecked(true);
	else
		m_brush_iterss_rb->setChecked(true);

	bval = vrv->GetUseDSLTBrush();
	{ QSignalBlocker b(m_dslt_chk); m_dslt_chk->setChecked(bval); }
	ival = vrv->GetBrushDSLT_R();
	SetSlider(m_dslt_r_sldr, ival);
	ChangeValue(m_dslt_r_text, QString::asprintf("%d", ival));
	ival = vrv->GetBrushDSLT_Q();
	SetSlider(m_dslt_q_sldr, ival);
	ChangeValue(m_dslt_q_text, QString::asprintf("%d", ival));
	dval = vrv->GetBrushDSLT_C();
	m_dft_dslt_c = dval;

	//threshold range
	if (sel_vol)
	{
		m_max_value = sel_vol->GetMaxValue();
		if (m_max_value <= 0.0)
			m_max_value = 255.0;
		//falloff
		SetSliderRange(m_brush_scl_translate_sldr, 0, int(m_max_value * 10.0));
		SetSlider(m_brush_scl_translate_sldr, int(m_dft_scl_translate * m_max_value * 10.0 + 0.5));
		ChangeValue(m_brush_scl_translate_text, QString::asprintf("%.1f", m_dft_scl_translate * m_max_value));
		SetSliderRange(m_ca_thresh_sldr, 0, int(m_max_value * 10.0));
		SetSlider(m_ca_thresh_sldr, int(m_dft_ca_thresh * m_max_value * 10.0 + 0.5));
		ChangeValue(m_ca_thresh_text, QString::asprintf("%.1f", m_dft_ca_thresh * m_max_value));

		SetSliderRange(m_dslt_c_sldr, 0, int(m_max_value * 10.0));
		SetSlider(m_dslt_c_sldr, int(m_dft_dslt_c * m_max_value * 10.0 + 0.5));
		ChangeValue(m_dslt_c_text, QString::asprintf("%.1f", m_dft_dslt_c * m_max_value));

		SetSliderRange(m_eve_threshold_sldr, 0, int(m_max_value));
		SetSlider(m_eve_threshold_sldr, int(m_dft_eve_thresh * m_max_value + 0.5));
		ChangeValue(m_eve_threshold_text, QString::asprintf("%d", int(m_dft_eve_thresh * m_max_value + 0.5)));

		int alpha = (int)(sel_vol->GetMaskAlpha() * 255.0 + 0.5);
		SetSlider(m_mask_overlay_alpha_sldr, alpha);
		ChangeValue(m_mask_overlay_alpha_text, QString::asprintf("%d", alpha));
		m_mask_overlay_alpha_sldr->setEnabled(true);
		m_mask_overlay_alpha_text->setEnabled(true);
	}
	else
	{
		m_mask_overlay_alpha_sldr->setEnabled(false);
		m_mask_overlay_alpha_text->setEnabled(false);
	}

	UpdateUndoRedo();
}

void BrushToolDialog::SelectBrush(int paint_mode)
{
	QSignalBlocker b1(m_append_act);
	QSignalBlocker b2(m_diffuse_act);
	QSignalBlocker b3(m_desel_act);
	m_append_act->setChecked(paint_mode == 2);
	m_diffuse_act->setChecked(paint_mode == 4);
	m_desel_act->setChecked(paint_mode == 3);
}

void BrushToolDialog::UpdateUndoRedo()
{
	VolumeData* vd = CurSelVol();
	if (vd && vd->GetTexture())
	{
		m_undo_act->setEnabled(vd->GetTexture()->get_undo());
		m_redo_act->setEnabled(vd->GetTexture()->get_redo());
	}
}

// ---------------------------------------------------------------------------
// brush commands (wx: through TreePanel::BrushAppend etc.)
// ---------------------------------------------------------------------------

void BrushToolDialog::OnBrushAppend(bool checked)
{
	{ QSignalBlocker b1(m_diffuse_act); QSignalBlocker b2(m_desel_act);
	  m_diffuse_act->setChecked(false); m_desel_act->setChecked(false); }
	if (m_tool) m_tool->SelectBrushMode(checked ? 2 : 0);
}

void BrushToolDialog::OnBrushDiffuse(bool checked)
{
	{ QSignalBlocker b1(m_append_act); QSignalBlocker b2(m_desel_act);
	  m_append_act->setChecked(false); m_desel_act->setChecked(false); }
	if (m_tool) m_tool->SelectBrushMode(checked ? 4 : 0);
}

void BrushToolDialog::OnBrushDesel(bool checked)
{
	{ QSignalBlocker b1(m_append_act); QSignalBlocker b2(m_diffuse_act);
	  m_append_act->setChecked(false); m_diffuse_act->setChecked(false); }
	if (m_tool) m_tool->SelectBrushMode(checked ? 3 : 0);
}

void BrushToolDialog::OnBrushClear()
{
	if (m_tool) m_tool->BrushClear();
}

void BrushToolDialog::OnBrushErase()
{
	SelectBrush(0);
	if (m_tool) m_tool->BrushErase();
}

void BrushToolDialog::OnBrushCreate()
{
	SelectBrush(0);
	if (m_tool) m_tool->BrushCreate();
}

void BrushToolDialog::BrushUndo()
{
	if (m_tool) m_tool->BrushUndo();   // emits settingsChanged -> GetSettings
	UpdateUndoRedo();
}

void BrushToolDialog::BrushRedo()
{
	if (m_tool) m_tool->BrushRedo();
	UpdateUndoRedo();
}

// wx BrushToolDlg::DrawBrush: preview the threshold on the current mask
void BrushToolDialog::DrawBrush(double val)
{
	BrushTool* tool = Tool();
	if (!tool || !m_ctx)
		return;
	RenderCanvas* canvas = tool->Canvas();
	VolumeData* sel_vol = canvas ? canvas->GetCurrentVolume() : nullptr;
	if (!sel_vol)
		return;
	bool persp = canvas->GetPerspective();
	if (!m_select_group_chk->isChecked())
	{
		double thval = val / sel_vol->GetMaxValue();
		if (thval < sel_vol->GetLeftThresh())
			thval = sel_vol->GetLeftThresh();
		if (sel_vol->GetMask(false))
		{
			sel_vol->DrawMaskThreshold((float)thval, persp, m_brush_use_absolute_chk->isChecked());
			sel_vol->GetMask(true);
		}
	}
	else
	{
		// wx: the volumes of the current volume's group
		// (m_cur_view->GetCurrentVolGroup())
		if (DataGroup* group = canvas->GetParentGroup(sel_vol))
		{
			for (int i = 0; i < group->GetVolumeNum(); i++)
			{
				VolumeData* vd = group->GetVolumeData(i);
				if (vd && vd->GetDisp())
				{
					double thval = val / vd->GetMaxValue();
					if (thval < vd->GetLeftThresh())
						thval = vd->GetLeftThresh();
					if (vd->GetMask(false))
					{
						vd->DrawMaskThreshold((float)thval, persp, m_brush_use_absolute_chk->isChecked());
						// wx reads back sel_vol here (typo): the other
						// volumes' CPU masks then stay stale for Extract/Erase
						vd->GetMask(true);
					}
				}
			}
		}
	}
	m_ctx->RefreshView();
}

// ---------------------------------------------------------------------------
// selection adjustment
// ---------------------------------------------------------------------------

void BrushToolDialog::OnBrushSclTranslateChange(int ival)
{
	double val = double(ival) / 10.0;
	m_brush_scl_translate_text->setText(QString::asprintf("%.1f", val));
	DrawBrush(val);
}

void BrushToolDialog::OnBrushSclTranslateTextEnter()
{
	double val = ToDouble(m_brush_scl_translate_text->text());
	m_dft_scl_translate = val / m_max_value;
	SetSlider(m_brush_scl_translate_sldr, int(val * 10.0 + 0.5));
	DrawBrush(val);
	if (BrushTool* t = Tool())
		t->SetBrushSclTranslate(m_dft_scl_translate);
}

void BrushToolDialog::OnBrushSclTranslateText(const QString& str)
{
	double val = ToDouble(str);
	m_dft_scl_translate = val / m_max_value;
	SetSlider(m_brush_scl_translate_sldr, int(val * 10.0 + 0.5));
	if (BrushTool* t = Tool())
		t->SetBrushSclTranslate(m_dft_scl_translate);
}

void BrushToolDialog::OnBrush2dinflChange(int ival)
{
	double val = double(ival) / 100.0;
	m_brush_2dinfl_text->setText(QString::asprintf("%.2f", val));
}

void BrushToolDialog::OnBrush2dinflText(const QString& str)
{
	double val = ToDouble(str);
	SetSlider(m_brush_2dinfl_sldr, int(val * 100.0));
	if (BrushTool* t = Tool())
		t->SetW2d(val);
}

void BrushToolDialog::OnBrushEdgeDetectChk(bool checked)
{
	if (BrushTool* t = Tool()) t->SetEdgeDetect(checked);
}

void BrushToolDialog::OnBrushHiddenRemovalChk(bool checked)
{
	if (BrushTool* t = Tool()) t->SetHiddenRemoval(checked);
}

void BrushToolDialog::OnBrushSelectGroupChk(bool checked)
{
	if (BrushTool* t = Tool()) t->SetSelectGroup(checked);
}

void BrushToolDialog::OnBrushUseAbsoluteValueChk(bool checked)
{
	if (BrushTool* t = Tool()) t->SetUseAbsoluteValue(checked);
}

// ---------------------------------------------------------------------------
// brush properties
// ---------------------------------------------------------------------------

void BrushToolDialog::OnBrushSize1Change(int ival)
{
	m_brush_size1_text->setText(QString::asprintf("%d", ival));
}

void BrushToolDialog::OnBrushSize1Text(const QString& str)
{
	double val = ToDouble(str);
	SetSlider(m_brush_size1_sldr, int(val));
	if (BrushTool* t = Tool())
	{
		t->SetBrushSize(val, -1.0);
		if (t->GetIntMode() == 2 && t->Canvas())
			t->Canvas()->RefreshView();
	}
}

void BrushToolDialog::OnBrushSize2Chk(bool checked)
{
	double val1 = ToDouble(m_brush_size1_text->text());
	double val2 = ToDouble(m_brush_size2_text->text());
	m_brush_size2_sldr->setEnabled(checked);
	m_brush_size2_text->setEnabled(checked);
	if (BrushTool* t = Tool())
	{
		t->SetUseBrushSize2(checked);
		t->SetBrushSize(val1, val2);
		if (t->Canvas()) t->Canvas()->RefreshView();
	}
}

void BrushToolDialog::OnBrushSize2Change(int ival)
{
	m_brush_size2_text->setText(QString::asprintf("%d", ival));
}

void BrushToolDialog::OnBrushSize2Text(const QString& str)
{
	double val = ToDouble(str);
	SetSlider(m_brush_size2_sldr, int(val));
	if (BrushTool* t = Tool())
	{
		t->SetBrushSize(-1.0, val);
		if (t->GetIntMode() == 2 && t->Canvas())
			t->Canvas()->RefreshView();
	}
}

void BrushToolDialog::OnBrushIterCheck()
{
	BrushTool* t = Tool();
	if (!t) return;
	if (m_brush_iterw_rb->isChecked())
		t->SetBrushIteration(BRUSH_TOOL_ITER_WEAK);
	else if (m_brush_iters_rb->isChecked())
		t->SetBrushIteration(BRUSH_TOOL_ITER_NORMAL);
	else if (m_brush_iterss_rb->isChecked())
		t->SetBrushIteration(BRUSH_TOOL_ITER_STRONG);
}

void BrushToolDialog::OnDSLTBrushChk(bool checked)
{
	if (BrushTool* t = Tool())
		t->SetUseDSLTBrush(checked);
	SetDSLTEnabled(checked);
}

void BrushToolDialog::OnDSLTBrushRadChange(int ival)
{
	m_dslt_r_text->setText(QString::asprintf("%d", ival));
}

void BrushToolDialog::OnDSLTBrushRadText(const QString& str)
{
	double val = ToDouble(str);
	SetSlider(m_dslt_r_sldr, int(val));
	if (BrushTool* t = Tool())
		t->SetBrushDSLT_R((int)val);
}

void BrushToolDialog::OnDSLTBrushQualityChange(int ival)
{
	m_dslt_q_text->setText(QString::asprintf("%d", ival));
}

void BrushToolDialog::OnDSLTBrushQualityText(const QString& str)
{
	double val = ToDouble(str);
	SetSlider(m_dslt_q_sldr, int(val));
	if (BrushTool* t = Tool())
		t->SetBrushDSLT_Q((int)val);
}

void BrushToolDialog::OnDSLTBrushCChange(int ival)
{
	double val = double(ival) / 10.0;
	m_dslt_c_text->setText(QString::asprintf("%.1f", val));
}

void BrushToolDialog::OnDSLTBrushCText(const QString& str)
{
	double val = ToDouble(str);
	m_dft_dslt_c = val / m_max_value;
	SetSlider(m_dslt_c_sldr, int(val * 10.0 + 0.5));
	if (BrushTool* t = Tool())
		t->SetBrushDSLT_C(m_dft_dslt_c);
}

void BrushToolDialog::OnMaskAlphaChange(int ival)
{
	m_mask_overlay_alpha_text->setText(QString::asprintf("%d", ival));
}

void BrushToolDialog::OnMaskAlphaText(const QString& str)
{
	double val = ToDouble(str);
	m_dft_mask_alpha = val;
	SetSlider(m_mask_overlay_alpha_sldr, int(val + 0.5));
	if (m_loading || !m_ctx)
		return;
	if (VolumeData* sel_vol = CurSelVol())
	{
		sel_vol->SetMaskAlpha(m_dft_mask_alpha / 255.0);
		m_ctx->RefreshView();
	}
}

// ---------------------------------------------------------------------------
// component analyzer
// ---------------------------------------------------------------------------

void BrushToolDialog::OnCAThreshChange(int ival)
{
	m_ca_thresh_text->setText(QString::asprintf("%.1f", double(ival) / 10.0));
}

void BrushToolDialog::OnCAThreshText(const QString& str)
{
	double val = ToDouble(str);
	m_dft_ca_thresh = val / m_max_value;
	SetSlider(m_ca_thresh_sldr, int(val * 10.0 + 0.5));
	if (m_loading || !m_ctx)
		return;
	VolumeCalculator* cal = m_ctx->GetCalculator();
	if (cal && cal->GetVolumeA())
	{
		cal->GetVolumeA()->SetMaskThreshold(m_dft_ca_thresh);
		m_ctx->RefreshView();
	}
}

void BrushToolDialog::OnCAAnalyzeBtn()
{
	if (!m_ctx)
		return;
	bool select = m_ca_select_only_chk->isChecked();
	double min_voxels = ToDouble(m_ca_min_text->text());
	double max_voxels = ToDouble(m_ca_max_text->text());
	bool ignore_max = m_ca_ignore_max_chk->isChecked();

	int comps = m_ctx->CompAnalysis(min_voxels, ignore_max ? -1.0 : max_voxels, m_dft_ca_thresh, select, true);
	VolumeSelector* sel = m_ctx->GetSelector();
	int volume = sel ? sel->GetVolumeNum() : 0;
	m_ca_comps_text->setText(QString::asprintf("%d", comps));
	m_ca_volume_text->setText(QString::asprintf("%d", volume));
	m_ctx->RefreshView();
}

void BrushToolDialog::OnCAIgnoreMaxChk(bool checked)
{
	m_ca_max_text->setEnabled(!checked);
}

void BrushToolDialog::OnCAMultiChannBtn()
{
	if (m_ctx)
		m_ctx->CompExport(0, m_ca_select_only_chk->isChecked());
}

void BrushToolDialog::OnCARandomColorBtn()
{
	if (m_ctx)
		m_ctx->CompExport(1, m_ca_select_only_chk->isChecked());
}

void BrushToolDialog::OnCAAnnotationsBtn()
{
	if (m_ctx)
		m_ctx->ShowAnnotations();
}

// ---------------------------------------------------------------------------
// noise removal
// ---------------------------------------------------------------------------

void BrushToolDialog::OnNRSizeChange(int ival)
{
	m_nr_size_text->setText(QString::asprintf("%d", ival));
}

void BrushToolDialog::OnNRSizeText(const QString& str)
{
	double val = ToDouble(str);
	SetSlider(m_nr_size_sldr, int(val));
}

// wx VRenderVulkanView::NoiseAnalysis(0, max_voxels, thresh)
void BrushToolDialog::OnNRAnalyzeBtn()
{
	if (!m_ctx)
		return;
	double max_voxels = ToDouble(m_nr_size_text->text());
	int comps = m_ctx->NoiseAnalysis(0.0, max_voxels, m_dft_ca_thresh);
	VolumeSelector* sel = m_ctx->GetSelector();
	int volume = sel ? sel->GetVolumeNum() : 0;
	m_ca_comps_text->setText(QString::asprintf("%d", comps));
	m_ca_volume_text->setText(QString::asprintf("%d", volume));
	m_ctx->RefreshView();
}

// wx VRenderVulkanView::NoiseRemoval(iter, thresh)
void BrushToolDialog::OnNRRemoveBtn()
{
	if (!m_ctx)
		return;
	double max_voxels = ToDouble(m_nr_size_text->text());
	m_ctx->NoiseRemoval(int(max_voxels), m_dft_ca_thresh);
}

// ---------------------------------------------------------------------------
// EVE
// ---------------------------------------------------------------------------

void BrushToolDialog::OnEVEMinRadiusChange(int ival)
{
	if (m_eve_min_radius_text) m_eve_min_radius_text->setText(QString::asprintf("%d", ival));
}

void BrushToolDialog::OnEVEMinRadiusText(const QString& str)
{
	double minval = ToDouble(str);
	m_dft_eve_min_radius = (int)minval;
	SetSlider(m_eve_min_radius_sldr, int(minval));
	double maxval = ToDouble(m_eve_max_radius_text->text());
	if (maxval < minval)
	{
		SetSlider(m_eve_max_radius_sldr, int(minval));
		ChangeValue(m_eve_max_radius_text, QString::asprintf("%d", int(minval)));
	}
}

void BrushToolDialog::OnEVEMaxRadiusChange(int ival)
{
	if (m_eve_max_radius_text) m_eve_max_radius_text->setText(QString::asprintf("%d", ival));
}

void BrushToolDialog::OnEVEMaxRadiusText(const QString& str)
{
	double maxval = ToDouble(str);
	m_dft_eve_max_radius = (int)maxval;
	SetSlider(m_eve_max_radius_sldr, int(maxval));
	double minval = ToDouble(m_eve_min_radius_text->text());
	if (minval > maxval)
	{
		SetSlider(m_eve_min_radius_sldr, int(maxval));
		ChangeValue(m_eve_min_radius_text, QString::asprintf("%d", int(maxval)));
	}
}

void BrushToolDialog::OnEVEThresholdChange(int ival)
{
	m_eve_threshold_text->setText(QString::asprintf("%d", ival));
}

void BrushToolDialog::OnEVEThresholdText(const QString& str)
{
	double val = ToDouble(str);
	m_dft_eve_thresh = val / m_max_value;
	SetSlider(m_eve_threshold_sldr, int(val + 0.5));
	if (m_loading || !m_ctx)
		return;
	VolumeCalculator* cal = m_ctx->GetCalculator();
	if (cal && cal->GetVolumeA())
	{
		cal->GetVolumeA()->SetMaskThreshold(m_dft_eve_thresh);
		m_ctx->RefreshView();
	}
}

// wx VRenderVulkanView::EVEAnalysis (no 2D mask: its `if (!select)` tests
// the address of ::select, which is never null)
void BrushToolDialog::OnEVEAnalyzeBtn()
{
	if (!m_ctx)
		return;
	VolumeSelector* sel = m_ctx->GetSelector();
	if (!sel || !sel->GetVolume())
		return;
	double min_radius = ToDouble(m_eve_min_radius_text->text());
	double max_radius = ToDouble(m_eve_max_radius_text->text());
	double threshold = ToDouble(m_eve_threshold_text->text());
	if (m_tool && m_tool->Canvas())
		m_tool->Canvas()->EnsureVulkan();

	sel->SetSizeMap(false);
	sel->EVEAnalysis((int)min_radius, (int)max_radius, threshold);
	if (sel->GetAnnotations())
		m_ctx->ShowAnnotations();   // adds them to the DataManager as well
	m_ctx->RefreshView();
}

// ---------------------------------------------------------------------------
// defaults (default_brush_settings.dft)
// ---------------------------------------------------------------------------

namespace
{
	VVDString DefaultFilePath(bool for_save)
	{
		const VVDString name(L"default_brush_settings.dft");
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

void BrushToolDialog::SaveDefault()
{
	vvd::IniFile fconfig;
	double val;
	//brush properties
	fconfig.Write(L"brush_ini_thresh", m_dft_ini_thresh);
	fconfig.Write(L"brush_gm_falloff", m_dft_gm_falloff);
	fconfig.Write(L"brush_scl_falloff", m_dft_scl_falloff);
	fconfig.Write(L"brush_scl_translate", m_dft_scl_translate);
	//edge detect
	fconfig.Write(L"edge_detect", m_edge_detect_chk->isChecked());
	//hidden removal
	fconfig.Write(L"hidden_removal", m_hidden_removal_chk->isChecked());
	//select group
	fconfig.Write(L"select_group", m_select_group_chk->isChecked());
	//2d influence
	val = ToDouble(m_brush_2dinfl_text->text());
	fconfig.Write(L"brush_2dinfl", val);
	//size 1
	val = ToDouble(m_brush_size1_text->text());
	fconfig.Write(L"brush_size1", val);
	//size2 link
	fconfig.Write(L"use_brush_size2", m_brush_size2_chk->isChecked());
	//size 2
	val = ToDouble(m_brush_size2_text->text());
	fconfig.Write(L"brush_size2", val);
	//iterations
	int ival = m_brush_iterw_rb->isChecked() ? 1 :
		m_brush_iters_rb->isChecked() ? 2 :
		m_brush_iterss_rb->isChecked() ? 3 : 0;
	fconfig.Write(L"brush_iters", ival);

	fconfig.Write(L"mask_alpha", (int)m_dft_mask_alpha);

	fconfig.Write(L"use_dslt", m_dslt_chk->isChecked());
	val = ToDouble(m_dslt_r_text->text());
	fconfig.Write(L"dslt_r", val);
	val = ToDouble(m_dslt_q_text->text());
	fconfig.Write(L"dslt_q", val);
	fconfig.Write(L"dslt_c", m_dft_dslt_c);

	//component analyzer
	//selected only
	fconfig.Write(L"ca_select_only", m_ca_select_only_chk->isChecked());
	//min voxel
	val = ToDouble(m_ca_min_text->text());
	fconfig.Write(L"ca_min", val);
	//max voxel
	val = ToDouble(m_ca_max_text->text());
	fconfig.Write(L"ca_max", val);
	//ignore max
	fconfig.Write(L"ca_ignore_max", m_ca_ignore_max_chk->isChecked());
	//thresh
	fconfig.Write(L"ca_thresh", m_dft_ca_thresh);
	//noise removal
	fconfig.Write(L"nr_thresh", m_dft_nr_thresh);
	fconfig.Write(L"nr_size", m_dft_nr_size);
	//EVE
	fconfig.Write(L"eve_thresh", m_dft_eve_thresh);
	fconfig.Write(L"eve_min_radius", m_dft_eve_min_radius);
	fconfig.Write(L"eve_max_radius", m_dft_eve_max_radius);

	fconfig.Save(DefaultFilePath(true));
}

void BrushToolDialog::LoadDefault()
{
	const VVDString dft = DefaultFilePath(false);
	vvd::IniFile fconfig;
	if (!vvd::FileExists(dft) || !fconfig.Load(dft))
		return;

	struct LoadingGuard { bool& f; LoadingGuard(bool& b) : f(b) { f = true; } ~LoadingGuard() { f = false; } } guard(m_loading);

	QString str;
	double val;
	int ival;
	bool bval;

	//brush properties
	fconfig.Read(L"brush_ini_thresh", &m_dft_ini_thresh);
	fconfig.Read(L"brush_gm_falloff", &m_dft_gm_falloff);
	fconfig.Read(L"brush_scl_falloff", &m_dft_scl_falloff);
	if (fconfig.Read(L"brush_scl_translate", &m_dft_scl_translate))
	{
		str = QString::asprintf("%.1f", m_dft_scl_translate * m_max_value);
		SetSliderRange(m_brush_scl_translate_sldr, 0, int(m_max_value * 10.0));
		m_brush_scl_translate_text->setText(str);
	}
	//edge detect
	if (fconfig.Read(L"edge_detect", &bval))
		m_edge_detect_chk->setChecked(bval);
	//hidden removal
	if (fconfig.Read(L"hidden_removal", &bval))
		m_hidden_removal_chk->setChecked(bval);
	//select group
	if (fconfig.Read(L"select_group", &bval))
		m_select_group_chk->setChecked(bval);
	//2d influence
	if (fconfig.Read(L"brush_2dinfl", &val))
		m_brush_2dinfl_text->setText(QString::asprintf("%.2f", val));
	//size 1
	if (fconfig.Read(L"brush_size1", &val) && val > 0.0)
		m_brush_size1_text->setText(QString::asprintf("%d", (int)val));
	//size 2 link
	if (fconfig.Read(L"use_brush_size2", &bval))
	{
		{ QSignalBlocker b(m_brush_size2_chk); m_brush_size2_chk->setChecked(bval); }
		m_brush_size2_sldr->setEnabled(bval);
		m_brush_size2_text->setEnabled(bval);
	}
	//size 2
	if (fconfig.Read(L"brush_size2", &val) && val > 0.0)
		m_brush_size2_text->setText(QString::asprintf("%d", (int)val));
	//iterations
	if (fconfig.Read(L"brush_iters", &ival))
	{
		switch (ival)
		{
		case 1:
			m_brush_iterw_rb->setChecked(true);
			break;
		case 2:
			m_brush_iters_rb->setChecked(true);
			break;
		case 3:
			m_brush_iterss_rb->setChecked(true);
			break;
		}
	}
	if (fconfig.Read(L"mask_alpha", &val))
	{
		m_dft_mask_alpha = val;
		m_mask_overlay_alpha_text->setText(QString::asprintf("%d", (int)val));
	}

	if (fconfig.Read(L"use_dslt", &bval))
	{
		{ QSignalBlocker b(m_dslt_chk); m_dslt_chk->setChecked(bval); }
		SetDSLTEnabled(bval);
	}
	if (fconfig.Read(L"dslt_r", &val) && val > 0.0)
		m_dslt_r_text->setText(QString::asprintf("%d", (int)val));
	if (fconfig.Read(L"dslt_q", &val) && val > 0.0)
		m_dslt_q_text->setText(QString::asprintf("%d", (int)val));
	if (fconfig.Read(L"dslt_c", &m_dft_dslt_c))
	{
		double c = m_dft_dslt_c;
		SetSliderRange(m_dslt_c_sldr, 0, int(m_max_value * 10.0));
		m_dslt_c_text->setText(QString::asprintf("%.1f", c * m_max_value));
	}

	//component analyzer
	//selected only
	if (fconfig.Read(L"ca_select_only", &bval))
		m_ca_select_only_chk->setChecked(bval);
	//min voxel
	if (fconfig.Read(L"ca_min", &val))
	{
		m_dft_ca_min = (int)val;
		m_ca_min_text->setText(QString::asprintf("%d", (int)val));
	}
	//max voxel
	if (fconfig.Read(L"ca_max", &val))
	{
		m_dft_ca_max = (int)val;
		m_ca_max_text->setText(QString::asprintf("%d", (int)val));
	}
	//ignore max
	if (fconfig.Read(L"ca_ignore_max", &bval))
	{
		{ QSignalBlocker b(m_ca_ignore_max_chk); m_ca_ignore_max_chk->setChecked(bval); }
		m_ca_max_text->setEnabled(!bval);
	}
	//thresh
	if (fconfig.Read(L"ca_thresh", &m_dft_ca_thresh))
	{
		double th = m_dft_ca_thresh;
		SetSliderRange(m_ca_thresh_sldr, 0, int(m_max_value * 10.0));
		m_ca_thresh_text->setText(QString::asprintf("%.1f", th * m_max_value));
	}
	//nr thresh
	fconfig.Read(L"nr_thresh", &m_dft_nr_thresh);
	//nr size
	if (fconfig.Read(L"nr_size", &m_dft_nr_size))
		m_nr_size_text->setText(QString::asprintf("%d", (int)m_dft_nr_size));

	//eve thresh
	fconfig.Read(L"eve_thresh", &m_dft_eve_thresh);
	//eve radius
	int min_r = m_dft_eve_min_radius, max_r = m_dft_eve_max_radius;
	bool has_min = fconfig.Read(L"eve_min_radius", &min_r);
	bool has_max = fconfig.Read(L"eve_max_radius", &max_r);
	if (has_min)
		m_eve_min_radius_text->setText(QString::asprintf("%d", min_r));
	if (has_max)
		m_eve_max_radius_text->setText(QString::asprintf("%d", max_r));
	if (has_min) m_dft_eve_min_radius = min_r;
	if (has_max) m_dft_eve_max_radius = max_r;
}

// ---------------------------------------------------------------------------
// calculations
// ---------------------------------------------------------------------------

void BrushToolDialog::OnLoadA()
{
	m_vol1 = CurSelVol();
	if (m_vol1)
		m_calc_a_text->setText(VolName(m_vol1));
}

void BrushToolDialog::OnLoadB()
{
	m_vol2 = CurSelVol();
	if (m_vol2)
		m_calc_b_text->setText(VolName(m_vol2));
}

void BrushToolDialog::LoadVolumes()
{
	DataManager* mgr = m_ctx ? m_ctx->GetDataManager() : nullptr;
	if (!mgr) return;
	m_vol1 = mgr->GetVolumeData(VVDString(m_calc_a_text->text().toStdWString()));
	m_vol2 = mgr->GetVolumeData(VVDString(m_calc_b_text->text().toStdWString()));
}

// wx OnCalcSub / OnCalcAdd / OnCalcDiv / OnCalcIsc (need_b) and OnCalcFill
void BrushToolDialog::Calc(int type, bool need_b)
{
	LoadVolumes();
	if (!m_vol1 || (need_b && !m_vol2) || !m_ctx)
		return;
	VolumeCalculator* cal = m_ctx->GetCalculator();
	if (!cal)
		return;
	// wx SetVolumeA: calculator + selector
	cal->SetVolumeA(m_vol1);
	if (VolumeSelector* sel = m_ctx->GetSelector())
		sel->SetVolume(m_vol1);
	if (need_b)
		cal->SetVolumeB(m_vol2);
	else
	{
		m_vol2 = nullptr;
		cal->SetVolumeB(nullptr);
		m_calc_b_text->clear();
	}
	m_ctx->Calculate(type);
	m_vol1 = cal->GetVolumeA();
	m_vol2 = cal->GetVolumeB();
	m_calc_a_text->setText(VolName(m_vol1));
	m_calc_b_text->setText(VolName(m_vol2));
}

// ---------------------------------------------------------------------------
// wx BrushToolDlg::OnIdle: while the pointer is over the component-analyzer
// or EVE threshold slider, highlight the voxels above the threshold.
// ---------------------------------------------------------------------------

void BrushToolDialog::OnIdle()
{
	if (!isVisible() || !m_ctx)
		return;
	// wx: if (!m_cur_view || m_cur_view->m_capture) return;
	if (!m_tool || !m_tool->Canvas() || !m_tool->Canvas()->IsVulkanReady() || m_tool->Canvas()->IsCapturing())
		return;
	VolumeCalculator* cal = m_ctx->GetCalculator();
	VolumeData* vd = cal ? cal->GetVolumeA() : nullptr;
	if (!vd)
		return;

	QPoint pos = QCursor::pos();
	QRect ca_reg(m_ca_thresh_sldr->mapToGlobal(QPoint(0, 0)), m_ca_thresh_sldr->size());
	QRect eve_reg(m_eve_threshold_sldr->mapToGlobal(QPoint(0, 0)), m_eve_threshold_sldr->size());

	bool cur_mode = vd->GetHighlightingMode();

	if (m_notebook && m_notebook->currentIndex() == 2 &&
		(ca_reg.contains(pos) || eve_reg.contains(pos)))
	{
		if (!cur_mode)
		{
			if (FLIVR::Texture::mask_undo_num_ == 0)
			{
				FLIVR::Texture::mask_undo_num_ = 1;
				m_mask_undo_num_modified = true;
			}
			vd->AddEmptyMask();
			if (vd->GetVR())
				vd->GetVR()->return_mask();
			if (vd->GetTexture())
				vd->GetTexture()->push_mask();
			auto blank = std::shared_ptr<vks::VTexture>();
			vd->Set2DWeight(blank, blank);
			vd->DrawMask(0, 5, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false, NULL, true, false, false);
			if (vd->GetVR())
			{
				vd->GetVR()->return_mask();
				vd->GetVR()->clear_tex_current_mask();
			}
			vd->SetUseMaskThreshold(true);
			if (ca_reg.contains(pos))
				vd->SetMaskThreshold(m_dft_ca_thresh);
			else
				vd->SetMaskThreshold(m_dft_eve_thresh);
			vd->SetHighlightingMode(true);
			m_ctx->RefreshView();
		}
	}
	else
	{
		if (cur_mode)
		{
			if (vd->GetTexture())
			{
				vd->GetTexture()->mask_undos_backward();
				if (m_mask_undo_num_modified)
				{
					m_mask_undo_num_modified = false;
					FLIVR::Texture::mask_undo_num_ = 0;
					vd->GetTexture()->trim_mask_undos_tail();
				}
			}
			if (vd->GetVR())
				vd->GetVR()->clear_tex_current_mask();
			vd->SetUseMaskThreshold(false);
			vd->SetHighlightingMode(false);
			m_ctx->RefreshView();
		}
	}
}

void BrushToolDialog::SetTool(BrushTool* tool)
{
	if (tool == m_tool) return;
	if (m_tool) disconnect(m_tool, nullptr, this, nullptr);
	m_tool = tool;
	if (m_tool)
	{
		connect(m_tool, &BrushTool::settingsChanged, this, &BrushToolDialog::GetSettings);
		connect(m_tool, &BrushTool::brushSelected, this, &BrushToolDialog::SelectBrush);
	}
	GetSettings();
}
