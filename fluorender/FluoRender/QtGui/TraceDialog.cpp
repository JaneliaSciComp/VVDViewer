// "Components & Tracking": Qt port of the wx TraceDlg / TraceListCtrl
// (TraceDlg.cpp). See TraceDialog.h.

#include "TraceDialog.h"

#include <QTabWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QIntValidator>
#include <QSignalBlocker>
#include <QPixmap>
#include <QScrollBar>
#include <QElapsedTimer>
#include <algorithm>
#include <set>
#include <cmath>
#include <chrono>

#include "RenderCanvas.h"
#include "RulerTool.h"
#include "TestRun.h"
#include "FLIVR/Color.h"
#include "FLIVR/Texture.h"
#include "FLIVR/VolumeRenderer.h"
#include "Formats/lbl_reader.h"
#include "Formats/msk_writer.h"
#include "Core/FileUtil.h"
#include "img/icons.h"

using namespace FLIVR;

namespace
{
	QIcon IconFromPng(const unsigned char* data, int len)
	{
		QPixmap pm;
		pm.loadFromData(data, (uint)len, "PNG");
		return QIcon(pm);
	}
#define VVD_ICON(name) IconFromPng(icons::name##_png, (int)sizeof(icons::name##_png))

	QPushButton* Btn(const QString& t, int w, QWidget* p)
	{
		auto* b = new QPushButton(t, p);
		b->setAutoDefault(false);
		b->setFixedWidth(w);
		return b;
	}
	QLabel* Label(const QString& t, int w, QWidget* p)
	{
		auto* l = new QLabel(t, p);
		if (w > 0) l->setFixedWidth(w);
		return l;
	}
	QColor IdColor(unsigned int id)
	{
		// wx: Color c = HSVColor(id % 360, 1.0, 1.0); wxColor(c.r()*255, ...)
		Color c(HSVColor(id % 360, 1.0, 1.0));
		return QColor(int(c.r() * 255), int(c.g() * 255), int(c.b() * 255));
	}
	// wx data_name.Left(data_name.find_last_of('.')) + ".lbl"
	std::wstring LabelName(const std::wstring& data_name)
	{
		const size_t dot = data_name.find_last_of(L'.');
		return (dot == std::wstring::npos ? data_name : data_name.substr(0, dot)) + L".lbl";
	}
	bool ToULong(const QString& s, unsigned long& v)
	{
		bool ok = false;
		const qulonglong u = s.trimmed().toULongLong(&ok);
		if (ok) v = (unsigned long)u;
		return ok && !s.trimmed().isEmpty();
	}
	bool ToLong(const QString& s, long& v)
	{
		bool ok = false;
		const qlonglong l = s.trimmed().toLongLong(&ok);
		if (ok) v = (long)l;
		return ok;
	}
}

// ---------------------------------------------------------------------------
// TraceListWidget (wx TraceListCtrl)
// ---------------------------------------------------------------------------

TraceListWidget::TraceListWidget(TraceDialog* dlg, int type, QWidget* parent)
	: QTreeWidget(parent), m_dlg(dlg), m_type(type)
{
	setColumnCount(6);
	setHeaderLabels({ QString(), "ID", "Size", "X", "Y", "Z" });
	setRootIsDecorated(false);
	setSelectionMode(QAbstractItemView::ExtendedSelection);
	setUniformRowHeights(true);
	header()->resizeSection(0, 20);
	for (int c = 1; c < 6; c++) header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
	setObjectName(type == 0 ? "TraceListCurr" : "TraceListPrev");
}

void TraceListWidget::Append(const QString& gtype, unsigned int id, const QColor& color,
	int size, double cx, double cy, double cz)
{
	auto* item = new QTreeWidgetItem(this);
	item->setText(0, gtype);
	item->setText(1, QString::asprintf("%u", id));
	item->setText(2, QString::asprintf("%d", size));
	item->setText(3, QString::asprintf("%f", cx));
	item->setText(4, QString::asprintf("%f", cy));
	item->setText(5, QString::asprintf("%f", cz));
	for (int c = 0; c < 6; c++) item->setBackground(c, color);
}

void TraceListWidget::UpdateTraces(RenderCanvas* canvas)
{
	if (canvas)
		m_canvas = canvas;
	TraceGroup* traces = m_canvas ? m_canvas->GetTraceGroup() : nullptr;
	if (!traces)
		return;
	DeleteAllItems();
	FL::CellList sel_cells = traces->GetCellList();
	std::vector<FL::pCell> cells;
	for (auto siter = sel_cells.begin(); siter != sel_cells.end(); ++siter)
		cells.push_back(siter->second);
	if (cells.empty())
		return;
	std::sort(cells.begin(), cells.end(), [](const FL::pCell c1, const FL::pCell c2) {
		unsigned int vid1 = c1->GetVertexId();
		unsigned int vid2 = c2->GetVertexId();
		if (vid1 == vid2)
			return c1->GetSizeUi() > c2->GetSizeUi();
		return vid1 < vid2;
	});
	for (size_t i = 0; i < cells.size(); ++i)
	{
		const unsigned int id = cells[i]->Id();
		const unsigned int vid = cells[i]->GetVertexId();
		const int size = (int)(cells[i]->GetSizeUi());
		const Point center = cells[i]->GetCenter();
		QString gtype;
		if (vid == 0)
			gtype = QString(QChar(0x25ef));
		else
		{
			const bool prev = i > 0 && cells[i - 1]->GetVertexId() == vid;
			const bool next = i + 1 < cells.size() && cells[i + 1]->GetVertexId() == vid;
			if (prev)
				gtype = QString(QChar(next ? 0x2502 : 0x2514));
			else
				gtype = QString(QChar(next ? 0x250c : 0x2500));
		}
		Append(gtype, id, IdColor(id), size, center.x(), center.y(), center.z());
	}
}

void TraceListWidget::DeleteSelection()
{
	if (m_type == 0)
	{
		if (m_dlg)
		{
			m_dlg->CompDelete();
			if (m_dlg->GetManualAssist())
				m_dlg->CellLink(true);
		}
	}
	else if (m_type == 1)
	{
		const QList<QTreeWidgetItem*> sel = selectedItems();
		for (QTreeWidgetItem* it : sel) delete it;
	}
}

QString TraceListWidget::GetText(int item, int col) const
{
	QTreeWidgetItem* it = topLevelItem(item);
	return it ? it->text(col) : QString();
}

bool TraceListWidget::IsItemSelected(int item) const
{
	QTreeWidgetItem* it = topLevelItem(item);
	return it && it->isSelected();
}

void TraceListWidget::CopySelection()
{
	for (int i = 0; i < topLevelItemCount(); i++)
		if (topLevelItem(i)->isSelected())
		{
			QApplication::clipboard()->setText(topLevelItem(i)->text(1));
			return;
		}
}

void TraceListWidget::keyPressEvent(QKeyEvent* e)
{
	if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)
		DeleteSelection();
	if (e->key() == Qt::Key_C && (e->modifiers() & Qt::ControlModifier))
		CopySelection();
	QTreeWidget::keyPressEvent(e);
}

void TraceListWidget::contextMenuEvent(QContextMenuEvent* e)
{
	if (selectedItems().isEmpty())
		return;
	QMenu menu(this);
	menu.addAction(tr("Copy"), this, &TraceListWidget::CopySelection);
	menu.addAction(tr("Delete"), this, &TraceListWidget::DeleteSelection);
	menu.exec(e->globalPos());
}

// ---------------------------------------------------------------------------
// pages (wx CreateMapPage ... CreateAnalysisPage)
// ---------------------------------------------------------------------------

QWidget* TraceDialog::CreateMapPage(QWidget* parent)
{
	auto* page = new QWidget(parent);
	//load trace
	auto* sizer_1 = new QHBoxLayout();
	m_load_trace_text = new QLineEdit(page);
	m_load_trace_text->setReadOnly(true);
	m_load_trace_btn = Btn(tr("Load"), 65, page);
	m_save_trace_btn = Btn(tr("Save"), 65, page);
	m_saveas_trace_btn = Btn(tr("Save As"), 65, page);
	sizer_1->addSpacing(5);
	sizer_1->addWidget(Label(tr("Track map:"), 70, page));
	sizer_1->addWidget(m_load_trace_text, 1);
	sizer_1->addWidget(m_load_trace_btn);
	sizer_1->addWidget(m_save_trace_btn);
	sizer_1->addWidget(m_saveas_trace_btn);
	//generate
	auto* sizer_2 = new QHBoxLayout();
	m_gen_map_prg = new QProgressBar(page);
	m_gen_map_prg->setRange(0, 100);
	m_gen_map_prg->setValue(0);
	m_gen_map_prg->setTextVisible(false);
	m_gen_map_prg->setFixedHeight(18);
	m_gen_map_spin = new QSpinBox(page);
	m_gen_map_spin->setRange(0, 100);   // wxSpinCtrl defaults
	m_gen_map_spin->setValue(1);
	m_gen_map_spin->setFixedWidth(50);
	m_gen_map_btn = Btn(tr("Generate"), 75, page);
	m_refine_t_btn = Btn(tr("Refine T"), 75, page);
	m_refine_all_btn = Btn(tr("Refine All"), 75, page);
	sizer_2->addSpacing(5);
	sizer_2->addWidget(Label(tr("New map:"), 70, page));
	sizer_2->addWidget(m_gen_map_prg, 1);
	sizer_2->addSpacing(10);
	sizer_2->addWidget(Label(tr("X"), 10, page));
	sizer_2->addWidget(m_gen_map_spin);
	sizer_2->addSpacing(10);
	sizer_2->addWidget(m_gen_map_btn);
	sizer_2->addWidget(m_refine_t_btn);
	sizer_2->addWidget(m_refine_all_btn);
	auto* sizer_v = new QVBoxLayout(page);
	sizer_v->addSpacing(10);
	sizer_v->addLayout(sizer_1);
	sizer_v->addSpacing(10);
	sizer_v->addLayout(sizer_2);
	sizer_v->addSpacing(10);
	connect(m_load_trace_btn, &QPushButton::clicked, this, [this]() { OnLoadTrace(); });
	connect(m_save_trace_btn, &QPushButton::clicked, this, &TraceDialog::OnSaveTrace);
	connect(m_saveas_trace_btn, &QPushButton::clicked, this, [this]() { OnSaveasTrace(); });
	connect(m_gen_map_btn, &QPushButton::clicked, this, &TraceDialog::OnGenMapBtn);
	connect(m_refine_t_btn, &QPushButton::clicked, this, &TraceDialog::OnRefineTBtn);
	connect(m_refine_all_btn, &QPushButton::clicked, this, &TraceDialog::OnRefineAllBtn);
	return page;
}

