// Port of MeasureDlg / RulerListCtrl (MeasureDlg.cpp). See MeasureDialog.h.

#include "MeasureDialog.h"
#include "RulerTool.h"
#include "RenderCanvas.h"
#include "AnalysisContext.h"
#include "SettingsDialog.h"
#include "RigidCellsReportDialog.h"
#include "RigidCellsWarp.h"

#include <QToolBar>
#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLineEdit>
#include <QSlider>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QFrame>
#include <QTreeWidget>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QIntValidator>
#include <QSignalBlocker>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QColorDialog>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPixmap>
#include <QIcon>
#include <climits>
#include <algorithm>

#include "DataManager.h"
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

	inline QString S2Q(const std::wstring& s) { return QString::fromStdWString(s); }

}

// list columns: MeasureDialog::Column (MeasureDialog.h)
enum
{
	ColName = MeasureDialog::ColID, ColStartVol = MeasureDialog::ColStartVol, ColEndVol = MeasureDialog::ColEndVol,
	ColColor = MeasureDialog::ColColor, ColLength = MeasureDialog::ColLength, ColAngle = MeasureDialog::ColAngle,
	ColPoints = MeasureDialog::ColPoints, ColTime = MeasureDialog::ColTime, ColInfo = MeasureDialog::ColInfo,
	ColNum = MeasureDialog::ColNum
};

// ---------------------------------------------------------------------------
// RulerListWidget: the wx RulerListCtrl's key / drag handling
// ---------------------------------------------------------------------------

class RulerListWidget : public QTreeWidget
{
public:
	explicit RulerListWidget(MeasureDialog* dlg) : QTreeWidget(dlg), m_dlg(dlg) {}

protected:
	void keyPressEvent(QKeyEvent* e) override
	{
		// wx OnKeyDown: WXK_DELETE / WXK_BACK -> DeleteSelection
		if (state() != QAbstractItemView::EditingState &&
			(e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace))
		{
			m_dlg->DeleteSelection();
			return;
		}
		QTreeWidget::keyPressEvent(e);
	}
	void mousePressEvent(QMouseEvent* e) override
	{
		QTreeWidget::mousePressEvent(e);
		m_dragging_row = -1;
		m_press_pos = e->position().toPoint();
		if (e->button() == Qt::LeftButton)
			if (QTreeWidgetItem* it = itemAt(m_press_pos))
				m_dragging_row = indexOfTopLevelItem(it);
		m_drag_started = false;
	}
	void mouseMoveEvent(QMouseEvent* e) override
	{
		// wx OnBeginDrag / OnDragging: the dragged ruler swaps places with
		// the ruler under the mouse
		if (m_dragging_row >= 0 && (e->buttons() & Qt::LeftButton) &&
			state() != QAbstractItemView::EditingState)
		{
			QPoint pos = e->position().toPoint();
			if (!m_drag_started && (pos - m_press_pos).manhattanLength() < 4)
				return;
			if (!m_drag_started)
			{
				m_drag_started = true;
				setCursor(Qt::WaitCursor);
			}
			if (QTreeWidgetItem* it = itemAt(pos))
			{
				int index = indexOfTopLevelItem(it);
				if (index >= 0 && index != m_dragging_row)
				{
					m_dlg->DragTo(m_dragging_row, index);
					m_dragging_row = index;
				}
			}
			return;
		}
		QTreeWidget::mouseMoveEvent(e);
	}
	void mouseReleaseEvent(QMouseEvent* e) override
	{
		if (m_drag_started)
			unsetCursor();
		m_dragging_row = -1;
		m_drag_started = false;
		QTreeWidget::mouseReleaseEvent(e);
	}

private:
	MeasureDialog* m_dlg;
	int m_dragging_row = -1;
	bool m_drag_started = false;
	QPoint m_press_pos;
};

// ---------------------------------------------------------------------------
// MeasureDialog
// ---------------------------------------------------------------------------

MeasureDialog::MeasureDialog(RulerTool* tool, AnalysisContext* ctx, SettingsDialog* settings, QWidget* parent)
	: QDialog(parent), m_tool(tool), m_ctx(ctx), m_settings(settings)
{
	setWindowTitle("Measurement");

	//toolbar
	m_toolbar = new QToolBar(this);
	m_toolbar->setIconSize(QSize(24, 24));
	m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	m_locator_act = m_toolbar->addAction(VVD_ICON(listicon_locator_24x24), "Locator");
	m_locator_act->setCheckable(true);
	m_locator_act->setToolTip("Add locators to the render view by clicking");
	m_ruler_act = m_toolbar->addAction(VVD_ICON(listicon_line_24x24), "2pt Ruler");
	m_ruler_act->setCheckable(true);
	m_ruler_act->setToolTip("Add rulers to the render view by clicking two end points");
	m_ruler_mp_act = m_toolbar->addAction(VVD_ICON(listicon_polyline_24x24), "2+pt Ruler");
	m_ruler_mp_act->setCheckable(true);
	m_ruler_mp_act->setToolTip("Add a polyline ruler to the render view by clicking its points");
	m_ruler_edit_act = m_toolbar->addAction(VVD_ICON(listicon_edit_24x24), "Edit");
	m_ruler_edit_act->setCheckable(true);
	m_ruler_edit_act->setToolTip("Select and move ruler points");
	QAction* del_act = m_toolbar->addAction(VVD_ICON(listicon_delete), "Delete");
	del_act->setToolTip("Delete a selected ruler");
	QAction* delall_act = m_toolbar->addAction(VVD_ICON(listicon_delall), "Delete All");
	delall_act->setToolTip("Delete all rulers");
	QAction* export_act = m_toolbar->addAction(VVD_ICON(listicon_save), "Export");
	export_act->setToolTip("Export rulers to a text file");
	QAction* import_act = m_toolbar->addAction(VVD_ICON(plus), "Import");
	import_act->setToolTip("Import rulers from a BigWarp csv");
	connect(m_locator_act, &QAction::triggered, this, &MeasureDialog::OnNewLocator);
	connect(m_ruler_act, &QAction::triggered, this, &MeasureDialog::OnNewRuler);
	connect(m_ruler_mp_act, &QAction::triggered, this, &MeasureDialog::OnNewRulerMP);
	connect(m_ruler_edit_act, &QAction::triggered, this, &MeasureDialog::OnRulerEdit);
	connect(del_act, &QAction::triggered, this, &MeasureDialog::OnDelete);
	connect(delall_act, &QAction::triggered, this, &MeasureDialog::OnDeleteAll);
	connect(export_act, &QAction::triggered, this, &MeasureDialog::OnExport);
	connect(import_act, &QAction::triggered, this, &MeasureDialog::OnImport);

	auto* st_line = new QFrame(this);
	st_line->setFrameShape(QFrame::HLine);
	st_line->setFrameShadow(QFrame::Sunken);

	//options
	auto* st = new QLabel("Pointing Method:", this);
	st->setAlignment(Qt::AlignCenter);
#ifdef __APPLE__
	st->setFixedWidth(110);
#else
	st->setFixedWidth(100);
#endif
	auto* sizer_1 = new QHBoxLayout();
	m_int_method_combo = new QComboBox(this);
	m_int_method_combo->addItem("View Plane");
	m_int_method_combo->addItem("Maximum Intensity");
	m_int_method_combo->addItem("Accumulated Intensity");
	m_int_method_combo->setCurrentIndex(0);
	m_int_method_combo->setMinimumWidth(162);
	sizer_1->addWidget(st, 0, Qt::AlignVCenter);
	sizer_1->addSpacing(10);
	sizer_1->addWidget(m_int_method_combo, 0, Qt::AlignVCenter);
	//not in wx: pick the volume under the click by its maximum intensity
	m_auto_select_chk = new QCheckBox("Auto-select", this);
	m_auto_select_chk->setObjectName("ruler_auto_select");
	m_auto_select_chk->setToolTip("While creating or editing rulers (all ruler types, also painting in ruler mode), "
		"each click first finds the displayed volume (any view, any shown group) with the brightest voxel along the "
		"click ray, compared as the normalized value after the volume's transfer function and clipping (what you see), "
		"selects it in the Workspace tree (it becomes the current volume) and places the point on it with the Pointing "
		"Method (View Plane: on the view plane). The point's Start / End Volume is that volume. "
		"Where no volume is hit, the point is placed as without Auto-select.");
	sizer_1->addSpacing(10);
	sizer_1->addWidget(m_auto_select_chk, 0, Qt::AlignVCenter);
	sizer_1->addStretch(1);
	connect(m_auto_select_chk, &QCheckBox::toggled, this, &MeasureDialog::OnAutoSelectCheck);
	connect(m_int_method_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MeasureDialog::OnIntensityMethodsCombo);

	//more options
	auto* sizer_2 = new QHBoxLayout();
	m_transient_chk = new QCheckBox("Transient", this);
	m_use_transfer_chk = new QCheckBox("Use Volume Properties", this);
	m_scatter_btn = new QPushButton("Scatter Rulers", this);
	m_scatter_btn->setMinimumWidth(120);
	auto* st2 = new QLabel("Density:", this);
	st2->setAlignment(Qt::AlignCenter);
	m_density_txt = new QLineEdit("1000", this);
	m_density_txt->setValidator(new QIntValidator(0, INT_MAX, m_density_txt));
	m_density_txt->setFixedWidth(60);
	sizer_2->addSpacing(10);
	sizer_2->addWidget(m_transient_chk, 0, Qt::AlignVCenter);
	sizer_2->addSpacing(10);
	sizer_2->addWidget(m_use_transfer_chk, 0, Qt::AlignVCenter);
	sizer_2->addSpacing(10);
	sizer_2->addWidget(m_scatter_btn, 0, Qt::AlignVCenter);
	sizer_2->addSpacing(2);
	sizer_2->addWidget(st2, 0, Qt::AlignVCenter);
	sizer_2->addSpacing(1);
	sizer_2->addWidget(m_density_txt, 0, Qt::AlignVCenter);
	sizer_2->addStretch(1);
	connect(m_transient_chk, &QCheckBox::toggled, this, &MeasureDialog::OnTransientCheck);
	connect(m_use_transfer_chk, &QCheckBox::toggled, this, &MeasureDialog::OnUseTransferCheck);
	connect(m_scatter_btn, &QPushButton::clicked, this, &MeasureDialog::OnScatterRulers);

	m_warp_btn = new QPushButton("Apply Transform", this);
	m_warp_btn->setMinimumWidth(120);
	m_warp_lambda = 0.0;
	//transform model selector (matches BigWarp's transform types)
	m_warp_type_combo = new QComboBox(this);
	m_warp_type_combo->addItems({ "Thin Plate Spline", "Affine", "Similarity", "Rigid", "Translation", "Cells: shape preserving" });
	m_warp_type_combo->setItemData(WarpCells, "Shape-preserving warp of a label / binary volume (ImageJ plugin "
		"\"Apply BigWarp to Cells\"): every object keeps its shape and label value", Qt::ToolTipRole);
	m_warp_type_combo->setCurrentIndex(0);
	m_warp_type_combo->setMinimumWidth(140);
	//warp result interpolation (NN preserves exact voxel values, e.g. label volumes)
	m_warp_interp_combo = new QComboBox(this);
	m_warp_interp_combo->addItems({ "Nearest Neighbor", "Linear" });
	m_warp_interp_combo->setCurrentIndex(0);
	m_warp_interp_combo->setMinimumWidth(140);
	auto* st3 = new QLabel("Stiffness:", this);
	st3->setAlignment(Qt::AlignCenter);
	m_warp_stiffness_sldr = new QSlider(Qt::Horizontal, this);
	m_warp_stiffness_sldr->setRange(0, 300);
	m_warp_stiffness_sldr->setValue(0);
	m_warp_stiffness_sldr->setMinimumWidth(100);
	m_warp_stiffness_sldr->setToolTip("Thin Plate Spline stiffness (dimensionless, same as the ImageJ "
		"\"Apply BigWarp with Stiffness\" and \"Apply BigWarp to Cells\" plugins: lambda = stiffness x mean "
		"squared landmark distance from the landmarks' centroid). 0 = exact interpolation, as BigWarp.");
	m_warp_stiffness_text = new QLabel("0.000", this);
	m_warp_stiffness_text->setAlignment(Qt::AlignCenter);
	m_warp_stiffness_text->setFixedWidth(40);
	connect(m_warp_btn, &QPushButton::clicked, this, &MeasureDialog::OnWarp);
	connect(m_warp_type_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MeasureDialog::OnWarpType);
	connect(m_warp_stiffness_sldr, &QSlider::valueChanged, this, &MeasureDialog::OnWarpStiffness);

	//warp / registration row (BigWarp-style transforms on its own line)
	auto* sizer_3 = new QHBoxLayout();
	auto* st4 = new QLabel("Transform:", this);
	st4->setAlignment(Qt::AlignCenter);
	auto* st5 = new QLabel("Interpolation:", this);
	st5->setAlignment(Qt::AlignCenter);
	sizer_3->addSpacing(10);
	sizer_3->addWidget(st4, 0, Qt::AlignVCenter);
	sizer_3->addSpacing(5);
	sizer_3->addWidget(m_warp_type_combo, 0, Qt::AlignVCenter);
	sizer_3->addSpacing(5);
	sizer_3->addWidget(st5, 0, Qt::AlignVCenter);
	sizer_3->addSpacing(5);
	sizer_3->addWidget(m_warp_interp_combo, 0, Qt::AlignVCenter);
	sizer_3->addStretch(1);

	//stiffness row (slider stretches to full width)
	auto* sizer_3s = new QHBoxLayout();
	sizer_3s->addSpacing(10);
	sizer_3s->addWidget(st3, 0, Qt::AlignVCenter);
	sizer_3s->addSpacing(1);
	sizer_3s->addWidget(m_warp_stiffness_sldr, 1, Qt::AlignVCenter);
	sizer_3s->addSpacing(1);
	sizer_3s->addWidget(m_warp_stiffness_text, 0, Qt::AlignVCenter);
	sizer_3s->addSpacing(10);

	//warp apply row (Apply Transform button aligned to the right)
	auto* sizer_4 = new QHBoxLayout();
	sizer_4->addStretch(1);
	sizer_4->addWidget(m_warp_btn, 0, Qt::AlignVCenter);
	sizer_4->addSpacing(10);

	//list
	m_rulerlist = new RulerListWidget(this);
	m_rulerlist->setColumnCount(ColNum);
	m_rulerlist->setHeaderLabels({ "ID", "Start Volume", "End Volume", "Color", "Length", "Angle/Pitch",
		"Start/End Points (X, Y, Z)", "Time", "Volumes/Score" });
	m_rulerlist->setRootIsDecorated(false);
	m_rulerlist->setUniformRowHeights(true);
	m_rulerlist->setSelectionMode(QAbstractItemView::SingleSelection);
	m_rulerlist->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_rulerlist->header()->setStretchLastSection(false);
	m_rulerlist->header()->setSectionsMovable(false);
	m_rulerlist->setColumnWidth(ColName, 50);   // half the former Name column's 100
	for (int c = ColName + 1; c < ColNum; ++c)
		m_rulerlist->resizeColumnToContents(c);
	connect(m_rulerlist, &QTreeWidget::itemDoubleClicked, this, &MeasureDialog::OnItemDoubleClicked);
	connect(m_rulerlist, &QTreeWidget::itemChanged, this, &MeasureDialog::OnItemChanged);

	//sizer
	auto* sizerV = new QVBoxLayout(this);
	sizerV->setContentsMargins(0, 0, 0, 0);
	sizerV->setSpacing(0);
	sizerV->addWidget(m_toolbar);
	sizerV->addWidget(st_line);
	sizerV->addSpacing(10);
	sizerV->addLayout(sizer_1);
	sizerV->addSpacing(10);
	sizerV->addLayout(sizer_2);
	sizerV->addSpacing(10);
	sizerV->addLayout(sizer_3);
	sizerV->addSpacing(10);
	sizerV->addLayout(sizer_3s);
	sizerV->addSpacing(10);
	BuildCellPanel();
	sizerV->addWidget(m_cell_panel);
	sizerV->addLayout(sizer_4);
	sizerV->addSpacing(10);
	sizerV->addWidget(m_rulerlist, 1);

	resize(700, 500);   // wx pane FloatingSize(700, 500)

	if (m_tool)
	{
		connect(m_tool, &RulerTool::rulersChanged, this, &MeasureDialog::GetSettings);
		connect(m_tool, &RulerTool::intModeChanged, this, &MeasureDialog::GetSettings);
	}
	GetSettings();
	OnWarpType(m_warp_type_combo->currentIndex());
}