QWidget* TraceDialog::CreateSelectPage(QWidget* parent)
{
	auto* page = new QWidget(parent);
	//selection tools
	auto* sizer_1 = new QHBoxLayout();
	m_comp_id_text = new QLineEdit(page);
	m_comp_id_text->setFixedWidth(77);
	auto* comp_id_x_btn = Btn(tr("X"), 23, page);
	auto* comp_full_btn = Btn(tr("FullCompt"), 80, page);
	auto* comp_exclusive_btn = Btn(tr("Replace"), 65, page);
	auto* comp_append_btn = Btn(tr("Append"), 65, page);
	auto* comp_clear_btn = Btn(tr("Clear"), 65, page);
	sizer_1->addSpacing(5);
	sizer_1->addWidget(Label(tr("Selection tools:"), 100, page));
	sizer_1->addWidget(m_comp_id_text);
	sizer_1->addWidget(comp_id_x_btn);
	sizer_1->addSpacing(10);
	sizer_1->addWidget(comp_full_btn);
	sizer_1->addWidget(comp_exclusive_btn);
	sizer_1->addWidget(comp_append_btn);
	sizer_1->addWidget(comp_clear_btn);
	sizer_1->addStretch(1);
	//cell size filter
	auto* sizer_2 = new QHBoxLayout();
	m_cell_size_sldr = new QSlider(Qt::Horizontal, page);
	m_cell_size_sldr->setRange(0, 100);
	m_cell_size_sldr->setValue(20);
	m_cell_size_text = new QLineEdit("20", page);
	m_cell_size_text->setFixedWidth(60);
	m_cell_size_text->setValidator(new QIntValidator(0, 2147483647, m_cell_size_text));
	sizer_2->addSpacing(5);
	sizer_2->addWidget(Label(tr("Component size:"), 130, page));
	sizer_2->addWidget(m_cell_size_sldr, 1);
	sizer_2->addWidget(m_cell_size_text);
	auto* sizer_v = new QVBoxLayout(page);
	sizer_v->addSpacing(10);
	sizer_v->addLayout(sizer_1);
	sizer_v->addSpacing(10);
	sizer_v->addLayout(sizer_2);
	sizer_v->addSpacing(10);
	// wx: EVT_TEXT(ID_CompIDText) syncs the Linkage page's text, Enter = OnCompFull
	connect(m_comp_id_text, &QLineEdit::textEdited, this, [this]() { OnCompIDText(false); });
	connect(m_comp_id_text, &QLineEdit::returnPressed, this, &TraceDialog::OnCompFull);
	connect(comp_id_x_btn, &QPushButton::clicked, this, [this]() { m_comp_id_text->clear(); m_comp_id_text2->clear(); });
	connect(comp_full_btn, &QPushButton::clicked, this, &TraceDialog::OnCompFull);
	connect(comp_exclusive_btn, &QPushButton::clicked, this, &TraceDialog::OnCompExclusive);
	connect(comp_append_btn, &QPushButton::clicked, this, [this]() { OnCompAppend(false); });
	connect(comp_clear_btn, &QPushButton::clicked, this, &TraceDialog::OnCompClear);
	// wx OnCellSizeChange / OnCellSizeText
	connect(m_cell_size_sldr, &QSlider::valueChanged, this, [this](int v) { m_cell_size_text->setText(QString::number(v)); OnCellSizeText(); });
	connect(m_cell_size_text, &QLineEdit::textEdited, this, &TraceDialog::OnCellSizeText);
	return page;
}

QWidget* TraceDialog::CreateLinkPage(QWidget* parent)
{
	auto* page = new QWidget(parent);
	auto* sizer_1 = new QHBoxLayout();
	m_comp_id_text2 = new QLineEdit(page);
	m_comp_id_text2->setFixedWidth(77);
	auto* comp_id_x_btn = Btn(tr("X"), 23, page);
	auto* comp_append_btn = Btn(tr("Append"), 65, page);
	auto* comp_clear_btn = Btn(tr("Clear"), 65, page);
	m_manual_assist_check = new QToolButton(page);
	m_manual_assist_check->setObjectName("AutoLinkCheck");
	m_manual_assist_check->setCheckable(true);
	m_manual_assist_check->setAutoRaise(true);
	m_manual_assist_check->setIconSize(QSize(92, 23));
	m_manual_assist_check->setToolTip(tr("Automatically link selected IDs after each paint brush stroke"));
	sizer_1->addSpacing(5);
	sizer_1->addWidget(Label(tr("Selection tools:"), 100, page));
	sizer_1->addWidget(m_comp_id_text2);
	sizer_1->addWidget(comp_id_x_btn);
	sizer_1->addSpacing(10);
	sizer_1->addWidget(comp_append_btn);
	sizer_1->addWidget(comp_clear_btn);
	sizer_1->addStretch(1);
	sizer_1->addWidget(m_manual_assist_check);
	//ID link controls
	auto* sizer_2 = new QHBoxLayout();
	auto* excl = Btn(tr("Excl. Link"), 75, page);
	auto* link = Btn(tr("Link IDs"), 75, page);
	auto* isolate = Btn(tr("Isolate"), 75, page);
	auto* unlink = Btn(tr("Unlink IDs"), 80, page);
	sizer_2->addStretch(1);
	sizer_2->addWidget(excl);
	sizer_2->addWidget(link);
	sizer_2->addWidget(isolate);
	sizer_2->addWidget(unlink);
	sizer_2->addStretch(1);
	auto* sizer_v = new QVBoxLayout(page);
	sizer_v->addSpacing(10);
	sizer_v->addLayout(sizer_1);
	sizer_v->addSpacing(10);
	sizer_v->addLayout(sizer_2);
	sizer_v->addSpacing(10);
	connect(m_comp_id_text2, &QLineEdit::textEdited, this, [this]() { OnCompIDText(true); });
	connect(m_comp_id_text2, &QLineEdit::returnPressed, this, [this]() { OnCompAppend(false); });
	connect(comp_id_x_btn, &QPushButton::clicked, this, [this]() { m_comp_id_text->clear(); m_comp_id_text2->clear(); });
	connect(comp_append_btn, &QPushButton::clicked, this, [this]() { OnCompAppend(false); });
	connect(comp_clear_btn, &QPushButton::clicked, this, &TraceDialog::OnCompClear);
	connect(m_manual_assist_check, &QToolButton::toggled, this, [this](bool on) {
		// wx OnManualAssistCheck
		m_manual_assist = on;
		UpdateCheckIcons();
		if (m_manual_assist)
			StatAppend("Auto link enabled. Remember to turn it off after tracking!\n");
		else if (!m_auto_id)
			m_stat_text->clear();
	});
	connect(excl, &QPushButton::clicked, this, &TraceDialog::OnCellExclusiveLink);
	connect(link, &QPushButton::clicked, this, &TraceDialog::OnCellLink);
	connect(isolate, &QPushButton::clicked, this, &TraceDialog::OnCellIsolate);
	connect(unlink, &QPushButton::clicked, this, &TraceDialog::OnCellUnlink);
	return page;
}

QWidget* TraceDialog::CreateModifyPage(QWidget* parent)
{
	auto* page = new QWidget(parent);
	auto* sizer_1 = new QHBoxLayout();
	m_cell_new_id_text = new QLineEdit(page);
	m_cell_new_id_text->setFixedWidth(77);
	auto* new_id_x_btn = Btn(tr("X"), 23, page);
	auto* comp_append2_btn = Btn(tr("Append"), 65, page);
	auto* comp_clear_btn = Btn(tr("Clear"), 65, page);
	m_auto_id_chk = new QToolButton(page);
	m_auto_id_chk->setObjectName("AutoAssignIDCheck");
	m_auto_id_chk->setCheckable(true);
	m_auto_id_chk->setAutoRaise(true);
	m_auto_id_chk->setIconSize(QSize(92, 23));
	m_auto_id_chk->setToolTip(tr("Automatically assign an ID to selection after each paint brush stroke"));
	sizer_1->addSpacing(5);
	sizer_1->addWidget(Label(tr("New ID/Selection:"), 100, page));
	sizer_1->addWidget(m_cell_new_id_text);
	sizer_1->addWidget(new_id_x_btn);
	sizer_1->addSpacing(10);
	sizer_1->addWidget(comp_append2_btn);
	sizer_1->addWidget(comp_clear_btn);
	sizer_1->addStretch(1);
	sizer_1->addWidget(m_auto_id_chk);
	auto* sizer_2 = new QHBoxLayout();
	auto* assign = Btn(tr("Assign ID"), 75, page);
	auto* add = Btn(tr("Add ID"), 75, page);
	auto* replace = Btn(tr("Replace ID"), 85, page);
	auto* combine = Btn(tr("Combine"), 75, page);
	auto* divide = Btn(tr("Divide"), 75, page);
	m_cell_segment_btn = Btn(tr("Segment"), 75, page);
	sizer_2->addStretch(1);
	sizer_2->addWidget(assign);
	sizer_2->addWidget(add);
	sizer_2->addWidget(replace);
	sizer_2->addWidget(combine);
	sizer_2->addWidget(divide);
	sizer_2->addWidget(m_cell_segment_btn);
	m_cell_segment_btn->hide();   // wx m_cell_segment_btn->Hide()
	sizer_2->addStretch(1);
	auto* sizer_v = new QVBoxLayout(page);
	sizer_v->addSpacing(10);
	sizer_v->addLayout(sizer_1);
	sizer_v->addSpacing(10);
	sizer_v->addLayout(sizer_2);
	sizer_v->addSpacing(10);
	connect(m_cell_new_id_text, &QLineEdit::textChanged, this, &TraceDialog::OnCellNewIDText);
	connect(m_cell_new_id_text, &QLineEdit::returnPressed, this, [this]() { OnCompAppend(true); });
	connect(new_id_x_btn, &QPushButton::clicked, this, [this]() { m_cell_new_id_text->clear(); });
	connect(comp_append2_btn, &QPushButton::clicked, this, [this]() { OnCompAppend(true); });
	connect(comp_clear_btn, &QPushButton::clicked, this, &TraceDialog::OnCompClear);
	connect(m_auto_id_chk, &QToolButton::toggled, this, [this](bool on) {
		// wx OnAutoIDChk
		m_auto_id = on;
		UpdateCheckIcons();
		if (m_auto_id)
			StatAppend("Auto ID assignment enabled. Remember to turn it off after finish tracking!\n");
		else if (!m_manual_assist)
			m_stat_text->clear();
	});
	connect(assign, &QPushButton::clicked, this, &TraceDialog::OnCellNewID);
	connect(add, &QPushButton::clicked, this, &TraceDialog::OnCellAppendID);
	connect(replace, &QPushButton::clicked, this, &TraceDialog::OnCellReplaceID);
	connect(combine, &QPushButton::clicked, this, &TraceDialog::OnCellCombineID);
	connect(divide, &QPushButton::clicked, this, &TraceDialog::OnCellDivideID);
	connect(m_cell_segment_btn, &QPushButton::clicked, this, &TraceDialog::OnCellSegment);
	return page;
}