MeasureDialog::~MeasureDialog() = default;

void MeasureDialog::SetTool(RulerTool* tool)
{
	if (tool == m_tool) return;
	if (m_tool) disconnect(m_tool, nullptr, this, nullptr);
	m_tool = tool;
	//Auto-select is one option for every view (Settings)
	if (m_tool && m_settings)
		m_tool->SetRulerAutoSelect(m_settings->GetRulerAutoSelect());
	if (m_tool)
	{
		connect(m_tool, &RulerTool::rulersChanged, this, &MeasureDialog::GetSettings);
		connect(m_tool, &RulerTool::intModeChanged, this, &MeasureDialog::GetSettings);
	}
	GetSettings();
}

void MeasureDialog::showEvent(QShowEvent* e)
{
	QDialog::showEvent(e);
	GetSettings();
}

int MeasureDialog::GetWarpTransformType() const
{
	return m_warp_type_combo ? m_warp_type_combo->currentIndex() : 0;
}

int MeasureDialog::GetWarpInterpolation() const
{
	return m_warp_interp_combo ? m_warp_interp_combo->currentIndex() : 0;
}

void MeasureDialog::GetSettings()
{
	UpdateList();
	if (!m_tool)
		return;

	QSignalBlocker b1(m_locator_act), b2(m_ruler_act), b3(m_ruler_mp_act), b4(m_ruler_edit_act);
	m_locator_act->setChecked(false);
	m_ruler_act->setChecked(false);
	m_ruler_mp_act->setChecked(false);
	m_ruler_edit_act->setChecked(false);

	int int_mode = m_tool->GetIntMode();
	if (int_mode == 5 || int_mode == 7)
	{
		int ruler_type = m_tool->GetRulerType();
		if (ruler_type == 0)
			m_ruler_act->setChecked(true);
		else if (ruler_type == 1)
			m_ruler_mp_act->setChecked(true);
		else if (ruler_type == 2)
			m_locator_act->setChecked(true);
	}
	else if (int_mode == 6)
		m_ruler_edit_act->setChecked(true);

	QSignalBlocker b5(m_int_method_combo), b6(m_use_transfer_chk), b7(m_transient_chk), b8(m_auto_select_chk);
	m_int_method_combo->setCurrentIndex(m_tool->GetPointVolumeMode());
	m_auto_select_chk->setChecked(m_tool->GetRulerAutoSelect());
	m_use_transfer_chk->setChecked(m_tool->GetRulerUseTransf());
	m_transient_chk->setChecked(m_tool->GetRulerTimeDep());
}

QString MeasureDialog::UnitText() const
{
	switch (m_ctx ? m_ctx->GetScaleBarUnit() : 1)
	{
	case 0:
		return "nm";
	case 1:
	default:
		return QString::fromWCharArray(L"μm");
	case 2:
		return "mm";
	}
}

Ruler* MeasureDialog::RulerOf(QTreeWidgetItem* item) const
{
	if (!item || !m_tool) return nullptr;
	Ruler* r = reinterpret_cast<Ruler*>(item->data(ColName, Qt::UserRole).value<quintptr>());
	std::vector<Ruler*>* list = m_tool->GetRulerList();
	if (std::find(list->begin(), list->end(), r) == list->end())
		return nullptr;
	return r;
}

// RulerListCtrl::OnGetItemText for one ruler row
void MeasureDialog::FillItem(QTreeWidgetItem* item, Ruler* ruler)
{
	item->setData(ColName, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(ruler)));
	item->setText(ColName, S2Q(ruler->GetNameDisp()));
	if (ruler->GetUseColor())
		item->setText(ColColor, QString::asprintf("RGB(%d, %d, %d)",
			int(ruler->GetColor().r() * 255),
			int(ruler->GetColor().g() * 255),
			int(ruler->GetColor().b() * 255)));
	else
		item->setText(ColColor, QString());
	item->setText(ColLength, QString::asprintf("%.2f", ruler->GetLength()) + UnitText());
	item->setText(ColAngle, QString::asprintf("%.1f", ruler->GetAngle()) + "Deg");
	QString points;
	if (ruler->GetNumPoint() > 0)
	{
		Point* p = ruler->GetPoint(0);
		points += QString::asprintf("(%.2f, %.2f, %.2f)", p->x(), p->y(), -p->z());
	}
	if (ruler->GetNumPoint() > 1)
	{
		Point* p = ruler->GetPoint(ruler->GetNumPoint() - 1);
		points += ", ";
		points += QString::asprintf("(%.2f, %.2f, %.2f)", p->x(), p->y(), -p->z());
	}
	item->setText(ColPoints, points);
	// same points as the Start/End column: the first and (if any) the last
	item->setText(ColStartVol, ruler->GetNumPoint() > 0 ? S2Q(ruler->GetPointVolName(0)) : QString());
	item->setText(ColEndVol, ruler->GetNumPoint() > 1 ?
		S2Q(ruler->GetPointVolName(ruler->GetNumPoint() - 1)) : QString());
	if (ruler->GetTimeDep())
		item->setText(ColTime, QString::number(ruler->GetTime()));
	else
		item->setText(ColTime, "N/A");
	item->setText(ColInfo, S2Q(ruler->GetDelInfoValues(L", ")));
	item->setFlags(item->flags() | Qt::ItemIsEditable);
}

// RulerListCtrl::UpdateRulers (rulers of the current time point)
void MeasureDialog::UpdateList()
{
	if (!m_tool) return;
	m_updating = true;
	Ruler* sel = RulerOf(m_rulerlist->currentItem());
	m_rulerlist->clear();

	std::vector<Ruler*>* ruler_list = m_tool->GetRulerList();
	int tseq = m_tool->GetCurTime();
	QTreeWidgetItem* sel_item = nullptr;
	for (Ruler* ruler : *ruler_list)
	{
		if (!ruler) continue;
		if (ruler->GetTimeDep() &&
			ruler->GetTime() != tseq)
			continue;
		auto* item = new QTreeWidgetItem(m_rulerlist);
		FillItem(item, ruler);
		if (ruler == sel) sel_item = item;
	}
	// wx m_counts[0]: the rulers of the current time point
	m_counts.clear();
	m_counts.push_back(m_rulerlist->topLevelItemCount());
	// wx m_show_anno (always on): the annotation rows after the rulers
	// (RulerListCtrl::UpdateRulers counts, OnGetItemText's annotation branch)
	CountAnnotations();
	if (RenderCanvas* view = m_tool->Canvas())
	{
		QList<QTreeWidgetItem*> rows;
		int cid = 1;
		for (int i = 0; i < view->GetLayerNum(); i++)
		{
			TreeLayer* layer = view->GetLayer(i);
			if (!layer || layer->IsA() != 4) continue;
			Annotations* ann = (Annotations*)layer;
			if (!ann->GetDisp()) continue;
			if (cid >= (int)m_counts.size()) break;
			const long n = m_counts[cid];
			const QString aname = S2Q(ann->GetName());
			for (long id = 0; id < n; id++)
			{
				auto* item = new QTreeWidgetItem();
				const QString name = QString::fromStdString(ann->GetTextText((int)id)) + "  (" + aname + ")";
				item->setText(ColName, name);
				Point p = ann->GetTextPos((int)id);
				int resx = 1, resy = 1, resz = 1;
				if (VolumeData* vd = ann->GetVolume())
					vd->GetResolution(resx, resy, resz);
				item->setText(ColPoints, QString::asprintf("(%.2f, %.2f, %.2f)", p.x() * resx, p.y() * resy, p.z() * resz));
				// wx: the first tab-separated field of the info (score, or
				// voxel count + " voxels" without a mesh)
				const QString info = QString::fromStdString(ann->GetTextInfo((int)id));
				const QStringList tok = info.split('\t', Qt::SkipEmptyParts);
				if (!tok.isEmpty())
					item->setText(ColInfo, ann->GetMesh() ? tok.first() : tok.first() + " voxels");
				rows << item;
			}
			cid++;
		}
		m_rulerlist->addTopLevelItems(rows);
	}
	if (m_rulerlist->topLevelItemCount() > 0)
	{
		for (int c = ColName + 1; c < ColNum; ++c)
			m_rulerlist->resizeColumnToContents(c);
	}
	if (sel_item)
	{
		QSignalBlocker b(m_rulerlist);
		m_rulerlist->setCurrentItem(sel_item);
	}
	m_updating = false;
}