QWidget* TraceDialog::CreateAnalysisPage(QWidget* parent)
{
	auto* page = new QWidget(parent);
	auto* sizer_1 = new QHBoxLayout();
	auto* rulers = Btn(tr("Rulers"), 65, page);
	auto* uni = Btn(tr("UniIDs"), 65, page);
	sizer_1->addSpacing(5);
	sizer_1->addWidget(Label(tr("Convert to:"), 100, page));
	sizer_1->addWidget(rulers);
	sizer_1->addWidget(uni);
	sizer_1->addStretch(1);
	auto* sizer_2 = new QHBoxLayout();
	auto* comps = Btn(tr("Compnts"), 65, page);
	auto* links = Btn(tr("Links"), 65, page);
	auto* save = Btn(tr("Save As"), 65, page);
	sizer_2->addSpacing(5);
	sizer_2->addWidget(Label(tr("Information:"), 100, page));
	sizer_2->addWidget(comps);
	sizer_2->addWidget(links);
	sizer_2->addWidget(save);
	sizer_2->addStretch(1);
	auto* sizer_v = new QVBoxLayout(page);
	sizer_v->addSpacing(10);
	sizer_v->addLayout(sizer_1);
	sizer_v->addSpacing(10);
	sizer_v->addLayout(sizer_2);
	sizer_v->addSpacing(10);
	connect(rulers, &QPushButton::clicked, this, &TraceDialog::OnConvertToRulers);
	connect(uni, &QPushButton::clicked, this, [this]() { OnConvertConsistent(); });
	connect(comps, &QPushButton::clicked, this, &TraceDialog::OnAnalyzeComp);
	connect(links, &QPushButton::clicked, this, &TraceDialog::OnAnalyzeLink);
	connect(save, &QPushButton::clicked, this, [this]() { OnSaveResult(); });
	return page;
}

TraceDialog::TraceDialog(QWidget* parent)
	: QWidget(parent, Qt::Tool)
{
	setObjectName("TraceDlg");
	setWindowTitle(tr("Components & Tracking"));
	resize(500, 600);   // wx wxSize(500, 600)

	m_notebook = new QTabWidget(this);
	m_notebook->addTab(CreateMapPage(m_notebook), QString::fromWCharArray(L"Track Map ⇨"));
	m_notebook->addTab(CreateSelectPage(m_notebook), QString::fromWCharArray(L"Selection ⇨"));
	m_notebook->addTab(CreateLinkPage(m_notebook), QString::fromWCharArray(L"Linkage ⇨"));
	m_notebook->addTab(CreateModifyPage(m_notebook), QString::fromWCharArray(L"Modify ⇨"));
	m_notebook->addTab(CreateAnalysisPage(m_notebook), tr("Analysis"));

	//ghost num
	auto* sizer_1 = new QHBoxLayout();
	m_ghost_show_tail_chk = new QCheckBox(tr("Tail"), this);
	m_ghost_num_sldr = new QSlider(Qt::Horizontal, this);
	m_ghost_num_sldr->setRange(0, 20);
	m_ghost_num_sldr->setValue(10);
	m_ghost_num_text = new QLineEdit("10", this);
	m_ghost_num_text->setFixedWidth(60);
	m_ghost_num_text->setValidator(new QIntValidator(0, 2147483647, m_ghost_num_text));
	m_ghost_show_lead_chk = new QCheckBox(tr("Lead"), this);
	sizer_1->addSpacing(5);
	sizer_1->addWidget(Label(tr("Ghosts:"), 70, this));
	sizer_1->addWidget(m_ghost_show_tail_chk);
	sizer_1->addSpacing(5);
	sizer_1->addWidget(m_ghost_num_sldr, 1);
	sizer_1->addWidget(m_ghost_num_text);
	sizer_1->addSpacing(5);
	sizer_1->addWidget(m_ghost_show_lead_chk);

	//lists
	auto* box2 = new QGroupBox(tr("ID Lists"), this);
	auto* sizer_2 = new QVBoxLayout(box2);
	auto* sizer_21 = new QHBoxLayout();
	m_cell_time_curr_st = new QLabel(tr("    Current T"), box2);
	m_cell_time_prev_st = new QLabel(tr("    Previous T"), box2);
	auto* prev_btn = Btn(tr("< Backward"), 80, box2);
	auto* next_btn = Btn(tr("Forward >"), 80, box2);
	sizer_21->addWidget(m_cell_time_curr_st, 1);
	sizer_21->addWidget(prev_btn);
	sizer_21->addWidget(next_btn);
	sizer_21->addWidget(m_cell_time_prev_st, 1);
	auto* sizer_22 = new QHBoxLayout();
	m_trace_list_curr = new TraceListWidget(this, 0, box2);
	m_trace_list_prev = new TraceListWidget(this, 1, box2);
	sizer_22->addWidget(m_trace_list_curr, 1);
	sizer_22->addWidget(m_trace_list_prev, 1);
	sizer_2->addLayout(sizer_21);
	sizer_2->addLayout(sizer_22, 1);

	//stats text
	auto* box3 = new QGroupBox(tr("Output"), this);
	auto* sizer_3 = new QVBoxLayout(box3);
	m_stat_text = new QPlainTextEdit(box3);
	m_stat_text->setObjectName("TraceStatText");
	m_stat_text->setReadOnly(true);
	m_stat_text->setFixedHeight(100);
	sizer_3->addWidget(m_stat_text);

	auto* sizer_v = new QVBoxLayout(this);
	sizer_v->addSpacing(10);
	sizer_v->addWidget(m_notebook, 0);
	sizer_v->addSpacing(10);
	sizer_v->addLayout(sizer_1);
	sizer_v->addSpacing(10);
	sizer_v->addWidget(box2, 1);
	sizer_v->addSpacing(10);
	sizer_v->addWidget(box3, 0);
	sizer_v->addSpacing(10);

	// wx OnGhostNumChange / OnGhostNumText / OnGhostShowTail / OnGhostShowLead
	connect(m_ghost_num_sldr, &QSlider::valueChanged, this, [this](int v) { m_ghost_num_text->setText(QString::number(v)); OnGhostNumText(); });
	connect(m_ghost_num_text, &QLineEdit::textEdited, this, &TraceDialog::OnGhostNumText);
	connect(m_ghost_show_tail_chk, &QCheckBox::clicked, this, [this](bool show) {
		TraceGroup* tg = m_view ? m_view->GetTraceGroup() : nullptr;
		if (tg) { tg->SetDrawTail(show); m_view->RefreshView(); } });
	connect(m_ghost_show_lead_chk, &QCheckBox::clicked, this, [this](bool show) {
		TraceGroup* tg = m_view ? m_view->GetTraceGroup() : nullptr;
		if (tg) { tg->SetDrawLead(show); m_view->RefreshView(); } });
	connect(prev_btn, &QPushButton::clicked, this, &TraceDialog::OnCellPrev);
	connect(next_btn, &QPushButton::clicked, this, &TraceDialog::OnCellNext);
	UpdateCheckIcons();
}

TraceDialog::~TraceDialog() = default;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

VolumeData* TraceDialog::CurVolume() const
{
	return m_view ? m_view->GetCurrentVolume() : nullptr;   // wx m_view->m_glview->m_cur_vol
}

void TraceDialog::StatSet(const QString& s)
{
	m_stat_text->setPlainText(s);
}

void TraceDialog::StatAppend(const QString& s)
{
	// wx (*m_stat_text) << str
	m_stat_text->moveCursor(QTextCursor::End);
	m_stat_text->insertPlainText(s);
	m_stat_text->moveCursor(QTextCursor::End);
}

QString TraceDialog::StatText() const
{
	return m_stat_text->toPlainText();
}

QString TraceDialog::TrackMapText() const
{
	return m_load_trace_text->text();
}