// RulerListCtrl::UpdateRulers, annotation part: one count per shown
// annotations layer; for a mesh annotation the texts whose first info field
// (score) is above the Min Score threshold (the texts are sorted by score;
// lists over 10000 are first bracketed in steps of 10000, as wx)
void MeasureDialog::CountAnnotations()
{
	RenderCanvas* view = m_tool ? m_tool->Canvas() : nullptr;
	if (!view) return;
	auto score_of = [](Annotations* ann, int j) {
		const QString info = QString::fromStdString(ann->GetTextInfo(j));
		const QStringList tok = info.split('\t', Qt::SkipEmptyParts);
		return tok.isEmpty() ? 0.0 : tok.first().toDouble();   // wx STOD: 0 on garbage
	};
	for (int i = 0; i < view->GetLayerNum(); i++)
	{
		TreeLayer* layer = view->GetLayer(i);
		if (!layer || layer->IsA() != 4) continue;
		Annotations* ann = (Annotations*)layer;
		if (!ann->GetDisp()) continue;
		const int tnum = ann->GetTextNum();
		long count = 0;
		if (ann->GetMesh())
		{
			int st = 0;
			int ed = tnum;
			if (tnum > 10000)
			{
				for (int j = 0; j < tnum; j += 10000)
				{
					if (ann->GetThreshold() < score_of(ann, j))
						st = j;
					else
					{
						ed = j;
						break;
					}
				}
			}
			count = st;
			for (int j = st; j < ed; j++)
			{
				if (ann->GetThreshold() < score_of(ann, j))
					count++;
				else
					break;
			}
		}
		else
			count = tnum;
		m_counts.push_back(count);
	}
}

// RulerListCtrl::GetCount
long MeasureDialog::GetCount(Annotations* ann) const
{
	if (m_counts.empty()) return 0;
	if (!ann) return m_counts[0];
	RenderCanvas* view = m_tool ? m_tool->Canvas() : nullptr;
	if (!view) return 0;
	size_t id = 1;
	for (int i = 0; i < view->GetLayerNum(); i++)
	{
		TreeLayer* layer = view->GetLayer(i);
		if (!layer || layer->IsA() != 4) continue;
		Annotations* annotations = (Annotations*)layer;
		if (!annotations->GetDisp()) continue;
		if (ann == annotations && id < m_counts.size())
			return m_counts[id];
		id++;
	}
	return 0;
}

int MeasureDialog::RowCount() const
{
	return m_rulerlist->topLevelItemCount();
}

QString MeasureDialog::RowText(int row, int col) const
{
	QTreeWidgetItem* it = m_rulerlist->topLevelItem(row);
	return it ? it->text(col) : QString();
}

// wx OnAct / OnLeftDClick -> ShowTextCtrls: name text box (and colour
// picker). Here: edit the name in place, or pick the colour.
void MeasureDialog::OnItemDoubleClicked(QTreeWidgetItem* item, int column)
{
	Ruler* ruler = RulerOf(item);
	if (!ruler) return;
	if (column == ColColor)
	{
		// wx OnColorChange
		QColor init = Qt::black;
		if (ruler->GetUseColor())
		{
			Color c = ruler->GetColor();
			init = QColor(int(c.r() * 255.0), int(c.g() * 255.0), int(c.b() * 255.0));
		}
		QColor qc = QColorDialog::getColor(init, this, "Ruler Color");
		if (!qc.isValid()) return;
		Color color(qc.red() / 255.0, qc.green() / 255.0, qc.blue() / 255.0);
		ruler->SetColor(color);
		m_updating = true;
		item->setText(ColColor, QString::asprintf("RGB(%d, %d, %d)",
			int(color.r() * 255), int(color.g() * 255), int(color.b() * 255)));
		m_updating = false;
		if (m_tool->Canvas()) m_tool->Canvas()->RefreshView();
		return;
	}
	m_rulerlist->editItem(item, ColName);
}

// wx EndEdit: name_disp <- text box
void MeasureDialog::OnItemChanged(QTreeWidgetItem* item, int column)
{
	if (m_updating || column != ColName) return;
	Ruler* ruler = RulerOf(item);
	if (!ruler) return;
	ruler->SetNameDisp(vvd::String(item->text(ColName).toStdWString()));
	if (m_tool->Canvas()) m_tool->Canvas()->RefreshView();
}

// wx OnDragging: swap the dragged ruler with the one under the mouse
void MeasureDialog::DragTo(int from_row, int to_row)
{
	Ruler* a = RulerOf(m_rulerlist->topLevelItem(from_row));
	Ruler* b = RulerOf(m_rulerlist->topLevelItem(to_row));
	if (!a || !b || a == b) return;
	std::vector<Ruler*>* list = m_tool->GetRulerList();
	auto ia = std::find(list->begin(), list->end(), a);
	auto ib = std::find(list->begin(), list->end(), b);
	if (ia == list->end() || ib == list->end()) return;
	std::iter_swap(ia, ib);
	m_updating = true;
	FillItem(m_rulerlist->topLevelItem(from_row), b);
	FillItem(m_rulerlist->topLevelItem(to_row), a);
	m_updating = false;
	QSignalBlocker blk(m_rulerlist);
	m_rulerlist->setCurrentItem(m_rulerlist->topLevelItem(to_row));
}

// RulerListCtrl::DeleteSelection
void MeasureDialog::DeleteSelection()
{
	if (!m_tool) return;
	Ruler* ruler = RulerOf(m_rulerlist->currentItem());
	if (!ruler) return;
	std::vector<Ruler*>* ruler_list = m_tool->GetRulerList();
	auto it = std::find(ruler_list->begin(), ruler_list->end(), ruler);
	if (it == ruler_list->end()) return;
	ruler_list->erase(it);
	delete ruler;
	m_tool->RulersModified();   // UpdateRulers(); RefreshGL()
}

// RulerListCtrl::DeleteAll
void MeasureDialog::DeleteAll(bool cur_time)
{
	if (!m_tool) return;
	std::vector<Ruler*>* ruler_list = m_tool->GetRulerList();
	if (cur_time)
	{
		int tseq = m_tool->GetCurTime();
		for (int i = (int)ruler_list->size() - 1; i >= 0; i--)
		{
			Ruler* ruler = (*ruler_list)[i];
			if (ruler &&
				((ruler->GetTimeDep() &&
					ruler->GetTime() == tseq) ||
					!ruler->GetTimeDep()))
			{
				ruler_list->erase(ruler_list->begin() + i);
				delete ruler;
			}
		}
	}
	else
	{
		for (int i = (int)ruler_list->size() - 1; i >= 0; i--)
		{
			Ruler* ruler = (*ruler_list)[i];
			if (ruler)
				delete ruler;
		}
		ruler_list->clear();
	}
	m_tool->RulersModified();
}