void TraceDialog::Yield()
{
	// wx m_app->Yield()
	QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void TraceDialog::UpdateCheckIcons()
{
	// wx SetToolNormalBitmap(auto_link_on / off, auto_assign_on / off)
	m_manual_assist_check->setIcon(m_manual_assist ? VVD_ICON(auto_link_on) : VVD_ICON(auto_link_off));
	m_auto_id_chk->setIcon(m_auto_id ? VVD_ICON(auto_assign_on) : VVD_ICON(auto_assign_off));
}

void TraceDialog::SetManualAssist(bool b)
{
	m_manual_assist_check->setChecked(b);
}

void TraceDialog::SetAutoID(bool b)
{
	m_auto_id_chk->setChecked(b);
}

void TraceDialog::SaveLabel(VolumeData* vd, const std::shared_ptr<VL_Nrrd>& label)
{
	// wx: save label mask to disk (<data name of m_cur_time>.lbl)
	BaseReader* reader = vd ? vd->GetReader() : nullptr;
	if (!reader || !label)
		return;
	const std::wstring label_name = LabelName(reader->GetCurName(m_cur_time, vd->GetCurChannel()));
	MSKWriter msk_writer;
	msk_writer.SetData(label);
	msk_writer.Save(label_name, 1);
}

void TraceDialog::ViewClosed(RenderCanvas* canvas)
{
	if (m_view == canvas)
	{
		m_view = nullptr;
		m_trace_list_curr->UpdateTraces(nullptr);
	}
}

void TraceDialog::VolumeRemoved(VolumeData*)
{
	// the lists keep their text rows (wx); nothing refers to the volume
}

// ---------------------------------------------------------------------------
// wx GetSettings / UpdateList
// ---------------------------------------------------------------------------

void TraceDialog::GetSettings(RenderCanvas* canvas)
{
	if (!canvas) return;
	m_view = canvas;
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (trace_group)
	{
		const QString str = QString::fromStdWString(trace_group->GetPath());
		if (!str.isEmpty())
			m_load_trace_text->setText(str);
		else
			m_load_trace_text->setText(tr("Track map created but not saved"));
		UpdateList();
		const int ghost_num = trace_group->GetGhostNum();
		{
			QSignalBlocker b1(m_ghost_num_text), b2(m_ghost_num_sldr);
			m_ghost_num_text->setText(QString::number(ghost_num));
			m_ghost_num_sldr->setValue(ghost_num);
		}
		m_ghost_show_tail_chk->setChecked(trace_group->GetDrawTail());
		m_ghost_show_lead_chk->setChecked(trace_group->GetDrawLead());
	}
	else
	{
		m_cur_time = m_view->GetTSeqCurNum();
		m_load_trace_text->setText(tr("No Track map"));
	}
	//manual tracking assist / auto id
	{
		QSignalBlocker b1(m_manual_assist_check), b2(m_auto_id_chk);
		m_manual_assist_check->setChecked(m_manual_assist);
		m_auto_id_chk->setChecked(m_auto_id);
	}
	UpdateCheckIcons();
}

void TraceDialog::UpdateList()
{
	if (!m_view) return;
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (trace_group)
	{
		const int cur_time = trace_group->GetCurTime();
		const int prv_time = trace_group->GetPrvTime();
		if (cur_time != m_cur_time)
		{
			//copy current to previous
			m_trace_list_prev->DeleteAllItems();
			for (int item = 0; item < m_trace_list_curr->ItemCount(); item++)
			{
				unsigned long id = 0;
				long size = 0;
				ToULong(m_trace_list_curr->GetText(item, 1), id);
				ToLong(m_trace_list_curr->GetText(item, 2), size);
				const double x = m_trace_list_curr->GetText(item, 3).toDouble();
				const double y = m_trace_list_curr->GetText(item, 4).toDouble();
				const double z = m_trace_list_curr->GetText(item, 5).toDouble();
				m_trace_list_prev->Append(m_trace_list_curr->GetText(item, 0), (unsigned int)id,
					IdColor((unsigned int)id), (int)size, x, y, z);
			}
			m_cur_time = cur_time;
			m_prv_time = prv_time;
		}
		//set time text
		m_cell_time_curr_st->setText(QString("    Current T: %1").arg(m_cur_time));
		if (m_prv_time != m_cur_time)
			m_cell_time_prev_st->setText(QString("    Previous T: %1").arg(m_prv_time));
		else
			m_cell_time_prev_st->setText("    Previous T");
	}
	m_trace_list_curr->UpdateTraces(m_view);
}

// ---------------------------------------------------------------------------
// track map page
// ---------------------------------------------------------------------------

void TraceDialog::OnLoadTrace(const QString& file_in)
{
	if (!m_view) return;
	QString filename = file_in;
	if (filename.isEmpty())
		filename = QFileDialog::getOpenFileName(this, tr("Choose a FluoRender track file"), QString(), "*.track");
	if (filename.isEmpty()) return;
	if (m_view->LoadTraceGroup(filename))
		m_load_trace_text->setText(filename);
}

void TraceDialog::OnSaveTrace()
{
	if (!m_view) return;
	QString filename;
	if (TraceGroup* trace_group = m_view->GetTraceGroup())
		filename = QString::fromStdWString(trace_group->GetPath());
	if (!filename.isEmpty() && QFileInfo::exists(filename))
		m_view->SaveTraceGroup(filename);
	else
		OnSaveasTrace();
}

void TraceDialog::OnSaveasTrace(const QString& file_in)
{
	if (!m_view) return;
	QString filename = file_in;
	if (filename.isEmpty())
		filename = QFileDialog::getSaveFileName(this, tr("Save a FluoRender track file"), QString(), "*.track");
	if (filename.isEmpty()) return;
	if (m_view->SaveTraceGroup(filename))
		m_load_trace_text->setText(filename);
}

void TraceDialog::OnGhostNumText()
{
	long ival = 0;
	ToLong(m_ghost_num_text->text(), ival);
	{
		QSignalBlocker b(m_ghost_num_sldr);
		m_ghost_num_sldr->setValue((int)ival);
	}
	if (m_view)
		if (TraceGroup* trace_group = m_view->GetTraceGroup())
		{
			trace_group->SetGhostNum((int)ival);
			m_view->RefreshView();
		}
}

void TraceDialog::OnCellSizeText()
{
	long ival = 0;
	ToLong(m_cell_size_text->text(), ival);
	{
		QSignalBlocker b(m_cell_size_sldr);
		m_cell_size_sldr->setValue((int)ival);
	}
	if (m_view)
		if (TraceGroup* trace_group = m_view->GetTraceGroup())
			trace_group->SetCellSize((int)ival);
}

// ---------------------------------------------------------------------------
// analysis page
// ---------------------------------------------------------------------------

void TraceDialog::OnConvertToRulers()
{
	if (!m_view)
		return;
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (!trace_group)
		return;
	VolumeData* vd = CurVolume();
	if (!vd)
		return;
	RulerTool* ruler_tool = m_hooks.ruler_tool ? m_hooks.ruler_tool(m_view) : nullptr;
	if (!ruler_tool || !ruler_tool->GetRulerList())
		return;
	double spcx, spcy, spcz;
	vd->GetSpacings(spcx, spcy, spcz);
	//get rulers
	FL::RulerList rulers;
	trace_group->GetMappedRulers(rulers);
	for (auto iter = rulers.begin(); iter != rulers.end(); ++iter)
	{
		(*iter)->Scale(spcx, spcy, spcz);
		ruler_tool->GetRulerList()->push_back(*iter);
	}
	ruler_tool->RulersModified();
	m_view->RefreshView();
	if (m_hooks.measure_settings) m_hooks.measure_settings();
}

void TraceDialog::OnConvertConsistent(const QString& dir_in)
{
	if (!m_view)
		return;
	VolumeData* vd = CurVolume();
	if (!vd)
		return;
	BaseReader* reader = vd->GetReader();
	if (!reader)
		return;
	LBLReader lbl_reader;
	MSKWriter lbl_writer;
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (!trace_group)
		return;
	QString out_dir = dir_in;
	if (out_dir.isEmpty())
		out_dir = QFileDialog::getExistingDirectory(this, tr("Save labels in"));
	if (out_dir.isEmpty())
		return;

	StatSet("Generating consistent IDs in");
	Yield();
	FL::TrackMap& track_map = trace_group->GetTrackMap();
	FL::TrackMapProcessor tm_processor;
	int chan = vd->GetCurChannel();
	int nx, ny, nz;
	vd->GetResolution(nx, ny, nz);
	unsigned long long size = (unsigned long long)nx * (unsigned long long)ny * (unsigned long long)nz;
	unsigned long long index;
	std::shared_ptr<VL_Nrrd> nrrd_label_in1, nrrd_label_in2, nrrd_label_out1, nrrd_label_out2;
	boost::unordered_map<unsigned int, unsigned int> cell_map;
	unsigned int label_in1, label_in2;
	BaseReader* lr = &lbl_reader;
	auto out_name = [&out_dir](const std::wstring& label_name) {
		// wx: out_dir + label_name.Right(Length() - find_last_of(GETSLASH()))
		const size_t slash = label_name.find_last_of(L"/\\");
		return out_dir.toStdWString() + (slash == std::wstring::npos ? L"/" + label_name : label_name.substr(slash));
	};

	//read first frame
	std::wstring label_name = LabelName(reader->GetCurName(0, chan));
	lbl_reader.SetFile(label_name);
	nrrd_label_in1 = lr->Convert(0, chan, true);
	if (!nrrd_label_in1 || !nrrd_label_in1->getNrrd())
	{
		StatAppend("Label in 1 of frame 0 could not be read.\n");
		return;
	}
	StatAppend(QString::asprintf("Label in 1 of frame %d read.\n", 0));
	Yield();
	//duplicate
	nrrd_label_in2 = std::make_shared<VL_Nrrd>(nrrd_label_in1->getNrrdDeepCopy());
	for (index = 0; index < size; ++index)
	{
		label_in1 = ((unsigned int*)(nrrd_label_in1->getNrrd()->data))[index];
		auto iter = cell_map.find(label_in1);
		if (iter != cell_map.end())
			((unsigned int*)(nrrd_label_in2->getNrrd()->data))[index] = iter->second;
		else if (tm_processor.GetMappedID(track_map, label_in1, label_in2, 0))
		{
			((unsigned int*)(nrrd_label_in2->getNrrd()->data))[index] = label_in2;
			cell_map.insert(std::pair<unsigned int, unsigned int>(label_in1, label_in2));
		}
	}
	lbl_writer.SetData(nrrd_label_in2);
	lbl_writer.Save(out_name(label_name), 1);
	StatAppend(QString::asprintf("Label in 2 of frame %d written.\n", 0));
	Yield();

	unsigned int label_out1, label_out2;
	//remaining frames
	for (size_t fi = 1; fi < track_map.GetFrameNum(); ++fi)
	{
		cell_map.clear();
		//read fi frame
		label_name = LabelName(reader->GetCurName((int)fi, chan));
		lbl_reader.SetFile(label_name);
		nrrd_label_out1 = lr->Convert((int)fi, chan, true);
		if (!nrrd_label_out1 || !nrrd_label_out1->getNrrd())
		{
			StatAppend(QString::asprintf("Label out 1 of frame %d could not be read.\n", int(fi)));
			return;
		}
		StatAppend(QString::asprintf("Label out 1 of frame %d read.\n", int(fi)));
		Yield();
		//copy
		nrrd_label_out2 = std::make_shared<VL_Nrrd>(nrrd_label_out1->getNrrdDeepCopy());
		for (index = 0; index < size; ++index)
		{
			label_out1 = ((unsigned int*)(nrrd_label_out1->getNrrd()->data))[index];
			auto iter = cell_map.find(label_out1);
			if (iter != cell_map.end())
				((unsigned int*)(nrrd_label_out2->getNrrd()->data))[index] = iter->second;
			else if (tm_processor.GetMappedID(track_map, label_out1, label_in1, fi, fi - 1))
			{
				label_out2 = GetMappedID(label_in1,
					(unsigned int*)(nrrd_label_in1->getNrrd()->data),
					(unsigned int*)(nrrd_label_in2->getNrrd()->data), size);
				if (label_out2)
				{
					((unsigned int*)(nrrd_label_out2->getNrrd()->data))[index] = label_out2;
					cell_map.insert(std::pair<unsigned int, unsigned int>(label_out1, label_out2));
				}
			}
		}
		//save
		lbl_writer.SetData(nrrd_label_out2);
		lbl_writer.Save(out_name(label_name), 1);
		StatAppend(QString::asprintf("Label out 2 of frame %d written.\n", int(fi)));
		Yield();
		//swap
		nrrd_label_in1 = nrrd_label_out1;
		nrrd_label_in2 = nrrd_label_out2;
	}
	StatAppend("All done.\n");
}

void TraceDialog::OnAnalyzeComp()
{
	Measure();
	StatSet(OutputMeasureResult());
}

void TraceDialog::OnAnalyzeLink()
{
	if (!m_view)
		return;
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (!trace_group)
		return;
	size_t frames = trace_group->GetTrackMap().GetFrameNum();
	if (frames == 0)
		StatSet("ERROR! Generate a track map first.\n");
	else
		StatSet(QString::asprintf("Time point number: %d\n", int(frames)));
	StatAppend("Time\tIn Orphan\tOut Orphan\tIn Multi\tOut Multi\n");
	FL::VertexList in_orphan_list, out_orphan_list, in_multi_list, out_multi_list;
	for (size_t fi = 0; fi < frames; ++fi)
	{
		trace_group->GetLinkLists(fi, in_orphan_list, out_orphan_list, in_multi_list, out_multi_list);
		StatAppend(QString("%1\t%2\t%3\t%4\t%5\n").arg(int(fi)).arg(int(in_orphan_list.size()))
			.arg(int(out_orphan_list.size())).arg(int(in_multi_list.size())).arg(int(out_multi_list.size())));
	}
}

void TraceDialog::OnSaveResult(const QString& file_in)
{
	QString filename = file_in;
	if (filename.isEmpty())
		filename = QFileDialog::getSaveFileName(this, tr("Save results"), QString(), tr("Text file (*.txt)"));
	if (!filename.isEmpty())
		SaveOutputResult(filename);
}

bool TraceDialog::SaveOutputResult(const QString& filename)
{
	// wx: wxTextOutputStream << the output text (native line ends)
	QFile f(filename);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
		return false;
	QTextStream ts(&f);
	ts << m_stat_text->toPlainText();
	return true;
}

// ---------------------------------------------------------------------------
// component tools (selection page)
// ---------------------------------------------------------------------------

void TraceDialog::CompDelete()
{
	if (!m_view)
		return;
	std::vector<unsigned int> ids;
	for (int item = 0; item < m_trace_list_curr->ItemCount(); item++)
	{
		if (m_trace_list_curr->IsItemSelected(item))
			continue;
		unsigned long ival = 0;
		if (ToULong(m_trace_list_curr->GetText(item, 1), ival) && ival)
			ids.push_back((unsigned int)ival);
	}
	bool clear_all = ids.empty();
	//get current mask
	VolumeData* vd = CurVolume();
	if (!vd)
		return;
	auto nrrd_mask = vd->GetMask(true);
	if (!nrrd_mask || !nrrd_mask->getNrrd())
		return;
	unsigned char* data_mask = (unsigned char*)(nrrd_mask->getNrrd()->data);
	if (!data_mask)
		return;
	//get current label
	Texture* tex = vd->GetTexture();
	if (!tex)
		return;
	auto nrrd_label = tex->get_nrrd(tex->nlabel());
	if (!nrrd_label || !nrrd_label->getNrrd())
		return;
	unsigned int* data_label = (unsigned int*)(nrrd_label->getNrrd()->data);
	if (!data_label)
		return;
	int nx, ny, nz;
	vd->GetResolution(nx, ny, nz);
	unsigned long long for_size = (unsigned long long)nx * (unsigned long long)ny * (unsigned long long)nz;
	for (unsigned long long index = 0; index < for_size; ++index)
	{
		if (clear_all)
			data_mask[index] = 0;
		else if (std::find(ids.begin(), ids.end(), data_label[index]) != ids.end())
			data_mask[index] = 255;
		else
			data_mask[index] = 0;
	}
	//invalidate label mask in gpu
	vd->GetVR()->clear_tex_current();
	//update view
	CellUpdate();
}

void TraceDialog::CompClear()
{
	// wx frame->GetTree()->BrushClear()
	if (m_hooks.brush_clear) m_hooks.brush_clear();
	CellUpdate();
	m_trace_list_prev->DeleteAllItems();
}

void TraceDialog::OnCompIDText(bool from_second)
{
	// wx OnCompIDText: the Selection and Linkage pages share the ID text
	if (!from_second)
		m_comp_id_text2->setText(m_comp_id_text->text());
	else
		m_comp_id_text->setText(m_comp_id_text2->text());
}

void TraceDialog::OnCompFull()
{
	if (m_comp_id_text->text().isEmpty())
		CellFull();
	else
		OnCompAppend(false);
}

// wx OnCompAppend: from_new_id = the Modify page's Append / Enter (the "New
// ID/Selection" text), else the Selection / Linkage ID text
void TraceDialog::OnCompAppend(bool from_new_id)
{
	if (!m_view)
		return;
	const QString str = from_new_id ? m_cell_new_id_text->text() : m_comp_id_text->text();
	if (!str.isEmpty())
	{
		bool get_all = false;
		unsigned long ival = 0;
		if (str.toLower() == "all")
			get_all = true;
		else
		{
			ToULong(str, ival);
			if (ival == 0)
				return;
		}
		unsigned int id = (unsigned int)ival;
		//get current mask
		VolumeData* vd = CurVolume();
		if (!vd)
			return;
		auto nrrd_mask = vd->GetMask(true);
		if (!nrrd_mask || !nrrd_mask->getNrrd())
		{
			vd->AddEmptyMask();
			nrrd_mask = vd->GetMask(false);
		}
		if (!nrrd_mask || !nrrd_mask->getNrrd())
			return;
		unsigned char* data_mask = (unsigned char*)(nrrd_mask->getNrrd()->data);
		if (!data_mask)
			return;
		//get current label
		Texture* tex = vd->GetTexture();
		if (!tex)
			return;
		auto nrrd_label = tex->get_nrrd(tex->nlabel());
		if (!nrrd_label || !nrrd_label->getNrrd())
			return;
		unsigned int* data_label = (unsigned int*)(nrrd_label->getNrrd()->data);
		if (!data_label)
			return;
		//select append
		int nx, ny, nz;
		vd->GetResolution(nx, ny, nz);
		unsigned long long for_size = (unsigned long long)nx * (unsigned long long)ny * (unsigned long long)nz;
		for (unsigned long long index = 0; index < for_size; ++index)
		{
			if (get_all)
			{
				if (data_label[index])
					data_mask[index] = 255;
			}
			else if (data_label[index] == id)
				data_mask[index] = 255;
		}
		//invalidate label mask in gpu
		vd->GetVR()->clear_tex_current();
	}
	//update view
	CellUpdate();
}

void TraceDialog::OnCompExclusive()
{
	if (!m_view)
		return;
	unsigned long ival = 0;
	ToULong(m_comp_id_text->text(), ival);
	if (ival != 0)
	{
		unsigned int id = (unsigned int)ival;
		VolumeData* vd = CurVolume();
		if (!vd)
			return;
		auto nrrd_mask = vd->GetMask(true);
		if (!nrrd_mask || !nrrd_mask->getNrrd())
			return;
		unsigned char* data_mask = (unsigned char*)(nrrd_mask->getNrrd()->data);
		if (!data_mask)
			return;
		Texture* tex = vd->GetTexture();
		if (!tex)
			return;
		auto nrrd_label = tex->get_nrrd(tex->nlabel());
		if (!nrrd_label || !nrrd_label->getNrrd())
			return;
		unsigned int* data_label = (unsigned int*)(nrrd_label->getNrrd()->data);
		if (!data_label)
			return;
		int nx, ny, nz;
		vd->GetResolution(nx, ny, nz);
		unsigned long long for_size = (unsigned long long)nx * (unsigned long long)ny * (unsigned long long)nz;
		for (unsigned long long index = 0; index < for_size; ++index)
			data_mask[index] = data_label[index] == id ? 255 : 0;
		vd->GetVR()->clear_tex_current();
	}
	CellUpdate();
}

// ---------------------------------------------------------------------------
// ID link controls
// ---------------------------------------------------------------------------

void TraceDialog::CellUpdate()
{
	if (!m_view)
		return;
	long ival = 0;
	ToLong(m_ghost_num_text->text(), ival);
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (trace_group)
		trace_group->SetGhostNum((int)ival);
	else
		m_view->CreateTraceGroup();
	m_view->GetTraces();
	m_view->RefreshView();
}

void TraceDialog::CellFull()
{
	if (!m_view)
		return;
	//cell size filter
	long ival = 0;
	ToLong(m_cell_size_text->text(), ival);
	unsigned int slimit = (unsigned int)ival;
	//get current mask
	VolumeData* vd = CurVolume();
	if (!vd)
		return;
	auto nrrd_mask = vd->GetMask(true);
	if (!nrrd_mask || !nrrd_mask->getNrrd())
		return;
	unsigned char* data_mask = (unsigned char*)(nrrd_mask->getNrrd()->data);
	if (!data_mask)
		return;
	//get current label
	Texture* tex = vd->GetTexture();
	if (!tex)
		return;
	auto nrrd_label = tex->get_nrrd(tex->nlabel());
	if (!nrrd_label || !nrrd_label->getNrrd())
		return;
	unsigned int* data_label = (unsigned int*)(nrrd_label->getNrrd()->data);
	if (!data_label)
		return;
	//get selected IDs
	int nx, ny, nz;
	unsigned long long index;
	vd->GetResolution(nx, ny, nz);
	unsigned int label_value;
	FL::CellList sel_labels;
	for (int i = 0; i < nx; ++i)
		for (int j = 0; j < ny; ++j)
			for (int k = 0; k < nz; ++k)
			{
				index = (unsigned long long)nx * ny * k + (unsigned long long)nx * j + i;
				if (data_mask[index] && data_label[index])
				{
					label_value = data_label[index];
					auto label_iter = sel_labels.find(label_value);
					if (label_iter == sel_labels.end())
					{
						FL::pCell cell(new FL::Cell(label_value));
						cell->Inc(i, j, k, 1.0f);
						sel_labels.insert(std::pair<unsigned int, FL::pCell>(label_value, cell));
					}
					else
						label_iter->second->Inc(i, j, k, 1.0f);
				}
			}
	//reselect
	for (int i = 0; i < nx; ++i)
		for (int j = 0; j < ny; ++j)
			for (int k = 0; k < nz; ++k)
			{
				index = (unsigned long long)nx * ny * k + (unsigned long long)nx * j + i;
				if (data_label[index])
				{
					label_value = data_label[index];
					auto label_iter = sel_labels.find(label_value);
					if (label_iter != sel_labels.end() && label_iter->second->GetSizeUi() > slimit)
						data_mask[index] = 255;
					else
						data_mask[index] = 0;
				}
			}
	vd->GetVR()->clear_tex_current();
	CellUpdate();
}

void TraceDialog::AddLabel(int item, TraceListWidget* list_ctrl, FL::CellList& list)
{
	unsigned long id = 0, size = 0;
	ToULong(list_ctrl->GetText(item, 1), id);
	ToULong(list_ctrl->GetText(item, 2), size);
	const double x = list_ctrl->GetText(item, 3).toDouble();
	const double y = list_ctrl->GetText(item, 4).toDouble();
	const double z = list_ctrl->GetText(item, 5).toDouble();
	FL::pCell cell(new FL::Cell((unsigned int)id));
	cell->SetSizeUi((unsigned int)size);
	cell->SetSizeF(float(size));
	FLIVR::Point p(x, y, z);
	cell->SetCenter(p);
	list.insert(std::pair<unsigned int, FL::pCell>((unsigned int)id, cell));
}

void TraceDialog::FillList(TraceListWidget* list_ctrl, FL::CellList& list)
{
	for (int item = 0; item < list_ctrl->ItemCount(); item++)
		if (list_ctrl->IsItemSelected(item))
			AddLabel(item, list_ctrl, list);
	if (list.empty())
		for (int item = 0; item < list_ctrl->ItemCount(); item++)
			AddLabel(item, list_ctrl, list);
}

void TraceDialog::CellNewID(bool append)
{
	if (!m_view)
		return;
	//trace group
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (!trace_group)
	{
		m_view->CreateTraceGroup();
		trace_group = m_view->GetTraceGroup();
	}
	VolumeData* vd = CurVolume();
	if (!vd)
		return;
	std::shared_ptr<VL_Nrrd> nrrd_mask;
	if (m_auto_id)
		nrrd_mask = vd->GetMask(false);   //get prev mask
	else
		nrrd_mask = vd->GetMask(true);    //get current mask
	if (!nrrd_mask || !nrrd_mask->getNrrd())
	{
		vd->AddEmptyMask();
		nrrd_mask = vd->GetMask(false);
	}
	if (!nrrd_mask || !nrrd_mask->getNrrd())
		return;
	unsigned char* data_mask = (unsigned char*)(nrrd_mask->getNrrd()->data);
	if (!data_mask)
		return;
	//get current label
	Texture* tex = vd->GetTexture();
	if (!tex)
		return;
	std::shared_ptr<VL_Nrrd> nrrd_label = tex->get_nrrd(tex->nlabel());
	if (!nrrd_label || !nrrd_label->getNrrd())
	{
		vd->AddEmptyLabel();
		nrrd_label = tex->get_nrrd(tex->nlabel());
	}
	if (!nrrd_label || !nrrd_label->getNrrd())
		return;
	unsigned int* data_label = (unsigned int*)(nrrd_label->getNrrd()->data);
	if (!data_label)
		return;
	int nx, ny, nz;
	vd->GetResolution(nx, ny, nz);
	unsigned long long for_size = (unsigned long long)nx * (unsigned long long)ny * (unsigned long long)nz;
	unsigned long long index;
	//get ID of currently/previously masked region
	unsigned long id_str = 0;
	unsigned long id_vol = 0;
	bool id_str_empty = false;
	if (!ToULong(m_cell_new_id_text->text(), id_str))
	{
		id_str_empty = true;
		for (index = 0; index < for_size; ++index)
		{
			if (data_mask[index] && data_label[index])
			{
				id_vol = data_label[index];
				break;
			}
		}
	}
	//generate a unique ID
	unsigned int new_id = 0;
	unsigned int inc = 0;
	if (id_str_empty)
	{
		if (id_vol)
		{
			if (m_auto_id) { new_id = (unsigned int)id_vol; inc = 0; }
			else { new_id = (unsigned int)id_vol + 10; inc = 10; }
		}
		else
		{
			new_id = 10;
			inc = 10;
		}
	}
	else
	{
		if (id_str)
		{
			new_id = (unsigned int)id_str;
			inc = m_auto_id ? 0 : 360;
		}
		else
		{
			new_id = 0;
			inc = 0;
		}
	}
	unsigned int stop_id = new_id;
	if (inc)
		while (vd->SearchLabel(new_id))
		{
			new_id += inc;
			if (new_id == stop_id)
			{
				StatAppend(QString::asprintf("ID assignment failed. Type a different ID than %d", stop_id));
				return;
			}
		}
	if (m_auto_id)
	{
		//get current mask
		nrrd_mask = vd->GetMask(true);
		if (!nrrd_mask)
			return;
		// wx keeps data_mask pointing at the previous mask's data here
	}
	//update label volume, set mask region to the new ID
	FL::pCell cell;
	if (new_id)
		cell = FL::pCell(new FL::Cell(new_id));
	for (int i = 0; i < nx; ++i)
		for (int j = 0; j < ny; ++j)
			for (int k = 0; k < nz; ++k)
			{
				index = (unsigned long long)nx * ny * k + (unsigned long long)nx * j + i;
				if (data_mask[index])
				{
					if (m_auto_id)
					{
						if (data_label[index] && data_label[index] != new_id)
						{
							data_mask[index] = 0;
							continue;
						}
					}
					else if (append && data_label[index])
						continue;
					data_label[index] = new_id;
					if (new_id)
						cell->Inc(i, j, k, 1.0f);
				}
			}
	if (new_id)
		trace_group->AddCell(cell, m_cur_time);
	//invalidate label mask in gpu
	vd->GetVR()->clear_tex_current();
	//save label mask to disk
	SaveLabel(vd, nrrd_label);
	CellUpdate();
}

void TraceDialog::CellEraseID()
{
	if (!m_view)
		return;
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (!trace_group)
	{
		m_view->CreateTraceGroup();
		trace_group = m_view->GetTraceGroup();
	}
	VolumeData* vd = CurVolume();
	if (!vd)
		return;
	//get prev mask
	auto nrrd_mask = vd->GetMask(false);
	if (!nrrd_mask || !nrrd_mask->getNrrd())
		return;
	unsigned char* data_mask = (unsigned char*)(nrrd_mask->getNrrd()->data);
	if (!data_mask)
		return;
	//get prev label
	auto nrrd_label = vd->GetLabel(false);
	if (!nrrd_label || !nrrd_label->getNrrd())
		return;
	unsigned int* data_label = (unsigned int*)(nrrd_label->getNrrd()->data);
	if (!data_label)
		return;
	int nx, ny, nz;
	vd->GetResolution(nx, ny, nz);
	unsigned long long for_size = (unsigned long long)nx * (unsigned long long)ny * (unsigned long long)nz;
	unsigned long long index;
	//find old IDs
	std::set<unsigned int> id_list;
	for (index = 0; index < for_size; ++index)
		if (data_mask[index] && data_label[index])
			id_list.insert(data_label[index]);
	if (!id_list.empty())
	{
		//current mask
		nrrd_mask = vd->GetMask(true);
		if (!nrrd_mask)
			return;
		data_mask = (unsigned char*)(nrrd_mask->getNrrd()->data);
		if (!data_mask)
			return;
		for (index = 0; index < for_size; ++index)
		{
			if (data_label[index] && id_list.find(data_label[index]) != id_list.end() && !data_mask[index])
				data_label[index] = 0;
		}
		vd->GetVR()->clear_tex_current();
		SaveLabel(vd, nrrd_label);
	}
	CellUpdate();
}

void TraceDialog::CellLink(bool exclusive)
{
	if (!m_view)
		return;
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (!trace_group)
	{
		m_view->CreateTraceGroup();
		trace_group = m_view->GetTraceGroup();
	}
	FL::CellList list_cur, list_prv;
	FillList(m_trace_list_curr, list_cur);
	FillList(m_trace_list_prev, list_prv);
	if (list_cur.size() == 0 || list_prv.size() == 0)
		return;
	//link them
	trace_group->LinkCells(list_cur, list_prv, m_cur_time, m_prv_time, exclusive);
	m_view->RefreshView();
}

void TraceDialog::OnCellIsolate()
{
	if (!m_view)
		return;
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (!trace_group)
		return;
	FL::CellList list_cur;
	FillList(m_trace_list_curr, list_cur);
	if (list_cur.size() == 0)
		return;
	trace_group->IsolateCells(list_cur, m_cur_time);
	m_view->RefreshView();
}

void TraceDialog::OnCellUnlink()
{
	if (!m_view)
		return;
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (!trace_group)
		return;
	FL::CellList list_cur, list_prv;
	FillList(m_trace_list_curr, list_cur);
	FillList(m_trace_list_prev, list_prv);
	if (list_cur.size() == 0 || list_prv.size() == 0)
		return;
	trace_group->UnlinkCells(list_cur, list_prv, m_cur_time, m_prv_time);
	m_view->RefreshView();
}

// ---------------------------------------------------------------------------
// ID edit controls (modify page)
// ---------------------------------------------------------------------------

void TraceDialog::OnCellNewIDText()
{
	unsigned long id = 0;
	QColor color(255, 255, 255);
	if (ToULong(m_cell_new_id_text->text(), id))
		color = id ? IdColor((unsigned int)id) : QColor(24, 167, 181);
	QPalette pal = m_cell_new_id_text->palette();
	pal.setColor(QPalette::Base, color);
	m_cell_new_id_text->setPalette(pal);
}

void TraceDialog::OnCellReplaceID()
{
	if (!m_view)
		return;
	unsigned long id = 0;
	if (!ToULong(m_cell_new_id_text->text(), id))
		return;
	if (!id)
		return;
	TraceGroup* trace_group = m_view->GetTraceGroup();
	bool track_map = trace_group && trace_group->GetTrackMap().GetFrameNum();
	FL::CellList list_cur;
	FillList(m_trace_list_curr, list_cur);
	VolumeData* vd = CurVolume();
	if (!vd)
		return;
	auto nrrd_mask = vd->GetMask(true);
	if (!nrrd_mask || !nrrd_mask->getNrrd())
		return;
	unsigned char* data_mask = (unsigned char*)(nrrd_mask->getNrrd()->data);
	if (!data_mask)
		return;
	Texture* tex = vd->GetTexture();
	if (!tex)
		return;
	auto nrrd_label = tex->get_nrrd(tex->nlabel());
	if (!nrrd_label || !nrrd_label->getNrrd())
		return;
	unsigned int* data_label = (unsigned int*)(nrrd_label->getNrrd()->data);
	if (!data_label)
		return;
	//replace ID
	boost::unordered_map<unsigned int, unsigned int> list_rep;
	unsigned int old_id, new_id;
	int nx, ny, nz;
	vd->GetResolution(nx, ny, nz);
	unsigned long long for_size = (unsigned long long)nx * (unsigned long long)ny * (unsigned long long)nz;
	for (unsigned long long index = 0; index < for_size; ++index)
	{
		old_id = data_label[index];
		if (!data_mask[index] || !old_id || old_id == id)
			continue;
		auto list_rep_iter = list_rep.find(old_id);
		if (list_rep_iter != list_rep.end())
		{
			data_label[index] = list_rep_iter->second;
			continue;
		}
		auto cell_iter = list_cur.find(old_id);
		if (cell_iter != list_cur.end())
		{
			new_id = (unsigned int)id;
			while (vd->SearchLabel(new_id))
				new_id += 360;
			//add cell to list_rep
			list_rep.insert(std::pair<unsigned int, unsigned int>(old_id, new_id));
			if (track_map)
				trace_group->ReplaceCellID(old_id, new_id, m_cur_time);
			data_label[index] = new_id;
		}
	}
	vd->GetVR()->clear_tex_current();
	SaveLabel(vd, nrrd_label);
	CellUpdate();
}

void TraceDialog::OnCellCombineID()
{
	if (!m_view)
		return;
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (!trace_group)
		return;
	FL::CellList list_cur;
	FillList(m_trace_list_curr, list_cur);
	if (list_cur.size() <= 1)
		return;   //nothing to combine
	//find the largest cell in the list
	FL::pCell cell;
	for (auto cell_iter = list_cur.begin(); cell_iter != list_cur.end(); ++cell_iter)
	{
		if (cell)
		{
			if (cell_iter->second->GetSizeUi() > cell->GetSizeUi())
				cell = cell_iter->second;
		}
		else
			cell = cell_iter->second;
	}
	if (!cell)
		return;
	VolumeData* vd = CurVolume();
	if (!vd)
		return;
	auto nrrd_mask = vd->GetMask(true);
	if (!nrrd_mask || !nrrd_mask->getNrrd())
		return;
	unsigned char* data_mask = (unsigned char*)(nrrd_mask->getNrrd()->data);
	if (!data_mask)
		return;
	Texture* tex = vd->GetTexture();
	if (!tex)
		return;
	auto nrrd_label = tex->get_nrrd(tex->nlabel());
	if (!nrrd_label || !nrrd_label->getNrrd())
		return;
	unsigned int* data_label = (unsigned int*)(nrrd_label->getNrrd()->data);
	if (!data_label)
		return;
	int nx, ny, nz;
	vd->GetResolution(nx, ny, nz);
	unsigned long long for_size = (unsigned long long)nx * (unsigned long long)ny * (unsigned long long)nz;
	for (unsigned long long index = 0; index < for_size; ++index)
	{
		if (!data_mask[index] || !data_label[index])
			continue;
		if (list_cur.find(data_label[index]) != list_cur.end())
			data_label[index] = cell->Id();
	}
	vd->GetVR()->clear_tex_current();
	SaveLabel(vd, nrrd_label);
	//modify graphs
	trace_group->CombineCells(cell, list_cur, m_cur_time);
	CellUpdate();
}

void TraceDialog::OnCellDivideID()
{
	if (!m_view)
		return;
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (!trace_group)
		return;
	FL::CellList list_cur;
	FillList(m_trace_list_curr, list_cur);
	if (list_cur.size() <= 1)
		return;   //nothing to divide
	trace_group->DivideCells(list_cur, m_cur_time);
}

// wx VRenderVulkanView::Segment, after the selection
void TraceDialog::OnViewSegmented(int sel_mode)
{
	if (m_auto_id)
	{
		if (sel_mode == 1 || sel_mode == 2 || sel_mode == 4)
			CellNewID(true);
		else if (sel_mode == 3)
			CellEraseID();
	}
	if (m_manual_assist)
	{
		if (!m_auto_id)
			CellUpdate();
		CellLink(true);
	}
}

// ---------------------------------------------------------------------------
// measurement
// ---------------------------------------------------------------------------

void TraceDialog::Measure()
{
	if (!m_view)
		return;
	VolumeData* vd = CurVolume();
	if (!vd)
		return;
	Texture* tex = vd->GetTexture();
	if (!tex)
		return;
	auto nrrd_data = tex->get_nrrd(0);
	if (!nrrd_data || !nrrd_data->getNrrd())
		return;
	int bits = nrrd_data->getNrrd()->type;
	void* data_data = nrrd_data->getNrrd()->data;
	if (!data_data)
		return;
	auto nrrd_mask = vd->GetMask(true);
	if (!nrrd_mask || !nrrd_mask->getNrrd())
		return;
	unsigned char* data_mask = (unsigned char*)(nrrd_mask->getNrrd()->data);
	if (!data_mask)
		return;
	auto nrrd_label = tex->get_nrrd(tex->nlabel());
	if (!nrrd_label || !nrrd_label->getNrrd())
		return;
	unsigned int* data_label = (unsigned int*)(nrrd_label->getNrrd()->data);
	if (!data_label)
		return;
	m_info_list.clear();
	int nx, ny, nz;
	vd->GetResolution(nx, ny, nz);
	unsigned long long for_size = (unsigned long long)nx * (unsigned long long)ny * (unsigned long long)nz;
	for (unsigned long long index = 0; index < for_size; ++index)
	{
		if (!data_mask[index] || !data_label[index])
			continue;
		unsigned int id = data_label[index];
		// wx: 8 / 16-bit data only (other types leave value undefined there;
		// 0 here, so those voxels are skipped)
		double value = 0.0;
		if (bits == nrrdTypeUChar)
			value = ((unsigned char*)data_data)[index];
		else if (bits == nrrdTypeUShort)
			value = ((unsigned short*)data_data)[index];
		if (value <= 1.0)
			continue;
		int k = int(index / ((unsigned long long)nx * ny));
		int j = int(index % ((unsigned long long)nx * ny));
		int i = j % nx;
		j = j / nx;
		double ext = GetExt(data_label, index, id, nx, ny, nz, i, j, k);
		//find in list
		int found = -1;
		for (int ilist = 0; ilist < (int)m_info_list.size(); ++ilist)
			if (m_info_list[ilist].id == id) { found = ilist; break; }
		if (found == -1)
		{
			measure_info info;
			info.id = id;
			info.total_num = 1;
			info.mean = 0.0;
			info.variance = 0.0;
			info.m2 = 0.0;
			double delta = value - info.mean;
			info.mean += delta / info.total_num;
			info.m2 += delta * (value - info.mean);
			info.min = value;
			info.max = value;
			info.ext_sum = ext;
			m_info_list.push_back(info);
		}
		else
		{
			measure_info& f = m_info_list[found];
			f.total_num++;
			double delta = value - f.mean;
			f.mean += delta / f.total_num;
			f.m2 += delta * (value - f.mean);
			f.min = value < f.min ? value : f.min;
			f.max = value > f.max ? value : f.max;
			f.ext_sum += ext;
		}
	}
	for (size_t i = 0; i < m_info_list.size(); ++i)
		if (m_info_list[i].total_num > 0)
			m_info_list[i].variance = std::sqrt(m_info_list[i].m2 / (m_info_list[i].total_num));
	std::sort(m_info_list.begin(), m_info_list.end(), measure_info::cmp_id);
}

QString TraceDialog::OutputMeasureResult()
{
	QString str = "Statistics on the selection:\n";
	str += "A total of " + QString::asprintf("%llu", (unsigned long long)m_info_list.size()) + " component(s) selected\n";
	str += "ID\tTotalN\tSurfaceN\tMean\tSigma\tMinimum\tMaximum\n";
	for (const measure_info& m : m_info_list)
	{
		str += QString::asprintf("%u\t", m.id);
		str += QString::asprintf("%u\t", m.total_num);
		str += QString::asprintf("%.0f\t", m.ext_sum);
		str += QString::asprintf("%.2f\t", m.mean);
		str += QString::asprintf("%.2f\t", m.variance);
		str += QString::asprintf("%.2f\t", m.min);
		str += QString::asprintf("%.2f\n", m.max);
	}
	return str;
}

double TraceDialog::GetExt(unsigned int* data_label, unsigned long long index, unsigned int id,
	int nx, int ny, int nz, int i, int j, int k)
{
	bool surface_vox, contact_vox;
	unsigned long long indexn;
	const unsigned long long nxy = (unsigned long long)nx * ny;
	if (i == 0 || i == nx - 1 || j == 0 || j == ny - 1 || k == 0 || k == nz - 1)
	{
		//border voxel
		surface_vox = true;
		contact_vox = false;
		if (i > 0) { indexn = index - 1; if (data_label[indexn] && data_label[indexn] != id) contact_vox = true; }
		if (!contact_vox && i < nx - 1) { indexn = index + 1; if (data_label[indexn] && data_label[indexn] != id) contact_vox = true; }
		if (!contact_vox && j > 0) { indexn = index - nx; if (data_label[indexn] && data_label[indexn] != id) contact_vox = true; }
		if (!contact_vox && j < ny - 1) { indexn = index + nx; if (data_label[indexn] && data_label[indexn] != id) contact_vox = true; }
		if (!contact_vox && k > 0) { indexn = index - nxy; if (data_label[indexn] && data_label[indexn] != id) contact_vox = true; }
		if (!contact_vox && k < nz - 1) { indexn = index + nxy; if (data_label[indexn] && data_label[indexn] != id) contact_vox = true; }
	}
	else
	{
		surface_vox = false;
		contact_vox = false;
		const unsigned long long nbr[6] = { index - 1, index + 1, index - nx, index + nx, index - nxy, index + nxy };
		for (int n = 0; n < 6; n++)
		{
			if (n > 0 && surface_vox && contact_vox) break;
			indexn = nbr[n];
			if (data_label[indexn] == 0)
				surface_vox = true;
			if (data_label[indexn] && data_label[indexn] != id)
				surface_vox = contact_vox = true;
		}
	}
	(void)contact_vox;
	return surface_vox ? 1.0 : 0.0;
}

unsigned int TraceDialog::GetMappedID(unsigned int id, unsigned int* data_label1,
	unsigned int* data_label2, unsigned long long size)
{
	for (unsigned long long i = 0; i < size; ++i)
		if (data_label1[i] == id)
			return data_label2[i];
	return 0;
}

void TraceDialog::OnCellPrev()
{
	if (m_hooks.frame_step) m_hooks.frame_step(-1);   // wx VMovieView::DownFrame
}

void TraceDialog::OnCellNext()
{
	if (m_hooks.frame_step) m_hooks.frame_step(1);    // wx VMovieView::UpFrame
}

// ---------------------------------------------------------------------------
// automatic tracking (wx GenMap / RefineMap)
// ---------------------------------------------------------------------------

void TraceDialog::GenMap()
{
	if (!m_view)
		return;
	VolumeData* vd = CurVolume();
	if (!vd)
		return;
	BaseReader* reader = vd->GetReader();
	if (!reader)
		return;
	LBLReader lbl_reader;
	BaseReader* lr = &lbl_reader;
	m_view->CreateTraceGroup();
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (!trace_group)
		return;
	StatSet("Generating track map.\n");
	Yield();
	FL::TrackMap& track_map = trace_group->GetTrackMap();
	FL::TrackMapProcessor tm_processor;
	int chan = vd->GetCurChannel();
	int frames = reader->GetTimeNum();
	int resx, resy, resz;
	vd->GetResolution(resx, resy, resz);
	std::shared_ptr<VL_Nrrd> nrrd_data1, nrrd_data2, nrrd_label1, nrrd_label2;
	bool file_err = false;
	size_t iter_num = (size_t)m_gen_map_spin->value();
	iter_num *= 2;
	tm_processor.SetSizes(track_map, resx, resy, resz);
	float prog_bit = 100.0f / float(frames * (2 + iter_num));
	float prog = 0.0f;
	m_gen_map_prg->setValue(int(prog));
	for (int i = 0; i < frames; ++i)
	{
		if (i == 0)
		{
			nrrd_data1 = reader->Convert(i, chan, true);
			if (!nrrd_data1 || !nrrd_data1->getNrrd())
			{
				file_err = true;
				continue;
			}
			std::wstring lblname = LabelName(reader->GetCurName(i, chan));
			lbl_reader.SetFile(lblname);
			nrrd_label1 = lr->Convert(i, chan, true);
			if (!nrrd_label1)
			{
				file_err = true;
				continue;
			}
			tm_processor.InitializeFrame(track_map, nrrd_data1->getNrrd()->data, nrrd_label1->getNrrd()->data, i);
		}
		else
		{
			nrrd_data2 = reader->Convert(i, chan, true);
			if (!nrrd_data2 || !nrrd_data2->getNrrd())
			{
				file_err = true;
				continue;
			}
			std::wstring lblname = LabelName(reader->GetCurName(i, chan));
			lbl_reader.SetFile(lblname);
			nrrd_label2 = lr->Convert(i, chan, true);
			if (!nrrd_label2)
			{
				file_err = true;
				continue;
			}
			tm_processor.InitializeFrame(track_map, nrrd_data2->getNrrd()->data, nrrd_label2->getNrrd()->data, i);
			//link maps 1 and 2
			tm_processor.LinkMaps(track_map, i - 1, i,
				nrrd_data1->getNrrd()->data, nrrd_data2->getNrrd()->data,
				nrrd_label1->getNrrd()->data, nrrd_label2->getNrrd()->data);
			nrrd_data1 = nrrd_data2;
			nrrd_label1 = nrrd_label2;
		}
		prog += prog_bit;
		m_gen_map_prg->setValue(int(prog));
		StatAppend(QString::asprintf("Time point %d initialized.\n", i));
		Yield();
	}
	if (file_err)
		StatAppend("ERROR! Certain file(s) missing. Check if label files exist.\n");
	//resolve multiple links of single vertex
	for (size_t fi = 0; fi < track_map.GetFrameNum(); ++fi)
	{
		tm_processor.ResolveGraph(track_map, fi, fi + 1);
		tm_processor.ResolveGraph(track_map, fi, fi - 1);
		prog += prog_bit;
		m_gen_map_prg->setValue(int(prog));
		StatAppend(QString::asprintf("Time point %d resolved.\n", int(fi)));
		Yield();
	}
	const auto t1 = std::chrono::high_resolution_clock::now();
	//check branch
	for (size_t iteri = 0; iteri < iter_num; ++iteri)
	{
		for (size_t fi = 0; fi < track_map.GetFrameNum(); ++fi)
		{
			tm_processor.MatchFrames(track_map, fi, fi + 1, iteri != 0);
			tm_processor.MatchFrames(track_map, fi, fi - 1, iteri != 0);
			prog += prog_bit;
			m_gen_map_prg->setValue(int(prog));
			StatAppend(QString::asprintf("Time point %d linked.\n", int(fi)));
			Yield();
		}
		if (++iteri >= iter_num)
			break;
		for (size_t fi = 0; fi < track_map.GetFrameNum(); ++fi)
		{
			tm_processor.UnmatchFrames(track_map, fi, fi - 1);
			tm_processor.UnmatchFrames(track_map, fi, fi + 1);
			//link orphans
			tm_processor.ExMatchFrames(track_map, fi, fi + 1);
			tm_processor.ExMatchFrames(track_map, fi, fi - 1);
			prog += prog_bit;
			m_gen_map_prg->setValue(int(prog));
			StatAppend(QString::asprintf("Time point %d unlinked.\n", int(fi)));
			Yield();
		}
	}
	const auto t2 = std::chrono::high_resolution_clock::now();
	const double secs = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1).count();
	StatAppend(QString::asprintf("Wall clock time: %.4fs\n", secs));
	m_gen_map_prg->setValue(100);
	GetSettings(m_view);
}