// RulerListCtrl::Export (text file)
void MeasureDialog::Export(const QString& filename)
{
	if (!m_tool) return;
	std::vector<Ruler*>* ruler_list = m_tool->GetRulerList();

	QFile f(filename);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		return;
	QTextStream tos(&f);

	QString str;
	QString unit = UnitText();
	int num_points;
	Point* p;
	Ruler* ruler;

	tos << "Name\tColor\tLength(" << unit << ")\tAngle/Pitch(Deg)\tx1\ty1\tz1\txn\tyn\tzn\tTime\tvolumes/scores\tstart_volume\tend_volume\n";

	Color color;
	for (size_t i = 0; i < ruler_list->size(); i++)
	{
		ruler = (*ruler_list)[i];
		if (!ruler) continue;

		tos << S2Q(ruler->GetName()) << "\t";
		if (ruler->GetUseColor())
		{
			color = ruler->GetColor();
			str = QString::asprintf("RGB(%d, %d, %d)",
				int(color.r() * 255), int(color.g() * 255), int(color.b() * 255));
		}
		else
			str = "N/A";
		tos << str << "\t";
		str = QString::asprintf("%.2f", ruler->GetLength());
		tos << str << "\t";
		str = QString::asprintf("%.1f", ruler->GetAngle());
		tos << str << "\t";
		str = "";
		num_points = ruler->GetNumPoint();
		if (num_points > 0)
		{
			p = ruler->GetPoint(0);
			str += QString::asprintf("%.2f\t%.2f\t%.2f", p->x(), p->y(), -p->z());
		}
		if (num_points > 1)
		{
			p = ruler->GetPoint(num_points - 1);
			str += "\t";
			str += QString::asprintf("%.2f\t%.2f\t%.2f", p->x(), p->y(), -p->z());
		}
		else
			str += "\t\t\t";
		tos << str << "\t";
		if (ruler->GetTimeDep())
			str = QString::number(ruler->GetTime());
		else
			str = "N/A";
		tos << str << "\t";
		tos << S2Q(ruler->GetInfoValues()) << "\t";
		// appended after the wx columns: volumes of the first / last point
		tos << (num_points > 0 ? S2Q(ruler->GetPointVolName(0)) : QString()) << "\t";
		tos << (num_points > 1 ? S2Q(ruler->GetPointVolName(num_points - 1)) : QString()) << "\n";
	}

	// wx m_show_anno: the annotation rows counted by the last UpdateList
	if (RenderCanvas* view = m_tool->Canvas())
	{
		size_t cid = 1;
		for (int i = 0; i < view->GetLayerNum(); i++)
		{
			TreeLayer* layer = view->GetLayer(i);
			if (!layer || layer->IsA() != 4) continue;
			Annotations* ann = (Annotations*)layer;
			if (!ann->GetDisp()) continue;
			if (cid >= m_counts.size()) break;
			const QString aname = S2Q(ann->GetName());
			const int tnum = ann->GetTextNum();
			for (int j = 0; j < tnum && j < m_counts[cid]; j++)
			{
				const QString name = QString::fromStdString(ann->GetTextText(j)) + "  (" + aname + ")";
				Point p = ann->GetTextPos(j);
				int resx = 1, resy = 1, resz = 1;
				if (VolumeData* vd = ann->GetVolume())
					vd->GetResolution(resx, resy, resz);
				const QString points = QString::asprintf("%.2f\t%.2f\t%.2f", p.x() * resx, p.y() * resy, p.z() * resz);
				const QString info = QString::fromStdString(ann->GetTextInfo(j));
				const QStringList tok = info.split('\t', Qt::SkipEmptyParts);
				QString vx_or_score;
				if (!tok.isEmpty())
					vx_or_score = ann->GetMesh() ? tok.first() : tok.first() + " voxels";
				tos << name << "\t";
				tos << "N/A" << "\t";
				tos << QString::asprintf("%.2f", 0.0) << "\t";
				tos << QString::asprintf("%.1f", 0.0) << "\t";
				tos << points << "\t";
				tos << "\t\t" << "\t";
				tos << "N/A" << "\t";
				// + the (empty) start / end volume columns of the Qt header
				tos << vx_or_score << "\t\t\n";
			}
			cid++;
		}
	}
	tos.flush();
	f.close();
}

// ---- toolbar (MeasureDlg::OnNewLocator ... OnRulerEdit) ----------------------

void MeasureDialog::OnNewLocator(bool checked)
{
	if (!m_tool) return;

	if (m_ruler_mp_act->isChecked())
		m_tool->FinishRuler();

	QSignalBlocker b1(m_ruler_act), b2(m_ruler_mp_act), b3(m_ruler_edit_act);
	m_ruler_act->setChecked(false);
	m_ruler_mp_act->setChecked(false);
	m_ruler_edit_act->setChecked(false);

	QSignalBlocker bt(m_tool);
	if (checked)
	{
		m_tool->SetIntMode(5);
		m_tool->SetRulerType(2);
	}
	else
		m_tool->SetIntMode(1);
	bt.unblock();
	Q_EMIT m_tool->intModeChanged(m_tool->GetIntMode());
}

void MeasureDialog::OnNewRuler(bool checked)
{
	if (!m_tool) return;

	if (m_ruler_mp_act->isChecked())
		m_tool->FinishRuler();

	QSignalBlocker b1(m_locator_act), b2(m_ruler_mp_act), b3(m_ruler_edit_act);
	m_locator_act->setChecked(false);
	m_ruler_mp_act->setChecked(false);
	m_ruler_edit_act->setChecked(false);

	QSignalBlocker bt(m_tool);
	if (checked)
	{
		m_tool->SetIntMode(5);
		m_tool->SetRulerType(0);
	}
	else
		m_tool->SetIntMode(1);
	bt.unblock();
	Q_EMIT m_tool->intModeChanged(m_tool->GetIntMode());
}

void MeasureDialog::OnNewRulerMP(bool checked)
{
	if (!m_tool) return;

	QSignalBlocker b1(m_locator_act), b2(m_ruler_act), b3(m_ruler_edit_act);
	m_locator_act->setChecked(false);
	m_ruler_act->setChecked(false);
	m_ruler_edit_act->setChecked(false);

	QSignalBlocker bt(m_tool);
	if (checked)
	{
		m_tool->SetIntMode(5);
		m_tool->SetRulerType(1);
	}
	else
	{
		m_tool->SetIntMode(1);
		m_tool->FinishRuler();
	}
	bt.unblock();
	Q_EMIT m_tool->intModeChanged(m_tool->GetIntMode());
}

void MeasureDialog::OnRulerEdit(bool checked)
{
	if (!m_tool) return;

	if (m_ruler_mp_act->isChecked())
		m_tool->FinishRuler();

	QSignalBlocker b1(m_locator_act), b2(m_ruler_act), b3(m_ruler_mp_act);
	m_locator_act->setChecked(false);
	m_ruler_act->setChecked(false);
	m_ruler_mp_act->setChecked(false);

	QSignalBlocker bt(m_tool);
	if (checked)
		m_tool->SetIntMode(6);
	else
		m_tool->SetIntMode(1);
	bt.unblock();
	Q_EMIT m_tool->intModeChanged(m_tool->GetIntMode());
}

void MeasureDialog::OnDelete()
{
	DeleteSelection();
}

void MeasureDialog::OnDeleteAll()
{
	DeleteAll();
}

void MeasureDialog::OnImport()
{
	QString filename = QFileDialog::getOpenFileName(this, "Import rulers", QString(),
		"BigWarp landmarks (*.csv)");
	if (filename.isEmpty())
		return;
	QString suffix = QFileInfo(filename).suffix().toLower();
	if (suffix == "csv" && m_tool)
		m_tool->ImportBigWarpCSV(filename);
}

void MeasureDialog::OnExport()
{
	QString filter;
	QString filename = QFileDialog::getSaveFileName(this, "Export rulers", QString(),
		"Text file (*.txt);;BigWarp landmarks (*.csv)", &filter);
	if (filename.isEmpty())
		return;
	QString suffix = QFileInfo(filename).suffix().toLower();
	if (suffix.isEmpty())
	{
		// the wx file dialog appends the extension of the selected filter
		suffix = filter.contains("*.csv") ? "csv" : "txt";
		filename += "." + suffix;
	}
	if (suffix == "txt")
		Export(filename);
	else if (suffix == "csv")
	{
		if (m_tool)
			m_tool->ExportBigWarpCSV(filename);
	}
}

// ---- options ------------------------------------------------------------------

void MeasureDialog::OnIntensityMethodsCombo(int index)
{
	if (!m_tool) return;
	m_tool->SetPointVolumeMode(index);
	if (m_settings)
		m_settings->SetPointVolumeMode(index);
}

void MeasureDialog::OnUseTransferCheck(bool checked)
{
	if (!m_tool) return;
	m_tool->SetRulerUseTransf(checked);
	if (m_settings)
		m_settings->SetRulerUseTransf(checked);
}

void MeasureDialog::OnTransientCheck(bool checked)
{
	if (!m_tool) return;
	m_tool->SetRulerTimeDep(checked);
	if (m_settings)
		m_settings->SetRulerTimeDep(checked);
}

void MeasureDialog::OnAutoSelectCheck(bool checked)
{
	if (!m_tool) return;
	m_tool->SetRulerAutoSelect(checked);
	if (m_settings)
		m_settings->SetRulerAutoSelect(checked);
}

void MeasureDialog::OnScatterRulers()
{
	if (!m_tool) return;
	bool ok = false;
	long ival = m_density_txt->text().toLong(&ok);
	if (!ok) ival = 0;
	m_tool->ScatterRulers(ival);
}

// VRenderVulkanView::WarpCurrentVolumeInternal (the result volume is added
// with override-vox off, like the wx view)
void MeasureDialog::OnWarp()
{
	if (!m_tool) return;
	QString err;
	const bool cells = GetWarpTransformType() == WarpCells;
	rigidcells::Report report;
	VolumeData* vd = cells ?
		m_tool->WarpCurrentVolumeCells(GetCellParams(), &report, &err) :
		m_tool->WarpCurrentVolume(GetWarpTransformType(), GetWarpLambda(), GetWarpInterpolation(), &err);
	if (!vd)
	{
		if (!err.isEmpty())
			QMessageBox::information(this, "Warp", err);
		return;
	}
	if (m_ctx)
	{
		DataManager* mgr = m_ctx->GetDataManager();
		bool btmp = mgr ? mgr->GetOverrideVox() : false;
		if (mgr) mgr->SetOverrideVox(false);
		if (mgr) mgr->AddVolumeData(vd);
		m_ctx->AddVolume(vd);
		if (mgr) mgr->SetOverrideVox(btmp);
		m_ctx->RefreshView();
	}
	if (cells)
	{
		if (!m_cell_report)
			m_cell_report = new RigidCellsReportDialog(this);
		m_cell_report->SetReport(report, QString::fromStdWString(vd->GetName().ToStdWstring()));
		m_cell_report->show();
		m_cell_report->raise();
	}
}

void MeasureDialog::OnWarpStiffness(int v)
{
	m_warp_lambda = (double)v / 1000.0;
	m_warp_stiffness_text->setText(QString::asprintf("%.3f", m_warp_lambda));
}

void MeasureDialog::OnWarpType(int)
{
	//stiffness (lambda) only applies to the Thin Plate Spline models (the
	//cell transform fits the same TPS first); the cell transform always
	//renders nearest neighbour (label values are kept)
	const int t = GetWarpTransformType();
	bool is_tps = (t == 0 || t == WarpCells);
	m_warp_stiffness_sldr->setEnabled(is_tps);
	m_warp_stiffness_text->setEnabled(is_tps);
	m_warp_interp_combo->setEnabled(t != WarpCells);
	if (m_cell_panel)
		m_cell_panel->setVisible(t == WarpCells);
}

// ---- "Cells: shape preserving" ------------------------------------------------