void TraceDialog::RefineMap(int t)
{
	if (!m_view)
		return;
	VolumeData* vd = CurVolume();
	if (!vd)
		return;
	TraceGroup* trace_group = m_view->GetTraceGroup();
	if (!trace_group)
		return;
	if (t < 0)
		StatSet("Refining track map for all time points.\n");
	else
		StatSet(QString::asprintf("Refining track map at time point %d.\n", t));
	Yield();
	FL::TrackMap& track_map = trace_group->GetTrackMap();
	FL::TrackMapProcessor tm_processor;
	int start_frame, end_frame;
	if (t < 0)
	{
		start_frame = 0;
		end_frame = (int)track_map.GetFrameNum() - 1;
	}
	else
		start_frame = end_frame = t;
	int resx, resy, resz;
	vd->GetResolution(resx, resy, resz);
	size_t iter_num = (size_t)m_gen_map_spin->value();
	iter_num *= 2;
	tm_processor.SetSizes(track_map, resx, resy, resz);
	float prog_bit = 100.0f / float((end_frame - start_frame + 1) * iter_num);
	float prog = 0.0f;
	m_gen_map_prg->setValue(int(prog));
	unsigned int last_op = track_map.GetLastOp();
	auto link_frames = [&]() {
		for (size_t fi = start_frame; fi <= (size_t)end_frame; ++fi)
		{
			tm_processor.MatchFrames(track_map, fi, fi + 1, false);
			tm_processor.MatchFrames(track_map, fi, fi - 1, false);
			prog += prog_bit;
			m_gen_map_prg->setValue(int(prog));
			StatAppend(QString::asprintf("Time point %d linked.\n", int(fi)));
			Yield();
		}
	};
	auto unlink_frames = [&]() {
		for (size_t fi = start_frame; fi <= (size_t)end_frame; ++fi)
		{
			tm_processor.UnmatchFrames(track_map, fi, fi - 1);
			tm_processor.UnmatchFrames(track_map, fi, fi + 1);
			tm_processor.ExMatchFrames(track_map, fi, fi + 1);
			tm_processor.ExMatchFrames(track_map, fi, fi - 1);
			prog += prog_bit;
			m_gen_map_prg->setValue(int(prog));
			StatAppend(QString::asprintf("Time point %d unlinked.\n", int(fi)));
			Yield();
		}
	};
	const auto t1 = std::chrono::high_resolution_clock::now();
	//check branch
	for (size_t iteri = 0; iteri < iter_num; ++iteri)
	{
		if (last_op == 1) unlink_frames(); else link_frames();
		if (++iteri >= iter_num)
			break;
		if (last_op == 1) link_frames(); else unlink_frames();
	}
	const auto t2 = std::chrono::high_resolution_clock::now();
	const double secs = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1).count();
	StatAppend(QString::asprintf("Wall clock time: %.4fs\n", secs));
	m_gen_map_prg->setValue(100);
}