void MeasureDialog::BuildCellPanel()
{
	m_cell_panel = new QWidget(this);
	m_cell_panel->setObjectName("cell_panel");
	auto* g = new QGridLayout(m_cell_panel);
	g->setContentsMargins(10, 0, 10, 10);
	g->setHorizontalSpacing(8);
	g->setVerticalSpacing(4);
	auto label = [&](const QString& text, int row, int col, const QString& tip) {
		auto* l = new QLabel(text, m_cell_panel);
		l->setToolTip(tip);
		g->addWidget(l, row, col, Qt::AlignRight | Qt::AlignVCenter);
		return l;
	};
	auto dspin = [&](const char* name, double lo, double hi, int dec, double step) {
		auto* b = new QDoubleSpinBox(m_cell_panel);
		b->setObjectName(name);
		b->setRange(lo, hi); b->setDecimals(dec); b->setSingleStep(step);
		b->setMinimumWidth(80);
		connect(b, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeasureDialog::OnCellOptionChanged);
		return b;
	};
	auto ispin = [&](const char* name, int lo, int hi) {
		auto* b = new QSpinBox(m_cell_panel);
		b->setObjectName(name);
		b->setRange(lo, hi);
		b->setMinimumWidth(80);
		connect(b, qOverload<int>(&QSpinBox::valueChanged), this, &MeasureDialog::OnCellOptionChanged);
		return b;
	};

	m_cell_local_combo = new QComboBox(m_cell_panel);
	m_cell_local_combo->setObjectName("cell_local");
	for (int i = 0; i < 4; i++) m_cell_local_combo->addItem(rigidcells::LocalModeName(i));
	m_cell_objects_combo = new QComboBox(m_cell_panel);
	m_cell_objects_combo->setObjectName("cell_objects");
	for (int i = 0; i < 2; i++) m_cell_objects_combo->addItem(rigidcells::ObjectModeName(i));
	m_cell_beta = dspin("cell_beta", 0.01, 1000.0, 2, 0.5);
	m_cell_sigma_min = dspin("cell_sigma_min", 0.001, 1000.0, 3, 0.05);
	m_cell_neurite = dspin("cell_neurite", 0.0, 1000.0, 3, 0.1);
	m_cell_samples = ispin("cell_samples", 10, 100000000);
	m_cell_min_voxels = ispin("cell_min_voxels", 1, 2147483647);
	m_cell_radius = dspin("cell_radius", 0.0, 100000.0, 3, 0.5);
	m_cell_device_combo = new QComboBox(m_cell_panel);
	m_cell_device_combo->setObjectName("cell_device");
	m_cell_device_combo->addItem("CPU (all cores)");
	for (const std::string& n : rigidcells::GpuDeviceNames())
		m_cell_device_combo->addItem(QString("GPU: ") + QString::fromStdString(n));
	m_cell_loo_chk = new QCheckBox("Landmark diagnostics (leave-one-out)", m_cell_panel);
	m_cell_loo_chk->setObjectName("cell_loo");

	const QString tipLocal = "Translation / Rigid / Similarity move each whole object with ONE transform fitted to the TPS "
		"over its voxels. Adaptive: the TPS displacement is averaged over each object's own voxels with a flat-top kernel "
		"whose radius is 'rigidity factor' x the local thickness, so somata move as one rigid block while structures "
		"thinner than the neurite radius (fibers) follow the TPS.";
	int row = 0;
	label("Local transform:", row, 0, tipLocal);
	g->addWidget(m_cell_local_combo, row, 1, 1, 3);
	m_cell_local_combo->setToolTip(tipLocal);
	row++;
	label("Objects:", row, 0, "26-connected components per label value, or one object per label value");
	g->addWidget(m_cell_objects_combo, row, 1, 1, 3);
	row++;
	label("Rigidity factor:", row, 0, "Adaptive: smoothing = factor x local thickness (radius); larger = more rigid somata");
	g->addWidget(m_cell_beta, row, 1);
	label("Min. smoothing:", row, 2, "Adaptive: minimum kernel radius (physical units); smaller = fibers follow the TPS closer");
	g->addWidget(m_cell_sigma_min, row, 3);
	row++;
	label("Neurite radius:", row, 0, "Adaptive: structures thinner than this radius (physical units) follow the TPS");
	g->addWidget(m_cell_neurite, row, 1);
	label("TPS samples / object:", row, 2, "TPS samples per object for the fits and statistics");
	g->addWidget(m_cell_samples, row, 3);
	row++;
	label("Min. object voxels:", row, 0, "Objects with fewer voxels are dropped");
	g->addWidget(m_cell_min_voxels, row, 1);
	label("Landmark search radius:", row, 2, "Landmarks within this distance (physical units) of an object are checked against its transform");
	g->addWidget(m_cell_radius, row, 3);
	row++;
	label("Compute device:", row, 0, "Adaptive field on the CPU (all cores) or an OpenCL GPU");
	g->addWidget(m_cell_device_combo, row, 1, 1, 3);
	row++;
	g->addWidget(m_cell_loo_chk, row, 1, 1, 3);
	g->setColumnStretch(4, 1);

	//settings (SettingsDialog /rigid cells)
	rigidcells::Params p;
	if (m_settings)
		p = m_settings->GetCellWarpParams();
	m_cell_local_combo->setCurrentIndex(std::min(3, std::max(0, p.local_mode)));
	m_cell_objects_combo->setCurrentIndex(p.per_label ? 1 : 0);
	m_cell_beta->setValue(p.beta);
	m_cell_sigma_min->setValue(p.sigma_min);
	m_cell_neurite->setValue(p.neurite);
	m_cell_samples->setValue(p.samples);
	m_cell_min_voxels->setValue(p.min_voxels);
	m_cell_radius->setValue(p.radius);
	//the plugin's default device is the first GPU when there is one
	int dev = p.device < 0 ? (m_cell_device_combo->count() > 1 ? 1 : 0) : p.device;
	m_cell_device_combo->setCurrentIndex(dev < m_cell_device_combo->count() ? dev : 0);
	m_cell_loo_chk->setChecked(p.loo);
	connect(m_cell_local_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MeasureDialog::OnCellOptionChanged);
	connect(m_cell_objects_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MeasureDialog::OnCellOptionChanged);
	connect(m_cell_device_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MeasureDialog::OnCellOptionChanged);
	connect(m_cell_loo_chk, &QCheckBox::toggled, this, &MeasureDialog::OnCellOptionChanged);
	UpdateCellEnables();
	m_cell_panel->setVisible(false);
}

void MeasureDialog::UpdateCellEnables()
{
	const bool adaptive = m_cell_local_combo->currentIndex() == rigidcells::ADAPTIVE;
	m_cell_beta->setEnabled(adaptive);
	m_cell_sigma_min->setEnabled(adaptive);
	m_cell_neurite->setEnabled(adaptive);
	m_cell_device_combo->setEnabled(adaptive);
}

rigidcells::Params MeasureDialog::GetCellParams() const
{
	rigidcells::Params p;
	p.local_mode = m_cell_local_combo->currentIndex();
	p.per_label = m_cell_objects_combo->currentIndex() == 1;
	p.stiffness = m_warp_lambda;
	p.beta = m_cell_beta->value();
	p.sigma_min = m_cell_sigma_min->value();
	p.neurite = m_cell_neurite->value();
	p.samples = m_cell_samples->value();
	p.min_voxels = m_cell_min_voxels->value();
	p.radius = m_cell_radius->value();
	p.device = m_cell_device_combo->currentIndex();
	p.loo = m_cell_loo_chk->isChecked();
	return p;
}

void MeasureDialog::OnCellOptionChanged()
{
	UpdateCellEnables();
	if (m_settings)
		m_settings->SetCellWarpParams(GetCellParams());
}
